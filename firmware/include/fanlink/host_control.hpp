#pragma once

#include <algorithm>
#include <array>

#include "fanlink/serial_protocol.hpp"
#include "fanlink/tx_controller.hpp"

namespace fanlink {

// Only normal control dispatch. The caller must first enforce maintenance
// arbitration and feed frames through the version/CRC-checking serial parser.
// Rejected lengths, nodes and message types never refresh the Host watchdog.
inline bool handleHostControlFrame(const SerialFrame& frame, TxController& controller) {
  switch (frame.type) {
    case serial_type::kSetAllDuty: {
      if (frame.payloadLength != kSetAllDutyPayloadSize) return false;
      std::array<std::uint8_t, kNodeCount> duty{};
      std::copy_n(frame.payload.begin(), duty.size(), duty.begin());
      controller.setAllDuty(duty);
      return true;
    }
    case serial_type::kSetOneDuty:
      return frame.payloadLength == 2U &&
             controller.setNodeDuty(frame.payload[0], frame.payload[1]);
    case serial_type::kHeartbeat:
      if (frame.payloadLength != 0U) return false;
      controller.noteHostHeartbeat();
      return true;
    default:
      return false;
  }
}

}  // namespace fanlink
