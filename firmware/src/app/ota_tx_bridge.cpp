#include "fanlink/ota_tx_bridge.hpp"
#include <algorithm>
#include "fanlink/byte_utils.hpp"

namespace fanlink::ota {
bool TxBridge::enqueue(const std::uint8_t* data, std::size_t length,
                       std::uint16_t sequence) {
  Packet packet{};
  if (pending_ || haveReply_ || !decodeRequest(data, length, packet) ||
      packet.networkId != network_ || packet.operation == Op::Fragment ||
      packet.operation == Op::BlockFinish) return false;
  if (!active_) {
    if (packet.operation != Op::Begin) return false;
    active_ = true;
    session_ = packet.session;
    imageSize_ = packet.offset;
    compatibility_ = bytes::getU32Le(packet.data.data());
    node_ = packet.nodeId;
    startedMs_ = clock_.millis();
    std::array<std::uint8_t, kNodeCount> full{};
    full.fill(255);
    controller_.setAllDuty(full);
  } else if (packet.session != session_ || packet.nodeId != node_) {
    return false;
  }
  request_ = packet;
  std::copy_n(data, 30, requestBytes_.begin());
  sequence_ = sequence;
  lastRequestMs_ = clock_.millis();
  pending_ = true;
  blockCount_ = 0;
  return true;
}

bool TxBridge::enqueueBlock(const std::uint8_t* data, std::size_t length,
                            std::uint16_t sequence) {
  Packet packet{};
  if (!active_ || pending_ || haveReply_ || length < 30U ||
      !decodeRequest(data, 30U, packet) || packet.operation != Op::Fragment ||
      packet.networkId != network_ || packet.session != session_ ||
      packet.nodeId != node_ || packet.offset >= imageSize_ ||
      packet.offset % kBlockSize != 0U) return false;
  const auto count = std::min<std::uint32_t>(kBlockSize, imageSize_ - packet.offset);
  const auto first = std::min(10U, count);
  if (length != 30U + count - first) return false;
  for (std::size_t i = first; i < 10U; ++i)
    if (packet.data[i] != 0U) return false;
  std::copy_n(packet.data.begin(), first, block_.begin());
  std::copy_n(data + 30U, count - first, block_.begin() + first);
  request_ = packet;
  sequence_ = sequence;
  lastRequestMs_ = clock_.millis();
  pending_ = true;
  blockCount_ = count;
  return true;
}

void TxBridge::release() {
  // setAllDuty preserves full speed; no buffered old duty may be replayed by
  // callers. This is deliberately not a replay of the previous host setting.
  active_ = pending_ = false;
  std::array<std::uint8_t, kNodeCount> full{};
  full.fill(255);
  controller_.setAllDuty(full);
}

bool TxBridge::service() {
  if (!active_) return false;
  if (static_cast<std::uint32_t>(clock_.millis() - lastRequestMs_) >= 10000U) {
    release();
    return true;
  }
  if (static_cast<std::uint32_t>(clock_.millis() - startedMs_) < 600U) {
    controller_.runScheduledSlot();
    return true;
  }
  if (!pending_) return true;
  const auto imageCount = blockCount_ != 0U ? blockCount_ :
      (request_.operation == Op::Data && request_.offset < imageSize_ ?
       std::min(10U, imageSize_ - request_.offset) : 0U);
  if (imageCount != 0U) {
    const auto nowUs = clock_.micros();
    if (haveImageSend_ &&
        static_cast<std::uint32_t>(nowUs - lastImageSendUs_) < imageSendIntervalUs_)
      return true;  // Cooperative wait; keep the same pending request.
    haveImageSend_ = true;
    lastImageSendUs_ = nowUs;
    imageSendIntervalUs_ = static_cast<std::uint32_t>(
        (imageCount * 1000000U + kImageBytesPerSecond - 1U) / kImageBytesPerSecond);
    // Each attempt consumes the budget, including retransmissions/failures.
    // No credit accumulates while idle, and microsecond rollover is unsigned.
  }
  pending_ = false;
  if (blockCount_ != 0U) {
    const auto startOffset = request_.offset;
    for (std::size_t offset = 0; offset < blockCount_; offset += 10U) {
      request_.offset = startOffset + offset;
      request_.data = {};
      std::copy_n(block_.begin() + offset, std::min<std::size_t>(10U, blockCount_ - offset),
                  request_.data.begin());
      if (!encodeRequest(request_, requestBytes_) ||
          radio_.transmit(requestBytes_.data(), requestBytes_.size()) != RadioResult::Ok) {
        (void)radio_.begin(network_);
        return true;
      }
      // Allow RX FIFO consumption/rearm without changing normal RF slot timing.
      clock_.delayMicroseconds(1000U);
    }
    request_.operation = Op::BlockFinish;
    request_.offset = startOffset + blockCount_;
    request_.data = {};
    (void)encodeRequest(request_, requestBytes_);
    blockCount_ = 0;
  }
  if (radio_.transmit(requestBytes_.data(), requestBytes_.size()) != RadioResult::Ok) {
    (void)radio_.begin(network_);
    return true;
  }
  const auto start = clock_.millis();
  for (unsigned attempts = 0; attempts < 64; ++attempts) {
    const auto elapsed = static_cast<std::uint32_t>(clock_.millis() - start);
    if (elapsed >= 300U) break;
    std::array<std::uint8_t, 30> bytes{};
    const auto received = radio_.receive(bytes.data(), bytes.size(),
                                         std::min(25U, 300U - elapsed));
    if (received.result == RadioResult::IoError) {
      (void)radio_.begin(network_);
      break;
    }
    Packet reply{};
    if (received.result != RadioResult::Ok ||
        static_cast<std::uint32_t>(clock_.millis() - start) >= 300U ||
        !decodeReply(bytes.data(), received.length, reply) ||
        reply.networkId != network_ || reply.session != session_ ||
        reply.nodeId != node_ || reply.operation != request_.operation) continue;
    if (reply.data[0] == 0) {
      std::uint32_t expectedOffset = 0;
      State expectedState = State::Manifest;
      switch (request_.operation) {
        case Op::Begin: break;
        case Op::Hash:
          expectedState = request_.offset == 10 ? State::Receiving : State::Manifest;
          break;
        case Op::Data:
          if (request_.offset >= imageSize_) continue;
          expectedOffset = request_.offset + std::min(10U, imageSize_ - request_.offset);
          expectedState = State::Receiving;
          break;
        case Op::Finish: expectedOffset = imageSize_; expectedState = State::Verified; break;
        case Op::Commit: expectedOffset = imageSize_; expectedState = State::Committed; break;
        case Op::Abort: expectedState = State::Idle; break;
        case Op::Fragment: continue;
        case Op::BlockFinish:
          expectedOffset = request_.offset;
          expectedState = State::Receiving;
          break;
      }
      if (reply.offset != expectedOffset ||
          reply.data[1] != static_cast<std::uint8_t>(expectedState) ||
          fanlink::bytes::getU32Le(reply.data.data() + 2) != compatibility_) continue;
    }
    replyBytes_ = bytes;
    haveReply_ = true;
    // Retain the maintenance session after Abort too, so a lost serial ACK can
    // be retried against the RX's cached Abort ACK; idle timeout releases it.
    break;
  }
  return true;
}

bool TxBridge::takeReply(std::array<std::uint8_t, 30>& data,
                         std::uint16_t& sequence) {
  if (!haveReply_) return false;
  data = replyBytes_;
  sequence = sequence_;
  haveReply_ = false;
  return true;
}
}  // namespace fanlink::ota
