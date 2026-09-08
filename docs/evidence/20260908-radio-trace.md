# 2026-09-08 software radio trace diagnosis

User has no logic analyzer and requested software diagnosis. Both boards are
BETAFPV2.4GHz Lite RX, TX role on ACM0/if00 and RX2 UART on ACM1/if02. SSH
read-only inspection confirmed RG35XX Debug BSP V3, dualUSB1a86:55de and no
bridge/UART owner before capture. No PHY, power, pins, duty or C6 changes.

## Implementation and verification

- Fixed32-event rolling history and immutable32-event frozen snapshot, <=1088B
  ring object. CRC/I/O triggers plus every64 successful receive; dropped trigger
  and history overwrite counters saturate. Fixed work, no heap/delay/print.
- Only existing checked SPI results are recorded; one timestamp plus two GPIO
  samples per event. No new SPI. Freeze copies at most512B and has real CPU cost.
- TX trace is a fair25ms optional family behind STATUS/METRICS; original six
  families remain1000ms. Trace formatting is skipped within2ms of the next slot.
- RX trace runs only when ordinary250ms logger is not due, outside maintenance,
  with armedRX and sampled DIO/BUSY low. Both writers require spare UART FIFO
  capacity and never flush. Guard samples cannot exclude a subsequent edge.
- `make verify` PASS (`build/20260908-radio-trace-verify-final.log`);2744native
  assertions, actualRadioLib simulatedI/O regressions, ring and scheduler
  ASan/UBSan,96PC tests including20trace tests, syntax smoke.
- First build/verify attempts stopped on source-manifest drift while independent
  parser tests were still being finalized; no artifact was deployed from them.
  After freeze, final verification and clean4/4 actualPlatformIO builds passed.
- Artifacts: `firmware/dist/20260908-lite-radio-trace`, SHA256SUMS allPASS.
  TX315456B SHA4137c267779dac17670f6fb75a7a8d10ffba7bb64d6e7ce737a0f60a7971ba7d;
  RX2310224B SHA49638331b743c091f3d521e2c33897ef481944fa663458c7e27249062a327a85.
  TX staticRAM33732/81920B; RX32024/81920B. Compiled size is not runtime heap proof.

## Pre-upgrade capture

`build/20260908-radio-trace-precheck.jsonl` and paired rawfiles,60.030s effective
data;66.036s including relay teardown.239/239STATUS fresh, parserrejects0.
Initial buffered RX text before3s is excluded for counter comparisons, not
attributed to current firmware behavior. Both UARTs released and fuser-free.
Independent summary: `build/20260908-radio-trace-precheck-analysis.json`.
After3s exclusion: RFN3.547..59.555s,56polls/48accepted/8timeout/7CRC
(CRC overlaps timeout);228/228freshstatus,0staleruns, maxquantizedage1700ms.
RFT retained successful observations2823..2852us, completions3930..3969us;
RX retained reply call1245..1264us. These are diagnostic samples, not a histogram
of every packet. No uptime decrease, RX recovery/IO delta0.

## Deployment and capture status

Online RX2 thenTX OTA completed. See `build/20260908-radio-trace-upgrade.log`.
RX310224B: start16.138s,COMMITTED412.054s (395.916s); restored telemetry and
Query beforeTX. TX315456B: start445.156s,COMMITTED549.139s (103.983s);
uptime restart observed,35s recovery observation and8s final fresh telemetry
passed. Final Query bothLite13/flags0, RX2UARTmode0,8receivers/125ms/5120Bps.
NewRTE emitted; no Flash pre-read/readback or independent flash provenance.

Initial pairedcapture `20260908-radio-trace-postcheck` failed before1s because
RX SSH authentication was denied; TX still had fresh telemetry. Kept failed
capture as evidence. One bounded SSH recheck succeeded and confirmed UARTs
released; retry `20260908-radio-trace-postcheck-retry1` is the diagnostic run.
Do not classify SSH failure as RF/firmware failure.

## Completed paired capture

Retry captured 300.071s of data (306.084s including cleanup). Final artifacts:
`build/20260908-radio-trace-postcheck-retry1.jsonl` plus paired raw files;
`build/20260908-radio-trace-analysis.json` for trace analysis and
`build/20260908-radio-trace-postcheck-analysis.json` for independent counters.
The older counter helper labels new RX RTE lines as nonstandard; these are
expected trace records, not serial corruption. Final trace issues and parser
rejects are zero. The early in-progress report is not the final result.

Excluding the first 3s and using the comparable <=300s window: 296 polls,
267 accepted replies (90.20%), 29 timeouts and 29 CRC errors. CRC overlaps
timeout; do not add them. Including the final 300.071s RFN adds one successful
poll: 297/268/29/29. STATUS after exclusion: 1186/1188 fresh, two stale samples
in one run, sampled stale hold 0.499s, maximum age 2800ms. This sampled hold
is not exact physical outage duration. No TX/RX uptime decrease or reset.
RX local reply attempts/successes both increased by 295; failure, expiration,
I/O error and recovery deltas were zero. RX receive CRC increased by 18.
Previous same-window postfix capture was 296/270/26/24 with two stale runs;
one short comparison cannot establish a link-quality improvement or regression.

83/83 snapshots complete: TX 29 CRC + 4 sampled good; RX 15 CRC + 35 sampled
good; no I/O-trigger snapshots. Good packets are sampled every 64 successes,
so snapshot ratios are not packet-loss rates. All 44 current CRC segments
have IRQ 0x0042 and packet error 0x12; good segments have IRQ 0x0002/error 0x02.
All report length 30, offset 0 and sync 1. Across retained history, 146 clear
ends and 282 each arm-stage/launch/end samples have DIO/BUSY low and no
unexpected checked driver return. These history counts are not independent
transactions. BUSY high was observed only at DIO observation, not as a
persistent fault. Successful clear return does not prove IRQ-zero readback.

| Sampled duration (us) | TX CRC | TX good | RX CRC | RX good |
|---|---|---|---|---|
| Rearm after DIO | 508-513 | 508-517 | 502-521 | 517-544 |
| IRQ clear | 75-76 | 63-98 | 55-56 | 55-84 |
| DIO observed to result | 916-973 | 1191-1276 | 825-882 | 1116-1181 |

CRC paths skip FIFO reads, explaining shorter result latency; this is not
evidence of a cause. TX good and CRC packet-status RSSI both decode to
-16.5dBm. Latest TX context associates them with poll 2, but cannot identify
the sender of a corrupt frame. RX good RSSI is -16.5 to -18dBm; RX CRC
RSSI is -68.5 to -111.5dBm. This difference merits investigation, but does
not establish interference or that these are corrupted normal TX frames.
Final frozen dropped-trigger counters TX1/RX6 mean diagnostic suppression,
not RF packet loss.

Retained successful TX observation times are 2782-2990us (median 2879),
completion 4043-4296us (median 4116). Previous medians were 2830/3948us;
this is not an isolated measurement of tracing overhead.

Conclusion: chip-reported CRC persists. No IRQ-clear failure, stuck DIO/BUSY,
checked SPI-return failure or distinctive rearm delay was reproduced. Root
cause remains unproven; software samples cannot rule out short physical-edge
faults. A next candidate is bounded capture of CRC-failed FIFO bytes as
untrusted diagnostic data to compare sequence/fields/bit corruption; this
would add SPI and timing cost and is NOT implemented. CRC rejection remains
strict; no speculative PHY change was made.

Final live cleanup check: both UARTs fuser-free, bridge running=0. No active
capture/automation, no C6 changes, no Git commit/push. RX2 remains UART mode;
both boards report Lite/+13dBm/flags0, 8 receivers/125ms/5120Bps OTA cap.

## Interpretation boundaries

RTE is sampled software evidence, not a logic-analyzer trace. The ports have
independent micros clocks; UART arrival times cannot align physical edges.
Successful IRQclear return is not IRQ-zero readback. GPIO low snapshots do not
exclude pulses; high postclear state is suspicious, not proof of residualIRQ.
Rolling history need not contain the whole transaction; pending snapshots
suppress new triggers. The analyzer separates completeness, valid fields,
boot epochs, current fault segment,32-bit wrap and dropped capture evidence.
RX2 UART mode has no physical PWM/tachometer acceptance; this is not a C6 fan
installation test, RF emissions measurement or multihour stability proof.
