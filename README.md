> 最新候选：**0.2.0-dev / ADR-025，原生 8 槽 Host v2 / RF v1**。
> 所有 Host 外层帧均为 version 2：SET_ALL 8B、STATUS 54B、METRICS 16B，
> 无 wire16、补齐、截断或双栈；8B header、2B CRC、128B payload 上限保留。
> TX/C6/PC 必须协调升级。旧 v1 TX 只能用归档 v1 工具或另行授权有线 ROM
> 刷入 v2；新工具不能向旧 TX 请求 BOOT/OTA。RF 保持固定 30B，RX 不需立即刷。
> 8 RX / 125 ms 轮询 / 持久化引脚和功率设置均保留；本轮未刷设备或部署 C6。
> 用户确认 uart=GPIO1 TX/3 RX，pwm=GPIO1 PWM/3 Tach，pwmswap=GPIO3 PWM/1 Tach。
> TX 软件 BOOT、本地串口 OTA、RX 空中 OTA 均保留；OTA 镜像数据上限 5120 B/s，
> 不是实测吞吐保证。当前能力/命令/迁移边界见 [功能验收](docs/CAPABILITIES.md)。
> 本轮不刷设备、不部署 C6；以下旧候选与现场记录均为历史。

> 2026-09-08 用户明确授权新增 RX 射频 OTA。当前 0.1.8-dev 候选保留既有控制协议，
> 增加独立维护升级通道；历史 OTA 非目标对本功能已被本次授权替代。使用与边界见
> `docs/OTA.md`、`docs/OTA_WIRE.md` 和 ADR-018。未刷写实机。

# FanLink — Unreleased candidate (`0.2.0-dev`)

以下两段是 0.1.5/0.1.6 时期的历史记录，不描述当前源码或设备版本：

当时的候选修复回复时序：TX 按首次 DIO 软件观察时刻判定原 5 ms 窗口，RX 只等待
观察后 1200 µs 的剩余量，并修复 sync-only IRQ 未唤醒接收的问题。新增 RFT
拒收/时序诊断；不改空口、GPIO、25 kHz PWM 或 C6 离线阈值。软件回归已通过，
clean 编译与产物记录见 [联合方案证据](docs/evidence/20260907-supervised-radio-solution.md)。
**当前设备写入记录仍是 0.1.5-dev，候选没有刷入；低 PWM 的物理根因及改善尚未验收。**
用户最新复现为 RX 重插恢复、100% 有回传但偶断、中间 PWM 控制有效但无回传。
下面是上一轮历史背景，不是新的刷写授权。

本轮针对近距离间歇遥测断联优化：跨软件等待保持单包接收、减少重复 PWM 写入、
增加每节点 `RFN` 诊断，保持 C6 保护阈值、射频参数和 v1 二进制协议不变。
新源码与实际运行验收必须区分：TX 和 RX Node01 随后已分别获授权直接写入
同批 `0.1.5-dev`，未读回或发送 run/reset。用户报告实际控制有效但没有回传，
当前仅调查原因，尚未通过双向 RF/长期稳定性验收。代码证据见
[`20260907-link-timing.md`](docs/evidence/20260907-link-timing.md)，最新 RX 写入与
状态记录见 [`20260907-rx-ssh-flash.md`](docs/evidence/20260907-rx-ssh-flash.md)。

FanLink 是一套面向 **1 个发射端（TX）+ 最多 8 个单风扇接收节点（RX）** 的专用无线控制固件。

每隔 125 ms，TX 广播全部 8 路占空比并指定 `poll_id=1..8`，一秒轮询完所有节点。Host 原生 8 槽；RF Control 仍为 30B，其中 8 个 duty 后有 8 个发送 255 的 reserved 字节（不是通道）。节点 X 只应用 `duty[X-1]`，只有 `poll_id == X` 时才回传 RPM。项目不包含 ExpressLRS 应用层，也不兼容标准 ELRS OTA、绑定或遥测。

源码仓库：[atsrxL/FanLink](https://github.com/atsrxL/FanLink)，默认分支 `main`。

```bash
git clone https://github.com/atsrxL/FanLink.git
cd FanLink
make verify
```

电脑端代码位于 [`pc/`](pc/README.md)，TX/RX 固件代码位于
[`firmware/`](firmware/README.md)。本文命令均从仓库根目录执行。

生产 Host 是 **FanLinkC6 ESPHome**（GPIO1 TX / GPIO2 RX，115200 8N1，
每 200 ms 发送完整 8 通道 Host v2 指令）；`pc/` 仅作为调试面板。此处是协调
升级后的目标契约，不表示 C6 已部署。射频自动恢复、原窗口内错帧后继续接收和
重复旧包保护保持不变；5 miss / 6 s 在线冗余不放宽 2.5 s 遥测新鲜度或
500 ms 安全 watchdog。较早的 v1 可靠性修复证据（非本次 v2 验收）见
[`20260907-radio-recovery.md`](docs/evidence/20260907-radio-recovery.md)。

下文引用的历史 `dist/` 构建产物与日志保留在原本地工作目录，不随源码提交；
按第 5 节重新构建时，新产物写入 `firmware/dist/`。`TEST_RESULTS.md` 和
`docs/evidence/20260904-*.md` 保留迁移前的历史命令路径与证据日期。

> **2026-09-04 历史验证记录：`Unreleased` / `0.1.3-dev` 的 native 与 ASan/UBSan 各 269 项、面板 20 项、CLI/Python/JavaScript/firmware-smoke/CMake/CTest 和 `make verify` 已通过；TX/RX01/RX16 真实依赖 clean 构建 3/3 通过。当时已刷的 `0.1.2` TX/RX01 实机链路可控制并回传 RPM，RX 链路灯常亮；新 `0.1.3-dev` 的 RX 25 mW 档、Node 01 脚位/极性修正及独立双向 RSSI 当时尚未刷写和实测。本次源码整理没有重新读取设备状态。**

## 1. 已包含的功能

| 功能 | 状态 |
|---|---|
| 8 路完整状态广播，原生 8 槽 Host v2 | 当前迁移契约；历史 v1 验收不可沿用 |
| `poll_id` 1→8 循环，125 ms 间隔 | 已实现 |
| 30 字节固定 Control 包 | 已实现并测试 |
| 30 字节固定 RPM Reply 包 | 已实现并测试 |
| CRC-16/CCITT-FALSE | 已实现并测试 |
| 16 位序号、旧包拒绝、精确重试、断线重捕获、回绕 | 已实现并测试 |
| TX 上位机 watchdog → 全速 | 已实现 |
| RX 无线链路 watchdog → 全速 | 已实现 |
| ESP8266/ESP8285 25 kHz PWM 适配 | 已实现，待示波器验证 |
| Tach 下降沿周期采集与 RPM 计算 | 已实现，待电气与实机验证 |
| RadioLib SX1280 FLRC 固定频点适配 | 真实依赖编译通过，待硬件验证 |
| 二进制上位机协议与 Python CLI | Host v2：54-byte 状态 payload、16-byte 双向 RSSI payload |
| TX 本地浏览器控制面板 | 已实现；离线测试及真实 TX 串口基本数据路径已验证，RF 与精确时序待验 |
| FHSS、绑定、动态功率、板载/Wi-Fi WebUI、OTA | 明确不在 v0.1 中；本地 Host 控制面板除外 |

## 2. 数据路径

```text
FanLinkC6（生产）/ 电脑终端（调试）
  │ FanLink Host v2 二进制串口协议，115200 8N1，8 个 duty
  ▼
FanLink TX
  │ 每 125 ms：RF v1 / network_id + sequence + poll_id + duty[8]
  ▼
RX 1 ... RX 8
  │ 每个节点只取自己的 duty
  │ 只允许被 poll 的节点发 RPM Reply（含 RX 测得的下行 RSSI）
  ▼
FanLink TX 聚合 8 个节点状态，并把上下行 RSSI 分开回传给上位机
```

`duty[0..7]` 是开环 PWM duty 命令，不是 RPM 目标：`255` 表示释放外部
开漏级并请求全速，较小值表示较小的命令占空比。实际 RPM 仅由 Tach 回传；在完成
具体风扇、PWM 波形和转速标定前，不得把 duty 换算成承诺转速或自动闭环目标。

默认 125 ms 时隙对应：

- 控制广播 8 Hz；
- 8 个节点一轮约 1 秒；
- 每节点理论轮询频率约 1 Hz。

这些只是 bring-up 参数，必须在真实射频模块上测量空中时长、BUSY/DIO1、RF 前端切换和收发 turnaround 后再冻结。

## 3. 仓库结构

```text
.
├── pc/                              # 电脑端 FanLink 串口控制台
│   ├── panel/                       # 浏览器界面
│   ├── tools/                       # Python 面板后端、串口 CLI、依赖
│   └── tests/                       # 电脑端离线测试
├── firmware/                        # ESP8285/SX1280 TX 与 RX 固件
│   ├── platformio.ini               # PlatformIO 目标与依赖
│   ├── CMakeLists.txt               # 原生 C++ 测试构建
│   ├── include/                     # 协议和控制器公共接口
│   ├── src/                         # TX/RX 入口、协议、控制器
│   ├── lib/                         # PWM/Tach/RadioLib 硬件适配
│   ├── scripts/                     # 原生测试与 RadioLib 构建钩子
│   ├── tools/                       # 固件构建、仿真、语法检查
│   └── tests/                       # C++ 测试及 Arduino API stub
├── scripts/                         # 全仓库源码清单与打包
├── docs/                            # 架构、构建、面板说明与历史证据
├── Makefile                         # 校验与面板启动统一入口
└── *.md / HANDOFF_STATUS.json        # 需求、协议、硬件、交接及许可证
```

## 4. 接手后的第一组命令

```bash
./firmware/scripts/run_native_tests.sh
python3 pc/tools/fanlink_cli.py --self-test
python3 -m py_compile pc/tools/*.py firmware/tools/*.py firmware/scripts/*.py
./firmware/tools/run_firmware_syntax_smoke.sh
python3 -m unittest discover -s pc/tests -p 'test_fanlink_panel.py' -v
node --check pc/panel/app.js
```

`0.1.1` 历史证据基线：

```text
native C++ tests                    PASS: 209 checks
Python host protocol self-test      PASS
Python self-test with -O             PASS
Python syntax compile               PASS
Arduino/RadioLib stub syntax smoke  PASS
CMake + CTest                       PASS: 1/1
PlatformIO clean TX/RX01/RX16       PASS: 3/3
```

这些 `209 checks` 和 `3/3` 只描述 `0.1.1` 证据包，不描述当前
`Unreleased` 工作树。2026-09-04 的 `0.1.3-dev` 结果为 native 与 ASan/UBSan 各
`PASS: 269 checks`、面板 `20 tests — OK`，CLI 双模式、Python/JavaScript、
firmware syntax smoke、CMake/CTest（1/1）、source manifest 和 `make verify`
均 PASS；同源码 clean PlatformIO TX/RX01/RX16 为 `3/3 PASS`。旧 `0.1.2`
固件的真实 TX/RX01 已观察到控制、RPM 回包和 RX 常亮链接灯；新功率、引脚、
极性和 RSSI 路径仍待刷写验收。记录见 [`TEST_RESULTS.md`](TEST_RESULTS.md)、
[`docs/evidence/20260904-tx-panel-usb.md`](docs/evidence/20260904-tx-panel-usb.md)
与 [`docs/evidence/20260904-v013-power-node01-rssi.md`](docs/evidence/20260904-v013-power-node01-rssi.md)。

完整交付时测试记录见 [`TEST_RESULTS.md`](TEST_RESULTS.md)。

`run_firmware_syntax_smoke.sh` 只检查本仓库代码在小型 API stub 下的语法闭合，**不能替代 PlatformIO 真实依赖编译，更不能替代硬件测试**。

## 5. PlatformIO 构建入口

依赖在 `firmware/platformio.ini` 中固定为：

- PlatformIO `espressif8266@4.2.1`；
- RadioLib `7.7.1`；
- C++17。

TX：

```bash
pio run -d firmware -e fanlink_tx_esp8285
```

RX 节点 1、8：

```bash
pio run -d firmware -e fanlink_rx_node01
pio run -d firmware -e fanlink_rx_node08
```

本地没有全局 `pio` 时，可用固定 Core 版本 clean 构建 TX 与代表性 RX，并收集日志和 `.bin`：

```bash
PLATFORMIO_CORE_DIR=/tmp/fanlink-pio-core \
  uv run --with 'platformio==6.1.19' python3 firmware/tools/build_firmware.py \
  --tx --rx 1 --rx 8 --run-id <new-unique-run-id>
```

历史 `0.1.3-dev` 同源码 clean 构建使用 PlatformIO Core 6.1.19、
`espressif8266@4.2.1`、Arduino ESP8266 Core 3.1.2 和 RadioLib 7.7.1，
位于 `dist/20260904-v0.1.3-25mw-node01-rssi-reviewed/`。其 TX/RX01/RX16 三个
目标均完成 clean + build，且 `BUILD_INFO.json`、`SHA256SUMS`、每固件独立
校验文件和完整日志已生成并回读校验；这证明固定依赖下三目标 clean 编译成功，
不主张不同批次二进制逐位一致，也不证明刷写或硬件工作。

`0.1.1` 历史审查产物位于
`dist/20260904-jhemcu-lite-betafpv-nano-reviewed/`；其中的
`BUILD_INFO.json`、`SHA256SUMS` 和 `*.build.log` 用于追溯。这些是旧基线的编译
证据，不包含当前控制面板/RX LED 等 `Unreleased` 变更，也不是可盲刷的硬件验收固件。

## 6. 硬件配置入口

默认宏在：

```text
firmware/include/fanlink/build_config.hpp
```

关键项：

```text
FANLINK_NETWORK_ID
FANLINK_NODE_ID
FANLINK_RADIO_NSS_PIN
FANLINK_RADIO_DIO1_PIN
FANLINK_RADIO_RST_PIN
FANLINK_RADIO_BUSY_PIN
FANLINK_RADIO_BUSY_TIMEOUT_MS
FANLINK_RADIO_RXEN_PIN
FANLINK_RADIO_TXEN_PIN
FANLINK_RADIO_USE_DCDC
FANLINK_RADIO_FREQUENCY_MHZ
FANLINK_RADIO_OUTPUT_POWER_DBM
FANLINK_FAN_PWM_PIN
FANLINK_TACH_PIN
FANLINK_GATE_ACTIVE_HIGH
```

以下是历史 target 配置来源说明，不代表当前两块 Lite 的设置或迁移验收。
当时 PlatformIO target 按 ExpressLRS targets commit
`c4bd7b823594c233e673828ab493a2f8319a756a` 对齐：两者共用
NSS=15、DIO1=4、RST=2、BUSY=5、SCK=14、MISO=12、MOSI=13；BETAFPV
Nano 另有 RXEN=9、TXEN=10 且使用 DC-DC，JHEMCU Lite 按官方
target 保持 LDO。BETAFPV 的共享 RX 档位现为 PA 输入 `-6 dBm`（上游
25 mW 档）；JHEMCU 为 SX1280 上限 `+13 dBm`（芯片端约 20 mW），没有
外部 PA 时不能由固件产生真正 25 mW。仅 Node 01 因本机走线使用 PWM=GPIO3、
Tach=GPIO1 和相反的平台输出极性；Node 02–16 仍为 PWM=GPIO1、Tach=GPIO3。
这些 target 记录不能代替用户手上
PCB revision、DCDC 电感、PA/LNA、TCXO、焊盘与刷写恢复路径的实物核对。
为了不让射频驱动卡死绕过 RX failsafe，RadioLib 以静态 64-byte
缓冲区编译，且实际使用的 SPI/BUSY/TX_DONE 等待均有 25 ms
硬上限；这只是软件失效收敛，仍需实板故障注入和独立过温保护。

## 7. 风扇接口必须按四线标准接法理解

标准四线 PC 风扇通常为：

| 风扇针脚 | 功能 |
|---:|---|
| 1 | GND |
| 2 | +12 V |
| 3 | Tach / Sense 输出 |
| 4 | PWM Control 输入 |

因此：

- RX 的 PWM GPIO 必须经外部 N-MOS/NPN 开漏级连接风扇 Pin 4；
- 风扇 Pin 3 Tach 经确认安全的 3.3 V 上拉/电平保护后进入 RX Tach GPIO；
- 不能把 3.3 V GPIO 直接推挽到风扇 PWM 线；
- MCU 复位或掉电时，外部电路应让晶体管关闭、PWM 线释放，风扇趋向全速。

完整接线和风险见 [`HARDWARE.md`](HARDWARE.md) 与 [`SAFETY.md`](SAFETY.md)。

## 8. TX 本地控制面板与 CLI

本地控制面板默认只监听 `127.0.0.1:8765`：

```bash
uv run --with 'pyserial>=3.5,<4' \
  python3 pc/tools/fanlink_panel.py --open
```

无硬件时可运行 `python3 pc/tools/fanlink_panel.py --demo --open`。Demo 只验证界面与
数据路径，不证明真实串口或 TX/RX 工作。以下为旧 v1 面板历史实机证据，不是
当前 Host v2 验收：当时面板能连接真实 TX、显示
状态快照；正常 disconnect 和后端进程退出后，均由独立 CLI 再次观察到 TX
failsafe。后端退出时页面在下一次轮询锁定全部控件，并将 watchdog、RPM、实际
duty 等清为未知。已刷旧固件的 Node 01 可控制且有 RPM 回传；新面板会分别显示
RX→TX 上行 RSSI 与 TX→RX 下行 RSSI，并在节点或指标过期时显示未知。新 RSSI
帧、精确保活/watchdog 时间和修正后的物理 0/255 端点仍待刷写验收。
操作语义、证据和剩余验收项见
[`docs/CONTROL_PANEL.md`](docs/CONTROL_PANEL.md)。

安装 CLI 依赖：

```bash
python3 -m pip install -r pc/tools/requirements.txt
```

持续维持全部节点 180/255 的占空比并监看状态：

```bash
python3 pc/tools/fanlink_cli.py --port /dev/ttyUSB0 hold --all 180
```

持续发送恰好 8 个值（Host v2）：

```bash
python3 pc/tools/fanlink_cli.py --port /dev/ttyUSB0 hold --values \
  255 240 224 208 192 176 160 144
```

单次 `set`/`full` 命令只发一帧；默认 500 ms 后 TX watchdog 会回到全部全速。正常运行应使用 `hold` 或由上位机周期发送 `SET_ALL_DUTY`/`HEARTBEAT`。

## 9. 明确不做的事情

历史 v0.1 非目标如下；用户后来授权的 FanLink OTA 与静态功率设置已被新增，
不受历史 OTA 非目标限制。当前仍不包含：

- 标准 ELRS 空口兼容；
- 完整 CRSF、MSP、Lua、Wi-Fi、板载/设备托管 WebUI；
- 动态节点发现、自动地址分配；
- FHSS、动态功率、速率扫描；
- 通用舵机/传感器/遥测框架；
- 加密认证。

这些功能不得在没有测量依据和用户明确需求时重新引入。

## 10. Codex 阅读顺序

1. `AGENTS.md`
2. `REQUIREMENTS.md`
3. `COMMUNICATION_REQUIREMENTS.md`
4. `HANDOFF_TO_CODEX.md`
5. `PROTOCOL.md`
6. `HARDWARE.md`
7. `TASKS.md`
8. `codex_prompt.txt`

## 11. 许可证

本仓库自有源码使用 MIT License。RadioLib 和 ESP8266 Arduino Core 作为外部依赖，不在压缩包中；详见 `THIRD_PARTY_NOTICES.md`。本包没有复制 ExpressLRS 源码。
