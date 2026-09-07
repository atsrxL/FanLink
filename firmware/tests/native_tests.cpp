#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "fanlink/crc16.hpp"
#include "fanlink/interfaces.hpp"
#include "fanlink/link_led_pattern.hpp"
#include "fanlink/protocol.hpp"
#include "fanlink/rx_controller.hpp"
#include "fanlink/serial_protocol.hpp"
#include "fanlink/tx_controller.hpp"
#include "fanlink/tx_diagnostic_text.hpp"

namespace {

int gFailures = 0;
int gChecks = 0;

#define CHECK(condition)                                                                  \
  do {                                                                                    \
    ++gChecks;                                                                            \
    if (!(condition)) {                                                                   \
      ++gFailures;                                                                         \
      std::cerr << "CHECK failed at " << __FILE__ << ':' << __LINE__ << ": " #condition \
                << '\n';                                                                  \
    }                                                                                     \
  } while (false)

class FakeClock final : public fanlink::IClock {
 public:
  std::uint32_t millis() const override { return static_cast<std::uint32_t>(nowUs_ / 1000U); }
  std::uint32_t micros() const override { return static_cast<std::uint32_t>(nowUs_); }
  void delayMilliseconds(std::uint32_t milliseconds) override {
    nowUs_ += static_cast<std::uint64_t>(milliseconds) * 1000U;
  }
  void delayMicroseconds(std::uint32_t microseconds) override { nowUs_ += microseconds; }
  void advanceMs(std::uint32_t milliseconds) { delayMilliseconds(milliseconds); }
  void advanceUs(std::uint32_t microseconds) { delayMicroseconds(microseconds); }

 private:
  std::uint64_t nowUs_{0};
};

void testLinkLedPattern() {
  using fanlink::link_led::shouldBeOn;

  CHECK(shouldBeOn(true, 0U));
  CHECK(shouldBeOn(true, 1000000U));

  CHECK(shouldBeOn(false, 0U));
  CHECK(shouldBeOn(false, 99U));
  CHECK(!shouldBeOn(false, 100U));
  CHECK(!shouldBeOn(false, 199U));
  CHECK(shouldBeOn(false, 200U));
  CHECK(shouldBeOn(false, 299U));
  CHECK(!shouldBeOn(false, 300U));
  CHECK(!shouldBeOn(false, 799U));
  CHECK(shouldBeOn(false, 800U));
  CHECK(shouldBeOn(false, 0xFFFFFFFFU - 95U));
}

struct QueuedReceive {
  fanlink::RadioResult result{fanlink::RadioResult::Timeout};
  std::vector<std::uint8_t> data{};
  std::int8_t rssi{-70};
  int driverError{0};
  std::uint32_t delayMs{0};
};

class FakeRadio final : public fanlink::IRadioPort {
 public:
  explicit FakeRadio(FakeClock* clock = nullptr) : clock_(clock) {}

  bool begin(std::uint32_t networkId) override {
    begunNetworkId = networkId;
    began = beginSucceeds;
    lastDriverError = began ? 0 : beginDriverError;
    return began;
  }

  fanlink::RadioResult transmit(const std::uint8_t* data, std::size_t length) override {
    transmissions.emplace_back(data, data + length);
    lastDriverError = transmitDriverError;
    return transmitResult;
  }

  fanlink::RadioRxResult receive(std::uint8_t* data, std::size_t capacity,
                                 std::uint32_t timeoutMs) override {
    lastTimeoutMs = timeoutMs;
    receiveTimeouts.push_back(timeoutMs);
    if (receives.empty()) {
      lastDriverError = 0;
      return {fanlink::RadioResult::Timeout, 0U, -127, lastDriverError};
    }
    auto next = receives.front();
    receives.pop_front();
    lastDriverError = next.driverError;
    if (clock_ != nullptr) {
      clock_->advanceMs(next.delayMs);
    }
    if (next.result != fanlink::RadioResult::Ok) {
      return {next.result, 0U, next.rssi, next.driverError};
    }
    const auto copyLength = std::min(capacity, next.data.size());
    std::copy_n(next.data.begin(), copyLength, data);
    return {fanlink::RadioResult::Ok, next.data.size(), next.rssi, next.driverError};
  }

  int lastError() const override { return lastDriverError; }

  void queuePacket(const std::vector<std::uint8_t>& data, std::int8_t rssi = -70,
                   std::uint32_t delayMs = 0U) {
    receives.push_back({fanlink::RadioResult::Ok, data, rssi, 0, delayMs});
  }

  void queueTimeout(std::uint32_t delayMs = 0U) {
    receives.push_back({fanlink::RadioResult::Timeout, {}, -127, 0, delayMs});
  }

  void queueFailure(fanlink::RadioResult result, int driverError,
                    std::uint32_t delayMs = 0U) {
    receives.push_back({result, {}, -127, driverError, delayMs});
  }

  bool beginSucceeds{true};
  bool began{false};
  std::uint32_t begunNetworkId{0};
  fanlink::RadioResult transmitResult{fanlink::RadioResult::Ok};
  int beginDriverError{0};
  int transmitDriverError{0};
  int lastDriverError{0};
  std::uint32_t lastTimeoutMs{0};
  std::vector<std::uint32_t> receiveTimeouts{};
  std::deque<QueuedReceive> receives{};
  std::vector<std::vector<std::uint8_t>> transmissions{};

 private:
  FakeClock* clock_{nullptr};
};

class FakeFan final : public fanlink::IFanOutput {
 public:
  bool begin() override {
    began = true;
    return beginSucceeds;
  }
  void setDuty(std::uint8_t duty) override {
    applied = duty;
    history.push_back(duty);
  }
  void setFullSpeed() override { setDuty(255U); }
  std::uint8_t appliedDuty() const override { return applied; }

  bool beginSucceeds{true};
  bool began{false};
  std::uint8_t applied{0};
  std::vector<std::uint8_t> history{};
};

class FakeTach final : public fanlink::ITachometer {
 public:
  bool begin() override {
    began = true;
    return beginSucceeds;
  }
  fanlink::TachSample sample(std::uint32_t nowUs) override {
    lastSampleUs = nowUs;
    return current;
  }

  bool beginSucceeds{true};
  bool began{false};
  fanlink::TachSample current{};
  std::uint32_t lastSampleUs{0};
};

std::vector<std::uint8_t> makeControl(std::uint32_t networkId, std::uint16_t sequence,
                                      std::uint8_t pollId,
                                      const std::array<std::uint8_t, 16>& duty,
                                      std::uint8_t flags = 0U) {
  fanlink::ControlPacket packet{};
  packet.networkId = networkId;
  packet.sequence = sequence;
  packet.pollId = pollId;
  packet.flags = flags;
  packet.duty = duty;
  std::array<std::uint8_t, fanlink::kControlPacketSize> encoded{};
  CHECK(fanlink::encodeControlPacket(packet, encoded));
  return {encoded.begin(), encoded.end()};
}

std::vector<std::uint8_t> makeRpm(std::uint32_t networkId, std::uint16_t sequence,
                                  std::uint8_t nodeId, std::uint16_t rpm,
                                  std::uint8_t duty = 128U,
                                  std::uint8_t status = 0U) {
  fanlink::RpmPacket packet{};
  packet.networkId = networkId;
  packet.sequenceEcho = sequence;
  packet.nodeId = nodeId;
  packet.status = status;
  packet.rpm = rpm;
  packet.appliedDuty = duty;
  packet.missedDownlinks = 2U;
  packet.downlinkRssiDbm = -61;
  std::array<std::uint8_t, fanlink::kRpmPacketSize> encoded{};
  CHECK(fanlink::encodeRpmPacket(packet, encoded));
  return {encoded.begin(), encoded.end()};
}

void testCrc() {
  const std::string input = "123456789";
  CHECK(fanlink::crc16CcittFalse(
            reinterpret_cast<const std::uint8_t*>(input.data()), input.size()) == 0x29B1U);
}

void testControlPacketRoundTrip() {
  fanlink::ControlPacket source{};
  source.networkId = 0x12345678UL;
  source.sequence = 0xABCDU;
  source.pollId = 16U;
  source.flags = fanlink::control_flag::kHostFailsafe;
  for (std::size_t i = 0; i < source.duty.size(); ++i) {
    source.duty[i] = static_cast<std::uint8_t>(i * 13U);
  }

  std::array<std::uint8_t, fanlink::kControlPacketSize> encoded{};
  CHECK(fanlink::encodeControlPacket(source, encoded));
  CHECK(encoded.size() == 30U);

  fanlink::ControlPacket decoded{};
  CHECK(fanlink::decodeControlPacket(encoded.data(), encoded.size(), decoded));
  CHECK(decoded.networkId == source.networkId);
  CHECK(decoded.sequence == source.sequence);
  CHECK(decoded.pollId == source.pollId);
  CHECK(decoded.flags == source.flags);
  CHECK(decoded.duty == source.duty);

  encoded[19] ^= 0x40U;
  CHECK(!fanlink::decodeControlPacket(encoded.data(), encoded.size(), decoded));
}

void testRpmPacketRoundTrip() {
  fanlink::RpmPacket source{};
  source.networkId = 0xCAFEBABEU;
  source.sequenceEcho = 99U;
  source.nodeId = 7U;
  source.status = fanlink::node_status::kSequenceGap;
  source.rpm = 4321U;
  source.appliedDuty = 201U;
  source.missedDownlinks = 4U;
  source.downlinkRssiDbm = -77;

  std::array<std::uint8_t, fanlink::kRpmPacketSize> encoded{};
  CHECK(fanlink::encodeRpmPacket(source, encoded));
  CHECK(encoded.size() == fanlink::kAirPacketSize);
  CHECK(encoded.size() == 30U);
  fanlink::RpmPacket decoded{};
  CHECK(fanlink::decodeRpmPacket(encoded.data(), encoded.size(), decoded));
  CHECK(decoded.networkId == source.networkId);
  CHECK(decoded.sequenceEcho == source.sequenceEcho);
  CHECK(decoded.nodeId == source.nodeId);
  CHECK(decoded.status == source.status);
  CHECK(decoded.rpm == source.rpm);
  CHECK(decoded.appliedDuty == source.appliedDuty);
  CHECK(decoded.missedDownlinks == source.missedDownlinks);
  CHECK(decoded.downlinkRssiDbm == source.downlinkRssiDbm);
}

void testSerialParserRoundTripAndResync() {
  fanlink::SerialFrame source{};
  source.type = fanlink::serial_type::kSetAllDuty;
  source.sequence = 41U;
  source.payloadLength = 16U;
  for (std::uint8_t i = 0; i < 16U; ++i) {
    source.payload[i] = static_cast<std::uint8_t>(i + 10U);
  }

  std::array<std::uint8_t, fanlink::kSerialMaxFrameSize> encoded{};
  std::size_t length = 0U;
  CHECK(fanlink::encodeSerialFrame(source, encoded, length));
  CHECK(length == 26U);

  fanlink::SerialFrameParser parser;
  fanlink::SerialFrame result{};
  CHECK(!parser.feed(0x00U, result));
  CHECK(!parser.feed(0x46U, result));
  CHECK(!parser.feed(0x01U, result));

  bool completed = false;
  for (std::size_t i = 0; i < length; ++i) {
    completed = parser.feed(encoded[i], result) || completed;
  }
  CHECK(completed);
  CHECK(result.type == source.type);
  CHECK(result.sequence == source.sequence);
  CHECK(result.payloadLength == source.payloadLength);
  CHECK(std::equal(source.payload.begin(), source.payload.begin() + source.payloadLength,
                   result.payload.begin()));

  auto corrupted = encoded;
  corrupted[9] ^= 0x80U;
  completed = false;
  for (std::size_t i = 0; i < length; ++i) {
    completed = parser.feed(corrupted[i], result) || completed;
  }
  CHECK(!completed);
  CHECK(parser.rejectedFrameCount() >= 1U);

  for (std::size_t i = 0; i < length; ++i) {
    completed = parser.feed(encoded[i], result) || completed;
  }
  CHECK(completed);
}

void testLinkMetricsSerialFrame() {
  CHECK(fanlink::kLinkMetricsPayloadSize == 32U);

  fanlink::SerialFrame source{};
  source.type = fanlink::serial_type::kLinkMetricsSnapshot;
  source.sequence = 73U;
  source.payloadLength =
      static_cast<std::uint16_t>(fanlink::kLinkMetricsPayloadSize);
  source.payload.fill(static_cast<std::uint8_t>(-127));
  source.payload[0] = static_cast<std::uint8_t>(-48);
  source.payload[1] = static_cast<std::uint8_t>(-61);

  std::array<std::uint8_t, fanlink::kSerialMaxFrameSize> encoded{};
  std::size_t length = 0U;
  CHECK(fanlink::encodeSerialFrame(source, encoded, length));

  fanlink::SerialFrameParser parser;
  fanlink::SerialFrame decoded{};
  bool completed = false;
  for (std::size_t i = 0; i < length; ++i) {
    completed = parser.feed(encoded[i], decoded) || completed;
  }
  CHECK(completed);
  CHECK(decoded.type == fanlink::serial_type::kLinkMetricsSnapshot);
  CHECK(decoded.payloadLength == fanlink::kLinkMetricsPayloadSize);
  CHECK(static_cast<std::int8_t>(decoded.payload[0]) == -48);
  CHECK(static_cast<std::int8_t>(decoded.payload[1]) == -61);
  CHECK(static_cast<std::int8_t>(decoded.payload[2]) == -127);
}

void testTxPollCycleAndHostFailsafe() {
  constexpr std::uint32_t networkId = 0x11223344UL;
  FakeClock clock;
  FakeRadio radio;
  fanlink::TxController tx(
      radio, clock, fanlink::TxControllerConfig{networkId, 500U, 10000U, 5U});
  CHECK(tx.begin());
  CHECK(radio.begunNetworkId == networkId);

  // No host command yet: first transmitted packet must force full speed.
  tx.runOneSlot();
  CHECK(radio.transmissions.size() == 1U);
  fanlink::ControlPacket packet{};
  CHECK(fanlink::decodeControlPacket(radio.transmissions[0].data(),
                                     radio.transmissions[0].size(), packet));
  CHECK(packet.pollId == 1U);
  CHECK((packet.flags & fanlink::control_flag::kForceFullSpeed) != 0U);
  CHECK(std::all_of(packet.duty.begin(), packet.duty.end(),
                    [](std::uint8_t value) { return value == 255U; }));

  std::array<std::uint8_t, 16> desired{};
  for (std::size_t i = 0; i < desired.size(); ++i) {
    desired[i] = static_cast<std::uint8_t>(20U + i);
  }
  tx.setAllDuty(desired);
  tx.runOneSlot();
  CHECK(fanlink::decodeControlPacket(radio.transmissions[1].data(),
                                     radio.transmissions[1].size(), packet));
  CHECK(packet.pollId == 2U);
  CHECK(packet.flags == 0U);
  CHECK(packet.duty == desired);

  // Complete one full poll cycle and verify strict 1..16 ordering.
  for (int i = 0; i < 14; ++i) {
    tx.runOneSlot();
  }
  CHECK(radio.transmissions.size() == 16U);
  for (std::size_t i = 0; i < 16U; ++i) {
    CHECK(fanlink::decodeControlPacket(radio.transmissions[i].data(),
                                       radio.transmissions[i].size(), packet));
    CHECK(packet.pollId == static_cast<std::uint8_t>(i + 1U));
  }
  CHECK(tx.nextPollId() == 1U);

  clock.advanceMs(501U);
  tx.runOneSlot();
  CHECK(fanlink::decodeControlPacket(radio.transmissions.back().data(),
                                     radio.transmissions.back().size(), packet));
  CHECK((packet.flags & fanlink::control_flag::kHostFailsafe) != 0U);
  CHECK(tx.hostFailsafeActive());
}

void testTxAcceptsOnlyMatchingReply() {
  constexpr std::uint32_t networkId = 0x01020304UL;
  FakeClock clock;
  FakeRadio radio;
  fanlink::TxController tx(
      radio, clock, fanlink::TxControllerConfig{networkId, 500U, 10000U, 5U});
  CHECK(tx.begin());
  tx.noteHostHeartbeat();

  radio.queuePacket(makeRpm(networkId, 0U, 1U, 3456U, 150U), -48);
  tx.runOneSlot();
  const auto& node1 = tx.nodeTelemetry(1U);
  CHECK(node1.seen);
  CHECK(node1.rpm == 3456U);
  CHECK(node1.appliedDuty == 150U);
  CHECK(node1.uplinkRssiDbm == -48);
  CHECK(node1.downlinkRssiDbm == -61);
  CHECK(node1.missedPolls == 0U);

  // Slot 2 expects node 2/sequence 1; wrong node must be ignored.
  radio.queuePacket(makeRpm(networkId, 1U, 1U, 9999U), -40);
  tx.runOneSlot();
  CHECK(!tx.nodeTelemetry(2U).seen);
  CHECK(tx.nodeTelemetry(2U).missedPolls == 1U);
  CHECK(tx.nodeTelemetry(1U).rpm == 3456U);
}

void testTxRadioDiagnostics() {
  constexpr std::uint32_t networkId = 0x0BADCAFEUL;

  FakeClock failedClock;
  FakeRadio failedRadio;
  failedRadio.beginSucceeds = false;
  failedRadio.beginDriverError = -705;
  fanlink::TxController failedTx(
      failedRadio, failedClock,
      fanlink::TxControllerConfig{networkId, 500U, 10000U, 5U});
  CHECK(!failedTx.begin());
  CHECK(!failedTx.radioDiagnostics().beginOk);
  CHECK(failedTx.radioDiagnostics().beginDriverError == -705);
  CHECK(failedTx.radioDiagnostics().lastFault == fanlink::TxRadioFault::Begin);

  FakeClock clock;
  FakeRadio radio;
  fanlink::TxController tx(
      radio, clock, fanlink::TxControllerConfig{networkId, 500U, 10000U, 5U});
  CHECK(tx.begin());
  CHECK(tx.radioDiagnostics().beginOk);

  radio.transmitResult = fanlink::RadioResult::IoError;
  radio.transmitDriverError = -5;
  tx.runOneSlot();
  CHECK(tx.radioDiagnostics().lastFault == fanlink::TxRadioFault::Transmit);
  CHECK(tx.radioDiagnostics().lastDriverError == -5);
  CHECK(tx.radioDiagnostics().lastNodeId == 1U);
  CHECK(tx.radioDiagnostics().lastSequence == 0U);
  CHECK(tx.radioDiagnostics().transmitFailures == 1U);
  CHECK(tx.radioDiagnostics().lastTransmitDriverError == -5);

  radio.transmitResult = fanlink::RadioResult::Ok;
  radio.transmitDriverError = 0;
  radio.queueFailure(fanlink::RadioResult::Timeout, -6);
  tx.runOneSlot();
  CHECK(tx.radioDiagnostics().lastFault == fanlink::TxRadioFault::ReceiveTimeout);
  CHECK(tx.radioDiagnostics().lastDriverError == -6);
  CHECK(tx.radioDiagnostics().receiveTimeouts == 1U);
  CHECK(tx.radioDiagnostics().lastReceiveDriverError == -6);

  radio.queueFailure(fanlink::RadioResult::CrcError, -7);
  tx.runOneSlot();
  CHECK(tx.radioDiagnostics().lastFault == fanlink::TxRadioFault::ReceiveCrc);
  CHECK(tx.radioDiagnostics().receiveCrcErrors == 1U);

  radio.queueFailure(fanlink::RadioResult::IoError, -705);
  tx.runOneSlot();
  CHECK(tx.radioDiagnostics().lastFault == fanlink::TxRadioFault::ReceiveIo);
  CHECK(tx.radioDiagnostics().receiveIoErrors == 1U);

  radio.queuePacket(makeRpm(networkId, 4U, 1U, 1234U));
  tx.runOneSlot();
  CHECK(tx.radioDiagnostics().lastFault == fanlink::TxRadioFault::InvalidReply);
  CHECK(tx.radioDiagnostics().invalidReplies == 1U);

  radio.queuePacket(makeRpm(networkId, 5U, 6U, 4321U));
  tx.runOneSlot();
  CHECK(tx.radioDiagnostics().lastFault == fanlink::TxRadioFault::InvalidReply);
  CHECK(tx.radioDiagnostics().lastDriverError == 0);
  CHECK(tx.radioDiagnostics().lastTransmitDriverError == -5);
  CHECK(tx.radioDiagnostics().lastReceiveDriverError == -705);
  CHECK(tx.radioDiagnostics().acceptedReplies == 1U);
  CHECK(tx.radioDiagnostics().generation == 6U);
  CHECK(tx.nextPollId() == 7U);
  CHECK(tx.nextSequence() == 6U);
}

void testTxRadioDiagnosticTextBounded() {
  fanlink::TxRadioDiagnostics diagnostics{};
  diagnostics.beginOk = true;
  diagnostics.beginDriverError = std::numeric_limits<int>::min();
  diagnostics.lastFault = fanlink::TxRadioFault::InvalidReply;
  diagnostics.lastDriverError = std::numeric_limits<int>::min();
  diagnostics.lastTransmitDriverError = std::numeric_limits<int>::min();
  diagnostics.lastReceiveDriverError = std::numeric_limits<int>::min();
  diagnostics.lastNodeId = 16U;
  diagnostics.lastSequence = std::numeric_limits<std::uint16_t>::max();
  diagnostics.transmitFailures = std::numeric_limits<std::uint32_t>::max();
  diagnostics.receiveTimeouts = std::numeric_limits<std::uint32_t>::max();
  diagnostics.receiveCrcErrors = std::numeric_limits<std::uint32_t>::max();
  diagnostics.receiveIoErrors = std::numeric_limits<std::uint32_t>::max();
  diagnostics.invalidReplies = std::numeric_limits<std::uint32_t>::max();
  diagnostics.acceptedReplies = std::numeric_limits<std::uint32_t>::max();

  std::array<char, fanlink::kSerialMaxPayload + 1U> text{};
  const auto length = fanlink::formatTxRadioDiagnostics(diagnostics, text.data(), text.size());
  CHECK(length > 0U);
  CHECK(length <= fanlink::kSerialMaxPayload);
  CHECK(std::strlen(text.data()) == length);
  CHECK(std::string(text.data()).find("B-32768") != std::string::npos);
  CHECK(std::string(text.data()).find("f7") != std::string::npos);
  CHECK(fanlink::formatTxRadioDiagnostics(diagnostics, text.data(), 8U) == 0U);
  CHECK(text[0] == '\0');
}

void testTxHostFailsafeAtExactTimeout() {
  constexpr std::uint32_t networkId = 0x31415926UL;
  FakeClock clock;
  FakeRadio radio;
  fanlink::TxController tx(
      radio, clock, fanlink::TxControllerConfig{networkId, 500U, 10000U, 5U});
  CHECK(tx.begin());

  std::array<std::uint8_t, 16> duty{};
  duty.fill(42U);
  tx.setAllDuty(duty);
  clock.advanceMs(500U);
  tx.runOneSlot();

  fanlink::ControlPacket packet{};
  CHECK(fanlink::decodeControlPacket(radio.transmissions.back().data(),
                                     radio.transmissions.back().size(), packet));
  CHECK((packet.flags & fanlink::control_flag::kForceFullSpeed) != 0U);
  CHECK((packet.flags & fanlink::control_flag::kHostFailsafe) != 0U);
  CHECK(std::all_of(packet.duty.begin(), packet.duty.end(),
                    [](std::uint8_t value) { return value == 255U; }));
}

void testTxRejectsWrongNetworkSequenceAndCrcReplies() {
  constexpr std::uint32_t networkId = 0x76543210UL;
  FakeClock clock;
  FakeRadio radio;
  fanlink::TxController tx(
      radio, clock, fanlink::TxControllerConfig{networkId, 500U, 10000U, 5U});
  CHECK(tx.begin());
  tx.noteHostHeartbeat();

  radio.queuePacket(makeRpm(networkId + 1U, 0U, 1U, 1001U));
  tx.runOneSlot();
  CHECK(!tx.nodeTelemetry(1U).seen);
  CHECK(tx.nodeTelemetry(1U).missedPolls == 1U);

  radio.queuePacket(makeRpm(networkId, 99U, 2U, 1002U));
  tx.runOneSlot();
  CHECK(!tx.nodeTelemetry(2U).seen);
  CHECK(tx.nodeTelemetry(2U).missedPolls == 1U);

  auto corrupt = makeRpm(networkId, 2U, 3U, 1003U);
  corrupt[12] ^= 0x01U;
  radio.queuePacket(corrupt);
  tx.runOneSlot();
  CHECK(!tx.nodeTelemetry(3U).seen);
  CHECK(tx.nodeTelemetry(3U).missedPolls == 1U);
}

void testRxChannelSelectionPollingAndFailsafe() {
  constexpr std::uint32_t networkId = 0x55667788UL;
  FakeClock clock;
  FakeRadio radio;
  FakeFan fan;
  FakeTach tach;
  tach.current = fanlink::TachSample{1234U, true};
  fanlink::RxController rx(
      radio, clock, fan, tach,
      fanlink::RxControllerConfig{networkId, 3U, 25U, 500U, 1200U, 64U, 1500U});
  CHECK(rx.begin());
  CHECK(fan.began);
  CHECK(tach.began);
  CHECK(fan.appliedDuty() == 255U);

  std::array<std::uint8_t, 16> duty{};
  duty.fill(10U);
  duty[2] = 77U;
  radio.queuePacket(makeControl(networkId, 10U, 2U, duty), -55);
  rx.runOnce();
  CHECK(fan.appliedDuty() == 77U);
  CHECK(radio.transmissions.empty());
  CHECK(!rx.linkFailsafeActive());

  duty[2] = 88U;
  radio.queuePacket(makeControl(networkId, 11U, 3U, duty), -56);
  rx.runOnce();
  CHECK(fan.appliedDuty() == 88U);
  CHECK(radio.transmissions.size() == 1U);
  fanlink::RpmPacket reply{};
  CHECK(fanlink::decodeRpmPacket(radio.transmissions.back().data(),
                                 radio.transmissions.back().size(), reply));
  CHECK(reply.nodeId == 3U);
  CHECK(reply.sequenceEcho == 11U);
  CHECK(reply.rpm == 1234U);
  CHECK(reply.appliedDuty == 88U);
  CHECK(reply.downlinkRssiDbm == -56);

  clock.advanceMs(501U);
  radio.queueTimeout();
  rx.runOnce();
  CHECK(rx.linkFailsafeActive());
  CHECK(fan.appliedDuty() == 255U);
}

void testRxForceFullAndSequenceGap() {
  constexpr std::uint32_t networkId = 0xAABBCCDDUL;
  FakeClock clock;
  FakeRadio radio;
  FakeFan fan;
  FakeTach tach;
  tach.current = fanlink::TachSample{2222U, true};
  fanlink::RxController rx(
      radio, clock, fan, tach,
      fanlink::RxControllerConfig{networkId, 5U, 25U, 500U, 10U, 64U, 1500U});
  CHECK(rx.begin());

  std::array<std::uint8_t, 16> duty{};
  duty.fill(50U);
  duty[4] = 12U;
  radio.queuePacket(makeControl(networkId, 100U, 4U, duty));
  rx.runOnce();
  CHECK(fan.appliedDuty() == 12U);

  radio.queuePacket(makeControl(networkId, 103U, 5U, duty,
                                fanlink::control_flag::kForceFullSpeed));
  rx.runOnce();
  CHECK(fan.appliedDuty() == 255U);
  CHECK(radio.transmissions.size() == 1U);

  fanlink::RpmPacket reply{};
  CHECK(fanlink::decodeRpmPacket(radio.transmissions.back().data(),
                                 radio.transmissions.back().size(), reply));
  CHECK((reply.status & fanlink::node_status::kSequenceGap) != 0U);
  CHECK(reply.missedDownlinks == 2U);
}

void testRxReceiveDeadlineAndFailsafeStatusLatch() {
  constexpr std::uint32_t networkId = 0x0BADCAFEUL;
  FakeClock clock;
  FakeRadio radio(&clock);
  FakeFan fan;
  FakeTach tach;
  tach.current = fanlink::TachSample{1500U, true};
  fanlink::RxController rx(
      radio, clock, fan, tach,
      fanlink::RxControllerConfig{networkId, 1U, 25U, 500U, 0U, 64U, 1500U});
  CHECK(rx.begin());

  std::array<std::uint8_t, 16> duty{};
  duty.fill(40U);
  radio.queuePacket(makeControl(networkId, 10U, 2U, duty));
  rx.runOnce();
  CHECK(fan.appliedDuty() == 40U);
  CHECK(!rx.linkFailsafeActive());

  clock.advanceMs(490U);
  duty.fill(20U);
  radio.queuePacket(makeControl(networkId, 11U, 1U, duty), -50, 10U);
  rx.runOnce();
  CHECK(radio.lastTimeoutMs == 10U);
  CHECK(clock.millis() == 500U);
  CHECK(rx.linkFailsafeActive());
  CHECK(fan.appliedDuty() == 255U);
  CHECK(radio.transmissions.empty());

  duty.fill(30U);
  const auto recovery = makeControl(networkId, 12U, 1U, duty);
  radio.queuePacket(recovery);
  rx.runOnce();
  CHECK(!rx.linkFailsafeActive());
  CHECK(fan.appliedDuty() == 30U);
  CHECK(radio.transmissions.size() == 1U);

  fanlink::RpmPacket reply{};
  CHECK(fanlink::decodeRpmPacket(radio.transmissions.back().data(),
                                 radio.transmissions.back().size(), reply));
  CHECK((reply.status & fanlink::node_status::kLinkFailsafe) != 0U);
  CHECK((rx.pendingStatus() & fanlink::node_status::kLinkFailsafe) == 0U);

  radio.queuePacket(recovery);
  rx.runOnce();
  CHECK(radio.transmissions.size() == 2U);
  CHECK(fanlink::decodeRpmPacket(radio.transmissions.back().data(),
                                 radio.transmissions.back().size(), reply));
  CHECK((reply.status & fanlink::node_status::kLinkFailsafe) == 0U);
}

void testRxReacquiresSequenceAfterLinkTimeout() {
  constexpr std::uint32_t networkId = 0x89ABCDEFUL;
  FakeClock clock;
  FakeRadio radio;
  FakeFan fan;
  FakeTach tach;
  fanlink::RxController rx(
      radio, clock, fan, tach,
      fanlink::RxControllerConfig{networkId, 4U, 25U, 500U, 0U, 64U, 1500U});
  CHECK(rx.begin());

  std::array<std::uint8_t, 16> duty{};
  duty.fill(25U);
  radio.queuePacket(makeControl(networkId, 0x7FFFU, 3U, duty));
  rx.runOnce();
  CHECK(fan.appliedDuty() == 25U);

  const auto historySize = fan.history.size();
  clock.advanceMs(500U);
  duty.fill(90U);
  radio.queuePacket(makeControl(networkId, 0U, 3U, duty));
  rx.runOnce();
  CHECK(fan.history.size() >= historySize + 2U);
  CHECK(fan.history[historySize] == 255U);
  CHECK(fan.appliedDuty() == 90U);
  CHECK(!rx.linkFailsafeActive());
}

void testRxInvalidPacketsDoNotRefreshWatchdog() {
  constexpr std::uint32_t networkId = 0x13572468UL;
  FakeClock clock;
  FakeRadio radio;
  FakeFan fan;
  FakeTach tach;
  fanlink::RxController rx(
      radio, clock, fan, tach,
      fanlink::RxControllerConfig{networkId, 6U, 25U, 500U, 0U, 64U, 1500U});
  CHECK(rx.begin());

  std::array<std::uint8_t, 16> duty{};
  duty.fill(70U);
  radio.queuePacket(makeControl(networkId, 100U, 1U, duty));
  rx.runOnce();
  CHECK(fan.appliedDuty() == 70U);

  clock.advanceMs(400U);
  radio.queuePacket(makeControl(networkId + 1U, 101U, 1U, duty));
  rx.runOnce();
  CHECK(fan.appliedDuty() == 70U);

  clock.advanceMs(40U);
  auto corrupt = makeControl(networkId, 101U, 1U, duty);
  corrupt[12] ^= 0x01U;
  radio.queuePacket(corrupt);
  rx.runOnce();
  CHECK(fan.appliedDuty() == 70U);

  clock.advanceMs(40U);
  radio.queuePacket(makeControl(networkId, 99U, 1U, duty));
  rx.runOnce();
  CHECK(radio.lastTimeoutMs == 20U);
  CHECK(fan.appliedDuty() == 70U);

  clock.advanceMs(20U);
  radio.queueTimeout();
  rx.runOnce();
  CHECK(rx.linkFailsafeActive());
  CHECK(fan.appliedDuty() == 255U);
}

void testRxRejectsStaleSequenceButAcceptsRetryAndWrap() {
  constexpr std::uint32_t networkId = 0x10203040UL;
  FakeClock clock;
  FakeRadio radio;
  FakeFan fan;
  FakeTach tach;
  tach.current = fanlink::TachSample{1800U, true};
  fanlink::RxController rx(
      radio, clock, fan, tach,
      fanlink::RxControllerConfig{networkId, 2U, 25U, 500U, 10U, 64U, 1500U});
  CHECK(rx.begin());

  std::array<std::uint8_t, 16> duty{};
  duty.fill(30U);
  duty[1] = 80U;
  const auto original = makeControl(networkId, 100U, 2U, duty);
  radio.queuePacket(original);
  rx.runOnce();
  CHECK(fan.appliedDuty() == 80U);
  CHECK(radio.transmissions.size() == 1U);

  // Clearly older sequence: ignore both duty and poll permission.
  duty[1] = 10U;
  radio.queuePacket(makeControl(networkId, 99U, 2U, duty));
  rx.runOnce();
  CHECK(fan.appliedDuty() == 80U);
  CHECK(radio.transmissions.size() == 1U);

  // An exact same-sequence retry is permitted and may generate another reply.
  radio.queuePacket(original);
  rx.runOnce();
  CHECK(fan.appliedDuty() == 80U);
  CHECK(radio.transmissions.size() == 2U);

  // Reusing the sequence with changed content is not a retry.
  clock.advanceMs(400U);
  duty[1] = 81U;
  radio.queuePacket(makeControl(networkId, 100U, 2U, duty));
  rx.runOnce();
  CHECK(fan.appliedDuty() == 80U);
  CHECK(radio.transmissions.size() == 2U);

  clock.advanceMs(40U);
  duty[1] = 80U;
  radio.queuePacket(makeControl(networkId, 100U, 1U, duty));
  rx.runOnce();
  CHECK(fan.appliedDuty() == 80U);
  CHECK(radio.transmissions.size() == 2U);

  clock.advanceMs(40U);
  radio.queuePacket(makeControl(networkId, 100U, 2U, duty,
                                fanlink::control_flag::kForceFullSpeed));
  rx.runOnce();
  CHECK(fan.appliedDuty() == 80U);
  CHECK(radio.transmissions.size() == 2U);

  clock.advanceMs(20U);
  radio.queueTimeout();
  rx.runOnce();
  CHECK(rx.linkFailsafeActive());
  CHECK(fan.appliedDuty() == 255U);

  // Exercise wrap-around in a fresh controller: 65535 -> 0 is forward by one.
  FakeRadio wrapRadio;
  FakeFan wrapFan;
  FakeTach wrapTach;
  fanlink::RxController wrapRx(
      wrapRadio, clock, wrapFan, wrapTach,
      fanlink::RxControllerConfig{networkId, 2U, 25U, 500U, 10U, 64U, 1500U});
  CHECK(wrapRx.begin());
  duty[1] = 90U;
  wrapRadio.queuePacket(makeControl(networkId, 0xFFFFU, 1U, duty));
  wrapRx.runOnce();
  duty[1] = 91U;
  wrapRadio.queuePacket(makeControl(networkId, 0U, 1U, duty));
  wrapRx.runOnce();
  CHECK(wrapFan.appliedDuty() == 91U);
}

void testRxRejectsWrongNetworkAndBadCrc() {
  constexpr std::uint32_t networkId = 0x99998888UL;
  FakeClock clock;
  FakeRadio radio;
  FakeFan fan;
  FakeTach tach;
  fanlink::RxController rx(
      radio, clock, fan, tach,
      fanlink::RxControllerConfig{networkId, 1U, 25U, 500U, 10U, 64U, 1500U});
  CHECK(rx.begin());

  std::array<std::uint8_t, 16> duty{};
  duty.fill(1U);
  auto wrongNetwork = makeControl(networkId + 1U, 1U, 1U, duty);
  radio.queuePacket(wrongNetwork);
  rx.runOnce();
  CHECK(rx.linkFailsafeActive());
  CHECK(fan.appliedDuty() == 255U);
  CHECK(radio.transmissions.empty());

  auto corrupt = makeControl(networkId, 2U, 1U, duty);
  corrupt[12] ^= 0x01U;
  radio.queuePacket(corrupt);
  rx.runOnce();
  CHECK(rx.linkFailsafeActive());
  CHECK(radio.transmissions.empty());
}

}  // namespace

int main() {
  testLinkLedPattern();
  testCrc();
  testControlPacketRoundTrip();
  testRpmPacketRoundTrip();
  testSerialParserRoundTripAndResync();
  testLinkMetricsSerialFrame();
  testTxPollCycleAndHostFailsafe();
  testTxAcceptsOnlyMatchingReply();
  testTxRadioDiagnostics();
  testTxRadioDiagnosticTextBounded();
  testTxHostFailsafeAtExactTimeout();
  testTxRejectsWrongNetworkSequenceAndCrcReplies();
  testRxChannelSelectionPollingAndFailsafe();
  testRxForceFullAndSequenceGap();
  testRxReceiveDeadlineAndFailsafeStatusLatch();
  testRxReacquiresSequenceAfterLinkTimeout();
  testRxInvalidPacketsDoNotRefreshWatchdog();
  testRxRejectsStaleSequenceButAcceptsRetryAndWrap();
  testRxRejectsWrongNetworkAndBadCrc();

  if (gFailures == 0) {
    std::cout << "PASS: " << gChecks << " checks\n";
    return 0;
  }
  std::cerr << "FAIL: " << gFailures << " of " << gChecks << " checks failed\n";
  return 1;
}
