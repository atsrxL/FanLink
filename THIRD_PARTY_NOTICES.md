# Third-party notices

## RadioLib

The embedded build declares RadioLib as a PlatformIO dependency. Its source is not bundled in this archive.

- Project: RadioLib
- Upstream: https://github.com/jgromes/RadioLib
- Intended version: 7.7.1
- License: MIT

The PlatformIO pre-build hook `firmware/scripts/platformio_patch_radiolib.py` applies
three narrow, version- and source-checked integration changes to the installed
7.7.1 source: target-selected SX128x regulator mode during initial setup, a
hard limit for the TX BUSY wait, and a hard limit for the TX_DONE/DIO1 wait.
It fails the build if the resolved version or expected source snippets differ.
The dependency source itself is still not bundled.

Review the actual dependency lock/build output, patch hook and upstream license
before distributing binaries.

## ESP8266 Arduino Core / PlatformIO Espressif 8266

The embedded build declares `espressif8266@4.2.1` with the Arduino framework. Toolchain/framework source is not bundled. Review the exact resolved versions and licenses in the final build environment.

## ExpressLRS

No ExpressLRS source is copied into this starter package. ExpressLRS-compatible hardware targets may be consulted to establish board pin/RF-front-end facts. If a future contributor imports or derives ExpressLRS code, they must review GPL-3.0 obligations, update this notice and decide the project distribution/license strategy before release.
