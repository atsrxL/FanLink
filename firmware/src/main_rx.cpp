#if defined(FANLINK_BUILD_RX)

#include <Arduino.h>

#include "fanlink/build_config.hpp"
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
using fanlink::platform::esp8266::Esp8266FanOutput;
using fanlink::platform::esp8266::Esp8266StatusLed;
using fanlink::platform::esp8266::Esp8266Tachometer;
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
Esp8266FanOutput fanOutput(fanlink::build::kFanPwmPin, fanlink::build::kPwmHz,
                           fanlink::build::kGateActiveHigh);
Esp8266Tachometer tachometer(
    fanlink::build::kTachPin, fanlink::build::kTachPulsesPerRev,
    fanlink::build::kTachTimeoutUs, fanlink::build::kTachMinEdgeUs,
    fanlink::build::kTachMaxRpm);
Esp8266StatusLed statusLed(fanlink::build::kStatusLedPin,
                           fanlink::build::kStatusLedActiveHigh);
RxController controller(
    radioPort, clockPort, fanOutput, tachometer,
    RxControllerConfig{fanlink::build::kNetworkId,
                       fanlink::build::kNodeId,
                       fanlink::build::kRxReceiveTimeoutMs,
                       fanlink::build::kRxLinkTimeoutMs,
                       fanlink::build::kReplyDelayUs,
                       fanlink::build::kStallDutyThreshold,
                       fanlink::build::kStallGraceMs});

}  // namespace

void setup() {
  // No Serial.begin(): GPIO1 and GPIO3 are dedicated to fan PWM and Tach.
  statusLed.begin();
  controller.begin();
  statusLed.update(!controller.linkFailsafeActive(), clockPort.millis());
}

void loop() {
  controller.runOnce();
  statusLed.update(!controller.linkFailsafeActive(), clockPort.millis());
  yield();
}

#endif  // FANLINK_BUILD_RX
