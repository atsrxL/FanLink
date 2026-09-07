#if defined(FANLINK_BUILD_TX)

#include <Arduino.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "fanlink/build_config.hpp"
#include "fanlink/byte_utils.hpp"
#include "fanlink/protocol.hpp"
#include "fanlink/serial_protocol.hpp"
#include "fanlink/tx_controller.hpp"
#include "fanlink/tx_diagnostic_text.hpp"
#include "fanlink/platform/esp8266/arduino_clock.hpp"
#include "fanlink/platform/esp8266/radiolib_sx1280_port.hpp"

namespace {

using fanlink::SerialFrame;
using fanlink::SerialFrameParser;
using fanlink::TxController;
using fanlink::TxControllerConfig;
using fanlink::build::kHostBaud;
using fanlink::platform::esp8266::ArduinoClock;
using fanlink::platform::esp8266::RadioLibSx1280Port;
using fanlink::platform::esp8266::Sx1280PortConfig;

ArduinoClock clockPort;
RadioLibSx1280Port radioPort(Sx1280PortConfig{
    fanlink::build::kRadioNssPin,
    fanlink::build::kRadioDio1Pin,
    fanlink::build::kRadioRstPin,
    fanlink::build::kRadioBusyPin,
    fanlink::build::kRadioRxEnPin,
    fanlink::build::kRadioTxEnPin,
    fanlink::build::kRadioUseDcdc,
    fanlink::build::kRadioSpiHz,
    fanlink::build::kRadioFrequencyMhz,
    fanlink::build::kRadioBitrateKbps,
    fanlink::build::kRadioCodingRate,
    fanlink::build::kRadioOutputPowerDbm,
    fanlink::build::kRadioPreambleBits,
    fanlink::build::kRadioBusyTimeoutMs,
});
TxController controller(
    radioPort, clockPort,
    TxControllerConfig{fanlink::build::kNetworkId, fanlink::build::kHostTimeoutMs,
                       fanlink::build::kSlotPeriodUs, fanlink::build::kReplyTimeoutMs});
SerialFrameParser hostParser;
std::uint16_t statusSequence = 0U;
std::uint32_t lastStatusMs = 0U;
std::uint32_t lastLinkMetricsMs = 0U;
std::uint32_t lastDiagnosticMs = 0U;
std::uint32_t lastDiagnosticGeneration = 0U;

bool writeSerialFrame(const SerialFrame& frame) {
  std::array<std::uint8_t, fanlink::kSerialMaxFrameSize> encoded{};
  std::size_t length = 0U;
  if (!fanlink::encodeSerialFrame(frame, encoded, length)) {
    return false;
  }
  return Serial.write(encoded.data(), length) == length;
}

void handleHostFrame(const SerialFrame& frame) {
  switch (frame.type) {
    case fanlink::serial_type::kSetAllDuty: {
      if (frame.payloadLength != fanlink::kNodeCount) {
        return;
      }
      std::array<std::uint8_t, fanlink::kNodeCount> duty{};
      std::copy_n(frame.payload.begin(), fanlink::kNodeCount, duty.begin());
      controller.setAllDuty(duty);
      return;
    }

    case fanlink::serial_type::kSetOneDuty:
      if (frame.payloadLength == 2U) {
        controller.setNodeDuty(frame.payload[0], frame.payload[1]);
      }
      return;

    case fanlink::serial_type::kHeartbeat:
      if (frame.payloadLength == 0U) {
        controller.noteHostHeartbeat();
      }
      return;

    default:
      return;
  }
}

void serviceHostInput() {
  SerialFrame frame{};
  while (Serial.available() > 0) {
    const auto byte = static_cast<std::uint8_t>(Serial.read());
    if (hostParser.feed(byte, frame)) {
      handleHostFrame(frame);
    }
  }
}

bool emitStatusSnapshot() {
  const auto nowMs = clockPort.millis();
  if (static_cast<std::uint32_t>(nowMs - lastStatusMs) < fanlink::build::kStatusPeriodMs) {
    return false;
  }
  lastStatusMs = nowMs;

  SerialFrame frame{};
  frame.type = fanlink::serial_type::kStatusSnapshot;
  frame.sequence = statusSequence++;
  frame.payloadLength = 102U;
  frame.payload[0] = controller.hostFailsafeActive() ? 0x01U : 0x00U;
  frame.payload[1] = controller.nextPollId();
  fanlink::bytes::putU32Le(frame.payload.data() + 2U, nowMs / 1000U);

  for (std::uint8_t nodeId = 1U; nodeId <= fanlink::kNodeCount; ++nodeId) {
    const auto& node = controller.nodeTelemetry(nodeId);
    const std::size_t offset = 6U + static_cast<std::size_t>(nodeId - 1U) * 6U;
    fanlink::bytes::putU16Le(frame.payload.data() + offset, node.rpm);

    const auto ageMs = node.seen ? static_cast<std::uint32_t>(nowMs - node.lastSeenMs) : 25500U;
    std::uint8_t status = node.status;
    if (!node.seen || ageMs > 1000U) {
      status |= fanlink::node_status::kTelemetryStale;
    }
    frame.payload[offset + 2U] = status;
    frame.payload[offset + 3U] = node.appliedDuty;
    frame.payload[offset + 4U] = fanlink::bytes::saturateU8(ageMs / 100U);
    frame.payload[offset + 5U] = node.missedPolls;
  }

  (void)writeSerialFrame(frame);
  return true;
}

bool emitLinkMetricsSnapshot() {
  const auto nowMs = clockPort.millis();
  if (static_cast<std::uint32_t>(nowMs - lastLinkMetricsMs) <
      fanlink::build::kStatusPeriodMs) {
    return false;
  }
  lastLinkMetricsMs = nowMs;

  SerialFrame frame{};
  frame.type = fanlink::serial_type::kLinkMetricsSnapshot;
  frame.sequence = statusSequence++;
  frame.payloadLength =
      static_cast<std::uint16_t>(fanlink::kLinkMetricsPayloadSize);

  for (std::uint8_t nodeId = 1U; nodeId <= fanlink::kNodeCount; ++nodeId) {
    const auto& node = controller.nodeTelemetry(nodeId);
    const std::size_t offset = static_cast<std::size_t>(nodeId - 1U) * 2U;
    const bool fresh =
        node.seen && static_cast<std::uint32_t>(nowMs - node.lastSeenMs) <= 1000U;
    const auto uplinkRssi =
        fresh ? node.uplinkRssiDbm : static_cast<std::int8_t>(-127);
    const auto downlinkRssi =
        fresh ? node.downlinkRssiDbm : static_cast<std::int8_t>(-127);
    frame.payload[offset] = static_cast<std::uint8_t>(uplinkRssi);
    frame.payload[offset + 1U] = static_cast<std::uint8_t>(downlinkRssi);
  }

  (void)writeSerialFrame(frame);
  return true;
}

bool emitRadioDiagnostic() {
  const auto nowMs = clockPort.millis();
  if (static_cast<std::uint32_t>(nowMs - lastDiagnosticMs) < 1000U) {
    return false;
  }

  const auto& diag = controller.radioDiagnostics();
  if (diag.generation == lastDiagnosticGeneration) {
    return false;
  }
  lastDiagnosticMs = nowMs;

  char message[fanlink::kSerialMaxPayload + 1U]{};
  const auto count = fanlink::formatTxRadioDiagnostics(diag, message, sizeof(message));
  if (count == 0U || count > fanlink::kSerialMaxPayload) {
    return false;
  }

  SerialFrame frame{};
  frame.type = fanlink::serial_type::kErrorEvent;
  frame.sequence = statusSequence++;
  frame.payloadLength = static_cast<std::uint16_t>(count);
  std::memcpy(frame.payload.data(), message, frame.payloadLength);
  if (!writeSerialFrame(frame)) {
    return false;
  }
  lastDiagnosticGeneration = diag.generation;
  return true;
}

}  // namespace

void setup() {
  Serial.begin(kHostBaud, SERIAL_8N1);
  controller.begin();
  lastStatusMs = clockPort.millis();
  lastLinkMetricsMs = lastStatusMs;
  lastDiagnosticMs = lastStatusMs;
}

void loop() {
  serviceHostInput();
  controller.runOneSlot();
  if (!emitRadioDiagnostic() && !emitStatusSnapshot()) {
    (void)emitLinkMetricsSnapshot();
  }
  yield();
}

#endif  // FANLINK_BUILD_TX
