#pragma once
#include <algorithm>
#include "fanlink/ota_receiver.hpp"
#include "fanlink/tx_controller.hpp"

namespace fanlink::ota {
// Streaming guard: staging may contain any bytes, but only exactly one matching
// descriptor can reach verify/commit. Not cryptographic authentication.
class IdentityStorage final : public IUpdateStorage {
 public:
  IdentityStorage(IUpdateStorage& storage, const std::array<std::uint8_t, 28>& identity)
      : storage_(storage), identity_(identity) {}
  bool begin(std::uint32_t size, const std::array<std::uint8_t, 16>& md5) override {
    window_ = {}; filled_ = matches_ = 0; bad_ = complete_ = false;
    return storage_.begin(size, md5);
  }
  bool write(const std::uint8_t* data, std::size_t length) override {
    if (!storage_.write(data, length)) return false;
    for (std::size_t i = 0; i < length; ++i) {
      std::move(window_.begin() + 1, window_.end(), window_.begin());
      window_.back() = data[i];
      if (filled_ < window_.size()) ++filled_;
      if (filled_ >= 16 && std::equal(window_.end() - 16, window_.end(), identity_.begin())) {
        if (matches_ < 2) ++matches_;
      }
      if (filled_ == window_.size() &&
          std::equal(window_.begin(), window_.begin() + 16, identity_.begin())) {
        complete_ = true;
        if (window_ != identity_) bad_ = true;
      }
    }
    return true;
  }
  bool verify() override { return matches_ == 1 && complete_ && !bad_ && storage_.verify(); }
  bool commit() override { return matches_ == 1 && complete_ && !bad_ && storage_.commit(); }
  void abort() override { storage_.abort(); }
 private:
  IUpdateStorage& storage_;
  std::array<std::uint8_t, 28> identity_, window_{};
  std::size_t filled_{0};
  unsigned matches_{0};
  bool bad_{false}, complete_{false};
};

class TxFullSpeed final : public IFanOutput {
 public:
  explicit TxFullSpeed(TxController& controller) : controller_(controller) {}
  bool begin() override { setFullSpeed(); return true; }
  void setDuty(std::uint8_t) override { setFullSpeed(); }
  void setFullSpeed() override {
    std::array<std::uint8_t, kNodeCount> full{}; full.fill(255);
    controller_.setAllDuty(full);
  }
  std::uint8_t appliedDuty() const override { return 255; }
 private:
  TxController& controller_;
};

class TxLocalUpdate {
 public:
  TxLocalUpdate(IUpdateStorage& storage, IClock& clock, TxController& controller,
                std::uint32_t network, std::uint8_t node, std::uint32_t compatibility)
      : clock_(clock), controller_(controller), fan_(controller),
        receiver_(storage, fan_, network, node, compatibility),
        network_(network), compatibility_(compatibility), node_(node) {}
  bool enqueue(const std::uint8_t* data, std::size_t length,
               std::uint16_t sequence, bool block);
  bool service();
  bool takeReply(std::array<std::uint8_t, 30>& data, std::uint16_t& sequence);
  bool active() const { return active_; }
  bool rebootDue() const { return receiver_.rebootDue(clock_.millis()); }
 private:
  IClock& clock_;
  TxController& controller_;
  TxFullSpeed fan_;
  Receiver receiver_;
  const std::uint32_t network_, compatibility_;
  const std::uint8_t node_;
  std::uint32_t started_{0}, imageSize_{0}, session_{0};
  std::uint16_t sequence_{0};
  Packet request_{};
  std::array<std::uint8_t, kBlockSize> block_{};
  std::size_t blockCount_{0};
  std::array<std::uint8_t, 30> reply_{};
  bool active_{false}, pending_{false}, haveReply_{false};
};
}  // namespace fanlink::ota
