# 2026-09-11 RX tach diagnostic candidate — not deployed

User confirms physical fan rotates despite RPM0 and requests raw pulse
diagnostics. This turn only edits/builds/tests code; no API/serial/device access,
no mode/power setting, no reset, no OTA, no commit/push. Current fault root cause
is not established. Documentation: ADR-027, PROTOCOL.md, docs/TACH_DIAGNOSTICS.md.

## Implementation

Tach platform records saturating raw ISR edges (including full-queue drops),
accepted estimator updates and filter rejections, plus last raw edge age.
OverflowDrops also exists locally. Warmup is not rejection; rejection counts
mix per-edge and failed-window decisions, not a one-to-one partition.
The estimator and control semantics are unchanged. Queries never drain queue
or reset counters. Once/sec main-loop diagnostic bookkeeping maintains age
even without Control; this closes an independently found multiwrap-age bug.

Separate RF0x42/43, Host0x16/94 fixed30-byte protocol and PC JSON reader.
TX queues at most250ms, receives at most20ms, one explicit exchange/sec, no
retry. Ordinary Host commands still parsed; diagnostics never refresh normal
telemetry or control watchdog. RX opt-out of fullspeed applies only to valid
diagnostic replies; original maintenance behavior defaults unchanged.
PC validates arguments before opening, DTR/RTSfalse before UART open, supports
explicit socket passthrough, does not enable C6 passthrough or send heartbeat.

## Validation

- Native before/after PASS2902.
- Tach20 scenarios normal+ASan/UBSan PASS, including562 diagnostic checks,
  no-sample diagnostics-only multiwrap quiet age and preserved pending queue.
- Runtime pin/settings tests PASS; UART tach disabled explicitly.
- New C++ diagnostic strict codec/matching/arbitration/timeouts/watchdog PASS,
  including sanitizer. PC6 tests PASS, including reset-line ordering and preopen
  argument guards. Independent final review no remaining blockers.
- Full make verify exit0: build/20260911-tach-diagnostics-verify-final.log.
  First run reached manifest check then failed because final interface comment
  landed after manifest; regenerated and reran full suite successfully.
- Four real clean builds exit0, PlatformIO6.1.19/ESP8266Core3.1.2/RadioLib7.7.1,
  existing local cache /Users/at/.platformio retained. No toolchain download needed.
- Build input manifestSHA66ab29be7dc7597ae54e3352dd09ca329355a15cecb80025470cda8eb2463e2e.
  Final documentation changes are later than this compiled-source snapshot.

Artifacts/logs: firmware/dist/20260911-tach-diagnostics. Independent
`shasum -a256 -c SHA256SUMS` verified all binaries, logs and BUILD_INFO.

| Target | Bytes | SHA256 |
| --- | ---: | --- |
| TX | 316672 | 69c6b6ff6777e02a5248956a2c3db9e081b07956d13fdb6561169e3c4c5e2ed2 |
| RX1 | 311664 | b515e378d39df71710c4622eee1a99e48503941a2000f40a3541d472ff87cc2e |
| RX8 | 311664 | 5c020c8f12ff71db39acd7ca6bdabae4363c365b5d4dffbf24bdfb6889c16cf7 |
| RX2 UART | 311664 | 115dbd2b9aaefd8d709ea2c95099ea08db74701004a536e27a9c1ebd93ad68b6 |

No instrument/electrical/RF/physical RPM accuracy/long-run validation. Both TX
and RX need authorized upgrade to read new counters; cannot retroactively read
unrecorded old-image pulses. C6 unchanged; passthrough can carry the new query
but its fullspeed/control interruption must be explicit when later authorized.
