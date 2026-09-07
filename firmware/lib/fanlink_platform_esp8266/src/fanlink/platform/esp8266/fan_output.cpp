#include "fanlink/platform/esp8266/fan_output.hpp"

#include <Arduino.h>

namespace fanlink::platform::esp8266 {

bool Esp8266FanOutput::begin() {
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
  appliedDuty_ = duty;
  if (!initialized_) {
    return;
  }

  // An active-high gate turns the pull-down transistor on. The fan's logical
  // PWM is therefore the complement of the GPIO PWM.
  const auto gpioDuty =
      gateActiveHigh_ ? static_cast<std::uint8_t>(255U - duty) : duty;
  analogWrite(pin_, gpioDuty);
}

}  // namespace fanlink::platform::esp8266
