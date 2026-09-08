#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "fanlink/protocol.hpp"

namespace fanlink {

inline constexpr std::uint8_t kSerialMagic0 = 0x46U;  // 'F'
inline constexpr std::uint8_t kSerialMagic1 = 0x4CU;  // 'L'
inline constexpr std::uint8_t kSerialProtocolVersion = 0x02U;
inline constexpr std::size_t kSerialMaxPayload = 128U;
inline constexpr std::size_t kSerialHeaderSize = 8U;
inline constexpr std::size_t kSerialCrcSize = 2U;
inline constexpr std::size_t kSetAllDutyPayloadSize = kNodeCount;
inline constexpr std::size_t kStatusSnapshotPayloadSize = 6U + 6U * kNodeCount;
inline constexpr std::size_t kLinkMetricsPayloadSize = 2U * kNodeCount;
inline constexpr std::size_t kSerialMaxFrameSize =
    kSerialHeaderSize + kSerialMaxPayload + kSerialCrcSize;

namespace serial_type {
inline constexpr std::uint8_t kSetAllDuty = 0x01U;
inline constexpr std::uint8_t kSetOneDuty = 0x02U;
inline constexpr std::uint8_t kHeartbeat = 0x03U;
inline constexpr std::uint8_t kStatusSnapshot = 0x80U;
inline constexpr std::uint8_t kErrorEvent = 0x81U;
inline constexpr std::uint8_t kLinkMetricsSnapshot = 0x82U;
}  // namespace serial_type

struct SerialFrame {
  std::uint8_t type{0};
  std::uint16_t sequence{0};
  std::uint16_t payloadLength{0};
  std::array<std::uint8_t, kSerialMaxPayload> payload{};
};

bool encodeSerialFrame(const SerialFrame& frame,
                       std::array<std::uint8_t, kSerialMaxFrameSize>& out,
                       std::size_t& encodedLength);

class SerialFrameParser {
 public:
  bool feed(std::uint8_t byte, SerialFrame& completedFrame);
  void reset();
  std::uint32_t rejectedFrameCount() const { return rejectedFrameCount_; }

 private:
  enum class State : std::uint8_t { SeekMagic0, SeekMagic1, ReadFrame };

  bool tryComplete(SerialFrame& completedFrame);
  void rejectAndResync(std::uint8_t lastByte);

  State state_{State::SeekMagic0};
  std::array<std::uint8_t, kSerialMaxFrameSize> buffer_{};
  std::size_t length_{0};
  std::size_t expectedLength_{0};
  std::uint32_t rejectedFrameCount_{0};
};

}  // namespace fanlink
