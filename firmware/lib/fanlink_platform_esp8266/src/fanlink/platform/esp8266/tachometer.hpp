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
  void processEdge(std::uint32_t edgeUs);
  void resetEstimate();

  static Esp8266Tachometer* instance_;
  int pin_;
  std::uint8_t pulsesPerRevolution_;
  std::uint32_t timeoutUs_;
  std::uint32_t minimumEdgeUs_;
  std::uint32_t maximumRpm_;
  static constexpr std::uint8_t kQueueSize = 32U;
  static constexpr std::uint8_t kPeriodCount = 5U;
  volatile std::uint32_t edges_[kQueueSize]{};
  volatile std::uint8_t writeIndex_{0};
  volatile std::uint8_t readIndex_{0};
  volatile bool overflow_{false};
  std::uint32_t minimumPeriodUs_{0};
  std::uint32_t lastRawEdgeUs_{0};
  std::uint32_t anchorUs_{0};
  std::uint32_t lastTrustedEdgeUs_{0};
  std::uint32_t periods_[kPeriodCount]{};
  std::uint8_t periodCount_{0};
  std::uint8_t periodIndex_{0};
  std::uint16_t rpm_{0};
  bool haveRawEdge_{false};
  bool haveAnchor_{false};
  bool haveEstimate_{false};
  bool initialized_{false};
};

}  // namespace fanlink::platform::esp8266
