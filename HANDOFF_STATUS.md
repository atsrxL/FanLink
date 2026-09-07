# FanLink hand-off status

## 2026-09-07 源码入库

- Git 根目录为独立 FanLink 源码树；仓库为
  `https://github.com/atsrxL/FanLink`，分支为 `main`。
- `pc/` 包含 FanLink Python 串口控制台，`firmware/` 包含 TX/RX 固件与构建工具；
  早期 ELRS Node.js 控制台不纳入本仓库。根目录保留文档与统一校验入口。
- 纳入固件、控制面板、工具、测试、文档和现有 GitHub Actions；各层级 `.pio/`、
  `build/`、`dist/`、Python 缓存及外层历史压缩包/备份保留在本地。
- `make verify` 通过：native 269 项、面板 20 项、CLI、Python/JavaScript、
  firmware stub smoke 和 source manifest。
- 源码打包脚本已补充排除 `.git` 元数据；本次未修改固件行为。
- 迁移后 TX/RX01/RX16 真实依赖 clean 构建 3/3 通过，固件 SHA-256 回读一致；
  产物与构建时源码清单位于 `firmware/dist/20260907-git-layout/`。
  `SOURCE_MANIFEST.build-input.sha256` 保留构建输入，之后仅补充发布说明文档。
- 本轮日志：`build/git-publication-20260907-layout-verify.log` 与
  `build/git-publication-20260907-platformio.log`（仅本地保留）。
- 下方是 2026-09-04 的历史构建与实机交接快照。本次只完成源码入库相关工作，
  没有重新读取设备状态，不能据此判断目前实际刷入的固件。

## 2026-09-04 历史交接快照

最后更新：2026-09-04  
当前工作树：Unreleased / `0.1.3-dev`  
状态：所请求的软件修改、离线回归、sanitizer、CMake/CTest、源码门禁及真实依赖
TX/RX01/RX16 clean 构建均通过。新固件尚未刷写；当前实机仍运行 `0.1.2`。

## 本轮配置

- Air protocol 仍为 v1，Control/RPM Reply 均固定 30 bytes。
- Host protocol 仍为 v1；新增 `0x82 LINK_METRICS_SNAPSHOT`，payload 固定
  32 bytes，每节点依次为 RX→TX 上行 RSSI、TX→RX 下行 RSSI 两个 `int8`；
  `-127` 表示未知。原 `0x80` 102-byte 状态帧不变。
- JHEMCU 2.4 GHz Lite TX 保持 `+13 dBm`。该板是裸 SX1280 配置，约 20 mW，
  是 RadioLib/官方 target 上限，不能由固件真实提升到 25 mW。
- BETAFPV 2.4 GHz Nano RX 设为 `-6 dBm` PA drive，对应官方 25 mW 功率档。
- 仅 RX Node 01 使用 PWM GPIO3、Tach/Sensor GPIO1、低有效门极；Node 02-16
  仍为 PWM GPIO1、Tach GPIO3、默认门极极性。
- Host/空口逻辑不反转：`0=0%/停转请求`，`255=100%/全速请求`。Node 01
  平台层反相用于纠正当前走线。

## 当前源码验证

```text
native C++                              PASS: 269 checks
native ASan/UBSan                       PASS: 269 checks
Python CLI self-test and -O self-test  PASS
Python compile / JavaScript syntax      PASS
control-panel unittest                  PASS: 20 tests
firmware stub syntax smoke              PASS
CMake + CTest                           PASS: 1/1
source manifest / make verify           PASS
PlatformIO clean TX/RX01/RX16           PASS: 3/3
```

正式构建证据：
`dist/20260904-v0.1.3-25mw-node01-rssi-reviewed/`。其中 `BUILD_INFO.json`、
`SHA256SUMS`、各固件校验文件及完整日志是产物大小和 SHA-256 的唯一依据。

## 已刷 `0.1.2` 的实机证据

- TX Host 端口 `/dev/cu.usbmodem<device>`（公开记录已隐去设备唯一编号）可持续连接，Host watchdog 正常；
- RX GPIO16 链接状态为常亮，用户确认控制有效；
- 双向 RF Reply 已工作：实时快照曾显示 Node 01 requested/applied=`143`、
  RPM=`1749`、age=`0`、missed=`0`、stale=`false`；
- 另一观察中 requested/applied=`0` 但 RPM 约 `2787`，确认这块 Node 01 的物理
  0/255 方向相反，是本轮平台极性修正的依据；
- TX 诊断累计 accepted reply 持续增长。总 timeout 主要包含不存在的 Node 02-16，
  不能直接当作 Node 01 丢包率。

这些实机证据属于旧 `0.1.2`。它不证明新功率、Node 01 新脚位/极性或 `0x82`
双向 RSSI 已工作。

## 尚未验证与风险

1. `0.1.3-dev` TX/RX01 尚未刷写、启动和相互通信。
2. TX 不能达到用户期望的真实 25 mW；当前硬件安全上限是 `+13 dBm`，约 20 mW。
3. BETAFPV 的 25 mW 只是官方功率档配置，未用功率计验证天线口输出/EIRP。
4. Node 01 的 GPIO3 PWM、GPIO1 Tach、0/255 端点、25 kHz 波形和 failsafe
   必须在刷写后实测；Tach 高电平与转速标定仍缺仪器证据。
5. 新上下行 RSSI 数值、过期变未知、远距离表现、airtime/turnaround/PER 尚未实测。
6. GPIO16 搜索态“双闪+500 ms 停顿”尚未完整观察；只观察到链接常亮。
7. PCB revision、射频功率法规、精确 watchdog/Host 周期及长期热稳定仍未完成。

## 下一步唯一动作

让 BETAFPV RX Node 01 进入 ESP8266 ROM BOOT；随后刷入
`dist/20260904-v0.1.3-25mw-node01-rssi-reviewed/fanlink_rx_node01.bin`，重启后先
确认 LED、PWM 0/255 方向与 RPM，再刷 TX 并验收两列 RSSI。
