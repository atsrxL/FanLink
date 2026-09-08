#pragma once
#include <cstdint>
#define EEPROM_start 0x402FB000UL
#define FLASH_HAL_OK 0
extern "C" {
std::int32_t flash_hal_read(std::uint32_t, std::uint32_t, std::uint8_t*);
std::int32_t flash_hal_write(std::uint32_t, std::uint32_t, const std::uint8_t*);
std::int32_t flash_hal_erase(std::uint32_t, std::uint32_t);
}
