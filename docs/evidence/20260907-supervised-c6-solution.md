# 2026-09-07 C6 supervised diagnostic candidate

## Scope and delivery status

- Owner: C6 task `01a06d61-1664-7f03-a624-9b088aee4ed7`; supervisory task `01a07c08-b2c1-7cb1-b6f8-0ee17c3545d9`.
- Changed only C6 component/tests under `/path/to/esphome/config/devices/fanlinkc6/` and this explicitly requested evidence file. No TX/RX source edits, OTA, hardware restart, duty command or HA write in this supervised round.
- Final candidate: **20260907T134042Z-5374**. Configuration, merged API/Wi-Fi gate, compile, SMB readback, native/sanitizer tests and wire compatibility pass. Initially unflashed; **user-authorized OTA completed 2026-09-08 around 01:00 CST**, see follow-up below. Physical control/telemetry recovery is still not accepted.
- Superseded intermediate: `20260907T133203Z-4349` used class-only limiting and did not guarantee per-node fairness. Do not deploy it as the final supervised solution. Existing binaries were preserved, not overwritten/deleted.

## Actually flashed versus candidate

### 2026-09-08 authorized OTA follow-up

User explicitly requested `ota`. Reverified the exact final image: 948784 bytes, SHA256 `5c6c46910db32c29528f3a9b6284f311b9b892d31bc24990e4daa9a329e5f9d5`; independent esptool verification confirmed valid ESP32-C6 image/checksum. Before upload, native API expected name and MAC plus local ARP all matched `fanlinkc6 / [device MAC redacted] / <C6_LAN_IP>`.

ESPHome OTA v2 on port3232 returned `(0, <C6_LAN_IP>)`, `OTA successful`, upload 4.98 s. Post-reboot API compilation time changed from `2026-09-07 07:53:00 +0000` to **`2026-09-07 13:40:44 +0000`**, with the same name/MAC. This build is now flashed, not merely a candidate. No TX/RX flash, manual duty command, HA write or additional device restart was performed.

Read-only observation ran 01:01:06–01:02:17 CST (70 s plus final dump). At 01:01:43, `Boot seems successful; resetting boot loop counter`; API remained available and final identity unchanged. Initial→final C6 counters: UART success112→451, failures0; STATUS timeouts0; valid frames255→1017; CRC0; format0; raw recovery discard0. Diagnostic received/emitted/suppressed69/69/0→277/277/0, pending0; final RFN/RF/RFT each92/92/0 and RFR1/1/0. Thus new live logger paths including RFT and RFR are verified, not only native tests.

**RF symptom persists:** seven CH01 offline→online pairs were logged in this observation (timestamps to one-second resolution): 01:01:18→22, 01:01:33→35, 01:01:44→47, 01:01:55→55, 01:02:01→02, 01:02:08→10, 01:02:15→16. Example first outage: RFN accepted replies plateaued at o9032 while p13057→13082 and age831→4867 ms; RFT d9043/l11/h1234 plateaued during this gap. At 01:01:22 RFR attempts4/successes4/failures0 was visible, followed by new accepted replies and reconnection. This is a temporal association, not proof of the root cause.

At observation end CH01 Connected=True, requested/applied PWM both100%, missed0. API RPM27598 and RSSI NaN were reported; RPM is unverified telemetry, not an independently measured physical speed. No control transition/low-PWM acceptance was attempted. OTA and C6 boot acceptance pass; RF stability and physical control acceptance do not pass on the basis of this test.

### Pre-OTA historical identity (2026-09-07)

At approximately 21:30 CST this round, a read-only native API connection with `expected_name=fanlinkc6` returned:

```text
IP: <C6_LAN_IP>
name: fanlinkc6
MAC: [device MAC redacted]
ESPHome: 2026.8.0
compilation_time: 2026-09-07 07:53:00 +0000
```

This matches the image successfully OTA'd earlier in this task, not the new candidate:

```text
/Volumes/ZSSD/Target/ESPHome/20260907T075258Z-3958/fanlinkc6__fanlinkc6_build_firmware.ota.bin
947488 bytes
SHA256 1a45b3459009b905c723c0059de47aeba83f419a5182a848522d8c05df8a02d4
```

Do not infer running TX/RX versions from C6 compilation time. Their build/flash identities belong to the TX/RX owner and must be recorded separately before joint hardware acceptance.

## Evidence already separating C6 from RF failures

These are bounded observations from this conversation, not claims that every later failure has the same cause.

1. 19:49:55–19:50:17 CST: C6 UART successful commands increased 6491→6588; TX failures/status timeouts/CRC/format were zero. CH01 disconnected at 19:49:56.114 and reconnected at 56.404 (290 ms), with LED state following the connection change. Missed polls 5→0. Requested 39%, reported applied 38.8235%, RPM about 1508–1562. No evidence of a C6 transport recovery or API disconnect in that interval.
2. 20:58:28.090–20:58:54.359 CST: RFN Node01 `p1813→1975`, `o121→121`, `t1692→1854`, `a165162→191434`, `m255`, `S136`, `x0`, `c8`, `i0`, `v0`. Thus 162 completed TX polls, zero additional accepted replies and 162 timeout outcomes in this 26.269 s window. C6 UART success 4116→4236, failures zero; historical status timeouts/CRC/format 628/200/536 did not increase. They must not be described as new errors in this window.
3. 21:14:15.844–21:14:38.037 CST, after “现在控制也无效了”: a new counter epoch had UART success 383→479, failures/status timeouts/CRC/format zero. Node01 disconnected, missed 255, RPM/applied/RSSI unavailable. Global RF `t7886→9981`, `o0`. This global timeout count includes empty node slots and is not Node01 packet loss. A displayed requested 100% is not physical application confirmation. The epoch change is not evidence of a reboot performed by this task.
4. User reproduction supplied through coordination: unplug/replug RX restores operation; at 100% PWM telemetry exists but occasionally drops; intermediate PWM can physically change speed while telemetry disappears. This is not proof that every “control ineffective” episode has a functioning command path.

Read-only code checks exclude these specific proposed C6 mechanisms:

- One-second blue LED enforcement calls `turn_on`, not an off/on cycle. Repeated ESPHome power requests do not retrigger the GPIO19 power-enable delay. GPIO20 is data; GPIO19 is power.
- Node-only offline changes telemetry/LED, not the requested duties. UART/status safety failure is the separate full-speed reset path.
- Online remains: transport READY; STATUS age ≤750 ms; node age not 0xFF; node age×100 + snapshot age ≤1000 ms; stale bit 0x80 clear. No threshold was relaxed to hide RF dropouts.
- The actual prior 0x81 handler had one shared 1000 ms log gate. Missing RF/RFR/RFN output was therefore not evidence that TX never produced those messages.

## Final diagnostic change

Production files:

```text
/path/to/esphome/config/devices/fanlinkc6/components/fanlink/fanlink_diagnostics.h
/path/to/esphome/config/devices/fanlinkc6/components/fanlink/fanlink.h
/path/to/esphome/config/devices/fanlinkc6/components/fanlink/fanlink.cpp
```

- 35 fixed latest-value slots: RFN n1..n16, RFT n1..n16, RF, RFR, OTHER. Exact complete prefixes and decimal `n1 ` through `n16 `; malformed/truncated/zero-padded/out-of-range node text goes to OTHER rather than another node's slot.
- Class round-robin, then node round-robin. Every pending class gets an opportunity within five emissions; every pending node within sixteen opportunities for its class. At an adequately serviced loop this is at most about 16 s under all-class/all-node continuous saturation. This is a scheduling-opportunity bound, not a hard real-time guarantee if the system itself blocks.
- One log sink call at most every 200 ms globally: ≤5 calls per half-open rolling second, no burst quota per node. Fixed cache is 5752 bytes in the host test ABI; target ABI may differ. Payload remains ≤128 bytes, text storage 129 including terminator. No heap queue or unlimited backlog.
- Arrival replaces only that slot's pending old value; the newest value survives. The consumed record is not replayed. This is explicitly a latest-value diagnostic cache, not a lossless event history.
- `received` counts valid parsed 0x81 events admitted to the cache; `emitted` counts calls to the logging sink; `suppressed` counts replaced pending records; `pending` is the number of occupied slots. Class and aggregate counters are printed by dump_config; each emitted record prints its class counters.
- Counters saturate at uint32 max, pending remains exact. Before saturation, `received = emitted + suppressed + pending`; do not assume this equality after saturation. Logger/API transport can independently drop an emitted line; emitted is not a delivery acknowledgement.
- Each log prints `rx_ms` and `age_ms`. RFN `a` and RFT offsets describe the record at reception, not the later print instant. For an approximate current RFN age add cache age, without confusing it with an independently confirmed fresh reply.
- Raw stale-RX recovery drain bytes are now visible in dump_config. They are not parsed as frames, so absence from received counts does not establish that TX never sent an event during drain, UART corruption or other upstream loss.
- `process_error_event_` only enqueues. Emission is at loop end, after STATUS expiry, command, telemetry and LED work. No STATUS freshness, duty, node-online, LED or safety thresholds were changed.
- Retained limitations: a log call has bounded count/size but is not a measured hard CPU deadline; the earlier generic UART `write_array` API can block before flush and cannot confirm partial writes. This patch does not claim to fix those API limitations.

Source SHA256 used for final build and verified against generated build sources:

```text
fanlink_diagnostics.h 8a33836dfb09fffb1834f65664ddaff1617422edb40f8662413b2fd80f4a6584
fanlink.cpp 9f22f64c37402aad6784ed976d6b4cf7ff7de80d34d8873a4a13d2a502efcc1f
fanlink.h 277bacea915e7141b34d442aba60fa54352a8d6be03cc933ab76438100ef786b
fanlink_protocol.h 4de80abe35d75a2ebd0cc958d77265100f7099ab4e1b0c4703c2842485cbfb3a
fanlink_transport.h 1384a4a9b3f7f9a0282b24367f92d779bf183645118c69c3e93f50bd053c3c61
```

## Reproduce tests

Run from `/path/to/esphome`:

```bash
bash config/devices/fanlinkc6/tests/run_native_tests.sh
FANLINK_SANITIZE=1 bash config/devices/fanlinkc6/tests/run_native_tests.sh
bash config/devices/fanlinkc6/tests/run_diagnostic_wiring_checks.sh
bash config/devices/fanlinkc6/tests/run_tx_diagnostics_compat_tests.sh /path/to/FanLink/firmware
FANLINK_SANITIZE=1 bash config/devices/fanlinkc6/tests/run_tx_diagnostics_compat_tests.sh /path/to/FanLink/firmware
bash /path/to/FanLink/firmware/tools/run_c6_wire_compat_tests.sh /path/to/esphome/config/devices/fanlinkc6/components/fanlink
```

Results, normal and ASan/UBSan where applicable:

| Check | Result |
| --- | --- |
| Actual diagnostic cache | 320694 PASS |
| Diagnostic + actual transport safety cores | 29574 PASS |
| Existing C6 protocol | 38 PASS |
| Existing C6 transport safety | 297 PASS |
| Production source wiring contract | PASS |
| Actual TX RFT formatter / bidirectional UART / C6 cache | 2461 PASS |
| Existing C6/TX wire contract | 1277 PASS |

Cache coverage includes four rotations of exact adversarial `n1@1,n2@1000,n3@2001,n4@3000...` jitter, every node observed four times; 32 s saturated 35-slot pressure with all nodes seen at least twice and RF/RFR/OTHER served; latest replacement, receive timestamp/age, no replay, millis wrap, empty/128/129-byte and malformed prefix boundaries. Full saturation runs the actual cache enqueue/emit template with uint8 counters for 600 emissions and replacements, not just the increment helper; firmware uses uint32 counters.

Production-equivalent isolation is checked in two parts: source contract asserts actual 0x81 dispatch→enqueue and loop placement of the logger lambda; real parser/cache/SafetyState tests continuously feed diagnostics across the exact 750/751 ms boundary, proving no STATUS generation update, no duty write and full-speed expiry. This is not a claim of real API log-delivery testing on hardware.

## Independent TX/RX snapshot compatibility review

At review snapshot, new `receiveForUs` is connected end-to-end; `firmware/src/app/tx_controller.cpp` around lines 240–264 uses remaining microseconds, rejects invalid/late observation time, and still strictly validates network/node/sequence/application CRC. The RadioLib port captures software RxDone observation before SPI processing. Native TX suite 693 checks passed at this snapshot, including 4999/5000 us, remaining-window, wrap, late-processing acceptance and wrong-identity rejection.

Actual formatter, not copied grammar, was used by the C6 compatibility test:

```text
RFT n%u d%lu l%lu h%lu a%lu z%lu r%u
firmware/include/fanlink/tx_diagnostic_text.hpp SHA256 eff3f3c398525a082642440314a8e5c971d0ca17a9f26e676418fd2edca54fd2
firmware/src/app/tx_controller.cpp SHA256 0df4b82eca5e5a2c43f1eab2e00ea446390cef809823e9b1b0a0c673c1b874e4
```

- 128 combinations n1..n16 × r0..r7 with all uint32 fields saturated: maximum 70 bytes. Actual TX serial encoder→C6 parser→C6 encoder→TX parser round-trip and production cache pass; no mocked protocol or isValidNodeId implementation.
- `d`: Ok returns carrying an observation timestamp, before identity checks, not matched replies. `l`: time-window/invalid-timestamp rejection count, not solely RF late arrival. `h`: strictly matched replies observed in-window but processed at/after deadline and still accepted.
- `a/z`: latest Ok observation/completion offsets relative to that poll's receive window; no observation gives a=UINT32_MAX. `r`: retained last rejection reason, not cleared by a success: 0 none, 1 envelope/application CRC, 2 network, 3 node, 4 sequence, 5 late observed, 6 invalid timestamp, 7 legacy late return.
- STATUS 0x80 remains 102 bytes, METRICS 0x82 32 bytes, ERROR_EVENT 0x81 ≤128; air packet remains 30 bytes. C6 does not infer a binary protocol extension from text.
- RX ownPolls/replyAttempts/lateReplyCalls/expiredRepliesSkipped remain local diagnostics; they are not available through the unchanged air/UART path. Do not list them as already collectable from C6.
- Software observation is not a hardware DIO-edge timestamp. PWM/TIMER1 NMI, SPI delay, power/noise remain hardware hypotheses. This review does not prove their causal share or accept the TX/RX physical fix.

## Final build and artifact proof

MS-A2 was already online; CT9000 already running, hostname esphome-builder, unprivileged, 30 vCPU, 16 GiB RAM, 4 GiB swap, 128 GiB vmdata, no failed systemd units. No VM301 or host reboot/start was used. Runtime scripts matched canonical SHA and bash syntax checks. uv 0.12.8, Python 3.13.5, ESPHome 2026.8.0 from locked `/opt/esphome-builder`.

Only named invocation:

```bash
pct exec 9000 -- /usr/local/sbin/esphome-lxc-build fanlinkc6.yaml
```

Validation PASS, API/Wi-Fi gate PASS without exposing secret values, compile PASS, two firmware images. SMB probe plus size/SHA readback of all four final files passed. Delivery:

```text
smb://<ARTIFACT_SERVER>/<SHARE>/Target/ESPHome/20260907T134042Z-5374
/Volumes/ZSSD/Target/ESPHome/20260907T134042Z-5374
```

| File | Bytes | SHA256 |
| --- | ---: | --- |
| fanlinkc6__fanlinkc6_build_firmware.ota.bin | 948784 | 5c6c46910db32c29528f3a9b6284f311b9b892d31bc24990e4daa9a329e5f9d5 |
| fanlinkc6__fanlinkc6_build_firmware.factory.bin | 1014320 | 5a77ba080d05ccaf9e9122f3833e31f78aa8cd812579600ec7c990f4f3949265 |
| BUILD-RESULTS.tsv | 77 | 2108014d2adb3cc623946ee6adc62ff6e1906887fecab445245410ff482f6c0d |
| SHA256SUMS | 308 | fcee64bf4b74b52e37fd5b466545f61b0a14aacb1dbe0a1bc39adaa1e1da06c8 |

esptool 5.3.1 image-info: ESP32-C6 chip ID13, 4MB, fanlinkc6/2026.8.0, ESP-IDF5.5.5, checksum and validation hash valid. Local mounted-share hashes independently match remote readback.

Final build retained caches (bytes before→after): uv 179261812→179261812; PlatformIO 9240615287→9240615287; ESPHome build 5915555684→5915649353. Paths `/var/cache/esphome-builder/{uv,platformio,xdg,ccache}` and `/srv/esphome/config/.esphome` remain. Task staging directories and transient per-run delivery/log copies were purged; no persistent compile-cache cleanup. CT9000-only timer was verified for **2026-09-07 15:40:54 UTC / 23:40:54 CST**. Host remains online.

## Joint hardware acceptance — pending, requires separate authorization

Before measurements: verify C6 identity/MAC and flashed build, TX/RX version/hash, RX01 physical target, antenna/distance and supply conditions. Record DUT power events and counter epochs; do not compare deltas across reset/saturation. Capture wall-clock API logs and states continuously, dump_config before/after. No new duty, flash or reboot was performed for this document.

Common fields: physical fan response, requested PWM versus reported applied PWM, RPM, online transitions, missed polls, both RSSIs; STATUS uptime/failsafe/header and C6 TX failures/status timeouts/CRC/format; RFN p/o/m/a/S/x/t/c/i/v; RFT d/l/h/a/z/r; RF/RFR; diagnostic class received/emitted/suppressed/pending and rx_ms/age_ms. RFN p is completed TX polls, not proven RX reception. Loss ratios require same-node unsaturated deltas and explicit window length; global RF timeout totals and missed255 cannot supply that denominator.

### Case A: intermediate PWM changes speed but telemetry disappears

After authorized joint candidate deployment, repeat a fixed safe intermediate setting from the user's reproduction, then 100%, at least three cycles without RX power cycling. Suggested minimum hold is two minutes per setting, with a five-minute stable intermediate hold. Keep load, antennas, distance and supply unchanged for this comparison.

Acceptance requires actual speed following each command AND continuing valid telemetry through the hold and subsequent transitions, without reboot/power-cycle recovery. RFN o must keep increasing while p increases; no sustained o-flat/age-growing window, no unavailable RPM/applied state or stale/online drop attributable to this symptom. RFT h growth can demonstrate that the deadline patch accepted late-processed but timely observed replies; it alone does not establish full recovery. If physical speed changes but o stops/RPM goes unavailable, control-only success is not acceptance.

### Case B: 100% PWM has telemetry but occasionally drops

Separately hold 100% for at least ten minutes after stabilization under the same physical conditions. Acceptance requires no unexpected Node01 online→offline transitions, no unavailable telemetry intervals, no stale flag/combined-age threshold crossing, and no C6 transport failures/timeouts or TX/RX restarts. Report completed polls and accepted-reply deltas as well as timing/CRC/I/O changes; isolated missed polls without freshness loss are recorded, not silently equated to a disconnect.

If either case fails, preserve the aligned diagnostics and reject that case. Do not relax C6 online/safety thresholds, set unavailable telemetry to fabricated values, or claim all radio faults are fixed merely because compile/OTA succeeds. Compare late timing counters first; if absent/flat, distinguish RX-local observability gaps from power/RF/SPI/PWM hypotheses using separately authorized hardware measurements.
