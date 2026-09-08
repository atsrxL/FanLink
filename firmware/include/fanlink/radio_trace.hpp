#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace fanlink {
// Software samples, not edge timestamps. No SPI, allocation or serial here.
enum class RadioTraceEvent : std::uint8_t {
  TxBegin = 1, TxEnd, ArmBegin, ArmStageEnd, ArmLaunchBegin, ArmEnd,
  DioObserved, StandbyEnd, IrqRead, BufferRead, PacketStatusRead, FifoRead,
  ClearBegin, ClearEnd, RxResult, TxContext, RxContext, FaultCleanup,
};
enum class RadioTraceKind : std::uint8_t { Crc = 1, Io = 2, Good = 3 };
namespace radio_trace_valid {
inline constexpr std::uint8_t kTime = 1U, kPins = 2U, kValue = 4U, kResult = 8U;
}
struct RadioTraceSample {
  std::uint32_t micros{0}, value{0};
  std::int16_t result{0};
  RadioTraceEvent event{RadioTraceEvent::TxBegin};
  std::uint8_t pins{0}, validity{0};
};
static_assert(sizeof(RadioTraceSample) <= 16U, "trace RAM budget");
struct RadioTraceRecord {
  std::uint32_t generation{0}, dropped{0}, historyOverwrites{0};
  RadioTraceKind kind{RadioTraceKind::Good};
  std::uint8_t index{0}, count{0};
  RadioTraceSample sample{};
};
class RadioTraceRing {
 public:
  static constexpr std::size_t kCapacity = 32U;
  static constexpr std::uint8_t kGoodInterval = 64U;
  void record(const RadioTraceSample& sample) {
    history_[next_] = sample;
    next_ = static_cast<std::uint8_t>((next_ + 1U) % kCapacity);
    if (size_ < kCapacity) ++size_; else increment(overwrites_);
  }
  // Freeze after the result's existing cleanup/rearm, retaining a rolling
  // bounded prehistory. An undrained snapshot is immutable, even on errors.
  void finish(RadioTraceKind kind) {
    if (kind == RadioTraceKind::Good) {
      if (++good_ < kGoodInterval) return;
      good_ = 0;
    }
    if (pending_ != 0U) { increment(dropped_); return; }
    if (size_ == 0U) return;
    if (++generation_ == 0U) ++generation_;
    frozenDropped_ = dropped_; frozenOverwrites_ = overwrites_; kind_ = kind;
    pending_ = size_; cursor_ = 0;
    const auto oldest = (next_ + kCapacity - size_) % kCapacity;
    for (std::size_t i = 0; i < size_; ++i) frozen_[i] = history_[(oldest + i) % kCapacity];
  }
  bool peek(RadioTraceRecord& out) const {
    if (pending_ == 0U) return false;
    out = {generation_, frozenDropped_, frozenOverwrites_, kind_, cursor_, pending_, frozen_[cursor_]};
    return true;
  }
  void pop() {
    if (pending_ != 0U && ++cursor_ == pending_) { pending_ = 0; cursor_ = 0; }
  }
  std::uint32_t dropped() const { return dropped_; }
 private:
  static void increment(std::uint32_t& value) { if (value != UINT32_MAX) ++value; }
  std::array<RadioTraceSample, kCapacity> history_{}, frozen_{};
  std::uint32_t generation_{0}, dropped_{0}, overwrites_{0};
  std::uint32_t frozenDropped_{0}, frozenOverwrites_{0};
  std::uint8_t next_{0}, size_{0}, cursor_{0}, pending_{0}, good_{0};
  RadioTraceKind kind_{RadioTraceKind::Good};
};
static_assert(sizeof(RadioTraceRing) <= 1088U, "bounded trace RAM");

// Fixed grammar, no newline. Source stream establishes TX/RX role; context
// words are untrusted packet fields, not accepted/authenticated identities.
inline std::size_t formatRadioTrace(const RadioTraceRecord& record, char* out, std::size_t capacity) {
  if (out == nullptr || capacity == 0U) return 0;
  const auto& s = record.sample;
  const int n = std::snprintf(out, capacity,
      "RTE g%lu k%u i%u c%u d%lu w%lu t%lu e%u v%u p%u a%08lX r%d",
      static_cast<unsigned long>(record.generation), static_cast<unsigned>(record.kind),
      record.index, record.count, static_cast<unsigned long>(record.dropped),
      static_cast<unsigned long>(record.historyOverwrites), static_cast<unsigned long>(s.micros),
      static_cast<unsigned>(s.event), s.validity, s.pins, static_cast<unsigned long>(s.value), s.result);
  if (n <= 0 || static_cast<std::size_t>(n) >= capacity || n >= 118) { out[0] = '\0'; return 0; }
  return static_cast<std::size_t>(n);
}
}  // namespace fanlink
