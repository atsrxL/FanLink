#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>

#include "fanlink/interfaces.hpp"
#include "fanlink/protocol.hpp"
#include "fanlink/rx_controller.hpp"
#include "fanlink/tx_controller.hpp"

namespace {

constexpr std::uint32_t kNetworkId = 0x01020304U;

void require(bool condition, const char* reason) {
  if (!condition) {
    throw std::runtime_error(reason);
  }
}

struct VirtualClock final : fanlink::IClock {
  std::uint64_t nowUs{0};
  std::uint32_t millis() const override { return static_cast<std::uint32_t>(nowUs / 1000U); }
  std::uint32_t micros() const override { return static_cast<std::uint32_t>(nowUs); }
  void delayMilliseconds(std::uint32_t value) override { nowUs += std::uint64_t(value) * 1000U; }
  void delayMicroseconds(std::uint32_t value) override { nowUs += value; }
};

struct FakeFan final : fanlink::IFanOutput {
  std::uint8_t duty{255};
  unsigned beginCalls{0};
  bool begin() override {
    ++beginCalls;
    return true;
  }
  void setDuty(std::uint8_t value) override { duty = value; }
  void setFullSpeed() override { duty = 255U; }
  std::uint8_t appliedDuty() const override { return duty; }
};

struct FakeTach final : fanlink::ITachometer {
  unsigned beginCalls{0};
  bool begin() override {
    ++beginCalls;
    return true;
  }
  fanlink::TachSample sample(std::uint32_t) override { return {1200U, true}; }
};

struct VirtualAir {
  std::array<std::uint8_t, fanlink::kControlPacketSize> down{};
  std::array<std::uint8_t, fanlink::kRpmPacketSize> up{};
  bool haveDown{false};
  bool haveUp{false};
  bool receiverScheduled{false};
};

// A configuration-loss fault persists until begin(). This port deliberately
// does not model SX1280/SPI/interrupts, packet airtime, analog RF or real clocks.
struct FakeRadio final : fanlink::IRadioPort {
  VirtualAir& air;
  VirtualClock& clock;
  bool txSide;
  bool fault{false};
  bool silentFault{false};
  bool allowBegin{true};
  unsigned beginCalls{0};
  int driverError{0};
  fanlink::RxController* receiver{nullptr};
  FakeFan* fan{nullptr};

  FakeRadio(VirtualAir& airValue, VirtualClock& clockValue, bool txSideValue)
      : air(airValue), clock(clockValue), txSide(txSideValue) {}

  bool begin(std::uint32_t networkId) override {
    require(networkId == kNetworkId, "radio network ID changed during recovery");
    ++beginCalls;
    if (fan != nullptr) {
      require(fan->duty == 255U, "RX did not enter full speed before radio begin");
    }
    fault = !allowBegin;
    if (!fault) {
      silentFault = false;
    }
    driverError = fault ? -705 : 0;
    if (txSide) {
      air.haveUp = false;
    } else {
      air.haveDown = false;
    }
    return !fault;
  }

  int lastError() const override { return driverError; }

  fanlink::RadioResult transmit(const std::uint8_t* data, std::size_t length) override {
    if (fault) {
      driverError = -705;
      return fanlink::RadioResult::IoError;
    }
    driverError = 0;
    if (silentFault) {
      return fanlink::RadioResult::Ok;
    }
    if (txSide) {
      require(length == air.down.size(), "control wire length changed");
      std::memcpy(air.down.data(), data, length);
      air.haveDown = true;
      air.haveUp = false;
    } else {
      require(length == air.up.size(), "RPM wire length changed");
      std::memcpy(air.up.data(), data, length);
      air.haveUp = true;
    }
    return fanlink::RadioResult::Ok;
  }

  fanlink::RadioRxResult receive(std::uint8_t* data, std::size_t capacity,
                                std::uint32_t timeoutMs) override {
    if (fault) {
      driverError = -705;
      return {fanlink::RadioResult::IoError, 0U, -127, driverError};
    }
    if (silentFault) {
      if (txSide) {
        clock.delayMilliseconds(timeoutMs);
      }
      driverError = -6;
      return {fanlink::RadioResult::Timeout, 0U, -127, driverError};
    }
    if (txSide && receiver != nullptr) {
      air.receiverScheduled = true;
      receiver->runOnce();
    }
    auto& available = txSide ? air.haveUp : air.haveDown;
    if (available) {
      const auto& packet = txSide ? air.up : air.down;
      require(capacity >= packet.size(), "receive buffer too small");
      std::memcpy(data, packet.data(), packet.size());
      available = false;
      driverError = 0;
      return {fanlink::RadioResult::Ok, packet.size(), -60, 0};
    }
    if (txSide) {
      clock.delayMilliseconds(timeoutMs);
    }
    driverError = -6;
    return {fanlink::RadioResult::Timeout, 0U, -127, driverError};
  }
};

struct Pair {
  VirtualClock clock;
  VirtualAir air;
  FakeRadio txRadio{air, clock, true};
  FakeRadio rxRadio{air, clock, false};
  FakeFan fan;
  FakeTach tach;
  fanlink::RxController rx{rxRadio, clock, fan, tach,
      fanlink::RxControllerConfig{kNetworkId}};
  fanlink::TxController tx{txRadio, clock, fanlink::TxControllerConfig{kNetworkId, 500U, 10000U, 5U}};
  std::array<std::uint8_t, fanlink::kNodeCount> requestedDuty{};
  std::uint32_t lastHostSendMs{0};
  std::uint32_t hostFrames{0};
  bool hostEnabled{true};
  bool hostSent{false};

  Pair() {
    txRadio.receiver = &rx;
    rxRadio.fan = &fan;
    requestedDuty.fill(255U);
    requestedDuty[0] = 87U;
  }

  void begin(bool txStarts = true, bool rxStarts = true) {
    txRadio.allowBegin = txStarts;
    rxRadio.allowBegin = rxStarts;
    require(rx.begin() == rxStarts, "unexpected RX startup result");
    require(tx.begin() == txStarts, "unexpected TX startup result");
  }

  void slot() {
    const auto nowMs = clock.millis();
    if (hostEnabled && (!hostSent || static_cast<std::uint32_t>(nowMs - lastHostSendMs) >= 200U)) {
      // Production FanLinkC6 sends the full 16-byte SET_ALL_DUTY every 200 ms.
      tx.setAllDuty(requestedDuty);
      lastHostSendMs = nowMs;
      hostSent = true;
      ++hostFrames;
    }
    air.receiverScheduled = false;
    tx.runOneSlot();
    if (!air.receiverScheduled) {
      rx.runOnce();  // RX still runs while TX has no usable radio.
    }
  }
};

void testTwelveHoursWithAutomaticRadioRecovery() {
  Pair pair;
  pair.begin();
  constexpr std::uint32_t kSlots = 4320000U;
  constexpr std::uint32_t kRxFaultSlot = 1080000U;
  constexpr std::uint32_t kTxFaultSlot = 2160000U;
  bool rxRecoveryFullObserved = false;
  bool txOutageFullObserved = false;
  unsigned sequenceWraps = 0U;
  for (std::uint32_t slot = 0U; slot < kSlots; ++slot) {
    if (slot == kRxFaultSlot) {
      pair.rxRadio.fault = true;
    }
    if (slot == kTxFaultSlot) {
      pair.txRadio.fault = true;
      pair.txRadio.allowBegin = false;
    }
    if (slot == kTxFaultSlot + 50U) {
      pair.txRadio.allowBegin = true;
    }
    const auto beforeUs = pair.clock.nowUs;
    const auto beforeSequence = pair.tx.nextSequence();
    pair.slot();
    if (pair.tx.nextSequence() < beforeSequence) {
      ++sequenceWraps;
    }
    require(pair.clock.nowUs - beforeUs == 10000U, "virtual slot duration changed at rollover");
    require(!pair.tx.hostFailsafeActive(), "200 ms host cadence became stale");
    if (slot >= kRxFaultSlot && slot < kRxFaultSlot + 400U) {
      rxRecoveryFullObserved = rxRecoveryFullObserved || pair.fan.duty == 255U;
    } else if (slot >= kTxFaultSlot && slot < kTxFaultSlot + 400U) {
      txOutageFullObserved = txOutageFullObserved || pair.fan.duty == 255U;
    } else {
      require(!pair.rx.linkFailsafeActive() && pair.fan.duty == 87U,
              "healthy stream lost control outside injected fault windows");
    }
  }
  require(rxRecoveryFullObserved && txOutageFullObserved, "fault full-speed state not observed");
  require(pair.txRadio.beginCalls == 3U, "unexpected TX recovery attempts");
  require(pair.rxRadio.beginCalls == 3U, "unexpected RX recovery attempts");
  require(pair.tx.radioDiagnostics().recoveryFailures == 1U, "TX failed retry not recorded");
  require(pair.tx.radioDiagnostics().recoverySuccesses == 1U, "TX did not recover automatically");
  require(pair.rx.radioDiagnostics().recoverySuccesses == 2U, "RX did not recover automatically");
  require(pair.fan.beginCalls == 1U && pair.tach.beginCalls == 1U, "peripheral reinitialized");
  require(sequenceWraps == 65U, "unexpected sequence resets or rollover count");
  require(pair.tx.radioDiagnostics().acceptedReplies > 269900U, "excessive simulated reply loss");
  std::cout << "PASS virtual 12h slots=" << kSlots
            << " micros_wraps=" << pair.clock.nowUs / (1ULL << 32U)
            << " control_sequence_wraps=" << sequenceWraps
            << " host_full_state_frames=" << pair.hostFrames
            << " accepted_node1_replies=" << pair.tx.radioDiagnostics().acceptedReplies
            << " TX_begin_calls=" << pair.txRadio.beginCalls
            << " RX_begin_calls=" << pair.rxRadio.beginCalls << '\n';
}

void testFailedStartupRecoversWithoutControllerBegin() {
  for (const bool txFailure : {false, true}) {
    Pair pair;
    pair.begin(!txFailure, txFailure);
    for (unsigned slot = 0U; slot < 300U; ++slot) {
      if (slot == 50U) {
        pair.txRadio.allowBegin = true;
        pair.rxRadio.allowBegin = true;
      }
      pair.slot();
      if (slot < 100U) {
        require(pair.fan.duty == 255U, "startup failure released full speed");
      }
    }
    require(pair.fan.duty == 87U && !pair.rx.linkFailsafeActive(), "startup did not recover");
    require(pair.tx.radioDiagnostics().acceptedReplies > 0U, "startup recovered without replies");
    require(pair.txRadio.beginCalls == (txFailure ? 2U : 1U), "TX startup retry count");
    require(pair.rxRadio.beginCalls == (txFailure ? 1U : 2U), "RX startup retry count");
    require(pair.fan.beginCalls == 1U && pair.tach.beginCalls == 1U, "startup reinitialized Tach");
    std::cout << "PASS " << (txFailure ? "TX" : "RX")
              << " failed-startup automatic recovery; full speed until valid control\n";
  }
}

void testProductionHostLossAndMillisRollover() {
  Pair pair;
  pair.clock.nowUs = (1ULL << 32U) * 1000U - 200000U;
  pair.begin();
  for (unsigned slot = 0U; slot < 200U; ++slot) {
    pair.slot();
    require(pair.fan.duty == 87U && !pair.tx.hostFailsafeActive(), "millis rollover lost control");
  }
  require(pair.txRadio.beginCalls == 1U && pair.rxRadio.beginCalls == 1U, "rollover reset radio");
  pair.hostEnabled = false;
  for (unsigned slot = 0U; slot < 100U; ++slot) {
    pair.slot();
    if (static_cast<std::uint32_t>(pair.clock.millis() - pair.lastHostSendMs) >= 510U) {
      require(pair.fan.duty == 255U, "host timeout failed to broadcast full speed");
    }
  }
  require(pair.tx.hostFailsafeActive() && pair.fan.duty == 255U, "host outage not safe");
  pair.hostEnabled = true;
  pair.slot();
  require(!pair.tx.hostFailsafeActive() && pair.fan.duty == 87U, "new C6 command did not restore control");
  std::cout << "PASS millis rollover and production 200ms/500ms host loss/recovery\n";
}

void testSilentRadioFaultRecoversWithoutIoErrors() {
  for (const bool txFailure : {false, true}) {
    Pair pair;
    pair.begin();
    for (unsigned slot = 0U; slot < 100U; ++slot) {
      pair.slot();
    }
    (txFailure ? pair.txRadio : pair.rxRadio).silentFault = true;
    bool fullSpeedObserved = false;
    for (unsigned slot = 0U; slot < 700U; ++slot) {
      pair.slot();
      fullSpeedObserved = fullSpeedObserved || pair.fan.duty == 255U;
      if (slot >= 600U) {
        require(pair.fan.duty == 87U && !pair.rx.linkFailsafeActive(),
                "silent radio timeout did not recover within observation interval");
      }
    }
    require(fullSpeedObserved, "silent radio outage did not enter full speed");
    require((txFailure ? pair.txRadio : pair.rxRadio).beginCalls == 2U,
            "silent fault did not trigger exactly one recovery on the failed radio");
    require(pair.tx.radioDiagnostics().transmitFailures == 0U &&
                pair.tx.radioDiagnostics().receiveIoErrors == 0U &&
                pair.rx.radioDiagnostics().receiveIoErrors == 0U,
            "silent fault incorrectly depended on an I/O error");
    require(pair.fan.beginCalls == 1U && pair.tach.beginCalls == 1U,
            "silent fault reinitialized peripherals");
    std::cout << "PASS " << (txFailure ? "TX" : "RX")
              << " silent radio timeout model: automatic recovery without I/O errors\n";
  }
}

}  // namespace

int main() {
  try {
    testTwelveHoursWithAutomaticRadioRecovery();
    testFailedStartupRecoversWithoutControllerBegin();
    testProductionHostLossAndMillisRollover();
    testSilentRadioFaultRecoversWithoutIoErrors();
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  std::cout << "Virtual-clock controller tests only; real RF/PWM endurance remains unverified.\n";
}
