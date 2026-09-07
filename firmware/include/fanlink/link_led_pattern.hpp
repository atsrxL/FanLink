#pragma once

#include <cstdint>

namespace fanlink::link_led {

inline constexpr std::uint32_t kFlashOnMs = 100U;
inline constexpr std::uint32_t kBetweenFlashesMs = 100U;
inline constexpr std::uint32_t kAfterDoubleFlashMs = 500U;
inline constexpr std::uint32_t kSecondFlashStartMs = kFlashOnMs + kBetweenFlashesMs;
inline constexpr std::uint32_t kSearchCycleMs =
    kSecondFlashStartMs + kFlashOnMs + kAfterDoubleFlashMs;

// Searching: 100 ms on, 100 ms off, 100 ms on, then 500 ms off.
// Connected: continuously on. The caller supplies wrap-safe elapsed time.
constexpr bool shouldBeOn(bool connected, std::uint32_t searchElapsedMs) {
  if (connected) {
    return true;
  }
  const auto phaseMs = searchElapsedMs % kSearchCycleMs;
  return phaseMs < kFlashOnMs ||
         (phaseMs >= kSecondFlashStartMs &&
          phaseMs < kSecondFlashStartMs + kFlashOnMs);
}

}  // namespace fanlink::link_led
