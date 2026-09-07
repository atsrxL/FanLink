# FanLink test plan

## 1. Desktop regression

Required on every common/controller change:

```bash
./firmware/scripts/run_native_tests.sh
python3 pc/tools/fanlink_cli.py --self-test
python3 -m py_compile pc/tools/*.py firmware/tools/*.py firmware/scripts/*.py
```

Required after platform/Arduino edits:

```bash
./firmware/tools/run_firmware_syntax_smoke.sh
```

Coverage currently includes CRC, exact packet lengths, round trips, parser
resync, poll ordering, exact reply matching, both watchdog boundary values,
blocking receive crossing the RX deadline, force-full, sequence
gap/stale/exact-retry/reacquisition/wrap, wrong network and corrupt CRC.

## 2. Real dependency build matrix

| Environment | Required result |
|---|---|
| `fanlink_tx_esp8285` | clean build/link |
| `fanlink_rx_node01` | clean build/link |
| `fanlink_rx_node16` | clean build/link |
| all 16 RX environments | reproducible labelled binaries |

Record PlatformIO version, resolved platform/framework version, RadioLib
version, full logs, source-manifest digest and binary SHA-256. The reviewed local
run path is `dist/20260904-jhemcu-lite-betafpv-nano-reviewed/`.

## 3. Board initialization

For each supported PCB revision:

- flash/recovery works;
- boot straps remain valid;
- SPI ID/commands succeed;
- BUSY does not hang;
- DIO1 events occur;
- TCXO and RF front end enter safe states;
- minimum-power near-field packet exchange succeeds.

## 4. Air protocol tests

- correct network ID accepted;
- wrong network ID ignored;
- bad application CRC ignored;
- wrong type/length/version ignored;
- stale sequence ignored and watchdog not refreshed;
- identical duplicate Control may retry, but changed content with the same sequence is rejected;
- after RF watchdog expiry, the next valid Control establishes a new sequence baseline;
- `65535→0` accepted;
- unpolled RX produces no RF transmission;
- polled reply echoes exact sequence/node;
- delayed/wrong reply does not update another node.

## 5. PWM measurements

At the MCU output and fan Pin 4, measure duty values:

```text
0, 1, 64, 128, 192, 254, 255
```

Record:

- frequency and duty error;
- external transistor inversion;
- rise/fall times;
- RF transmit/receive disturbance;
- GPIO1 boot pulse;
- RX reset and link-loss transition to full speed.

## 6. Tach tests

- measure static high level before MCU connection;
- validate pulses/revolution;
- compare RPM at low/mid/high speed with an independent source;
- unplug Tach and confirm timeout;
- inject bounce/noise and verify minimum edge filtering;
- verify ISR load at maximum expected RPM;
- verify stall grace and status bit.

## 7. Timing tests

Use debug pins/logic analyzer to measure:

- Control airtime;
- TX radio turnaround;
- RX decode + reply delay;
- Reply airtime;
- TX receive completion;
- total slot time;
- worst-case Host status emission impact.

A 10 ms slot is accepted only when worst-case margin is documented.

## 8. Multi-node tests

Start at 2 nodes, then 4, then 16/test fixture:

- channel isolation;
- strict one-responder behavior;
- missing node does not block;
- one noisy/failing node does not disturb control;
- telemetry freshness matches expected 160 ms sweep;
- duplicate node ID collision is detected operationally;
- packet error and missed poll counters are recorded.

## 9. Fault injection

- Host process exit;
- serial cable removal;
- TX reset/power loss;
- RX reset/power loss;
- RF attenuation/interference;
- corrupt packets;
- wrong network ID;
- Tach open/short;
- RF front-end enable fault;
- brownout/watchdog reset;
- firmware update/reflash while fan system is powered.

Every failure must end in a documented safe fan state.

## 10. Endurance

Minimum progression:

1. 1 TX + 1 RX, 30 minutes;
2. 1 TX + 1 RX, 8 hours;
3. 1 TX + 4 RX, 8 hours;
4. intended node count, intended thermal load, 24 hours or longer.

Record resets, missed polls, stale telemetry, RPM anomalies and temperatures.
