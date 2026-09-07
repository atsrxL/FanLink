# Architecture decision log

## ADR-001 — Standalone FanLink application

**Decision:** Build a small dedicated application and radio HAL rather than deleting features from the full ExpressLRS application tree.

**Reason:** The required behavior is only a 16-byte full-state broadcast plus one explicitly polled short response. Retaining ELRS routing, generic telemetry, MSP, Lua, WebUI and model state would add coupling without product value.

**Consequence:** Standard ELRS OTA/binding/telemetry interoperability is intentionally lost.

## ADR-002 — Full state in every Control packet

**Decision:** Send all 16 duty bytes every slot.

**Reason:** A lost downlink self-recovers on the next packet without acknowledgements, delta history or per-node control state.

## ADR-003 — Explicit one-node polling

**Decision:** Each Control packet carries one `poll_id`; only that node may transmit.

**Reason:** Shared network identity does not create safe multi-node uplink arbitration. Explicit polling prevents designed-in collisions when node IDs are unique.

## ADR-004 — Fail-safe means full speed

**Decision:** Host timeout, RF timeout, reset and initialization failure bias toward 100% fan command.

**Reason:** Loss of cooling is the hazardous default. Software behavior is complemented by an external open-drain stage that releases the fan control line when the MCU is not driving.

## ADR-005 — Compile-time node IDs for v0.1

**Decision:** Provide RX build environments 01–16 instead of a provisioning UI.

**Reason:** This avoids flash configuration, WebUI, discovery and a new provisioning protocol during bring-up.

## ADR-006 — Fixed 30-byte packets in both directions

**Decision:** Control and RPM Reply are both exactly 30 bytes.

**Reason:** The blocking radio adapter can use one known receive length on both roles. The unused Reply bytes are reserved and CRC-protected.

**Consequence:** Slightly more reply airtime. This can only be changed in a new protocol version after measured need.

## ADR-007 — RadioLib 7.7.1 starter backend

**Decision:** Depend on RadioLib rather than copying ExpressLRS radio source.

**Reason:** It gives the project a compact replaceable SX1280 adapter and avoids importing a large application stack.

**Consequence:** Exact commercial-board PA/LNA/RF-switch/TCXO behavior remains
target-specific. RadioLib 7.7.1 is integrated through a fail-closed pre-build
patch for target-selected regulator mode and bounded TX waits; upgrading the
dependency requires re-reviewing those source-checked changes.

## ADR-008 — Fixed-frequency FLRC first

**Decision:** v0.1 uses one fixed SX1280 FLRC configuration.

**Reason:** Hardware pinout, RF switching and turnaround must be proven before adding synchronization and hopping.

**Consequence:** The baseline is not claimed suitable for interference robustness or final regulatory deployment.

## ADR-009 — Binary Host protocol, CRSF deferred

**Decision:** Use a small length-delimited binary serial protocol with CRC for Host↔TX.

**Reason:** It is deterministic, fixed-buffer, easy to test and carries exactly the required duty/status data.

**Consequence:** Existing CRSF tools are not directly compatible. A future CRSF adapter may translate at the Host boundary while leaving FanLink air packets unchanged.

## ADR-010 — Blocking radio calls as a measured baseline

**Decision:** Start with bounded blocking `transmit` and `receive` behind
`IRadioPort`. Cap the RadioLib SPI/BUSY and TX_DONE waits at 25 ms so a driver
fault cannot permanently prevent RX from returning to its 500 ms link watchdog.

**Reason:** It minimizes first bring-up state. The 10 ms target must be measured rather than pre-optimizing an asynchronous driver.

**Consequence:** A single fault can still overrun the nominal 10 ms slot, and
software cannot replace independent thermal protection. Host service/status
jitter, RF turnaround and BUSY fault injection must be measured; replace only
the adapter if margin is insufficient.

## ADR-011 — Reject clearly stale sequence values

**Decision:** Use modular 16-bit ordering. A duplicate value is accepted only
when the complete Control packet is identical to the last accepted packet;
forward deltas below half the sequence space are accepted; older values are
rejected without refreshing the watchdog. An RF watchdog expiry clears the old
sequence baseline so a restarted TX can be acquired again.

**Reason:** Delayed or contradictory packets must not overwrite a newer fan
command or gain stale poll permission, while a TX restart must not leave RX
stuck in failsafe for up to half the sequence space.

## ADR-012 — Local loopback TX control panel

**Decision:** Provide the user-requested Host dashboard as a local Python HTTP
service and static browser UI on `127.0.0.1`, reusing the existing binary Host
protocol without adding Wi-Fi or a web server to the ESP8285 firmware.

**Reason:** The operator needs one view for all 16 requested duty values, TX
state, RPM telemetry and RF diagnostics. Keeping it at the Host boundary avoids
changing the frozen air protocol and preserves the TX watchdog.

**Consequence:** The API has no authentication and therefore remains loopback
only by default. A browser/API acknowledgement proves only Host command
acceptance; it does not prove RF delivery, PWM waveform, actual RPM or thermal
safety.

## ADR-013 — Separate Host link-metrics frame

**Decision:** Preserve the version-1 102-byte `STATUS_SNAPSHOT` and add a new
`LINK_METRICS_SNAPSHOT` (`0x82`) carrying one uplink and one downlink RSSI byte
per node. The 30-byte air packets remain unchanged because the RX reply already
contains its downlink RSSI measurement.

**Reason:** Existing Host parsers must not silently reinterpret a changed
record size. Separating the 32-byte metrics payload keeps legacy status framing
compatible while exposing both directions unambiguously.

**Consequence:** A Host that wants link metrics must parse both frame types and
correlate them by node ID/order. `-127` is unknown; a stale RSSI value must not
be presented as a current connectivity guarantee.
