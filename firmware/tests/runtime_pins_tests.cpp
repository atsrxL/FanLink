#include <cassert>
#include <cstdlib>
#include <iostream>
#include "fanlink/platform/esp8266/runtime_rx_pins.hpp"
static int expectedPwm = -1, expectedTach = -1, expectedOff = 0;
static unsigned pwmCalls = 0, tachCalls = 0, pinCalls = 0;
void digitalWrite(int pin, int value) { assert(pin == expectedPwm && value == expectedOff); }
void pinMode(int pin, int mode) {
  ++pinCalls;
  assert((pin == expectedPwm && mode == OUTPUT) || (pin == expectedTach && mode == INPUT_PULLUP));
}
void analogWriteRange(std::uint32_t range) { assert(range == 255); }
void analogWriteFreq(std::uint32_t frequency) { assert(frequency == 25000); }
void analogWrite(int pin, int value) { assert(pin == expectedPwm && value >= 0 && value <= 255); ++pwmCalls; }
int digitalPinToInterrupt(int pin) { assert(pin == expectedTach); return pin; }
void attachInterrupt(int pin, void (*)(), int mode) { assert(pin == expectedTach && mode == FALLING); ++tachCalls; }
std::uint32_t micros() { return 0; }
void noInterrupts() {}
void interrupts() {}
int main(int argc, char** argv) {
  assert(argc == 3);
  const int modeValue = std::atoi(argv[1]);
  const bool polarity = std::atoi(argv[2]) != 0;
  const auto mode = static_cast<fanlink::settings::PinMode>(modeValue);
  if (modeValue == 1) { expectedPwm = 1; expectedTach = 3; }
  if (modeValue == 2) { expectedPwm = 3; expectedTach = 1; }
  expectedOff = polarity ? 0 : 1;
  using namespace fanlink::platform::esp8266;
  RuntimeFanOutput fan(25000,polarity);
  RuntimeTachometer tach(2,1000000,250,30000);
  fan.select(mode); tach.select(mode);
  assert(fan.begin() && tach.begin());
  // Live selection cannot remux active GPIOs; reboot-only configuration.
  fan.select(fanlink::settings::PinMode::Uart); tach.select(fanlink::settings::PinMode::Uart);
  for (unsigned duty = 0; duty <= 255; ++duty) {
    fan.setDuty(duty);
    assert(fan.appliedDuty() == (expectedPwm < 0 ? 255 : duty));
  }
  fan.setFullSpeed(); assert(fan.appliedDuty() == 255);
  assert(!tach.sample(0).valid);
  if (expectedPwm < 0) assert(pinCalls == 0 && pwmCalls == 0 && tachCalls == 0);
  else assert(pinCalls == 2 && pwmCalls == 257 && tachCalls == 1);
  std::cout << "PASS: runtime pin mode " << modeValue << " polarity " << polarity << '\n';
}
