# 2026-09-11 C6 passthrough TX then RX1 deployment

User explicitly authorized TX application update through C6 passthrough, then
RX1 RF OTA. Candidate batch20260911-tach-diagnostics SHA list independently
verified. C6 mDNS now .76, exact known MAC and build09:15:02 verified.
No ROM/Flash preread, no mode/power settings writes, no C6 OTA, no RX2 action.

## TX completed

Image316672B SHA69c6b6ff6777e02a5248956a2c3db9e081b07956d13fdb6561169e3c4c5e2ed2,
compatibilityE532A96B. Same exclusiveTCP socket, explicitly targettx application
OTA. COMMIT ACK confirmed225.617s. Fresh Hostv2 postboot uptime7s versus3500s
before; RX1age200ms/miss0. TXLite+13flags0 and RX1pwmswap/+13flags0 retained.
This proves successful submission and reboot/recovery, not fullFlash readback.

## RX1 failed before commit; cleanup verified

Image311664B SHAb515e378d39df71710c4622eee1a99e48503941a2000f40a3541d472ff87cc2e,
compatibility746001BA. Began only after TX recovery and both settings queries
passed. No setting changes. Lastconfirmed73000B(~23%), failed829.48s with
BLOCK_FINISH no matching ACK after12 attempts. commit_sent=false,committed=false;
uploader attempted best-effortAbort, not proof of RX Abort acceptance. No
application copy/commit started; old app remains, new tach diagnostics unavailable.
No automatic restart/retry was performed.

Finally disabled passthrough and observed35s recovery: passthroughfalse, RX1
Connectedtrue/RPM0/requested-applied100/miss0/pwmswap/ConfirmedbyRXQuery.
RFN accepted count advances; all TCP/API connections released. Helper exit1
reflects RX upload failure, not failure of previously completed TX upgrade.

## Slow-transfer evidence

RX effective progress approximately92-95B/s, versus TXsamepath1404B/s.
Historical docs/evidence/20260908-ota-3k.md recorded1293B/s RFfast transfer.
Current fast100B/rate5120 cap unchanged; TX OTA bridge source unmodified,
tach query idle and not yet sent. Normal125ms polls are paused during OTA.
Each100B uses10RFfragments plusBlockFinish; TX waits300ms forRFACK, host
timeout800ms thenreplay. Slow~1s/block is consistent with retries, not proof
which fragment/ACK failed. Final failure proves12attempts lacked matchingACK.
No per-block RTT/NACK/timeoutcounts logged, so specific physical cause unproven.

Logs/helper/JSON: build/20260911-tach-deploy/deploy.log, deploy.py, report.json.
Maintenance fullspeed is requested; physical behavior/electrical/RPM accuracy
not verified. Existing RF issues not declared fixed. No commit/push.
