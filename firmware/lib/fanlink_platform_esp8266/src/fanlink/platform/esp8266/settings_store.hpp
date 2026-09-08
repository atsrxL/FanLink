#pragma once
#include <Arduino.h>
#include <flash_hal.h>
#include <cstring>
#include "fanlink/settings.hpp"

namespace fanlink::platform::esp8266 {
// Use the dedicated linker-reserved EEPROM sector outside OTA staging, without
// EEPROM.begin()'s hidden Flash-read errors or dynamic buffer. Every hardware
// operation is checked; successful writes are read back before acknowledging.
class Esp8266SettingsStore final : public settings::ISettingsStore {
 public:
  bool read(std::uint8_t* data, std::size_t length) override {
    if (!data || length != settings::kRecordSize) return false;
    return flash_hal_read(address(), length, data) == FLASH_HAL_OK;
  }
  bool write(const std::uint8_t* data, std::size_t length) override {
    if (!data || length != settings::kRecordSize) return false;
    if (flash_hal_erase(address(), 4096) != FLASH_HAL_OK ||
        flash_hal_write(address(), length, data) != FLASH_HAL_OK) return false;
    std::array<std::uint8_t, settings::kRecordSize> verify{};
    return read(verify.data(), verify.size()) && std::memcmp(data, verify.data(), length) == 0;
  }
 private:
  static std::uint32_t address() { return EEPROM_start - 0x40200000UL; }
};
}  // namespace fanlink::platform::esp8266
