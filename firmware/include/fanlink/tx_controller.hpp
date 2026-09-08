#pragma once
#include "fanlink/poll_cadence.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#include "fanlink/interfaces.hpp"
#include "fanlink/protocol.hpp"

namespace fanlink {

struct TxControllerConfig {
  std::uint32_t networkId{0};
  std::uint32_t hostTimeoutMs{500};
  std::uint32_t slotPeriodUs{kNormalSlotPeriodUs};
  std::uint32_t replyTimeoutMs{5};
  std::uint32_t radioRetryDelayMs{1000};
  std::uint32_t radioRetryMaxDelayMs{8000};
  std::uint32_t radioLinkRecoveryMs{5000};
  std::uint8_t radioErrorThreshold{3};
};

struct NodeTelemetry {
  std::uint16_t rpm{0};
  std::uint8_t status{node_status::kTelemetryStale};
  std::uint8_t appliedDuty{255};
  std::uint8_t missedPolls{0};
  std::int8_t uplinkRssiDbm{-127};
  std::int8_t downlinkRssiDbm{-127};
  std::uint32_t lastSeenMs{0};
  bool seen{false};
};

enum class ReplyRejectReason : std::uint8_t {
  None, Envelope, Network, Node, Sequence, LateObserved, InvalidTimestamp, LegacyLateReturn,
};

namespace tx_rx_evidence_valid {
inline constexpr std::uint8_t kIrq = 1U << 0U;
inline constexpr std::uint8_t kPacketStatus = 1U << 1U;
inline constexpr std::uint8_t kObserved = 1U << 2U;
inline constexpr std::uint8_t kReceiveStarted = 1U << 3U;
}  // namespace tx_rx_evidence_valid

struct TxReceiveErrorEvent {
  // 0: no event, 1: CrcError, 2: IoError. Neither authorizes telemetry.
  std::uint8_t kind{0};
  std::uint8_t validity{0};
  std::uint8_t errorFlags{0};
  std::uint8_t packetStatusError{0};
  std::uint8_t packetStatusSync{0};
  std::int8_t errorRssiDbm{-127};
  std::uint16_t sequence{0};
  std::uint16_t irq{0};
  std::uint32_t observedOffsetUs{UINT32_MAX};
  std::uint32_t receiveStartedOffsetUs{UINT32_MAX};
};

struct TxReceiveErrorDiagnostics {
  // One event may increment several causes. These belong to the poll window,
  // never to an authenticated sender. Legacy/missing causes count as unknown.
  std::uint32_t events{0};
  std::uint32_t syncWithoutRxDone{0};
  std::uint32_t irqCrc{0};
  std::uint32_t packetCrc{0};
  std::uint32_t header{0};
  std::uint32_t irqSync{0};
  std::uint32_t unknown{0};
  TxReceiveErrorEvent last{};
};

struct TxNodeDiagnostics {
  // A completed transmit is not proof that this node received the Control.
  std::uint32_t transmittedPolls{0};
  std::uint32_t acceptedReplies{0};
  std::uint32_t transmitFailures{0};
  std::uint32_t receiveTimeouts{0};
  // Events in this node's poll window, not authenticated packet origins.
  std::uint32_t receiveCrcErrors{0};
  std::uint32_t receiveIoErrors{0};
  std::uint32_t invalidReplies{0};
  std::uint32_t observedReplies{0};
  std::uint32_t lateReplies{0};
  std::uint32_t processedAfterDeadline{0};
  std::uint32_t lastObservedOffsetUs{UINT32_MAX};
  std::uint32_t lastCompletedOffsetUs{0};
  ReplyRejectReason lastRejectReason{ReplyRejectReason::None};
  // First receive call in the latest successfully transmitted poll, even if
  // it times out. Rearm/retry observations must not replace initial RX timing.
  std::uint32_t lastReceiveStartedOffsetUs{UINT32_MAX};
  TxReceiveErrorDiagnostics receiveErrors{};
};

enum class TxRadioFault : std::uint8_t {
  None,
  Begin,
  Encode,
  Transmit,
  ReceiveTimeout,
  ReceiveCrc,
  ReceiveIo,
  InvalidReply,
};

struct TxRadioDiagnostics {
  bool beginOk{false};
  int beginDriverError{0};
  TxRadioFault lastFault{TxRadioFault::None};
  int lastDriverError{0};
  int lastTransmitDriverError{0};
  int lastReceiveDriverError{0};
  std::uint8_t lastNodeId{0};
  std::uint16_t lastSequence{0};
  std::uint32_t generation{0};
  std::uint32_t transmitFailures{0};
  std::uint32_t receiveTimeouts{0};
  std::uint32_t receiveCrcErrors{0};
  std::uint32_t receiveIoErrors{0};
  std::uint32_t invalidReplies{0};
  std::uint32_t acceptedReplies{0};
  std::uint32_t recoveryAttempts{0};
  std::uint32_t recoverySuccesses{0};
  std::uint32_t recoveryFailures{0};
};

class TxController {
 public:
  TxController(IRadioPort& radio, IClock& clock, TxControllerConfig config);

  bool begin();
  void setAllDuty(const std::array<std::uint8_t, kNodeCount>& duty);
  bool setNodeDuty(std::uint8_t nodeId, std::uint8_t duty);
  void noteHostHeartbeat();
  void runOneSlot();
  // Cooperative normal/maintenance-preamble entry point. False means not due
  // (or begin not called), true means one bounded slot/recovery attempt ran.
  // Never sleeps out the 125ms period; all callers share one actual-start clock.
  bool runScheduledSlot();

  const std::array<std::uint8_t, kNodeCount>& requestedDuty() const { return requestedDuty_; }
  const NodeTelemetry& nodeTelemetry(std::uint8_t nodeId) const;
  const TxNodeDiagnostics& nodeDiagnostics(std::uint8_t nodeId) const;
  std::uint8_t nextPollId() const { return nextPollId_; }
  std::uint16_t nextSequence() const { return sequence_; }
  bool hostFailsafeActive() const;
  std::uint8_t effectiveDuty(std::uint8_t nodeId) const;
  const TxRadioDiagnostics& radioDiagnostics() const { return radioDiagnostics_; }

 private:
  bool hostIsFresh(std::uint32_t nowMs) const;
  void markHostAlive();
  void recordMiss(std::uint8_t nodeId);
  void recordRadioFault(TxRadioFault fault, int driverError, std::uint8_t nodeId,
                        std::uint16_t sequence);
  void recordReceiveError(const RadioRxResult& result, std::uint8_t nodeId,
                          std::uint16_t sequence, std::uint32_t receiveStartUs);
  bool acceptReply(const std::uint8_t* data, std::size_t length, std::uint8_t expectedNode,
                   std::uint16_t expectedSequence, std::int8_t uplinkRssiDbm,
                   std::uint32_t processingAgeUs);
  bool beginRadio(bool recovery);
  bool serviceRadioRecovery();
  void finishSlot(std::uint32_t slotStartUs);
  void runSlot(bool waitForPeriod);

  IRadioPort& radio_;
  IClock& clock_;
  TxControllerConfig config_;
  CooperativePollCadence cadence_;
  std::array<std::uint8_t, kNodeCount> requestedDuty_{};
  std::array<NodeTelemetry, kNodeCount> nodes_{};
  std::array<TxNodeDiagnostics, kNodeCount> nodeDiagnostics_{};
  std::uint8_t nextPollId_{1};
  std::uint16_t sequence_{0};
  std::uint32_t lastHostMessageMs_{0};
  std::uint32_t lastRadioAttemptMs_{0};
  std::uint32_t lastReplyProgressMs_{0};
  std::uint32_t radioRetryDelayMs_{1000};
  TxRadioDiagnostics radioDiagnostics_{};
  std::uint8_t consecutiveTransmitErrors_{0};
  std::uint8_t consecutiveReceiveErrors_{0};
  bool hostSeen_{false};
  bool beginCalled_{false};
  bool initialized_{false};
};

}  // namespace fanlink
