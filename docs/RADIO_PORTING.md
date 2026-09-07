# Radio porting notes

## 1. Public radio contract

`IRadioPort` deliberately exposes only:

```cpp
bool begin(uint32_t networkId);
RadioResult transmit(const uint8_t* data, size_t length);
RadioRxResult receive(uint8_t* data, size_t capacity, uint32_t timeoutMs);
int lastError() const;
```

Protocol/controller code must not include RadioLib types or board GPIO details.

## 2. Current starter adapter

`RadioLibSx1280Port` currently assumes:

- SX1280-compatible radio;
- FLRC modulation;
- fixed frequency;
- fixed 30-byte packets in both directions;
- blocking transmit and receive;
- a 25 ms hard cap on SPI/BUSY and TX_DONE waits;
- static RadioLib buffers sized for the 30-byte FanLink path;
- a four-byte sync word derived from `network_id`;
- RadioLib hardware CRC plus FanLink application CRC;
- optional two-pin RXEN/TXEN support via `setRfSwitchPins`.

`firmware/platformio.ini` supplies separate JHEMCU-Lite and BETAFPV-Nano target values.
They are reviewed build profiles, but remain physically unverified until each
actual PCB revision is inspected.

## 3. Resolved RadioLib 7.7.1 integration

The real dependency build confirmed the `Module` constructor, `beginFLRC`,
four-byte sync word, two-byte CRC, fixed-length packet call, receive timeout
units and RSSI APIs used by this adapter. Upstream 7.7.1 selects DC-DC during
`SX128x::config()` and has unbounded/incorrectly derived TX waits for this FLRC
path. `firmware/scripts/platformio_patch_radiolib.py` therefore checks the exact library
version and source snippets, then applies the target-selected regulator and
25 ms BUSY/TX_DONE hard limits before compilation. Any mismatch fails the build
instead of silently applying a partial patch.

Hardware work must still verify:

- RSSI availability after FLRC reception;
- standby/sleep state after blocking operations;
- BUSY/DIO1 fault behavior and driver error codes on the actual board;
- the measured 10 ms timing margin.

Do not “fix” an API mismatch by removing length checks or application CRC.

## 4. RF front end

Many ELRS receiver boards have external PA/LNA, RF switches, TCXO enables or shared control pins. Two generic RXEN/TXEN pins may be insufficient. If so, implement a board-specific adapter/state table:

```text
RESET/BOOT
IDLE
RX_PREPARE
RX_ACTIVE
TX_PREPARE
TX_ACTIVE
FAULT_SAFE
```

Measure settle time between GPIO state changes and actual RF activity. A wrong truth table can produce no link or unsafe RF operation.

## 5. Fixed-length packet rationale

Both air directions are 30 bytes. The current adapter passes the caller's exact capacity as the known receive length and reports that length on success. If a future driver uses dynamic packet length, retain exact 30-byte validation in `decodeControlPacket`/`decodeRpmPacket`.

## 6. Timing instrumentation

Expose temporary debug pins for:

- TX slot start;
- radio transmit start/end;
- receive window start/end;
- RX packet accepted;
- RX reply start/end;
- RF-switch state.

Capture at least 1,000 slots and calculate worst-case rather than average timing.

## 7. When to go asynchronous

Only replace the blocking adapter after measurements show one of:

- slot overrun;
- unacceptable control jitter;
- Host serial starvation;
- watchdog/yield issues;
- insufficient turnaround margin.

Keep `IRadioPort` or introduce a reviewed asynchronous interface; do not leak driver state throughout controllers.

## 8. FHSS milestone

FHSS is not merely a frequency counter. A later design needs:

- a deterministic hop table shared by all nodes;
- a rendezvous/reacquisition channel or scan strategy;
- sequence-to-hop mapping;
- missed-packet recovery;
- resynchronization after RX reboot;
- RF switch and PLL settle timing;
- tests for long loss bursts and wrap;
- regional channel/power review.

Add it only after fixed-frequency single/multi-node behavior is stable and measured.
