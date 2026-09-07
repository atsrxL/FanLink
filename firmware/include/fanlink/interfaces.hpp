#pragma once

#include <cstddef>
#include <cstdint>

namespace fanlink {

enum class RadioResult : std::uint8_t { Ok, Timeout, CrcError, IoError };

struct RadioRxResult {
  RadioResult result{RadioResult::IoError};
  std::size_t length{0};
  std::int8_t rssiDbm{-127};
  int driverError{0};
};

class IRadioPort {
 public:
  virtual ~IRadioPort() = default;
  virtual bool begin(std::uint32_t networkId) = 0;
  virtual RadioResult transmit(const std::uint8_t* data, std::size_t length) = 0;
  virtual RadioRxResult receive(std::uint8_t* data, std::size_t capacity,
                                std::uint32_t timeoutMs) = 0;
  virtual int lastError() const = 0;
};

class IClock {
 public:
  virtual ~IClock() = default;
  virtual std::uint32_t millis() const = 0;
  virtual std::uint32_t micros() const = 0;
  virtual void delayMilliseconds(std::uint32_t milliseconds) = 0;
  virtual void delayMicroseconds(std::uint32_t microseconds) = 0;
};

class IFanOutput {
 public:
  virtual ~IFanOutput() = default;
  virtual bool begin() = 0;
  virtual void setDuty(std::uint8_t duty) = 0;
  virtual void setFullSpeed() = 0;
  virtual std::uint8_t appliedDuty() const = 0;
};

struct TachSample {
  std::uint16_t rpm{0};
  bool valid{false};
};

class ITachometer {
 public:
  virtual ~ITachometer() = default;
  virtual bool begin() = 0;
  virtual TachSample sample(std::uint32_t nowUs) = 0;
};

}  // namespace fanlink
