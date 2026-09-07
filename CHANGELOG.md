# Changelog

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
