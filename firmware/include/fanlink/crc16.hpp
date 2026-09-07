#pragma once

#include <cstddef>
#include <cstdint>

namespace fanlink {

// CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, refin/refout false, xorout 0.
std::uint16_t crc16CcittFalse(const std::uint8_t* data, std::size_t length,
                             std::uint16_t initial = 0xFFFFU);

}  // namespace fanlink
