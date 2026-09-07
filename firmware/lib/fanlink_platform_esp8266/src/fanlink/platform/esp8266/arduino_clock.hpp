#pragma once

#include <Arduino.h>

#include "fanlink/interfaces.hpp"

namespace fanlink::platform::esp8266 {

class ArduinoClock final : public IClock {
 public:
  std::uint32_t millis() const override { return ::millis(); }
  std::uint32_t micros() const override { return ::micros(); }
  void delayMilliseconds(std::uint32_t milliseconds) override { ::delay(milliseconds); }
  void delayMicroseconds(std::uint32_t microseconds) override { ::delayMicroseconds(microseconds); }
};

}  // namespace fanlink::platform::esp8266
