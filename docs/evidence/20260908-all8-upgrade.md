# 2026-09-08 — All-eight RX builds and coordinated Host v2 upgrade

## Current delivery status

Latest user mapping is ACM0=RX1, ACM1=RX2. RX1-only manual-BOOT ROM flash
succeeded: fanlink_rx_node01.bin,310256 bytes, SHA256
`ddb250bc8c4342b4c3ad28ea8ccef1fcdc52da3a7f17da869ac7e78de062fb26`.
ROM reported310272 padded bytes written in36.3s, exit0, staying in bootloader.
No Flash pre-read/readback or automatic reset; saved settings partition was not
erased. ACM0 independently released, bridge0. RX2 was not operated. User must
remove BOOT short and power cycle; RX1 runtime/settings/RF acceptance pending.
Log: `build/20260908-all8-rx1-rom.log`. Earlier seven-unreachable statement below
describes the preceding OTA stage; RX1 has now been written over ROM.

Later user-authorized RX2 ACM1 manual-BOOT ROM write of the same UART image
did not reach erase/write. First attempt stopped at a stale USB topology guard
before remote UART open. Read-only topology verification allowed one retry,
which failed ROM synchronization with invalid packet head0x52, exit2. No reset
or Flash pre-read/readback was performed. ACM1 was independently confirmed
released, bridge0. Current application state needs renewed acceptance; the
earlier OTA evidence below is historical and unchanged. Retry log:
`build/20260908-all8-rx2-rom-retry1.log`.

Ten real clean builds and `make verify` passed. RX2 RF OTA completed its transfer,
Commit ACK and short telemetry/settings recovery checks. TX UART OTA received
Commit ACK at115.413s and passed35s plus8s Host v2 recovery acceptance. The
production C6 compact8/pinmode OTA and actual API/HA entity exposure passed;
end-to-end RX Query awaits physical-link confirmation. The other seven RX devices were
unreachable and have build artifacts only, not deployment or hardware acceptance.

The Host contract remains version2, SET_ALL8/STATUS54/METRICS16/MAX128 with no
permanent legacy adapter. RF remains version1/fixed30 bytes, with eight duty
bytes plus eight reserved255 bytes. Polling, power defaults, pin-mode settings
and watchdog safety policy are not relaxed by this deployment.

## Build and software verification

- Batch: `firmware/dist/20260908-all8-upgrade`.
- Metadata and per-environment build logs: `BUILD_INFO.json` in that directory.
- Full verification: `build/20260908-all8-full-verify.log` (PASS).
- PlatformIO6.1.19, espressif8266@4.2.1, Arduino ESP8266 Core3.1.2, RadioLib7.7.1.
- Build input manifest SHA256:
  `cbaa5b5acd923e2f2ae7b2e6a28cd7174c0e3e38ec29eec62961ea38c924f66a`.

All filenames below are relative to the batch directory; generated firmware and
raw build/device logs remain local and excluded from Git. Hashes identify the
build artifacts, not Flash readback from a device.

| Artifact | Bytes | SHA256 |
| --- | ---: | --- |
| fanlink_tx_esp8285.bin | 315472 | `31cbb783b2c2cf714ce54dd65e8c32ec868ca80fbed2047e483cc2fefacb8252` |
| fanlink_rx_node01.bin | 310256 | `ddb250bc8c4342b4c3ad28ea8ccef1fcdc52da3a7f17da869ac7e78de062fb26` |
| fanlink_rx_node02.bin | 310256 | `647f329466fbe56484e72446c2c23192d8287fd5eda2a31600a7b6183c97a4c7` |
| fanlink_rx_node03.bin | 310256 | `acd20c0e0d502aee63eae023c6cc0fcb1c635bf48af5e82bb04d975fbd3cc2a0` |
| fanlink_rx_node04.bin | 310256 | `914e33a0704665471047cd1a949fb2caa7e684df69ccb3875b827c25a57a8602` |
| fanlink_rx_node05.bin | 310256 | `7da3deeb73b8f286fe8bfa730b72d5cce708bc53adb0a55a670e82e91f259c6a` |
| fanlink_rx_node06.bin | 310256 | `aca42ac0157ebf5015e8f379e984c2ef6017be47586d999d116ee15250b4ecd6` |
| fanlink_rx_node07.bin | 310256 | `4c2accf464ce6dd43fb310ca6c47df4ba6c8cfe06b89ffbde9559d87cdc66db0` |
| fanlink_rx_node08.bin | 310256 | `a56412e3bab6b277f98903f880088522a4d76f10347477bcb4dc0cb1588ca36e` |
| fanlink_rx_node02_uart_diag.bin | 310256 | `afbb7c2c54e104a746bf81a0292854692d80c06142b2d59a4824ae8b1adf10ec` |

## RX2 RF OTA and recovery

Local operation evidence: `build/20260908-all8-rx2-ota.log`.
The archived Host v1 path through the still-old TX transferred the RX2 UART
image:310256 bytes,100% at380.150s, Commit ACK at380.205s.
The old Host version in this RX2 operation is not evidence that TX migrated.

The35s recovery check passed with33 fresh tail **status snapshots**, followed
by an8s check with32. These are not independent RF packets or a packet-success
rate. The parser reported zero rejects; final settings Query confirmed Lite
profile1, UART mode0, +13dBm, flags0, eight receivers and125ms slots.
The operation recorded `UPGRADED_AND_RECOVERED` at424.558s and relay closure at
427.56s. After the RX operation both UARTs were independently checked released
with `fuser`, and bridge running count was0. This does not assert their later
state during the separate TX operation.

Acceptance is limited to completed transfer/Commit ACK and short recovery with
configuration retained. No direct RX uptime reset or Flash readback was checked.
The full-speed observation is protocol/state evidence on an UART diagnostic
receiver, not physical fan behavior or a measured PWM waveform.

## TX UART OTA and Host v2 recovery

The315472-byte TX image in the table received Commit ACK at115.413s. At the end
of the35s observation, TX uptime was32 versus pre-upgrade5466, demonstrating a
runtime restart; Host version2/STATUS54 and monotonic-uptime checks passed, with
32 fresh tail status snapshots. These snapshots are not independent RF packets.
Both version2 Queries confirmed TX Lite/+13dBm/flags0 and RX2 UART/+13dBm/flags0,
eight receivers,125ms period and5120B/s OTA cap.

An additional8s observation ended at uptime40 with32 fresh tail status snapshots.
The operation recorded `UPGRADED_AND_RECOVERED` at158.506s and relay closure at
161.511s. A separate post-operation check again confirmed both UARTs released
with `fuser` and bridge running count0. This is short Host/runtime recovery
acceptance, not Flash readback or proof of an RF CRC fix.

## C6 and acceptance boundaries

The separate production C6 task compiled and OTA-deployed the exact963312-byte
candidate, SHA256 `045288e031020c5c1bdae900e946af3b3102c105a637ff79137737a2fe5a2e89`.
Identity was checked before OTA. The recovered API reports compilation time
2026-09-08 07:12:24 UTC and stayed connected through a45s observation.
Actual API and HA registry/state reads confirm eight enabled config selects
`select.fanlinkc6_ch01_rx_pin_mode` through `select.fanlinkc6_ch08_rx_pin_mode`,
each with exactly `uart`, `pwm`, `pwmswap`, plus eight enabled diagnostic
`sensor.fanlinkc6_chNN_rx_settings_status` entities. No HA writes or history
cleanup were performed. The new API has75 entities, no CH09..16 or exposed LED.
The main task independently inspected the API/HA acceptance JSON reports.
The final C6/TX header compatibility test passed1105 checks.

Pin mappings: uart=GPIO1 TX/GPIO3 RX; pwm=GPIO1 PWM/GPIO3 Tach;
pwmswap=GPIO3 PWM/GPIO1 Tach. Successful settings changes are saved and reboot
the RX; the C6 uses ACK and subsequent Query confirmation, not optimistic UI
updates or automatic Set retries. TX retains its required Host UART.

All eight C6 Connected states are currently false, modes unknown and settings
status `Unknown; awaiting online RX Query`. Firmware/API/HA exposure is accepted,
but C6-to-TX physical connection and end-to-end Query/control are not. The user
must confirm/reconnect that link for further acceptance. No RX mode, power or
requested duty was changed, and no C6 serial passthrough was enabled.

Neither ten successful builds nor RX2 short recovery establish simultaneous
eight-receiver operation, an RF CRC root-cause fix, multi-hour reliability or
physical fan fail-safe acceptance. Other seven receivers remain unflashed.
