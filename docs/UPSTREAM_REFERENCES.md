# Upstream references for the next engineer

External source code is not bundled in this package. Verify the exact revision used by the final build.

## RadioLib

- Repository: https://github.com/jgromes/RadioLib
- SX128x API header: https://github.com/jgromes/RadioLib/blob/7.7.1/src/modules/SX128x/SX128x.h
- SX128x implementation: https://github.com/jgromes/RadioLib/blob/7.7.1/src/modules/SX128x/SX128x.cpp
- Resolved and compiled dependency: 7.7.1

The local integration reviewed FLRC configuration, sync word, CRC,
fixed-length receive, timeout units and RF-switch control against this exact
source. See `firmware/scripts/platformio_patch_radiolib.py` for the fail-closed regulator
and TX-wait integration; RSSI and RF behavior still require actual hardware.

## ESP8266 Arduino Core

- Repository: https://github.com/esp8266/Arduino
- PWM/waveform implementation: https://github.com/esp8266/Arduino/blob/master/cores/esp8266/core_esp8266_waveform_pwm.cpp
- Waveform declarations: https://github.com/esp8266/Arduino/blob/master/cores/esp8266/core_esp8266_waveform.h

Confirm 25 kHz behavior, static endpoints, interrupt interaction, GPIO1 boot behavior and the core version resolved by PlatformIO.

## PlatformIO Espressif 8266

- Platform: https://github.com/platformio/platform-espressif8266
- Resolved and compiled baseline: `espressif8266@4.2.1`, containing Arduino
  ESP8266 Core 3.1.2

## ExpressLRS hardware references

No ExpressLRS source is included. The exact commercial board target/hardware definition may still be the best source for pin and RF-front-end mapping:

- Repository: https://github.com/ExpressLRS/ExpressLRS
- Hardware-target repository: https://github.com/ExpressLRS/targets
- Reviewed target revision: `c4bd7b823594c233e673828ab493a2f8319a756a`
- JHEMCU entry: `jhemcu.rx_2400.lite`, layout `RX/Generic 2400.json`.
- BETAFPV entry: `betafpv.rx_2400.nano`, layout `RX/Generic 2400 PA.json`, with
  `radio_dcdc=true` overlay.

Both entries agree on SX1280 NSS/DIO1/RST/BUSY/SPI pins. The BETAFPV PA layout additionally
declares RXEN GPIO9 and TXEN GPIO10. JHEMCU is officially an RX target; using that hardware as
the FanLink TX role is a project-specific repurposing, not standard ELRS compatibility.

Target JSON does not replace PCB-revision, schematic or bench verification.

Importing ExpressLRS code would introduce GPL-3.0 obligations and must not be done casually. Pin mapping facts may be reimplemented without copying application code, but document their source.

## Four-wire fan interface

Use the actual fan datasheet or authoritative four-wire PWM fan specification for electrical limits, nominal 25 kHz control and Tach behavior. Do not assume all fan variants stop at 0% or expose the same internal pull-up.
