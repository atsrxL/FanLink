#pragma once
#include <algorithm>
#include "fanlink/ota_receiver.hpp"

namespace fanlink::ota {
class RxMaintenance final : public IRxMaintenance {
 public:
  explicit RxMaintenance(Receiver& receiver) : receiver_(receiver) {}
  void tick(std::uint32_t now) override { receiver_.tick(now); }
  bool active() const override { return receiver_.active(); }
  bool handle(const std::uint8_t* data, std::size_t length,
              std::uint8_t* reply, std::size_t& replyLength,
              std::uint32_t now) override {
    replyLength = 0;
    Packet request{}, response{};
    if (!decodeRequest(data, length, request)) return false;
    if (receiver_.process(request, response, now)) {
      std::array<std::uint8_t, kPacketSize> encoded{};
      if (encodeReply(response, encoded)) {
        std::copy(encoded.begin(), encoded.end(), reply);
        replyLength = encoded.size();
      }
    }
    return true;
  }
 private:
  Receiver& receiver_;
};
}  // namespace fanlink::ota
