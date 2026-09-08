#include "fanlink/ota_receiver.hpp"

#include <algorithm>

#include "fanlink/byte_utils.hpp"
#include "fanlink/protocol.hpp"

namespace fanlink::ota {
namespace {
bool same(const Packet& a, const Packet& b) {
  return a.networkId == b.networkId && a.session == b.session &&
         a.nodeId == b.nodeId && a.operation == b.operation &&
         a.offset == b.offset && a.data == b.data;
}
bool zeroFrom(const Packet& request, std::size_t first) {
  for (std::size_t i = first; i < request.data.size(); ++i)
    if (request.data[i] != 0U) return false;
  return true;
}
}  // namespace

Receiver::Receiver(IUpdateStorage& storage, IFanOutput& fan, std::uint32_t network,
                   std::uint8_t node, std::uint32_t compatibility)
    : storage_(storage), fan_(fan), network_(network), node_(node),
      compatibility_(compatibility) {}

bool Receiver::active() const {
  return state_ == State::Manifest || state_ == State::Receiving ||
         state_ == State::Verified || state_ == State::Committed;
}

bool Receiver::rebootDue(std::uint32_t nowMs) const {
  return state_ == State::Committed &&
         static_cast<std::uint32_t>(nowMs - committedMs_) >= kRebootGraceMs;
}

void Receiver::fail() {
  fan_.setFullSpeed();
  storage_.abort();
  state_ = State::Failed;
  haveCached_ = false;
}

void Receiver::tick(std::uint32_t nowMs) {
  if (active() && state_ != State::Committed &&
      static_cast<std::uint32_t>(nowMs - lastProgressMs_) >= kProgressTimeoutMs) fail();
}

void Receiver::respond(const Packet& request, Packet& reply, Status status) const {
  reply = {};
  reply.networkId = network_;
  reply.session = request.session;
  reply.nodeId = node_;
  reply.operation = request.operation;
  reply.offset = nextOffset_;
  reply.data[0] = static_cast<std::uint8_t>(status);
  reply.data[1] = static_cast<std::uint8_t>(state_);
  bytes::putU32Le(reply.data.data() + 2U, compatibility_);
}

bool Receiver::process(const Packet& request, Packet& reply, std::uint32_t nowMs) {
  tick(nowMs);
  const auto operation = static_cast<std::uint8_t>(request.operation);
  if (node_ < 1U || node_ > kNodeCount || request.networkId != network_ ||
      request.nodeId != node_ || request.session == 0U || operation < 1U || operation > 8U)
    return false;
  if (request.session != session_) {
    if (request.operation != Op::Begin) return false;
    if (active()) {
      respond(request, reply, Status::Busy);
      return true;
    }
  }
  // Fragments never generate unsolicited ACKs. Retain the last committed block
  // so a lost block ACK can be retried without writing twice or accepting edits.
  if (request.operation == Op::Fragment) {
    if (state_ != State::Receiving || dataMode_ == 1U || request.offset >= size_)
      return false;
    if (request.offset < blockStart_) return false;
    if (request.offset >= blockStart_ + kBlockSize && nextOffset_ > blockStart_) {
      blockStart_ = nextOffset_;
      blockMask_ = 0;
      blockBad_ = false;
    }
    const auto relative = request.offset - blockStart_;
    if (relative >= kBlockSize || relative % 10U != 0U) return false;
    dataMode_ = 2U;
    const auto count = std::min(10U, size_ - request.offset);
    if (!zeroFrom(request, count)) { blockBad_ = true; return false; }
    const auto bit = static_cast<std::uint16_t>(1U << (relative / 10U));
    if ((blockMask_ & bit) != 0U) {
      if (!std::equal(request.data.begin(), request.data.begin() + count,
                      block_.begin() + relative)) blockBad_ = true;
    } else {
      std::copy_n(request.data.begin(), count, block_.begin() + relative);
      blockMask_ |= bit;
    }
    return false;  // Only a complete BlockFinish refreshes the progress timer.
  }
  if (request.operation == Op::BlockFinish && blockBad_) {
    respond(request, reply, Status::BadImage);
    return true;
  }
  if (haveCached_ && same(request, lastRequest_)) {
    reply = lastReply_;
    return true;  // Lost ACK retries never repeat writes or refresh progress time.
  }
  auto reject = [&](Status status) {
    respond(request, reply, status);
    return true;
  };
  // A modified retransmission is not a new operation, even if it still has a
  // plausible offset. In particular it must not reset a Begin or re-commit.
  if (haveCached_ && request.session == lastRequest_.session &&
      request.operation == lastRequest_.operation && request.offset == lastRequest_.offset)
    return reject(Status::BadImage);

  switch (request.operation) {
    case Op::Begin:
      // A terminated transfer needs a new session: replayed Begin packets must
      // not reopen staging after an abort, failure or progress timeout.
      if (active() || request.session == session_) return reject(Status::BadState);
      if (!zeroFrom(request, 4U) || request.offset == 0U || request.offset > kMaxImageSize)
        return reject(Status::BadImage);
      if (bytes::getU32Le(request.data.data()) != compatibility_)
        return reject(Status::BadTarget);
      fan_.setFullSpeed();
      session_ = request.session;
      size_ = request.offset;
      nextOffset_ = 0U;
      hashOffset_ = 0U;
      md5_ = {};
      block_ = {};
      blockStart_ = 0;
      blockMask_ = 0;
      blockBad_ = false;
      dataMode_ = 0;
      state_ = State::Manifest;
      break;
    case Op::Hash: {
      if (state_ != State::Manifest) return reject(Status::BadState);
      if (request.offset != hashOffset_) return reject(Status::BadOffset);
      const std::size_t count = hashOffset_ == 0U ? 10U : 6U;
      if (!zeroFrom(request, count)) return reject(Status::BadImage);
      std::copy_n(request.data.begin(), count, md5_.begin() + hashOffset_);
      hashOffset_ = static_cast<std::uint8_t>(hashOffset_ + count);
      if (hashOffset_ == md5_.size()) {
        if (!storage_.begin(size_, md5_)) {
          fail();
          return reject(Status::StorageError);
        }
        state_ = State::Receiving;
      }
      break;
    }
    case Op::Data: {
      if (state_ != State::Receiving) return reject(Status::BadState);
      if (dataMode_ == 2U) return reject(Status::BadState);
      dataMode_ = 1U;
      if (request.offset != nextOffset_ || nextOffset_ >= size_)
        return reject(Status::BadOffset);
      const std::size_t count = std::min<std::uint32_t>(10U, size_ - nextOffset_);
      if (!zeroFrom(request, count)) return reject(Status::BadImage);
      if (!storage_.write(request.data.data(), count)) {
        fail();
        return reject(Status::StorageError);
      }
      nextOffset_ += static_cast<std::uint32_t>(count);
      break;
    }
    case Op::Fragment: return false;  // Handled above, with no per-fragment ACK.
    case Op::BlockFinish: {
      if (state_ != State::Receiving || dataMode_ != 2U)
        return reject(Status::BadState);
      if (!zeroFrom(request, 0U)) return reject(Status::BadImage);
      const auto count = std::min<std::uint32_t>(kBlockSize, size_ - blockStart_);
      const auto mask = static_cast<std::uint16_t>((1U << ((count + 9U) / 10U)) - 1U);
      if (blockStart_ != nextOffset_ || request.offset != blockStart_ + count ||
          blockMask_ != mask) return reject(Status::BadOffset);
      if (!storage_.write(block_.data(), count)) {
        fail();
        return reject(Status::StorageError);
      }
      nextOffset_ += count;
      break;
    }
    case Op::Finish:
      if (state_ != State::Receiving) return reject(Status::BadState);
      if (request.offset != size_ || nextOffset_ != size_) return reject(Status::BadOffset);
      if (!zeroFrom(request, 0U)) return reject(Status::BadImage);
      if (!storage_.verify()) {
        fail();
        return reject(Status::BadImage);
      }
      state_ = State::Verified;
      break;
    case Op::Commit:
      if (state_ != State::Verified) return reject(Status::BadState);
      if (request.offset != size_) return reject(Status::BadOffset);
      if (!zeroFrom(request, 0U)) return reject(Status::BadImage);
      if (!storage_.commit()) {
        fail();
        return reject(Status::StorageError);
      }
      state_ = State::Committed;
      committedMs_ = nowMs;
      break;
    case Op::Abort:
      if (state_ == State::Committed) return reject(Status::BadState);
      if (request.offset != 0U) return reject(Status::BadOffset);
      if (!zeroFrom(request, 0U)) return reject(Status::BadImage);
      fan_.setFullSpeed();
      storage_.abort();
      state_ = State::Idle;
      nextOffset_ = 0U;
      break;
  }
  lastProgressMs_ = nowMs;
  respond(request, reply, Status::Ok);
  lastRequest_ = request;
  lastReply_ = reply;
  haveCached_ = true;
  return true;
}
}  // namespace fanlink::ota
