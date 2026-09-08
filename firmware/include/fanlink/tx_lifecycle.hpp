#pragma once
#include <algorithm>
#include "fanlink/byte_utils.hpp"
#include "fanlink/serial_protocol.hpp"
#include "fanlink/ota_tx_local.hpp"

namespace fanlink {
// Host-only lifecycle gate; platform transition is explicitly acknowledged by
// caller only after the complete serial ACK has been accepted for transmission.
class TxLifecycle {
 public:
  TxLifecycle(IClock& clock, TxController& controller, std::uint32_t network)
      : clock_(clock), controller_(controller), fan_(controller), network_(network) {}
  bool enqueue(const SerialFrame& frame, bool busy) {
    constexpr std::array<std::uint8_t, 7> guard{{'F','L','T','X','C','T','L'}};
    if (frame.payloadLength != 16 || bytes::getU32Le(frame.payload.data()) != network_ ||
        bytes::getU32Le(frame.payload.data() + 4) == 0 ||
        (frame.payload[8] != 1 && frame.payload[8] != 2) ||
        !std::equal(guard.begin(), guard.end(), frame.payload.begin() + 9)) return false;
    if (haveReply_) return false;
    if (active_ && !std::equal(request_.payload.begin(), request_.payload.begin() + 16,
                                frame.payload.begin())) busy = true;
    reply_ = frame; reply_.type = 0x92; reply_.payloadLength = 17;
    reply_.payload[16] = busy ? 1 : 0;
    haveReply_ = true;
    if (!busy && !active_) {
      request_ = frame; active_ = true; started_ = clock_.millis(); fan_.setFullSpeed();
    }
    return true;
  }
  bool service() {
    if (!active_) return false;
    if (!acknowledged_ && static_cast<std::uint32_t>(clock_.millis() - started_) >= 10000) {
      active_ = haveReply_ = false; fan_.setFullSpeed(); return true;
    }
    if (static_cast<std::uint32_t>(clock_.millis() - started_) < 600) controller_.runScheduledSlot();
    return true;
  }
  bool replyReady(SerialFrame& frame) const {
    if (!haveReply_ || (reply_.payload[16] == 0 &&
        static_cast<std::uint32_t>(clock_.millis() - started_) < 600)) return false;
    frame = reply_; return true;
  }
  void replyWritten(bool success) {
    if (!haveReply_) return;
    if (success && reply_.payload[16] == 0 && !acknowledged_) {
      acknowledged_ = true; acknowledgedMs_ = clock_.millis();
    }
    haveReply_ = false; // Failed writes require an explicit host retry.
  }
  std::uint8_t dueAction() const {
    return acknowledged_ && static_cast<std::uint32_t>(clock_.millis() - acknowledgedMs_) >= 250
        ? request_.payload[8] : 0;
  }
  bool active() const { return active_; }
 private:
  IClock& clock_;
  TxController& controller_;
  ota::TxFullSpeed fan_;
  const std::uint32_t network_;
  SerialFrame request_{}, reply_{};
  std::uint32_t started_{0}, acknowledgedMs_{0};
  bool active_{false}, haveReply_{false}, acknowledged_{false};
};
}  // namespace fanlink
