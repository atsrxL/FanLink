#pragma once
#include "fanlink/settings.hpp"
#include "fanlink/ota_tx_local.hpp"

namespace fanlink::settings {
// One maintenance owner for local settings and remote RF settings. ACKs are
// retained until UART has room. A missing RF ACK is a timeout, never success.
class TxSettings {
 public:
  TxSettings(IRadioPort& radio, IClock& clock, TxController& controller,
             SettingsService& local, std::uint32_t network)
      : radio_(radio), clock_(clock), controller_(controller), fan_(controller),
        local_(local), network_(network) {}
  bool enqueue(const std::uint8_t* data, std::size_t length,
               std::uint16_t sequence, bool busy) {
    Request r{};
    if (!decodeRequest(data, length, r) || r.networkId != network_ || haveReply_) return false;
    if (active_) {
      if (sameRequest(r, request_)) {
        sequence_ = sequence;
        if (cached_) haveReply_ = true;
        return true; // Duplicate never resets the prelude or reboot grace.
      }
      return false; // Busy remote requests must not invent the peer's profile.
    }
    if (busy) {
      if (r.nodeId != 0) return false;
      Reply reply{}; local_.process(r, reply, clock_.millis(), true);
      encodeReply(reply, reply_); sequence_ = sequence; haveReply_ = true;
      return true;
    }
    request_ = r; sequence_ = sequence; cached_ = false;
    startedMs_ = clock_.millis(); active_ = true; waiting_ = false;
    mutation_ = r.operation != Operation::Query;
    if (mutation_) fan_.setFullSpeed();
    return true;
  }
  bool service() {
    if (!active_) return false;
    if (cached_) {
      if (!haveReply_ && !local_.pending() &&
          (!remotePending_ || std::uint32_t(clock_.millis() - repliedMs_) >= kApplyGraceMs))
        active_ = remotePending_ = false;
      else if (mutation_) controller_.runScheduledSlot();
      return true;
    }
    if (mutation_ && std::uint32_t(clock_.millis() - startedMs_) < 600) {
      controller_.runScheduledSlot(); return true;
    }
    if (request_.nodeId == 0) {
      Reply reply{};
      if (local_.process(request_, reply, clock_.millis())) finish(reply);
      else active_ = false;
      return true;
    }
    if (!waiting_) {
      std::array<std::uint8_t, kPacketSize> data{}; encodeRequest(request_, data);
      if (radio_.transmit(data.data(), data.size()) != RadioResult::Ok) {
        active_ = false; return true;
      }
      waiting_ = true; sentUs_ = clock_.micros();
    }
    const auto elapsedUs = std::uint32_t(clock_.micros() - sentUs_);
    if (elapsedUs >= 300000) {
      active_ = waiting_ = false; return true;
    }
    std::array<std::uint8_t, kPacketSize> data{};
    const auto rx = radio_.receiveForUs(data.data(), data.size(),
        std::min<std::uint32_t>(1000, 300000 - elapsedUs));
    Reply reply{};
    if (std::uint32_t(clock_.micros() - sentUs_) < 300000 &&
        rx.result == RadioResult::Ok && decodeReply(data.data(), rx.length, reply) &&
        sameRequest(request_, reply.request)) finish(reply);
    return true;
  }
  bool replyReady(std::array<std::uint8_t, kPacketSize>& data, std::uint16_t& sequence) const {
    if (!haveReply_) return false;
    data = reply_; sequence = sequence_; return true;
  }
  void replyWritten(bool success) {
    if (success && haveReply_) {
      if (request_.nodeId == 0 && mutation_ && cached_ && local_.pending()) localAckWritten_ = true;
      haveReply_ = false;
    }
  }
  bool localRebootReady() const { return localAckWritten_ && local_.rebootDue(clock_.millis()); }
  bool active() const { return active_ || local_.pending(); }
 private:
  void finish(const Reply& reply) {
    encodeReply(reply, reply_); cached_ = haveReply_ = true; waiting_ = false;
    remotePending_ = request_.nodeId != 0 && reply.status == Status::Ok && (reply.flags & 1);
    repliedMs_ = clock_.millis();
  }
  IRadioPort& radio_; IClock& clock_; TxController& controller_; ota::TxFullSpeed fan_;
  SettingsService& local_; const std::uint32_t network_;
  Request request_{};
  std::array<std::uint8_t, kPacketSize> reply_{};
  std::uint16_t sequence_{0};
  std::uint32_t startedMs_{0}, sentUs_{0}, repliedMs_{0};
  bool active_{false}, mutation_{false}, waiting_{false}, cached_{false};
  bool haveReply_{false}, remotePending_{false};
  bool localAckWritten_{false};
};
} // namespace fanlink::settings
