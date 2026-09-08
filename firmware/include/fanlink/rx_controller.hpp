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
  std::uint32_t radioRetryDelayMs{1000};
  std::uint32_t radioRetryMaxDelayMs{8000};
  std::uint32_t radioLinkRecoveryMs{2000};
  std::uint8_t radioErrorThreshold{3};
  // Do not start an already obsolete reply. This is a software observation
  // age limit, not an airtime guarantee or a change to the TX receive window.
  std::uint32_t replyMaxAgeUs{5000};
};

struct RxRadioDiagnostics {
  std::uint32_t recoveryAttempts{0};
  std::uint32_t recoverySuccesses{0};
  std::uint32_t recoveryFailures{0};
  std::uint32_t receiveIoErrors{0};
  std::uint32_t transmitErrors{0};
  int lastDriverError{0};
  std::uint32_t ownPolls{0};
  std::uint32_t malformedControls{0};
  std::uint32_t wrongNetworkControls{0};
  std::uint32_t staleControls{0};
  std::uint32_t expiredControls{0};
  std::uint32_t replyAttempts{0};
  std::uint32_t replySuccesses{0};
  std::uint32_t replyFailures{0};
  std::uint32_t lateReplyCalls{0};
  std::uint32_t expiredRepliesSkipped{0};
  // Measured from first software DIO observation to transmit() invocation;
  // NOT physical RX_DONE edge to RF TX start. No UART or wire fields added.
  std::uint32_t lastReplyCallAgeUs{0};
  std::uint32_t maxReplyCallAgeUs{0};
};

class RxController {
 public:
  RxController(IRadioPort& radio, IClock& clock, IFanOutput& fan, ITachometer& tach,
               RxControllerConfig config);

  void setMaintenance(IRxMaintenance* maintenance) { maintenance_ = maintenance; }
  bool begin();
  void runOnce();

  bool linkFailsafeActive() const { return !haveLink_ || linkFailsafe_; }
  std::uint8_t appliedDuty() const { return fan_.appliedDuty(); }
  std::uint16_t lastRpm() const { return lastRpm_; }
  std::uint8_t pendingStatus() const { return pendingStatus_; }
  std::uint8_t missedDownlinksSinceReply() const { return missedDownlinksSinceReply_; }
  const RxRadioDiagnostics& radioDiagnostics() const { return radioDiagnostics_; }

 private:
  enum class SequenceResult : std::uint8_t { Rejected, Retry, Fresh };
  void updateLinkFailsafe(std::uint32_t nowMs);
  void enterLinkFailsafe();
  void processControl(const ControlPacket& packet, std::int8_t downlinkRssiDbm,
                      std::uint32_t nowMs, const RadioRxResult& received);
  void updateTachAndStall(std::uint32_t nowMs);
  void sendReply(const ControlPacket& packet, std::int8_t downlinkRssiDbm,
                 const RadioRxResult& received);
  SequenceResult noteSequence(const ControlPacket& packet, std::uint32_t nowMs);
  bool beginRadio(bool recovery);
  bool serviceRadioRecovery();

  IRxMaintenance* maintenance_{nullptr};
  IRadioPort& radio_;
  IClock& clock_;
  IFanOutput& fan_;
  ITachometer& tach_;
  RxControllerConfig config_;
  std::uint32_t lastValidPacketMs_{0};
  std::uint32_t spinGraceStartedMs_{0};
  std::uint32_t lastRadioAttemptMs_{0};
  std::uint32_t lastControlProgressMs_{0};
  std::uint32_t radioRetryDelayMs_{1000};
  std::uint32_t sequenceCandidateMs_{0};
  std::uint16_t lastSequence_{0};
  std::uint16_t sequenceCandidate_{0};
  ControlPacket lastControlPacket_{};
  std::uint16_t lastRpm_{0};
  std::uint8_t pendingStatus_{node_status::kLinkFailsafe | node_status::kTachTimeout};
  std::uint8_t missedDownlinksSinceReply_{0};
  RxRadioDiagnostics radioDiagnostics_{};
  std::uint8_t consecutiveTransmitErrors_{0};
  std::uint8_t consecutiveReceiveErrors_{0};
  bool haveLink_{false};
  bool haveSequence_{false};
  bool haveSequenceCandidate_{false};
  bool linkFailsafe_{true};
  bool spinGraceActive_{false};
  bool initialized_{false};
  bool radioReady_{false};
};

}  // namespace fanlink
