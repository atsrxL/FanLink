#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "fanlink/interfaces.hpp"
#include "fanlink/protocol.hpp"

namespace fanlink {

struct TxControllerConfig {
  std::uint32_t networkId{0};
  std::uint32_t hostTimeoutMs{500};
  std::uint32_t slotPeriodUs{10000};
  std::uint32_t replyTimeoutMs{5};
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
};

class TxController {
 public:
  TxController(IRadioPort& radio, IClock& clock, TxControllerConfig config);

  bool begin();
  void setAllDuty(const std::array<std::uint8_t, kNodeCount>& duty);
  bool setNodeDuty(std::uint8_t nodeId, std::uint8_t duty);
  void noteHostHeartbeat();
  void runOneSlot();

  const std::array<std::uint8_t, kNodeCount>& requestedDuty() const { return requestedDuty_; }
  const NodeTelemetry& nodeTelemetry(std::uint8_t nodeId) const;
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
  bool acceptReply(const std::uint8_t* data, std::size_t length, std::uint8_t expectedNode,
                   std::uint16_t expectedSequence, std::int8_t uplinkRssiDbm);

  IRadioPort& radio_;
  IClock& clock_;
  TxControllerConfig config_;
  std::array<std::uint8_t, kNodeCount> requestedDuty_{};
  std::array<NodeTelemetry, kNodeCount> nodes_{};
  std::uint8_t nextPollId_{1};
  std::uint16_t sequence_{0};
  std::uint32_t lastHostMessageMs_{0};
  TxRadioDiagnostics radioDiagnostics_{};
  bool hostSeen_{false};
  bool initialized_{false};
};

}  // namespace fanlink
