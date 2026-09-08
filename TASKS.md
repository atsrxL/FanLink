# FanLink task backlog

## 2026-09-08 ADR-025（当前优先契约）

- [x] 规范改为原生 8 槽 Host v2：全部外层帧 version2，SET_ALL8/STATUS54/METRICS16；无 wire16、补齐、截断或双栈。
- [x] 明确 RF v1 固定30B，8 duty + 8 reserved255；8B header/2B CRC/MAX_PAYLOAD128 不变。
- [x] 保留 125ms 八节点轮询、500ms watchdog、5miss/6s presence 与 2.5s freshness。
- [x] 完成本轮 TX/PC/C6 v2 跨实现与错误版本/长度拒绝门禁；精确结果由本轮证据记录，旧 PASS 不作为新版本证据。
- [ ] 用户另行授权后协调部署 TX/C6/PC 并验收；本轮不刷设备，RX 无需立即刷。
- [ ] 旧 v1 TX 仅用归档 v1 工具或有线 ROM 迁移；v2 工具不得尝试旧 TX BOOT/OTA。

## 2026-09-08 ADR-023（历史能力基线；wire16 已被 ADR-025 替代）

- [x] 最大 8 RX，125 ms 非阻塞轮询，一秒一轮，保留旧 wire envelope。
- [x] TX 软件重启/ROM BOOT、本地 UART OTA，RX RF OTA 保留。
- [x] TX/RX 持久化功率；RX uart/pwm/pwmswap 定向设置、重启生效。
- [x] 5120 B/s 双端 OTA 节流与 clean 四目标编译（完整结果见能力证据）。
- [ ] 实机验证 8 节点、设置断电保存、三种引脚模式及 OTA；本轮未刷写。
- [ ] C6 独立适配 8 节点、2500 ms 遥测新鲜度和新设置命令（未部署）。

## P0 — historical software baseline (16-channel Host v1, superseded)

- [x] Define fixed 30-byte Control and RPM packets with application CRC.
- [x] Implement full-state 16-channel TX scheduler and poll 1–16.
- [x] Implement single-channel RX selection and poll-gated reply.
- [x] Implement host and RF fail-safe-to-full-speed behavior.
- [x] Implement stale sequence rejection, retry, gap and wrap behavior.
- [x] Add desktop-testable interfaces/controllers.
- [x] Add binary Host protocol and Python CLI.
- [x] Add a backward-compatible Host link-metrics frame with separate uplink and
  downlink RSSI for all 16 nodes.
- [x] Add native tests and Arduino/RadioLib stub syntax smoke.
- [x] Add PlatformIO environments for TX and RX 01–16.
- [x] Run clean PlatformIO TX build with real dependencies.
- [x] Run clean PlatformIO RX Node 01 and Node 16 builds.
- [x] Resolve real RadioLib 7.7.1 API/compiler differences without changing wire format.
- [x] Add PlatformIO CI after the first successful real build.

## P0 — exact hardware target

- [ ] Identify TX product, PCB revision and original ELRS target.
- [ ] Identify RX product, PCB revision and original ELRS target.
- [ ] Verify MCU, flash, oscillator and SX1280/SX1281 variant.
- [ ] Verify NSS, DIO1, RESET, BUSY, SCK, MISO and MOSI.
- [ ] Verify TCXO enable/frequency if present.
- [ ] Implement and verify PA/LNA/antenna-switch state table.
- [ ] Determine minimum and maximum safe RF output settings.
- [ ] Verify GPIO1/GPIO3 pads and all attached components.
- [ ] Document recovery flashing/unbrick procedure.
- [ ] Split generic build configuration into exact TX and RX board targets.

Product-level profiles are now identified as JHEMCU 2.4GHz Lite RX hardware
repurposed as FanLink TX and BETAFPV 2.4GHz Nano RX, using ExpressLRS targets
commit `c4bd7b823594c233e673828ab493a2f8319a756a`. The two combined identification
items remain open because the physical PCB revisions and original firmware
targets from the actual boards have not been read back.

## P0 — single-node bench bring-up

- [x] Implement and native-test the non-blocking BETAFPV Nano GPIO16 800 ms link-LED pattern.
- [ ] Verify GPIO16 polarity, visible double-flash/solid pattern, watchdog transition and invalid-packet behavior on the physical RX.
- [ ] Initialize SX1280 at minimum safe power.
- [x] Prove one-way 30-byte Control reception on the physical Node 01.
- [ ] Prove network ID, sequence and own-channel selection on a debug pin/log.
- [ ] Build the external open-drain PWM interface.
- [ ] Scope 25 kHz at duty 0/1/64/128/192/254/255.
- [ ] Measure GPIO1 boot waveform and verify hardware-default full speed.
- [ ] Measure fan Pin 3 Tach high voltage before MCU connection.
- [ ] Verify RPM against an independent reading.
- [ ] Enable one polled Reply and measure turnaround/airtime.
- [ ] Tune reply delay, receive timeout and slot period from measurements.
- [ ] Test host timeout, TX loss, RX reset, corrupt packet and wrong network ID.
- [ ] Run one TX + one RX continuously for at least 30 minutes, then 8 hours.

Current partial bench evidence: the already-flashed `0.1.2` RX showed a steady link
LED, responded to duty changes, and returned fresh Node 01 RPM/status frames. This
proves Control reception and a functioning reply path, but not the LED search pattern,
reply timing/airtime, exact power, PWM waveform, Tach voltage/calibration, or the new
`0.1.3-dev` Node 01 pin/polarity/RSSI behavior. A logical duty of `0` still produced
about 2787 RPM on the old image, motivating the Node 01-only polarity correction.

## P0 — multiple nodes

- [ ] Bring up two uniquely labelled node IDs.
- [ ] Confirm an unpolled node never transmits.
- [ ] Confirm a missing node cannot block later slots.
- [ ] Bring up at least four nodes and check channel isolation.
- [ ] Deliberately duplicate a node ID and document collision symptoms.
- [ ] Validate complete 1–8 logic with hardware or an RF test fixture.

## P1 — reliability after measured baseline

- [x] Preserve TX sync/IRQ CRC/packet CRC error evidence and software timing; add bounded RFC/RFE plus FIFO-guarded optional diagnostics (ADR-022).
- [ ] Separately deploy reviewed0.1.11 TX and repeat paired capture at intended~30cm; physical return-path root cause remains unproven, RX02 need not be reflashed.
- [x] Retain single RX between software waits; consume checked IRQ/FIFO metadata and rearm after packets.
- [x] Skip unchanged PWM writes while preserving real full-speed initialization.
- [x] Add per-node RFN evidence and prioritize STATUS/METRICS over diagnostic text.
- [ ] Separately authorize and bench-test 0.1.5-dev TX/RX optimization; do not weaken C6 freshness thresholds.
- [x] Recover radio-only faults/startup failures with bounded backoff, without reinitializing Tach.
- [x] Continue TX reception after CRC/invalid frames within the original reply window.
- [x] Prevent duplicate old controls from extending the watchdog or releasing failsafe.
- [x] Add deterministic long-runtime/fault regressions and real RadioLib simulated-I/O tests.
- [x] Historical v1 check: verify then-unchanged wire framing against the actual production FanLinkC6 protocol header (not v2 acceptance).
- [ ] Bench-validate the new recovery code and log sustained operation after separately authorized flashing.
- [x] Add TX counters for radio begin/transmit, RX timeout/CRC/I/O, invalid replies and accepted replies.
- [x] Add RX RAM counters for malformed/stale/expired controls, own polls and reply calls/results.
- [x] Separate software receive observation from completion; regress TX deadline and RX remaining guard.
- [x] Map sync-only FLRC error to DIO before starting RX and test rearm with real RadioLib simulation.
- [ ] Bench-validate unflashed 0.1.6-dev at PWM 0/1/64/128/192/254/255; instrument actual DIO/BUSY/PWM/power.
- [ ] Add parser/property fuzz tests.
- [ ] Determine whether bounded blocking RadioLib calls and turnaround satisfy the measured 125 ms slot.
- [ ] Replace only the radio adapter with an asynchronous DIO1 state machine if measurements require it.
- [ ] Quantify packet error rate, jitter and telemetry freshness.
- [ ] Add watchdog/brownout tests.
- [ ] Design rendezvous + FHSS as a separate protocol milestone.
- [ ] Complete regional RF compliance review.

## P2 — optional after stable hardware

- [x] User-authorized TX local UART update/restart and software ROM BOOT commands (ADR-020), desktop/sanitizer tests and real three-target builds.
- [ ] Separately deploy new TX and validate local update/reboot/ROM sync with no-reset, then RF recovery; C6 maintenance integration remains separate.

- [ ] Compile-time metadata/status query.
- [ ] Persistent node provisioning, only if 8 labelled binaries are operationally unacceptable.
- [ ] Optional CRSF Host Adapter that leaves the FanLink air protocol unchanged.
- [x] Historical v1 local TX Host dashboard with 16-channel duty, status/RPM telemetry, separate
  uplink/downlink RSSI and RF diagnostics (Host↔TX and old-firmware Node 01 RF/RPM
  paths observed; new RSSI frame still requires flashing).
- [x] User-authorized RF OTA software strategy and implementation (ADR-018).
- [ ] Bench-test RF OTA, interrupted transfer, reboot and final-copy power loss.

Do not move P2 work ahead of unresolved P0 safety or hardware items.
