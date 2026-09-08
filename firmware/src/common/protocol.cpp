#include "fanlink/protocol.hpp"

#include <algorithm>

#include "fanlink/byte_utils.hpp"
#include "fanlink/crc16.hpp"

namespace fanlink {
namespace {

bool hasValidEnvelope(const std::uint8_t* data, std::size_t length, std::size_t expectedLength,
                      std::uint8_t expectedType) {
  if (data == nullptr || length != expectedLength) {
    return false;
  }
  if (data[0] != kAirMagic0 || data[1] != kAirMagic1 || data[2] != kProtocolVersion ||
      data[3] != expectedType) {
    return false;
  }
  const auto receivedCrc = bytes::getU16Le(data + expectedLength - 2U);
  const auto computedCrc = crc16CcittFalse(data, expectedLength - 2U);
  return receivedCrc == computedCrc;
}

}  // namespace

bool isValidNodeId(std::uint8_t nodeId) { return nodeId >= 1U && nodeId <= kNodeCount; }

bool encodeControlPacket(const ControlPacket& packet,
                         std::array<std::uint8_t, kControlPacketSize>& out) {
  if (packet.pollId != 0U && !isValidNodeId(packet.pollId)) {
    return false;
  }

  out.fill(0U);
  out[0] = kAirMagic0;
  out[1] = kAirMagic1;
  out[2] = kProtocolVersion;
  out[3] = kControlPacketType;
  bytes::putU32Le(out.data() + 4U, packet.networkId);
  bytes::putU16Le(out.data() + 8U, packet.sequence);
  out[10] = packet.pollId;
  out[11] = packet.flags;
  std::copy_n(packet.duty.begin(), kNodeCount, out.begin() + 12U);
  // Fixed RF v1 envelope: bytes20..27 are reserved, not extra channels.
  std::fill(out.begin() + 12U + kNodeCount, out.begin() + 28U, 255U);
  bytes::putU16Le(out.data() + 28U, crc16CcittFalse(out.data(), 28U));
  return true;
}

bool decodeControlPacket(const std::uint8_t* data, std::size_t length, ControlPacket& out) {
  if (!hasValidEnvelope(data, length, kControlPacketSize, kControlPacketType)) {
    return false;
  }
  if (data[10] != 0U && !isValidNodeId(data[10])) {
    return false;
  }

  ControlPacket candidate{};
  candidate.networkId = bytes::getU32Le(data + 4U);
  candidate.sequence = bytes::getU16Le(data + 8U);
  candidate.pollId = data[10];
  candidate.flags = data[11];
  std::copy_n(data + 12U, kNodeCount, candidate.duty.begin());
  out = candidate;
  return true;
}

bool encodeRpmPacket(const RpmPacket& packet,
                     std::array<std::uint8_t, kRpmPacketSize>& out) {
  if (!isValidNodeId(packet.nodeId)) {
    return false;
  }

  out.fill(0U);
  out[0] = kAirMagic0;
  out[1] = kAirMagic1;
  out[2] = kProtocolVersion;
  out[3] = kRpmPacketType;
  bytes::putU32Le(out.data() + 4U, packet.networkId);
  bytes::putU16Le(out.data() + 8U, packet.sequenceEcho);
  out[10] = packet.nodeId;
  out[11] = packet.status;
  bytes::putU16Le(out.data() + 12U, packet.rpm);
  out[14] = packet.appliedDuty;
  out[15] = packet.missedDownlinks;
  out[16] = static_cast<std::uint8_t>(packet.downlinkRssiDbm);
  out[17] = 0U;
  bytes::putU16Le(out.data() + 28U, crc16CcittFalse(out.data(), 28U));
  return true;
}

bool decodeRpmPacket(const std::uint8_t* data, std::size_t length, RpmPacket& out) {
  if (!hasValidEnvelope(data, length, kRpmPacketSize, kRpmPacketType) ||
      !isValidNodeId(data[10])) {
    return false;
  }

  RpmPacket candidate{};
  candidate.networkId = bytes::getU32Le(data + 4U);
  candidate.sequenceEcho = bytes::getU16Le(data + 8U);
  candidate.nodeId = data[10];
  candidate.status = data[11];
  candidate.rpm = bytes::getU16Le(data + 12U);
  candidate.appliedDuty = data[14];
  candidate.missedDownlinks = data[15];
  candidate.downlinkRssiDbm = static_cast<std::int8_t>(data[16]);
  out = candidate;
  return true;
}

}  // namespace fanlink
