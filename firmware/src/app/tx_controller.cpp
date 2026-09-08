#include "fanlink/tx_controller.hpp"

#include <algorithm>
#include <limits>

#include "fanlink/byte_utils.hpp"

namespace fanlink {
namespace {

const NodeTelemetry kInvalidNodeTelemetry{};
const TxNodeDiagnostics kInvalidNodeDiagnostics{};

void addSaturating(std::uint32_t& value) {
  if (value != std::numeric_limits<std::uint32_t>::max()) {
    ++value;
  }
}

std::uint32_t evidenceOffset(bool present, std::uint32_t timestamp,
                             std::uint32_t windowStart, std::uint32_t completed) {
  const auto completedOffset = static_cast<std::uint32_t>(completed - windowStart);
  const auto offset = static_cast<std::uint32_t>(timestamp - windowStart);
  // Half-range bound makes ordering unambiguous; never turn a future or
  // previous-slot timestamp into apparent fresh evidence across micros wrap.
  return present && completedOffset < 0x80000000U && offset <= completedOffset
             ? offset : UINT32_MAX;
}

}  // namespace

TxController::TxController(IRadioPort& radio, IClock& clock, TxControllerConfig config)
    : radio_(radio), clock_(clock), config_(config), cadence_(config.slotPeriodUs) {
  requestedDuty_.fill(255U);
}

bool TxController::begin() {
  requestedDuty_.fill(255U);
  nextPollId_ = 1U;
  sequence_ = 0U;
  cadence_.reset();
  hostSeen_ = false;
  beginCalled_ = true;
  radioDiagnostics_ = {};
  nodeDiagnostics_ = {};
  radioRetryDelayMs_ = std::max(1U, config_.radioRetryDelayMs);
  return beginRadio(false);
}

bool TxController::beginRadio(bool recovery) {
  if (recovery) {
    addSaturating(radioDiagnostics_.recoveryAttempts);
  }
  initialized_ = radio_.begin(config_.networkId);
  lastRadioAttemptMs_ = clock_.millis();
  radioDiagnostics_.beginOk = initialized_;
  radioDiagnostics_.beginDriverError = radio_.lastError();
  if (initialized_) {
    consecutiveTransmitErrors_ = 0U;
    consecutiveReceiveErrors_ = 0U;
    lastReplyProgressMs_ = lastRadioAttemptMs_;
    radioRetryDelayMs_ = std::max(1U, config_.radioRetryDelayMs);
    if (recovery) {
      addSaturating(radioDiagnostics_.recoverySuccesses);
      addSaturating(radioDiagnostics_.generation);
    }
  } else {
    if (recovery) {
      addSaturating(radioDiagnostics_.recoveryFailures);
      const auto maximumDelay = std::max(radioRetryDelayMs_, config_.radioRetryMaxDelayMs);
      radioRetryDelayMs_ = radioRetryDelayMs_ > maximumDelay / 2U
                               ? maximumDelay
                               : std::min(maximumDelay, radioRetryDelayMs_ * 2U);
    }
    recordRadioFault(TxRadioFault::Begin, radioDiagnostics_.beginDriverError, 0U, 0U);
  }
  return initialized_;
}

bool TxController::serviceRadioRecovery() {
  const auto nowMs = clock_.millis();
  const auto errorThreshold = std::max<std::uint8_t>(1U, config_.radioErrorThreshold);
  if (initialized_ &&
      (consecutiveTransmitErrors_ >= errorThreshold ||
       consecutiveReceiveErrors_ >= errorThreshold ||
       static_cast<std::uint32_t>(nowMs - lastReplyProgressMs_) >=
           config_.radioLinkRecoveryMs)) {
    initialized_ = false;
  }
  if (!initialized_ &&
      static_cast<std::uint32_t>(nowMs - lastRadioAttemptMs_) >= radioRetryDelayMs_) {
    (void)beginRadio(true);
  }
  return initialized_;
}

void TxController::markHostAlive() {
  lastHostMessageMs_ = clock_.millis();
  hostSeen_ = true;
}

void TxController::setAllDuty(const std::array<std::uint8_t, kNodeCount>& duty) {
  requestedDuty_ = duty;
  markHostAlive();
}

bool TxController::setNodeDuty(std::uint8_t nodeId, std::uint8_t duty) {
  if (!isValidNodeId(nodeId)) {
    return false;
  }
  requestedDuty_[nodeId - 1U] = duty;
  markHostAlive();
  return true;
}

void TxController::noteHostHeartbeat() { markHostAlive(); }

bool TxController::hostIsFresh(std::uint32_t nowMs) const {
  return hostSeen_ &&
         static_cast<std::uint32_t>(nowMs - lastHostMessageMs_) < config_.hostTimeoutMs;
}

bool TxController::hostFailsafeActive() const { return !hostIsFresh(clock_.millis()); }

std::uint8_t TxController::effectiveDuty(std::uint8_t nodeId) const {
  if (!isValidNodeId(nodeId) || hostFailsafeActive()) {
    return 255U;
  }
  return requestedDuty_[nodeId - 1U];
}

const NodeTelemetry& TxController::nodeTelemetry(std::uint8_t nodeId) const {
  return isValidNodeId(nodeId) ? nodes_[nodeId - 1U] : kInvalidNodeTelemetry;
}

const TxNodeDiagnostics& TxController::nodeDiagnostics(std::uint8_t nodeId) const {
  return isValidNodeId(nodeId) ? nodeDiagnostics_[nodeId - 1U] : kInvalidNodeDiagnostics;
}

void TxController::recordMiss(std::uint8_t nodeId) {
  if (!isValidNodeId(nodeId)) {
    return;
  }
  auto& node = nodes_[nodeId - 1U];
  node.missedPolls = bytes::addSaturatingU8(node.missedPolls, 1U);
}

void TxController::recordRadioFault(TxRadioFault fault, int driverError,
                                    std::uint8_t nodeId, std::uint16_t sequence) {
  radioDiagnostics_.lastFault = fault;
  radioDiagnostics_.lastDriverError = driverError;
  radioDiagnostics_.lastNodeId = nodeId;
  radioDiagnostics_.lastSequence = sequence;
  addSaturating(radioDiagnostics_.generation);
}

void TxController::recordReceiveError(const RadioRxResult& result, std::uint8_t nodeId,
                                      std::uint16_t sequence,
                                      std::uint32_t receiveStartUs) {
  auto& diagnostics = nodeDiagnostics_[nodeId - 1U].receiveErrors;
  const auto& evidence = result.evidence;
  addSaturating(diagnostics.events);
  auto& event = diagnostics.last;
  event = {};
  event.kind = result.result == RadioResult::CrcError ? 1U : 2U;
  event.sequence = sequence;
  if (evidence.irqValid) {
    event.validity |= tx_rx_evidence_valid::kIrq;
    event.irq = evidence.irq;
    event.errorFlags |= evidence.errorFlags &
        (radio_rx_error::kSyncWithoutRxDone | radio_rx_error::kIrqCrc |
         radio_rx_error::kHeader | radio_rx_error::kIrqSync);
  }
  if (evidence.packetStatusValid) {
    event.validity |= tx_rx_evidence_valid::kPacketStatus;
    event.packetStatusError = evidence.packetStatusError;
    event.packetStatusSync = evidence.packetStatusSync;
    event.errorRssiDbm = evidence.errorRssiDbm;
    event.errorFlags |= evidence.errorFlags & radio_rx_error::kPacketCrc;
  }
  if (event.errorFlags & radio_rx_error::kSyncWithoutRxDone)
    addSaturating(diagnostics.syncWithoutRxDone);
  if (event.errorFlags & radio_rx_error::kIrqCrc) addSaturating(diagnostics.irqCrc);
  if (event.errorFlags & radio_rx_error::kPacketCrc) addSaturating(diagnostics.packetCrc);
  if (event.errorFlags & radio_rx_error::kHeader) addSaturating(diagnostics.header);
  if (event.errorFlags & radio_rx_error::kIrqSync) addSaturating(diagnostics.irqSync);
  if (event.errorFlags == 0U) addSaturating(diagnostics.unknown);

  const auto completed = clock_.micros();
  event.observedOffsetUs = evidenceOffset(evidence.eventObserved,
      evidence.eventObservedUs, receiveStartUs, completed);
  event.receiveStartedOffsetUs = evidenceOffset(evidence.receiveStarted,
      evidence.receiveStartedUs, receiveStartUs, completed);
  // A receive-start after this same event observation is internally
  // inconsistent. Keep both raw sources invalid instead of inventing order.
  if (event.observedOffsetUs != UINT32_MAX &&
      event.receiveStartedOffsetUs != UINT32_MAX &&
      event.receiveStartedOffsetUs > event.observedOffsetUs) {
    event.observedOffsetUs = event.receiveStartedOffsetUs = UINT32_MAX;
  }
  if (event.observedOffsetUs != UINT32_MAX)
    event.validity |= tx_rx_evidence_valid::kObserved;
  if (event.receiveStartedOffsetUs != UINT32_MAX)
    event.validity |= tx_rx_evidence_valid::kReceiveStarted;
}

bool TxController::acceptReply(const std::uint8_t* data, std::size_t length,
                               std::uint8_t expectedNode, std::uint16_t expectedSequence,
                               std::int8_t uplinkRssiDbm, std::uint32_t processingAgeUs) {
  RpmPacket packet{};
  auto& reason = nodeDiagnostics_[expectedNode - 1U].lastRejectReason;
  if (!decodeRpmPacket(data, length, packet)) {
    reason = ReplyRejectReason::Envelope;
    return false;
  }
  if (packet.networkId != config_.networkId) {
    reason = ReplyRejectReason::Network;
    return false;
  }
  if (packet.nodeId != expectedNode) {
    reason = ReplyRejectReason::Node;
    return false;
  }
  if (packet.sequenceEcho != expectedSequence) {
    reason = ReplyRejectReason::Sequence;
    return false;
  }

  auto& node = nodes_[packet.nodeId - 1U];
  node.rpm = packet.rpm;
  node.status = static_cast<std::uint8_t>(packet.status & ~node_status::kTelemetryStale);
  node.appliedDuty = packet.appliedDuty;
  node.missedPolls = 0U;
  node.uplinkRssiDbm = uplinkRssiDbm;
  node.downlinkRssiDbm = packet.downlinkRssiDbm;
  // Do not make telemetry younger merely because processing was delayed.
  node.lastSeenMs = clock_.millis() - processingAgeUs / 1000U -
                    (processingAgeUs % 1000U != 0U ? 1U : 0U);
  node.seen = true;
  lastReplyProgressMs_ = node.lastSeenMs;
  return true;
}

void TxController::runOneSlot() {
  if (!beginCalled_) return;
  // Explicit legacy/blocking calls are also an actual slot start for callers
  // that subsequently switch back to the cooperative maintenance/normal path.
  cadence_.reset();
  (void)cadence_.take(clock_.micros());
  runSlot(true);
}

bool TxController::runScheduledSlot() {
  if (!beginCalled_ || !cadence_.take(clock_.micros())) return false;
  runSlot(false);
  return true;
}

void TxController::runSlot(bool waitForPeriod) {
  if (!beginCalled_) {
    return;
  }

  const auto slotStartUs = clock_.micros();
  if (!serviceRadioRecovery()) {
    if (waitForPeriod) finishSlot(slotStartUs);
    return;
  }
  const auto nowMs = clock_.millis();
  const auto polledNode = nextPollId_;
  auto& nodeDiagnostics = nodeDiagnostics_[polledNode - 1U];
  const auto slotSequence = sequence_;
  const bool hostFresh = hostIsFresh(nowMs);

  ControlPacket packet{};
  packet.networkId = config_.networkId;
  packet.sequence = slotSequence;
  packet.pollId = polledNode;
  packet.duty.fill(255U);
  std::copy(requestedDuty_.begin(), requestedDuty_.end(), packet.duty.begin());
  if (!hostFresh) {
    packet.duty.fill(255U);
    packet.flags = control_flag::kForceFullSpeed | control_flag::kHostFailsafe;
  }

  std::array<std::uint8_t, kControlPacketSize> encoded{};
  const bool built = encodeControlPacket(packet, encoded);
  bool replyAccepted = false;

  if (!built) {
    recordRadioFault(TxRadioFault::Encode, 0, polledNode, slotSequence);
  } else if (radio_.transmit(encoded.data(), encoded.size()) != RadioResult::Ok) {
    const auto driverError = radio_.lastError();
    addSaturating(radioDiagnostics_.transmitFailures);
    addSaturating(nodeDiagnostics.transmitFailures);
    radioDiagnostics_.lastTransmitDriverError = driverError;
    recordRadioFault(TxRadioFault::Transmit, driverError, polledNode, slotSequence);
    consecutiveTransmitErrors_ = bytes::addSaturatingU8(consecutiveTransmitErrors_, 1U);
  } else {
    addSaturating(nodeDiagnostics.transmittedPolls);
    consecutiveTransmitErrors_ = 0U;
    std::array<std::uint8_t, kRpmPacketSize> reply{};
    const auto receiveStartUs = clock_.micros();
    nodeDiagnostics.lastReceiveStartedOffsetUs = UINT32_MAX;
    const auto receiveBudgetUs = static_cast<std::uint64_t>(config_.replyTimeoutMs) * 1000U;
    bool receiveIoError = false;
    bool receiveTimedOut = false;
    bool rejectedReply = false;
    int timeoutDriverError = 0;
    // The deadline is primary; also bound calls if a faulty port repeatedly
    // returns immediately while the clock has not advanced.
    for (unsigned attempts = 0U;
         attempts < 32U && !replyAccepted && !receiveIoError && !receiveTimedOut;
         ++attempts) {
      const auto elapsedUs = static_cast<std::uint32_t>(clock_.micros() - receiveStartUs);
      if (elapsedUs >= receiveBudgetUs) {
        break;
      }
      const auto remainingUs = static_cast<std::uint32_t>(receiveBudgetUs - elapsedUs);
      const auto result = radio_.receiveForUs(reply.data(), reply.size(), remainingUs);
      if (attempts == 0U) {
        nodeDiagnostics.lastReceiveStartedOffsetUs = evidenceOffset(
            result.evidence.receiveStarted, result.evidence.receiveStartedUs,
            receiveStartUs, clock_.micros());
        const auto observed = evidenceOffset(result.evidence.eventObserved,
            result.evidence.eventObservedUs, receiveStartUs, clock_.micros());
        if (observed != UINT32_MAX &&
            nodeDiagnostics.lastReceiveStartedOffsetUs > observed) {
          nodeDiagnostics.lastReceiveStartedOffsetUs = UINT32_MAX;
        }
      }
      switch (result.result) {
        case RadioResult::Ok: {
          const auto completedUs = clock_.micros();
          const auto completedOffset = static_cast<std::uint32_t>(completedUs - receiveStartUs);
          const auto observedOffset = result.receiveObserved
              ? static_cast<std::uint32_t>(result.receiveObservedUs - receiveStartUs)
              : completedOffset;
          nodeDiagnostics.lastObservedOffsetUs = result.receiveObserved ? observedOffset : UINT32_MAX;
          nodeDiagnostics.lastCompletedOffsetUs = completedOffset;
          if (result.receiveObserved) {
            addSaturating(nodeDiagnostics.observedReplies);
          }
          // Subtraction rejects observations from earlier slots across wrap,
          // and observedOffset > completedOffset rejects future timestamps.
          if (observedOffset > completedOffset || observedOffset >= receiveBudgetUs) {
            addSaturating(nodeDiagnostics.lateReplies);
            nodeDiagnostics.lastRejectReason = observedOffset > completedOffset
                ? ReplyRejectReason::InvalidTimestamp
                : (result.receiveObserved ? ReplyRejectReason::LateObserved
                                          : ReplyRejectReason::LegacyLateReturn);
            break;
          }
          replyAccepted = acceptReply(reply.data(), result.length, polledNode, slotSequence,
                                      result.rssiDbm, completedOffset - observedOffset);
          if (replyAccepted) {
            if (completedOffset >= receiveBudgetUs) {
              addSaturating(nodeDiagnostics.processedAfterDeadline);
            }
            addSaturating(radioDiagnostics_.acceptedReplies);
            addSaturating(nodeDiagnostics.acceptedReplies);
            // A later success must not erase the last actionable radio fault.
            addSaturating(radioDiagnostics_.generation);
          } else {
            rejectedReply = true;
            addSaturating(radioDiagnostics_.invalidReplies);
            addSaturating(nodeDiagnostics.invalidReplies);
            recordRadioFault(TxRadioFault::InvalidReply, result.driverError, polledNode,
                             slotSequence);
          }
          break;
        }
        case RadioResult::Timeout:
          receiveTimedOut = true;
          timeoutDriverError = result.driverError;
          radioDiagnostics_.lastReceiveDriverError = result.driverError;
          break;
        case RadioResult::CrcError:
          recordReceiveError(result, polledNode, slotSequence, receiveStartUs);
          rejectedReply = true;
          addSaturating(radioDiagnostics_.receiveCrcErrors);
          addSaturating(nodeDiagnostics.receiveCrcErrors);
          radioDiagnostics_.lastReceiveDriverError = result.driverError;
          recordRadioFault(TxRadioFault::ReceiveCrc, result.driverError, polledNode,
                           slotSequence);
          break;
        case RadioResult::IoError:
          recordReceiveError(result, polledNode, slotSequence, receiveStartUs);
          receiveIoError = true;
          addSaturating(radioDiagnostics_.receiveIoErrors);
          addSaturating(nodeDiagnostics.receiveIoErrors);
          radioDiagnostics_.lastReceiveDriverError = result.driverError;
          recordRadioFault(TxRadioFault::ReceiveIo, result.driverError, polledNode,
                           slotSequence);
          break;
      }
    }
    consecutiveReceiveErrors_ = receiveIoError
                                    ? bytes::addSaturatingU8(consecutiveReceiveErrors_, 1U)
                                    : 0U;
    if (!replyAccepted && !receiveIoError) {
      addSaturating(radioDiagnostics_.receiveTimeouts);
      addSaturating(nodeDiagnostics.receiveTimeouts);
      if (!rejectedReply) {
        recordRadioFault(TxRadioFault::ReceiveTimeout,
                         timeoutDriverError, polledNode, slotSequence);
      }
    }
  }

  if (!replyAccepted) {
    recordMiss(polledNode);
  }

  sequence_ = static_cast<std::uint16_t>(sequence_ + 1U);
  nextPollId_ = nextPollId_ >= kNodeCount ? 1U : static_cast<std::uint8_t>(nextPollId_ + 1U);

  if (waitForPeriod) finishSlot(slotStartUs);
}

void TxController::finishSlot(std::uint32_t slotStartUs) {
  const auto elapsedUs = static_cast<std::uint32_t>(clock_.micros() - slotStartUs);
  if (elapsedUs < config_.slotPeriodUs) {
    clock_.delayMicroseconds(config_.slotPeriodUs - elapsedUs);
  }
}

}  // namespace fanlink
