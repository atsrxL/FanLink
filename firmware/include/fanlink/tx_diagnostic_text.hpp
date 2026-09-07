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

}  // namespace fanlink
