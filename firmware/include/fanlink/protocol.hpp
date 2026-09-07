#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace fanlink {

inline constexpr std::uint8_t kNodeCount = 16U;
inline constexpr std::uint8_t kAirMagic0 = 0xF1U;
inline constexpr std::uint8_t kAirMagic1 = 0xA5U;
inline constexpr std::uint8_t kProtocolVersion = 0x01U;
inline constexpr std::uint8_t kControlPacketType = 0x10U;
inline constexpr std::uint8_t kRpmPacketType = 0x11U;
inline constexpr std::size_t kAirPacketSize = 30U;
inline constexpr std::size_t kControlPacketSize = kAirPacketSize;
inline constexpr std::size_t kRpmPacketSize = kAirPacketSize;

namespace control_flag {
inline constexpr std::uint8_t kForceFullSpeed = 1U << 0U;
inline constexpr std::uint8_t kHostFailsafe = 1U << 1U;
}  // namespace control_flag

namespace node_status {
inline constexpr std::uint8_t kLinkFailsafe = 1U << 0U;
inline constexpr std::uint8_t kTachTimeout = 1U << 1U;
inline constexpr std::uint8_t kStall = 1U << 2U;
inline constexpr std::uint8_t kSequenceGap = 1U << 3U;
inline constexpr std::uint8_t kRadioError = 1U << 4U;
inline constexpr std::uint8_t kConfigError = 1U << 5U;
inline constexpr std::uint8_t kTelemetryStale = 1U << 7U;  // TX-side synthesized bit.
}  // namespace node_status

struct ControlPacket {
  std::uint32_t networkId{0};
  std::uint16_t sequence{0};
  std::uint8_t pollId{1};
  std::uint8_t flags{0};
  std::array<std::uint8_t, kNodeCount> duty{};
};

struct RpmPacket {
  std::uint32_t networkId{0};
  std::uint16_t sequenceEcho{0};
  std::uint8_t nodeId{1};
  std::uint8_t status{0};
  std::uint16_t rpm{0};
  std::uint8_t appliedDuty{255};
  std::uint8_t missedDownlinks{0};
  std::int8_t downlinkRssiDbm{-127};
};

bool isValidNodeId(std::uint8_t nodeId);

bool encodeControlPacket(const ControlPacket& packet,
                         std::array<std::uint8_t, kControlPacketSize>& out);
bool decodeControlPacket(const std::uint8_t* data, std::size_t length, ControlPacket& out);

bool encodeRpmPacket(const RpmPacket& packet,
                     std::array<std::uint8_t, kRpmPacketSize>& out);
bool decodeRpmPacket(const std::uint8_t* data, std::size_t length, RpmPacket& out);

}  // namespace fanlink
