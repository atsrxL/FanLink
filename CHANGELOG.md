# Changelog

## 2026-09-11 — OTA attempt instrumentation

- Optional PC uploader observer reports per-attempt RTT, ACK/BadOffset/timeout,
  offsets and cumulative retry counters; no retry, pacing or wire change.
- Observer exceptions cannot disrupt Commit accounting; progress cancellation
  retains pre-Commit Abort. OTA36 and TX12 tests pass.
- RX1-only bounded retry is separately recorded in deployment evidence.

## 2026-09-11 — Read-only RX tach diagnostics (ADR-027, not deployed)

- Add lifetime raw-edge, accepted-estimator and rejection counters plus raw-edge
  age; instrument without changing the RPM filter or clearing state on queries.
- Add separate fixed30B diagnostic RF types0x42/0x43 and Host v2 types0x16/0x94,
  bounded exclusive query transport and a PC reader. Control/RPM layout unchanged.
- No device restart, mode change, configuration write or firmware deployment.
  Candidate build/test results recorded in the new evidence and handoff entry.

## 2026-09-08 — Settings default fallback and C6 repair (ADR-026)

- Replace invalid-storage pin-disable/minimum-power behavior with validated
  compiled pin/power defaults; preserve valid saved selections and watchdogs.
- Keep invalid-record diagnostics and explicit persistence; no boot/Query writes.
- C6 distinguishes fallback from persisted settings and permits explicit repair,
  including same-mode saves and legacy Safe254 recovery. Wire layouts unchanged.
- Verification, build and deployment evidence recorded separately; source edits
  alone do not repair the currently installed RX image or saved configuration.

## 2026-09-08 — All-eight RX build coverage and coordinated upgrade

- Verify and clean-build TX, RX01..RX08 and RX2 UART (ten environments).
  Seven unreachable RX devices receive build artifacts only, not deployment.
- RX2 RF OTA receives Commit ACK and passes short telemetry/settings recovery
  checks; fresh status snapshots are not independent RF packet counts. No
  direct RX uptime-reset or Flash-readback verification is claimed.
- TX UART OTA receives Commit ACK and passes35s plus8s Host v2/STATUS54,
  uptime-restart and retained-settings recovery checks. The separate production
  C6 compact8/pinmode OTA completed; actual API and HA confirm eight enabled
  uart/pwm/pwmswap selects and eight settings-status sensors. C6-to-TX physical
  connection and end-to-end RX Query remain unverified; no mode was changed.
- Preserve RF/wire/watchdog policy; no RF CRC fix or physical fan-safety claim.
  Evidence and artifact hashes: `docs/evidence/20260908-all8-upgrade.md`.

## 2026-09-08 — Native compact8 Host v2 (ADR-025)

- Replace ADR-023's legacy Host envelope with exactly eight slots: all outer
  Host frames use version2, SET_ALL payload8, STATUS54 (6+8×6), METRICS16 (8×2).
  Remove wire16, padding/truncation and dual-stack compatibility. Keep the
  8-byte header, 2-byte CRC and128-byte payload ceiling for OTA/diagnostics.
- Preserve RF version1/fixed30-byte packets: eight duty bytes plus eight
  reserved255 bytes, not extra channels. RF OTA/settings bytes,125ms poll cadence,
  500ms watchdogs and5miss/6s presence versus2.5s freshness remain unchanged.
- Require coordinated TX/PC/production C6 migration. Current v2 tools cannot
  request BOOT/OTA on a v1 TX; use archived v1 tools or separately authorized
  wired ROM installation. RX does not need immediate flashing for this change.
- Update current contracts without rewriting historical builds/bench evidence.
  This migration does not flash hardware or deploy C6; software/build gates and
  deployment evidence are recorded separately, not inherited from old entries.

## 2026-09-08 — DIY safety review and Host offline tolerance

- Separate connection presence from telemetry freshness: tolerate fewer than
  five consecutive missed own-node polls and less than six seconds since a
  reply; stale data stays stale at2.5s, and lost/untrusted Host transport is
  never granted the RF tolerance window. Fresh miss-zero evidence is needed
  to establish/recover presence. No wire, RF or safety-watchdog changes.
- Adapt PC reference UI/API and tests; production C6 adaptation is tracked
  in its existing task, not deployed by this FanLink change.
- Document conditional noncritical DIY suitability, independent thermal and
  hardware-default-full requirements, recovery limits and historical replay.

## 2026-09-08 — Bounded software radio event tracing

- Record existing SPI results and sampled DIO1/BUSY states in a fixed RAM ring;
  freeze CRC/I/O fault timelines after existing cleanup and sample successful
  receives every64. No additional SPI commands, RF/power/pin changes or logging
  inside critical radio operations. Software samples are not hardware edges.
- Drain immutable snapshots as optional RTE text, with25ms throttling, strict
  nonblocking FIFO checks and STATUS/METRICS priority; pause during maintenance.
  Track dropped snapshots and overwritten history; never infer absence of
  faults from incomplete or sampled traces.
- Add offline paired-capture analysis and ring/scheduler regression coverage.
  Deployment and diagnostic conclusions belong to the dated evidence report.

## 2026-09-08 — Long-test diagnostic fairness and checked TX completion

- Reproduce optional diagnostic starvation with a 128-byte UART FIFO. Schedule
  six text families fairly with bounded retries; STATUS/METRICS retain priority,
  one optional attempt per loop, and impossible frames cannot block other families.
- Require checked SX128x TX_DONE IRQ before successful blocking transmit; a
  stuck-high DIO without TX_DONE now fails safely, preserving SPI/timeout errors.
- Add real-RadioLib simulated-I/O and production-writer FIFO regressions. These
  defects are proven in tests, not established causes of现场RF CRC loss. PHY,
  wire layouts, power, pin roles, polling and failsafe behavior are unchanged.
- Baseline, builds and deployment state: `docs/evidence/20260908-lite-30min.md`.

## 2026-09-08 — Both boards corrected to BETAFPV Lite, max defaults

- User corrected RX hardware identification: TX and RX are both Lite. Default
  all builds to chip +13 dBm; RX removes Nano RXEN9/TXEN10 and DC-DC, uses Lite
  settings profile. Node pin roles, polarity, LED and wire protocols unchanged.
- Preserve strict settings profile checks and low-power corrupt-record fallback;
  old Nano settings require explicit pin-role then power repair. Changed RX OTA
  identity requires wired BOOT migration; never bypass compatibility checks.
- Add actual build-flag regressions for all ten environments and settings tests
  for saved low-power overrides, max defaults and cross-profile safe recovery.
- Build/flash evidence: `docs/evidence/20260908-both-lite-max13.md`.

## 0.2.0-dev — 2026-09-08 (8-node capabilities; unflashed)

Historical ADR-023 milestone: its legacy Host envelope decision is superseded
by ADR-025 above. The following records the implementation at that time.

- Eight active receivers at 125 ms cadence, shared cooperative scheduling for
  normal polls and maintenance preambles; preserve legacy 16-channel envelopes.
- Add CRC-checked Host/RF settings protocol, board-limited power and persistent
  RX uart/pwm/pwmswap roles, safe corrupt-record handling and reboot grace.
- Unify normal and UART RX builds with OTA and settings support. Retain TX
  software reboot/ROM BOOT and local UART OTA; enforce a 5120 B/s image-data cap.
- Keep RF PHY and fragment guard unchanged; no claim of measured 5 KiB/s or RF
  stability improvement. C6 needs separate 8-node/2500 ms freshness adaptation.
- Validation, migration and hardware boundaries: `docs/CAPABILITIES.md`.

## 0.1.11-dev — 2026-09-08 (TX receive-error evidence; unflashed)

- Preserve independent sync/IRQ CRC/packet-status CRC causes, checked raw radio
  metadata and software receive-start/event times through the radio adapter.
- Add per-poll-window cause counters and sticky last-error evidence, distinct
  from accepted telemetry; report as bounded optional RFC/RFE Host text.
- Keep RF frequency, power, coding, sync word, packet bytes, bad-packet rejection
  and C6 freshness unchanged. RX02 diagnostic UART mapping remains compatible.
- This repairs a diagnostic blind spot, not a proven physical cause of the
  observed return-path loss. Validation and deployment state are recorded in
  `docs/evidence/20260908-tx-rf-evidence.md`; no flashing is part of this change.

## 2026-09-08 — RX02 UART diagnostic variant

- Reviewed diagnostic build and make verify PASS. First ACM0 ROM attempt received
  no serial data before erase/write. User-authorized retry at 09:24 CST succeeded:
  295936 padded bytes / 34.9 s, exit0, stayed in BOOT; UART released, ACM1 untouched.
  No pre-read/readback/reset; application startup and paired RF diagnosis pending.

- Add opt-in `fanlink_rx_node02_uart_diag`: GPIO1 UART TX / GPIO3 UART RX,
  no physical PWM/Tach, no OTA identity/updater. Normal RX environments unchanged.
- Post-controller bounded text includes reset reason, uptime, link state, poll/
  reply, raw receive/error, rejection/recovery and reply timing counters; UART
  pressure skips a line instead of waiting. RX02 remains poll-gated, RF unchanged.
- Add no-GPIO adapters and timing/FIFO/line-bound/Node02 poll regressions.
  This diagnostic-only device must be disconnected from fan signal circuitry.

## 0.1.10-dev — 2026-09-08 (TX local maintenance; unflashed)

- Add explicit TX local UART image upload on Host 0x12/0x13 with 0x91 replies,
  isolated from RX RF OTA. Stream-check unique TX identity before complete MD5
  verification and explicit Commit; retain staged update and reboot grace.
- Add guarded reboot / ROM UART download request 0x14 and 0x92 ACK using pinned
  Core restart/rebootIntoUartDownloadMode APIs. Require 600ms full-speed preamble
  and successful ACK transmission before lifecycle action; no implicit flasher.
- Add Python TX target uploader and explicit single-request lifecycle CLI, with
  wrong-role/target, malformed ACK, duplicate, timeout and release regressions.
- Preserve RF framing, power, GPIO, Tach and RX updater. No hardware operation
  or C6 change performed; see docs/TX_UPDATE.md for first deployment and limits.

## 0.1.9-dev — 2026-09-08 (3 KiB/s target; TX/RX separately written)

- Authorized same-image fast OTA completed:300240B in232.16s through Commit ACK,
  1293.16B/s (1.263KiB/s), about3.81x legacy but below3KiB/s target. Post-commit
  100 snapshots/99 fresh/0 parser rejects, exit0 and UART released. No readback,
  power-cut or long-term acceptance; log:`build/20260908-ota-fast-live.log`.

- Subsequent separately authorized RX ROM retry passed: source300240B, padded
  301056B/32.9s, exit0. First attempt failed sync before erase/write. No readback
  or reset, UART released; both endpoints written0.1.9, fast mode still untested.

- Subsequent authorized TX ROM write passed: source299088B, padded300032B/32.3s,
  exit0; no readback/reset, UART released. RX then remained0.1.8 and fast mode has not
  been hardware tested. Evidence:`docs/evidence/20260908-ota-3k.md`.

- Add opt-in100-byte Host block relay (0x11), fixed30-byte RF fragments (OTA op7)
  and cumulative block ACK (op8), with1000us rearm guard;650kbps PHY unchanged.
- RX buffers/validates a complete block before storage, rejects altered replay,
  preserves last-block idempotence and progress timeout; legacy10-byte migration
  remains the uploader default. No control/PWM/Tach/fingerprint change.
- Uploader caps fast payload traffic including retries at3072B/s; this is a
  target/cap, not measured throughput. No C6 change, new reset or OTA operation.
- Actual storage adapter supports bounded100-byte writes and retains complete
  image cancellation protection. Tests cover missing fragments/retry, duplicate
  block ACK, tail padding, mode isolation and mutable Core write-buffer isolation.

- First same-image RF OTA hardware attempt passed Begin and exceeded21percent,
  then host SSH/USB outage interrupted DATA acknowledgments; host reboot observed.
  No Commit sent or complete OTA claimed; no automatic retry.

- Separately confirmed RX01 BOOT deployment succeeded: ROM wrote300032 padded
  bytes/32.9s, exit0, no readback/reset. Both TX/RX01 written0.1.8; runtime unverified.

- Separately authorized0.1.8 TX wired deployment succeeded: ROM wrote299008
  padded bytes/32.3s, exit0, no readback/reset; RX unchanged, OTA not bench-tested.

- 2026-09-08 OTA original-task acceptance: local clean TX/RX01/RX16 builds3/3
  PASS, real RX images pass uploader inspect, unique descriptors and wrong-role/
  node rejection checked; aligned RX staging margin159744 bytes. No compile fix,
  physical flash or C6 change. Artifacts `firmware/dist/20260908-rf-ota/`.

## 0.1.8-dev — 2026-09-08 (RF OTA candidate, unflashed)

- Add TX-to-RX stop-and-wait RF OTA, explicit compatibility and MD5 checks,
  idempotent retries, timeout/abort and explicit commit with reboot grace.
- Hold full speed during maintenance; preserve Control/RPM v1, Node01 wiring,
  fixed RF settings, and the 0.1.7 Tach filter. First installation is wired.
- Add direct-serial Python uploader and fault/integration/storage regressions.
  ESP8266 stages in free sketch space; final eboot copy is not A/B rollback.
- Current physical TX is BETAFPV Lite using existing Generic2400 parameters.
  C6 forwarding, real OTA, power-cut and sustained fan acceptance remain unverified.

## 0.1.7-dev — 2026-09-08 (RX Tach candidate, unflashed)

- Replace single-last-period Tach estimation with bounded capture and multi-period
  noise rejection; preserve real no-pulse timeout and existing control safety.
- Add actual platform Tach edge-injection regressions to `make verify` rather
  than relying exclusively on controller FakeTach tests. Wire format, RF settings,
  PWM pin/frequency/polarity and C6 behavior remain unchanged.
- User-replaced BETAFPV Lite TX runs the previous 0.1.6 final TX; C6 observations
  still show erroneous RPM despite remaining online. This candidate is not flashed.

## 0.1.6-dev — 2026-09-07 (unflashed supervised candidate)

- Final TX subsequently written successfully: 296960 padded bytes/76.2s, exit0.
  Both user-confirmed TX and RX01 now have final 0.1.6 write records; no readback,
  automatic reset or RF/PWM acceptance. UART released.

- Subsequently wrote the final RX01 image to the user-confirmed connected RX:
  ROM write exit0, 291840 padded bytes/31.7s, no readback/reset, UART released.
  TX final candidate remains unflashed; RX application/RF/PWM acceptance pending.

- Subsequent retry could not reach the saved SSH host; no serial access or flash.

- Latest authorized RX retry found no remote USB serial device; guarded relay
  exited before ROM connection. No erase/write performed.

- Separately authorized RX01 direct-ROM attempt also received no serial data;
  no erase/write started, tunnel released, flashed version unchanged.

- Authorized TX direct-ROM write attempt did not connect (no serial data,
  exit 2); no erase/write started. Tunnel released; flashed version unchanged.
  One user-authorized retry returned the same result; stopped without further probes.
  A second separately authorized retry also received no serial data; no erase/write.

- Final freshness audit rounds processing age toward older telemetry; regression
  RED 3/708 then GREEN 708 including micros/millis rollover. Unique final batch:
  `firmware/dist/20260907-supervised-radio-final/`; earlier candidate retained.

- Carry first software DIO observation separately from adapter completion; TX
  keeps the original 5 ms budget and rejects late/invalid timestamps without
  weakening envelope, network, node or sequence validation. Preserve sub-ms waits.
- RX waits only the remainder of its 1200 us turnaround; skip obsolete replies
  at 5000 us and reject controls already 500 ms old. Processing does not renew
  control freshness. Retain unchanged 25 kHz PWM and safe full-speed behavior.
- Map FLRC sync error to DIO before launching RX; rearm on sync-only events.
- Add optional RFT text with observation/completion offsets and retained rejection
  reason; RX RAM-only own-poll/reply/error counters do not alter wire or use UART.
- TX regression RED 6/649 to GREEN 693; RX 509 checks and precise old-delay RED;
  actual RadioLib simulated-I/O regressions PASS. No physical timing claim.
- TX/RX devices remain at the previously written 0.1.5-dev batch. User's
  PWM-dependent telemetry failure is not established as NMI versus electrical;
  no flash/reset/control/OTA here. Full evidence, build status and acceptance plan:
  `docs/evidence/20260907-supervised-radio-solution.md`.

## 0.1.5-dev RX Node01 direct ROM write; telemetry investigation — 2026-09-07

- After TX, the user separately authorized the connected RX. Wrote the same
  link-timing batch's Node01 image (290,128 source bytes, SHA-256
  `5414c7e775e6558c1603409f0ad8c83d15c66967a55dff62a8995ac67affd5fb`).
- Erased only `0x00000000..0x00046fff` in 1.75 seconds; ROM-padded write
  290,816 bytes at 0x0 in 31.6 seconds, exit 0, ending in bootloader.
- Preserved 115200, no-stub, no-reset and flash header settings. No Flash
  pre-read/backup/readback or run/reset; task-owned relay closed and UART released.
- Both TX/RX01 now have successful 0.1.5-dev write records. The user subsequently
  reports effective control but no telemetry return. Cause investigation is
  pending; this documentation update adds no code fix or device operation and
  does not claim RF/PWM/endurance acceptance. Evidence and log locations:
  `docs/evidence/20260907-rx-ssh-flash.md`.

## 0.1.5-dev TX-only direct ROM write — 2026-09-07

- User authorized TX-only deployment of the link-timing batch. Source image
  295,824 bytes; ROM-padded write 295,936 bytes at 0x0 in 33.1 seconds, exit 0.
- Kept flash header settings, 115200, no-stub and no-reset; no Flash pre-read,
  backup, readback, run/reset or RX/C6 firmware changes. Final state is bootloader,
  not verified application/RF operation. Log: `build/tx-v015-ssh-flash.qK5yGx/direct-rom-write.log`.

## 0.1.5-dev — 2026-09-07 (initial code-only stage)

- Retain SX1280 single-packet RX across bounded software timeouts and rearm
  immediately after valid/CRC packets, removing the loop's timeout blind gap.
  Avoid continuous mode per the Semtech congested-traffic erratum.
- Use checked IRQ, length/offset, packet status, FIFO and IRQ-clear operations;
  reject inconsistent frames and metadata/SPI errors without returning old data.
- Skip redundant analogWrite for unchanged duty; initial/repeated begin still
  performs the real safe full-speed GPIO/PWM sequence.
- Add per-node saturating RFN poll/reply/age/status/error evidence within 0x81
  (at most 113 bytes); prioritize STATUS/METRICS without changing their layout
  or periods. Do not attribute 15 empty-node timeouts to the physical Node01.
- Preserve RF power/frequency/GPIO, v1 air/Host binary frames, sequence rules,
  reply timing and all C6/Host/RX safety thresholds. No flash or control writes.
- Before/after real-RadioLib simulated-I/O regression and new PWM/native tests
  recorded in `docs/evidence/20260907-link-timing.md`. RF improvement and sustained
  physical operation remain unverified until separately authorized bench work.

## RX Node01 direct ROM retry completed — 2026-09-07

- Wrote the existing `0.1.4-dev` RX Node01 image at `0x0`, preserving the three
  flash settings with `keep`, at 115200 in `--no-stub` / no-reset ROM mode.
- Source length 289,200 bytes; ROM-padded write 289,792 bytes in 30.0 seconds,
  exit 0, ending with `Staying in bootloader.`. Log:
  `build/tx-flash-rg35xx.YsEdGp/rx01-direct-rom-retry.log`.
- No backup/readback or run/reset. TX was not changed this attempt; C6 and
  RG35XX firmware remain unchanged. Startup/RF/PWM acceptance is pending;
  UART is released. The next action is user release of RX BOOT and normal
  power-up/observation.

## Earlier RX direct-write attempt — 2026-09-07

- Attempted the user-authorized RX Node01 ROM write without backup/readback
  or reset. Bootloader handshake returned no serial data (exit 2), before
  erase/write. TX's earlier successful write remains unchanged; C6 untouched.

## TX direct ROM write completed — 2026-09-07

- After the user re-entered BOOT and requested writing without further reads,
  wrote the existing `0.1.4-dev` TX image at `0x0` through RG35XX SSH/USB UART
  using esptool 5.0.2 ROM mode (`--no-stub`), preserving all flash header settings.
- The 294,224-byte source image was padded to 294,912 ROM-transfer bytes;
  writing completed in 30.7 seconds with exit 0 and `Staying in bootloader.`.
- Did not repeat backup/read-flash or independent chip-id/flash-id commands,
  and did not issue run/reset. Device readback/hash, startup and RF acceptance
  remain unverified. RX, C6 and RG35XX firmware were not changed.
- Closed the SSH UART tunnel and released the port. The next action is for
  the user to release BOOT and power TX normally, then observe startup/link.
  Source-image SHA and write evidence are in `docs/evidence/20260907-tx-ssh-flash.md`.

## Earlier TX service attempt — 2026-09-07

- User authorized TX-only flashing through RG35XX SSH/USB UART. Identified
  ESP8285H16 and backed up its complete 2 MiB flash before any erase/write.
- The bootloader stopped responding after backup exit; the write attempt
  failed during connection and did not erase or program the device. A later
  TX-only BOOT power cycle enabled the direct ROM write recorded above.
- Retained backup/image hashes and the earlier proposed readback/passive-check
  workflow as historical evidence; that workflow is not the current next action.

## 0.1.4-dev — 2026-09-07

- Added radio-only TX/RX recovery after hard I/O failures or sustained absence
  of valid link progress, with failed-begin backoff and no Tach/PWM reinit.
- Kept RX full speed before reset and retained the original TX Host command
  age across recovery; duplicate old controls no longer renew the watchdog or
  release failsafe. Restarted-TX older sequences require two advancing frames.
- Continued TX receive attempts after CRC/invalid replies within the same 5 ms
  window, with no deadline extension and a 32-attempt defensive limit.
- Released the RF front end via GPIO on driver error paths, preserved IRQ
  cleanup errors, and replaced nested startup retry loops with a single
  bounded radio discovery attempt plus controller backoff.
- Added limited `RFR attempts=... successes=... failures=...` diagnostics in
  existing ERROR_EVENT frames. Kept power, frequency, GPIO and v1 wire layouts.
- Validated the actual production FanLinkC6 wire implementation (1277 checks),
  507 native/sanitized checks, 21 PC compatibility tests, simulated real
  RadioLib faults and a 12-hour virtual controller run including recovery.
- Passed clean TX/RX01/RX16 real PlatformIO builds with SHA-256 readback.
  Build-input provenance and logs: `firmware/dist/20260907-radio-recovery/`.
- PC remains debugging-only. C6 UART failure/recovery/diagnostic work was
  dispatched to the user's existing ESPHome task. The implementation stage
  did not flash hardware or change production controls; subsequent TX-only
  writing is recorded above, without physical long-term RF validation.

## Source repository — 2026-09-07

- Prepared the standalone FanLink firmware, host panel, tools, tests and docs
  for Git version control on `main` at `https://github.com/atsrxL/FanLink`.
- Separated the Python serial console into `pc/` and embedded sources/build
  tools into `firmware/`; updated root Makefile, CI and documentation paths.
  The earlier ExpressLRS Node.js console is outside this repository.
- Kept generated firmware, build caches and historical archives local; added
  clone instructions and clarified the local-only historical build evidence.
- Excluded `.git` metadata from the existing source-package script.
- Redacted the local USB device's unique identifier from published bench records.
- Re-ran `make verify`: 269 native checks, 20 panel tests, CLI self-test,
  Python/JavaScript syntax, firmware stub smoke and source manifest passed.
  No device flashing or hardware measurements were performed for publication.
- Passed clean real-dependency TX/RX01/RX16 builds after moving the PlatformIO
  project into `firmware/`; firmware hashes were read back and verified. Local
  evidence is in `firmware/dist/20260907-git-layout/`, including the build-input
  source manifest retained before the final documentation-only updates.

## 0.1.3-dev — 2026-09-04

- Added a loopback-only TX browser control panel with strict 16-channel input,
  safe `255` defaults, 200 ms full-state refresh, serial status/diagnostic
  parsing, demo mode and explicit disconnect behavior. Its duty controls are
  open-loop PWM commands, not closed-loop RPM targets.
- Added Host message `0x82` (`LINK_METRICS_SNAPSHOT`). Its fixed 32-byte payload
  carries two signed `int8` values for each of Nodes 01–16: uplink RSSI followed
  by downlink RSSI. `-127` means unknown; the panel displays the two directions
  separately and returns expired metrics to unknown instead of retaining them.
- Passed 20 offline control-panel tests, native and ASan/UBSan suites at 269
  checks each, CLI self-tests in normal and optimized modes, Python/JavaScript
  syntax, firmware stub smoke and CMake/CTest 1/1,
  then connected the panel to a physical TX at `/dev/cu.usbmodem<device>`
  (USB `1A86:55D3`). Live snapshots and the browser agreed; disconnect reset the
  panel state safely, and a separate CLI subsequently showed persistent TX
  failsafe with all duties at 255.
- Exact 200 ms serial timing, the exact 500 ms watchdog boundary, new
  `ERROR_EVENT` fault injection, separate RSSI values, corrected PWM endpoints
  and measured RF/PWM/Tach behavior remain unverified.
- Verified that terminating the connected backend makes the browser lock all
  controls on its next poll and clear watchdog/RPM/applied-duty fields to unknown;
  an independent CLI again observed TX failsafe and all duties at 255. Evidence:
  `docs/evidence/20260904-tx-panel-usb.md`.
- Added a non-blocking BETAFPV Nano GPIO16 status LED. Search/link-failsafe uses
  two 100 ms flashes followed by 500 ms off; a fresh RX-accepted FanLink
  downlink keeps the LED continuously on.
- Added compile-time LED pin/conflict checks and native boundary tests for the
  complete 800 ms pattern without changing the FanLink air packet format or
  the meaning of existing Host messages.
- Added bounded TX `ERROR_EVENT` diagnostics that distinguish radio init,
  transmit, receive timeout, receive CRC/I/O, invalid application replies and
  accepted replies, with saturating counters and no air-protocol change.
- Kept the latest actionable TX/RX driver errors across later successful slots,
  split the last TX/RX raw codes, and bounded the worst-case compact diagnostic
  text to one 128-byte Host frame so a control panel cannot display a silently
  cleared or truncated fault.
- Set the shared BETAFPV Nano RX radio configuration to `-6 dBm`, corresponding
  to the official PA target's 25 mW power step. Kept the JHEMCU Lite TX's bare
  SX1280 at `+13 dBm`, approximately its 20 mW hardware ceiling; these are
  target configuration values, not conducted-power measurements. Retained
  `-18 dBm` only as the generic unknown-board fallback.
- Added a Node 01-only hardware override: fan PWM on GPIO3, Tach on GPIO1 and an
  active-low external gate. Nodes 02–16 retain the default PWM GPIO1, Tach GPIO3
  and active-high gate. The electrical inversion does not change the logical
  command contract: `0` requests stop and `255` requests full speed.
- Made the clean firmware collector install each environment's pinned packages
  before invoking `pio run -t clean`, so a new `.pio` tree cannot fail merely
  because the strict RadioLib pre-build hook runs before dependency discovery.
- Kept the `0.1.1` and `0.1.2` build evidence as historical baselines, then passed
  the `0.1.3-dev` source manifest, `make verify` and clean TX/RX01/RX16 PlatformIO
  build. The reviewed batch, logs, provenance and reread-verified SHA-256 files are
  in `dist/20260904-v0.1.3-25mw-node01-rssi-reviewed/`.
- Recorded later application-level evidence from the already-flashed `0.1.2` pair:
  RX link LED steady, duty control effective and fresh Node 01 RPM replies. A duty
  of `0` still yielded about 2787 RPM on that image, establishing the physical
  polarity mismatch. The new `0.1.3-dev` correction and RSSI path remain unflashed.

## 0.1.1 — 2026-09-04

Local review and real dependency-build milestone:

- Added target profiles for JHEMCU 2.4GHz Lite RX hardware repurposed as the
  FanLink TX and BETAFPV 2.4GHz Nano as the FanLink RX, derived from pinned
  ExpressLRS target data.
- Added the BETAFPV Nano RXEN/TXEN truth table and per-target LDO/DC-DC
  regulator selection; retained minimum `-18 dBm` bring-up power.
- Corrected RadioLib 7.7.1 integration for project includes, real error codes,
  exact 30-byte fixed packet mode and timeout units without changing protocol v1.
- Added a version/source-checked RadioLib integration hook so JHEMCU remains in
  LDO mode from initial configuration and BUSY/TX_DONE waits are hard-capped at
  25 ms; enabled 64-byte static RadioLib buffers for the 30-byte data path.
- Hardened RX watchdog deadline handling, TX watchdog equality, sequence
  reacquisition, exact duplicate acceptance and failsafe-status latching.
- Reordered ESP8266 PWM output setup to preload the safe level and corrected a
  Tach timestamp race; GPIO16 Tach is now rejected explicitly.
- Expanded native regression coverage from 143 to 209 checks and passed an
  AddressSanitizer/UndefinedBehaviorSanitizer run.
- Added pinned PlatformIO CI plus Python syntax, optimized self-test, firmware
  syntax and source-manifest gates.
- Made local builds clean and transactional, with unique run directories,
  complete logs, provenance and SHA-256 manifests; made source ZIP packaging
  exclude generated artifacts and use a portable read-back-verified checksum.
- Corrected the deterministic loss-ratio simulator and made Python self-tests
  effective under `PYTHONOPTIMIZE=1`.
- Completed clean real-dependency builds for TX, RX Node 01 and RX Node 16 with
  PlatformIO Core 6.1.19, `espressif8266@4.2.1`, Arduino ESP8266 Core 3.1.2 and
  RadioLib 7.7.1.
- No flashing, RF exchange, PWM/Tach measurement, thermal test or PCB-revision
  confirmation is claimed.

## 0.1.0 — 2026-09-04

Initial Codex hand-off starter:

- Created a standalone FanLink application rather than an ExpressLRS fork.
- Added fixed 30-byte Control and 30-byte RPM Reply packets.
- Added CRC-16/CCITT-FALSE and fixed-length binary host serial protocol.
- Added full-state 16-channel downlink and strict poll 1–16 scheduling.
- Added TX host watchdog and RX link watchdog, both failing to full speed.
- Added RX stale-sequence rejection, same-sequence retry support, gap accounting and 16-bit wrap handling.
- Added platform-independent TX/RX controllers and 143 native checks.
- Added ESP8266/ESP8285 fan PWM, Tach and RadioLib SX1280 starter adapters.
- Added PlatformIO environments for TX and RX nodes 01–16.
- Added Python CLI, cluster simulation, build collector, syntax smoke test and hand-off documentation.
- Pinned intended embedded dependencies to `espressif8266@4.2.1` and RadioLib `7.7.1`.
- Set the generic SX1280 chip-power default to `-18 dBm` as a cautious bring-up starting point.
- No embedded dependency build or hardware validation is claimed.
