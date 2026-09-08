#include <Arduino.h>

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "fanlink/platform/esp8266/fan_output.hpp"

namespace {

enum class Operation { Digital, Mode, Range, Frequency, Pwm };

struct Call {
  Operation operation;
  int pin;
  std::uint32_t value;
};

std::vector<Call> calls;
unsigned checks = 0;

void check(bool condition, const char* message) {
  ++checks;
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void checkCall(std::size_t index, Operation operation, int pin, std::uint32_t value) {
  check(index < calls.size(), "missing Arduino call");
  const auto& call = calls[index];
  check(call.operation == operation && call.pin == pin && call.value == value,
        "unexpected Arduino call/order/value");
}

void checkSafeBegin(bool activeHigh, std::uint8_t pin) {
  check(calls.size() == 5U, "begin must really establish GPIO and PWM full speed");
  checkCall(0, Operation::Digital, pin, activeHigh ? LOW : HIGH);
  checkCall(1, Operation::Mode, pin, OUTPUT);
  checkCall(2, Operation::Range, -1, 255U);
  checkCall(3, Operation::Frequency, -1, 25000U);
  checkCall(4, Operation::Pwm, pin, activeHigh ? 0U : 255U);
}

void testPolarity(bool activeHigh, std::uint8_t pin) {
  using fanlink::platform::esp8266::Esp8266FanOutput;
  Esp8266FanOutput fan(pin, 25000U, activeHigh);
  calls.clear();
  check(fan.appliedDuty() == 255U, "initial logical state must be full speed");
  fan.setDuty(83U);
  check(calls.empty(), "uninitialized output must not touch GPIO/PWM");

  check(fan.begin(), "valid begin failed");
  checkSafeBegin(activeHigh, pin);
  check(fan.appliedDuty() == 255U, "begin must replace a pre-init duty request with full speed");
  calls.clear();
  fan.setFullSpeed();
  fan.setDuty(255U);
  check(calls.empty(), "unchanged full speed should be idempotent");

  // Every changed duty writes synchronously, including both static endpoints.
  for (unsigned duty = 0; duty <= 255; ++duty) {
    const auto logical = static_cast<std::uint8_t>(duty);
    calls.clear();
    fan.setDuty(logical);
    check(fan.appliedDuty() == logical, "logical duty changed by gate polarity");
    check(calls.size() == 1U, "changed duty must immediately write exactly once");
    checkCall(0, Operation::Pwm, pin, activeHigh ? 255U - duty : duty);
    for (unsigned repeat = 0; repeat < 16; ++repeat) {
      fan.setDuty(logical);
    }
    check(calls.size() == 1U, "duplicate duty rebuilt PWM state");
  }

  fan.setDuty(0U);
  calls.clear();
  check(fan.begin(), "repeated begin failed");
  checkSafeBegin(activeHigh, pin);
  check(fan.appliedDuty() == 255U, "repeated begin did not force full speed");

  // Even an already-full-speed cache must never suppress initialization.
  calls.clear();
  check(fan.begin(), "full-speed repeated begin failed");
  checkSafeBegin(activeHigh, pin);
}

void testInvalid(int pin, std::uint32_t frequency) {
  using fanlink::platform::esp8266::Esp8266FanOutput;
  Esp8266FanOutput fan(pin, frequency, true);
  calls.clear();
  check(!fan.begin(), "invalid configuration accepted");
  fan.setDuty(0U);
  fan.setDuty(83U);
  fan.setFullSpeed();
  check(calls.empty(), "invalid configuration attempted hardware access");
  check(fan.appliedDuty() == 255U, "full-speed request lost after failed begin");
  check(!fan.begin(), "invalid repeated begin accepted");
  check(calls.empty(), "invalid repeated begin attempted hardware access");
}

}  // namespace

void digitalWrite(std::uint8_t pin, std::uint8_t value) {
  calls.push_back({Operation::Digital, pin, value});
}
void pinMode(std::uint8_t pin, std::uint8_t mode) {
  calls.push_back({Operation::Mode, pin, mode});
}
void analogWriteRange(std::uint32_t range) {
  calls.push_back({Operation::Range, -1, range});
}
void analogWriteFreq(std::uint32_t frequency) {
  calls.push_back({Operation::Frequency, -1, frequency});
}
void analogWrite(std::uint8_t pin, int value) {
  calls.push_back({Operation::Pwm, pin, static_cast<std::uint32_t>(value)});
}

int main() {
  testPolarity(false, 3U);  // Node 01 gate polarity.
  testPolarity(true, 1U);   // Generic/default gate polarity.
  testInvalid(-1, 25000U);
  testInvalid(17, 25000U);
  testInvalid(3, 0U);
  std::cout << "PASS: fan output " << checks << " checks (real platform source, Arduino call recorder)\n";
}
