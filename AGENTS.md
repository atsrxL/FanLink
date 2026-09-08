# AGENTS.md — Codex repository instructions

Read these files before editing code:

1. `REQUIREMENTS.md`
2. `COMMUNICATION_REQUIREMENTS.md`
3. `HANDOFF_TO_CODEX.md`
4. `PROTOCOL.md`
5. `HARDWARE.md`
6. `SAFETY.md`
7. `TASKS.md`

## Non-negotiable behavior

- User revision2026-09-08 ADR-025 supersedes wire16 preservation below: native
  eight-slot Host v2 (SET_ALL8/STATUS54/METRICS16), no legacy channel adapter.
  RF v1 remains fixed30, with eight duty bytes and eight reserved255 bytes.

- User revision 2026-09-08 (ADR-023): maximum 8 active receivers, poll1..8 at
  125ms intervals (nominal full cycle1s). Its former wire16 migration envelope
  is superseded by ADR-025 above.
- TX broadcasts every active channel's full state every slot; no delta updates.
- RX node X applies only `duty[X-1]`.
- RX node X transmits only when `poll_id == X`.
- Any uncertain control state biases the fan toward full speed.
- Air Control and RPM Reply packets are both fixed at 30 bytes in protocol v1.
- Do not import the complete ExpressLRS application stack.
- Do not claim standard ELRS compatibility.
- Fixed-frequency RF is the v0.1 bring-up baseline; FHSS is a separately reviewed milestone.
- Never call a commercial ELRS target flash-ready without its exact schematic/target definition and RF-front-end truth table.

## Engineering rules

- Run `./firmware/scripts/run_native_tests.sh` before and after common/controller edits.
- Run `./firmware/tools/run_firmware_syntax_smoke.sh` after Arduino/platform edits.
- Then run real PlatformIO builds; stub APIs are not proof of compatibility.
- Keep Tach ISR work constant-time: no logging, division, allocation, encoding or radio calls.
- Avoid heap allocation and Arduino `String` in firmware paths.
- Preserve full-state downlinks; do not convert to delta updates.
- Validate exact length, magic, version, type, CRC, network ID, node ID and sequence.
- Use unsigned wrap-safe time subtraction.
- Reject clearly stale sequence values without refreshing the RX watchdog.
- Keep board GPIO/RF-front-end details outside protocol/controller code.
- Update `PROTOCOL.md`, tests and `CHANGELOG.md` in the same change as any wire-format change.
- Treat `network_id` as filtering, not authentication or encryption.
- Never infer hardware verification from compilation or syntax smoke.

## First takeover milestone

1. Re-run all desktop tests.
2. Build `fanlink_tx_esp8285`, `fanlink_rx_node01`, `fanlink_rx_node08` and `fanlink_rx_node02_uart_diag` with real PlatformIO dependencies.
3. Fix only concrete compiler/API issues; preserve RF v1, implement Host v2 (ADR-025).
4. Identify the exact TX/RX boards and create board-specific pin/RF-front-end definitions.
5. Bring up one TX + one RX at minimum safe RF power.
6. Scope 25 kHz PWM and verify every fail-safe path.

The user explicitly authorizes the ADR-023 capabilities (persistent pin modes,
power settings, TX/RX OTA and5120B/s OTA cap); this supersedes historical non-goals
only for those features. No physical flashing or C6 deployment is authorized.
