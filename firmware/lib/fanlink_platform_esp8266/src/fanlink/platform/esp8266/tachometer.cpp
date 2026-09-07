#include "fanlink/platform/esp8266/tachometer.hpp"

#include <Arduino.h>
#include <limits>

namespace fanlink::platform::esp8266 {

Esp8266Tachometer* Esp8266Tachometer::instance_ = nullptr;

bool Esp8266Tachometer::begin() {
  if (pin_ < 0 || pin_ >= 16 || pulsesPerRevolution_ == 0U || instance_ != nullptr) {
    return false;
  }
  instance_ = this;
  pinMode(pin_, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pin_), interruptThunk, FALLING);
  initialized_ = true;
  return true;
}

void IRAM_ATTR Esp8266Tachometer::interruptThunk() {
  if (instance_ != nullptr) {
    instance_->onFallingEdge();
  }
}

void IRAM_ATTR Esp8266Tachometer::onFallingEdge() {
  const auto nowUs = ::micros();
  const auto previous = lastEdgeUs_;
  if (previous == 0U) {
    lastEdgeUs_ = nowUs;
    return;
  }

  const auto delta = static_cast<std::uint32_t>(nowUs - previous);
  if (delta >= minimumEdgeUs_) {
    periodUs_ = delta;
    lastEdgeUs_ = nowUs;
  }
}

TachSample Esp8266Tachometer::sample(std::uint32_t nowUs) {
  if (!initialized_) {
    return {};
  }

  // The caller-provided timestamp may have been captured immediately before an
  // edge ISR. Snapshot a fresh time and both ISR-owned values atomically so the
  // edge can never appear to be "in the future" and underflow the age check.
  (void)nowUs;
  noInterrupts();
  const auto sampledNowUs = ::micros();
  const auto lastEdgeUs = lastEdgeUs_;
  const auto periodUs = periodUs_;
  interrupts();

  if (lastEdgeUs == 0U || periodUs == 0U ||
      static_cast<std::uint32_t>(sampledNowUs - lastEdgeUs) > timeoutUs_) {
    return {};
  }

  const std::uint64_t denominator =
      static_cast<std::uint64_t>(periodUs) * pulsesPerRevolution_;
  if (denominator == 0U) {
    return {};
  }
  const std::uint64_t calculated = 60000000ULL / denominator;
  if (calculated == 0U || calculated > maximumRpm_ ||
      calculated > std::numeric_limits<std::uint16_t>::max()) {
    return {};
  }

  return TachSample{static_cast<std::uint16_t>(calculated), true};
}

}  // namespace fanlink::platform::esp8266
