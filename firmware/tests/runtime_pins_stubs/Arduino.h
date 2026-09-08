#pragma once
#include <cstdint>
#define IRAM_ATTR
#define LOW 0
#define HIGH 1
#define OUTPUT 1
#define INPUT_PULLUP 2
#define FALLING 2
void digitalWrite(int, int);
void pinMode(int, int);
void analogWriteRange(std::uint32_t);
void analogWriteFreq(std::uint32_t);
void analogWrite(int, int);
int digitalPinToInterrupt(int);
void attachInterrupt(int, void (*)(), int);
std::uint32_t micros();
void noInterrupts();
void interrupts();
