#pragma once

#include <Arduino.h>

#include <cstdint>

#include "fanlink/interfaces.hpp"

namespace fanlink::platform::esp8266 {

class Esp8266Tachometer final : public ITachometer {
 public:
  Esp8266Tachometer(int pin, std::uint8_t pulsesPerRevolution, std::uint32_t timeoutUs,
                    std::uint32_t minimumEdgeUs, std::uint32_t maximumRpm)
      : pin_(pin),
        pulsesPerRevolution_(pulsesPerRevolution),
        timeoutUs_(timeoutUs),
        minimumEdgeUs_(minimumEdgeUs),
        maximumRpm_(maximumRpm) {}

  bool begin() override;
  TachSample sample(std::uint32_t nowUs) override;

 private:
  static void IRAM_ATTR interruptThunk();
  void IRAM_ATTR onFallingEdge();

  static Esp8266Tachometer* instance_;
  int pin_;
  std::uint8_t pulsesPerRevolution_;
  std::uint32_t timeoutUs_;
  std::uint32_t minimumEdgeUs_;
  std::uint32_t maximumRpm_;
  volatile std::uint32_t lastEdgeUs_{0};
  volatile std::uint32_t periodUs_{0};
  bool initialized_{false};
};

}  // namespace fanlink::platform::esp8266
