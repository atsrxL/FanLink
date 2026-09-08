#pragma once

#include <cstdint>

// Only the Arduino boundary is replaced. Tests compile the real fan output.
constexpr std::uint8_t LOW = 0;
constexpr std::uint8_t HIGH = 1;
constexpr std::uint8_t OUTPUT = 1;

void digitalWrite(std::uint8_t pin, std::uint8_t value);
void pinMode(std::uint8_t pin, std::uint8_t mode);
void analogWriteRange(std::uint32_t range);
void analogWriteFreq(std::uint32_t frequency);
void analogWrite(std::uint8_t pin, int value);
