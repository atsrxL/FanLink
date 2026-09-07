#include "fanlink/rx_controller.hpp"

#include <algorithm>
#include <array>

#include "fanlink/byte_utils.hpp"

namespace fanlink {

RxController::RxController(IRadioPort& radio, IClock& clock, IFanOutput& fan, ITachometer& tach,
                           RxControllerConfig config)
    : radio_(radio), clock_(clock), fan_(fan), tach_(tach), config_(config) {}

bool RxController::begin() {
  const bool configValid = isValidNodeId(config_.nodeId);
  if (!configValid) {
    pendingStatus_ |= node_status::kConfigError;
  }

  const bool fanReady = fan_.begin();
  fan_.setFullSpeed();
  const bool tachReady = tach_.begin();
  const bool radioReady = configValid && radio_.begin(config_.networkId);
  initialized_ = fanReady && tachReady && radioReady;
  return initialized_;
}

void RxController::updateLinkFailsafe(std::uint32_t nowMs) {
  const bool expired = !haveLink_ ||
                       static_cast<std::uint32_t>(nowMs - lastValidPacketMs_) >=
                           config_.linkTimeoutMs;
  if (expired) {
    if (!linkFailsafe_ || fan_.appliedDuty() != 255U) {
      fan_.setFullSpeed();
    }
    haveLink_ = false;
    haveSequence_ = false;
    linkFailsafe_ = true;
    pendingStatus_ |= node_status::kLinkFailsafe;
  }
}

bool RxController::noteSequence(const ControlPacket& packet) {
  const auto sequence = packet.sequence;
  if (!haveSequence_) {
    lastSequence_ = sequence;
    lastControlPacket_ = packet;
    haveSequence_ = true;
    return true;
  }

  const auto delta = static_cast<std::uint16_t>(sequence - lastSequence_);
  if (delta == 0U) {
    // A duplicate is a retry only when the entire Control payload is identical.
    return packet.networkId == lastControlPacket_.networkId &&
           packet.sequence == lastControlPacket_.sequence &&
           packet.pollId == lastControlPacket_.pollId &&
           packet.flags == lastControlPacket_.flags && packet.duty == lastControlPacket_.duty;
  }
  if (delta >= 0x8000U) {
    // Modular 16-bit ordering: values more than half a sequence space behind
    // the latest accepted packet are stale and must not refresh the watchdog.
    return false;
  }
  if (delta > 1U) {
    pendingStatus_ |= node_status::kSequenceGap;
    missedDownlinksSinceReply_ =
        bytes::addSaturatingU8(missedDownlinksSinceReply_, static_cast<std::uint32_t>(delta - 1U));
  }
  lastSequence_ = sequence;
  lastControlPacket_ = packet;
  return true;
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

void RxController::sendReply(const ControlPacket& packet, std::int8_t downlinkRssiDbm) {
  clock_.delayMicroseconds(config_.replyDelayUs);
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
  if (!encodeRpmPacket(reply, encoded) ||
      radio_.transmit(encoded.data(), encoded.size()) != RadioResult::Ok) {
    pendingStatus_ |= node_status::kRadioError;
    return;
  }

  missedDownlinksSinceReply_ = 0U;
  pendingStatus_ &= static_cast<std::uint8_t>(~node_status::kLinkFailsafe);
  pendingStatus_ &= static_cast<std::uint8_t>(~node_status::kSequenceGap);
  pendingStatus_ &= static_cast<std::uint8_t>(~node_status::kRadioError);
}

void RxController::processControl(const ControlPacket& packet, std::int8_t downlinkRssiDbm,
                                  std::uint32_t nowMs) {
  if (!noteSequence(packet)) {
    return;
  }
  haveLink_ = true;
  linkFailsafe_ = false;
  lastValidPacketMs_ = nowMs;

  const bool forceFull = (packet.flags & control_flag::kForceFullSpeed) != 0U;
  const auto desiredDuty = forceFull ? 255U : packet.duty[config_.nodeId - 1U];
  fan_.setDuty(desiredDuty);
  updateTachAndStall(nowMs);

  if (packet.pollId == config_.nodeId) {
    sendReply(packet, downlinkRssiDbm);
  }
}

void RxController::runOnce() {
  if (!initialized_) {
    fan_.setFullSpeed();
    return;
  }

  const auto receiveStartMs = clock_.millis();
  updateLinkFailsafe(receiveStartMs);

  const bool linkWasHealthy = haveLink_ && !linkFailsafe_;
  std::uint32_t receiveTimeoutMs = config_.receiveTimeoutMs;
  if (linkWasHealthy) {
    const auto elapsedMs =
        static_cast<std::uint32_t>(receiveStartMs - lastValidPacketMs_);
    const auto remainingMs = config_.linkTimeoutMs - elapsedMs;
    receiveTimeoutMs = std::min(receiveTimeoutMs, remainingMs);
  }

  std::array<std::uint8_t, kControlPacketSize> buffer{};
  const auto result = radio_.receive(buffer.data(), buffer.size(), receiveTimeoutMs);
  const auto nowMs = clock_.millis();

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
    ControlPacket packet{};
    if (decodeControlPacket(buffer.data(), result.length, packet) &&
        packet.networkId == config_.networkId) {
      processControl(packet, result.rssiDbm, nowMs);
    }
  } else if (result.result == RadioResult::IoError) {
    pendingStatus_ |= node_status::kRadioError;
  }

  updateLinkFailsafe(clock_.millis());
}

}  // namespace fanlink
