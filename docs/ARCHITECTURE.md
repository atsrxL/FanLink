# FanLink architecture

## 1. Layering

```text
Host application / pc/tools/fanlink_cli.py
        │ FanLink binary serial protocol
        ▼
firmware/src/main_tx.cpp
        │
        ▼
TxController
  - requested duty[16]
  - host watchdog
  - sequence and poll scheduler
  - per-node telemetry table
        │ IRadioPort
        ▼
RadioLibSx1280Port (replaceable adapter)
        │ fixed 30-byte Control / RPM Reply
        ▼
RadioLibSx1280Port
        │ IRadioPort
        ▼
RxController
  - envelope/network/sequence validation
  - duty[node_id-1]
  - link watchdog
  - poll-gated reply
        ├───────────────┐
        ▼               ▼
IFanOutput          ITachometer
Esp8266FanOutput    Esp8266Tachometer
```

The common/controller layer has no Arduino, SPI, RadioLib or GPIO types. Hardware replacement should occur behind the four interfaces in `interfaces.hpp`.

## 2. TX slot state machine

One `runOneSlot()` performs:

```text
sample host watchdog
  ↓
build full duty[16] + current poll_id + sequence
  ↓
transmit 30-byte Control
  ↓
receive one 30-byte Reply for a bounded window
  ↓
accept only exact network/node/sequence match
  ↓
update node record, or increment missed poll
  ↓
sequence++, poll_id wraps 16→1
  ↓
delay until the nominal slot boundary
```

Control transmission never depends on reply success. One missing node cannot block later nodes.

## 3. RX state machine

One `runOnce()` performs:

```text
check link watchdog; force full speed if expired
  ↓
receive exactly one 30-byte candidate Control
  ↓
validate length, magic, version, type, CRC, network
  ↓
validate modular sequence ordering
  ├─ stale: ignore, do not refresh watchdog
  └─ first/new/exact retry: refresh link and apply own duty
                         ↓
                   poll_id == node_id?
                     ├─ no: return to receive
                     └─ yes: delay, sample Tach, send Reply
```

An identical Control packet may be retried with the same sequence. Reusing a
sequence with changed network, poll, flags or duty content is rejected. A
sequence more than half the 16-bit space behind the latest accepted value is
rejected as stale. `65535→0` is normal forward progress. When the RF watchdog
expires, RX clears the old sequence baseline so the next valid packet can
reacquire after a TX reboot or a long outage.

## 4. Why every downlink carries full state

- A lost packet needs no command retransmission queue.
- The next valid packet self-heals all channels.
- RX keeps no per-channel version table.
- Control does not wait for telemetry acknowledgements.
- Sixteen bytes are small enough to justify the simpler failure model.

## 5. Why both air directions are 30 bytes

The starter RadioLib adapter uses a known fixed receive length. Equal-sized Control and Reply frames let both TX and RX receive into the same size without runtime packet-length discovery. Reply bytes 17–27 are reserved and zeroed. This trades eleven bytes of airtime for a simpler first hardware bring-up.

## 6. Fixed frequency first

The largest unknowns are the actual module's pin map, BUSY/DIO behavior, TCXO and RF front end. Proving a fixed-frequency single-node loop isolates those variables. Rendezvous and FHSS must be a later protocol milestone with separate synchronization tests.

## 7. Timing and memory rules

- Fixed-size arrays only in slot paths.
- No Arduino `String` or recursion in firmware control paths. RadioLib is built
  with `RADIOLIB_STATIC_ONLY=1` and a 64-byte payload buffer so the 30-byte
  packet SPI path does not allocate from the heap.
- Tach ISR records only timestamp/period.
- Public controllers do not print.
- RX does not initialize Serial because GPIO1/GPIO3 are intended for fan I/O.
- Blocking RadioLib calls are a bring-up baseline. All SPI/BUSY and TX_DONE
  waits used by FanLink are hard-capped at 25 ms, below the 500 ms RX link
  watchdog, but this is not proof that the 10 ms slot target is met.
- If measurements show inadequate margin, replace only `IRadioPort` with an asynchronous state machine.
