#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "fanlink/serial_protocol.hpp"
#include "fanlink/tx_diagnostic_text.hpp"

namespace fanlink {

enum class TxDiagnosticFamily : std::uint8_t { Recovery, Node, Timing, Radio, Counts, Event, Trace };
enum class TxDiagnosticAttempt : std::uint8_t { NotReady, Blocked, Sent, Dropped };

// Optional text only: callers must service STATUS/METRICS first. A full FIFO
// retains the due family until it fits, so short frames cannot repeatedly steal
// its space. NotReady skips unavailable data; Dropped throttles an unencodable
// or impossible-to-fit frame without pinning the queue forever.
class TxDiagnosticScheduler {
 public:
  static constexpr std::size_t kFamilyCount = 7U;
  static constexpr std::uint32_t kPeriodMs = 1000U;
  static constexpr std::uint32_t kTracePeriodMs = 25U;
  static constexpr std::size_t kTxFifoCapacity = 128U;

  void reset(std::uint32_t nowMs) { lastMs_.fill(nowMs); next_ = 0U; }

  template <typename Emit>
  void service(std::uint32_t nowMs, Emit emit) {
    for (std::size_t checked = 0U; checked < kFamilyCount; ++checked) {
      const auto index = next_;
      const auto period = index == static_cast<std::size_t>(TxDiagnosticFamily::Trace)
          ? kTracePeriodMs : kPeriodMs;
      if (static_cast<std::uint32_t>(nowMs - lastMs_[index]) >= period) {
        const auto result = emit(static_cast<TxDiagnosticFamily>(index));
        if (result == TxDiagnosticAttempt::Blocked) return;
        if (result != TxDiagnosticAttempt::NotReady) {
          lastMs_[index] = nowMs;
          next_ = (index + 1U) % kFamilyCount;
          return; // at most one frame attempt (and write) per service call
        }
      }
      next_ = (index + 1U) % kFamilyCount;
    }
  }

 private:
  std::array<std::uint32_t, kFamilyCount> lastMs_{};
  std::size_t next_{0U};
};

template <typename SerialPort>
TxDiagnosticAttempt writeTxOptionalDiagnostic(SerialPort& serial, const char* message,
                                               std::size_t count, std::uint16_t& sequence) {
  // Preserve the grammar. Oversized text is expendable, never a permanent
  // queue head. A FIFO-blocked retry does not consume a serial sequence.
  if (message == nullptr || count == 0U || count > kSerialMaxPayload)
    return TxDiagnosticAttempt::Dropped;
  SerialFrame frame{};
  frame.type = serial_type::kErrorEvent;
  frame.sequence = sequence;
  frame.payloadLength = static_cast<std::uint16_t>(count);
  std::memcpy(frame.payload.data(), message, count);
  std::array<std::uint8_t, kSerialMaxFrameSize> encoded{};
  std::size_t length = 0U;
  if (!encodeSerialFrame(frame, encoded, length) ||
      length >= TxDiagnosticScheduler::kTxFifoCapacity) return TxDiagnosticAttempt::Dropped;
  if (!txDiagnosticFrameCanWrite(length, serial.availableForWrite()))
    return TxDiagnosticAttempt::Blocked;
  ++sequence;
  return serial.write(encoded.data(), length) == length
      ? TxDiagnosticAttempt::Sent : TxDiagnosticAttempt::Dropped;
}

}  // namespace fanlink
