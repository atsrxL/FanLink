#pragma once

#include <RadioLib.h>
#include <SPI.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "fanlink/interfaces.hpp"

namespace fanlink::platform::esp8266 {

struct Sx1280PortConfig {
  int nssPin{15};
  int dio1Pin{4};
  int resetPin{2};
  int busyPin{5};
  int rxEnablePin{-1};
  int txEnablePin{-1};
  bool useDcdc{false};
  std::uint32_t spiHz{2000000UL};
  float frequencyMhz{2405.0F};
  float bitRateKbps{650.0F};
  std::uint8_t codingRate{3U};
  std::int8_t outputPowerDbm{-18};
  std::uint16_t preambleBits{16U};
  std::uint32_t busyTimeoutMs{25U};
};

class RadioLibSx1280Port final : public IRadioPort {
 public:
  explicit RadioLibSx1280Port(Sx1280PortConfig config);

  bool begin(std::uint32_t networkId) override;
  RadioResult transmit(const std::uint8_t* data, std::size_t length) override;
  RadioRxResult receive(std::uint8_t* data, std::size_t capacity,
                        std::uint32_t timeoutMs) override;
  int lastError() const override { return lastError_; }

 private:
  static std::array<std::uint8_t, 4> makeSyncWord(std::uint32_t networkId);
  static RadioResult mapResult(int state);
  static std::int8_t clampRssi(float rssi);

  Sx1280PortConfig config_;
  SPISettings spiSettings_;
  ArduinoHal hal_;
  Module module_;
  SX1280 radio_;
  int lastError_{RADIOLIB_ERR_NONE};
  bool initialized_{false};
};

}  // namespace fanlink::platform::esp8266
