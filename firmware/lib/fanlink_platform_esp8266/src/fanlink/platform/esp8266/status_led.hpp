#pragma once

#include <cstdint>

namespace fanlink::platform::esp8266 {

class Esp8266StatusLed {
 public:
  Esp8266StatusLed(int pin, bool activeHigh) : pin_(pin), activeHigh_(activeHigh) {}

  bool begin();
  void update(bool connected, std::uint32_t nowMs);

 private:
  void write(bool on);

  int pin_;
  bool activeHigh_;
  std::uint32_t patternStartedMs_{0U};
  bool lastConnected_{false};
  bool haveState_{false};
  bool initialized_{false};
};

}  // namespace fanlink::platform::esp8266
