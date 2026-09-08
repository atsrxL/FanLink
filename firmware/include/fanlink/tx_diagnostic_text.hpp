#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <limits>

#include "fanlink/tx_controller.hpp"

namespace fanlink {

// RadioLib currently uses small negative error constants. Clamp only their
// human-readable representation so every possible diagnostic still fits in a
// single 128-byte ERROR_EVENT; the controller retains the full-width values.
constexpr int printableRadioError(int value) {
  return std::max(-32768, std::min(32767, value));
}

// ESP8266 Core 3.1.2 may wait when a write fills the final reported FIFO byte.
// Diagnostic frames are expendable: require a spare byte, never flush/wait.
constexpr bool txDiagnosticFrameCanWrite(std::size_t frameLength, int available) {
  return frameLength != 0U && available > 0 &&
         static_cast<std::size_t>(available) > frameLength;
}

inline std::size_t formatTxRadioDiagnostics(const TxRadioDiagnostics& diagnostics,
                                            char* destination,
                                            std::size_t capacity) {
  if (destination == nullptr || capacity == 0U) {
    return 0U;
  }
  const int count = std::snprintf(
      destination, capacity,
      "RF b%u B%d f%u e%d X%d R%d n%u s%u x%lu t%lu c%lu i%lu v%lu o%lu",
      diagnostics.beginOk ? 1U : 0U, printableRadioError(diagnostics.beginDriverError),
      static_cast<unsigned>(diagnostics.lastFault),
      printableRadioError(diagnostics.lastDriverError),
      printableRadioError(diagnostics.lastTransmitDriverError),
      printableRadioError(diagnostics.lastReceiveDriverError), diagnostics.lastNodeId,
      diagnostics.lastSequence,
      static_cast<unsigned long>(diagnostics.transmitFailures),
      static_cast<unsigned long>(diagnostics.receiveTimeouts),
      static_cast<unsigned long>(diagnostics.receiveCrcErrors),
      static_cast<unsigned long>(diagnostics.receiveIoErrors),
      static_cast<unsigned long>(diagnostics.invalidReplies),
      static_cast<unsigned long>(diagnostics.acceptedReplies));
  if (count <= 0 || static_cast<std::size_t>(count) >= capacity) {
    destination[0] = '\0';
    return 0U;
  }
  return static_cast<std::size_t>(count);
}

// A separate ERROR_EVENT leaves the existing RF text grammar and its 128-byte
// worst-case bound intact. Reinitialization success is not peer-link success.
inline std::size_t formatTxRadioRecoveryDiagnostics(const TxRadioDiagnostics& diagnostics,
                                                    char* destination,
                                                    std::size_t capacity) {
  if (destination == nullptr || capacity == 0U) {
    return 0U;
  }
  const int count = std::snprintf(destination, capacity,
      "RFR attempts=%lu successes=%lu failures=%lu",
      static_cast<unsigned long>(diagnostics.recoveryAttempts),
      static_cast<unsigned long>(diagnostics.recoverySuccesses),
      static_cast<unsigned long>(diagnostics.recoveryFailures));
  if (count <= 0 || static_cast<std::size_t>(count) >= capacity) {
    destination[0] = '\0';
    return 0U;
  }
  return static_cast<std::size_t>(count);
}

inline std::uint32_t txNodeTelemetryAgeMs(const NodeTelemetry& node, std::uint32_t nowMs) {
  return node.seen ? static_cast<std::uint32_t>(nowMs - node.lastSeenMs)
                   : std::numeric_limits<std::uint32_t>::max();
}

inline std::uint8_t txNodeTelemetryStatus(const NodeTelemetry& node, std::uint32_t nowMs) {
  return static_cast<std::uint8_t>(
      node.status | (txNodeTelemetryAgeMs(node, nowMs) > kTxTelemetryFreshnessMs
                         ? node_status::kTelemetryStale
                         : 0U));
}

// Never-seen slots would dominate a one-node installation. Retain previously
// seen nodes in this rotation after they go stale so their outage is visible.
inline std::uint8_t nextTxNodeDiagnosticId(const TxController& controller,
                                          std::uint8_t previousNodeId) {
  auto nodeId = isValidNodeId(previousNodeId) ? previousNodeId : 0U;
  for (std::size_t attempt = 0U; attempt < kNodeCount; ++attempt) {
    nodeId = nodeId >= kNodeCount ? 1U : nodeId + 1U;
    if (controller.nodeTelemetry(static_cast<std::uint8_t>(nodeId)).seen) {
      return static_cast<std::uint8_t>(nodeId);
    }
  }
  return 0U;
}

// Preserve RFN/RFT's existing selection. New error diagnostics also show a
// never-seen node with errors; an entirely quiet cold start reports Node01.
inline std::uint8_t nextTxReceiveErrorDiagnosticId(const TxController& controller,
                                                 std::uint8_t previousNodeId) {
  auto nodeId = isValidNodeId(previousNodeId) ? previousNodeId : 0U;
  for (std::size_t attempt = 0U; attempt < kNodeCount; ++attempt) {
    nodeId = nodeId >= kNodeCount ? 1U : nodeId + 1U;
    const auto id = static_cast<std::uint8_t>(nodeId);
    if (controller.nodeTelemetry(id).seen ||
        controller.nodeDiagnostics(id).receiveErrors.events != 0U) return id;
  }
  return 1U;
}

// The longest valid record is 113 bytes, including every uint32 counter at its
// saturation limit. Counters describe completed slots; no in-flight poll is
// included in p. CRC/I/O/invalid events identify a poll window, not a sender.
inline std::size_t formatTxNodeDiagnostics(std::uint8_t nodeId,
                                          const NodeTelemetry& node,
                                          const TxNodeDiagnostics& diagnostics,
                                          std::uint32_t nowMs,
                                          char* destination,
                                          std::size_t capacity) {
  if (destination == nullptr || capacity == 0U) {
    return 0U;
  }
  if (!isValidNodeId(nodeId)) {
    destination[0] = '\0';
    return 0U;
  }
  const int count = std::snprintf(
      destination, capacity,
      "RFN n%u p%lu o%lu m%u a%lu S%u x%lu t%lu c%lu i%lu v%lu",
      nodeId, static_cast<unsigned long>(diagnostics.transmittedPolls),
      static_cast<unsigned long>(diagnostics.acceptedReplies), node.missedPolls,
      static_cast<unsigned long>(txNodeTelemetryAgeMs(node, nowMs)),
      txNodeTelemetryStatus(node, nowMs),
      static_cast<unsigned long>(diagnostics.transmitFailures),
      static_cast<unsigned long>(diagnostics.receiveTimeouts),
      static_cast<unsigned long>(diagnostics.receiveCrcErrors),
      static_cast<unsigned long>(diagnostics.receiveIoErrors),
      static_cast<unsigned long>(diagnostics.invalidReplies));
  if (count <= 0 || static_cast<std::size_t>(count) >= capacity) {
    destination[0] = '\0';
    return 0U;
  }
  return static_cast<std::size_t>(count);
}

// Independent optional text: existing RF/RFN grammar and binary frames stay
// unchanged. Counters are attributed to a poll window, not a proven sender.
inline std::size_t formatTxTimingDiagnostics(std::uint8_t nodeId,
                                             const TxNodeDiagnostics& diagnostics,
                                             char* destination, std::size_t capacity) {
  if (destination == nullptr || capacity == 0U) return 0U;
  if (!isValidNodeId(nodeId)) {
    destination[0] = '\0';
    return 0U;
  }
  const int count = std::snprintf(destination, capacity,
      "RFT n%u d%lu l%lu h%lu a%lu z%lu r%u",
      nodeId, static_cast<unsigned long>(diagnostics.observedReplies),
      static_cast<unsigned long>(diagnostics.lateReplies),
      static_cast<unsigned long>(diagnostics.processedAfterDeadline),
      static_cast<unsigned long>(diagnostics.lastObservedOffsetUs),
      static_cast<unsigned long>(diagnostics.lastCompletedOffsetUs),
      static_cast<unsigned>(diagnostics.lastRejectReason));
  if (count <= 0 || static_cast<std::size_t>(count) >= capacity) {
    destination[0] = '\0';
    return 0U;
  }
  return static_cast<std::size_t>(count);
}

inline std::size_t formatTxReceiveErrorCounts(std::uint8_t nodeId,
                                             const TxNodeDiagnostics& diagnostics,
                                             char* destination, std::size_t capacity) {
  if (destination == nullptr || capacity == 0U) return 0U;
  if (!isValidNodeId(nodeId)) {
    destination[0] = '\0';
    return 0U;
  }
  const auto& errors = diagnostics.receiveErrors;
  const int count = std::snprintf(destination, capacity,
      "RFC n%u e%lu s%lu c%lu p%lu h%lu y%lu u%lu j%lu", nodeId,
      static_cast<unsigned long>(errors.events),
      static_cast<unsigned long>(errors.syncWithoutRxDone),
      static_cast<unsigned long>(errors.irqCrc),
      static_cast<unsigned long>(errors.packetCrc),
      static_cast<unsigned long>(errors.header),
      static_cast<unsigned long>(errors.irqSync),
      static_cast<unsigned long>(errors.unknown),
      static_cast<unsigned long>(diagnostics.lastReceiveStartedOffsetUs));
  if (count <= 0 || static_cast<std::size_t>(count) >= capacity) {
    destination[0] = '\0';
    return 0U;
  }
  return static_cast<std::size_t>(count);
}

inline std::size_t formatTxReceiveErrorEvent(std::uint8_t nodeId,
                                            const TxNodeDiagnostics& diagnostics,
                                            char* destination, std::size_t capacity) {
  if (destination == nullptr || capacity == 0U) return 0U;
  if (!isValidNodeId(nodeId)) {
    destination[0] = '\0';
    return 0U;
  }
  const auto& event = diagnostics.receiveErrors.last;
  const int count = std::snprintf(destination, capacity,
      "RFE n%u k%u s%u v%u f%u q%04X p%02X y%02X r%d a%lu b%lu", nodeId,
      static_cast<unsigned>(event.kind), static_cast<unsigned>(event.sequence),
      static_cast<unsigned>(event.validity), static_cast<unsigned>(event.errorFlags),
      static_cast<unsigned>(event.irq), static_cast<unsigned>(event.packetStatusError),
      static_cast<unsigned>(event.packetStatusSync), static_cast<int>(event.errorRssiDbm),
      static_cast<unsigned long>(event.observedOffsetUs),
      static_cast<unsigned long>(event.receiveStartedOffsetUs));
  if (count <= 0 || static_cast<std::size_t>(count) >= capacity) {
    destination[0] = '\0';
    return 0U;
  }
  return static_cast<std::size_t>(count);
}

}  // namespace fanlink
