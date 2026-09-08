# 2026-09-08 0.2.0 TX / RX2 ROM write attempts

User authorized both TX and RX2 writes and reported both in BOOT. Existing
mapping retained: RX2 ttyACM0/USB if00, TX ttyACM1/if02. RG35XX Debug BSP V3,
USB identity and no port owners verified through SSH without opening UART.
Global known_hosts had an older host key; the previously saved task-specific
strict known_hosts matched the offered key and authenticated successfully.
No host-key verification bypass or registry change was performed.

Both candidate binaries matched BUILD_INFO and SHA files in
`firmware/dist/20260908-eight-node-capabilities/`:

- TX314240B: fd2ab968acb84b5428911ae651ac97c5d387102196aa707c2dc4c570e9c1e69d
- RX2 UART308800B: e7b4bd44f90a11aa1509aee9bcb0d165d0c0e06f2f387eb1c40ba8d9836184cc

Each was attempted once via guarded exclusive SSH UART relay and esptool5.0.2,
ESP8266 ROM/no-stub,115200,no-reset before/after,write-flash0x0,keep flash options.
No pre-read, Flash readback, reset or application capture was issued.

Both attempts failed at ROM synchronization: `No serial data received`, exit2.
Neither reached erase/write; no firmware was updated and MCU identity was not
observed. Logs: `build/20260908-v020-tx-rom-write.log` and
`build/20260908-v020-rx2-rom-write.log` (relay logs beside them).
Both agent-owned relays stopped; final host check `BOTH_UARTS_RELEASED`.

USB enumeration and successful SSH do not establish working target UART/BOOT.
User must re-establish BOOT and confirm signal wiring before a new attempt.
RX2 image defaults GPIO1TX/GPIO3RX only when settings storage uses defaults;
valid pre-existing persisted settings take precedence. No settings erase was
authorized or attempted.

## User-authorized TX-only retry (ACM1)

User requested TX first on ACM1. Same SHA-verified0.2.0 TX binary and guarded
no-reset/no-stub ROM write were attempted once. It again failed during sync:
`No serial data received`, exit2, before erase/write. No MCU identity observed,
no reset/readback, RX2 untouched. TX relay stopped and `TX_UART_RELEASED` checked.
Log: `build/20260908-v020-tx-rom-write-retry1.log`.

## User confirmed BOOT again: TX retry and transport alternative

TX-only retry2 again failed at sync before erase/write. A second bounded attempt
replaced the local PTY with a loopback-only raw TCP socket to the same guarded
SSH/ACM1 relay; this also returned no serial data, exit2. Logs:
`build/20260908-v020-tx-rom-write-retry2.log`,
`build/20260908-v020-tx-rom-write-socket.log`, and matching relay logs.

Live SSH hostname, USB if02→ACM1 mapping and free port were confirmed. Independent
read-only relay review found no definite framing/secret-output/SSH PTY defect;
this does not prove physical UART transmission. Socket alternative removes the
local PTY as a required cause but does not eliminate adapter/wiring/shared relay
faults. Do not conclude that the user failed to enter BOOT from absent replies.
Agent relays stopped, TX_UART_RELEASED confirmed, RX2 untouched, no Flash read or
reset issued. Next discriminator requires user-assisted ACM1 TX/RX loopback with
target disconnected; not performed or implicitly authorized here.

## Host reboot retry

User rebooted the RG35XX host and authorized another TX attempt. Live uptime
35.71s, same host/adapter identity and if02→ACM1, no bridge or owner. First relay
guard correctly stopped before UART open because USB topology changed from
3-1 to1-1 after reboot. A new task-specific relay was pinned to the freshly
verified1-1 identity (no host-key bypass or persistent device change).
The corrected direct ROM write again failed at sync: no serial data, exit2,
before erase/write. Logs: `build/20260908-v020-tx-rom-write-post-host-reboot.log`
(guard failure) and `build/20260908-v020-tx-rom-write-reboot-corrected.log`
(actual sync attempt), with separate relay logs. Final TX_UART_RELEASED;
RX2 untouched, no Flash read or reset. Host reboot alone did not restore a
bootloader response; cause remains unproven pending physical loopback isolation.

## User-assisted ACM1 loopback: PASS

User confirmed adapter TX/RX short after being asked to disconnect the target.
The same post-reboot guarded SSH/ACM1 relay was tested directly through binary
stdin/stdout pipes, without flashing, BOOT commands or RX2 access. Three bounded
rounds returned exact matches:26/26 bytes (including control/SLIP bytes),256/256
ascending0..255,256/256 descending255..0; total538 bytes, no mismatch, no relay
stderr. Log: `build/20260908-acm1-loopback.log`. Relay stopped and
TX_UART_RELEASED confirmed.

This establishes end-to-end host/SSH/ACM1 adapter loopback at115200 in this
configuration. It does not validate target-connected electrical levels, common
ground, crossover wiring or ROM boot straps. Next step is to remove adapter
loopback, reconnect adapterTX→boardRX/adapterRX←boardTX and GND, then enter target
BOOT and obtain user confirmation before another direct write. No automatic
flash follows a successful physical loopback test.

## Reconnected target, user confirmed BOOT: still no response

User re-entered BOOT after successful loopback and authorized the next TX-only
attempt. Same SHA-verified image, ACM1 identity guard and direct no-reset/no-stub
ROM write again failed at sync with zero received serial data, exit2, before
erase/write. Log: `build/20260908-v020-tx-rom-write-post-loopback.log`.
Relay had no reported guard/SSH/socat error before cleanup. TX_UART_RELEASED
confirmed; RX2 untouched, no Flash pre-read/readback/reset. The preceding
successful adapter loopback must not be confused with target-connected link
validation. Board wiring, common ground, signal levels and boot straps remain
unverified; do not repeatedly claim that user did not enter BOOT.

## User moved TX to ACM0

Latest user explicitly reassigned TX toACM0, superseding earlier RX2=ACM0 mapping.
Read-only USB enumeration confirmed if00→ACM0 on1-1 and free port. Same verified
TX0.2.0 image was attempted once via newly guardedACM0 relay,115200,no-stub,
no-reset before/after. Again SYNC returned no serial data,exit2,before erase/write.
Log: `build/20260908-v020-tx-rom-write-acm0.log`. ACM0_RELEASED confirmed;
ACM1 and RX2 were not opened or changed. No Flash read or reset performed.

## Replacement adapter: TX write SUCCESS

User replaced the adapter and authorized another TX attempt. Only one adapter
enumerated:1a86:55d3 USB Single Serial,serial5C4C054004,if00→ACM0,USB3-1,
by-path platform-5101400.usb-usb-0:1:1.0. No bridge or UART owner was active.
New task-specific relay validated this identity before opening ACM0.

Same0.2.0 TX image314240B/SHA
fd2ab968acb84b5428911ae651ac97c5d387102196aa707c2dc4c570e9c1e69d
was written directly by esptool5.0.2 at115200,no-stub,no-reset before/after,
keep flash parameters,address0. ROM reported ESP8285H16,MAC[device MAC redacted],
matching the recorded TX. Erase0x00000000..0x0004CFFF took2.40s; padded314368B
write completed in35.5s,70.8kbit/s,exit0,`Staying in bootloader`.

Log: `build/20260908-v020-tx-rom-write-new-adapter.log`, relay log alongside.
No Flash pre-read/readback, no reset/application capture; EEPROM was not erased.
Relay stopped and TX_UART_RELEASED verified. RX2 untouched and not yet upgraded.
This proves completed ROM writing, not app startup/RF/OTA acceptance. The changed
adapter restored communication in this run, but does not isolate the previous
adapter's failure mechanism or negate its earlier successful loopback.

## User-authorized RX2 write with replacement adapter: SUCCESS

User then confirmed RX2 ready. Same replacement adapterACM0/if00/serial5C4C054004
and guarded relay wrote only RX2 default-UART0.2.0 image308800B, SHA
e7b4bd44f90a11aa1509aee9bcb0d165d0c0e06f2f387eb1c40ba8d9836184cc.
ROM reported ESP8285H16,MAC[device MAC redacted] (recorded RX2). Erase range
0x00000000..0x0004BFFF took1.95s; padded309248B written in35.3s,70.1kbit/s,
exit0,`Staying in bootloader`. Log:
`build/20260908-v020-rx2-rom-write-new-adapter.log`.
No pre-read/readback/reset/application capture; EEPROM untouched. UART_RELEASED
verified after relay cleanup. TX and RX2 now both have completed ROM writes of
0.2.0 candidates; actual startup, selected pin mode and RF remain unverified.
RX2 compile default remains GPIO1TX/GPIO3RX with OTA/config enabled; a valid
persisted setting would still override that default. No pin/settings erase was
attempted. User should remove BOOT short and power-cycle before runtime testing.
