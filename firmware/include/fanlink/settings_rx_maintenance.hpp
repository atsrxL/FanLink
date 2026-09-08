#pragma once
#include "fanlink/settings.hpp"
#include "fanlink/ota_rx_maintenance.hpp"

namespace fanlink::settings {
// One arbiter for both maintenance protocols: neither can release the other's
// full-speed latch or write Flash concurrently.
class RxMaintenance final : public IRxMaintenance {
 public:
  RxMaintenance(SettingsService& settings, ota::Receiver& receiver, IFanOutput& fan)
      : settings_(settings), receiver_(receiver), ota_(receiver), fan_(fan) {}
  void tick(std::uint32_t now) override {
    ota_.tick(now); if (active()) fan_.setFullSpeed();
  }
  bool active() const override { return settings_.pending() || ota_.active(); }
  bool handle(const std::uint8_t* data, std::size_t length, std::uint8_t* reply,
              std::size_t& replyLength, std::uint32_t now) override {
    replyLength = 0;
    Request request{};
    if (decodeRequest(data, length, request)) {
      // Ask read-only query first to establish exact network/node ownership.
      Reply response{};
      auto probe = request; probe.operation = Operation::Query; probe.value = 0;
      if (!settings_.process(probe, response, now)) return true;
      if (request.operation != Operation::Query) fan_.setFullSpeed();
      if (settings_.process(request, response, now, receiver_.active())) {
        std::array<std::uint8_t, kPacketSize> encoded{};
        if (encodeReply(response, encoded)) {
          std::copy(encoded.begin(), encoded.end(), reply); replyLength = encoded.size();
        }
      }
      return true;
    }
    if (settings_.pending()) {
      // No reply is safer than acknowledging an OTA session we cannot own.
      // The TX side arbitrates requests; an out-of-band sender must retry later.
      ota::Packet packet{};
      return ota::decodeRequest(data, length, packet);
    }
    return ota_.handle(data, length, reply, replyLength, now);
  }
 private:
  SettingsService& settings_;
  ota::Receiver& receiver_;
  ota::RxMaintenance ota_;
  IFanOutput& fan_;
};
}  // namespace fanlink::settings
