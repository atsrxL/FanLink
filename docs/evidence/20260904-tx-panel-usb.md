# 2026-09-04 TX control-panel USB acceptance

## Scope and setup

- Host: macOS, local HTTP service bound to `127.0.0.1:8765`.
- TX: JHEMCU 2.4GHz Lite hardware, connected through
  `/dev/cu.usbmodem<device>` (`VID:PID 1A86:55D3`; unique device identifier redacted for publication).
- Serial: 115200 8N1 through the project Host protocol.
- Fan remained disconnected. This check did not exercise an RX, PWM output,
  Tach input, fan, temperature path or successful RF reply.
- The attached TX was still running the earlier firmware image and therefore
  did not emit the new compact `ERROR_EVENT` diagnostic.

## Current-source software gates

```text
./scripts/run_native_tests.sh
  PASS: 260 checks

Clang AddressSanitizer + UndefinedBehaviorSanitizer native binary
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

The sanitizer invocation was rerun without leak detection because the macOS
AddressSanitizer runtime reported that `detect_leaks` is unsupported. Address
and undefined-behavior instrumentation both remained enabled for the passing
run.

## Current-source release and clean-build gates

After the panel/LED/diagnostic sources and hand-off documents were finalized:

```text
./scripts/source_manifest.sh --write
./scripts/source_manifest.sh --check
make verify
  PASS

PLATFORMIO_CORE_DIR=/tmp/fanlink-pio-power-ab-core-01a06bca \
  uv run --with 'platformio==6.1.19' python3 tools/build_firmware.py \
  --tx --rx 1 --rx 16 \
  --run-id 20260904-v0.1.2-panel-led-rfdiag-reviewed
  PASS: TX/RX01/RX16 clean + build
```

Evidence directory:

```text
dist/20260904-v0.1.2-panel-led-rfdiag-reviewed/
fanlink_tx_esp8285.bin
fanlink_rx_node01.bin
fanlink_rx_node16.bin
```

`BUILD_INFO.json`, aggregate and per-binary SHA-256 files, and complete clean
build logs were generated and reread. Exact sizes and digests live in that batch;
they are not duplicated here because separate clean builds can embed different
build metadata. This is compile/package provenance, not a flash readback or
physical-function acceptance.

## Physical TX serial observations

1. `/api/ports` enumerated the exact USB UART, including `VID:PID 1A86:55D3`.
2. `POST /api/connect` returned `connected=true`; the new session started with
   all 16 requested duties equal to `255`.
3. A subsequent live state contained:

   ```text
   host_failsafe=false
   uptime_seconds=3130
   next_poll_id=2
   status_snapshot_fresh=true
   last_status_age_seconds=0.0837
   serial_rejected_frames=0
   unknown_frames=0
   status_decode_errors=0
   ```

4. All 16 node records remained `rpm=0`, `age_100ms=255`,
   `missed_polls=255`, `stale=true`; no RF/RX link is claimed.
5. Two HTTP state samples around a nominal 1.1 second wait showed
   `send_sequence=100` then `104`, and `last_send_at` advanced by about
   0.951 seconds. This supports continued full-state sending but is not an
   on-wire measurement of the exact 200 ms interval.
6. The browser UI displayed the same live port, Host-watchdog state, changing
   poll ID, fresh frame age and 0/16 online nodes.

## Failure-path observations

- A normal panel disconnect reset the backend view to all duties `255`, no live
  snapshot and all nodes stale. An independent CLI monitor then repeatedly
  displayed `TX=FAILSAFE` and duty `255` for every node.
- The HTTP backend was also terminated while connected. Within the next browser
  poll, the UI displayed `本地服务不可达`, disabled every control, changed Host
  watchdog to `未知`, and cleared RPM, applied duty, age and miss values to `—`.
- After that backend termination, an independent CLI snapshot began with:

  ```text
  TX=FAILSAFE uptime=3276s next_poll=8
  N01..N16 duty=255 age=stale miss=255
  ```

These observations prove eventual TX Host failsafe after the two stop paths.
They do not measure the exact 500 ms threshold or prove a full-speed PWM
waveform at an RX/fan.

## Still not accepted

- first Host frame captured on wire and exact 200 ms cadence;
- exact stop-to-failsafe time boundary;
- compact `ERROR_EVENT` from the new TX firmware;
- unplugged-USB and injected serial-I/O recovery/reconnect;
- TX-to-RX RF, RX status LED, 25 kHz PWM, Tach/RPM or thermal behavior.
