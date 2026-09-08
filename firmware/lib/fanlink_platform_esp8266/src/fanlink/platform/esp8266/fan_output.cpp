#include "fanlink/platform/esp8266/fan_output.hpp"

#include <Arduino.h>

namespace fanlink::platform::esp8266 {

bool Esp8266FanOutput::begin() {
  initialized_ = false;
  outputApplied_ = false;
  if (pin_ < 0 || pin_ > 16 || frequencyHz_ == 0U) {
    return false;
  }

  // Establish the hardware-safe full-speed state before enabling PWM.
  digitalWrite(pin_, gateActiveHigh_ ? LOW : HIGH);
  pinMode(pin_, OUTPUT);
  analogWriteRange(255U);
  analogWriteFreq(frequencyHz_);
  initialized_ = true;
  setFullSpeed();
  return true;
}

void Esp8266FanOutput::setDuty(std::uint8_t duty) {
  // Repeated full-state downlinks need not rebuild Arduino's PWM/NMI state.
  // Only skip after a real write: begin() must always establish full speed.
  if (initialized_ && outputApplied_ && duty == appliedDuty_) {
    return;
  }
  appliedDuty_ = duty;
  if (!initialized_) {
    return;
  }

  // An active-high gate turns the pull-down transistor on. The fan's logical
  // PWM is therefore the complement of the GPIO PWM.
  const auto gpioDuty =
      gateActiveHigh_ ? static_cast<std::uint8_t>(255U - duty) : duty;
  analogWrite(pin_, gpioDuty);
  outputApplied_ = true;
}

}  // namespace fanlink::platform::esp8266
