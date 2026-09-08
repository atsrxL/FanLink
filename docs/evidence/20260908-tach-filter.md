# 2026-09-08 RX Tach candidate (0.1.7-dev, unflashed)

## Scope and baseline

User authorized fixing erratic RPM after replacing TX with BETAFPV 2.4GHz Lite RX
hardware running the existing 0.1.6 TX image. Only RX Tach estimation changes;
no RF tuning, GPIO/PWM change, C6 OTA, flash operation or OTA feature is included.
Old deployment remains 0.1.6 on both ends. The user separately asked about future
RF OTA; this is a capability discussion, not authority to implement an updater.

Read-only C6 observation (identity MAC [device MAC redacted]) at PWM about 70 percent:
connected remained ON while RPM included 0, 1490, 1531, 20746 and 29211. A second
20-second event capture is retained at `build/20260908-tach-before-c6.events`
(curl exit28 is the deliberate capture deadline, not a device timeout).
RFN reply timeouts still increase: fewer visible disconnections does not mean
all polls succeed. No physical Tach waveform or independent RPM measurement exists.

Old code used only the last accepted edge interval; a single 5 ms interval at
2 pulses/revolution reports6000 RPM, while500 us reports invalid/0. The controller
already maps invalid Tach to0/TACH_TIMEOUT; that wire semantic is unchanged.

## Candidate algorithm

- ISR only enqueues raw timestamps in a fixed32-entry ring (31 usable). No divide,
  sort, heap, radio or logs in ISR. Overflow drops ambiguous history and reports invalid.
- Main loop rejects raw intervals below the physical configured maximum-RPM period
  (1000 us at2 PPR/30000 RPM), without moving the accepted-period anchor. Checking
  consecutive raw intervals prevents fast noise from aliasing through a simple divider.
- Five-period window, median-centered +/-20 percent consistency and at least4
  inliers; average agreeing periods. The newest period must also agree to renew
  freshness. Isolated outliers hold a previous valid value only within its timeout.
- Only trustworthy edge timestamps refresh the estimate; polling and rejected
  noise do not. No trustworthy period for1 second returns invalid/0, retaining
  controller Tach and stall behavior. Reacquisition needs a new coherent window.
- Unsigned timestamps and explicit presence flags handle timestamp0 and micros wrap.

This is not a universal electrical-noise detector: consistent in-range periodic
interference can look exactly like Tach. Persistent incoherent noise may correctly
produce0/TACH_TIMEOUT rather than an invented plausible RPM. Actual pin voltage,
grounding, edge waveform and PWM interrupt interactions still need bench validation.
Startup and real speed changes now require several periods (about0.1s at1400 RPM);
very low speeds can take longer. No duty-derived RPM cap or cosmetic C6 smoothing.

## Validation and artifacts

- Before change: native708 PASS.
- New actual Esp8266Tachometer edge-injection tests fail against old code for
  isolated500us/5000us, burst and missing-edge cases (4 red scenarios).
- After change: `make verify` PASS, including708 native,2657 PWM,509 RX timing,
  1804 joint timing,21 panel tests, CLI and stub syntax; log `build/20260908-tach-verify.log`.
- Actual Tach class:19 scenarios/14665 checks, normal and ASan/UBSan each PASS.
  Sanitizer log `build/20260908-tach-sanitizer.log`; runner
  `SANITIZE=1 ./firmware/tools/run_tachometer_tests.sh`.
- Independent source safety review: no blocking defect found. Dense noise and
  real ESP8266 execution time remain bench-only acceptance items.
- Clean TX/RX01/RX16 PlatformIO builds3/3 PASS; log `build/20260908-tach-build.log`.
  Core6.1.19, espressif8266@4.2.1, Arduino3.1.2, RadioLib7.7.1.
- Unique artifact batch: `firmware/dist/20260908-tach-filter/`, unflashed.
  Build-input manifest preserved as `SOURCE_MANIFEST.build-input.sha256` in batch;
  digest `4cb9def59a568deb7c23eef95a4c0b2c4e8baa56f99eeb6a98802b9d1d84c864`
  matches BUILD_INFO. Subsequent changes are final documentation only.

| Image | Bytes | SHA-256 |
|---|---:|---|
| RX01 |292128|`3a318c65926110b514156082873faf25c157768da6b88904bcf0e2f5464c4543`|
| RX16 |292144|`f68bb5dff5807d2538b29d7775bb283ffad2e801805818500db951bdac1e7051`|
| TX (regression build only) |296848|`058b750ddea47658f4ea76f5bf73498a5d9d6bb4726b483c4ef75ce22fdcf907`|

Reproduce with a new unique run id:

```sh
PLATFORMIO_CORE_DIR=/tmp/fanlink-pio-core-v013-preflight \
uv run --offline --with platformio==6.1.19 python3 firmware/tools/build_firmware.py \
  --tx --rx 1 --rx 16 --run-id NEW_UNIQUE_RUN_ID
```

## Next physical acceptance (separately authorized flash)

Only RX01 needs updating for this change; TX/C6 protocol and settings are unchanged.
Keep an independent cooling safeguard. After user-confirmed RX01 BOOT/direct-ROM
write, compare RPM against a reference at stable and changing duty, disconnect
Tach to confirm0/TACH_TIMEOUT, and check link-loss full speed. Capture at least30min
of C6 RPM/RFN plus Tach/PWM waveforms before claiming hardware stability. Never
disconnect a cooling fan from a thermally loaded system merely to run this test.
