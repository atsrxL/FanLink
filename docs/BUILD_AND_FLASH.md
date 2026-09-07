# Build and flash guide

Run the commands below from the repository root. Firmware sources and the
PlatformIO project live in `firmware/`; PC tools and tests live in `pc/`.

## 1. Desktop verification

Run before and after common/controller changes:

```bash
./firmware/scripts/run_native_tests.sh
python3 pc/tools/fanlink_cli.py --self-test
python3 -m py_compile pc/tools/*.py firmware/tools/*.py firmware/scripts/*.py
./firmware/tools/run_firmware_syntax_smoke.sh
```

Optional CMake path:

```bash
cmake -S firmware -B build/cmake-firmware -DCMAKE_BUILD_TYPE=Debug
cmake --build build/cmake-firmware --parallel
ctest --test-dir build/cmake-firmware --output-on-failure
```

The syntax smoke uses repository stubs. It does not download or compile the actual Arduino Core or RadioLib.

## 2. Real PlatformIO build

Install PlatformIO in an environment with dependency access, then run:

```bash
pio run -d firmware -e fanlink_tx_esp8285
pio run -d firmware -e fanlink_rx_node01
pio run -d firmware -e fanlink_rx_node16
```

Create one clean, evidenced TX/RX01/RX16 review run without installing a global
PlatformIO CLI:

```bash
PLATFORMIO_CORE_DIR=/tmp/fanlink-pio-core \
  uv run --with 'platformio==6.1.19' python3 firmware/tools/build_firmware.py \
  --tx --rx 1 --rx 16 --run-id <new-unique-run-id>
```

For all labelled receiver images, replace the two `--rx` arguments with
`--all-rx`. Each selected environment is cleaned before compilation. A run is
published to one unique `firmware/dist/<run-id>/` only after every selected build
succeeds; failed logs are retained under `firmware/dist/failed-<run-id>/`. The published
directory includes each binary, per-file checksum, complete build log,
`BUILD_INFO.json`, and unified `SHA256SUMS`.

## 3. Pinned baseline

`firmware/platformio.ini` declares:

```text
platform = espressif8266@4.2.1
board = esp8285
framework = arduino
RadioLib = 7.7.1
C++ = gnu++17
```

Do not silently upgrade dependencies during initial takeover. First reproduce or fix the pinned build; then record any upgrade and reason in `CHANGELOG.md` and an ADR.

## 4. Configuration overrides

Defaults live in `firmware/include/fanlink/build_config.hpp`. PlatformIO build flags may override macros, for example:

```ini
-DFANLINK_NETWORK_ID=0x12345678UL
-DFANLINK_RADIO_FREQUENCY_MHZ=2405.0F
-DFANLINK_RADIO_OUTPUT_POWER_DBM=-6
-DFANLINK_RADIO_NSS_PIN=...
-DFANLINK_RADIO_DIO1_PIN=...
-DFANLINK_RADIO_RST_PIN=...
-DFANLINK_RADIO_BUSY_PIN=...
-DFANLINK_RADIO_BUSY_TIMEOUT_MS=25UL
-DFANLINK_RADIO_RXEN_PIN=...
-DFANLINK_RADIO_TXEN_PIN=...
-DFANLINK_RADIO_USE_DCDC=0
-DFANLINK_FAN_PWM_PIN=1
-DFANLINK_TACH_PIN=3
-DFANLINK_GATE_ACTIVE_HIGH=1
```

The reviewed JHEMCU Lite profile uses LDO (`0`); the BETAFPV Nano profile uses
DC-DC (`1`). These values follow the pinned ExpressLRS target data but still
require PCB-revision and bench confirmation before flashing.

The `0.1.3-dev` target-specific RF settings are:

| Target | `FANLINK_RADIO_OUTPUT_POWER_DBM` | Hardware interpretation |
|---|---:|---|
| JHEMCU 2.4G Lite TX | `+13 dBm` | Bare SX1280, approximately the 20 mW hardware ceiling |
| All BETAFPV Nano RX nodes | `-6 dBm` | Drive level corresponding to the official PA target's 25 mW power step |

The BETAFPV value is the configured SX1280/PA profile, not a measured antenna-port
power claim. Do not raise either value without confirming the exact PCB, RF path,
regulatory limit and conducted-power/thermal behavior.

Node 01 alone overrides the shared fan I/O defaults:

| RX environment | Fan PWM | Tach | External gate |
|---|---:|---:|---|
| `fanlink_rx_node01` | GPIO3 | GPIO1 | Active-low (`FANLINK_GATE_ACTIVE_HIGH=0`) |
| `fanlink_rx_node02`–`fanlink_rx_node16` | GPIO1 | GPIO3 | Active-high (default) |

The gate polarity is an electrical output-stage detail. It does not invert the
FanLink duty contract: logical `0` still requests stop and logical `255` still
requests full speed.

## 5. Before first flash

Record all of the following:

- exact board and PCB revision;
- original target/schematic source;
- boot strap pins and flash mode;
- serial recovery wiring;
- SPI/DIO/BUSY/RESET mapping;
- TCXO and RF-front-end state table;
- safe minimum transmit power;
- whether GPIO1/GPIO3 are disconnected from the programmer during normal use;
- external fan PWM and Tach protection circuit.

## 6. First-flash sequence

1. Flash without a fan connected.
2. Confirm the MCU can be recovered and reflashed.
3. Confirm power rails and idle RF-front-end states.
4. Bring up SX1280 at minimum safe power.
5. Test one-way Control packets before allowing RX transmission.
6. Attach debug/logic analyzer signals.
7. Add the open-drain PWM stage and scope it with a safe load.
8. Only after voltage checks, connect Tach and a fan.

Do not generate or distribute “final” `.bin` files until the exact board target and failure tests are documented.
