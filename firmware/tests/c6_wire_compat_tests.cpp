// Compile against the actual production C6 component, not a copied encoder.
#include "fanlink_protocol.h"
#include "fanlink_transport.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "fanlink/serial_protocol.hpp"
#include "fanlink/protocol.hpp"

namespace c6 = esphome::fanlink::wire;

namespace {
std::size_t checks = 0;
void check(bool condition, const char* description) {
  ++checks;
  if (!condition) {
    std::cerr << "FAIL: " << description << '\n';
    std::exit(1);
  }
}

void testCommands() {
  fanlink::SerialFrameParser txParser;
  const std::array<std::uint16_t, 4> sequences{65534U, 65535U, 0U, 1U};
  for (const auto sequence : sequences) {
    for (const float percent : {0.0f, 20.0f, 40.0f, 60.0f, 80.0f, 100.0f}) {
      std::array<std::uint8_t, fanlink::kNodeCount> duty{};
      duty.fill(c6::percent_to_raw(percent));
      std::array<std::uint8_t, c6::MAX_FRAME_SIZE> encoded{};
      const auto length = c6::encode_frame(c6::TYPE_SET_ALL_DUTY, duty.data(), duty.size(),
                                           sequence, encoded.data(), encoded.size());
      check(length == 18U && encoded[2] == 2U, "C6 eight-slot v2 command is 18 bytes");
      fanlink::SerialFrame received{};
      std::size_t frames = 0;
      for (std::size_t i = 0; i < length; ++i) {
        frames += txParser.feed(encoded[i], received) ? 1U : 0U;
      }
      check(frames == 1U && received.type == fanlink::serial_type::kSetAllDuty &&
                received.sequence == sequence && received.payloadLength == duty.size() &&
                std::equal(duty.begin(), duty.end(), received.payload.begin()),
            "TX accepts actual C6 encoder including sequence wrap");
      std::array<std::uint8_t, fanlink::kSerialMaxFrameSize> roundTrip{};
      std::size_t roundTripLength = 0;
      check(fanlink::encodeSerialFrame(received, roundTrip, roundTripLength) &&
                roundTripLength == length &&
                std::equal(encoded.begin(), encoded.begin() + length, roundTrip.begin()),
            "C6 and TX encoders agree byte-for-byte");
    }
  }
  check(txParser.rejectedFrameCount() == 0U, "no C6 command rejected");
}

void testTelemetry() {
  c6::StreamParser c6Parser;
  const std::array<std::uint16_t, 4> sequences{65534U, 65535U, 0U, 1U};
  for (const auto sequence : sequences) {
    for (const auto type : {fanlink::serial_type::kStatusSnapshot,
                           fanlink::serial_type::kLinkMetricsSnapshot,
                           fanlink::serial_type::kErrorEvent}) {
      fanlink::SerialFrame sent{};
      sent.type = type;
      sent.sequence = sequence;
      sent.payloadLength = type == fanlink::serial_type::kStatusSnapshot ? 54U :
                           type == fanlink::serial_type::kLinkMetricsSnapshot ? 16U : 128U;
      // Includes zero, magic bytes, negative RSSI bytes and every boundary value.
      for (std::size_t i = 0; i < sent.payloadLength; ++i) {
        sent.payload[i] = static_cast<std::uint8_t>(i * 17U + sequence);
      }
      std::array<std::uint8_t, fanlink::kSerialMaxFrameSize> encoded{};
      std::size_t length = 0;
      check(fanlink::encodeSerialFrame(sent, encoded, length), "TX telemetry encodes");
      c6::Frame received{};
      std::size_t frames = 0;
      for (std::size_t i = 0; i < length; ++i) {
        const auto result = c6Parser.push(encoded[i], received);
        check(result == c6::ParseResult::NONE || result == c6::ParseResult::FRAME,
              "C6 parser accepts telemetry stream");
        frames += result == c6::ParseResult::FRAME ? 1U : 0U;
      }
      check(frames == 1U && received.type == sent.type && received.sequence == sequence &&
                received.payload_length == sent.payloadLength &&
                std::equal(sent.payload.begin(), sent.payload.begin() + sent.payloadLength,
                           received.payload.begin()),
            "C6 receives compact STATUS54/METRICS16 and unchanged ERROR128");
      encoded[length - 1U] ^= 0x80U;
      c6::ParseResult last = c6::ParseResult::NONE;
      for (std::size_t i = 0; i < length; ++i) {
        last = c6Parser.push(encoded[i], received);
      }
      check(last == c6::ParseResult::BAD_CRC, "C6 rejects corrupted TX telemetry");
      c6Parser.reset();
    }
  }
}

void testVersionIsolation() {
  // Every Host type uses the new outer version, including OTA/config/control.
  // The 30-byte inner maintenance payload is opaque to these stream parsers.
  const std::array<std::uint8_t, 16> types{
      0x01, 0x02, 0x03, 0x10, 0x11, 0x12, 0x13, 0x14,
      0x15, 0x80, 0x81, 0x82, 0x90, 0x91, 0x92, 0x93};
  for (const auto type : types) {
    const std::size_t payloadSize = type == 0x01 ? 8U : type == 0x02 ? 2U :
                                    type == 0x03 ? 0U : type == 0x80 ? 54U :
                                    type == 0x82 ? 16U : 30U;
    std::array<std::uint8_t, c6::MAX_PAYLOAD_SIZE> payload{};
    payload.fill(0xA5);
    std::array<std::uint8_t, c6::MAX_FRAME_SIZE> encoded{};
    const auto length = c6::encode_frame(type, payload.data(), payloadSize, 123U,
                                         encoded.data(), encoded.size());
    check(length == c6::HEADER_SIZE + payloadSize + c6::CRC_SIZE && encoded[2] == 2U,
          "all Host frame types use outer v2");
    auto legacy = encoded;
    legacy[2] = 1U;
    c6::write_le16(legacy.data() + length - 2U,
                  c6::crc16_ccitt_false(legacy.data(), length - 2U));
    check(c6::read_le16(legacy.data() + length - 2U) ==
              c6::crc16_ccitt_false(legacy.data(), length - 2U),
          "legacy rejection fixture has a valid CRC");
    fanlink::SerialFrameParser txParser;
    fanlink::SerialFrame txFrame{};
    txFrame.type = 0xEE;
    c6::StreamParser c6Parser;
    c6::Frame c6Frame{};
    c6Frame.type = 0xEE;
    std::size_t txFrames = 0, c6Frames = 0, formatErrors = 0;
    for (std::size_t i = 0; i < length; ++i) {
      txFrames += txParser.feed(legacy[i], txFrame) ? 1U : 0U;
      const auto result = c6Parser.push(legacy[i], c6Frame);
      c6Frames += result == c6::ParseResult::FRAME ? 1U : 0U;
      formatErrors += result == c6::ParseResult::BAD_FORMAT ? 1U : 0U;
    }
    check(txFrames == 0U && txParser.rejectedFrameCount() > 0U && txFrame.type == 0xEE,
          "TX rejects valid-CRC v1 without delivering a frame");
    check(c6Frames == 0U && formatErrors > 0U && c6Frame.type == 0xEE,
          "C6 rejects valid-CRC v1 without delivering a frame");
    for (std::size_t i = 0; i < length; ++i) {
      txFrames += txParser.feed(encoded[i], txFrame) ? 1U : 0U;
      c6Frames += c6Parser.push(encoded[i], c6Frame) == c6::ParseResult::FRAME ? 1U : 0U;
    }
    check(txFrames == 1U && c6Frames == 1U && txFrame.type == type &&
              c6Frame.type == type && txFrame.payloadLength == payloadSize &&
              c6Frame.payload_length == payloadSize,
          "both parsers resynchronize to v2 after rejecting v1");
  }
}

void testNodeBoundary() {
  namespace transport = esphome::fanlink::transport;
  static_assert(transport::DUTY_CHANNEL_COUNT == fanlink::kNodeCount);
  transport::SafetyState safety;
  check(fanlink::isValidNodeId(8U) && !fanlink::isValidNodeId(9U) &&
            !fanlink::isValidNodeId(16U) && !fanlink::isValidNodeId(0U),
        "firmware node eight is valid and nine/sixteen/zero are invalid");
  // C6 indices are zero based. Full speed is accepted even while recovering.
  check(safety.set_requested_duty(7U, 255U), "production C6 accepts channel eight");
  check(!safety.set_requested_duty(8U, 255U) &&
            !safety.set_requested_duty(15U, 255U),
        "production C6 rejects channel nine and sixteen even at full speed");
  check(safety.command_duty(7U) == 255U, "boundary checks preserve recovery full speed");
}
}  // namespace

int main() {
  static_assert(c6::VERSION == fanlink::kSerialProtocolVersion);
  static_assert(c6::VERSION == 2U);
  static_assert(fanlink::kNodeCount == 8U);
  static_assert(fanlink::kLinkMetricsPayloadSize == 16U);
  static_assert(fanlink::kProtocolVersion == 1U && fanlink::kAirPacketSize == 30U);
  static_assert(c6::MAX_PAYLOAD_SIZE == fanlink::kSerialMaxPayload);
  static_assert(c6::TYPE_SET_ALL_DUTY == fanlink::serial_type::kSetAllDuty);
  static_assert(c6::TYPE_STATUS_SNAPSHOT == fanlink::serial_type::kStatusSnapshot);
  static_assert(c6::TYPE_LINK_METRICS_SNAPSHOT == fanlink::serial_type::kLinkMetricsSnapshot);
  static_assert(c6::TYPE_ERROR_EVENT == fanlink::serial_type::kErrorEvent);
  testCommands();
  testTelemetry();
  testVersionIsolation();
  testNodeBoundary();
  std::cout << "PASS: C6/TX wire compatibility, " << checks << " checks\n";
}
