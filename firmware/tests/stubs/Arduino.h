#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

inline void* memcpy_P(void* destination, const void* source, std::size_t size) {
  return std::memcpy(destination, source, size);
}

#ifndef F_CPU
#define F_CPU 80000000UL
#endif
#ifndef ICACHE_RAM_ATTR
#define ICACHE_RAM_ATTR
#endif
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
#ifndef PROGMEM
#define PROGMEM
#endif

#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0
#define INPUT_PULLUP 2
#define FALLING 2
#define SERIAL_8N1 0

class HardwareSerial {
 public:
  void begin(unsigned long) {}
  void begin(unsigned long, int) {}
  void end() {}
  int available() const { return 0; }
  int availableForWrite() const { return 128; }
  void setDebugOutput(bool) {}
  int read() { return -1; }
  std::size_t write(const std::uint8_t*, std::size_t length) { return length; }
  std::size_t write(std::uint8_t) { return 1U; }
  void flush() {}
};

extern HardwareSerial Serial;

struct rst_info;
class ESPClass {
 public:
  std::uint32_t getFreeSketchSpace() const;
  void restart();
  void rebootIntoUartDownloadMode();
  rst_info* getResetInfoPtr();
};
extern ESPClass ESP;

inline std::uint32_t millis() { return 0U; }
inline std::uint32_t micros() { return 0U; }
inline void delay(unsigned long) {}
inline void delayMicroseconds(unsigned int) {}
inline void yield() {}
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return LOW; }
inline void noInterrupts() {}
inline void interrupts() {}
inline int digitalPinToInterrupt(int pin) { return pin; }
inline void attachInterrupt(int, void (*)(), int) {}
inline void detachInterrupt(int) {}
inline void analogWriteRange(std::uint32_t) {}
inline void analogWriteFreq(std::uint32_t) {}
inline void analogWrite(int, int) {}
