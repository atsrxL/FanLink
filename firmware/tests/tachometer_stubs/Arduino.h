#pragma once
#include <cstdint>
#define IRAM_ATTR
constexpr int INPUT_PULLUP = 2;
constexpr int FALLING = 3;
std::uint32_t micros();
void pinMode(int pin, int mode);
int digitalPinToInterrupt(int pin);
void attachInterrupt(int pin, void (*handler)(), int mode);
void noInterrupts();
void interrupts();
