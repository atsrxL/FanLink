#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "fanlink/rx_controller.hpp"

namespace {
unsigned checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
  std::cerr << "FAIL line " << __LINE__ << ": " #condition << '\n'; \
  std::exit(1); } } while (false)

struct Clock : fanlink::IClock {
  std::uint64_t us{1000000};
  std::uint32_t millis() const override { return static_cast<std::uint32_t>(us / 1000U); }
  std::uint32_t micros() const override { return static_cast<std::uint32_t>(us); }
  void delayMilliseconds(std::uint32_t v) override { us += v * 1000ULL; }
  void delayMicroseconds(std::uint32_t v) override { us += v; }
};
struct Fan : fanlink::IFanOutput {
  Clock& clock;
  std::uint32_t writeUs{0};
  std::uint8_t duty{255};
  explicit Fan(Clock& c) : clock(c) {}
  bool begin() override { return true; }
  void setDuty(std::uint8_t v) override { duty = v; clock.us += writeUs; }
  void setFullSpeed() override { duty = 255; }
  std::uint8_t appliedDuty() const override { return duty; }
};
struct Tach : fanlink::ITachometer {
  bool begin() override { return true; }
  fanlink::TachSample sample(std::uint32_t) override { return {1234, true}; }
};
struct Radio : fanlink::IRadioPort {
  Clock& clock;
  std::array<std::uint8_t, fanlink::kControlPacketSize> bytes{};
  std::array<std::uint8_t, fanlink::kRpmPacketSize> reply{};
  std::uint32_t processingUs{0}, observedUs{0}, txCalledUs{0};
  unsigned txCalls{0};
  bool queued{false}, metadata{true}, failTransmit{false};
  explicit Radio(Clock& c) : clock(c) {}
  bool begin(std::uint32_t) override { return true; }
  int lastError() const override { return failTransmit ? -7 : 0; }
  fanlink::RadioResult transmit(const std::uint8_t* data, std::size_t len) override {
    CHECK(len == reply.size());
    std::copy(data, data + len, reply.begin());
    ++txCalls;
    txCalledUs = clock.micros();
    return failTransmit ? fanlink::RadioResult::IoError : fanlink::RadioResult::Ok;
  }
  fanlink::RadioRxResult receive(std::uint8_t* data, std::size_t,
                                 std::uint32_t timeoutMs) override {
    if (!queued) {
      clock.delayMilliseconds(timeoutMs);
      return {fanlink::RadioResult::Timeout, 0, -127, -6};
    }
    queued = false;
    observedUs = clock.micros();
    clock.us += processingUs;  // FIFO/metadata/rearm under injected CPU load.
    std::copy(bytes.begin(), bytes.end(), data);
    return {fanlink::RadioResult::Ok, bytes.size(), -42, 0, metadata, observedUs};
  }
  void queue(std::uint16_t seq = 1, std::uint8_t poll = 1,
             std::uint8_t duty = 64, std::uint32_t network = 42) {
    fanlink::ControlPacket packet{};
    packet.networkId = network; packet.sequence = seq; packet.pollId = poll;
    packet.duty.fill(255); packet.duty[0] = duty;
    CHECK(fanlink::encodeControlPacket(packet, bytes)); queued = true;
  }
};
struct Fixture {
  Clock clock;
  Fan fan{clock};
  Tach tach;
  Radio radio{clock};
  fanlink::RxController rx{radio, clock, fan, tach, fanlink::RxControllerConfig{42}};
  Fixture() { CHECK(rx.begin()); }
};

void testTurnaround() {
  // These are injected delays, NOT measurements of real PWM interrupt cost.
  for (auto duty : {0, 1, 64, 128, 192, 254, 255}) {
    for (auto readUs : {0U, 400U, 1300U, 2500U, 3600U}) {
      Fixture f;
      f.radio.processingUs = readUs;
      f.fan.writeUs = duty == 0 || duty == 255 ? 0U : 700U;
      f.radio.queue(1, 1, static_cast<std::uint8_t>(duty));
      f.rx.runOnce();
      CHECK(f.radio.txCalls == 1);
      const auto age = f.radio.txCalledUs - f.radio.observedUs;
      CHECK(age == std::max(1200U, readUs + f.fan.writeUs));
      CHECK(f.rx.radioDiagnostics().lastReplyCallAgeUs == age);
      CHECK(f.rx.radioDiagnostics().maxReplyCallAgeUs == age);
      CHECK(f.rx.radioDiagnostics().ownPolls == 1);
      CHECK(f.rx.radioDiagnostics().replyAttempts == 1);
      CHECK(f.rx.radioDiagnostics().replySuccesses == 1);
      fanlink::RpmPacket reply{};
      CHECK(fanlink::decodeRpmPacket(f.radio.reply.data(), f.radio.reply.size(), reply));
      CHECK(reply.appliedDuty == duty);
      CHECK(reply.sequenceEcho == 1 && reply.nodeId == 1);
    }
  }
  Fixture wrap;
  wrap.clock.us = 0xfffffff0ULL;
  wrap.radio.processingUs = 800; wrap.fan.writeUs = 600;
  wrap.radio.queue(); wrap.rx.runOnce();
  CHECK(wrap.radio.txCalledUs - wrap.radio.observedUs == 1400U);
  CHECK(wrap.rx.radioDiagnostics().lateReplyCalls == 1);

  Fixture legacy;
  legacy.radio.metadata = false;
  legacy.radio.processingUs = 800; legacy.fan.writeUs = 600;
  legacy.radio.queue(); legacy.rx.runOnce();
  CHECK(legacy.radio.txCalledUs - legacy.radio.observedUs == 2600U);
  CHECK(legacy.rx.radioDiagnostics().maxReplyCallAgeUs == 0);
}

void testRejectAndSafety() {
  Fixture f;
  f.radio.processingUs = 4300; f.fan.writeUs = 700;
  f.radio.queue(); f.rx.runOnce();
  CHECK(f.fan.duty == 64);
  CHECK(f.radio.txCalls == 0);
  CHECK(f.rx.radioDiagnostics().expiredRepliesSkipped == 1);
  CHECK(f.rx.radioDiagnostics().replyAttempts == 0);
  // Last watchdog timestamp is the observation, not FIFO-return/application time.
  f.clock.us = 1500000; f.rx.runOnce();
  CHECK(f.fan.duty == 255 && f.rx.linkFailsafeActive());

  Fixture expired;
  expired.radio.processingUs = 500000;
  expired.radio.queue(); expired.rx.runOnce();
  CHECK(expired.fan.duty == 255 && expired.rx.linkFailsafeActive());
  CHECK(expired.radio.txCalls == 0);
  CHECK(expired.rx.radioDiagnostics().expiredControls == 1);

  Fixture pwmStall;
  pwmStall.fan.writeUs = 500000;
  pwmStall.radio.queue(); pwmStall.rx.runOnce();
  CHECK(pwmStall.fan.duty == 255 && pwmStall.rx.linkFailsafeActive());
  CHECK(pwmStall.radio.txCalls == 0);

  Fixture reject;
  reject.radio.queue(100, 2); reject.rx.runOnce();
  CHECK(reject.radio.txCalls == 0 && reject.rx.radioDiagnostics().ownPolls == 0);
  reject.radio.queue(99); reject.rx.runOnce();
  CHECK(reject.rx.radioDiagnostics().staleControls == 1);
  reject.radio.queue(101, 1, 64, 43); reject.rx.runOnce();
  CHECK(reject.rx.radioDiagnostics().wrongNetworkControls == 1);
  reject.radio.queue(101); reject.radio.bytes[0] ^= 1; reject.rx.runOnce();
  CHECK(reject.rx.radioDiagnostics().malformedControls == 1);
  CHECK(reject.radio.txCalls == 0);
  reject.radio.failTransmit = true;
  reject.radio.queue(101); reject.rx.runOnce();
  CHECK(reject.rx.radioDiagnostics().replyAttempts == 1);
  CHECK(reject.rx.radioDiagnostics().replyFailures == 1);
  CHECK(reject.rx.radioDiagnostics().replySuccesses == 0);
  CHECK(reject.rx.radioDiagnostics().transmitErrors == 1);

  Fixture duplicate;
  duplicate.radio.queue(100); duplicate.rx.runOnce();
  duplicate.clock.us = 1498000;
  duplicate.radio.queue(100); duplicate.rx.runOnce();
  CHECK(duplicate.radio.txCalls == 2);  // Healthy identical retry remains valid.
  duplicate.clock.us = 1500000; duplicate.rx.runOnce();
  CHECK(duplicate.fan.duty == 255 && duplicate.rx.linkFailsafeActive());
  duplicate.radio.queue(100); duplicate.rx.runOnce();
  CHECK(duplicate.radio.txCalls == 2);  // Retry cannot release expired control.
  CHECK(duplicate.fan.duty == 255);

  Clock clock;
  Fan fan(clock); Tach tach; Radio radio(clock);
  fanlink::RxControllerConfig badConfig{42};
  badConfig.replyMaxAgeUs = badConfig.replyDelayUs;
  fanlink::RxController invalid(radio, clock, fan, tach, badConfig);
  CHECK(!invalid.begin()); invalid.runOnce();
  CHECK(fan.duty == 255 && radio.txCalls == 0);
}
}  // namespace

int main() {
  testTurnaround(); testRejectAndSafety();
  std::cout << "PASS: " << checks << " RX reply timing checks (injected delays, no hardware)\n";
}
