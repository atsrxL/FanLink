# 2026-09-07 — Intermittent telemetry optimization, 0.1.5-dev

> Subsequent, separately authorized deployment: both TX and RX Node01 direct ROM
> writes of this 0.1.5-dev batch succeeded. TX: 295,936 padded bytes in 33.1 seconds;
> RX01: 290,816 padded bytes in 31.6 seconds; both exit 0. No pre-read/readback or
> run/reset. The user then reported working control but no telemetry return;
> cause investigation is pending, not RF acceptance. See `20260907-tx-ssh-flash.md`
> and `20260907-rx-ssh-flash.md`. The code-stage no-flash statements below describe
> the earlier optimization turn, not these later deployment authorizations.

## Scope and live observation

User requested code optimization following the C6 task's read-only capture.
This turn changes/tests/builds local code only: no flash, OTA, reboot, SSH/UART
connection, fan commands, C6 changes or Git publication.

The C6 task reported a 19:49:55–19:50:17 CST window on September 7: Node01
disconnected at 19:49:56.114 and connected at .404; API missed reached 5,
while UART TX failures, flush errors, STATUS timeouts, serial CRC and format
errors stayed zero. The C6's OTA was separately authorized in its own task;
its API build was 2026-09-07 07:53:00 UTC. These are cross-task observations,
not a new raw capture by this task. The 290 ms log transition is not the RF
gap's duration. Telemetry loss is not proof of downlink control loss.

Old global RF totals include all 16 polls. With 15 absent nodes their timeouts
cannot be interpreted as Node01 packet error rate. The original RF power,
frequency, node IDs, sync word, GPIO, wire frames and safety limits are retained.

## Changes and evidence boundaries

1. **Receive blind interval:** actual RadioLib 7.7.1 blocking receive ended in
   standby after each software/hardware wait. RX therefore could not receive
   during the next controller/LED/yield work. The new adapter retains a single
   `SetRx(0)` receive across software timeouts, then consumes FIFO and rearms
   after a packet. Normal timeout has no standby/SetRx/IRQ-clear operation.
   It is not uninterrupted RX during FIFO handling, nor proof of field cause.
2. **Safe checked reads:** validate RxDone, exact 30-byte length, offset and
   packet status with checked SPI calls. Do not reread unchecked metadata in
   `readData`; reject CRC from both IRQ and packet status. Errors/rearm failure
   return I/O failure and release RF pins. Old packets are abandoned by TX/reset.
3. **PWM overhead:** avoid repeated analogWrite for unchanged duty; the first
   and repeated initialization still really establishes full speed. A controller
   timing model demonstrated how injected slow PWM work can consume the reply
   window, but its 3.6 ms delay was synthetic, not measured on the physical RX.
4. **Evidence:** bounded `RFN` node-specific counters and instantaneous age/status;
   prioritize STATUS/METRICS. No C6 threshold/LED change. The C6 task reviewed the
   new ASCII text as compatible; its existing global 1-second log limiter can
   suppress RFN, so API logs remain sampled rather than complete raw UART evidence.

ADR-015/016 and PROTOCOL describe the details. Single RX avoids the continuous
mode congested-traffic BUSY erratum in the
[Semtech SX1280/81 data sheet](https://media.digikey.com/pdf/Data%20Sheets/Semtech%20PDFs/SX1280-81_Rev3.2_Mar2020.pdf)
sections 11.6.5, 14.3.3, 16.1/16.3. This is the manufacturer's document hosted
by a distributor. No evidence says that erratum caused the old finite-RX issue.

## Verification

- Baseline native: 507 checks. New native: 627 checks.
- Real-driver before-fix regression: fails the retained-RX assertion on the old
  adapter; `build/link-timing-20260907-driver-before.log` preserves the failure.
- New real-driver simulation: retained packets between waits, no repeated RX
  setup on timeout, CRC then valid packet, single-shot/DIO mask behavior,
  transmit/reset invalidation, microsecond wrap, rearm/BUSY failures, invalid
  lengths/IRQ, metadata/FIFO/clear SPI failures and packet-status CRC checks.
  It models SPI/GPIO and timer events, not measured RF latency or packet error rate.
- PWM platform source: 2657 checks (both polarities, all 256 duties, repeated
  values, initialization/reinitialization and invalid hardware configuration).
- Full `make verify` PASS: core 627, PWM 2657, PC 21 tests, CLI, syntax,
  source manifest, and 12-hour virtual runtime (4,320,000 slots, 10 micros wraps,
  65 sequence wraps, 216,000 full-state Host frames, 269,986 Node01 replies;
  injected radio faults/reinitialization and stale-control safety preserved).
- ASan/UBSan PASS 627 (macOS leak detection disabled); CMake/CTest PASS 1/1.
- Actual C6 protocol header cross-check PASS 1277. That private implementation
  was read for compatibility, not copied into this repository or modified.
- Clean local PlatformIO TX/RX01/RX16: PASS 3/3, full artifact/log SHA256SUMS
  readback PASS. The build-input manifest is preserved alongside BUILD_INFO;
  final handoff documentation was updated only after that snapshot.

Logs are retained under `build/link-timing-20260907-*.log`; generated binaries
and metadata are under `firmware/dist/20260907-link-timing/`, outside Git.
Exact sizes/hashes are in BUILD_INFO.json, per-image `.sha256` and SHA256SUMS.
The platform library metadata still reads 0.1.3-dev; 0.1.5-dev names this application
working-tree revision, not an added binary version field. Toolchain remains
PlatformIO 6.1.19 / espressif8266 4.2.1 / Arduino core 3.1.2 / RadioLib 7.7.1.

Reproduce locally (choose a new run ID; do not overwrite this batch):

```bash
PLATFORMIO_CORE_DIR=/tmp/fanlink-pio-core-v013-preflight \
  uv run --with 'platformio==6.1.19' python3 firmware/tools/build_firmware.py \
  --tx --rx 1 --rx 16 --run-id <new-unique-run-id>
```

## Acceptance / next action

New optimization is **not flashed or physically accepted**. Existing written
TX/RX01 artifacts remain the 0.1.4-dev batch `20260907-radio-recovery`.
After a new explicit deployment authorization, compare matched Node01 RFN
counter windows plus missed/age/status and C6 transitions, then run a sustained
test. Do not use empty-node aggregate timeouts or suppressed log lines as PER.
Any future BOOT flash must follow the user's direct-ROM-write/no-pre-read rule.
