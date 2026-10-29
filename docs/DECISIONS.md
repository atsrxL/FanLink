# Architecture decision log

## ADR-026 — Invalid storage uses validated firmware defaults

2026-09-08 user requests removal of the settings Safe mode after RX1 remained
radio-online but could not drive PWM or report Tach. Invalid or unreadable
persistent storage now selects the validated compiled pin mode and power, not
disabled pins and minimum power. RX01 defaults PWMSWAP; RX2 UART builds default
UART. Valid saved settings still override these defaults. Never use a different
node/board/network record or implicitly write Flash on boot/Query.

Keep flags bit1 as an invalid-storage diagnostic: with valid modes0..2 it means
operating on compiled defaults until an explicit successful setting save. Legacy
mode254 remains decodeable for explicit repair of installed old images, not an
operating fallback for bad storage. Invalid compiled configuration still fails
closed; it must not drive arbitrary pins or accept mutations.

C6 accepts a validated fallback Query with an explicit warning and permits an
explicit mode save, including selecting the same default mode. A legacy254 Query
may authorize explicit repair without claiming a valid current mode. Preserve
identity/capability/session checks, ACK plus post-reboot Query, and no automatic
Set retries. This supersedes the corrupt-storage pin-disable rule in ADR-023.
Host/RF500ms watchdogs, startup/maintenance full speed, CRC checks and hardware
overtemperature protection are unchanged. Correct node/variant and external
wiring remain essential: fallback is not automatic wiring discovery.

## ADR-025 — Native eight-slot Host v2, no legacy channel adapter

2026-09-08 user explicitly requests eight-slot framing and removal of redundant
channel code. This supersedes ADR-023 wire16 preservation. Host protocol version
becomes2, SET_ALL8, STATUS54 (6+8*6), METRICS16 (8*2); other payloads unchanged.
Remove inactive9..16 arrays, padding, overloads and branches, with strict1..8
validation including OTA. No dual-version parser or automatic fallback.

RF remains version1 and fixed30 bytes:8 duty bytes followed by8 reserved bytes
(sent255, ignored). This preserves existing on-air bytes and avoids changing
fixed-length radio reception shared by30-byte OTA/settings packets. Reserved
transport bytes are not channels. Serial max payload128 likewise remains for
OTA blocks and bounded diagnostic text, not unused node allocation.

C6 and PC must migrate with TX; mismatched Host versions are rejected and
cannot refresh watchdogs. RX on-air compatibility is unchanged, so immediate
RX flashing is not needed. Do not deploy this candidate automatically. Use
archived v1 tooling or wired ROM for upgrading oldTX; v2-only tools cannot
maintain/control an oldTX. Maintain full speed and independent cooling during
the coordinated TX/C6 transition; no temporary dual-stack product code.

Keep125ms full-state polling,500ms watchdogs,5miss/6s presence and2.5s stale
policy unchanged. Test old-version rejection, exact lengths and channel8/9
boundaries. Shorter serial envelopes do not establish an RF reliability fix.

## ADR-024 — Separate Host presence tolerance from safety and freshness

2026-09-08 user requests a safety/recoverability assessment and reasonable
consecutive-miss tolerance for DIY use. Historical node2 captures show up to
four consecutive missed polls and 4.8s reported age, without sampled MCU
restart. This supports a bounded reporting grace, not a radio reliability claim.

Decision: keep existing wire fields, strict CRC/identity/sequence checks,
full-state125ms broadcasts, RX/Host500ms failsafes and radio recovery. Host
presence, once established, tolerates missed_polls<5 AND combined node age
<6000ms only while Host STATUS remains trusted/fresh. Either limit makes it
offline. Never-seen/inactive nodes and transport loss are immediately offline.
Connection establishment/recovery requires fresh non-stale miss-zero evidence;
old snapshots cannot alone restore presence. At age>=2500ms or bit7 set, data
remain stale independently of presence and are not published as current RPM,
applied duty or RSSI. Keep real alarm bits visible.

The existing TX already increments misses once per failed own-node poll and
clears them only on validated matching replies. Therefore no TX/RX firmware
or air protocol update is needed; adapt PC and the separately owned C6 host.
Do not count other nodes, individual CRC events or STATUS frequency as misses.
The6s age cap covers paused polling/maintenance/recovery where miss stops.
This supersedes historical prohibitions on changing Host offline policy only
for reporting; it does not weaken safety or conceal telemetry staleness.

Use remains conditional noncritical DIY with independent overtemperature
protection and physically verified default-full hardware. No OTA/Flash or
C6 deployment is authorized by this code-only implementation.

## ADR-023 — Eight nodes,125ms polling and remotely configurable firmware

2026-09-08 user explicitly requests max8 receivers, one poll per125ms, TX software
BOOT/local OTA/RX OTA capped at5KiB/s and configurable TX power, plus RX OTA,
persistent remotely selected UART/PWM/PWMSWAP and RX transmit power. User confirms
UART=GPIO1TX/GPIO3RX, PWM=GPIO1PWM/GPIO3Tach, PWMSWAP=GPIO3PWM/GPIO1Tach; pin
mode changes persist and reboot RX. UART has no fan output but retains RF/OTA.

Keep30-byte normal RF and legacy16-entry Host envelopes; only nodes1..8 valid,
channels9..16 are inert/full/offline. Normal polls start on125ms cadence using
cooperative scheduling, not125ms blocking host-input sleeps. RX control watchdog
remains500ms because every downlink carries all8 duties. TX telemetry freshness
becomes2500ms for the1s per-node reply cycle; old C6's independent1000ms check
requires a separately deployed adaptation. Do not claim the old C6 UI is ready.

Add RF config request/reply0x40/0x41 and Host forwarding0x15/0x93, fixed30B with
CRC and explicit network/session/target/op. Exact contract is in
`docs/CONFIG_WIRE.md`. Query advertises capabilities, profile, mode, power,
8-node/125ms limits and5120B/s upload cap. Commands are manual, not automatic
power control. Validate against board profiles; never expose arbitrary GPIOs
or unchecked PA input values. Serialize setting changes with OTA/lifecycle,
send a full-speed preamble, and keep outputs safe through Flash writes/reboot.

Power/pin settings are checked persistent records, with safe handling of
corruption and idempotent retry. Runtime selections do not change image target
identity. Pin changes apply only after an acknowledgement grace and reboot;
old UART diagnostic images without OTA/config still require wired migration.
OTA cap is5120 image bytes/s (including retries), not a measured throughput
guarantee or a PHY-rate increase. Existing OTA validation and non-A/B power-loss
limits remain. No automatic flash, device reset or C6 task mutation this turn.

## ADR-022 — Preserve TX receive-error evidence without changing RF (2026-09-08)

The user requests continued repair at the intended approximately 30 cm working
distance. Distance separation is not a prerequisite and receiver overload is
not established. Paired evidence shows RX02 keeps replying while TX loses valid
telemetry; existing CrcError combines sync, IRQ CRC and packet-status CRC causes.

Append optional, zero-initialized receive evidence to the platform result:
checked raw IRQ and packet status with explicit validity, independent error
causes, software event-observation and SetRx-command-completion timestamps.
These timestamps are not physical RF/DIO edges. Preserve metadata before
rearming and never present previous-reception metadata as the current event.
Do not add SPI transactions to normal receive processing or read rejected
payloads. Legacy ports remain valid without this evidence.

TX records overlapping cause counters and the latest error separately from
successful telemetry. Rate-limited RFC/RFE text uses the existing optional
0x81 Host envelope and runs after controller work. Existing binary snapshots,
RF/RFN/RFT grammar, bad-packet rejection, watchdogs, RF parameters and RX2 UART
pin assignment remain unchanged. Unknown metadata stays unknown. Neither
diagnostic counters nor error RSSI refresh telemetry or establish a sender.

The candidate includes the previously developed, unflashed TX local maintenance
support. Building or simulation does not prove the field disconnect repaired;
deployment requires separate current TX BOOT authorization. The already-running
RX02 diagnostic image remains wire-compatible and need not be replaced.

## ADR-021 — RX02 UART-only diagnostic build (2026-09-08)

The user explicitly confirms RX2 GPIO1/3 must remain UART TX/RX for simultaneous
RX/TX diagnosis, not PWM/Tach. Add an opt-in separately named node02 diagnostic
environment with no physical fan or tach adapter and no OTA identity/updater.
It still participates in normal node2 poll/replies with unchanged RF parameters
and protocol, but reported full duty/zero RPM are diagnostic placeholders, not
physical fan measurements. Never connect its UART pads to a fan control/tach
circuit; this is not a cooling controller or failsafe hardware acceptance.

Only the dedicated entry point enables Serial. Emit bounded, rate-limited,
whole-line FIFO-guarded text after radio/controller work, outside ISRs and reply
turnaround. Drop diagnostic output under pressure rather than delaying RF.
No automatic action is triggered by received UART bytes. Retain normal RX
entry point, PWM/Tach mapping and OTA behavior for all existing environments.
The diagnostic image is wired-only and must not be mistaken for normal RX02.

## ADR-020 — User-authorized TX local update and boot entry (2026-09-08)

The user requests TX online firmware replacement/restart and command-triggered
UART boot upgrade. Add separate Host-only maintenance messages, not RF packet
changes or Wi-Fi. Reuse bounded OTA staging, complete-image verification and
explicit Commit, with TX role/network/profile validation. TX local maintenance
and RX RF OTA are exclusive. Before flash writes or restart/boot, discard old
Host control and broadcast full speed for at least 600 ms; maintain full speed
until fresh post-maintenance commands. Reject malformed/conflicting requests.

Use pinned ESP8266 Arduino Core 3.1.2 ESP.rebootIntoUartDownloadMode() for ROM
UART entry, not custom register manipulation or an assumed GPIO0 reset circuit.
This requires a running compatible application. Dead/corrupt firmware still
requires manual BOOT recovery. Command ACK is not proof that ROM sync or a new
application succeeded. Boot entry never implicitly erases or flashes a file.
No automatic DTR/RTS reset, pre-read, or Flash readback is introduced.

Online Commit retains the existing eboot non-A/B power-loss boundary. No C6
deployment, physical flash, reset or boot command is authorized by this code
request. Add host CLI, negative/timeout/duplicate tests and real builds before
separate hardware acceptance.

## ADR-019 — Bounded OTA block relay, 3 KiB/s target (2026-09-08)

The user requests a firmware targeting 3 KiB/s effective OTA. Keep the 650 kbps
PHY, fixed 30-byte packets, normal Control/RPM and legacy OTA operations unchanged.
Add Host 0x11 carrying one 100-byte image block: a legacy-shaped, CRC-protected
30-byte operation-7 Fragment followed by its remaining (at most 90) image bytes.
TX relays ten-byte fragments locally with a 1000 us post-transmit rearm guard,
then requests a single operation-8 BlockFinish cumulative ACK. RX buffers at most
100 bytes and a bitmap; it only writes a complete, identically retransmitted
block. Retain the last block to detect altered retransmissions after ACK loss.
No fragment ACK is emitted, avoiding collisions with the next fragment. Partial
blocks do not refresh the 10-second progress watchdog. Normal and fast data
modes cannot mix within a session. Abort/MD5/explicit Commit stay unchanged.

The uploader retains legacy mode by default for migrating a 0.1.8 RX through a
new TX. Fast mode is explicit, paced at a maximum 3072 image bytes per second;
it retries an incomplete block in full with bounded attempts. This is a target
and rate cap, not demonstrated throughput: serial serialization, SSH latency,
radio rearm and flash-sector stalls may reduce actual speed. No PHY increase,
new dependency, control timing change, flash readback or C6 modification is made.

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
rejected without refreshing the watchdog. This initial reacquisition behavior
is superseded by ADR-014 for the reliability repair: expiry must not make a
single cached duplicate into a new low-duty command.

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

## ADR-014 — Radio-only recovery and fresh-control reacquisition

**Decision:** Keep the bounded blocking adapter and both version-1 wire formats.
Recover only the radio with `IRadioPort::begin`, preserving the fan/Tach setup,
TX sequence/poll order and the host command's original age. Retry failed radio
initialization with bounded backoff; distinguish hard I/O failure from normal
timeouts for absent nodes. Sustained absence of any valid peer packet also
triggers reconfiguration, because lost modem configuration can return successful
SPI operations without communicating.

Before RX radio recovery, force duty 255. Keep the last sequence baseline on
link expiry: a duplicate cannot refresh the watchdog or release failsafe. A
strictly newer valid frame may reacquire; a restarted TX with an older sequence
must establish a short-lived candidate followed by a strictly forward frame.
Cold boot still accepts its first valid frame. Healthy identical duplicates may
be answered when polled, but do not extend the age of the latest new control.

TX continues receiving after CRC/invalid replies within the original fixed
reply deadline, never granting a new full timeout for each rejected frame.

**Reason:** A recoverable SX1280 reset must not require a power cycle, unrelated
empty node slots must not cause repeated resets, and recovery must not replay
stale low PWM or reattach Tach interrupts. The production FanLinkC6 sends a full
16-channel command every 200 ms, so the existing 500 ms host watchdog remains
the compatibility boundary.

**Consequence:** Recovery can temporarily overrun a nominal slot; initialization
and retry frequency must be bounded and fault-tested. The two-frame restarted-TX
rule tightens acceptance without changing any wire bytes. It is not replay
authentication: network ID and CRC are still only filtering/integrity checks.
Actual RF loss, watchdog timing and long-duration hardware behavior still need
bench validation. No FHSS, power change or over-the-air flasher is introduced.

## ADR-015 — Retain reception between bounded software waits

**Decision:** Keep `IRadioPort`, the 10 ms slot, 1.2 ms reply delay, 5 ms TX
reply budget and all watchdog thresholds. In the SX1280 adapter use RadioLib's
single-packet receive with no hardware timeout (`SetRx(0)`) and a bounded
software wait. An ordinary wait
timeout leaves the radio listening; consume pending packets before rearming,
and rearm immediately after reading a packet (including CRC errors). Transmit,
reinitialization and hard/argument errors invalidate that receive state and
release the RF front end. TX still validates the current poll and sequence,
and a pending old packet cannot refresh either controller's watchdog.

**Reason:** The previous blocking receive explicitly returned to standby after
every timeout and every packet. Thus RX did not listen during controller work,
LED updates/yield or before the next receive setup. Short polling slices must
service safety checks without introducing a matching recurring RF blind gap.

**Consequence:** The LNA can remain in receive mode between calls, never the
PA in transmit mode. Do not use `SetRx(0xFFFF)` continuous RX: Semtech's
SX1280/81 Rev 3.2 section 16.1 documents an all-modem congested-traffic BUSY
lockup and recommends single RX with rearming. Section 11.6.5 specifies that
`SetRx(0)` waits for one packet without a hardware timeout, then enters standby.
RX FIFO/IRQ retention, timeout bounds, CRC rearm, hardware
fault cleanup and timer wrap require tests against real RadioLib with simulated
SPI/GPIO. Chip configuration, power, modulation and wire bytes stay unchanged.
Read IRQ, buffer length/offset, packet status and FIFO with checked SPI calls;
do not repeat unchecked metadata reads through RadioLib's `readData`. Check
packet-status CRC as well as the IRQ CRC bit (Semtech section 16.3). The source
is [Semtech SX1280/81 Rev 3.2](https://media.digikey.com/pdf/Data%20Sheets/Semtech%20PDFs/SX1280-81_Rev3.2_Mar2020.pdf),
sections 11.6.5, 14.3.3 and 16.1/16.3 (manufacturer document, distributor mirror).
This removes a code-level blind interval; it is not proof of the cause or cure
of the user's observed RF loss, nor uninterrupted reception during FIFO reads.

## ADR-016 — Per-node RF evidence without changing freshness policy

**Decision:** Add a bounded `RFN` text record using the existing optional 0x81
Host frame. Per node retain saturating counts for transmitted polls, accepted
replies, TX failures, unanswered non-I/O windows, CRC, I/O and invalid reply events.
Report the consecutive miss count, current telemetry age and synthesized status
alongside those counters. Round-robin only ever-seen nodes at at most one record
per second. Give the existing STATUS and METRICS frames priority over diagnostic
text; keep their periods and binary layouts unchanged.

**Reason:** Aggregate timeouts are dominated by the 15 unconnected slots in a
one-node installation. They cannot identify that node's reply loss, and changing
the C6's offline threshold would conceal missing evidence rather than fix it.

**Consequence:** An old Host can ignore the new text. Node CRC/I/O/invalid counts
describe events in that node's poll window, not proof that the corrupt packet
originated from that node. Matched-window poll/reply deltas, not lifetime totals
or global counters, are needed for a measured reply success ratio. Counters are
diagnostic only and cannot renew control or telemetry freshness.

## ADR-017 — Observed receive deadline and remaining reply turnaround

**Decision:** Keep the 5 ms TX window, 1200 us RX guard, 10 ms slot and all wire
layouts. Carry first software DIO observation through the adapter, distinct from
completed packet processing. TX validates observation time within the original
window and all packet identity checks; legacy adapters use completion time.
Use a microsecond receive budget rather than truncating its fractional millisecond.
RX waits only the remaining guard, refuses to start replies whose observed age
has reached the configured TX reply budget, and never renews the control watchdog
by FIFO/PWM processing time. Explicitly map sync-only FLRC error to DIO before
SetRx, then consume/rearm it rather than indefinitely retaining a software armed
state that no longer represents active reception.

**Evidence:** TX regression fails 6/649 with the old completion deadline and passes
693 after correction. RX timing injection passes 509 checks; reverting only its
fixed delay makes the turnaround assertion fail. Actual RadioLib SPI/GPIO
simulation covers sync-only rearm and exact 4999/5000 us observations. These are
software regressions, not proof that PWM NMI or a specific silicon event caused
the field failure. Physical DIO/PWM/power timing remains an acceptance requirement.

**Observability:** Optional RFT retains recent rejection reason and observation
versus completion offsets without changing existing binary frames or C6 freshness.
RX RAM counters are not remotely available to C6 and must not be advertised as
live telemetry. No electrical pin reassignment or PWM frequency change is made.
The caller timestamp is not the physical IRQ edge or the on-air transmit edge.
See `docs/evidence/20260907-supervised-radio-solution.md` for the deployment and
fallback boundary; candidate 0.1.6-dev has not been flashed.

## ADR-018 — User-authorized RF firmware maintenance (2026-09-08)

The user explicitly requested implementation of TX-to-RX RF OTA and handoff to
existing task 01a06bca-02ba-7ea1-adfa-d067e4b10732 for firmware compilation.
This supersedes the historical OTA non-goal for this feature only. Keep normal
Control/RPM v1 untouched. New fixed 30-byte request/reply types 0x30/0x31 use
stop-and-wait maintenance; TX broadcasts full speed for at least 600 ms before
pausing normal slots. All nodes remain full speed through control loss; selected
RX latches full speed before flash allocation/erase and ignores control while
updating. Maintenance has a 10 s progress timeout; interrupted transfers restart
from byte zero, leaving the running application intact until explicit commit.

ESP8266 Core's existing Update/eboot stages in unused sketch space. Verify exact
size, image compatibility fingerprint, image magic, and MD5 before commit. MD5
and network/session IDs detect mistakes/corruption, not malicious firmware or
sender identity. No Wi-Fi, filesystem or new external dependency is added. The
Core Update allocator and MD5 implementation are permitted only in maintenance;
normal RF control paths remain fixed-buffer. Commit schedules eboot copy and a
reboot after an ACK retry grace period. This is not A/B rollback: loss of power
during the final eboot copy may require wired recovery; never claim guaranteed
power-loss recovery. First deployment requires wired TX and RX installation.

Only a directly connected exclusive TX Host serial port is the initial uploader
entry; the existing C6 bridge does not yet forward OTA commands. Do not alter C6
or silently stop any active serial owner. No physical write is authorized by
this development and compilation request.
# ADR-027 — Read-only RX tach diagnostics (2026-09-11)

User requests raw falling-edge count, accepted estimator-period count, filter
rejection count and raw last-edge age to distinguish missing pulses from rejected
pulses. Add a separate fixed30-byte RF diagnostic request/reply (0x42/0x43),
relayed by Host v2 types0x16/0x94. Do not repurpose Control/RPM reserved bytes,
change existing packet versions, change tach filtering or add unsolicited RX
transmissions. Snapshots never clear counters, reset sampling, persist settings
or reboot. Existing OTA/settings ownership, strict identities and bounded radio
timeouts must be preserved. Detailed wire layout and acquisition disturbance
are specified in PROTOCOL.md after implementation review.

Counters saturate at32 bits since boot; raw edges include queue overflow.
Valid periods mean newly accepted estimator updates (not repeated reads).
Rejects exclude warmup and count rejected processing decisions. Raw age is
independent of filter acceptance; wire milliseconds saturate65534 and65535
means no edge observed. Disabled UART-mode tach must be explicit, not misread
as disconnected wire. ISR work remains constant-time.

This turn only changes/tests/builds code. No hardware connection, restart, pin
mode mutation or deployment is authorized. Deployment necessarily replaces
current RAM evidence; this candidate can diagnose subsequent reproduction only.
