# 2026-09-08 Both BETAFPV Lite / maximum default power

User confirms both TX and RX are BETAFPV 2.4GHz Lite RX, superseding the earlier
Nano RX assumption. Both build defaults are chip +13 dBm (official table max,
not measured antenna output). RX uses LDO and no external RF switch pins.
RX01 PWM3/Tach1/gate0 and RX02 default UART roles remain unchanged.

Authoritative pinned upstream checked read-only:
- https://raw.githubusercontent.com/ExpressLRS/targets/c4bd7b823594c233e673828ab493a2f8319a756a/targets.json
- https://raw.githubusercontent.com/ExpressLRS/targets/c4bd7b823594c233e673828ab493a2f8319a756a/RX/Generic%202400.json

Lite target uses ESP8285, Generic2400, power_values=[13], LED16; no RXEN/TXEN
or radio_dcdc override. Physical PCB revision/output power still unmeasured.

Erased storage uses max defaults; valid same-profile records are preserved.
Existing Nano RX record is intentionally invalid under Lite: Safe254/-18 until
explicit SetPinMode, then SetPower13 with post-reboot Query. No implicit erase
or unsafe cross-board migration added. RX OTA compatibility identity changes;
current Nano firmware cannot accept Lite image over air. TX lifecycle supports
runtime-to-ROM BOOT; RX currently does not expose this command.

Native baseline2744, settings regression and syntax smoke PASS. Clean build and
flash results will be recorded below. User explicitly authorized command BOOT
and direct ROM write after build, without Flash pre-read/readback.

## Validation and artifacts

`make verify` PASS including native2744, settings/migration tests, actual ten
environment board macros, OTA/PC/platform regressions and source manifest.
Clean PlatformIO6.1.19 builds TX/RX01/RX08/RX02 UART4/4 PASS; complete artifact
SHA checks PASS. Binaries under `firmware/dist/20260908-both-lite-max13/`: TX
314240B, each RX308784B. Exact hashes: BUILD_INFO.json and SHA256SUMS.
Build log `build/20260908-both-lite-max13-build.log`, verification log
`build/20260908-both-lite-max13-verify.log`. Build input manifest retained at
`build/20260908-both-lite-max13-build-input.sha256` before outcome-only docs.
The first build was stopped by stale manifest before compilation; manifest
regenerated then clean build passed, no guard bypass.

## TX software BOOT and direct ROM write: PASS

Live SSH confirmed RG35XX V3, same guarded adapter, TX ACM0 / RX2 ACM1, no owners
and no bridge. TX Query reported profileLite, mode255, power13, flags0. Sent
one boot request; matched accepted ACK then esptool5.0.2 successfully synced
to recorded TX ESP8285H16. Direct no-stub115200 write0x0, no-reset before/after,
keep flash options. Wrote314368 padded bytes in35.6s, exit0, Staying in bootloader.
TX SHA a0748dd545abd52e150a132c5c1663abce25dd96f83f3a30b666d3e0a44a532b.
Log `build/20260908-lite-max13-tx-boot-flash.log` and relay log alongside.
No Flash pre-read/readback or reset; local image checksum is not device readback.
New application/RF acceptance pending power cycle. No RX2 UART open or write;
RX lacks runtime BOOT capability and waits for user manual BOOT. C6 untouched.

## RX2 manual BOOT and direct ROM write: PASS

User confirmed RX2 in BOOT. SSH/USB enumeration and idle-owner checks matched
the guarded ACM1/if02 mapping; no runtime query or BOOT/reset command sent.
Wrote `fanlink_rx_node02_uart_diag.bin` (308784 source bytes), SHA256
`0dd772e80681462bc3e925b2c2265cc62e24d017556f85f68ed658caa969d805`.
esptool5.0.2 no-stub115200, write0x0, keep flash parameters, no-reset before/after:
309248 padded bytes in35.4s, exit0, Staying in bootloader. No Flash pre-read,
readback or settings-sector erase. TX untouched during this write.
Log: `build/20260908-lite-max13-rx2-rom-write.log`.
Application and RF acceptance remain pending manual normal power-up. Old Nano
settings require explicit UART then power13 restoration with reboot/Query checks;
the max build default is not proof of current runtime power.

## Normal power-up, persisted recovery and 60-second runtime check

User confirmed both normally powered up. Guarded mapping remained TX ACM0/RX2
ACM1, bridge stopped and ports free. Initial Query: TX Lite/13/flags0; RX2
Lite/Safe254/-18/flags2, consistent with cross-profile settings rejection.
Only RX2 SetMode UART then SetPower13 were sent; each Ok/pending ACK followed
by six seconds and a matched post-restart Query. Final queries both Lite/13/flags0,
RX2 modeUART. TX setting already correct and not rewritten. Log
`build/20260908-lite-max13-runtime-restore.log`, exit0. No BOOT, firmware write,
duty/heartbeat or C6 change. These queries verify persisted runtime selections,
not measured RF output. RX UART diagnostic data also resumed.

Read-only dual capture `build/20260908-lite-max13-dual-runtime.jsonl` completed
60.003s; TX42949B/RX8990B, parser rejects0. All239 Node02 snapshots fresh,
no fresh-to-stale transitions, max reported age1900ms, max consecutive miss1.
TX uptime156→215s; RX39 diagnostic lines all L1, uptime22790→79851ms monotonic.
Excluding initial transport backlog, RFN3.992→60.003s:56 polls,46 accepted
(82.14%),10 timeouts and10 CRC-class errors; transmit/I/O/application rejects
unchanged. RFC confirms10 CRC errors, sync errors0. RX independently sampled
3.103→58.664s:55 own polls and55 local replies, failures/skips0; receive CRC+2,
recovery and malformed/stale/expired-control counters unchanged. Counter windows
are not packet-aligned. Host failsafe remained expected without a heartbeat;
UART diagnostic RX deliberately has no PWM/Tach, so RPM0 is not tach acceptance.
Both UARTs confirmed released afterward. Short run remained connected but
return-path loss persists; this is not a long-run or RF-root-cause pass.
