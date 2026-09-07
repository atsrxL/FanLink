#include "fanlink/tx_controller.hpp"

#include <algorithm>
#include <limits>

#include "fanlink/byte_utils.hpp"

namespace fanlink {
namespace {

const NodeTelemetry kInvalidNodeTelemetry{};

void addSaturating(std::uint32_t& value) {
  if (value != std::numeric_limits<std::uint32_t>::max()) {
    ++value;
  }
}

}  // namespace

TxController::TxController(IRadioPort& radio, IClock& clock, TxControllerConfig config)
    : radio_(radio), clock_(clock), config_(config) {
  requestedDuty_.fill(255U);
}

bool TxController::begin() {
  requestedDuty_.fill(255U);
  nextPollId_ = 1U;
  sequence_ = 0U;
  hostSeen_ = false;
  initialized_ = radio_.begin(config_.networkId);
  radioDiagnostics_ = {};
  radioDiagnostics_.beginOk = initialized_;
  radioDiagnostics_.beginDriverError = radio_.lastError();
  if (!initialized_) {
    recordRadioFault(TxRadioFault::Begin, radioDiagnostics_.beginDriverError, 0U, 0U);
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

bool TxController::acceptReply(const std::uint8_t* data, std::size_t length,
                               std::uint8_t expectedNode, std::uint16_t expectedSequence,
                               std::int8_t uplinkRssiDbm) {
  RpmPacket packet{};
  if (!decodeRpmPacket(data, length, packet) || packet.networkId != config_.networkId ||
      packet.nodeId != expectedNode || packet.sequenceEcho != expectedSequence) {
    return false;
  }

  auto& node = nodes_[packet.nodeId - 1U];
  node.rpm = packet.rpm;
  node.status = static_cast<std::uint8_t>(packet.status & ~node_status::kTelemetryStale);
  node.appliedDuty = packet.appliedDuty;
  node.missedPolls = 0U;
  node.uplinkRssiDbm = uplinkRssiDbm;
  node.downlinkRssiDbm = packet.downlinkRssiDbm;
  node.lastSeenMs = clock_.millis();
  node.seen = true;
  return true;
}

void TxController::runOneSlot() {
  if (!initialized_) {
    return;
  }

  const auto slotStartUs = clock_.micros();
  const auto nowMs = clock_.millis();
  const auto polledNode = nextPollId_;
  const auto slotSequence = sequence_;
  const bool hostFresh = hostIsFresh(nowMs);

  ControlPacket packet{};
  packet.networkId = config_.networkId;
  packet.sequence = slotSequence;
  packet.pollId = polledNode;
  packet.duty = requestedDuty_;
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
    radioDiagnostics_.lastTransmitDriverError = driverError;
    recordRadioFault(TxRadioFault::Transmit, driverError, polledNode, slotSequence);
  } else {
    std::array<std::uint8_t, kRpmPacketSize> reply{};
    const auto result = radio_.receive(reply.data(), reply.size(), config_.replyTimeoutMs);
    switch (result.result) {
      case RadioResult::Ok:
        replyAccepted = acceptReply(reply.data(), result.length, polledNode, slotSequence,
                                    result.rssiDbm);
        if (replyAccepted) {
          addSaturating(radioDiagnostics_.acceptedReplies);
          // A later success must not erase the last actionable radio fault.
          addSaturating(radioDiagnostics_.generation);
        } else {
          addSaturating(radioDiagnostics_.invalidReplies);
          recordRadioFault(TxRadioFault::InvalidReply, result.driverError, polledNode,
                           slotSequence);
        }
        break;
      case RadioResult::Timeout:
        addSaturating(radioDiagnostics_.receiveTimeouts);
        radioDiagnostics_.lastReceiveDriverError = result.driverError;
        recordRadioFault(TxRadioFault::ReceiveTimeout, result.driverError, polledNode,
                         slotSequence);
        break;
      case RadioResult::CrcError:
        addSaturating(radioDiagnostics_.receiveCrcErrors);
        radioDiagnostics_.lastReceiveDriverError = result.driverError;
        recordRadioFault(TxRadioFault::ReceiveCrc, result.driverError, polledNode,
                         slotSequence);
        break;
      case RadioResult::IoError:
        addSaturating(radioDiagnostics_.receiveIoErrors);
        radioDiagnostics_.lastReceiveDriverError = result.driverError;
        recordRadioFault(TxRadioFault::ReceiveIo, result.driverError, polledNode,
                         slotSequence);
        break;
    }
  }

  if (!replyAccepted) {
    recordMiss(polledNode);
  }

  sequence_ = static_cast<std::uint16_t>(sequence_ + 1U);
  nextPollId_ = nextPollId_ >= kNodeCount ? 1U : static_cast<std::uint8_t>(nextPollId_ + 1U);

  const auto elapsedUs = static_cast<std::uint32_t>(clock_.micros() - slotStartUs);
  if (elapsedUs < config_.slotPeriodUs) {
    clock_.delayMicroseconds(config_.slotPeriodUs - elapsedUs);
  }
}

}  // namespace fanlink
