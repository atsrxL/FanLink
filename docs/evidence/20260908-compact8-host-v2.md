# Native eight-slot Host v2 — 2026-09-08

User requested actual eight-slot framing without redundant legacy-channel code.
ADR-025 supersedes ADR-023's wire16 migration envelope. This is a source/build
candidate, not a deployed or physically verified release.

## Contract and implementation

- All Host outer frames use version2, including OTA, settings, reboot and BOOT.
- SET_ALL=8 bytes; STATUS=54 (6+8*6); METRICS=16 (8*2). Header8, CRC2,
  MAX_PAYLOAD128 remain for OTA block transfer and diagnostics.
- Removed kWireNodeCount, sixteen-duty arrays, legacy controller overload,
  PC/C6 padding and truncation. All actual channel storage/loops use eight.
- Normal Host dispatch has one production implementation in host_control.hpp,
  called after unchanged maintenance arbitration. Exact lengths and node IDs
  are checked before refreshing the Host watchdog.
- RF stays version1/fixed30; Control carries eight duties plus eight reserved
  bytes transmitted as255. No RF channel9..16 storage or meaning. OTA/settings
  RF formats and radio parameters unchanged. Reserved padding is required by
  the shared fixed-length RF envelope, not a legacy Host adapter.
- OTA encoder/decoder and direct Receiver validation now share node limit8.
- 125ms full-state broadcast, eight-node one-second cycle, 500ms TX/RX
  watchdogs, five-miss/six-second presence and 2.5-second freshness unchanged.

## Verification

- Pre-change make verify PASS: build/20260908-compact8-baseline.log.
- Final native ordinary and ASan/UBSan:2902 checks PASS. Includes real
  parser -> production dispatcher -> controller watchdog tests: valid8;
  invalid SET_ALL7/9/16; correct-CRC v1; SET_ONE8 accepted/0,9,16 rejected;
  invalid HEARTBEAT and unknown types cannot refresh the500ms watchdog.
- OTA receiver8141 checks PASS; node8 valid,0/9/16/17/255 invalid with valid
  CRC; invalid identity cannot enter maintenance or touch storage/fan.
- PC full unittest111 PASS; normal/optimized CLI selftest and JS syntax PASS.
- Real C6/TX cross-codec1105 checks PASS: bytewise8/54/16, sequence wrap,
  maintenance outer versions, v1 rejection and resynchronization.
- C6 task reported ordinary plus ASan/UBSan425990 checks each, four wiring
  checks, and real TX diagnostics1366 checks in both modes. Main task
  independently reran all425990 C6 sanitizer checks and1105 wire checks;
  logs build/20260908-compact8-c6-sanitizers.log and compact8-c6-wire.log.
- Final repository suite and manifest: build/20260908-compact8-delivery-verify.log.
  An intermediate final-verify failed only the manifest because documentation
  was being updated concurrently; all executable tests had passed.

## Real local clean builds

Pinned PlatformIO6.1.19/espressif8266 4.2.1/Arduino3.1.2/RadioLib7.7.1.
TX, RX01, RX08 and RX2 UART all built successfully in
firmware/dist/20260908-compact8-host-v2. After production Host dispatch
extraction, TX was rebuilt in firmware/dist/20260908-compact8-host-v2-final.
The final extraction affects TX only; RX images from the first batch remain
the matching candidates. Keep each batch's build-input manifest and BUILD_INFO.

| Candidate | Bytes | SHA256 |
|---|---:|---|
| Final TX |315472|0c2bea43be18479976bf580c6562f1c01bb849e69e105bc7315d22dc31b2cba0|
| RX01 |310256|4fe47de9e741ca54d723a43a1bced3c379a444c9d266c1739dffde3a10bd01f9|
| RX08 |310256|da21af283811a538816db79bdd894ad52be94b65c67b4b8ed49a246c9fe5df5f|
| RX2 UART |310256|f816b6c43ea8b88495324f5c27a27f597922a3e8a96b9fbe789d7a7e6fa74d32|

## Deployment boundary

No device access, flash, OTA, HA mutation, git commit or push this turn. C6
production task01a06d61-1664-7f03-a624-9b088aee4ed7 has a source candidate,
not a new ESPHome image. Its deployment/HA cleanup remain paused.
TX/C6/PC require coordinated migration; v2 tools cannot command an old v1 TX,
even for BOOT/OTA. Use archived v1 tools or authorized wired ROM to migrate
old TX, then switch to v2 tools. No permanent v1 fallback. RX requires no
immediate reflash for Host framing because RF bytes are unchanged.
RF CRC root cause and physical fan safety remain unproven; this simplification
does not establish improved RF range or reliability.
