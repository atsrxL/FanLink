#if defined(FANLINK_BUILD_TX)

#include <Arduino.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "fanlink/build_config.hpp"
#include "fanlink/host_control.hpp"
#include "fanlink/ota_identity.hpp"
#include "fanlink/ota_tx_bridge.hpp"
#include "fanlink/ota_tx_local.hpp"
#include "fanlink/tx_lifecycle.hpp"
#include "fanlink/settings_tx.hpp"
#include "fanlink/tach_diagnostic.hpp"
#include "fanlink/platform/esp8266/settings_store.hpp"
#include "fanlink/platform/esp8266/ota_storage.hpp"
#include "fanlink/byte_utils.hpp"
#include "fanlink/protocol.hpp"
#include "fanlink/serial_protocol.hpp"
#include "fanlink/tx_controller.hpp"
#include "fanlink/tx_diagnostic_text.hpp"
#include "fanlink/tx_diagnostic_scheduler.hpp"
#include "fanlink/platform/esp8266/arduino_clock.hpp"
#include "fanlink/platform/esp8266/radiolib_sx1280_port.hpp"

namespace {

using fanlink::SerialFrame;
using fanlink::SerialFrameParser;
using fanlink::TxController;
using fanlink::TxControllerConfig;
using fanlink::TxDiagnosticAttempt;
using fanlink::TxDiagnosticFamily;
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
fanlink::ota::TxBridge otaBridge(radioPort, clockPort, controller, fanlink::build::kNetworkId);
fanlink::tachdiag::TxBridge tachBridge(radioPort, clockPort, fanlink::build::kNetworkId);
const auto otaImageIdentity PROGMEM = fanlink::ota::imageIdentity(1);
std::array<std::uint8_t, 28> loadOtaIdentity() {
  std::array<std::uint8_t, 28> identity{};
  // ESP8266 flash constants need the PROGMEM-aware path, including when the
  // descriptor linker address is unaligned. Keep exactly one image marker.
  memcpy_P(identity.data(), otaImageIdentity.data(), identity.size());
  return identity;
}
fanlink::platform::esp8266::Esp8266OtaStorage localRawStorage;
fanlink::ota::IdentityStorage localStorage(localRawStorage, loadOtaIdentity());
fanlink::ota::TxLocalUpdate localUpdate(localStorage, clockPort, controller,
    fanlink::build::kNetworkId, fanlink::build::kNodeId, fanlink::ota::compatibilityId());
fanlink::TxLifecycle lifecycle(clockPort, controller, fanlink::build::kNetworkId);
fanlink::platform::esp8266::Esp8266SettingsStore settingsStore;
fanlink::settings::SettingsService localSettings(settingsStore, fanlink::build::kNetworkId,
    0, fanlink::settings::Profile::Lite, fanlink::settings::PinMode::NotApplicable,
    fanlink::build::kRadioOutputPowerDbm);
fanlink::settings::TxSettings settingsBridge(radioPort, clockPort, controller,
    localSettings, fanlink::build::kNetworkId);
SerialFrameParser hostParser;
std::uint16_t statusSequence = 0U;
std::uint32_t lastStatusMs = 0U;
std::uint32_t lastLinkMetricsMs = 0U;
fanlink::TxDiagnosticScheduler diagnosticScheduler;
std::uint32_t lastDiagnosticGeneration = 0U;
std::uint32_t lastRecoveryDiagnosticAttempts = 0U;
std::uint8_t lastDiagnosticNodeId = 0U;
std::uint8_t lastTimingNodeId = 0U;
std::uint8_t lastReceiveErrorCountsNodeId = 0U;
std::uint8_t lastReceiveErrorEventNodeId = 0U;
std::uint32_t lastScheduledStartUs = 0U;
bool haveScheduledStart = false;

bool writeSerialFrame(const SerialFrame& frame) {
  std::array<std::uint8_t, fanlink::kSerialMaxFrameSize> encoded{};
  std::size_t length = 0U;
  if (!fanlink::encodeSerialFrame(frame, encoded, length)) {
    return false;
  }
  return Serial.write(encoded.data(), length) == length;
}

bool writeDiagnosticFrame(const SerialFrame& frame) {
  std::array<std::uint8_t, fanlink::kSerialMaxFrameSize> encoded{};
  std::size_t length = 0U;
  if (!fanlink::encodeSerialFrame(frame, encoded, length) ||
      !fanlink::txDiagnosticFrameCanWrite(length, Serial.availableForWrite())) return false;
  return Serial.write(encoded.data(), length) == length;
}

TxDiagnosticAttempt writeOptionalDiagnostic(const char* message, std::size_t count) {
  return fanlink::writeTxOptionalDiagnostic(Serial, message, count, statusSequence);
}

void handleHostFrame(const SerialFrame& frame) {
  if (frame.type == fanlink::tachdiag::kHostRequest) {
    (void)tachBridge.enqueue(frame.payload.data(), frame.payloadLength, frame.sequence,
        otaBridge.active() || localUpdate.active() || lifecycle.active() || settingsBridge.active());
    return;
  }
  if (tachBridge.active()) {
    (void)fanlink::handleHostControlFrame(frame, controller);
    return;
  }
  if (frame.type == 0x15) {
    (void)settingsBridge.enqueue(frame.payload.data(), frame.payloadLength, frame.sequence,
        otaBridge.active() || localUpdate.active() || lifecycle.active());
    return;
  }
  if (frame.type == 0x14) {
    (void)lifecycle.enqueue(frame, otaBridge.active() || localUpdate.active() || settingsBridge.active());
    return;
  }
  if (settingsBridge.active()) return;
  if (lifecycle.active()) return;
  if (frame.type == 0x12 || frame.type == 0x13) {
    if (!otaBridge.active())
      (void)localUpdate.enqueue(frame.payload.data(), frame.payloadLength, frame.sequence,
                                frame.type == 0x13);
    return;
  }
  if (localUpdate.active()) return;
  if (frame.type == 0x11) {
    (void)otaBridge.enqueueBlock(frame.payload.data(), frame.payloadLength, frame.sequence);
    return;
  }
  if (frame.type == 0x10) {
    (void)otaBridge.enqueue(frame.payload.data(), frame.payloadLength, frame.sequence);
    return;
  }
  if (otaBridge.active()) return;
  (void)fanlink::handleHostControlFrame(frame, controller);
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
  frame.payloadLength = fanlink::kStatusSnapshotPayloadSize;
  frame.payload[0] = controller.hostFailsafeActive() ? 0x01U : 0x00U;
  frame.payload[1] = controller.nextPollId();
  fanlink::bytes::putU32Le(frame.payload.data() + 2U, nowMs / 1000U);

  for (std::uint8_t nodeId = 1U; nodeId <= fanlink::kNodeCount; ++nodeId) {
    const auto& node = controller.nodeTelemetry(nodeId);
    const std::size_t offset = 6U + static_cast<std::size_t>(nodeId - 1U) * 6U;
    fanlink::bytes::putU16Le(frame.payload.data() + offset, node.rpm);

    const auto ageMs = fanlink::txNodeTelemetryAgeMs(node, nowMs);
    frame.payload[offset + 2U] = fanlink::txNodeTelemetryStatus(node, nowMs);
    frame.payload[offset + 3U] = node.appliedDuty;
    frame.payload[offset + 4U] = fanlink::bytes::saturateU8(ageMs / 100U);
    frame.payload[offset + 5U] = node.missedPolls;
  }

  if (!writeDiagnosticFrame(frame)) { lastStatusMs = nowMs - fanlink::build::kStatusPeriodMs; return true; }
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
        node.seen && static_cast<std::uint32_t>(nowMs - node.lastSeenMs) <= fanlink::kTxTelemetryFreshnessMs;
    const auto uplinkRssi =
        fresh ? node.uplinkRssiDbm : static_cast<std::int8_t>(-127);
    const auto downlinkRssi =
        fresh ? node.downlinkRssiDbm : static_cast<std::int8_t>(-127);
    frame.payload[offset] = static_cast<std::uint8_t>(uplinkRssi);
    frame.payload[offset + 1U] = static_cast<std::uint8_t>(downlinkRssi);
  }

  if (!writeDiagnosticFrame(frame)) { lastLinkMetricsMs = nowMs - fanlink::build::kStatusPeriodMs; return true; }
  return true;
}

TxDiagnosticAttempt emitRadioDiagnostic() {
  const auto& diag = controller.radioDiagnostics();
  if (diag.generation == lastDiagnosticGeneration) {
    return TxDiagnosticAttempt::NotReady;
  }

  char message[fanlink::kSerialMaxPayload + 1U]{};
  const auto count = fanlink::formatTxRadioDiagnostics(diag, message, sizeof(message));
  const auto result = writeOptionalDiagnostic(message, count);
  if (result == TxDiagnosticAttempt::Sent) lastDiagnosticGeneration = diag.generation;
  return result;
}

TxDiagnosticAttempt emitRadioRecoveryDiagnostic() {
  const auto& diag = controller.radioDiagnostics();
  if (diag.recoveryAttempts == lastRecoveryDiagnosticAttempts) {
    return TxDiagnosticAttempt::NotReady;
  }
  char message[fanlink::kSerialMaxPayload + 1U]{};
  const auto count = fanlink::formatTxRadioRecoveryDiagnostics(diag, message, sizeof(message));
  const auto result = writeOptionalDiagnostic(message, count);
  if (result == TxDiagnosticAttempt::Sent) lastRecoveryDiagnosticAttempts = diag.recoveryAttempts;
  return result;
}

TxDiagnosticAttempt emitNodeDiagnostic() {
  const auto nowMs = clockPort.millis();
  const auto nodeId = fanlink::nextTxNodeDiagnosticId(controller, lastDiagnosticNodeId);
  if (nodeId == 0U) {
    return TxDiagnosticAttempt::NotReady;
  }
  char message[fanlink::kSerialMaxPayload + 1U]{};
  const auto count = fanlink::formatTxNodeDiagnostics(
      nodeId, controller.nodeTelemetry(nodeId), controller.nodeDiagnostics(nodeId),
      nowMs, message, sizeof(message));
  const auto result = writeOptionalDiagnostic(message, count);
  if (result == TxDiagnosticAttempt::Sent) lastDiagnosticNodeId = nodeId;
  return result;
}

TxDiagnosticAttempt emitTimingDiagnostic() {
  auto nodeId = fanlink::nextTxNodeDiagnosticId(controller, lastTimingNodeId);
  // A cold-start outage must still expose Node01 timing, unlike RFN's
  // intentionally ever-seen-only rotation.
  if (nodeId == 0U) nodeId = 1U;
  char message[fanlink::kSerialMaxPayload + 1U]{};
  const auto count = fanlink::formatTxTimingDiagnostics(
      nodeId, controller.nodeDiagnostics(nodeId), message, sizeof(message));
  const auto result = writeOptionalDiagnostic(message, count);
  if (result == TxDiagnosticAttempt::Sent) lastTimingNodeId = nodeId;
  return result;
}

TxDiagnosticAttempt emitReceiveErrorCounts() {
  const auto nodeId = fanlink::nextTxReceiveErrorDiagnosticId(
      controller, lastReceiveErrorCountsNodeId);
  char message[fanlink::kSerialMaxPayload + 1U]{};
  const auto count = fanlink::formatTxReceiveErrorCounts(
      nodeId, controller.nodeDiagnostics(nodeId), message, sizeof(message));
  const auto result = writeOptionalDiagnostic(message, count);
  if (result == TxDiagnosticAttempt::Sent) lastReceiveErrorCountsNodeId = nodeId;
  return result;
}

TxDiagnosticAttempt emitReceiveErrorEvent() {
  const auto nodeId = fanlink::nextTxReceiveErrorDiagnosticId(
      controller, lastReceiveErrorEventNodeId);
  char message[fanlink::kSerialMaxPayload + 1U]{};
  const auto count = fanlink::formatTxReceiveErrorEvent(
      nodeId, controller.nodeDiagnostics(nodeId), message, sizeof(message));
  const auto result = writeOptionalDiagnostic(message, count);
  if (result == TxDiagnosticAttempt::Sent) lastReceiveErrorEventNodeId = nodeId;
  return result;
}

TxDiagnosticAttempt emitRadioTrace() {
  // Do not format close to the next RF slot. Timestamp taken before the
  // controller's cadence check is conservative (slightly early).
  if (!haveScheduledStart ||
      static_cast<std::uint32_t>(clockPort.micros() - lastScheduledStartUs) >=
          fanlink::build::kSlotPeriodUs - 2000U) return TxDiagnosticAttempt::NotReady;
  fanlink::RadioTraceRecord record{};
  if (!radioPort.peekRadioTrace(record)) return TxDiagnosticAttempt::NotReady;
  if (Serial.availableForWrite() < 127) return TxDiagnosticAttempt::Blocked;
  char message[118]{};
  const auto count = fanlink::formatRadioTrace(record, message, sizeof(message));
  const auto result = writeOptionalDiagnostic(message, count);
  if (result == TxDiagnosticAttempt::Sent || result == TxDiagnosticAttempt::Dropped)
    radioPort.popRadioTrace();
  return result;
}

TxDiagnosticAttempt emitOptionalDiagnostic(TxDiagnosticFamily family) {
  switch (family) {
    case TxDiagnosticFamily::Recovery: return emitRadioRecoveryDiagnostic();
    case TxDiagnosticFamily::Node: return emitNodeDiagnostic();
    case TxDiagnosticFamily::Timing: return emitTimingDiagnostic();
    case TxDiagnosticFamily::Radio: return emitRadioDiagnostic();
    case TxDiagnosticFamily::Counts: return emitReceiveErrorCounts();
    case TxDiagnosticFamily::Event: return emitReceiveErrorEvent();
    case TxDiagnosticFamily::Trace: return emitRadioTrace();
  }
  return TxDiagnosticAttempt::NotReady;
}

}  // namespace

void setup() {
  asm volatile("" : : "r"(otaImageIdentity.data()) : "memory");
  Serial.begin(kHostBaud, SERIAL_8N1);
  (void)localSettings.begin();
  (void)radioPort.setOutputPower(localSettings.selected().powerDbm);
  controller.begin();
  lastStatusMs = clockPort.millis();
  lastLinkMetricsMs = lastStatusMs;
  diagnosticScheduler.reset(lastStatusMs);
}

void loop() {
  serviceHostInput();
  tachBridge.service();
  fanlink::tachdiag::Packet tachReply{}; std::uint16_t tachSequence = 0;
  if (tachBridge.replyReady(tachReply, tachSequence)) {
    SerialFrame frame{}; frame.type = fanlink::tachdiag::kHostReply;
    frame.sequence = tachSequence; frame.payloadLength = tachReply.size();
    std::copy(tachReply.begin(), tachReply.end(), frame.payload.begin());
    tachBridge.replyWritten(writeDiagnosticFrame(frame));
  }
  if (tachBridge.waiting()) { yield(); return; }
  const bool wasSettings = settingsBridge.active();
  const bool settingsService = settingsBridge.service();
  std::array<std::uint8_t, 30> settingsReply{};
  std::uint16_t settingsSequence = 0;
  if (settingsBridge.replyReady(settingsReply, settingsSequence)) {
    SerialFrame frame{}; frame.type = 0x93; frame.sequence = settingsSequence;
    frame.payloadLength = settingsReply.size();
    std::copy(settingsReply.begin(), settingsReply.end(), frame.payload.begin());
    settingsBridge.replyWritten(writeDiagnosticFrame(frame));
  }
  if (settingsService) {
    if (wasSettings && !settingsBridge.active()) {
      hostParser.reset();
      const int buffered = Serial.available();
      for (int i = 0; i < buffered; ++i) (void)Serial.read();
    }
    if (settingsBridge.localRebootReady()) { Serial.flush(); ESP.restart(); }
    yield(); return;
  }
  const bool wasLocal = localUpdate.active();
  const bool wasLifecycle = lifecycle.active();
  const bool localService = localUpdate.service();
  const bool lifecycleService = lifecycle.service();
  SerialFrame lifecycleReply{};
  if (lifecycle.replyReady(lifecycleReply)) lifecycle.replyWritten(writeSerialFrame(lifecycleReply));
  if (localService || lifecycleService) {
    if ((wasLocal && !localUpdate.active()) || (wasLifecycle && !lifecycle.active())) {
      hostParser.reset();
      const int buffered = Serial.available();
      for (int i = 0; i < buffered; ++i) (void)Serial.read();
    }
    std::array<std::uint8_t, 30> reply{};
    std::uint16_t sequence = 0;
    if (localUpdate.takeReply(reply, sequence)) {
      SerialFrame frame{}; frame.type = 0x91; frame.sequence = sequence;
      frame.payloadLength = reply.size();
      std::copy(reply.begin(), reply.end(), frame.payload.begin());
      (void)writeSerialFrame(frame);
    }
    const auto action = lifecycle.dueAction();
    if (action || localUpdate.rebootDue()) {
      Serial.flush();
      if (action == 2) ESP.rebootIntoUartDownloadMode();
      else ESP.restart();
    }
    yield(); return;
  }
  const bool wasMaintenance = otaBridge.active();
  if (otaBridge.service()) {
    if (wasMaintenance && !otaBridge.active()) {
      // A partial frame captured during maintenance must never become a new
      // duty command when its remaining bytes arrive after release.
      hostParser.reset();
      const int buffered = Serial.available();
      for (int i = 0; i < buffered; ++i) (void)Serial.read();
    }
    std::array<std::uint8_t, 30> reply{};
    std::uint16_t sequence = 0;
    if (otaBridge.takeReply(reply, sequence)) {
      SerialFrame frame{};
      frame.type = 0x90;
      frame.sequence = sequence;
      frame.payloadLength = reply.size();
      std::copy(reply.begin(), reply.end(), frame.payload.begin());
      (void)writeSerialFrame(frame);
    }
    yield();
    return;
  }
  const auto scheduledStartUs = clockPort.micros();
  if (controller.runScheduledSlot()) {
    lastScheduledStartUs = scheduledStartUs;
    haveScheduledStart = true;
    tachBridge.startAfterSlot();
  }
  // The production C6 freshness path takes precedence over optional text.
  // Quiet iterations drain at most one nonblocking UART frame. Keeping this
  // independent of RF slots prevents diagnostics starvation at 125 ms cadence.
  if (!emitStatusSnapshot() && !emitLinkMetricsSnapshot()) {
    diagnosticScheduler.service(clockPort.millis(), emitOptionalDiagnostic);
  }
  yield();
}

#endif  // FANLINK_BUILD_TX
