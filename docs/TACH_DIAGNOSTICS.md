# RX tach diagnostics (candidate, not deployed)

Purpose: distinguish a spinning fan with missing tach input from pulse filtering
or queue loss, without changing the tach estimator or resetting its state.
This cannot recover pulse history that older firmware never recorded. Installing
the candidate necessarily reboots the MCU; preserve current observations first.

## Interpretation

Take at least two snapshots in the same boot epoch. Counters saturate and reset
at boot; a decrease means do not calculate a delta across that interval.

| Observation | Meaning / next check |
| --- | --- |
| enabled=false | Tach is not initialized, including UART mode; not evidence of a broken wire |
| rawEdges stays0, edge age unknown | No falling-edge interrupt observed; inspect mode, input and electrical signal |
| rawEdges stops, edge age increases | Signal stopped reaching the ISR |
| rawEdges increases, validPeriods stays flat, rejects increase | Pulses reach software but estimator rejects them; inspect noise, timing and filter assumptions |
| validPeriods increases | New accepted RPM estimates are being produced; correlate fresh RPM and age |

The raw count includes edges dropped by a full ISR queue. Accepted periods count
new trusted estimator updates, not all plausible intervals and not repeated
snapshot reads. Warmup is not rejection. These counters do not partition every
edge one-to-one. Without an electrical trace, lack of ISR events cannot by itself
distinguish missing voltage edges from pin/interrupt configuration issues.

Do not interpret a disconnected diagnostic response as zero counters. Match
network, node, session, CRC and request sequence; timeout is unknown. Never
compare counters from different receivers or boots. Saturated counts no longer
give rates. Raw last-edge age is independent of the last trusted RPM age.

## Safety / deployment

No new automatic query loop or persistent setting is introduced. Existing
Control/RPM/STATUS frames, RF parameters, node polling and protections stay
unchanged. New TX and RX firmware are both needed for this query; unsupported
old firmware may simply time out. No C6 or HA entities are added by this change.
The existing C6 TCP passthrough can carry the Host query, but entering passthrough
is a separate explicit maintenance action that interrupts normal control and
requests full speed. It is not a passive capture and must not be enabled merely
to check TCP availability. This turn does not enable it or deploy firmware.

## Reader (only after separately authorized TX/RX deployment)

From the repository root, with sole ownership of the TX UART:

```sh
uv run --with pyserial python pc/tools/fanlink_tach.py \
  --port /dev/cu.YOUR_VERIFIED_TX --network 0x4B4E4C46 --node 1 --count 3
```

Alternatively use `--port socket://fanlinkc6.local:5000` only after explicitly
entering C6 passthrough and confirming device identity. Do not probe by opening
and closing the TCP port; that ends the single-client passthrough session.
The reader does not send heartbeat or restore PWM, and does not change modes.
An exclusive connection without host heartbeat lets the existing watchdog
request full speed. UART reset wiring must be disconnected; software cannot
guarantee every adapter has glitch-free open behavior.

Output is one matched JSON snapshot per query. `last_edge_age_ms: null` means
never observed,65534 means at least65.534s, not an exact age. Compare successive
raw_edges/valid_periods/filter_rejects; no automatic diagnosis is inferred from
a single snapshot. An error/timeout stops acquisition, not zero-filled output.
