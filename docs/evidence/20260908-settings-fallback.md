# 2026-09-08 invalid-storage fallback and C6 recovery

User authorized fixing the non-operational Safe mode. ADR-026 changes only
invalid/unreadable/foreign stored settings recovery: validated compiled defaults
remain operational, valid persisted selections remain authoritative, no automatic
Flash write. Explicit mode or power setting persists fallback. Invalid compiled
defaults remain a rejected-build guard. Full-speed/watchdog/OTA checks unchanged.

## Validation

- Common native before/after PASS2902; settings corruption/read failure matrix
  covers TX/RX1..8, both profiles and all three RX modes, plus persistence, write
  failure, retries and restart grace. Runtime pin and TX settings tests PASS.
- Complete make verify exit0: build/20260908-settings-fallback-verify.log.
- C6 settings20725, real-service cross21805, native and wiring suites PASS.
- Four real clean builds PASS, PlatformIO6.1.19/ESP8266Core3.1.2/RadioLib7.7.1.
  Artifacts: firmware/dist/20260908-settings-fallback; BUILD_INFO and logs retained.
  Input manifest SHA256: 3a3dc933a8487daf4276f1b417aed114fb5075b4ee268615375418d03c7e25de.

| Target | Bytes | SHA256 |
| --- | ---: | --- |
| TX | 315488 | e1b07edcce11eb4d9ff9c8da9fcaaae8beba79d84da363cd75450dcd59fd284c |
| RX1 | 310272 | bcf913da9ba21a8d049b5336db2187227436d0c685932d22cfda477678eaad49 |
| RX2 UART | 310272 | 94e9af529ab79a9ebf916959586dcb2e8a10e976f15dee7b72588a42478d2120 |
| RX8 | 310272 | f501ff6be943d154b9bdf1415d0a2c80aaea619b176c0387e5bf97989fd8bdcf |

## C6 deployment

Selected fanlinkc6-only CT9000 build and SMB four-file readback passed. OTA
963536B SHA315aa8c8d7f5ae0f3b4d790bddc68c42f70d93af278493834d0fd1d022111726.
Approved identity verified; normal OTA succeeded in15.30s, running build
2026-09-08 09:15:02UTC, 45s observation plus device_info roundtrip passed.
Evidence: /tmp/fanlinkc6-safe-fix-ota.bAChin/acceptance.json.
Initial helper used unavailable is_connected attribute; corrected helper only,
then repeated read-only verification, not OTA.

RX1 online, missed polls0, reported PWM100/RPM0, settings status now explicitly
Legacy invalid settings; this confirms C6 recovery logic deployed, not RX repair.
RX2 offline, untouched. Passthrough false after C6 acceptance. C6 restart defaults
all PWM requests100; no explicit PWM/settings command was sent by build agent.

## RX1 maintenance progress

First entry helper received no API logs and timed out before opening TCP or
sending OTA bytes. Cleanup explicitly disabled passthrough and40s recovery
confirmed RX1 online/PWM100. Explicit DEBUG subscription fixed readiness
observation. Second entry uses one retained TCP connection. Prequery proves
RX1 Lite/mode254/power-18/flags2/capabilities0x78. New and old compiled RX1
compatibility0x746001ba match. BEGIN/hash ACK and initial data accepted.
All310272B accepted, FINISH/COMMIT ACK succeeded in1020.184s. After restart,
first Query timed out then matching Query confirmed Lite/pwmswap/+13dBm/flags2,
8slots/125ms/5120Bps.40s C6 recovery confirmed passthroughfalse, RX1online and
PWM100; warning correctly changed to compiled-default persistence prompt.
C6 one explicit same-pwmswap selection followed by45s ACK/reboot/Query
verification passed, RPM reports resumed (about1572). Final independent raw
Query confirms mode2/power13/flags0.40s cleanup finished, passthroughfalse,
RX1online/settingsConfirmed/misses0, API and maintenance sockets closed.
Final helper expected100 assertion failed because after freshSTATUS/trusted/
Connected restoration, C6 logged a new CH01 PWM set to43% request. Helper has
no number_command and did not override it. Final requested43/applied43.1373/
RPM838 proves reported control/application values, not physical tach accuracy.
One TX status recovery timeout during settings restart triggered full-speed/
untrusted protection and subsequently recovered on freshSTATUS. sequence_gap
and RF diagnostic events remain; do not claim all wireless issues fixed.
Persistence evidence: build/20260908-settings-fallback/rx1-persist-api.log/.json
and rx1-persist-direct-query.log/.json in the same directory.
Logs: build/20260908-settings-fallback/rx1-c6-ota-retry1.log and matching JSON.

Independent C6 native rerun passed: channel25924, diagnostic safety29558,
diagnostics149863, liveness3446, passthrough3815, protocol213087, settings20725,
transport297. Same suites passed ASan/UBSan. Settings production wiring check
also passed.

## Boundaries

RX1 upgraded; TX/RX2/RX8 only built. No ROM Flash
preread/readback/reset performed. No RF parameter change or claim of CRC root
cause repair. Physical fan behavior not accepted. No Git commit or push.
