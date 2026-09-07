# FanLink validation results

Generated: 2026-09-04, local-only review  
Current working tree: `Unreleased` / `0.1.3-dev`  
Historical evidence baseline: `0.1.1`; current-source clean-build evidence: `0.1.3-dev`

## Host toolchain

```text
Darwin 24.6.0 arm64
Apple clang 17.0.0
cmake 4.3.4
Python 3.14.7
uv 0.9.27
```

Original input archive (preserved unchanged):

```text
FanLink_Starter_2026-09-04.zip
SHA-256 4f2ae680bcf4bd830a244c521d0d0c235db69af5626b87b907131df50fd19af5
```

## Evidence scope

The `209 checks`, sanitizer result, source/package gates and first three clean
PlatformIO builds below belong to the published `0.1.1` evidence baseline. They
do **not** cover the current Unreleased TX control panel, RX status LED, expanded
radio diagnostics or target-power changes. Current-source validation and its own
clean firmware evidence are recorded separately below.

## `0.1.1` desktop regression

```text
./scripts/run_native_tests.sh
  PASS: 209 checks

Clang AddressSanitizer + UndefinedBehaviorSanitizer native suite
  PASS: 209 checks

python3 tools/fanlink_cli.py --self-test
  PASS: fanlink_cli self-test

PYTHONOPTIMIZE=1 python3 tools/fanlink_cli.py --self-test
  PASS: fanlink_cli self-test

python3 -m py_compile tools/*.py scripts/*.py
  PASS

./tools/run_firmware_syntax_smoke.sh
  PASS: firmware syntax smoke (stub APIs only)

cmake -S . -B build/cmake-final -DCMAKE_BUILD_TYPE=Debug
cmake --build build/cmake-final --parallel
ctest --test-dir build/cmake-final --output-on-failure
  PASS: 1/1 tests
```

The syntax smoke still uses repository API stubs and is not counted as the real
embedded build.

## `0.1.1` real clean PlatformIO builds

Command:

```bash
PLATFORMIO_CORE_DIR=/tmp/fanlink-pio-core-01a06bca \
  uv run --with 'platformio==6.1.19' python3 tools/build_firmware.py \
  --tx --rx 1 --rx 16 \
  --run-id 20260904-jhemcu-lite-betafpv-nano-reviewed
```

Resolved baseline:

```text
PlatformIO Core 6.1.19
Platform: espressif8266 4.2.1
Framework: Arduino ESP8266 Core 3.1.2
Toolchain: xtensa-lx106-elf GCC 10.3.0
RadioLib 7.7.1
```

Results:

```text
fanlink_tx_esp8285     CLEAN + BUILD PASS
fanlink_rx_node01      CLEAN + BUILD PASS
fanlink_rx_node16      CLEAN + BUILD PASS
```

The build hook also succeeded from a previously absent RX environment after
PlatformIO installed a fresh RadioLib 7.7.1 dependency. It verifies the exact
dependency version/source before applying the target regulator selection and
25 ms BUSY/TX_DONE hard limits. RadioLib is compiled with static 64-byte payload
buffers; the port owns its `ArduinoHal`, avoiding the heap-allocating convenience
`Module` constructor.

Published evidence directory:

```text
dist/20260904-jhemcu-lite-betafpv-nano-reviewed/
```

`BUILD_INFO.json` records dependency/source-manifest provenance. `SHA256SUMS`,
per-binary checksum files and the three complete `*.build.log` files provide
artifact-file checksum reread verification. This is not receiver flash readback.
Exact sizes and digests are intentionally read from that directory rather than
duplicated here.

## `0.1.1` target-profile evidence

Against ExpressLRS targets commit
`c4bd7b823594c233e673828ab493a2f8319a756a`:

- TX build: JHEMCU 2.4GHz Lite RX hardware repurposed as FanLink TX, LDO;
- RX builds: BETAFPV 2.4GHz Nano RX, RXEN=9, TXEN=10, DC-DC;
- common radio pins: NSS=15, DIO1=4, RST=2, BUSY=5, SCK=14, MISO=12,
  MOSI=13.

This validates build profiles, not the PCB revision of either physical board.

## `0.1.1` source/package gates

```text
make verify
  PASS

./scripts/source_manifest.sh --check
  PASS

reviewed source ZIP unzip -t
  PASS

reviewed source ZIP extracted SOURCE_MANIFEST.sha256
  PASS
```

## Previous `0.1.2-dev` control-panel checks

The following current-source checks were actually run. Full command/output evidence
is recorded in [`docs/evidence/20260904-tx-panel-usb.md`](docs/evidence/20260904-tx-panel-usb.md).

```text
./scripts/run_native_tests.sh
  PASS: 260 checks

Clang AddressSanitizer + UndefinedBehaviorSanitizer native suite
  PASS: 260 checks

python3 -m unittest discover -s tests -p 'test_fanlink_panel.py' -v
  Ran 17 tests in 3.155 s — OK

python3 tools/fanlink_cli.py --self-test
PYTHONOPTIMIZE=1 python3 tools/fanlink_cli.py --self-test
  PASS: fanlink_cli self-test (both)

python3 -m py_compile tools/*.py scripts/*.py tests/test_fanlink_panel.py
node --check panel/app.js
  PASS (exit 0)

./tools/run_firmware_syntax_smoke.sh
  PASS: firmware syntax smoke (stub APIs only)

cmake / build / ctest in build/cmake-20260904-v012
  PASS: 1/1 tests
```

The sanitizer pass retained address and undefined-behavior instrumentation but
disabled leak detection because the macOS ASan runtime reports it unsupported.

These are offline fake-serial, demo, validation and local HTTP/API tests. The
separate physical-TX results below establish part of the real serial path, but do
not establish exact 200 ms keepalive timing, the exact 500 ms watchdog boundary,
new `ERROR_EVENT` diagnostics, RF operation or fan behavior.

Current-source release gates:

```text
./scripts/source_manifest.sh --write
./scripts/source_manifest.sh --check
make verify
  PASS

PlatformIO clean TX/RX01/RX16
  PASS: 3/3
```

The provenance-bound firmware evidence batch is
`dist/20260904-v0.1.2-panel-led-rfdiag-reviewed/`. It contains complete logs,
`BUILD_INFO.json`, `SHA256SUMS`, per-binary checksum files and the following
reread-verified artifacts:

```text
fanlink_tx_esp8285.bin
fanlink_rx_node01.bin
fanlink_rx_node16.bin
```

Exact sizes and digests are read from that batch's `BUILD_INFO.json` and
`SHA256SUMS`, rather than duplicated in this manifest-covered document. Separate
clean batches can embed different build metadata and therefore need not have the
same binary digest.

This clean compilation does not establish flashing, application boot, RF, LED,
PWM, Tach/RPM, fan or thermal behavior.

## Current `0.1.3-dev` requested-change checks

This batch changes the BETAFPV reply-power step, Node 01-only fan wiring/polarity,
and a backward-compatible Host RSSI frame. Air packet length/version remain unchanged.

```text
./scripts/run_native_tests.sh
  PASS: 269 checks

Clang AddressSanitizer + UndefinedBehaviorSanitizer, detect_leaks=0
  PASS: 269 checks

python3 tools/fanlink_cli.py --self-test
PYTHONOPTIMIZE=1 python3 tools/fanlink_cli.py --self-test
  PASS: fanlink_cli self-test (both)

python3 -m unittest discover -s tests -p 'test_fanlink_panel.py' -v
  Ran 20 tests - OK

python3 -m py_compile tools/*.py scripts/*.py tests/test_fanlink_panel.py
node --check panel/app.js
./tools/run_firmware_syntax_smoke.sh
  PASS

cmake / build / ctest in build/cmake-20260904-v013
  PASS: 1/1

make verify
  PASS

PlatformIO clean TX/RX01/RX16
  PASS: 3/3
```

The preflight batch is
`dist/20260904-v0.1.3-25mw-node01-rssi-preflight/`. The final manifest-bound batch
is `dist/20260904-v0.1.3-25mw-node01-rssi-reviewed/`; read exact sizes and SHA-256
from its `BUILD_INFO.json` and `SHA256SUMS`. Full scope and physical observations
are in
[`docs/evidence/20260904-v013-power-node01-rssi.md`](docs/evidence/20260904-v013-power-node01-rssi.md).

These builds prove compilation with the pinned real dependencies. They do not prove
antenna-port power, new-image boot, corrected PWM endpoints, physical RSSI values, or
the Node 01 pin swap.

## Real TX serial acceptance — partial

| Item | Result |
|---|---|
| Enumerate and open the physical TX port at 115200 8N1 | **VERIFIED** — `/dev/cu.usbmodem<device>` (unique identifier redacted), USB `1A86:55D3` |
| Confirm safe initial API command state after connect | **VERIFIED** — `connected=true`, all 16 duties `255`; first on-wire frame not captured |
| Confirm complete `duty[16]` frames every 200 ms | **NOT TIMING-VERIFIED** |
| Receive and cross-check a real `STATUS_SNAPSHOT` | **VERIFIED** — `host_failsafe=false`, uptime 3130 s, `next_poll_id=2`, fresh age 0.0837 s, parser error counters all zero |
| Inject and cross-check a real `ERROR_EVENT` | **NOT VERIFIED** — attached TX has older firmware without the new diagnostic |
| POST disconnect panel state | **VERIFIED AS PANEL STATE ONLY** — immediately all `255`/stale; not itself TX-failsafe proof |
| Observe TX Host failsafe/full-speed after disconnect | **VERIFIED, TIMING NOT MEASURED** — independent CLI repeatedly showed `TX=FAILSAFE` and all node duties `255` |
| Disconnect/reconnect and serial-error recovery | **NOT VERIFIED** |
| Browser displays the physical TX state | **VERIFIED** — matched the API data |
| Backend process loss indication | **VERIFIED** — next poll locked all controls and cleared watchdog/RPM/applied-duty/age/miss to unknown |
| TX after backend process loss | **VERIFIED, TIMING NOT MEASURED** — independent CLI again showed `TX=FAILSAFE`, all duties `255` |
| USB unplug/serial-I/O error/reconnect | **NOT VERIFIED** |

The controls are open-loop PWM duty commands, not closed-loop RPM targets. A later
old-firmware `0.1.2` session established the Node 01 RF/control/reply path: the RX LED
was steady, duty control was effective, and a live API snapshot showed requested and
applied duty `143`, RPM `1749`, age `0`, missed polls `0` and non-stale telemetry.
An earlier duty `0` observation still showed about 2787 RPM, which is evidence of the
Node 01 physical polarity mismatch. It is not acceptance of the unflashed correction.

## Flash and application observation after the `0.1.1` validation run

TX and RX were subsequently flashed with the `0.1.2` images. No flash readback was
captured, but later application behavior established TX Host operation, RX steady-link
LED, duty control and RPM replies. This is behavioral acceptance of the running
`0.1.2` path, not binary readback and not acceptance of the unflashed `0.1.3-dev`
artifacts.

## Not validated

- No flashed image has been read back; the new `0.1.3-dev` images have not been flashed.
- Physical PCB revisions and original firmware targets were not read back.
- SX1280/SX1281, oscillator, DCDC inductor, TCXO and PA/LNA/RF-switch hardware
  were not inspected or measured.
- RF control/replies were observed, but separate RSSI values, antenna-port power,
  airtime, turnaround, packet-error rate and endurance were not measured.
- No 25 kHz PWM waveform, GPIO1 ROM boot transient or fail-to-full transition
  was measured on an oscilloscope.
- Tach voltage, pulses per revolution, RPM accuracy and stall behavior were not
  measured on a fan.
- The nominal 10 ms slot has not been accepted against worst-case physical
  timing.
- The TX local panel and old-firmware Node 01 data path have partial physical
  acceptance; exact timing, error injection, new RSSI, corrected PWM endpoints,
  calibrated RPM and thermal acceptance remain open.
