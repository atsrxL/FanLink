#pragma once

#include <cstddef>
#include <cstdint>

namespace fanlink {

enum class RadioResult : std::uint8_t { Ok, Timeout, CrcError, IoError };

// Optional evidence only. None of these flags authorizes accepting a packet.
// Separate causes may overlap in one event; legacy ports leave them unset.
namespace radio_rx_error {
inline constexpr std::uint8_t kSyncWithoutRxDone = 1U << 0U;
inline constexpr std::uint8_t kIrqCrc = 1U << 1U;
inline constexpr std::uint8_t kPacketCrc = 1U << 2U;
inline constexpr std::uint8_t kHeader = 1U << 3U;
inline constexpr std::uint8_t kIrqSync = 1U << 4U;
}  // namespace radio_rx_error

struct RadioRxEvidence {
  bool irqValid{false};
  bool packetStatusValid{false};
  bool eventObserved{false};
  bool receiveStarted{false};
  std::uint16_t irq{0};
  std::uint8_t packetStatusError{0};
  std::uint8_t packetStatusSync{0};
  std::uint8_t errorFlags{0};
  std::int8_t errorRssiDbm{-127};
  // Software timestamps in IClock::micros(), not physical DIO/RF edges.
  std::uint32_t eventObservedUs{0};
  // Completion of the SetRx command for THIS observation, before any rearm.
  std::uint32_t receiveStartedUs{0};
};

struct RadioRxResult {
  RadioResult result{RadioResult::IoError};
  std::size_t length{0};
  std::int8_t rssiDbm{-127};
  int driverError{0};
  // First software observation of RxDone, before FIFO/metadata work, in the
  // IClock::micros() timebase. Not a hardware edge timestamp. Legacy ports
  // leave receiveObserved false and callers conservatively use return time.
  bool receiveObserved{false};
  std::uint32_t receiveObservedUs{0};
  RadioRxEvidence evidence{};
};

class IRadioPort {
 public:
  virtual ~IRadioPort() = default;
  virtual bool begin(std::uint32_t networkId) = 0;
  virtual RadioResult transmit(const std::uint8_t* data, std::size_t length) = 0;
  virtual RadioRxResult receive(std::uint8_t* data, std::size_t capacity,
                                std::uint32_t timeoutMs) = 0;
  // Optional precise wait. Legacy ports retain a conservative whole-ms wait;
  // the hardware adapter overrides this to use the entire remaining window.
  virtual RadioRxResult receiveForUs(std::uint8_t* data, std::size_t capacity,
                                     std::uint32_t timeoutUs) {
    return receive(data, capacity, timeoutUs / 1000U);
  }
  virtual int lastError() const = 0;
};

// Optional maintenance extension; normal Control/RPM users need no updater.
class IRxMaintenance {
 public:
  virtual ~IRxMaintenance() = default;
  virtual void tick(std::uint32_t nowMs) = 0;
  virtual bool active() const = 0;
  // Read-only diagnostic replies may opt out; maintenance defaults stay safe.
  virtual bool replyRequiresFullSpeed() const { return true; }
  virtual bool handle(const std::uint8_t* data, std::size_t length,
                      std::uint8_t* reply, std::size_t& replyLength,
                      std::uint32_t nowMs) = 0;
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

// Lifetime (since begin/boot) saturating counters, never reset by sampling.
struct TachDiagnostics {
  std::uint32_t rawEdges{0};       // All falling ISR edges, including overflow.
  std::uint32_t validPeriods{0};   // Newly accepted estimator updates, not reads.
  std::uint32_t filterRejects{0};  // Rejected edges/windows; excludes warmup.
  // UINT32_MAX means never seen; known age saturates at UINT32_MAX-1.
  std::uint32_t lastEdgeAgeUs{UINT32_MAX};
  std::uint32_t overflowDrops{0};  // ISR edges not stored in the bounded ring.
  bool enabled{false};
};

class ITachometer {
 public:
  virtual ~ITachometer() = default;
  virtual bool begin() = 0;
  virtual TachSample sample(std::uint32_t nowUs) = 0;
  // Snapshot only: must not drain edges, update the estimator or reset counters.
  // May advance age-only wrap bookkeeping; periodically called even without RF.
  virtual TachDiagnostics diagnostics(std::uint32_t nowUs) {
    (void)nowUs;
    return {};
  }
};

}  // namespace fanlink
