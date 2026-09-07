#pragma once

#include <cstddef>
#include <cstdint>

namespace fanlink::bytes {

inline void putU16Le(std::uint8_t* out, std::uint16_t value) {
  out[0] = static_cast<std::uint8_t>(value & 0xFFU);
  out[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

inline std::uint16_t getU16Le(const std::uint8_t* in) {
  return static_cast<std::uint16_t>(in[0]) |
         static_cast<std::uint16_t>(static_cast<std::uint16_t>(in[1]) << 8U);
}

inline void putU32Le(std::uint8_t* out, std::uint32_t value) {
  out[0] = static_cast<std::uint8_t>(value & 0xFFU);
  out[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
  out[2] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
  out[3] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

inline std::uint32_t getU32Le(const std::uint8_t* in) {
  return static_cast<std::uint32_t>(in[0]) |
         (static_cast<std::uint32_t>(in[1]) << 8U) |
         (static_cast<std::uint32_t>(in[2]) << 16U) |
         (static_cast<std::uint32_t>(in[3]) << 24U);
}

inline std::uint8_t saturateU8(std::uint32_t value) {
  return value > 255U ? 255U : static_cast<std::uint8_t>(value);
}

inline std::uint8_t addSaturatingU8(std::uint8_t lhs, std::uint32_t rhs) {
  const auto sum = static_cast<std::uint32_t>(lhs) + rhs;
  return saturateU8(sum);
}

}  // namespace fanlink::bytes
