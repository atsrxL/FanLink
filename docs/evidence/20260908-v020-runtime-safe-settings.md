# 0.2.0 first runtime evidence: untrusted settings / safe mode

## Authorized settings recovery and repeat capture

User confirmed the proposed TX+13/RX2 UART/-6 recovery. Live SSH identity,
dual adapter mapping TX=ACM0/RX2=ACM1 and free UARTs were rechecked. The guarded
TX UART sent only Query and the three authorized settings, in order:
TX power13, RX2 modeUART, RX2 power-6. Each received Ok/pending ACK, followed
by a6s wait and matched Query with flags0 and the expected selected values.
RX2 intermediate state was UART/-10; final TX Lite13 and RX2 Nano UART/-6.
Log: `build/20260908-v020-restore-settings.log` (exit0). No firmware upload,
BOOT command, manual reset, duty/heartbeat or C6 change was performed. Setting
application intentionally uses firmware-managed automatic restart. Successful
queries verify persisted selection after restart, not calibrated RF output.

Repeat capture `build/20260908-v020-dual-runtime-restored.jsonl` completed
60.028s, TX45170B/RX8929B,239 status frames all fresh at2500ms threshold,
parser rejects0, no fresh-to-stale transition, max sampled age1700ms, max one
miss. TX uptime30→90;39 RXDIAG records all L1, RX uptime16885→73996ms monotonic.
After excluding initial SSH backlog, TX RFN at3.440→59.441s shows56 polls,
45 accepted(80.36%),11 timeouts,11 CRC-class errors; no TX I/O/application
reject/transmit failure increments. RFC separately reports10 CRC rather than
sync errors over its shorter3.440→57.441s window. RX own-poll/local-transmit
counters both+56 over4.126→59.738s, failures/skips0; RX receive CRC+3, recovery
and malformed/stale/expired control counters unchanged. These independently
sampled counter windows are not exact packet-by-packet correlation.

Compared with previous19/59 acceptance, this short repeat is materially better
but still lossy; it does not prove RF loss fixed, isolate a physical cause, or
validate long-run stability. RX UART mode deliberately has no fan/Tach, so RPM0
does not validate tachometer behavior. Host failsafe remained expected because
no heartbeat was sent. Both UARTs confirmed RELEASED after capture. No Git push.

User confirmed after flashing: TX=ACM0, RX2=ACM1, attached again to dual1a86:55de
adapterBD5AA1ABCD. Both free, no bridge. First simultaneous SSH startup hit one
authentication failure; standalone login succeeded, bounded retry staggered
connections1s and captured60.026s. No duty/heartbeat/config mutation/flash/reset.
Initial failed log preserved: build/20260908-v020-dual-runtime.jsonl.
Successful capture: build/20260908-v020-dual-runtime-retry.jsonl and -tx/-rx.raw.

Independent log analysis:

- TX46849 bytes, RX0 bytes;239 STATUS frames,151 fresh/88 stale, parser rejects0.
- RFN node2 p392→451:59 polls; o173→192:19 accepted(32.20%);40 timeouts;
  CRC-class events202→239:+37. TX transmit/I/O/application-reject counters+0.
- Excluding initial SSH backlog:57 polls,18 replies,39 timeouts,36 CRC events.
- Maximum node2 telemetry age7800ms, maximum7 consecutive misses;12 observed
  fresh→stale transitions. Snapshot counts are not independent poll-success rates.
- RF sequence delta448 over56.008s≈125.018ms/slot; RFN median cycle1.000s.
  TX uptime391→450 monotonic. Only even nextpoll IDs in250ms snapshots are sampling
  aliasing, not evidence that odd nodes are skipped.
- Valid node2 metrics uplink-44..-43dBm/downlink-52..-51dBm. Successful RX software
  observation2894..2914us and return4081..4138us, no RFT late/header reject increments.
- No RX diagnostic bytes, so RX poll/reply/reset/link counters unavailable. No
  RFC/RFE frames, so missing classifications cannot be treated as zero events.
- Hostfailsafe=true throughout is expected without sent heartbeat. UART mode
  RPM0/duty255 would not prove physical fan speed.

After capture, performed only protocol Query(node0,node2), via same guarded TX
UART, full Host/inner CRC and transaction echo matching. Both replied Ok:

| Target | Profile | Selected mode | Power chip dBm | Flags | Capabilities |
|---|---|---:|---:|---:|---:|
|TX0|Lite1|255(not applicable)|-18|2|103|
|RX2|Nano2|254(Safe)|-10|2|120|

Both advertise8 receivers,125ms,5120B/s. Query log:
build/20260908-v020-runtime-query.log. This proves both applications answer the
new protocol and exposes the boot safety fallback. flags2 means the persistent
record was not trusted (validation/read failure); raw storage was not read, so
old non-FanLink data versus storage failure is not yet isolated. No settings were
changed. RX Safe intentionally does not enable UART or PWM, consistent with
zero UART data; selected TX power is not intended Lite+13 default.

Recommended separately authorized recovery: setTX+13; setRX2 UART, then RX2-6,
respecting reboot grace and querying each application after restart. Do not
claim this alone fixes RF loss. C6 unaffected. Both ports released after capture
and query. Actual PHY power measurement, GPIO/OTA acceptance and long-run RF
stability remain unverified.
