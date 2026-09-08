#pragma once

#include <cstdio>
#include <limits>
#include "fanlink/rx_controller.hpp"

namespace fanlink::diagnostic {

// Diagnostic-only substitutes: no physical fan output or tach measurement.
// The fixed duty is a placeholder, NOT evidence that a fan is running full speed.
class NoFan final : public IFanOutput {
 public:
  bool begin() override { return true; }
  void setDuty(std::uint8_t) override {}
  void setFullSpeed() override {}
  std::uint8_t appliedDuty() const override { return 255; }
};
class NoTach final : public ITachometer {
 public:
  bool begin() override { return true; }
  TachSample sample(std::uint32_t) override { return {}; }
};

inline void increment(std::uint32_t& value) {
  if (value != UINT32_MAX) ++value;
}

// No logging, decoding, GPIO or clock calls on the receive-to-reply path.
// Raw sequence/poll are deliberately unauthenticated header observations.
class ObservedRadio final : public IRadioPort {
 public:
  explicit ObservedRadio(IRadioPort& port) : port_(port) {}
  bool begin(std::uint32_t network) override { return port_.begin(network); }
  RadioResult transmit(const std::uint8_t* data, std::size_t length) override {
    return port_.transmit(data, length);
  }
  RadioRxResult receive(std::uint8_t* data, std::size_t capacity,
                        std::uint32_t timeout) override {
    return observe(port_.receive(data, capacity, timeout), data, capacity);
  }
  RadioRxResult receiveForUs(std::uint8_t* data, std::size_t capacity,
                             std::uint32_t timeout) override {
    return observe(port_.receiveForUs(data, capacity, timeout), data, capacity);
  }
  int lastError() const override { return port_.lastError(); }
  std::uint32_t ok{0}, timeout{0}, crc{0}, io{0};
  std::uint16_t rawSequence{0};
  std::uint8_t rawPoll{0};
  std::int8_t rawRssi{-127};
 private:
  RadioRxResult observe(RadioRxResult result, const std::uint8_t* data,
                         std::size_t capacity) {
    switch (result.result) {
      case RadioResult::Ok:
        increment(ok);
        rawRssi = result.rssiDbm;
        if (result.length >= 11 && capacity >= 11) {
          rawSequence = static_cast<std::uint16_t>(data[8] | (data[9] << 8));
          rawPoll = data[10];
        }
        break;
      case RadioResult::Timeout: increment(timeout); break;
      case RadioResult::CrcError: increment(crc); break;
      case RadioResult::IoError: increment(io); break;
    }
    return result;
  }
  IRadioPort& port_;
};

class Logger {
 public:
  static constexpr std::size_t kLineCapacity = 120;
  static constexpr std::uint32_t kPeriodMs = 250;
  void setResetReason(std::uint32_t reason) { resetReason_ = reason; }
  void setIdentity(std::uint8_t node, bool otaAvailable) { node_ = node; otaAvailable_ = otaAvailable; }
  void noteLoop(std::uint32_t nowUs) {
    if (haveLoop_) {
      const auto gap = static_cast<std::uint32_t>(nowUs - lastLoopUs_);
      if (gap > maxLoopGapUs_) maxLoopGapUs_ = gap;
    }
    haveLoop_ = true;
    lastLoopUs_ = nowUs;
  }
  template <typename SerialPort>
  bool service(SerialPort& serial, std::uint32_t nowMs,
               const RxController& controller, const ObservedRadio& radio) {
    if (static_cast<std::uint32_t>(nowMs - lastLogMs_) < kPeriodMs) return false;
    lastLogMs_ = nowMs;  // No retry spin or catch-up burst when UART is full.
    char line[kLineCapacity];
    const auto& d = controller.radioDiagnostics();
    const auto u = [](std::uint32_t value) { return static_cast<unsigned long>(value); };
    int size = 0;
    switch (part_) {
      case 0:
        size = std::snprintf(line, sizeof(line),
            "RXDIAG n%u nofan=1 notach=1 ota=%s u%lu L%u S%u g%lu rst%lu\n",
            node_, otaAvailable_ ? "on" : "off", u(nowMs), !controller.linkFailsafeActive(), controller.pendingStatus(), u(maxLoopGapUs_), u(resetReason_));
        break;
      case 1:
        size = std::snprintf(line, sizeof(line),
            "RXD P u%lu p%lu a%lu o%lu f%lu x%lu\n", u(nowMs), u(d.ownPolls),
            u(d.replyAttempts), u(d.replySuccesses), u(d.replyFailures), u(d.expiredRepliesSkipped));
        break;
      case 2:
        size = std::snprintf(line, sizeof(line),
            "RXD R u%lu o%lu t%lu c%lu i%lu s%u p%u r%d\n", u(nowMs),
            u(radio.ok), u(radio.timeout), u(radio.crc), u(radio.io),
            radio.rawSequence, radio.rawPoll, radio.rawRssi);
        break;
      case 3:
        size = std::snprintf(line, sizeof(line),
            "RXD V u%lu m%lu n%lu s%lu e%lu\n", u(nowMs), u(d.malformedControls),
            u(d.wrongNetworkControls), u(d.staleControls), u(d.expiredControls));
        break;
      case 4:
        size = std::snprintf(line, sizeof(line),
            "RXD B u%lu a%lu o%lu f%lu i%lu x%lu e%d\n", u(nowMs),
            u(d.recoveryAttempts), u(d.recoverySuccesses), u(d.recoveryFailures),
            u(d.receiveIoErrors), u(d.transmitErrors), d.lastDriverError);
        break;
      default:
        size = std::snprintf(line, sizeof(line),
            "RXD T u%lu l%lu a%lu z%lu g%lu\n", u(nowMs), u(d.lateReplyCalls),
            u(d.lastReplyCallAgeUs), u(d.maxReplyCallAgeUs), u(maxLoopGapUs_));
        break;
    }
    // The UART has one writer. Do not call flush or write a partial line.
    if (size <= 0 || static_cast<std::size_t>(size) >= sizeof(line) ||
        serial.availableForWrite() <= size) return true;
    serial.write(reinterpret_cast<const std::uint8_t*>(line), static_cast<std::size_t>(size));
    part_ = static_cast<std::uint8_t>((part_ + 1U) % 6U);
    return true; // A due attempt owns this loop, even if the FIFO was full.
  }
 private:
  std::uint32_t lastLogMs_{0}, lastLoopUs_{0}, maxLoopGapUs_{0};
  std::uint32_t resetReason_{UINT32_MAX};
  std::uint8_t part_{0};
  std::uint8_t node_{2};
  bool otaAvailable_{false};
  bool haveLoop_{false};
};
}  // namespace fanlink::diagnostic
