# 2026-09-08 DIY safety, recovery and connection-tolerance assessment

Scope: inspect current code and existing captures; implement Host reporting
tolerance, not a PHY fix or relaxed safety watchdog. No live device access,
flash, duty/pin/power change or Git commit/push in this task. C6 production
adaptation assigned to existing task01a06d61-1664-7f03-a624-9b088aee4ed7,
with explicit code/tests-only and no OTA/serial/device actions.

## Conditional recommendation

Acceptable for ordinary noncritical DIY open-loop fan control only when the
cooled equipment retains independent thermal throttling/shutdown, fan power
is reliable, and physical fault injection has verified default-full behavior.
Not accepted as the sole unattended critical cooling protection. Existing
RX2 UART diagnostics cannot verify actual fan output, tach or thermal safety.
Latest five-minute run has267/296 matched replies,29 overlapping CRC/timeouts;
this is usable evidence for a slow-control reporting grace, not good RF quality
or proof of reliable operation over months. Burst errors are not independent.

## Existing protections and recovery

| Failure | Existing response | Boundary |
|---|---|---|
| One lost downlink | Next125ms slot broadcasts full state again | Subject to next frame actually arriving |
| Return-path loss only | Continue complete control broadcasts | RPM/applied state becomes unknown; not proof fan turns |
| RX no new valid control500ms | Local full-speed request | Main-loop timing/hardware release still require physical check |
| Host no valid command500ms | TX FORCE_FULL_SPEED/all255 | Next normal slot adds up to about125ms, not exact physical500ms |
| RX no new control2s / TX no reply anywhere5s | Radio reinitialization | RX first holds full; single missing node does not reset whole network |
| Three consecutive radio hard errors | Radio reinitialization | Retry failure backoff1/2/4/8s, not guaranteed recovery from broken hardware |
| Corrupt/wrong/old packet | Reject; do not renew valid-control age | CRC/networkID are not authentication |
| TX reboot behind RX sequence | After watchdog, require two advancing candidates | Duplicate old low-duty packets cannot release protection |
| Tach zero/stall | Report TACH_TIMEOUT/STALL | No automatic full-duty motor restart or blockage removal |
| OTA transfer interruption | Bounded timeout/abort, validate before commit | Final boot copy lacks A/B rollback; power failure may need wired repair |

Primary source: firmware/src/app/rx_controller.cpp (begin, watchdog, sequence,
stall, recovery), firmware/src/app/tx_controller.cpp (host freshness, full
state, reply checks, per-node misses, global recovery), SAFETY.md. Existing
native/runtime recovery and actualRadioLib simulatedI/O tests cover these
code paths; they are not physical fault-injection results. No software can
restore missing fan power, shorted PWM transistor or a mechanically jammed fan.

## Selected Host policy

- Startup/never seen/inactive node: offline.
- Once online: retain presence while consecutive own-node misses<5 AND age<6s.
- Either fifth missed poll or age>=6s: offline; no requirement to meet both.
- Host transport stale/disconnected/untrusted: offline immediately when its
  existing safety deadline is reached, without the6s RF grace.
- Recover only on fresh non-stale miss-zero node evidence; preserve elapsed
  age when receiving repeated STATUS, never count a STATUS as a new RF reply.
- age>=2.5s or TX stale bit: RPM/applied duty/RSSI not current even if presence
  is still true. Keep counters, age, safety/stall/config flags visible.
- Existing RX/Host500ms watchdog and current5ms receive window unchanged.

Each node is polled once per second although every control slot carries its
complete duty. Thus five misses normally mean about five seconds, NOT625ms.
6s is a backstop for pauses where no poll completes and the miss count freezes.
TX already provides the necessary age/miss fields; no TX/RX reflash is needed
for this Host-only policy. Legacy C6 source had a1000ms presence limit, too
little margin for1s own-node polling; its replacement is a separately tested
code candidate, not deployed. STATUS age rounds down in100ms units, so6s is
a reported-age threshold with quantization/transport latency, not an exact
physical timeout.

## Read-only replay of historical STATUS records

Exclude elapsed<=3s; use TX STATUS node2 only. Old stale counts use bit7.
New loss predicate is miss>=5 OR age_100ms>=60 for this already-seen node.
This is a counterfactual record replay, not new firmware/hardware observation.

| Local build/ capture file | Samples | Old stale samples/runs | New offline samples/runs | Max miss | Max age |
|---|---:|---:|---:|---:|---:|
| 20260908-lite-max13-30min.jsonl (earlier firmware baseline) |7187|54/19|0/0|4|4800ms|
| 20260908-lite-longtest-postfix.jsonl |1188|4/2|0/0|2|2800ms|
| 20260908-radio-trace-postcheck-retry1.jsonl (latest diagnostic) |1188|2/1|0/0|2|2800ms|

All captures report parser rejects0 and no sampled uptime decrease. Sampled
age is quantized100ms; this does not reveal physical state between records.
Telemetry would still be stale in all former stale windows; new reporting
does not improve RF success. No independence-based lifetime estimate is made.

## Verification and deployment

Baseline make verify PASS: build/20260908-diy-safety-baseline.log.
PC implementation:33/33 panel tests and executable JavaScript rendering
regression PASS; independent safety review PASS. New code separates online
latching from telemetry freshness and hides stale RPM/applied duty/RSSI.
C6 final miss-zero initial/recovery gate independently rerun by this task:
360144 native checks (including3454 liveness) and ASan/UBSan PASS, plus
liveness/passthrough/diagnostic production wiring checks PASS. Candidate
retains750ms transport trust,200ms controls and full-speed recovery; inactive
9..16 remain offline/NAN while the16-node wire stays unchanged. Post-transport
loss, the first trusted snapshot only establishes an age/miss comparison
baseline; recovery additionally needs an age decrease or misses clearing.
Wire has no RX reply sequence, so same-phase sampling can delay recovery.
Final FanLink make verify PASS: build/20260908-diy-safety-verify-final.log.
JSON syntax,git diff --check and refreshed source manifest check PASS.
No native check is physical fan acceptance. C6 later requested protocol
confirmation for a separately authorized eight-entity/HA cleanup/deployment;
replied source and latest flashed record still use16/102/32-byte Host payloads.
That subsequent C6 work is not included in this tested candidate or deployment
claim, and no TX/RX device action was performed for that confirmation.
TX/RX source and deployed radio-trace images are unchanged by this task, so
no new ESP8285 build or flash is warranted. C6 code adaptation is separate;
no ESPHome image or on-device acceptance is claimed by this report.
