#include "fanlink/ota_protocol.hpp"

#include <algorithm>

#include "fanlink/byte_utils.hpp"
#include "fanlink/crc16.hpp"
#include "fanlink/protocol.hpp"

namespace fanlink::ota {
namespace {
bool valid(const Packet& packet, bool reply) {
  const auto op = static_cast<std::uint8_t>(packet.operation);
  if (packet.session == 0U || packet.nodeId < 1U || packet.nodeId > kNodeCount ||
      op < 1U || op > 8U) return false;
  if (reply) {
    if (packet.data[0] > static_cast<std::uint8_t>(Status::Busy) ||
        packet.data[1] > static_cast<std::uint8_t>(State::Failed)) return false;
    for (std::size_t i = 6; i < packet.data.size(); ++i)
      if (packet.data[i] != 0U) return false;
  }
  return true;
}

bool encode(const Packet& packet, std::array<std::uint8_t, kPacketSize>& out, bool reply) {
  if (!valid(packet, reply)) return false;
  out = {};
  out[0] = 0xF1U;
  out[1] = 0xA5U;
  out[2] = 1U;
  out[3] = reply ? kReplyType : kRequestType;
  bytes::putU32Le(out.data() + 4U, packet.networkId);
  bytes::putU32Le(out.data() + 8U, packet.session);
  out[12] = packet.nodeId;
  out[13] = static_cast<std::uint8_t>(packet.operation);
  bytes::putU32Le(out.data() + 14U, packet.offset);
  std::copy(packet.data.begin(), packet.data.end(), out.begin() + 18U);
  bytes::putU16Le(out.data() + 28U, crc16CcittFalse(out.data(), 28U));
  return true;
}

bool decode(const std::uint8_t* data, std::size_t length, Packet& out, bool reply) {
  if (data == nullptr || length != kPacketSize || data[0] != 0xF1U ||
      data[1] != 0xA5U || data[2] != 1U ||
      data[3] != (reply ? kReplyType : kRequestType) ||
      bytes::getU16Le(data + 28U) != crc16CcittFalse(data, 28U)) return false;
  Packet decoded{};
  decoded.networkId = bytes::getU32Le(data + 4U);
  decoded.session = bytes::getU32Le(data + 8U);
  decoded.nodeId = data[12];
  decoded.operation = static_cast<Op>(data[13]);
  decoded.offset = bytes::getU32Le(data + 14U);
  std::copy(data + 18U, data + 28U, decoded.data.begin());
  if (!valid(decoded, reply)) return false;
  out = decoded;
  return true;
}
}  // namespace

bool encodeRequest(const Packet& packet, std::array<std::uint8_t, kPacketSize>& out) {
  return encode(packet, out, false);
}
bool encodeReply(const Packet& packet, std::array<std::uint8_t, kPacketSize>& out) {
  return encode(packet, out, true);
}
bool decodeRequest(const std::uint8_t* data, std::size_t length, Packet& out) {
  return decode(data, length, out, false);
}
bool decodeReply(const std::uint8_t* data, std::size_t length, Packet& out) {
  return decode(data, length, out, true);
}
}  // namespace fanlink::ota
