#include "fanlink/platform/esp8266/tachometer.hpp"

#include <Arduino.h>
#include <algorithm>
#include <limits>

namespace fanlink::platform::esp8266 {
namespace {
void increment(std::uint32_t& counter) {
  if (counter != UINT32_MAX) ++counter;
}
std::uint32_t addAge(std::uint32_t age, std::uint32_t elapsed) {
  if (age == UINT32_MAX) return age;
  constexpr auto maximum = UINT32_MAX - 1U;
  return elapsed >= maximum - age ? maximum : age + elapsed;
}
}  // namespace

Esp8266Tachometer* Esp8266Tachometer::instance_ = nullptr;

bool Esp8266Tachometer::begin() {
  if (pin_ < 0 || pin_ >= 16 || pulsesPerRevolution_ == 0U || maximumRpm_ == 0U ||
      timeoutUs_ == 0U || timeoutUs_ >= 0x80000000UL || instance_ != nullptr) {
    return false;
  }
  const auto maximumRate = static_cast<std::uint64_t>(pulsesPerRevolution_) *
      std::min(maximumRpm_, static_cast<std::uint32_t>(
          std::numeric_limits<std::uint16_t>::max()));
  minimumPeriodUs_ = std::max(minimumEdgeUs_, static_cast<std::uint32_t>(
      (60000000ULL + maximumRate - 1U) / maximumRate));
  if (minimumPeriodUs_ >= timeoutUs_) {
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
  const auto edgeUs = ::micros();
  if (rawEdges_ != UINT32_MAX) ++rawEdges_;
  latestRawEdgeUs_ = edgeUs;
  rawEdgePending_ = true;
  const auto next = static_cast<std::uint8_t>((writeIndex_ + 1U) & (kQueueSize - 1U));
  if (next == readIndex_) {
    overflow_ = true;
    if (overflowDrops_ != UINT32_MAX) ++overflowDrops_;
    return;
  }
  edges_[writeIndex_] = edgeUs;
  writeIndex_ = next;
}

void Esp8266Tachometer::resetEstimate() {
  haveAnchor_ = false;
  haveEstimate_ = false;
  periodCount_ = 0U;
  periodIndex_ = 0U;
  rpm_ = 0U;
}

void Esp8266Tachometer::processEdge(std::uint32_t edgeUs) {
  const auto rawPeriod = static_cast<std::uint32_t>(edgeUs - lastRawEdgeUs_);
  const bool shortEdge = haveRawEdge_ && rawPeriod < minimumPeriodUs_;
  lastRawEdgeUs_ = edgeUs;
  haveRawEdge_ = true;
  // Test consecutive RAW edges too: a high-frequency noise train must not
  // accumulate into a plausible period merely by skipping alternate edges.
  if (shortEdge) {
    increment(filterRejects_);
    return;
  }
  if (haveEstimate_ &&
      static_cast<std::uint32_t>(edgeUs - lastTrustedEdgeUs_) >= timeoutUs_) {
    resetEstimate();
  }
  const auto period = static_cast<std::uint32_t>(edgeUs - anchorUs_);
  if (!haveAnchor_ || period >= timeoutUs_) {
    resetEstimate();
    anchorUs_ = edgeUs;
    haveAnchor_ = true;
    return;
  }
  if (period < minimumPeriodUs_) {
    increment(filterRejects_);
    return;
  }
  anchorUs_ = edgeUs;
  periods_[periodIndex_] = period;
  periodIndex_ = static_cast<std::uint8_t>((periodIndex_ + 1U) % kPeriodCount);
  if (periodCount_ < kPeriodCount) {
    ++periodCount_;
  }
  if (periodCount_ < kPeriodCount) {
    return;
  }

  std::uint32_t sorted[kPeriodCount]{};
  std::copy(periods_, periods_ + periodCount_, sorted);
  std::sort(sorted, sorted + periodCount_);
  const auto median = sorted[periodCount_ / 2U];
  const auto tolerance = median / 5U;  // ±20% period consensus, not a fan RPM cap.
  const auto low = median - tolerance;
  const auto high = median + tolerance;
  std::uint64_t sum = 0U;
  std::uint8_t count = 0U;
  for (std::uint8_t i = 0U; i < periodCount_; ++i) {
    if (sorted[i] >= low && sorted[i] <= high) {
      sum += sorted[i];
      ++count;
    }
  }
  // An old majority may suppress an outlier, but that outlier cannot refresh
  // the age of the last good RPM. Repeated sample() calls cannot do so either.
  // Four of five periods must agree. A bare majority would incorrectly lock
  // onto one half of a recurring split pulse (e.g. alternating 5/15 ms).
  if (count < 4U || period < low || period > high) {
    increment(filterRejects_);
    return;
  }
  const auto calculated = (60000000ULL * count) / (sum * pulsesPerRevolution_);
  if (calculated == 0U || calculated > maximumRpm_ ||
      calculated > std::numeric_limits<std::uint16_t>::max()) {
    increment(filterRejects_);
    return;
  }
  rpm_ = static_cast<std::uint16_t>(calculated);
  lastTrustedEdgeUs_ = edgeUs;
  haveEstimate_ = true;
  increment(validPeriods_);
}

TachSample Esp8266Tachometer::sample(std::uint32_t nowUs) {
  if (!initialized_) {
    return {};
  }

  // Copy at most 31 timestamps while GPIO interrupts are masked; filtering and
  // division happen only after restoring interrupts. Never hold a radio wait.
  (void)nowUs;
  std::uint32_t pending[kQueueSize]{};
  std::uint8_t count = 0U;
  noInterrupts();
  const bool overflow = overflow_;
  while (readIndex_ != writeIndex_) {
    pending[count++] = edges_[readIndex_];
    readIndex_ = static_cast<std::uint8_t>((readIndex_ + 1U) & (kQueueSize - 1U));
  }
  overflow_ = false;
  const auto sampledNowUs = ::micros();
  rawAgeUs_ = rawEdgePending_
      ? addAge(0U, static_cast<std::uint32_t>(sampledNowUs - latestRawEdgeUs_))
      : addAge(rawAgeUs_, static_cast<std::uint32_t>(sampledNowUs - rawAgeClockUs_));
  rawAgeClockUs_ = sampledNowUs;
  rawEdgePending_ = false;
  interrupts();
  if (overflow) {
    // One rejected batch/window; actual lost ISR edges counted separately.
    increment(filterRejects_);
    // Unknown missing edges must not be treated as a lower real fan speed.
    resetEstimate();
    haveRawEdge_ = false;
    return {};
  }
  for (std::uint8_t i = 0U; i < count; ++i) {
    if (static_cast<std::uint32_t>(sampledNowUs - pending[i]) >= timeoutUs_) {
      increment(filterRejects_);
      resetEstimate();
      haveRawEdge_ = false;
      continue;
    }
    processEdge(pending[i]);
  }
  if (!haveEstimate_ ||
      static_cast<std::uint32_t>(sampledNowUs - lastTrustedEdgeUs_) >= timeoutUs_) {
    if (haveEstimate_) {
      resetEstimate();
    }
    return {};
  }
  return TachSample{rpm_, true};
}

TachDiagnostics Esp8266Tachometer::diagnostics(std::uint32_t nowUs) {
  if (!initialized_) return {};
  (void)nowUs;  // Caller time may precede an ISR; use a coherent fresh snapshot.
  noInterrupts();
  const auto currentUs = ::micros();
  const auto raw = rawEdges_;
  const auto dropped = overflowDrops_;
  rawAgeUs_ = rawEdgePending_
      ? addAge(0U, static_cast<std::uint32_t>(currentUs - latestRawEdgeUs_))
      : addAge(rawAgeUs_, static_cast<std::uint32_t>(currentUs - rawAgeClockUs_));
  rawAgeClockUs_ = currentUs;
  rawEdgePending_ = false;
  const auto age = rawAgeUs_;
  interrupts();
  return TachDiagnostics{raw, validPeriods_, filterRejects_, age, dropped, true};
}

}  // namespace fanlink::platform::esp8266
