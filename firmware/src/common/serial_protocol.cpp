#include "fanlink/serial_protocol.hpp"

#include <algorithm>

#include "fanlink/byte_utils.hpp"
#include "fanlink/crc16.hpp"

namespace fanlink {

bool encodeSerialFrame(const SerialFrame& frame,
                       std::array<std::uint8_t, kSerialMaxFrameSize>& out,
                       std::size_t& encodedLength) {
  if (frame.payloadLength > kSerialMaxPayload) {
    encodedLength = 0U;
    return false;
  }

  encodedLength = kSerialHeaderSize + frame.payloadLength + kSerialCrcSize;
  out.fill(0U);
  out[0] = kSerialMagic0;
  out[1] = kSerialMagic1;
  out[2] = kSerialProtocolVersion;
  out[3] = frame.type;
  bytes::putU16Le(out.data() + 4U, frame.payloadLength);
  bytes::putU16Le(out.data() + 6U, frame.sequence);
  std::copy_n(frame.payload.begin(), frame.payloadLength, out.begin() + kSerialHeaderSize);
  bytes::putU16Le(out.data() + kSerialHeaderSize + frame.payloadLength,
                  crc16CcittFalse(out.data(), kSerialHeaderSize + frame.payloadLength));
  return true;
}

void SerialFrameParser::reset() {
  state_ = State::SeekMagic0;
  length_ = 0U;
  expectedLength_ = 0U;
}

void SerialFrameParser::rejectAndResync(std::uint8_t lastByte) {
  ++rejectedFrameCount_;
  length_ = 0U;
  expectedLength_ = 0U;
  if (lastByte == kSerialMagic0) {
    buffer_[0] = lastByte;
    length_ = 1U;
    state_ = State::SeekMagic1;
  } else {
    state_ = State::SeekMagic0;
  }
}

bool SerialFrameParser::tryComplete(SerialFrame& completedFrame) {
  if (expectedLength_ == 0U || length_ != expectedLength_) {
    return false;
  }

  const auto payloadLength = bytes::getU16Le(buffer_.data() + 4U);
  const auto receivedCrc = bytes::getU16Le(buffer_.data() + expectedLength_ - kSerialCrcSize);
  const auto computedCrc = crc16CcittFalse(buffer_.data(), expectedLength_ - kSerialCrcSize);
  if (buffer_[2] != kSerialProtocolVersion || receivedCrc != computedCrc) {
    const auto lastByte = buffer_[expectedLength_ - 1U];
    rejectAndResync(lastByte);
    return false;
  }

  SerialFrame candidate{};
  candidate.type = buffer_[3];
  candidate.payloadLength = payloadLength;
  candidate.sequence = bytes::getU16Le(buffer_.data() + 6U);
  std::copy_n(buffer_.begin() + kSerialHeaderSize, payloadLength, candidate.payload.begin());
  completedFrame = candidate;
  reset();
  return true;
}

bool SerialFrameParser::feed(std::uint8_t byte, SerialFrame& completedFrame) {
  switch (state_) {
    case State::SeekMagic0:
      if (byte == kSerialMagic0) {
        buffer_[0] = byte;
        length_ = 1U;
        state_ = State::SeekMagic1;
      }
      return false;

    case State::SeekMagic1:
      if (byte == kSerialMagic1) {
        buffer_[1] = byte;
        length_ = 2U;
        state_ = State::ReadFrame;
      } else if (byte == kSerialMagic0) {
        buffer_[0] = byte;
        length_ = 1U;
      } else {
        reset();
      }
      return false;

    case State::ReadFrame:
      if (length_ >= buffer_.size()) {
        rejectAndResync(byte);
        return false;
      }
      buffer_[length_++] = byte;

      if (length_ == 6U) {
        const auto payloadLength = bytes::getU16Le(buffer_.data() + 4U);
        if (payloadLength > kSerialMaxPayload) {
          rejectAndResync(byte);
          return false;
        }
        expectedLength_ = kSerialHeaderSize + payloadLength + kSerialCrcSize;
      }

      return tryComplete(completedFrame);
  }

  reset();
  return false;
}

}  // namespace fanlink
