#pragma once

#include <cstddef>
#include <cstdint>

#include "SPI.h"

using RadioLibTime_t = std::uint32_t;

#define RADIOLIB_ERR_NONE 0
#define RADIOLIB_ERR_UNKNOWN (-1)
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
#define RADIOLIB_SX128X_RX_TIMEOUT_NONE 0x0000
#define RADIOLIB_SX128X_IRQ_RX_DONE 0x0002
#define RADIOLIB_SX128X_IRQ_SYNC_WORD_ERROR 0x0008
#define RADIOLIB_SX128X_IRQ_RX_TX_TIMEOUT 0x4000
#define RADIOLIB_SX128X_CMD_SET_DIO_IRQ_PARAMS 0x8D
#define RADIOLIB_IRQ_RX_DEFAULT_FLAGS 0xFFFF
#define RADIOLIB_IRQ_RX_DEFAULT_MASK 0x0002
#define RADIOLIB_RADIO_MODE_RX 2
#define RADIOLIB_SX128X_IRQ_ALL 0xFFFF
#define RADIOLIB_SX128X_IRQ_CRC_ERROR 0x0040
#define RADIOLIB_SX128X_IRQ_HEADER_ERROR 0x0020
#define RADIOLIB_SX128X_IRQ_HEADER_VALID 0x0010
#define RADIOLIB_SX128X_PACKET_STATUS_CRC_ERROR 0x10
#define RADIOLIB_SX128X_PACKET_STATUS_SYNC_ERROR 0x40
#define RADIOLIB_SX128X_PACKET_STATUS_LENGTH_ERROR 0x20
#define RADIOLIB_SX128X_PACKET_STATUS_ABORT_ERROR 0x08
#define RADIOLIB_SX128X_CMD_GET_IRQ_STATUS 0x15
#define RADIOLIB_SX128X_CMD_READ_BUFFER 0x1B
#define RADIOLIB_SX128X_CMD_GET_RX_BUFFER_STATUS 0x17
#define RADIOLIB_SX128X_CMD_GET_PACKET_STATUS 0x1D
#define RADIOLIB_STATIC_ONLY 1
#define RADIOLIB_STATIC_ARRAY_SIZE 64
#define RADIOLIB_STATIC_SPI_ARRAY_SIZE (3 * sizeof(std::uint32_t) + RADIOLIB_STATIC_ARRAY_SIZE)

class RadioLibHal {};

struct RadioModeConfig_t {
  struct {
    std::uint32_t timeout{};
    std::uint32_t irqFlags{};
    std::uint32_t irqMask{};
    std::size_t len{};
  } receive;
};

class ArduinoHal : public RadioLibHal {
 public:
  ArduinoHal(SPIClass&, SPISettings) {}
  RadioLibTime_t micros() { return 0U; }
  std::uint32_t digitalRead(std::uint32_t) { return 0U; }
  void yield() {}
};

class Module {
 public:
  struct SPIConfig {
    RadioLibTime_t timeout{1000U};
  };

  Module(RadioLibHal*, int, int, int, int) {}
  std::int16_t SPIreadStream(std::uint16_t, std::uint8_t*, std::size_t) { return 0; }
  std::int16_t SPIwriteStream(std::uint16_t, const std::uint8_t*, std::size_t) { return 0; }
  std::int16_t SPIreadStream(const std::uint8_t*, std::uint8_t, std::uint8_t*, std::size_t) { return 0; }
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
  std::int16_t startReceive(std::uint32_t) { return RADIOLIB_ERR_NONE; }
  std::int16_t stageMode(int, RadioModeConfig_t*) { return RADIOLIB_ERR_NONE; }
  std::int16_t launchMode() { return RADIOLIB_ERR_NONE; }
  std::int16_t readData(std::uint8_t*, std::size_t) { return RADIOLIB_ERR_NONE; }
  std::int16_t standby() { return RADIOLIB_ERR_NONE; }
  std::int16_t clearIrqFlags(std::uint32_t) { return RADIOLIB_ERR_NONE; }
  std::size_t getPacketLength(bool = true) { return 0U; }
  float getRSSI() { return -50.0F; }
};
