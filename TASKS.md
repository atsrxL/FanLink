# FanLink task backlog

## P0 — software baseline

- [x] Define fixed 30-byte Control and RPM packets with application CRC.
- [x] Implement full-state 16-channel TX scheduler and poll 1–16.
- [x] Implement single-channel RX selection and poll-gated reply.
- [x] Implement host and RF fail-safe-to-full-speed behavior.
- [x] Implement stale sequence rejection, retry, gap and wrap behavior.
- [x] Add desktop-testable interfaces/controllers.
- [x] Add binary Host protocol and Python CLI.
- [x] Add a backward-compatible Host link-metrics frame with separate uplink and
  downlink RSSI for all 16 nodes.
- [x] Add native tests and Arduino/RadioLib stub syntax smoke.
- [x] Add PlatformIO environments for TX and RX 01–16.
- [x] Run clean PlatformIO TX build with real dependencies.
- [x] Run clean PlatformIO RX Node 01 and Node 16 builds.
- [x] Resolve real RadioLib 7.7.1 API/compiler differences without changing wire format.
- [x] Add PlatformIO CI after the first successful real build.

## P0 — exact hardware target

- [ ] Identify TX product, PCB revision and original ELRS target.
- [ ] Identify RX product, PCB revision and original ELRS target.
- [ ] Verify MCU, flash, oscillator and SX1280/SX1281 variant.
- [ ] Verify NSS, DIO1, RESET, BUSY, SCK, MISO and MOSI.
- [ ] Verify TCXO enable/frequency if present.
- [ ] Implement and verify PA/LNA/antenna-switch state table.
- [ ] Determine minimum and maximum safe RF output settings.
- [ ] Verify GPIO1/GPIO3 pads and all attached components.
- [ ] Document recovery flashing/unbrick procedure.
- [ ] Split generic build configuration into exact TX and RX board targets.

Product-level profiles are now identified as JHEMCU 2.4GHz Lite RX hardware
repurposed as FanLink TX and BETAFPV 2.4GHz Nano RX, using ExpressLRS targets
commit `c4bd7b823594c233e673828ab493a2f8319a756a`. The two combined identification
items remain open because the physical PCB revisions and original firmware
targets from the actual boards have not been read back.

## P0 — single-node bench bring-up

- [x] Implement and native-test the non-blocking BETAFPV Nano GPIO16 800 ms link-LED pattern.
- [ ] Verify GPIO16 polarity, visible double-flash/solid pattern, watchdog transition and invalid-packet behavior on the physical RX.
- [ ] Initialize SX1280 at minimum safe power.
- [x] Prove one-way 30-byte Control reception on the physical Node 01.
- [ ] Prove network ID, sequence and own-channel selection on a debug pin/log.
- [ ] Build the external open-drain PWM interface.
- [ ] Scope 25 kHz at duty 0/1/64/128/192/254/255.
- [ ] Measure GPIO1 boot waveform and verify hardware-default full speed.
- [ ] Measure fan Pin 3 Tach high voltage before MCU connection.
- [ ] Verify RPM against an independent reading.
- [ ] Enable one polled Reply and measure turnaround/airtime.
- [ ] Tune reply delay, receive timeout and slot period from measurements.
- [ ] Test host timeout, TX loss, RX reset, corrupt packet and wrong network ID.
- [ ] Run one TX + one RX continuously for at least 30 minutes, then 8 hours.

Current partial bench evidence: the already-flashed `0.1.2` RX showed a steady link
LED, responded to duty changes, and returned fresh Node 01 RPM/status frames. This
proves Control reception and a functioning reply path, but not the LED search pattern,
reply timing/airtime, exact power, PWM waveform, Tach voltage/calibration, or the new
`0.1.3-dev` Node 01 pin/polarity/RSSI behavior. A logical duty of `0` still produced
about 2787 RPM on the old image, motivating the Node 01-only polarity correction.

## P0 — multiple nodes

- [ ] Bring up two uniquely labelled node IDs.
- [ ] Confirm an unpolled node never transmits.
- [ ] Confirm a missing node cannot block later slots.
- [ ] Bring up at least four nodes and check channel isolation.
- [ ] Deliberately duplicate a node ID and document collision symptoms.
- [ ] Validate complete 1–16 logic with hardware or an RF test fixture.

## P1 — reliability after measured baseline

- [x] Add TX counters for radio begin/transmit, RX timeout/CRC/I/O, invalid replies and accepted replies.
- [ ] Add RX counters for malformed packets and stale sequence rejects.
- [ ] Add parser/property fuzz tests.
- [ ] Determine whether bounded blocking RadioLib calls satisfy the measured 10 ms slot.
- [ ] Replace only the radio adapter with an asynchronous DIO1 state machine if measurements require it.
- [ ] Quantify packet error rate, jitter and telemetry freshness.
- [ ] Add watchdog/brownout tests.
- [ ] Design rendezvous + FHSS as a separate protocol milestone.
- [ ] Complete regional RF compliance review.

## P2 — optional after stable hardware

- [ ] Compile-time metadata/status query.
- [ ] Persistent node provisioning, only if 16 labelled binaries are operationally unacceptable.
- [ ] Optional CRSF Host Adapter that leaves the FanLink air protocol unchanged.
- [x] Local TX Host dashboard with 16-channel duty, status/RPM telemetry, separate
  uplink/downlink RSSI and RF diagnostics (Host↔TX and old-firmware Node 01 RF/RPM
  paths observed; new RSSI frame still requires flashing).
- [ ] Firmware update strategy.

Do not move P2 work ahead of unresolved P0 safety or hardware items.
