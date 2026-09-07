#pragma once

#include <cstddef>
#include <cstdint>

#include "SPI.h"

using RadioLibTime_t = std::uint32_t;

#define RADIOLIB_ERR_NONE 0
#define RADIOLIB_ERR_RX_TIMEOUT (-6)
#define RADIOLIB_ERR_CRC_MISMATCH (-7)
#define RADIOLIB_ERR_PACKET_TOO_LONG (-4)
#define RADIOLIB_ERR_NULL_POINTER (-28)
#define RADIOLIB_ERR_PACKET_TOO_SHORT (-30)
#define RADIOLIB_ERR_INVALID_FUNCTION (-1003)
#define RADIOLIB_SHAPING_0_5 1
#define RADIOLIB_SX128X_CMD_SET_REGULATOR_MODE 0x96
#define RADIOLIB_SX128X_REGULATOR_LDO 0x00
#define RADIOLIB_SX128X_REGULATOR_DC_DC 0x01
#define RADIOLIB_STATIC_ONLY 1
#define RADIOLIB_STATIC_ARRAY_SIZE 64
#define RADIOLIB_STATIC_SPI_ARRAY_SIZE (3 * sizeof(std::uint32_t) + RADIOLIB_STATIC_ARRAY_SIZE)

class RadioLibHal {};

class ArduinoHal : public RadioLibHal {
 public:
  ArduinoHal(SPIClass&, SPISettings) {}
};

class Module {
 public:
  struct SPIConfig {
    RadioLibTime_t timeout{1000U};
  };

  Module(RadioLibHal*, int, int, int, int) {}
  SPIConfig spiConfig{};
};

class SX1280 {
 public:
  explicit SX1280(Module*) {}
  void setRfSwitchPins(std::uint32_t, std::uint32_t) {}
  std::int16_t beginFLRC(float, std::uint16_t, std::uint8_t, std::int8_t,
                         std::uint16_t, std::uint8_t) {
    return RADIOLIB_ERR_NONE;
  }
  std::int16_t setSyncWord(std::uint8_t*, std::size_t) { return RADIOLIB_ERR_NONE; }
  std::int16_t setCRC(std::uint8_t, std::uint32_t, std::uint16_t) {
    return RADIOLIB_ERR_NONE;
  }
  std::int16_t fixedPacketLengthMode(std::uint8_t) { return RADIOLIB_ERR_NONE; }
  std::int16_t transmit(const std::uint8_t*, std::size_t) { return RADIOLIB_ERR_NONE; }
  std::int16_t receive(std::uint8_t*, std::size_t, RadioLibTime_t) {
    return RADIOLIB_ERR_NONE;
  }
  std::size_t getPacketLength(bool = true) { return 0U; }
  float getRSSI() { return -50.0F; }
};
