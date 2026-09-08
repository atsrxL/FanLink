#pragma once

#include <cstdint>

#include "fanlink/interfaces.hpp"

namespace fanlink::platform::esp8266 {

class Esp8266FanOutput final : public IFanOutput {
 public:
  Esp8266FanOutput(int pin, std::uint32_t frequencyHz, bool gateActiveHigh)
      : pin_(pin), frequencyHz_(frequencyHz), gateActiveHigh_(gateActiveHigh) {}

  bool begin() override;
  void setDuty(std::uint8_t duty) override;
  void setFullSpeed() override { setDuty(255U); }
  std::uint8_t appliedDuty() const override { return appliedDuty_; }

 private:
  int pin_;
  std::uint32_t frequencyHz_;
  bool gateActiveHigh_;
  std::uint8_t appliedDuty_{255U};
  bool initialized_{false};
  bool outputApplied_{false};
};

}  // namespace fanlink::platform::esp8266
