#if defined(FANLINK_BUILD_RX)

#include <Arduino.h>
#include <algorithm>
extern "C" {
#include <user_interface.h>
}

#include "fanlink/build_config.hpp"
#include "fanlink/ota_identity.hpp"
#include "fanlink/ota_receiver.hpp"
#include "fanlink/settings_rx_maintenance.hpp"
#include "fanlink/rx_uart_diagnostic.hpp"
#include "fanlink/platform/esp8266/settings_store.hpp"
#include "fanlink/platform/esp8266/runtime_rx_pins.hpp"
#include "fanlink/platform/esp8266/ota_storage.hpp"
#include "fanlink/rx_controller.hpp"
#include "fanlink/platform/esp8266/arduino_clock.hpp"
#include "fanlink/platform/esp8266/fan_output.hpp"
#include "fanlink/platform/esp8266/radiolib_sx1280_port.hpp"
#include "fanlink/platform/esp8266/status_led.hpp"
#include "fanlink/platform/esp8266/tachometer.hpp"

namespace {

using fanlink::RxController;
using fanlink::RxControllerConfig;
using fanlink::platform::esp8266::ArduinoClock;
using fanlink::platform::esp8266::RuntimeFanOutput;
using fanlink::platform::esp8266::Esp8266StatusLed;
using fanlink::platform::esp8266::RuntimeTachometer;
using fanlink::platform::esp8266::RadioLibSx1280Port;
using fanlink::platform::esp8266::Sx1280PortConfig;

ArduinoClock clockPort;
RadioLibSx1280Port hardwareRadio(Sx1280PortConfig{
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
fanlink::diagnostic::ObservedRadio radioPort(hardwareRadio);
fanlink::diagnostic::Logger logger;
bool uartMode{false};
std::uint32_t lastTraceMs{0};

void serviceRadioTrace() {
  const auto nowMs = clockPort.millis();
  if (static_cast<std::uint32_t>(nowMs - lastTraceMs) < 25U ||
      Serial.availableForWrite() < 127 || !hardwareRadio.canEmitDeferredTrace()) return;
  fanlink::RadioTraceRecord record{};
  if (!hardwareRadio.peekRadioTrace(record)) return;
  char line[119]{};
  const auto count = fanlink::formatRadioTrace(record, line, sizeof(line) - 1U);
  lastTraceMs = nowMs;
  if (count == 0U) { hardwareRadio.popRadioTrace(); return; }
  line[count] = '\n';
  if (Serial.availableForWrite() <= static_cast<int>(count + 1U)) return;
  // Never flush or retry a partial write on the receive-to-reply path.
  (void)Serial.write(reinterpret_cast<const std::uint8_t*>(line), count + 1U);
  hardwareRadio.popRadioTrace();
}
RuntimeFanOutput fanOutput(fanlink::build::kPwmHz, fanlink::build::kGateActiveHigh);
RuntimeTachometer tachometer(
    fanlink::build::kTachPulsesPerRev,
    fanlink::build::kTachTimeoutUs, fanlink::build::kTachMinEdgeUs,
    fanlink::build::kTachMaxRpm);
Esp8266StatusLed statusLed(fanlink::build::kStatusLedPin,
                           fanlink::build::kStatusLedActiveHigh);
fanlink::platform::esp8266::Esp8266OtaStorage otaStorage;
fanlink::platform::esp8266::Esp8266SettingsStore settingsStore;
constexpr fanlink::settings::PinMode defaultPinMode =
#if defined(FANLINK_RX_UART_DIAGNOSTIC)
    fanlink::settings::PinMode::Uart;
#else
    fanlink::build::kFanPwmPin == 3 ? fanlink::settings::PinMode::PwmSwap : fanlink::settings::PinMode::Pwm;
#endif
fanlink::settings::SettingsService settingsService(settingsStore,
    fanlink::build::kNetworkId, fanlink::build::kNodeId,
    fanlink::settings::Profile::Lite, defaultPinMode, fanlink::build::kRadioOutputPowerDbm);
fanlink::ota::Receiver otaReceiver(otaStorage, fanOutput, fanlink::build::kNetworkId,
    fanlink::build::kNodeId, fanlink::ota::compatibilityId());
fanlink::settings::RxMaintenance maintenance(settingsService, otaReceiver, fanOutput);
const auto otaImageIdentity PROGMEM = fanlink::ota::imageIdentity(2);
RxController controller(
    radioPort, clockPort, fanOutput, tachometer,
    [] {
      RxControllerConfig config{fanlink::build::kNetworkId,
                       fanlink::build::kNodeId,
                       fanlink::build::kRxReceiveTimeoutMs,
                       fanlink::build::kRxLinkTimeoutMs,
                       fanlink::build::kReplyDelayUs,
                       fanlink::build::kStallDutyThreshold,
                       fanlink::build::kStallGraceMs};
      config.replyMaxAgeUs = fanlink::build::kReplyTimeoutMs * 1000U;
      return config;
    }());

}  // namespace

void setup() {
  // Do not drive an unknown pin role while validating persistent storage.
  pinMode(1, INPUT);
  pinMode(3, INPUT);
  (void)settingsService.begin();
  const auto selected = settingsService.selected();
  fanOutput.select(selected.mode);
  tachometer.select(selected.mode);
  (void)hardwareRadio.setOutputPower(selected.powerDbm);
  uartMode = selected.mode == fanlink::settings::PinMode::Uart;
  if (uartMode) {
    logger.setIdentity(fanlink::build::kNodeId, true);
    Serial.begin(115200, SERIAL_8N1);
    Serial.setDebugOutput(false);
    if (const auto* reset = ESP.getResetInfoPtr()) logger.setResetReason(reset->reason);
  }
  asm volatile("" : : "r"(otaImageIdentity.data()) : "memory");
  controller.setMaintenance(&maintenance);
  statusLed.begin();
  controller.begin();
  statusLed.update(!controller.linkFailsafeActive(), clockPort.millis());
}

void loop() {
  if (uartMode) logger.noteLoop(clockPort.micros());
  controller.runOnce();
  if (otaReceiver.rebootDue(clockPort.millis()) || settingsService.rebootDue(clockPort.millis())) {
    fanOutput.setFullSpeed();
    ESP.restart();
  }
  statusLed.update(!controller.linkFailsafeActive(), clockPort.millis());
  if (uartMode && !maintenance.active() &&
      !logger.service(Serial, clockPort.millis(), controller, radioPort))
    serviceRadioTrace();
  yield();
}

#endif  // FANLINK_BUILD_RX
