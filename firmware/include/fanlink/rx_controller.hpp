#pragma once

#include <cstdint>

#include "fanlink/interfaces.hpp"
#include "fanlink/protocol.hpp"

namespace fanlink {

struct RxControllerConfig {
  std::uint32_t networkId{0};
  std::uint8_t nodeId{1};
  std::uint32_t receiveTimeoutMs{25};
  std::uint32_t linkTimeoutMs{500};
  std::uint32_t replyDelayUs{1200};
  std::uint8_t stallDutyThreshold{64};
  std::uint32_t stallGraceMs{1500};
};

class RxController {
 public:
  RxController(IRadioPort& radio, IClock& clock, IFanOutput& fan, ITachometer& tach,
               RxControllerConfig config);

  bool begin();
  void runOnce();

  bool linkFailsafeActive() const { return !haveLink_ || linkFailsafe_; }
  std::uint8_t appliedDuty() const { return fan_.appliedDuty(); }
  std::uint16_t lastRpm() const { return lastRpm_; }
  std::uint8_t pendingStatus() const { return pendingStatus_; }
  std::uint8_t missedDownlinksSinceReply() const { return missedDownlinksSinceReply_; }

 private:
  void updateLinkFailsafe(std::uint32_t nowMs);
  void processControl(const ControlPacket& packet, std::int8_t downlinkRssiDbm,
                      std::uint32_t nowMs);
  void updateTachAndStall(std::uint32_t nowMs);
  void sendReply(const ControlPacket& packet, std::int8_t downlinkRssiDbm);
  bool noteSequence(const ControlPacket& packet);

  IRadioPort& radio_;
  IClock& clock_;
  IFanOutput& fan_;
  ITachometer& tach_;
  RxControllerConfig config_;
  std::uint32_t lastValidPacketMs_{0};
  std::uint32_t spinGraceStartedMs_{0};
  std::uint16_t lastSequence_{0};
  ControlPacket lastControlPacket_{};
  std::uint16_t lastRpm_{0};
  std::uint8_t pendingStatus_{node_status::kLinkFailsafe | node_status::kTachTimeout};
  std::uint8_t missedDownlinksSinceReply_{0};
  bool haveLink_{false};
  bool haveSequence_{false};
  bool linkFailsafe_{true};
  bool spinGraceActive_{false};
  bool initialized_{false};
};

}  // namespace fanlink
