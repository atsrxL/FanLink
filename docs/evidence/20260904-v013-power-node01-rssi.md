# FanLink 0.1.3 power, Node 01 wiring and link-metrics evidence

Date: 2026-09-04  
Scope: local source, fixed toolchain builds, and observations from the already-flashed
`0.1.2` TX/RX pair. The new `0.1.3-dev` binaries were not flashed during this edit/build
step.

## Requested configuration

- JHEMCU 2.4 GHz Lite TX: `+13 dBm`, the RadioLib/SX1280 and official generic-target
  maximum. With no external PA this is approximately 20 mW chip-side, not a true
  25 mW antenna-port claim.
- BETAFPV 2.4 GHz Nano RX: SX1280 drive `-6 dBm`, which selects the official PA
  target's 25 mW power step for RX-to-TX replies.
- RX Node 01 only: PWM GPIO3, Tach/Sensor GPIO1, and
  `FANLINK_GATE_ACTIVE_HIGH=0`. Nodes 02-16 retain PWM GPIO1, Tach GPIO3, and the
  default gate polarity.
- Host duty semantics remain `0=0%/stop request`, `255=100%/full-speed request`.
  Only the Node 01 platform output polarity is changed.
- Host frame `0x82 LINK_METRICS_SNAPSHOT`: fixed 32-byte payload, two signed bytes
  per node (`uplink_rssi_dbm`, `downlink_rssi_dbm`); `-127` means unknown. TX and
  the panel suppress stale node values.

## Software verification

```text
./scripts/run_native_tests.sh
  PASS: 269 checks

Clang AddressSanitizer + UndefinedBehaviorSanitizer, detect_leaks=0
  PASS: 269 checks

python3 tools/fanlink_cli.py --self-test
PYTHONOPTIMIZE=1 python3 tools/fanlink_cli.py --self-test
  PASS: fanlink_cli self-test (both)

python3 -m py_compile tools/*.py scripts/*.py tests/test_fanlink_panel.py
node --check panel/app.js
  PASS

python3 -m unittest discover -s tests -p 'test_fanlink_panel.py' -v
  Ran 20 tests - OK

./tools/run_firmware_syntax_smoke.sh
  PASS: firmware syntax smoke (stub APIs only)

cmake -S . -B build/cmake-20260904-v013 -DCMAKE_BUILD_TYPE=Debug
cmake --build build/cmake-20260904-v013 --parallel
ctest --test-dir build/cmake-20260904-v013 --output-on-failure
  PASS: 1/1
```

Preflight clean PlatformIO build:

```text
dist/20260904-v0.1.3-25mw-node01-rssi-preflight/
fanlink_tx_esp8285     CLEAN + BUILD PASS
fanlink_rx_node01      CLEAN + BUILD PASS
fanlink_rx_node16      CLEAN + BUILD PASS
```

The final manifest-bound batch is
`dist/20260904-v0.1.3-25mw-node01-rssi-reviewed/`; its `BUILD_INFO.json`,
`SHA256SUMS`, per-image checksum and build logs are authoritative for sizes and
digests.

## Existing `0.1.2` hardware observations

The local panel remained connected to `/dev/cu.usbmodem<device>` at 115200
(unique device identifier redacted for publication).
One read-only `/api/state` snapshot showed:

```text
connected=true, host_failsafe=false
Node 01 requested_duty=143, applied_duty=143, rpm=1749
Node 01 age_100ms=0, missed_polls=0, status=0x08, stale=false
TX RF begin_ok=true, transmit_failures=0, accepted_replies=4704
```

Earlier endpoint evidence also showed a logical duty of `0` while the fan still
reported about 2787 RPM. Together with the user's physical control observation,
this is the basis for the Node 01-only polarity correction. The RX LED was observed
steady while linked and control plus RPM replies were functioning. These observations
prove a working application/RF/control/reply path for the old `0.1.2` pair, but do not
validate the newly built power, swapped pins, corrected endpoints, or `0x82` RSSI path.

The large aggregate RX-timeout count is dominated by polling absent Nodes 02-16 and
must not be interpreted as Node 01 packet-error rate. RF output power/EIRP, packet
airtime, turnaround, PWM frequency/duty waveform, Tach voltage/calibration, and
long-run thermal behavior were not instrumented.
