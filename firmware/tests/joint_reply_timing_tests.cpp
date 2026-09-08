// Deterministic load injection, NOT measurements of PWM NMI, SPI or RF airtime.
// Two independent MCU clocks execute the real controllers through fake air.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include "fanlink/rx_controller.hpp"
#include "fanlink/tx_controller.hpp"

namespace {
unsigned checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { std::cerr << "FAIL " << __LINE__ \
  << ": " #c << '\n'; std::exit(1); } } while (false)
struct Clock : fanlink::IClock {
  std::uint64_t us{1000000};
  std::uint32_t micros() const override { return static_cast<std::uint32_t>(us); }
  std::uint32_t millis() const override { return static_cast<std::uint32_t>(us / 1000); }
  void delayMicroseconds(std::uint32_t n) override { us += n; }
  void delayMilliseconds(std::uint32_t n) override { us += n * 1000ULL; }
};
struct Fan : fanlink::IFanOutput {
  Clock& clock;
  std::uint8_t duty{255};
  std::uint32_t pwmWriteUs{0};
  explicit Fan(Clock& c) : clock(c) {}
  bool begin() override { return true; }
  void setDuty(std::uint8_t n) override {
    duty = n;
    if (n != 0 && n != 255) clock.us += pwmWriteUs;
  }
  void setFullSpeed() override { duty = 255; }
  std::uint8_t appliedDuty() const override { return duty; }
};
struct Tach : fanlink::ITachometer {
  bool begin() override { return true; }
  fanlink::TachSample sample(std::uint32_t) override { return {1234, true}; }
};
enum class Corruption { None, Node, Sequence, Network, AppCrc, RadioCrc };
struct Air {
  Clock txClock, rxClock;
  std::array<std::uint8_t, fanlink::kControlPacketSize> control{};
  std::array<std::uint8_t, fanlink::kRpmPacketSize> reply{};
  std::uint64_t controlReady{0}, replyReady{0};
  // All numbers below are arbitrary injected microseconds, not board claims.
  std::uint32_t downReadUs{400}, rxPrepareUs{300}, uplinkUs{200};
  std::uint32_t txReadUs{200}, observerStallUs{0};
  unsigned sends{0}, replies{0};
  bool pendingControl{false}, pendingReply{false}, dropDownlink{false};
  Corruption corruption{Corruption::None};
  fanlink::RxController* rx{nullptr};
};
struct RxPort : fanlink::IRadioPort {
  Air& air;
  explicit RxPort(Air& a) : air(a) {}
  bool begin(std::uint32_t) override { return true; }
  int lastError() const override { return 0; }
  fanlink::RadioRxResult receive(std::uint8_t* data, std::size_t cap,
                                std::uint32_t timeoutMs) override {
    if (!air.pendingControl) {
      air.rxClock.delayMilliseconds(timeoutMs);
      return {fanlink::RadioResult::Timeout, 0, -127, -6};
    }
    CHECK(cap >= air.control.size());
    air.pendingControl = false;
    air.rxClock.us = std::max(air.rxClock.us, air.controlReady);
    auto observed = air.rxClock.micros();
    air.rxClock.us += air.downReadUs;
    std::copy(air.control.begin(), air.control.end(), data);
    return {fanlink::RadioResult::Ok, air.control.size(), -42, 0, true, observed};
  }
  fanlink::RadioResult transmit(const std::uint8_t* data, std::size_t len) override {
    CHECK(len == air.reply.size());
    std::copy(data, data + len, air.reply.begin());
    fanlink::RpmPacket p{};
    CHECK(fanlink::decodeRpmPacket(data, len, p));
    if (air.corruption == Corruption::Node) p.nodeId = p.nodeId == 1 ? 2 : 1;
    if (air.corruption == Corruption::Sequence) ++p.sequenceEcho;
    if (air.corruption == Corruption::Network) ++p.networkId;
    CHECK(fanlink::encodeRpmPacket(p, air.reply));
    if (air.corruption == Corruption::AppCrc) air.reply[12] ^= 1;
    air.rxClock.us += air.rxPrepareUs + air.uplinkUs;
    air.replyReady = air.rxClock.us;
    air.pendingReply = true;
    ++air.replies;
    return fanlink::RadioResult::Ok;
  }
};
struct TxPort : fanlink::IRadioPort {
  Air& air;
  explicit TxPort(Air& a) : air(a) {}
  bool begin(std::uint32_t) override { return true; }
  int lastError() const override { return 0; }
  fanlink::RadioResult transmit(const std::uint8_t* data, std::size_t len) override {
    CHECK(len == air.control.size());
    std::copy(data, data + len, air.control.begin());
    air.txClock.us += 200;  // Injected downlink airtime / TX completion.
    air.controlReady = air.txClock.us;
    air.pendingControl = !air.dropDownlink;
    air.pendingReply = false; // Slot transmit discards obsolete receive state.
    ++air.sends;
    return fanlink::RadioResult::Ok;
  }
  fanlink::RadioRxResult receive(std::uint8_t* d, std::size_t c,
                                std::uint32_t ms) override {
    return receiveForUs(d, c, ms * 1000U);
  }
  fanlink::RadioRxResult receiveForUs(std::uint8_t* data, std::size_t cap,
                                     std::uint32_t budget) override {
    if (air.pendingControl) air.rx->runOnce();
    if (!air.pendingReply) {
      air.txClock.us += budget;
      return {fanlink::RadioResult::Timeout, 0, -127, -6};
    }
    CHECK(cap >= air.reply.size());
    air.pendingReply = false;
    // Observer stall models scheduling after DIO-ready. It intentionally can
    // overrun budget, requiring the controller to reject a late observation.
    air.txClock.us = std::max(air.txClock.us, air.replyReady);
    air.txClock.us += air.observerStallUs;
    const auto observed = air.txClock.micros();
    air.txClock.us += air.txReadUs;
    std::copy(air.reply.begin(), air.reply.end(), data);
    return {air.corruption == Corruption::RadioCrc ? fanlink::RadioResult::CrcError
                                                 : fanlink::RadioResult::Ok,
            air.reply.size(), -40, 0, true, observed};
  }
};
struct Fixture {
  Air air;
  Fan fan{air.rxClock};
  Tach tach;
  RxPort rxPort{air};
  TxPort txPort{air};
  fanlink::RxController rx;
  fanlink::TxController tx{txPort, air.txClock, fanlink::TxControllerConfig{42, 500U, 10000U, 5U}};
  explicit Fixture(std::uint8_t node = 1)
      : rx(rxPort, air.rxClock, fan, tach, fanlink::RxControllerConfig{42, node}) {
    air.rx = &rx;
    CHECK(rx.begin()); CHECK(tx.begin());
  }
  void command(std::uint8_t duty) {
    std::array<std::uint8_t, fanlink::kNodeCount> duties{};
    duties.fill(duty); tx.setAllDuty(duties);
  }
};

void matrix() {
  for (auto duty : {0, 1, 64, 128, 192, 254, 255}) {
    for (auto read : {400U, 1300U, 2500U, 3600U}) {
      Fixture f;
      f.air.downReadUs = read;
      f.fan.pwmWriteUs = 700;
      f.air.txReadUs = 4200; // Valid observation; completion exceeds 5 ms.
      f.command(static_cast<std::uint8_t>(duty)); f.tx.runOneSlot();
      const auto callAge = std::max(1200U, read + (duty == 0 || duty == 255 ? 0U : 700U));
      CHECK(f.rx.radioDiagnostics().lastReplyCallAgeUs == callAge);
      CHECK(f.tx.nodeDiagnostics(1).lastObservedOffsetUs == callAge + 500U);
      CHECK(f.tx.nodeDiagnostics(1).lastCompletedOffsetUs == callAge + 4700U);
      CHECK(f.tx.nodeDiagnostics(1).processedAfterDeadline == 1);
      CHECK(f.tx.nodeTelemetry(1).seen);
      CHECK(f.tx.nodeTelemetry(1).appliedDuty == duty);
      CHECK(f.rx.appliedDuty() == duty);
      // At the heaviest intermediate-duty load, adding a fresh 1200 us after
      // processing would observe at 6000 us; remaining-delay observes at 4800.
      if (read == 3600 && duty != 0 && duty != 255) {
        CHECK(read + 700U + 1200U + 500U >= 5000U);
        CHECK(f.tx.nodeDiagnostics(1).lastObservedOffsetUs == 4800U);
      }
    }
  }
}
void boundariesAndRejects() {
  for (auto observed : {4999U, 5000U, 5001U}) {
    Fixture f;
    f.air.rxPrepareUs = observed - 1400U;
    f.command(64); f.tx.runOneSlot();
    CHECK(f.tx.nodeTelemetry(1).seen == (observed < 5000));
    CHECK(f.tx.nodeDiagnostics(1).lateReplies == (observed >= 5000 ? 1U : 0U));
  }
  Fixture wrap;
  wrap.air.txClock.us = wrap.air.rxClock.us = 0xffffff00ULL;
  wrap.air.txReadUs = 4200;
  wrap.command(64); wrap.tx.runOneSlot();
  CHECK(wrap.tx.nodeTelemetry(1).seen);
  CHECK(wrap.tx.nodeDiagnostics(1).lastObservedOffsetUs == 1700);
  Fixture stall;
  stall.air.observerStallUs = 3300;
  stall.command(64); stall.tx.runOneSlot();
  CHECK(!stall.tx.nodeTelemetry(1).seen);
  CHECK(stall.tx.nodeDiagnostics(1).lastRejectReason == fanlink::ReplyRejectReason::LateObserved);
  for (auto c : {Corruption::Node, Corruption::Sequence, Corruption::Network,
                 Corruption::AppCrc, Corruption::RadioCrc}) {
    Fixture f;
    f.air.corruption = c; f.air.txReadUs = 4200;
    f.command(64); f.tx.runOneSlot();
    CHECK(!f.tx.nodeTelemetry(1).seen);
    CHECK(f.tx.radioDiagnostics().acceptedReplies == 0);
    CHECK(f.tx.radioDiagnostics().invalidReplies == (c == Corruption::RadioCrc ? 0U : 1U));
    CHECK(f.tx.radioDiagnostics().receiveCrcErrors == (c == Corruption::RadioCrc ? 1U : 0U));
  }
}
void pollingAndSafety() {
  for (unsigned node = 1; node <= fanlink::kNodeCount; ++node) {
    Fixture f(static_cast<std::uint8_t>(node));
    f.command(64);
    for (unsigned poll = 1; poll <= fanlink::kNodeCount; ++poll) {
      CHECK(f.tx.nextPollId() == poll); f.tx.runOneSlot();
      CHECK(f.rx.appliedDuty() == 64);
    }
    CHECK(f.air.sends == fanlink::kNodeCount && f.air.replies == 1);
    CHECK(f.tx.nodeTelemetry(static_cast<std::uint8_t>(node)).seen);
    CHECK(f.tx.nextPollId() == 1 && f.tx.nextSequence() == fanlink::kNodeCount);
    CHECK(f.rx.radioDiagnostics().ownPolls == 1);
    // Host loss produces full-state full speed downlink, not just a display flag.
    f.air.txClock.us += 500000; f.tx.runOneSlot();
    CHECK(f.tx.hostFailsafeActive()); CHECK(f.rx.appliedDuty() == 255);
  }
  Fixture lost;
  lost.command(64); lost.tx.runOneSlot();
  lost.air.rxClock.us += 500000;
  lost.rx.runOnce();
  CHECK(lost.rx.linkFailsafeActive() && lost.rx.appliedDuty() == 255);
  Fixture overload;
  overload.air.downReadUs = 4300; overload.fan.pwmWriteUs = 700;
  overload.command(64); overload.tx.runOneSlot();
  CHECK(overload.rx.appliedDuty() == 64); // Control works, but already late reply is skipped.
  CHECK(overload.rx.radioDiagnostics().expiredRepliesSkipped == 1);
  CHECK(overload.air.replies == 0 && !overload.tx.nodeTelemetry(1).seen);
}
} // namespace
int main() {
  matrix(); boundariesAndRejects(); pollingAndSafety();
  std::cout << "PASS: " << checks << " joint TX/RX checks; assumed load injection, NOT hardware measurements\n";
}
