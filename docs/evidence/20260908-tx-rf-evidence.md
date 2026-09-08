# TX receive-error evidence candidate — 2026-09-08

## Milestone and boundary

Repair missing error classification and software timing evidence before changing
RF behavior. User requests normal operation at about30cm and cannot separate to
1m. The preceding paired120s capture is in `20260908-dual-rx02-tx.md`.
RX02 ownpoll/local-reply +732, TX accepted461/732; no RX reboot or stopped reply
observed. TX CRC-class events include different underlying failure causes.
This is not a proven saturation, antenna, timing or CRC root cause.

No serial connection, reset, BOOT request, flash, power change or C6 deployment
is performed in this implementation turn. Already-running RX02 UART diagnostic
firmware remains compatible; GPIO1=UART TX, GPIO3=UART RX, no fan/tach functions.
The new TX retains prior0.1.10 local maintenance code, which is not yet fielded.

## Implementation

- Optional `RadioRxEvidence` is local RAM metadata, not an air-packet extension.
- Checked IRQ/packet status have separate validity flags. Sync-only events do
  not pretend a complete packet or usable packet RSSI was received.
- Event and receive-start times are software observations in the same wrapping
  microsecond clock. Receive start means SetRx command completion, not measured
  RF readiness; event time is not a physical DIO edge.
- Capture the current event's RX-start value before rearming. Timeout must not
  replay an old IRQ/error. Error causes may overlap and must not be summed as
  independent packet failures.
- TX cause counters and sticky last-error data do not update RPM, node identity,
  normal RSSI, lastSeen or freshness. Good packets do not erase the last error.
- New optional RFC/RFE text is sent after controller work with strict line and
  frequency bounds. Existing binary Host/RF layouts and old diagnostic grammar
  remain unchanged. No RF parameter or accepted-packet rule is changed.
- All optional diagnostic writes require space for the encoded frame plus one
  byte; skip under FIFO pressure rather than waiting. Pinned Core3.1.2
  `cores/esp8266/uart.cpp:504-515` spins when occupancy reaches127, while
  `uart_available_for_write` reports capacity against128. STATUS and maintenance
  ACK paths are unchanged. This bounds diagnostics, not every UART path.
  Old diagnostic text too long to fit the physical FIFO may also be skipped;
  this never suppresses the higher-priority binary STATUS path. RFC/RFE are
  individually bounded to fit an empty FIFO, including their10-byte envelopes.

The deployed `setCRC(2)` argument is unchanged: RadioLib7.7.1 encodes0x20,
which the SX1280 FLRC packet parameters define as three hardware CRC bytes.
The FLRC polynomial is fixed, while the seed is configurable; this is distinct
from FanLink's unchanged16-bit application CRC. This API/documentation mismatch
does not explain an asymmetric loss with both endpoints configured alike.

## Validation

Before changes: `./firmware/scripts/run_native_tests.sh` -> PASS708 checks.
After changes: native1433 PASS, separate clang ASan/UBSan1433 PASS. New coverage
includes cause overlap/source validity, successful replies after bad events,
sticky errors, legacy ports, unknown/future/reversed/wrapped time, saturation,
text lengths and strict FIFO allowance. Independent review found no blocking
defect after metadata failure-preservation tests were added.

- `make verify` -> PASS, including native1433, virtual12h recovery, fan/tach,
  reply timing, OTA8102, real storage adapter242, TX maintenance,61 Python tests,
  RX02 diagnostic, syntax smoke and source manifest.
  Log: `build/20260908-tx-rf-evidence-verify.log`.
- Sanitizer log: `build/20260908-tx-rf-evidence-sanitized.log` -> PASS1433.
- `python3 firmware/scripts/run_radiolib_recovery_tests.py` -> PASS with actual
  pinned RadioLib and simulated SPI/GPIO. Good-frame SPI command count unchanged;
  error causes/metadata and cleanup/rearm fault matrices pass.
  Log: `build/20260908-tx-rf-evidence-radiolib.log`.
- CMake Debug build plus `ctest --test-dir build/cmake-firmware --output-on-failure`
  -> PASS4/4. These are software regressions, not RF endurance.

The real local production C6 header was checked read-only with
`bash firmware/tools/run_c6_wire_compat_tests.sh /path/to/esphome/config/devices/fanlinkc6/components/fanlink`
-> PASS1277 checks. Header SHA256:
`4de80abe35d75a2ebd0cc958d77265100f7099ab4e1b0c4703c2842485cbfb3a`.
This validates wire parsing, not C6 deployment or its rate-limited log visibility.

## Real build and artifacts

```sh
PLATFORMIO_CORE_DIR=/tmp/fanlink-pio-core-v013-preflight \
  uv run --with 'platformio==6.1.19' python3 firmware/tools/build_firmware.py \
  --tx --rx 1 --rx 16 --rx2-uart-diag --run-id 20260908-tx-rf-evidence
```

Clean4/4 PASS with espressif8266@4.2.1, Core3.1.2, RadioLib7.7.1. Only preexisting
Core elf2bin Python escape SyntaxWarnings appeared; no project compile errors.
Full log: `build/20260908-tx-rf-evidence-build.log`.
Directory: `firmware/dist/20260908-tx-rf-evidence/`; all `SHA256SUMS` entries pass.

| Image | Bytes | Static RAM | SHA256 |
|---|---:|---:|---|
| TX Lite | 310416 | 33428 | `cf19863c19ff63507b708d0fbca38377d21121ff1bdcd02ad8d2a06843e327c9` |
| RX01 Nano (build regression only) | 300144 | 30208 | `147fc4b9dd8562cd759f7f024399b43122419e7ff5f32d8c06799228b7b574ce` |
| RX16 Nano (build regression only) | 300144 | 30208 | `94b72d69dd28975f48b5079a23bcaffd6fc5098ef6813ff97b5779c444d6999a` |
| RX02 UART diagnostic (build regression only) | 295440 | 29876 | `adbd66bf51b5c69be5bd00cc5c1dbde019c6842c98442d9c1376ed2e6fddd38f` |

TX linked Flash306271B, binary310416B. The reviewed previous TX local-maintenance
candidate used32400B static RAM; this candidate adds1028B static RAM. TX and RX01
offline image-identity checks pass (role/network/node); no serial connection is
made by `--inspect`. Hardware compatibility and startup are still unverified.

Exact build-input source manifest is preserved at
`build/20260908-tx-rf-evidence-source-manifest.sha256`, SHA256
`3a5d56ef101d51133aaea0401262354f7fa13841237207bf4d8c22eb05eef4f5`, matching
BUILD_INFO.json. Subsequent source-manifest changes are evidence/status documents
only; no firmware source was edited after the reviewed build.

## Next sole hardware action

After the user separately identifies TX and confirms current BOOT readiness,
write only the reviewed new TX image using the established direct ROM-write
procedure (no pre-read/readback/reset). Keep the current RX02 and placement;
once applications run, repeat the paired read-only capture and correlate RFC/RFE
with RX02 reply counters. Neither successful flashing nor increased logging
alone establishes an improvement in return-path reliability.

Interpret the next trace conservatively: sync-only versus IRQ/packet CRC counts
identify different failure stages; error arrival versus SetRx-completion offsets
help distinguish synchronization/decoding from late software observation. RSSI
belongs to the untrusted error event, not an authenticated node. RFE is a sticky
latest-event sample at at most1Hz, not a lossless trace of every failed packet.
Do not change power, frequency, protocol, guard times or freshness thresholds
until the new trace supports a specific independently reviewed change.
