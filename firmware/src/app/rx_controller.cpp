#include "fanlink/rx_controller.hpp"

#include <algorithm>
#include <array>
#include <limits>

#include "fanlink/byte_utils.hpp"

namespace fanlink {
namespace {

void addSaturating(std::uint32_t& value) {
  if (value != std::numeric_limits<std::uint32_t>::max()) {
    ++value;
  }
}

}  // namespace

RxController::RxController(IRadioPort& radio, IClock& clock, IFanOutput& fan, ITachometer& tach,
                           RxControllerConfig config)
    : radio_(radio), clock_(clock), fan_(fan), tach_(tach), config_(config) {}

bool RxController::begin() {
  const bool configValid = isValidNodeId(config_.nodeId) && config_.linkTimeoutMs > 0U &&
      config_.replyMaxAgeUs > 0U && config_.replyMaxAgeUs < 0x80000000U &&
      config_.replyDelayUs < config_.replyMaxAgeUs;
  if (!configValid) {
    pendingStatus_ |= node_status::kConfigError;
  }

  const bool fanReady = fan_.begin();
  fan_.setFullSpeed();
  const bool tachReady = tach_.begin();
  // Peripheral initialization is one-shot. Radio recovery must never call
  // tach_.begin() again: the ESP8266 Tach ISR already owns its singleton.
  initialized_ = configValid && fanReady && tachReady;
  radioRetryDelayMs_ = std::max(1U, config_.radioRetryDelayMs);
  return initialized_ && beginRadio(false);
}

bool RxController::beginRadio(bool recovery) {
  if (recovery) {
    addSaturating(radioDiagnostics_.recoveryAttempts);
  }
  radioReady_ = radio_.begin(config_.networkId);
  lastRadioAttemptMs_ = clock_.millis();
  radioDiagnostics_.lastDriverError = radio_.lastError();
  if (radioReady_) {
    consecutiveTransmitErrors_ = 0U;
    consecutiveReceiveErrors_ = 0U;
    lastControlProgressMs_ = lastRadioAttemptMs_;
    radioRetryDelayMs_ = std::max(1U, config_.radioRetryDelayMs);
    if (recovery) {
      addSaturating(radioDiagnostics_.recoverySuccesses);
    }
  } else {
    pendingStatus_ |= node_status::kRadioError;
    if (recovery) {
      addSaturating(radioDiagnostics_.recoveryFailures);
      const auto maximumDelay = std::max(radioRetryDelayMs_, config_.radioRetryMaxDelayMs);
      radioRetryDelayMs_ = radioRetryDelayMs_ > maximumDelay / 2U
                               ? maximumDelay
                               : std::min(maximumDelay, radioRetryDelayMs_ * 2U);
    }
  }
  return radioReady_;
}

bool RxController::serviceRadioRecovery() {
  const auto nowMs = clock_.millis();
  const auto errorThreshold = std::max<std::uint8_t>(1U, config_.radioErrorThreshold);
  if (radioReady_ &&
      (consecutiveTransmitErrors_ >= errorThreshold ||
       consecutiveReceiveErrors_ >= errorThreshold ||
       ((!maintenance_ || !maintenance_->active()) &&
        static_cast<std::uint32_t>(nowMs - lastControlProgressMs_) >=
           config_.radioLinkRecoveryMs))) {
    // Hold full speed before touching radio reset/SPI, even if begin() blocks
    // or fails. Retain the accepted sequence baseline across radio resets.
    enterLinkFailsafe();
    haveSequenceCandidate_ = false;
    radioReady_ = false;
  }
  if (!radioReady_ &&
      static_cast<std::uint32_t>(nowMs - lastRadioAttemptMs_) >= radioRetryDelayMs_) {
    (void)beginRadio(true);
  }
  return radioReady_;
}

void RxController::enterLinkFailsafe() {
  if (!linkFailsafe_ || fan_.appliedDuty() != 255U) {
    fan_.setFullSpeed();
  }
  if (haveLink_) {
    haveSequenceCandidate_ = false;
  }
  haveLink_ = false;
  linkFailsafe_ = true;
  pendingStatus_ |= node_status::kLinkFailsafe;
}

void RxController::updateLinkFailsafe(std::uint32_t nowMs) {
  if (!haveLink_ || static_cast<std::uint32_t>(nowMs - lastValidPacketMs_) >=
                        config_.linkTimeoutMs) {
    enterLinkFailsafe();
  }
}

RxController::SequenceResult RxController::noteSequence(const ControlPacket& packet,
                                                         std::uint32_t nowMs) {
  const auto sequence = packet.sequence;
  if (!haveSequence_) {
    lastSequence_ = sequence;
    lastControlPacket_ = packet;
    haveSequence_ = true;
    return SequenceResult::Fresh;
  }

  const auto delta = static_cast<std::uint16_t>(sequence - lastSequence_);
  if (delta == 0U) {
    // A duplicate is a retry only when the entire Control payload is identical.
    const bool identical = packet.networkId == lastControlPacket_.networkId &&
                           packet.sequence == lastControlPacket_.sequence &&
                           packet.pollId == lastControlPacket_.pollId &&
                           packet.flags == lastControlPacket_.flags &&
                           packet.duty == lastControlPacket_.duty;
    return identical && haveLink_ && !linkFailsafe_ ? SequenceResult::Retry
                                                   : SequenceResult::Rejected;
  }
  if (delta >= 0x8000U) {
    // A rebooted TX may restart behind our baseline. Only after the old link
    // expires, require two advancing candidates within one watchdog interval.
    // A single old packet or a fixed duplicate cannot release full speed.
    if (haveLink_ ||
        static_cast<std::uint32_t>(nowMs - lastValidPacketMs_) < config_.linkTimeoutMs) {
      return SequenceResult::Rejected;
    }
    if (!haveSequenceCandidate_ ||
        static_cast<std::uint32_t>(nowMs - sequenceCandidateMs_) >= config_.linkTimeoutMs) {
      sequenceCandidate_ = sequence;
      sequenceCandidateMs_ = nowMs;
      haveSequenceCandidate_ = true;
      return SequenceResult::Rejected;
    }
    const auto candidateDelta = static_cast<std::uint16_t>(sequence - sequenceCandidate_);
    if (candidateDelta == 0U || candidateDelta >= 0x8000U) {
      return SequenceResult::Rejected;
    }
  } else if (delta > 1U) {
    pendingStatus_ |= node_status::kSequenceGap;
    missedDownlinksSinceReply_ =
        bytes::addSaturatingU8(missedDownlinksSinceReply_, static_cast<std::uint32_t>(delta - 1U));
  }
  lastSequence_ = sequence;
  lastControlPacket_ = packet;
  haveSequenceCandidate_ = false;
  return SequenceResult::Fresh;
}

void RxController::updateTachAndStall(std::uint32_t nowMs) {
  const auto sample = tach_.sample(clock_.micros());
  if (sample.valid) {
    lastRpm_ = sample.rpm;
    pendingStatus_ &= static_cast<std::uint8_t>(~node_status::kTachTimeout);
  } else {
    lastRpm_ = 0U;
    pendingStatus_ |= node_status::kTachTimeout;
  }

  if (fan_.appliedDuty() < config_.stallDutyThreshold || lastRpm_ > 0U) {
    spinGraceActive_ = false;
    pendingStatus_ &= static_cast<std::uint8_t>(~node_status::kStall);
    return;
  }

  if (!spinGraceActive_) {
    spinGraceStartedMs_ = nowMs;
    spinGraceActive_ = true;
    pendingStatus_ &= static_cast<std::uint8_t>(~node_status::kStall);
    return;
  }

  if (static_cast<std::uint32_t>(nowMs - spinGraceStartedMs_) >= config_.stallGraceMs) {
    pendingStatus_ |= node_status::kStall;
  }
}

void RxController::sendReply(const ControlPacket& packet, std::int8_t downlinkRssiDbm,
                             const RadioRxResult& received) {
  // Reading the FIFO, rearming RX and applying PWM already consume turnaround
  // time. Do not append the entire guard again after those operations.
  const auto elapsedUs = received.receiveObserved
      ? static_cast<std::uint32_t>(clock_.micros() - received.receiveObservedUs) : 0U;
  if (elapsedUs < config_.replyDelayUs) {
    clock_.delayMicroseconds(config_.replyDelayUs - elapsedUs);
  }
  updateLinkFailsafe(clock_.millis());
  updateTachAndStall(clock_.millis());

  RpmPacket reply{};
  reply.networkId = config_.networkId;
  reply.sequenceEcho = packet.sequence;
  reply.nodeId = config_.nodeId;
  reply.status = pendingStatus_;
  reply.rpm = lastRpm_;
  reply.appliedDuty = fan_.appliedDuty();
  reply.missedDownlinks = missedDownlinksSinceReply_;
  reply.downlinkRssiDbm = downlinkRssiDbm;

  std::array<std::uint8_t, kRpmPacketSize> encoded{};
  const bool encodedOk = encodeRpmPacket(reply, encoded);
  if (received.receiveObserved) {
    const auto callAgeUs =
        static_cast<std::uint32_t>(clock_.micros() - received.receiveObservedUs);
    if (callAgeUs >= config_.replyMaxAgeUs) {
      addSaturating(radioDiagnostics_.expiredRepliesSkipped);
      return;
    }
    radioDiagnostics_.lastReplyCallAgeUs = callAgeUs;
    radioDiagnostics_.maxReplyCallAgeUs =
        std::max(radioDiagnostics_.maxReplyCallAgeUs, callAgeUs);
    if (callAgeUs > config_.replyDelayUs) {
      addSaturating(radioDiagnostics_.lateReplyCalls);
    }
  }
  if (encodedOk) {
    addSaturating(radioDiagnostics_.replyAttempts);
  }
  if (!encodedOk || radio_.transmit(encoded.data(), encoded.size()) != RadioResult::Ok) {
    addSaturating(radioDiagnostics_.replyFailures);
    pendingStatus_ |= node_status::kRadioError;
    consecutiveTransmitErrors_ = bytes::addSaturatingU8(consecutiveTransmitErrors_, 1U);
    addSaturating(radioDiagnostics_.transmitErrors);
    radioDiagnostics_.lastDriverError = radio_.lastError();
    return;
  }

  addSaturating(radioDiagnostics_.replySuccesses);
  consecutiveTransmitErrors_ = 0U;
  missedDownlinksSinceReply_ = 0U;
  pendingStatus_ &= static_cast<std::uint8_t>(~node_status::kLinkFailsafe);
  pendingStatus_ &= static_cast<std::uint8_t>(~node_status::kSequenceGap);
  pendingStatus_ &= static_cast<std::uint8_t>(~node_status::kRadioError);
}

void RxController::processControl(const ControlPacket& packet, std::int8_t downlinkRssiDbm,
                                  std::uint32_t nowMs, const RadioRxResult& received) {
  const auto sequenceResult = noteSequence(packet, nowMs);
  if (sequenceResult == SequenceResult::Rejected) {
    addSaturating(radioDiagnostics_.staleControls);
    return;
  }
  if (sequenceResult == SequenceResult::Fresh) {
    haveLink_ = true;
    linkFailsafe_ = false;
    const auto ageUs = received.receiveObserved
        ? static_cast<std::uint32_t>(clock_.micros() - received.receiveObservedUs) : 0U;
    // Round toward an older timestamp, never extend the watchdog by processing
    // time (or by a fractional-millisecond conversion).
    lastValidPacketMs_ = nowMs - ageUs / 1000U - (ageUs % 1000U != 0U ? 1U : 0U);
    lastControlProgressMs_ = lastValidPacketMs_;

    const bool forceFull = (packet.flags & control_flag::kForceFullSpeed) != 0U;
    const auto desiredDuty = forceFull ? 255U : packet.duty[config_.nodeId - 1U];
    fan_.setDuty(desiredDuty);
    updateTachAndStall(nowMs);
  }

  if (packet.pollId == config_.nodeId) {
    addSaturating(radioDiagnostics_.ownPolls);
    sendReply(packet, downlinkRssiDbm, received);
  }
}

void RxController::runOnce() {
  if (!initialized_) {
    fan_.setFullSpeed();
    return;
  }

  if (maintenance_) {
    const bool wasActive = maintenance_->active();
    maintenance_->tick(clock_.millis());
    if (wasActive || maintenance_->active()) enterLinkFailsafe();
  }
  const auto receiveStartMs = clock_.millis();
  updateLinkFailsafe(receiveStartMs);
  if (!serviceRadioRecovery()) {
    return;
  }

  const auto radioReadyMs = clock_.millis();
  updateLinkFailsafe(radioReadyMs);
  const bool linkWasHealthy = haveLink_ && !linkFailsafe_;
  std::uint32_t receiveTimeoutMs = config_.receiveTimeoutMs;
  if (linkWasHealthy) {
    const auto elapsedMs =
        static_cast<std::uint32_t>(radioReadyMs - lastValidPacketMs_);
    const auto remainingMs = config_.linkTimeoutMs - elapsedMs;
    receiveTimeoutMs = std::min(receiveTimeoutMs, remainingMs);
  }

  std::array<std::uint8_t, kControlPacketSize> buffer{};
  const auto result = radio_.receive(buffer.data(), buffer.size(), receiveTimeoutMs);
  const auto nowMs = clock_.millis();
  if (result.result == RadioResult::IoError) {
    consecutiveReceiveErrors_ = bytes::addSaturatingU8(consecutiveReceiveErrors_, 1U);
    addSaturating(radioDiagnostics_.receiveIoErrors);
    radioDiagnostics_.lastDriverError = result.driverError;
    pendingStatus_ |= node_status::kRadioError;
  } else {
    consecutiveReceiveErrors_ = 0U;
  }

  if (maintenance_ && result.result == RadioResult::Ok) {
    std::array<std::uint8_t, kControlPacketSize> reply{};
    std::size_t replyLength = 0;
    const bool wasActive = maintenance_->active();
    const bool consumed = maintenance_->handle(buffer.data(), result.length,
        reply.data(), replyLength, nowMs);
    if (wasActive || maintenance_->active() ||
        (replyLength > 0 && maintenance_->replyRequiresFullSpeed())) enterLinkFailsafe();
    if (consumed) {
      if (replyLength == reply.size()) {
        // OTA flash work may exceed the control reply budget. TX grants a
        // separate 300 ms maintenance window; only explicitly addressed RX replies.
        clock_.delayMicroseconds(config_.replyDelayUs);
        if (radio_.transmit(reply.data(), replyLength) != RadioResult::Ok) {
          consecutiveTransmitErrors_ = bytes::addSaturatingU8(consecutiveTransmitErrors_, 1U);
        } else consecutiveTransmitErrors_ = 0;
      }
      return;
    }
    if (maintenance_->active()) return;
  }

  const bool crossedDeadline =
      linkWasHealthy &&
      static_cast<std::uint32_t>(nowMs - lastValidPacketMs_) >= config_.linkTimeoutMs;
  if (crossedDeadline) {
    updateLinkFailsafe(nowMs);
    if (result.result == RadioResult::IoError) {
      pendingStatus_ |= node_status::kRadioError;
    }
    return;
  }

  if (result.result == RadioResult::Ok) {
    if (result.receiveObserved &&
        static_cast<std::uint32_t>(clock_.micros() - result.receiveObservedUs) / 1000U >=
            config_.linkTimeoutMs) {
      addSaturating(radioDiagnostics_.expiredControls);
      updateLinkFailsafe(clock_.millis());
      return;
    }
    ControlPacket packet{};
    if (!decodeControlPacket(buffer.data(), result.length, packet)) {
      addSaturating(radioDiagnostics_.malformedControls);
    } else if (packet.networkId != config_.networkId) {
      addSaturating(radioDiagnostics_.wrongNetworkControls);
    } else {
      processControl(packet, result.rssiDbm, nowMs, result);
    }
  } else if (result.result == RadioResult::IoError) {
    pendingStatus_ |= node_status::kRadioError;
  }

  updateLinkFailsafe(clock_.millis());
  (void)serviceRadioRecovery();
}

}  // namespace fanlink
