#include "fanlink/platform/esp8266/status_led.hpp"

#include <Arduino.h>

#include "fanlink/link_led_pattern.hpp"

namespace fanlink::platform::esp8266 {

bool Esp8266StatusLed::begin() {
  if (pin_ < 0) {
    return false;
  }
  write(false);
  pinMode(pin_, OUTPUT);
  initialized_ = true;
  return true;
}

void Esp8266StatusLed::write(bool on) {
  const bool high = activeHigh_ ? on : !on;
  digitalWrite(pin_, high ? HIGH : LOW);
}

void Esp8266StatusLed::update(bool connected, std::uint32_t nowMs) {
  if (!initialized_) {
    return;
  }
  if (!haveState_ || connected != lastConnected_) {
    patternStartedMs_ = nowMs;
    lastConnected_ = connected;
    haveState_ = true;
  }
  const auto elapsedMs = static_cast<std::uint32_t>(nowMs - patternStartedMs_);
  write(link_led::shouldBeOn(connected, elapsedMs));
}

}  // namespace fanlink::platform::esp8266
