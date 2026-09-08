#pragma once
#include <array>
#include <cstdint>
#include "fanlink/build_config.hpp"

namespace fanlink::ota {
// Explicit little-endian values: stable across compiler/host architectures.
constexpr std::uint32_t identityWord(std::uint32_t hash, std::uint32_t word) {
  for (unsigned i = 0; i < 4; ++i) {
    hash = (hash ^ (word & 255U)) * 16777619U;
    word >>= 8U;
  }
  return hash;
}
constexpr std::uint32_t compatibilityId() {
  std::uint32_t h = 2166136261U;
  const std::uint32_t fields[] = {
    1U, build::kNetworkId, build::kNodeId,
    static_cast<std::uint32_t>(build::kFanPwmPin),
    static_cast<std::uint32_t>(build::kTachPin), build::kGateActiveHigh,
    build::kPwmHz, build::kTachPulsesPerRev,
    static_cast<std::uint32_t>(build::kRadioNssPin),
    static_cast<std::uint32_t>(build::kRadioDio1Pin),
    static_cast<std::uint32_t>(build::kRadioRstPin),
    static_cast<std::uint32_t>(build::kRadioBusyPin),
    static_cast<std::uint32_t>(build::kRadioRxEnPin),
    static_cast<std::uint32_t>(build::kRadioTxEnPin), build::kRadioUseDcdc,
    static_cast<std::uint32_t>(build::kRadioOutputPowerDbm),
    static_cast<std::uint32_t>(build::kRadioFrequencyMhz * 1000.0F),
    static_cast<std::uint32_t>(build::kRadioBitrateKbps),
    build::kRadioCodingRate, build::kRadioPreambleBits,
    static_cast<std::uint32_t>(build::kStatusLedPin), build::kStatusLedActiveHigh,
  };
  for (auto value : fields) h = identityWord(h, value);
  return h;
}
constexpr std::array<std::uint8_t, 28> imageIdentity(std::uint8_t role) {
  std::array<std::uint8_t, 28> result{{
    'F','A','N','L','I','N','K','-','O','T','A','-','V','1','!','!'}};
  const auto compatibility = compatibilityId();
  for (unsigned i = 0; i < 4; ++i) {
    result[16 + i] = static_cast<std::uint8_t>(compatibility >> (8U * i));
    result[20 + i] = static_cast<std::uint8_t>(build::kNetworkId >> (8U * i));
  }
  result[24] = role;
  result[25] = build::kNodeId;
  result[26] = 1;
  return result;
}
}  // namespace fanlink::ota
