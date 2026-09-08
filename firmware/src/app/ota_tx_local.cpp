#include "fanlink/ota_tx_local.hpp"
#include "fanlink/byte_utils.hpp"

namespace fanlink::ota {
bool TxLocalUpdate::enqueue(const std::uint8_t* data, std::size_t length,
                             std::uint16_t sequence, bool block) {
  Packet p{};
  if (pending_ || haveReply_ || length < 30 || !decodeRequest(data, 30, p) ||
      p.networkId != network_ || p.nodeId != node_) return false;
  if (!active_) {
    // A lost Abort ACK is retryable after releasing maintenance, without
    // reopening storage or repeating a full-speed preamble.
    if (!block && length == 30 && p.operation == Op::Abort &&
        request_.operation == Op::Abort && p.session == session_ && p.offset == 0 &&
        std::all_of(p.data.begin(), p.data.end(), [](std::uint8_t b) { return b == 0; })) {
      active_ = haveReply_ = true; sequence_ = sequence; started_ = clock_.millis() - 600U;
      fan_.setFullSpeed(); return true;
    }
    if (block || length != 30 || p.operation != Op::Begin || p.offset == 0 ||
        p.session == session_ ||
        p.offset > kMaxImageSize || bytes::getU32Le(p.data.data()) != compatibility_ ||
        !std::all_of(p.data.begin() + 4, p.data.end(), [](std::uint8_t b) { return b == 0; }))
      return false;
    imageSize_ = p.offset; session_ = p.session;
    active_ = true; started_ = clock_.millis(); fan_.setFullSpeed();
  } else if (p.session != session_) return false;
  blockCount_ = 0;
  if (block) {
    if (p.operation != Op::Fragment || p.offset >= imageSize_ || p.offset % kBlockSize != 0)
      return false;
    const auto count = std::min<std::uint32_t>(kBlockSize, imageSize_ - p.offset);
    const auto first = std::min(10U, count);
    if (length != 30 + count - first ||
        !std::all_of(p.data.begin() + first, p.data.end(), [](std::uint8_t b) { return b == 0; }))
      return false;
    std::copy_n(p.data.begin(), first, block_.begin());
    std::copy_n(data + 30, count - first, block_.begin() + first);
    blockCount_ = count;
  } else if (length != 30 || p.operation == Op::Fragment || p.operation == Op::BlockFinish)
    return false;
  request_ = p; sequence_ = sequence; pending_ = true;
  return true;
}
bool TxLocalUpdate::service() {
  if (!active_) return false;
  if (static_cast<std::uint32_t>(clock_.millis() - started_) < 600) {
    controller_.runScheduledSlot(); return true;
  }
  receiver_.tick(clock_.millis());
  if (pending_) {
    pending_ = false;
    Packet response{};
    if (blockCount_ != 0) {
      const auto start = request_.offset;
      for (std::size_t offset = 0; offset < blockCount_; offset += 10) {
        request_.offset = start + offset; request_.data = {};
        std::copy_n(block_.begin() + offset, std::min<std::size_t>(10, blockCount_ - offset), request_.data.begin());
        (void)receiver_.process(request_, response, clock_.millis());
      }
      request_.operation = Op::BlockFinish; request_.offset = start + blockCount_; request_.data = {};
    }
    if (receiver_.process(request_, response, clock_.millis()))
      haveReply_ = encodeReply(response, reply_);
  }
  if (!receiver_.active()) { active_ = false; fan_.setFullSpeed(); }
  return true;
}
bool TxLocalUpdate::takeReply(std::array<std::uint8_t, 30>& data, std::uint16_t& sequence) {
  if (!haveReply_) return false;
  data = reply_; sequence = sequence_; haveReply_ = false; return true;
}
}  // namespace fanlink::ota
