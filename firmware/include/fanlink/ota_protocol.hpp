#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace fanlink::ota {

inline constexpr std::size_t kPacketSize = 30U;
inline constexpr std::uint8_t kRequestType = 0x30U;
inline constexpr std::uint8_t kReplyType = 0x31U;
inline constexpr std::uint32_t kMaxImageSize = 512U * 1024U;
inline constexpr std::uint32_t kProgressTimeoutMs = 10000U;
inline constexpr std::uint32_t kRebootGraceMs = 2500U;
inline constexpr std::size_t kBlockSize = 100U;
inline constexpr std::uint32_t kImageBytesPerSecond = 5120U;

enum class Op : std::uint8_t { Begin = 1, Hash = 2, Data = 3, Finish = 4, Abort = 5, Commit = 6, Fragment = 7, BlockFinish = 8 };
enum class Status : std::uint8_t {
  Ok = 0, BadState = 1, BadTarget = 2, BadOffset = 3,
  BadImage = 4, StorageError = 5, Busy = 6
};
enum class State : std::uint8_t {
  Idle = 0, Manifest = 1, Receiving = 2, Verified = 3, Committed = 4, Failed = 5
};

struct Packet {
  std::uint32_t networkId{0};
  std::uint32_t session{0};
  std::uint8_t nodeId{1};
  Op operation{Op::Begin};
  std::uint32_t offset{0};
  std::array<std::uint8_t, 10> data{};
};

bool encodeRequest(const Packet& packet, std::array<std::uint8_t, kPacketSize>& out);
bool encodeReply(const Packet& packet, std::array<std::uint8_t, kPacketSize>& out);
bool decodeRequest(const std::uint8_t* bytes, std::size_t length, Packet& out);
bool decodeReply(const std::uint8_t* bytes, std::size_t length, Packet& out);

}  // namespace fanlink::ota
