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
#include "fanlink/host_control.hpp"
#include "fanlink/interfaces.hpp"
#include "fanlink/link_led_pattern.hpp"
#include "fanlink/poll_cadence.hpp"
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
  bool receiveObserved{false};
  std::uint32_t receiveObservedUs{0};
  std::uint32_t delayUs{0};
  fanlink::RadioRxEvidence evidence{};
};

class FakeRadio final : public fanlink::IRadioPort {
 public:
  explicit FakeRadio(FakeClock* clock = nullptr) : clock_(clock) {}

  bool begin(std::uint32_t networkId) override {
    ++beginCalls;
    if (fanForBeginCheck != nullptr) {
      dutyAtLastBegin = fanForBeginCheck->appliedDuty();
    }
    if (clock_ != nullptr) {
      clock_->advanceMs(beginDelayMs);
    }
    begunNetworkId = networkId;
    began = beginSucceeds;
    if (began) {
      faultNeedsBegin = false;
      transmitFaultNeedsBegin = false;
      receiveFaultNeedsBegin = false;
    }
    lastDriverError = began ? 0 : beginDriverError;
    return began;
  }

  fanlink::RadioResult transmit(const std::uint8_t* data, std::size_t length) override {
    transmissions.emplace_back(data, data + length);
    if (faultNeedsBegin || transmitFaultNeedsBegin) {
      lastDriverError = -705;
      return fanlink::RadioResult::IoError;
    }
    lastDriverError = transmitDriverError;
    return transmitResult;
  }

  fanlink::RadioRxResult receive(std::uint8_t* data, std::size_t capacity,
                                 std::uint32_t timeoutMs) override {
    lastTimeoutMs = timeoutMs;
    receiveTimeouts.push_back(timeoutMs);
    if (faultNeedsBegin || receiveFaultNeedsBegin) {
      lastDriverError = -705;
      return {fanlink::RadioResult::IoError, 0U, -127, lastDriverError};
    }
    if (receives.empty()) {
      lastDriverError = 0;
      return {fanlink::RadioResult::Timeout, 0U, -127, lastDriverError};
    }
    auto next = receives.front();
    receives.pop_front();
    lastDriverError = next.driverError;
    if (clock_ != nullptr) {
      clock_->advanceMs(next.delayMs);
      clock_->advanceUs(next.delayUs);
    }
    if (next.result != fanlink::RadioResult::Ok) {
      return {next.result, 0U, next.rssi, next.driverError,
              next.receiveObserved, next.receiveObservedUs, next.evidence};
    }
    const auto copyLength = std::min(capacity, next.data.size());
    std::copy_n(next.data.begin(), copyLength, data);
    return {fanlink::RadioResult::Ok, next.data.size(), next.rssi, next.driverError,
            next.receiveObserved, next.receiveObservedUs, next.evidence};
  }

  int lastError() const override { return lastDriverError; }

  fanlink::RadioRxResult receiveForUs(std::uint8_t* data, std::size_t capacity,
                                     std::uint32_t timeoutUs) override {
    receiveTimeoutsUs.push_back(timeoutUs);
    return receive(data, capacity, timeoutUs / 1000U);
  }

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
  bool faultNeedsBegin{false};
  bool transmitFaultNeedsBegin{false};
  bool receiveFaultNeedsBegin{false};
  unsigned beginCalls{0};
  std::uint32_t beginDelayMs{0};
  fanlink::IFanOutput* fanForBeginCheck{nullptr};
  std::uint8_t dutyAtLastBegin{0};
  bool began{false};
  std::uint32_t begunNetworkId{0};
  fanlink::RadioResult transmitResult{fanlink::RadioResult::Ok};
  int beginDriverError{0};
  int transmitDriverError{0};
  int lastDriverError{0};
  std::uint32_t lastTimeoutMs{0};
  std::vector<std::uint32_t> receiveTimeouts{};
  std::vector<std::uint32_t> receiveTimeoutsUs{};
  std::deque<QueuedReceive> receives{};
  std::vector<std::vector<std::uint8_t>> transmissions{};

 private:
  FakeClock* clock_{nullptr};
};

class FakeFan final : public fanlink::IFanOutput {
 public:
  bool begin() override {
    ++beginCalls;
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
  unsigned beginCalls{0};
  bool began{false};
  std::uint8_t applied{0};
  std::vector<std::uint8_t> history{};
};

class FakeTach final : public fanlink::ITachometer {
 public:
  bool begin() override {
    ++beginCalls;
    began = true;
    return beginSucceeds;
  }
  fanlink::TachSample sample(std::uint32_t nowUs) override {
    lastSampleUs = nowUs;
    return current;
  }

  bool beginSucceeds{true};
  unsigned beginCalls{0};
  bool began{false};
  fanlink::TachSample current{};
  std::uint32_t lastSampleUs{0};
};

std::vector<std::uint8_t> makeControl(std::uint32_t networkId, std::uint16_t sequence,
                                      std::uint8_t pollId,
                                      const std::array<std::uint8_t, fanlink::kNodeCount>& duty,
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
  source.pollId = fanlink::kNodeCount;
  source.flags = fanlink::control_flag::kHostFailsafe;
  for (std::size_t i = 0; i < source.duty.size(); ++i) {
    source.duty[i] = i < fanlink::kNodeCount ? static_cast<std::uint8_t>(i * 13U) : 255U;
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
  source.payloadLength = fanlink::kSetAllDutyPayloadSize;
  for (std::uint8_t i = 0; i < fanlink::kNodeCount; ++i) {
    source.payload[i] = static_cast<std::uint8_t>(i + 10U);
  }

  std::array<std::uint8_t, fanlink::kSerialMaxFrameSize> encoded{};
  std::size_t length = 0U;
  CHECK(fanlink::encodeSerialFrame(source, encoded, length));
  CHECK(length == 18U);
  CHECK(encoded[2] == 2U);

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

  // A correctly checksummed old Host version must not refresh control state.
  auto oldVersion = encoded;
  oldVersion[2] = 1U;
  const auto oldCrc = fanlink::crc16CcittFalse(oldVersion.data(), length - 2U);
  oldVersion[length - 2U] = static_cast<std::uint8_t>(oldCrc);
  oldVersion[length - 1U] = static_cast<std::uint8_t>(oldCrc >> 8U);
  for (std::size_t i = 0; i < length; ++i) {
    CHECK(!parser.feed(oldVersion[i], result));
  }

  for (std::size_t i = 0; i < length; ++i) {
    completed = parser.feed(encoded[i], result) || completed;
  }
  CHECK(completed);
}

void testLinkMetricsSerialFrame() {
  CHECK(fanlink::kStatusSnapshotPayloadSize == 54U);
  CHECK(fanlink::kLinkMetricsPayloadSize == 16U);

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

  std::array<std::uint8_t, fanlink::kNodeCount> desired{};
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

  // Complete one full poll cycle and verify strict 1..8 ordering.
  for (int i = 0; i < fanlink::kNodeCount - 2; ++i) {
    tx.runOneSlot();
  }
  CHECK(radio.transmissions.size() == fanlink::kNodeCount);
  for (std::size_t i = 0; i < fanlink::kNodeCount; ++i) {
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

void testNativeEightNodesFixedRfEnvelope() {
  static_assert(fanlink::kNodeCount == 8U);
  static_assert(fanlink::kControlPacketSize == 30U);
  static_assert(fanlink::kRpmPacketSize == 30U);
  fanlink::ControlPacket source{};
  CHECK(source.duty.size() == 8U);
  source.duty.fill(42U);
  source.pollId = 8U;
  std::array<std::uint8_t, fanlink::kAirPacketSize> bytes{};
  CHECK(fanlink::encodeControlPacket(source, bytes));
  for (unsigned index = 0U; index < 8U; ++index) CHECK(bytes[12U + index] == 42U);
  for (unsigned offset = 20U; offset < 28U; ++offset) CHECK(bytes[offset] == 255U);
  const auto seal = [&bytes]() {
    const auto crc = fanlink::crc16CcittFalse(bytes.data(), 28U);
    bytes[28] = static_cast<std::uint8_t>(crc);
    bytes[29] = static_cast<std::uint8_t>(crc >> 8U);
  };
  // RF reserved bytes never acquire a control meaning, even with valid CRC.
  for (unsigned offset = 20U; offset < 28U; ++offset) bytes[offset] = 0U;
  seal();
  fanlink::ControlPacket decoded{};
  CHECK(fanlink::decodeControlPacket(bytes.data(), bytes.size(), decoded));
  CHECK(decoded.duty == source.duty);
  for (unsigned node = 9U; node <= 16U; ++node) {
    CHECK(!fanlink::isValidNodeId(node));
    source.pollId = node;
    CHECK(!fanlink::encodeControlPacket(source, bytes));
    bytes[10] = node; seal();
    CHECK(!fanlink::decodeControlPacket(bytes.data(), bytes.size(), decoded));
    fanlink::RpmPacket rpm{};
    rpm.nodeId = node;
    CHECK(!fanlink::encodeRpmPacket(rpm, bytes));
    rpm.nodeId = 8U;
    CHECK(fanlink::encodeRpmPacket(rpm, bytes));
    bytes[10] = node; seal();
    CHECK(!fanlink::decodeRpmPacket(bytes.data(), bytes.size(), rpm));
    // Restore Control envelope for the next invalid poll.
    source.pollId = 8U;
    CHECK(fanlink::encodeControlPacket(source, bytes));
  }
  FakeClock clock;
  FakeRadio radio;
  fanlink::TxController tx(radio, clock, fanlink::TxControllerConfig{});
  CHECK(tx.begin());
  std::array<std::uint8_t, fanlink::kNodeCount> duties{};
  duties.fill(11U);
  tx.setAllDuty(duties);
  CHECK(tx.requestedDuty().size() == 8U);
  for (unsigned node = 9U; node <= 16U; ++node) {
    CHECK(!tx.setNodeDuty(node, 0U));
    CHECK(tx.effectiveDuty(node) == 255U);
    CHECK(!tx.nodeTelemetry(node).seen);
    CHECK(tx.nodeTelemetry(node).appliedDuty == 255U);
    CHECK(tx.nodeDiagnostics(node).transmittedPolls == 0U);
  }
  clock.advanceMs(500U);
  CHECK(!tx.setNodeDuty(9U, 0U));
  CHECK(tx.hostFailsafeActive());  // Invalid command must not renew Host watchdog.
}

void testRealHostControlDispatchRejectsWithoutRenewingWatchdog() {
  const auto exercise = [](std::uint8_t type, std::uint16_t payloadLength,
                           std::uint8_t node, bool legacy, bool accepted) {
    FakeClock clock;
    FakeRadio radio;
    fanlink::TxController tx(radio, clock, fanlink::TxControllerConfig{});
    CHECK(tx.begin());
    std::array<std::uint8_t, fanlink::kNodeCount> initial{};
    initial.fill(77U);
    tx.setAllDuty(initial);
    clock.advanceMs(499U);
    fanlink::SerialFrame source{};
    source.type = type;
    source.payloadLength = payloadLength;
    source.payload.fill(11U);
    if (type == fanlink::serial_type::kSetOneDuty) source.payload[0] = node;
    std::array<std::uint8_t, fanlink::kSerialMaxFrameSize> encoded{};
    std::size_t length = 0U;
    CHECK(fanlink::encodeSerialFrame(source, encoded, length));
    if (legacy) {
      encoded[2] = 1U;
      const auto crc = fanlink::crc16CcittFalse(encoded.data(), length - 2U);
      encoded[length - 2U] = static_cast<std::uint8_t>(crc);
      encoded[length - 1U] = static_cast<std::uint8_t>(crc >> 8U);
    }
    fanlink::SerialFrameParser parser;
    fanlink::SerialFrame frame{};
    unsigned delivered = 0U, applied = 0U;
    for (std::size_t i = 0U; i < length; ++i) {
      if (parser.feed(encoded[i], frame)) {
        ++delivered;
        applied += fanlink::handleHostControlFrame(frame, tx) ? 1U : 0U;
      }
    }
    CHECK(delivered == (legacy ? 0U : 1U));
    CHECK(applied == (accepted ? 1U : 0U));
    CHECK(!tx.hostFailsafeActive());
    clock.advanceMs(1U);
    CHECK(tx.hostFailsafeActive() == !accepted);
    if (!accepted) {
      CHECK(tx.requestedDuty() == initial);
      CHECK(tx.effectiveDuty(8U) == 255U);
    } else {
      if (type == fanlink::serial_type::kSetAllDuty) {
        for (auto duty : tx.requestedDuty()) CHECK(duty == 11U);
      } else if (type == fanlink::serial_type::kSetOneDuty) {
        CHECK(tx.requestedDuty()[7] == 11U);
        CHECK(tx.requestedDuty()[0] == 77U);
      } else {
        CHECK(tx.requestedDuty() == initial);
      }
      clock.advanceMs(499U);
      CHECK(tx.hostFailsafeActive());
    }
  };
  exercise(fanlink::serial_type::kSetAllDuty, 8U, 0U, false, true);
  for (auto length : {7U, 9U, 16U})
    exercise(fanlink::serial_type::kSetAllDuty, length, 0U, false, false);
  for (auto length : {8U, 16U})
    exercise(fanlink::serial_type::kSetAllDuty, length, 0U, true, false);
  exercise(fanlink::serial_type::kSetOneDuty, 2U, 8U, false, true);
  for (auto node : {0U, 9U, 16U})
    exercise(fanlink::serial_type::kSetOneDuty, 2U, node, false, false);
  for (auto length : {1U, 3U})
    exercise(fanlink::serial_type::kSetOneDuty, length, 8U, false, false);
  exercise(fanlink::serial_type::kSetOneDuty, 2U, 8U, true, false);
  exercise(fanlink::serial_type::kHeartbeat, 0U, 0U, false, true);
  exercise(fanlink::serial_type::kHeartbeat, 1U, 0U, false, false);
  exercise(fanlink::serial_type::kHeartbeat, 0U, 0U, true, false);
  exercise(0xFFU, 8U, 0U, false, false);
}

void testEightNodeOneSecondDefaultCycle() {
  FakeClock clock;
  FakeRadio radio;
  fanlink::TxController tx(radio, clock, fanlink::TxControllerConfig{});
  CHECK(tx.begin());
  for (unsigned slot = 0U; slot < 16U; ++slot) {
    CHECK(clock.micros() == slot * 125000U);
    tx.noteHostHeartbeat();
    CHECK(tx.setNodeDuty(static_cast<std::uint8_t>(slot % 8U + 1U), 73U));
    tx.runOneSlot();
    fanlink::ControlPacket packet{};
    CHECK(fanlink::decodeControlPacket(radio.transmissions.back().data(), 30U, packet));
    CHECK(packet.pollId == slot % 8U + 1U);
    CHECK(packet.sequence == slot);
    CHECK(packet.flags == 0U);
    CHECK(tx.nextPollId() == (slot + 1U) % 8U + 1U);
  }
  CHECK(clock.micros() == 2000000U);
  fanlink::NodeTelemetry node{};
  node.seen = true;
  node.lastSeenMs = 100U;
  node.status = 0U;
  CHECK((fanlink::txNodeTelemetryStatus(node, 1100U) & fanlink::node_status::kTelemetryStale) == 0U);
  CHECK((fanlink::txNodeTelemetryStatus(node, 2600U) & fanlink::node_status::kTelemetryStale) == 0U);
  CHECK((fanlink::txNodeTelemetryStatus(node, 2601U) & fanlink::node_status::kTelemetryStale) != 0U);
}

void testCooperativePollCadence() {
  fanlink::CooperativePollCadence cadence;
  CHECK(cadence.take(0U));
  CHECK(!cadence.take(0U));
  CHECK(!cadence.take(124999U));
  CHECK(cadence.take(125000U));
  CHECK(!cadence.take(125001U));
  // A long pause must not replay a burst of missed slots.
  CHECK(cadence.take(1000000U));
  CHECK(!cadence.take(1000001U));
  CHECK(!cadence.take(1124999U));
  CHECK(cadence.take(1125000U));
  cadence.reset();
  constexpr auto wrapStart = UINT32_MAX - 10U;
  CHECK(cadence.take(wrapStart));
  CHECK(!cadence.take(static_cast<std::uint32_t>(wrapStart + 124999U)));
  CHECK(cadence.take(static_cast<std::uint32_t>(wrapStart + 125000U)));

  FakeClock clock;
  FakeRadio radio;
  fanlink::TxController tx(radio, clock, fanlink::TxControllerConfig{});
  CHECK(!tx.runScheduledSlot());
  CHECK(tx.begin());
  cadence.reset();
  unsigned hostServiceCalls = 0U;
  unsigned polls = 0U;
  for (unsigned tick = 0U; tick < 1000U; ++tick) {
    // 1ms cooperative main-loop model: input service continues between polls.
    ++hostServiceCalls;
    if (tick % 200U == 0U) tx.noteHostHeartbeat();
    const auto beforeUs = clock.micros();
    if (tx.runScheduledSlot()) {
      ++polls;
      CHECK(clock.micros() == beforeUs);  // Fake radio immediate; no slot sleep.
    }
    // A second caller, e.g. a maintenance preamble, shares exactly that clock.
    CHECK(!tx.runScheduledSlot());
    clock.advanceUs(1000U);
  }
  CHECK(hostServiceCalls == 1000U && polls == 8U);
  CHECK(tx.nextPollId() == 1U && tx.nextSequence() == 8U);
  CHECK(!tx.hostFailsafeActive());
  clock.advanceMs(1000U);
  CHECK(tx.runScheduledSlot());
  CHECK(!tx.runScheduledSlot());
  clock.advanceUs(124999U);
  CHECK(!tx.runScheduledSlot());
  clock.advanceUs(1U);
  CHECK(tx.runScheduledSlot());
  // Actual controller cadence across micros wrap, not just its helper.
  FakeClock wrappedClock;
  FakeRadio wrappedRadio;
  wrappedClock.advanceUs(UINT32_MAX - 99U);
  fanlink::TxController wrappedTx(wrappedRadio, wrappedClock, fanlink::TxControllerConfig{});
  CHECK(wrappedTx.begin());
  CHECK(wrappedTx.runScheduledSlot());
  wrappedClock.advanceUs(124999U);
  CHECK(!wrappedTx.runScheduledSlot());
  wrappedClock.advanceUs(1U);
  CHECK(wrappedTx.runScheduledSlot());

  // Realistic receive work consumes its 5ms budget, not the whole125ms. The
  // next poll is measured from start, not from receive completion plus125ms.
  FakeClock busyClock;
  FakeRadio busyRadio(&busyClock);
  fanlink::TxController busyTx(busyRadio, busyClock, fanlink::TxControllerConfig{});
  CHECK(busyTx.begin());
  busyRadio.queueTimeout(5U);
  CHECK(busyTx.runScheduledSlot());
  CHECK(busyClock.micros() == 5000U);
  busyClock.advanceUs(119999U);
  CHECK(!busyTx.runScheduledSlot());
  busyClock.advanceUs(1U);
  CHECK(busyTx.runScheduledSlot());
  CHECK(busyRadio.transmissions.size() == 2U);
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
  // The invalid packet is now followed by another receive within this slot.
  // That fake receive returns Timeout with driver code 0.
  CHECK(tx.radioDiagnostics().lastReceiveDriverError == 0);
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

void testTxNodeDiagnosticsKeepPollWindowsSeparate() {
  constexpr std::uint32_t networkId = 0x01020304U;
  FakeClock clock;
  FakeRadio radio;
  fanlink::TxController tx(radio, clock, fanlink::TxControllerConfig{networkId, 500U, 10000U, 5U});
  CHECK(tx.begin());
  CHECK(tx.nodeDiagnostics(0U).transmittedPolls == 0U);
  CHECK(tx.nodeDiagnostics(17U).acceptedReplies == 0U);
  CHECK(fanlink::nextTxNodeDiagnosticId(tx, 0U) == 0U);

  // CRC and a packet naming node 2 occur during node 1's poll window. Neither
  // proves its sender, and only a matching reply updates node 1's telemetry.
  radio.queueFailure(fanlink::RadioResult::CrcError, -7);
  radio.queuePacket(makeRpm(networkId, 0U, 2U, 9999U));
  radio.queuePacket(makeRpm(networkId, 0U, 1U, 2400U));
  tx.runOneSlot();
  const auto& node1 = tx.nodeDiagnostics(1U);
  CHECK(node1.transmittedPolls == 1U);
  CHECK(node1.acceptedReplies == 1U);
  CHECK(node1.receiveCrcErrors == 1U);
  CHECK(node1.invalidReplies == 1U);
  CHECK(node1.receiveTimeouts == 0U);
  CHECK(tx.nodeDiagnostics(2U).invalidReplies == 0U);
  CHECK(!tx.nodeTelemetry(2U).seen);

  tx.runOneSlot();  // Node 2 never replies.
  CHECK(tx.nodeDiagnostics(2U).transmittedPolls == 1U);
  CHECK(tx.nodeDiagnostics(2U).receiveTimeouts == 1U);
  CHECK(node1.transmittedPolls == 1U);
  CHECK(node1.receiveTimeouts == 0U);

  radio.transmitResult = fanlink::RadioResult::IoError;
  tx.runOneSlot();  // Failed transmission does not count as a transmitted poll.
  CHECK(tx.nodeDiagnostics(3U).transmittedPolls == 0U);
  CHECK(tx.nodeDiagnostics(3U).transmitFailures == 1U);
  CHECK(tx.nodeDiagnostics(3U).receiveTimeouts == 0U);
  CHECK(tx.nodeTelemetry(3U).missedPolls == 1U);

  radio.transmitResult = fanlink::RadioResult::Ok;
  radio.queueFailure(fanlink::RadioResult::IoError, -705);
  tx.runOneSlot();
  CHECK(tx.nodeDiagnostics(4U).transmittedPolls == 1U);
  CHECK(tx.nodeDiagnostics(4U).receiveIoErrors == 1U);
  CHECK(tx.nodeDiagnostics(4U).receiveTimeouts == 0U);
  CHECK(tx.nodeTelemetry(4U).missedPolls == 1U);

  while (tx.nextPollId() != 8U) {
    tx.runOneSlot();
  }
  radio.queuePacket(makeRpm(networkId, tx.nextSequence(), 8U, 1600U));
  tx.runOneSlot();
  CHECK(fanlink::nextTxNodeDiagnosticId(tx, 0U) == 1U);
  CHECK(fanlink::nextTxNodeDiagnosticId(tx, 1U) == 8U);
  CHECK(fanlink::nextTxNodeDiagnosticId(tx, 8U) == 1U);
  CHECK(fanlink::nextTxNodeDiagnosticId(tx, 255U) == 1U);

  tx.runOneSlot();  // Node 1's next poll is lost; lifetime acceptance stays 1.
  CHECK(node1.transmittedPolls == 2U);
  CHECK(node1.acceptedReplies == 1U);
  CHECK(node1.receiveTimeouts == 1U);
  CHECK(tx.nodeTelemetry(1U).missedPolls == 1U);
  clock.advanceMs(2501U);
  CHECK(fanlink::nextTxNodeDiagnosticId(tx, 1U) == 8U);
  CHECK(fanlink::nextTxNodeDiagnosticId(tx, 8U) == 1U);
  CHECK((fanlink::txNodeTelemetryStatus(tx.nodeTelemetry(1U), clock.millis()) &
         fanlink::node_status::kTelemetryStale) != 0U);
  CHECK(node1.transmittedPolls == 2U);  // Formatting/selection never count a poll.
}

void testTxNodeDiagnosticCountersSaturate() {
  constexpr std::uint32_t networkId = 0x01020304U;
  constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
  FakeClock clock;
  FakeRadio radio;
  fanlink::TxController tx(radio, clock, fanlink::TxControllerConfig{networkId, 500U, 10000U, 5U});
  CHECK(tx.begin());
  // Seed the actual non-const object's counters near the limit so the native
  // regression exercises production increments without billions of slots.
  auto& node1 = const_cast<fanlink::TxNodeDiagnostics&>(tx.nodeDiagnostics(1U));
  using Counter = std::uint32_t fanlink::TxNodeDiagnostics::*;
  const std::array<Counter, 7U> counters{{
      &fanlink::TxNodeDiagnostics::transmittedPolls,
      &fanlink::TxNodeDiagnostics::acceptedReplies,
      &fanlink::TxNodeDiagnostics::transmitFailures,
      &fanlink::TxNodeDiagnostics::receiveTimeouts,
      &fanlink::TxNodeDiagnostics::receiveCrcErrors,
      &fanlink::TxNodeDiagnostics::receiveIoErrors,
      &fanlink::TxNodeDiagnostics::invalidReplies}};
  for (const auto counter : counters) {
    node1.*counter = maximum - 1U;
  }
  const auto returnToNode1 = [&]() {
    while (tx.nextPollId() != 1U) {
      tx.runOneSlot();
    }
  };
  for (unsigned repetition = 0U; repetition < 2U; ++repetition) {
    returnToNode1();
    radio.queueFailure(fanlink::RadioResult::CrcError, -7);
    radio.queuePacket(makeRpm(networkId + 1U, tx.nextSequence(), 1U, 1U));
    radio.queueFailure(fanlink::RadioResult::IoError, -705);
    tx.runOneSlot();

    returnToNode1();
    radio.queuePacket(makeRpm(networkId, tx.nextSequence(), 1U, 2400U));
    tx.runOneSlot();

    returnToNode1();
    tx.runOneSlot();  // Timeout, not I/O abort.

    returnToNode1();
    radio.transmitResult = fanlink::RadioResult::IoError;
    tx.runOneSlot();
    radio.transmitResult = fanlink::RadioResult::Ok;
    for (const auto counter : counters) {
      CHECK(node1.*counter == maximum);
    }
  }
}

void testTxNodeDiagnosticTextFreshnessWrapAndBound() {
  fanlink::NodeTelemetry node{};
  fanlink::TxNodeDiagnostics diagnostics{};
  std::array<char, fanlink::kSerialMaxPayload + 1U> text{};
  CHECK(fanlink::formatTxNodeDiagnostics(
            1U, node, diagnostics, 0U, text.data(), text.size()) > 0U);
  CHECK(std::string(text.data()) ==
        "RFN n1 p0 o0 m0 a4294967295 S128 x0 t0 c0 i0 v0");

  node.seen = true;
  node.lastSeenMs = 0xFFFFFFFFU - 499U;
  node.status = fanlink::node_status::kSequenceGap;
  node.missedPolls = 5U;
  diagnostics.transmittedPolls = 100U;
  diagnostics.acceptedReplies = 95U;
  CHECK(fanlink::txNodeTelemetryAgeMs(node, 500U) == 1000U);
  CHECK(fanlink::txNodeTelemetryStatus(node, 500U) == fanlink::node_status::kSequenceGap);
  CHECK(fanlink::formatTxNodeDiagnostics(
            1U, node, diagnostics, 2001U, text.data(), text.size()) > 0U);
  CHECK(std::string(text.data()) ==
        "RFN n1 p100 o95 m5 a2501 S136 x0 t0 c0 i0 v0");

  constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
  diagnostics = {maximum, maximum, maximum, maximum, maximum, maximum, maximum};
  node.status = 255U;
  node.missedPolls = 255U;
  node.lastSeenMs = 1U;
  const auto length = fanlink::formatTxNodeDiagnostics(
      8U, node, diagnostics, 0U, text.data(), text.size());
  CHECK(length == 112U);
  CHECK(length <= fanlink::kSerialMaxPayload);
  CHECK(std::strlen(text.data()) == length);
  CHECK(std::string(text.data()).find("a4294967295 S255") != std::string::npos);
  CHECK(fanlink::formatTxNodeDiagnostics(
            8U, node, diagnostics, 0U, text.data(), 8U) == 0U);
  CHECK(text[0] == '\0');
  CHECK(fanlink::formatTxNodeDiagnostics(
            0U, node, diagnostics, 0U, text.data(), text.size()) == 0U);
  CHECK(text[0] == '\0');
  CHECK(fanlink::formatTxNodeDiagnostics(8U, node, diagnostics, 0U, nullptr, 0U) == 0U);
}

void testTxHostFailsafeAtExactTimeout() {
  constexpr std::uint32_t networkId = 0x31415926UL;
  FakeClock clock;
  FakeRadio radio;
  fanlink::TxController tx(
      radio, clock, fanlink::TxControllerConfig{networkId, 500U, 10000U, 5U});
  CHECK(tx.begin());

  std::array<std::uint8_t, fanlink::kNodeCount> duty{};
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

  std::array<std::uint8_t, fanlink::kNodeCount> duty{};
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

  std::array<std::uint8_t, fanlink::kNodeCount> duty{};
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

  std::array<std::uint8_t, fanlink::kNodeCount> duty{};
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

  std::array<std::uint8_t, fanlink::kNodeCount> duty{};
  duty.fill(25U);
  radio.queuePacket(makeControl(networkId, 0x7FFFU, 3U, duty));
  rx.runOnce();
  CHECK(fan.appliedDuty() == 25U);

  const auto historySize = fan.history.size();
  clock.advanceMs(500U);
  duty.fill(90U);
  radio.queuePacket(makeControl(networkId, 0U, 3U, duty));
  rx.runOnce();
  CHECK(fan.history.size() >= historySize + 1U);
  CHECK(fan.history[historySize] == 255U);
  CHECK(fan.appliedDuty() == 255U);
  CHECK(rx.linkFailsafeActive());
  // A restarted TX must advance beyond its first candidate before the RX
  // releases full speed; the old accepted sequence baseline remains intact.
  clock.advanceMs(10U);
  radio.queuePacket(makeControl(networkId, 1U, 3U, duty));
  rx.runOnce();
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

  std::array<std::uint8_t, fanlink::kNodeCount> duty{};
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

  std::array<std::uint8_t, fanlink::kNodeCount> duty{};
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

  std::array<std::uint8_t, fanlink::kNodeCount> duty{};
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

void testTxContinuesAfterInvalidRepliesWithinOneDeadline() {
  constexpr std::uint32_t networkId = 0x01020304U;
  FakeClock clock;
  // The reply window itself crosses the 32-bit micros boundary.
  clock.advanceUs(0xFFFFFFFFU - 1999U);
  FakeRadio radio(&clock);
  fanlink::TxController tx(radio, clock, fanlink::TxControllerConfig{networkId, 500U, 10000U, 5U});
  CHECK(tx.begin());
  const auto startedUs = clock.micros();
  radio.queueFailure(fanlink::RadioResult::CrcError, -7, 1U);
  radio.queuePacket(makeRpm(networkId + 1U, 0U, 1U, 100U), -60, 1U);
  radio.queuePacket(makeRpm(networkId, 9U, 1U, 100U), -60, 1U);
  radio.queuePacket(makeRpm(networkId, 0U, 1U, 3456U), -60, 1U);
  tx.runOneSlot();
  CHECK(tx.nodeTelemetry(1U).rpm == 3456U);
  CHECK(tx.radioDiagnostics().acceptedReplies == 1U);
  CHECK(tx.radioDiagnostics().receiveCrcErrors == 1U);
  CHECK(tx.radioDiagnostics().invalidReplies == 2U);
  CHECK(tx.radioDiagnostics().receiveTimeouts == 0U);
  CHECK(radio.receiveTimeouts == std::vector<std::uint32_t>({5U, 4U, 3U, 2U}));
  CHECK(static_cast<std::uint32_t>(clock.micros() - startedUs) == 10000U);
}

void testTxObservedReplyDeadline() {
  constexpr std::uint32_t networkId = 0x12345678U;
  // Include independent micros and millis rollover. 4999 -> 6501 us must
  // retain observation millisecond 4, never manufacture millisecond 5.
  for (const auto startMs : {0U, 4294967U, 4294967293U}) {
    FakeClock clock;
    clock.advanceMs(startMs);
    const auto startedUs = clock.micros();
    FakeRadio radio(&clock);
    fanlink::TxController tx(radio, clock, fanlink::TxControllerConfig{networkId, 500U, 10000U, 5U});
    CHECK(tx.begin());
    radio.queuePacket(makeRpm(networkId, 0U, 1U, 3456U));
    radio.receives.back().delayUs = 6501U;
    radio.receives.back().receiveObserved = true;
    radio.receives.back().receiveObservedUs = startedUs + 4999U;
    tx.runOneSlot();
    CHECK(tx.nodeTelemetry(1U).seen);
    CHECK(tx.nodeTelemetry(1U).lastSeenMs == static_cast<std::uint32_t>(startMs + 4U));
    CHECK(tx.nodeDiagnostics(1U).processedAfterDeadline == 1U);
  }
  for (const auto start : {0U, 0xFFFFF000U}) {
    FakeClock clock;
    clock.advanceUs(start);
    FakeRadio radio(&clock);
    fanlink::TxController tx(radio, clock, fanlink::TxControllerConfig{networkId, 500U, 10000U, 5U});
    CHECK(tx.begin());
    radio.queuePacket(makeRpm(networkId, 0U, 1U, 3456U), -60, 7U);
    radio.receives.back().receiveObserved = true;
    radio.receives.back().receiveObservedUs = start + 4000U;
    tx.runOneSlot();
    // Packet observed in-window must survive 3 ms of post-observation work,
    // including simulated interrupt load / SPI / RX rearming overhead.
    CHECK(tx.nodeTelemetry(1U).seen);
    CHECK(tx.nodeTelemetry(1U).rpm == 3456U);
    CHECK(tx.nodeDiagnostics(1U).receiveTimeouts == 0U);
    CHECK(tx.nodeDiagnostics(1U).observedReplies == 1U);
    CHECK(tx.nodeDiagnostics(1U).processedAfterDeadline == 1U);
    CHECK(tx.nodeDiagnostics(1U).lastObservedOffsetUs == 4000U);
    CHECK(tx.nodeDiagnostics(1U).lastCompletedOffsetUs == 7000U);
  }
  for (const auto observed : {5000U, 6000U, 0xFFFFFFFFU, 8000U}) {
    FakeClock clock;
    FakeRadio radio(&clock);
    fanlink::TxController tx(radio, clock, fanlink::TxControllerConfig{networkId, 500U, 10000U, 5U});
    CHECK(tx.begin());
    radio.queuePacket(makeRpm(networkId, 0U, 1U, 3456U), -60, 7U);
    radio.receives.back().receiveObserved = true;
    radio.receives.back().receiveObservedUs = observed;
    tx.runOneSlot();
    CHECK(!tx.nodeTelemetry(1U).seen); // late, previous-window or future timestamp
    CHECK(tx.nodeDiagnostics(1U).lateReplies == 1U);
  }
  for (const auto start : {0U, 0xFFFFF000U}) {
    FakeClock clock;
    clock.advanceUs(start);
    FakeRadio radio(&clock);
    fanlink::TxController tx(radio, clock, fanlink::TxControllerConfig{networkId, 500U, 10000U, 5U});
    CHECK(tx.begin());
    radio.queueFailure(fanlink::RadioResult::CrcError, -7);
    radio.receives.back().delayUs = 4250U;
    radio.queuePacket(makeRpm(networkId, 0U, 1U, 3456U));
    radio.receives.back().delayUs = 749U;
    radio.receives.back().receiveObserved = true;
    radio.receives.back().receiveObservedUs = start + 4999U;
    tx.runOneSlot();
    CHECK(tx.nodeTelemetry(1U).seen);
    CHECK(radio.receiveTimeoutsUs == std::vector<std::uint32_t>({5000U, 750U}));
  }
  for (unsigned mismatch = 0U; mismatch < 4U; ++mismatch) {
    FakeClock clock;
    FakeRadio radio(&clock);
    fanlink::TxController tx(radio, clock, fanlink::TxControllerConfig{networkId, 500U, 10000U, 5U});
    CHECK(tx.begin());
    auto packet = makeRpm(mismatch == 1U ? networkId + 1U : networkId,
                          mismatch == 3U ? 1U : 0U, mismatch == 2U ? 2U : 1U, 3456U);
    if (mismatch == 0U) packet.back() ^= 1U;
    radio.queuePacket(packet, -60, 7U);
    radio.receives.back().receiveObserved = true;
    radio.receives.back().receiveObservedUs = 4000U;
    tx.runOneSlot();
    CHECK(!tx.nodeTelemetry(1U).seen);
    CHECK(tx.nodeDiagnostics(1U).invalidReplies == 1U);
    CHECK(static_cast<unsigned>(tx.nodeDiagnostics(1U).lastRejectReason) == mismatch + 1U);
  }
  fanlink::TxNodeDiagnostics maximum{};
  maximum.observedReplies = maximum.lateReplies = maximum.processedAfterDeadline = UINT32_MAX;
  maximum.lastObservedOffsetUs = maximum.lastCompletedOffsetUs = UINT32_MAX;
  maximum.lastRejectReason = fanlink::ReplyRejectReason::LegacyLateReturn;
  char message[129]{};
  const auto length = fanlink::formatTxTimingDiagnostics(8U, maximum, message, sizeof(message));
  CHECK(length > 0U && length <= 128U);
  CHECK(std::string(message).find("RFT n8 d4294967295") == 0U);
  CHECK(fanlink::formatTxTimingDiagnostics(8U, maximum, message, 2U) == 0U);
  CHECK(message[0] == '\0');
}

fanlink::RadioRxEvidence completeErrorEvidence(std::uint32_t start) {
  fanlink::RadioRxEvidence evidence{};
  evidence.irqValid = evidence.packetStatusValid = true;
  evidence.eventObserved = evidence.receiveStarted = true;
  evidence.irq = 0xC070U;
  evidence.packetStatusError = 0x10U;
  evidence.packetStatusSync = 0x01U;
  evidence.errorFlags = fanlink::radio_rx_error::kSyncWithoutRxDone |
      fanlink::radio_rx_error::kIrqCrc | fanlink::radio_rx_error::kPacketCrc |
      fanlink::radio_rx_error::kHeader | fanlink::radio_rx_error::kIrqSync;
  evidence.errorRssiDbm = -26;
  evidence.eventObservedUs = start + 900U;
  evidence.receiveStartedUs = start + 80U;
  return evidence;
}

void testTxReceiveErrorEvidenceAndRetention() {
  constexpr std::uint32_t networkId = 0x01020304U;
  for (const auto start : {0U, 0xFFFFFE00U}) {
    FakeClock clock;
    clock.advanceUs(start);
    FakeRadio radio(&clock);
    fanlink::TxController tx(radio, clock, fanlink::TxControllerConfig{networkId, 500U, 10000U, 5U});
    CHECK(tx.begin());
    CHECK(fanlink::nextTxReceiveErrorDiagnosticId(tx, 0U) == 1U);
    radio.queueFailure(fanlink::RadioResult::CrcError, -7, 1U);
    radio.receives.back().evidence = completeErrorEvidence(start);
    radio.queuePacket(makeRpm(networkId, 0U, 1U, 1400U), -65, 1U);
    radio.receives.back().evidence.receiveStarted = true;
    radio.receives.back().evidence.receiveStartedUs = start + 1200U;
    tx.runOneSlot();
    const auto& diagnostics = tx.nodeDiagnostics(1U);
    const auto& errors = diagnostics.receiveErrors;
    CHECK(errors.events == 1U);
    CHECK(errors.syncWithoutRxDone == 1U && errors.irqCrc == 1U &&
          errors.packetCrc == 1U && errors.header == 1U && errors.irqSync == 1U);
    CHECK(errors.unknown == 0U);
    CHECK(errors.last.kind == 1U && errors.last.sequence == 0U);
    CHECK(errors.last.validity == 15U && errors.last.errorFlags == 31U);
    CHECK(errors.last.irq == 0xC070U && errors.last.packetStatusError == 0x10U &&
          errors.last.packetStatusSync == 1U && errors.last.errorRssiDbm == -26);
    CHECK(errors.last.observedOffsetUs == 900U);
    CHECK(errors.last.receiveStartedOffsetUs == 80U);
    CHECK(diagnostics.lastReceiveStartedOffsetUs == 80U);  // Not retry's 1200.
    CHECK(diagnostics.receiveCrcErrors == 1U && diagnostics.acceptedReplies == 1U);
    CHECK(tx.nodeTelemetry(1U).rpm == 1400U && tx.nodeTelemetry(1U).uplinkRssiDbm == -65);
    CHECK(tx.radioDiagnostics().receiveTimeouts == 0U);

    const auto lastSeen = tx.nodeTelemetry(1U).lastSeenMs;
    while (tx.nextPollId() != 1U) tx.runOneSlot();
    const auto nextStart = clock.micros();
    radio.queueTimeout(5U);
    radio.receives.back().evidence.receiveStarted = true;
    radio.receives.back().evidence.receiveStartedUs = nextStart + 99U;
    tx.runOneSlot();
    CHECK(diagnostics.lastReceiveStartedOffsetUs == 99U);
    CHECK(errors.last.sequence == 0U && errors.last.observedOffsetUs == 900U);
    CHECK(errors.events == 1U);  // Ordinary timeout cannot replace last error.
    CHECK(tx.nodeTelemetry(1U).lastSeenMs == lastSeen);
    CHECK(tx.nodeTelemetry(1U).rpm == 1400U && tx.nodeTelemetry(1U).missedPolls == 1U);

    while (tx.nextPollId() != 1U) tx.runOneSlot();
    radio.queueFailure(fanlink::RadioResult::IoError, -705, 1U);
    const auto expectedSequence = tx.nextSequence();
    tx.runOneSlot();
    CHECK(errors.last.kind == 2U && errors.last.sequence == expectedSequence);
    CHECK(errors.last.validity == 0U && errors.last.errorFlags == 0U);
    CHECK(errors.last.errorRssiDbm == -127);
    CHECK(errors.last.observedOffsetUs == UINT32_MAX &&
          errors.last.receiveStartedOffsetUs == UINT32_MAX);
    CHECK(errors.events == 2U && errors.unknown == 1U);
    CHECK(diagnostics.lastReceiveStartedOffsetUs == UINT32_MAX);
    CHECK(tx.nodeTelemetry(1U).lastSeenMs == lastSeen);
  }
}

void testTxReceiveErrorInvalidMetadataAndSelection() {
  constexpr std::uint32_t networkId = 0x01020304U;
  for (unsigned source = 0U; source < 4U; ++source) {
    FakeClock clock;
    FakeRadio radio(&clock);
    fanlink::TxController tx(radio, clock, fanlink::TxControllerConfig{networkId, 500U, 10000U, 5U});
    CHECK(tx.begin());
    radio.queueFailure(fanlink::RadioResult::CrcError, -7, 2U);
    auto evidence = completeErrorEvidence(0U);
    evidence.irqValid = (source & 1U) != 0U;
    evidence.packetStatusValid = (source & 2U) != 0U;
    evidence.eventObserved = evidence.receiveStarted = false;
    radio.receives.back().evidence = evidence;
    tx.runOneSlot();
    const auto& errors = tx.nodeDiagnostics(1U).receiveErrors;
    CHECK(errors.last.validity == source);
    CHECK(errors.last.errorFlags == ((source & 1U ? 27U : 0U) | (source & 2U ? 4U : 0U)));
    CHECK(errors.unknown == (source == 0U ? 1U : 0U));
    CHECK(errors.irqCrc == (source & 1U ? 1U : 0U));
    CHECK(errors.packetCrc == (source & 2U ? 1U : 0U));
    CHECK(errors.last.errorRssiDbm == (source & 2U ? -26 : -127));
    CHECK(errors.last.observedOffsetUs == UINT32_MAX);
    CHECK(!tx.nodeTelemetry(1U).seen);
    CHECK(tx.nodeTelemetry(1U).uplinkRssiDbm == -127);
    CHECK(fanlink::nextTxNodeDiagnosticId(tx, 0U) == 0U);  // Original rotation unchanged.
    CHECK(fanlink::nextTxReceiveErrorDiagnosticId(tx, 0U) == 1U);
    radio.queueFailure(fanlink::RadioResult::CrcError, -7);
    tx.runOneSlot();
    CHECK(fanlink::nextTxReceiveErrorDiagnosticId(tx, 1U) == 2U);
    CHECK(fanlink::nextTxReceiveErrorDiagnosticId(tx, 2U) == 1U);
    CHECK(fanlink::nextTxReceiveErrorDiagnosticId(tx, 255U) == 1U);
  }
  // Missing, future, previous-slot, and reversed chronology must be unknown.
  for (const auto start : {0U, 0xFFFFFE00U}) {
    for (unsigned invalid = 0U; invalid < 4U; ++invalid) {
      FakeClock clock;
      clock.advanceUs(start);
      FakeRadio radio(&clock);
      fanlink::TxController tx(radio, clock, fanlink::TxControllerConfig{networkId, 500U, 10000U, 5U});
      CHECK(tx.begin());
      radio.queueFailure(fanlink::RadioResult::CrcError, -7, 2U);
      auto evidence = completeErrorEvidence(start);
      if (invalid == 0U) evidence.eventObserved = evidence.receiveStarted = false;
      if (invalid == 1U) evidence.eventObservedUs = evidence.receiveStartedUs = start + 2001U;
      if (invalid == 2U) evidence.eventObservedUs = evidence.receiveStartedUs = start - 1U;
      if (invalid == 3U) evidence.receiveStartedUs = start + 1000U; // After 900 us event.
      radio.receives.back().evidence = evidence;
      tx.runOneSlot();
      const auto& event = tx.nodeDiagnostics(1U).receiveErrors.last;
      CHECK(event.validity == 3U);
      CHECK(event.observedOffsetUs == UINT32_MAX && event.receiveStartedOffsetUs == UINT32_MAX);
      CHECK(tx.nodeDiagnostics(1U).lastReceiveStartedOffsetUs == UINT32_MAX);
      CHECK(!tx.nodeTelemetry(1U).seen);
    }
  }
}

void testTxReceiveErrorCountersAndTextBounded() {
  constexpr std::uint32_t networkId = 0x01020304U;
  FakeClock clock;
  FakeRadio radio(&clock);
  fanlink::TxController tx(radio, clock, fanlink::TxControllerConfig{networkId, 500U, 10000U, 5U});
  CHECK(tx.begin());
  auto& diagnostics = const_cast<fanlink::TxNodeDiagnostics&>(tx.nodeDiagnostics(1U));
  auto& errors = diagnostics.receiveErrors;
  using Counter = std::uint32_t fanlink::TxReceiveErrorDiagnostics::*;
  const std::array<Counter, 7U> counters{{
      &fanlink::TxReceiveErrorDiagnostics::events,
      &fanlink::TxReceiveErrorDiagnostics::syncWithoutRxDone,
      &fanlink::TxReceiveErrorDiagnostics::irqCrc,
      &fanlink::TxReceiveErrorDiagnostics::packetCrc,
      &fanlink::TxReceiveErrorDiagnostics::header,
      &fanlink::TxReceiveErrorDiagnostics::irqSync,
      &fanlink::TxReceiveErrorDiagnostics::unknown}};
  for (const auto counter : counters) errors.*counter = UINT32_MAX - 1U;
  for (unsigned repeat = 0U; repeat < 2U; ++repeat) {
    while (tx.nextPollId() != 1U) tx.runOneSlot();
    radio.queueFailure(fanlink::RadioResult::CrcError, -7, 1U);
    radio.receives.back().evidence = completeErrorEvidence(clock.micros());
    radio.queueFailure(fanlink::RadioResult::CrcError, -7);
    tx.runOneSlot();
    for (const auto counter : counters) CHECK(errors.*counter == UINT32_MAX);
  }
  diagnostics.lastReceiveStartedOffsetUs = UINT32_MAX;
  auto& event = errors.last;
  event.kind = event.validity = event.errorFlags = 255U;
  event.packetStatusError = event.packetStatusSync = 255U;
  event.errorRssiDbm = -128;
  event.sequence = event.irq = UINT16_MAX;
  event.observedOffsetUs = event.receiveStartedOffsetUs = UINT32_MAX;
  for (const auto formatter : {fanlink::formatTxReceiveErrorCounts,
                               fanlink::formatTxReceiveErrorEvent}) {
    std::array<char, 130U> message{};
    const auto length = formatter(8U, diagnostics, message.data(), 129U);
    CHECK(length > 0U && length <= fanlink::kSerialMaxPayload);
    CHECK(std::strlen(message.data()) == length);
    CHECK(fanlink::txDiagnosticFrameCanWrite(length + 10U, 128));
    const auto expected = std::string(message.data());
    for (std::size_t capacity = 1U; capacity <= length; ++capacity) {
      message.fill('Z');
      CHECK(formatter(8U, diagnostics, message.data(), capacity) == 0U);
      CHECK(message[0] == '\0');
      CHECK(message[capacity] == 'Z');
    }
    CHECK(formatter(8U, diagnostics, message.data(), length + 1U) == length);
    CHECK(std::string(message.data()) == expected);
    CHECK(formatter(0U, diagnostics, message.data(), message.size()) == 0U);
    CHECK(message[0] == '\0');
    CHECK(formatter(17U, diagnostics, message.data(), message.size()) == 0U);
    CHECK(formatter(1U, diagnostics, nullptr, 0U) == 0U);
    message[0] = 'Z';
    CHECK(formatter(1U, diagnostics, message.data(), 0U) == 0U);
    CHECK(message[0] == 'Z');
  }
  CHECK(!fanlink::txDiagnosticFrameCanWrite(128U, 128));
  CHECK(fanlink::txDiagnosticFrameCanWrite(127U, 128));
  CHECK(!fanlink::txDiagnosticFrameCanWrite(128U, 127));
  CHECK(!fanlink::txDiagnosticFrameCanWrite(0U, 128));
  CHECK(!fanlink::txDiagnosticFrameCanWrite(20U, -1));
  CHECK(!fanlink::txDiagnosticFrameCanWrite(20U, 0));
}

void testTxRejectsLateReplyAndBoundsNonAdvancingReceive() {
  constexpr std::uint32_t networkId = 0x01020304U;
  FakeClock clock;
  FakeRadio radio(&clock);
  fanlink::TxController tx(radio, clock, fanlink::TxControllerConfig{networkId, 500U, 10000U, 5U});
  CHECK(tx.begin());
  radio.queueFailure(fanlink::RadioResult::IoError, -705);
  tx.runOneSlot();
  // A valid node/sequence arriving exactly at the original deadline is late.
  radio.queuePacket(makeRpm(networkId, 1U, 2U, 3456U), -60, 5U);
  tx.runOneSlot();
  CHECK(!tx.nodeTelemetry(2U).seen);
  CHECK(tx.radioDiagnostics().lastFault == fanlink::TxRadioFault::ReceiveTimeout);
  CHECK(tx.radioDiagnostics().lastDriverError == 0);
  CHECK(tx.radioDiagnostics().lastReceiveDriverError == -705);

  const auto receivesBefore = radio.receiveTimeouts.size();
  for (unsigned i = 0U; i < 40U; ++i) {
    radio.queueFailure(fanlink::RadioResult::CrcError, -7);
  }
  tx.runOneSlot();
  CHECK(radio.receiveTimeouts.size() - receivesBefore == 32U);
  CHECK(tx.radioDiagnostics().receiveCrcErrors == 32U);
  CHECK(tx.nextPollId() == 4U);
  CHECK(clock.millis() == 30U);

  FakeClock deadlineClock;
  FakeRadio deadlineRadio(&deadlineClock);
  fanlink::TxController deadlineTx(
      deadlineRadio, deadlineClock, fanlink::TxControllerConfig{networkId, 500U, 10000U, 5U});
  CHECK(deadlineTx.begin());
  deadlineRadio.queueFailure(fanlink::RadioResult::CrcError, -7, 3U);
  deadlineRadio.queuePacket(makeRpm(networkId, 0U, 1U, 3456U), -60, 2U);
  deadlineTx.runOneSlot();
  CHECK(!deadlineTx.nodeTelemetry(1U).seen);
  CHECK(deadlineRadio.receiveTimeouts == std::vector<std::uint32_t>({5U, 2U}));
  CHECK(deadlineClock.millis() == 10U);
}

void testTxRadioStartupBackoffAndHostAgeAcrossMillisWrap() {
  constexpr std::uint32_t networkId = 0x01020304U;
  FakeClock clock;
  clock.advanceMs(0xFFFFFFFFU - 500U);
  FakeRadio radio(&clock);
  radio.beginSucceeds = false;
  radio.beginDriverError = -705;
  auto config = fanlink::TxControllerConfig{networkId, 500U, 10000U, 5U};
  config.slotPeriodUs = 0U;
  fanlink::TxController tx(radio, clock, config);
  CHECK(!tx.begin());
  CHECK(tx.setNodeDuty(1U, 77U));
  for (const auto delay : {1000U, 2000U, 4000U, 8000U, 8000U}) {
    const auto beginCalls = radio.beginCalls;
    clock.advanceMs(delay - 1U);
    tx.runOneSlot();
    CHECK(radio.beginCalls == beginCalls);
    clock.advanceMs(1U);
    tx.runOneSlot();
    CHECK(radio.beginCalls == beginCalls + 1U);
  }
  CHECK(radio.transmissions.empty());
  CHECK(tx.radioDiagnostics().recoveryFailures == 5U);
  radio.beginSucceeds = true;
  // Even a command fresh before a slow reinitialization must be checked again
  // after begin returns, using its original timestamp rather than refreshing it.
  clock.advanceMs(8000U);
  CHECK(tx.setNodeDuty(1U, 77U));
  radio.beginDelayMs = 600U;
  tx.runOneSlot();
  CHECK(tx.radioDiagnostics().recoveryAttempts == 6U);
  CHECK(tx.radioDiagnostics().recoverySuccesses == 1U);
  fanlink::ControlPacket packet{};
  CHECK(fanlink::decodeControlPacket(radio.transmissions.back().data(),
                                     radio.transmissions.back().size(), packet));
  CHECK(packet.duty[0] == 255U);
  CHECK((packet.flags & fanlink::control_flag::kHostFailsafe) != 0U);
  CHECK(tx.hostFailsafeActive());
}

void testTxRadioIoRecoveryAndEmptyPolls() {
  constexpr std::uint32_t networkId = 0x01020304U;
  for (const bool transmitFault : {false, true}) {
    FakeClock clock;
    FakeRadio radio;
    auto config = fanlink::TxControllerConfig{networkId, 500U, 10000U, 5U};
    config.slotPeriodUs = 0U;
    fanlink::TxController tx(radio, clock, config);
    CHECK(tx.begin());
    radio.transmitFaultNeedsBegin = transmitFault;
    radio.receiveFaultNeedsBegin = !transmitFault;
    for (unsigned i = 0U; i < 4U; ++i) {
      tx.runOneSlot();
    }
    CHECK(radio.beginCalls == 1U);
    CHECK(tx.nextSequence() == 3U);
    clock.advanceMs(999U);
    tx.runOneSlot();
    CHECK(radio.beginCalls == 1U);
    clock.advanceMs(1U);
    tx.noteHostHeartbeat();
    radio.queuePacket(makeRpm(networkId, 3U, 4U, 2400U));
    tx.runOneSlot();
    CHECK(radio.beginCalls == 2U);
    CHECK(tx.radioDiagnostics().recoverySuccesses == 1U);
    CHECK(tx.nodeTelemetry(4U).rpm == 2400U);
  }

  FakeClock clock;
  FakeRadio radio;
  fanlink::TxController tx(radio, clock, fanlink::TxControllerConfig{networkId, 500U, 10000U, 5U});
  CHECK(tx.begin());
  std::array<std::uint8_t, fanlink::kNodeCount> duty{};
  duty.fill(77U);
  for (unsigned slot = 0U; slot < 1200U; ++slot) {
    if (slot % 20U == 0U) {
      tx.setAllDuty(duty);  // Production C6 sends all eight channels every 200 ms.
    }
    if (tx.nextPollId() == 1U) {
      radio.queuePacket(makeRpm(networkId, tx.nextSequence(), 1U, 2400U, 77U));
    }
    tx.runOneSlot();
  }
  CHECK(radio.beginCalls == 1U);
  CHECK(tx.radioDiagnostics().recoveryAttempts == 0U);
  CHECK(tx.radioDiagnostics().acceptedReplies == 150U);
  CHECK(tx.radioDiagnostics().receiveTimeouts == 1050U);
  CHECK(tx.nodeDiagnostics(1U).transmittedPolls == 150U);
  CHECK(tx.nodeDiagnostics(1U).acceptedReplies == 150U);
  CHECK(tx.nodeDiagnostics(1U).receiveTimeouts == 0U);
  for (std::uint8_t nodeId = 2U; nodeId <= fanlink::kNodeCount; ++nodeId) {
    CHECK(tx.nodeDiagnostics(nodeId).transmittedPolls == 150U);
    CHECK(tx.nodeDiagnostics(nodeId).acceptedReplies == 0U);
    CHECK(tx.nodeDiagnostics(nodeId).receiveTimeouts == 150U);
  }
  CHECK(!tx.hostFailsafeActive());
}

void testNoNodesUsesObservationIntervalsInsteadOfResetLoop() {
  constexpr std::uint32_t networkId = 0x01020304U;
  FakeClock txClock;
  FakeRadio txRadio;
  auto txConfig = fanlink::TxControllerConfig{networkId, 500U, 10000U, 5U};
  txConfig.slotPeriodUs = 0U;
  fanlink::TxController tx(txRadio, txClock, txConfig);
  CHECK(tx.begin());
  txClock.advanceMs(4999U);
  tx.runOneSlot();
  CHECK(txRadio.beginCalls == 1U);
  txClock.advanceMs(1U);
  for (unsigned i = 0U; i < 100U; ++i) {
    tx.runOneSlot();
  }
  CHECK(txRadio.beginCalls == 2U);
  txClock.advanceMs(5000U);
  tx.runOneSlot();
  CHECK(txRadio.beginCalls == 3U);

  FakeClock rxClock;
  FakeRadio rxRadio;
  FakeFan fan;
  FakeTach tach;
  fanlink::RxController rx(rxRadio, rxClock, fan, tach,
                           fanlink::RxControllerConfig{networkId});
  CHECK(rx.begin());
  rxClock.advanceMs(1999U);
  rx.runOnce();
  CHECK(rxRadio.beginCalls == 1U);
  rxClock.advanceMs(1U);
  for (unsigned i = 0U; i < 100U; ++i) {
    rx.runOnce();
  }
  CHECK(rxRadio.beginCalls == 2U);
  rxClock.advanceMs(2000U);
  rx.runOnce();
  CHECK(rxRadio.beginCalls == 3U);
  CHECK(fan.appliedDuty() == 255U);
  CHECK(fan.beginCalls == 1U && tach.beginCalls == 1U);
}

void testRxRetriesOnlyRadioAfterStartupFailure() {
  constexpr std::uint32_t networkId = 0x01020304U;
  FakeClock clock;
  clock.advanceMs(0xFFFFFFFFU - 500U);
  FakeRadio radio(&clock);
  FakeFan fan;
  FakeTach tach;
  radio.fanForBeginCheck = &fan;
  radio.beginSucceeds = false;
  fanlink::RxController rx(radio, clock, fan, tach,
                           fanlink::RxControllerConfig{networkId, 1U, 25U, 500U, 0U});
  CHECK(!rx.begin());
  clock.advanceMs(999U);
  rx.runOnce();
  CHECK(radio.beginCalls == 1U);
  clock.advanceMs(1U);
  rx.runOnce();
  CHECK(radio.beginCalls == 2U);
  radio.beginSucceeds = true;
  clock.advanceMs(1999U);
  rx.runOnce();
  CHECK(radio.beginCalls == 2U);
  clock.advanceMs(1U);
  std::array<std::uint8_t, fanlink::kNodeCount> duty{};
  duty.fill(77U);
  radio.queuePacket(makeControl(networkId, 0U, 2U, duty));
  rx.runOnce();
  CHECK(radio.beginCalls == 3U);
  CHECK(radio.dutyAtLastBegin == 255U);
  CHECK(fan.beginCalls == 1U && tach.beginCalls == 1U);
  CHECK(fan.appliedDuty() == 77U);
  CHECK(!rx.linkFailsafeActive());
  CHECK(rx.radioDiagnostics().recoveryAttempts == 2U);
  CHECK(rx.radioDiagnostics().recoveryFailures == 1U);
  CHECK(rx.radioDiagnostics().recoverySuccesses == 1U);
}

void testRxIoRecoveryHoldsFullAndRetainsSequence() {
  constexpr std::uint32_t networkId = 0x01020304U;
  for (const bool transmitFault : {false, true}) {
    FakeClock clock;
    FakeRadio radio;
    FakeFan fan;
    FakeTach tach;
    radio.fanForBeginCheck = &fan;
    fanlink::RxController rx(radio, clock, fan, tach,
                             fanlink::RxControllerConfig{networkId, 1U, 25U, 500U, 0U});
    CHECK(rx.begin());
    std::array<std::uint8_t, fanlink::kNodeCount> duty{};
    duty.fill(77U);
    radio.queuePacket(makeControl(networkId, 100U, 2U, duty));
    rx.runOnce();
    CHECK(fan.appliedDuty() == 77U);
    radio.transmitFaultNeedsBegin = transmitFault;
    radio.receiveFaultNeedsBegin = !transmitFault;
    std::uint16_t lastSequence = 100U;
    for (unsigned i = 0U; i < 3U; ++i) {
      if (transmitFault) {
        radio.queuePacket(makeControl(networkId, ++lastSequence, 1U, duty));
      }
      rx.runOnce();
      if (transmitFault && i < 2U) {
        // Successful downlinks between failed RPM transmissions must not hide
        // three consecutive errors in the reply direction.
        radio.queuePacket(makeControl(networkId, ++lastSequence, 2U, duty));
        rx.runOnce();
      }
    }
    CHECK(rx.linkFailsafeActive());
    CHECK(fan.appliedDuty() == 255U);
    CHECK(radio.beginCalls == 1U);
    clock.advanceMs(1000U);
    radio.queuePacket(makeControl(networkId, lastSequence, transmitFault ? 1U : 2U, duty));
    rx.runOnce();
    CHECK(radio.beginCalls == 2U);
    CHECK(radio.dutyAtLastBegin == 255U);
    CHECK(fan.appliedDuty() == 255U);
    CHECK(rx.linkFailsafeActive());
    CHECK(fan.beginCalls == 1U && tach.beginCalls == 1U);
    CHECK(rx.radioDiagnostics().recoverySuccesses == 1U);
    radio.queuePacket(makeControl(networkId, ++lastSequence, 2U, duty));
    rx.runOnce();
    CHECK(fan.appliedDuty() == 77U);
    CHECK(!rx.linkFailsafeActive());
  }
}

void testRxDuplicatesCannotRefreshWatchdogOrResyncCandidate() {
  constexpr std::uint32_t networkId = 0x01020304U;
  FakeClock clock;
  FakeRadio radio;
  FakeFan fan;
  FakeTach tach;
  fanlink::RxController rx(radio, clock, fan, tach,
                           fanlink::RxControllerConfig{networkId, 1U, 25U, 500U, 0U});
  CHECK(rx.begin());
  std::array<std::uint8_t, fanlink::kNodeCount> duty{};
  duty.fill(77U);
  const auto original = makeControl(networkId, 100U, 1U, duty);
  radio.queuePacket(original);
  rx.runOnce();
  clock.advanceMs(400U);
  radio.queuePacket(original);
  rx.runOnce();
  CHECK(radio.transmissions.size() == 2U);
  CHECK(!rx.linkFailsafeActive());
  clock.advanceMs(100U);
  radio.queuePacket(original);
  rx.runOnce();
  CHECK(rx.linkFailsafeActive());
  CHECK(fan.appliedDuty() == 255U);
  CHECK(radio.transmissions.size() == 2U);
  clock.advanceMs(100U);
  radio.queuePacket(makeControl(networkId, 10U, 1U, duty));
  rx.runOnce();
  CHECK(fan.appliedDuty() == 255U);
  clock.advanceMs(499U);
  radio.queuePacket(makeControl(networkId, 10U, 1U, duty));
  rx.runOnce();
  clock.advanceMs(1U);
  radio.queuePacket(makeControl(networkId, 11U, 1U, duty));
  rx.runOnce();
  CHECK(fan.appliedDuty() == 255U);  // Previous candidate expired at exactly 500 ms.
  clock.advanceMs(10U);
  radio.queuePacket(makeControl(networkId, 10U, 1U, duty));
  rx.runOnce();
  CHECK(fan.appliedDuty() == 255U);
  clock.advanceMs(10U);
  radio.queuePacket(makeControl(networkId, 11U, 1U, duty));
  rx.runOnce();
  CHECK(fan.appliedDuty() == 255U);
  clock.advanceMs(10U);
  radio.queuePacket(makeControl(networkId, 12U, 1U, duty));
  rx.runOnce();
  CHECK(fan.appliedDuty() == 77U);
  CHECK(!rx.linkFailsafeActive());
}

void testRxDuplicateReplyDelayCannotOverrunFailsafe() {
  constexpr std::uint32_t networkId = 0x01020304U;
  FakeClock clock;
  FakeRadio radio;
  FakeFan fan;
  FakeTach tach;
  fanlink::RxController rx(radio, clock, fan, tach,
                           fanlink::RxControllerConfig{networkId});
  CHECK(rx.begin());
  std::array<std::uint8_t, fanlink::kNodeCount> duty{};
  duty.fill(77U);
  const auto original = makeControl(networkId, 100U, 1U, duty);
  radio.queuePacket(original);
  rx.runOnce();
  clock.advanceUs(497800U);  // First reply consumed 1200 us; now 499 ms.
  radio.queuePacket(original);
  rx.runOnce();
  CHECK(rx.linkFailsafeActive());
  CHECK(fan.appliedDuty() == 255U);
  fanlink::RpmPacket reply{};
  CHECK(fanlink::decodeRpmPacket(radio.transmissions.back().data(),
                                 radio.transmissions.back().size(), reply));
  CHECK(reply.appliedDuty == 255U);
  CHECK((reply.status & fanlink::node_status::kLinkFailsafe) != 0U);
}

void testTxRecoveryDiagnosticTextBounded() {
  fanlink::TxRadioDiagnostics diagnostics{};
  diagnostics.recoveryAttempts = std::numeric_limits<std::uint32_t>::max();
  diagnostics.recoverySuccesses = std::numeric_limits<std::uint32_t>::max();
  diagnostics.recoveryFailures = std::numeric_limits<std::uint32_t>::max();
  std::array<char, fanlink::kSerialMaxPayload + 1U> text{};
  const auto length = fanlink::formatTxRadioRecoveryDiagnostics(
      diagnostics, text.data(), text.size());
  CHECK(length > 0U && length <= fanlink::kSerialMaxPayload);
  CHECK(std::string(text.data()) ==
        "RFR attempts=4294967295 successes=4294967295 failures=4294967295");
  std::array<char, 5> tiny{{'x', 'x', 'x', 'x', 'x'}};
  CHECK(fanlink::formatTxRadioRecoveryDiagnostics(diagnostics, tiny.data(), tiny.size()) == 0U);
  CHECK(tiny[0] == '\0');
  CHECK(fanlink::formatTxRadioRecoveryDiagnostics(diagnostics, nullptr, 0U) == 0U);
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
  testNativeEightNodesFixedRfEnvelope();
  testRealHostControlDispatchRejectsWithoutRenewingWatchdog();
  testEightNodeOneSecondDefaultCycle();
  testCooperativePollCadence();
  testTxAcceptsOnlyMatchingReply();
  testTxRadioDiagnostics();
  testTxRadioDiagnosticTextBounded();
  testTxNodeDiagnosticsKeepPollWindowsSeparate();
  testTxNodeDiagnosticCountersSaturate();
  testTxNodeDiagnosticTextFreshnessWrapAndBound();
  testTxHostFailsafeAtExactTimeout();
  testTxRejectsWrongNetworkSequenceAndCrcReplies();
  testRxChannelSelectionPollingAndFailsafe();
  testRxForceFullAndSequenceGap();
  testRxReceiveDeadlineAndFailsafeStatusLatch();
  testRxReacquiresSequenceAfterLinkTimeout();
  testRxInvalidPacketsDoNotRefreshWatchdog();
  testRxRejectsStaleSequenceButAcceptsRetryAndWrap();
  testRxRejectsWrongNetworkAndBadCrc();
  testTxContinuesAfterInvalidRepliesWithinOneDeadline();
  testTxRejectsLateReplyAndBoundsNonAdvancingReceive();
  testTxObservedReplyDeadline();
  testTxReceiveErrorEvidenceAndRetention();
  testTxReceiveErrorInvalidMetadataAndSelection();
  testTxReceiveErrorCountersAndTextBounded();
  testTxRadioStartupBackoffAndHostAgeAcrossMillisWrap();
  testTxRadioIoRecoveryAndEmptyPolls();
  testNoNodesUsesObservationIntervalsInsteadOfResetLoop();
  testRxRetriesOnlyRadioAfterStartupFailure();
  testRxIoRecoveryHoldsFullAndRetainsSequence();
  testRxDuplicatesCannotRefreshWatchdogOrResyncCandidate();
  testRxDuplicateReplyDelayCannotOverrunFailsafe();
  testTxRecoveryDiagnosticTextBounded();

  if (gFailures == 0) {
    std::cout << "PASS: " << gChecks << " checks\n";
    return 0;
  }
  std::cerr << "FAIL: " << gFailures << " of " << gChecks << " checks failed\n";
  return 1;
}
