#pragma once

#include <cstdint>

#define MSBFIRST 1
#define SPI_MODE0 0

class SPISettings {
 public:
  SPISettings(std::uint32_t, int, int) {}
};

class SPIClass {
 public:
  void begin() {}
};

extern SPIClass SPI;
