# 2026-09-07 — Radio recovery and production C6 compatibility

## 本轮目标

修复“运行数小时后失联需要断电”的代码级恢复缺口及回包窗口过早结束问题。
用户确认 PC 面板只是调试，FanLinkC6 ESPHome 才是生产 Host；本轮明确不刷机。

## 完成内容

- TX/RX 只重初始化 SX1280，不重启 MCU，不重复初始化 PWM/Tach ISR。
- 连续 3 次硬错误或全链路长期无有效进展时恢复；TX 5 s、RX 2 s，失败重试
  1/2/4/8 s 封顶。缺少 Node 02–16 本身不是重置理由。
- RX 在重置前保持 255；TX 在恢复后重新检查原 Host 命令年龄，过期仍全 255。
- 正常重复包只重回包、不延长新控制有效期；失联后的重复包不能解除全速，
  倒退的新基线需要两个限时推进帧。冷启动首包和正常 16-bit 回绕兼容。
- TX 在原 5 ms 窗口内遇到 CRC/不匹配回包后继续等待；剩余时间向下取整，
  不启动零超时 receive，且最多 32 次尝试防止不推进的时钟/驱动路径忙循环。
- 错误出口 GPIO-only 关闭外部射频前端；失败的 SetTx/SetRx 清 staged mode。
  IRQ 清理失败保留错误，不再叠加另一轮失败 SPI 等待。
- radio reset 低 1 ms、释放后保守等待 10 ms、单次探测；移除运行时
  `10 × 3 s` 级别的嵌套初始化重试，后续重试交由控制器。
- 保持空口 v1、30/30 bytes，Host v1 的 STATUS102/METRICS32 不变。
  恢复计数通过既有 `0x81` 输出限频 `RFR attempts=... successes=... failures=...`。
  成功初始化不等于对端恢复在线。长键避免被旧 PC 解析器误读为故障枚举/有效回包。

## 修改文件与职责

- `firmware/src/app/{tx,rx}_controller.cpp` 与对应头文件：恢复状态机、接收窗口、序号保护。
- `firmware/lib/fanlink_platform_esp8266/.../radiolib_sx1280_port.*`、
  `firmware/scripts/platformio_patch_radiolib.py`：真实驱动集成和 GPIO 清理。
- `firmware/src/main_tx.cpp`、`tx_diagnostic_text.hpp`：限频恢复摘要。
- `firmware/tests/`、测试脚本和 CI：回归与故障模拟；PC 仅增加诊断兼容测试，
  未把调试面板的改造当作生产修复。
- 协议、ADR、SAFETY、README、交接文档：冻结边界及证据。

## 执行命令与原始结果摘要

已执行的基线/回归：

```text
./firmware/scripts/run_native_tests.sh                  PASS: 507 checks (baseline 269)
python3 -m unittest discover -s pc/tests ...            PASS: 21 tests (baseline 20)
python3 pc/tools/fanlink_cli.py --self-test              PASS
./firmware/tools/run_firmware_syntax_smoke.sh            PASS (stub only)
bash firmware/tools/run_c6_wire_compat_tests.sh <C6>     PASS: 1277 checks
python3 firmware/scripts/run_radiolib_recovery_tests.py  PASS (real library, simulated IO)
```

C6 的测试直接编译其实际 `components/fanlink/fanlink_protocol.h`，未用复制的编码器。
被核对的头文件 SHA-256：
`4de80abe35d75a2ebd0cc958d77265100f7099ab4e1b0c4703c2842485cbfb3a`。

真实 RadioLib 7.7.1、生产 adapter 和模拟 SPI/GPIO 故障结果：

```text
Repeated begin restores modem/frequency/sync/CRC/fixed-length configuration: PASS
BUSY stuck:    35,900 us, 1 reset attempt
Missing chip:  11,001 us, 1 reset attempt
Wrong version: 21,002 us, 1 reset attempt
TX_DONE lost:  25,013 us, PA idle / IRQ clear
SetTx/SetRx BUSY error: GPIO idle and staged mode cleared
IRQ cleanup error: first code retained, next successful operation clears IRQ
Patch migration/idempotence/fresh upstream/unknown-source rejection: PASS
```

上述时长只对应这几个模拟故障模型，**不是整个 begin 的绝对小于 40 ms 保证**。
每个 SPI/BUSY 阶段仍分别计时；极端的多次缓慢成功事务、调度抖动可更长。
507 项控制器测试另覆盖一次 600 ms begin：过期 Host 命令不会被恢复为低 PWM。

长时控制器回归（`make recovery-test`，**虚拟时间，不是实机耐久测试**）：

```text
PASS virtual 12h slots=4320000 micros_wraps=10 control_sequence_wraps=65
host_full_state_frames=216000 accepted_node1_replies=269986
TX_begin_calls=3 RX_begin_calls=3
PASS RX/TX failed-startup automatic recovery
PASS millis rollover and production 200ms/500ms host loss/recovery
PASS RX/TX silent radio timeout model: automatic recovery without I/O errors
```

12 小时主体注入 I/O 故障与一次 begin 失败；另有独立静默故障模型：发送返回 Ok
但不传输、接收只返回 Timeout，直到 radio.begin 才恢复。它验证无硬错误的场景也
会触发恢复，但不模拟射频传播、芯片发热或真实调度。

其余门禁：ASan/UBSan 507 checks（macOS 关闭不支持的 leak detection）、
CMake/CTest 1/1、Python/JavaScript、CLI 普通/优化模式通过。
三目标 clean PlatformIO 通过，固件/BUILD_INFO/日志的 SHA-256 均回读一致：

| 文件 | bytes | SHA-256 |
|---|---:|---|
| fanlink_tx_esp8285.bin | 294224 | 29835dd437a9db467b8e3e9de79b86ca74897b0dd3cb1fbb919bb5dd00976a6d |
| fanlink_rx_node01.bin | 289200 | 6866e610a7d1034d27f7f6d8310f2402d8b4971be8a8ac7fc718d2347317b303 |
| fanlink_rx_node16.bin | 289200 | 8fd2e16a661bcc47a6df07f70bef839a095f73f4db4eaee594ac7ff03802195d |

产物目录：`firmware/dist/20260907-radio-recovery/`（忽略的本地构建产物，不随 Git 提交）。
该目录的 `SOURCE_MANIFEST.build-input.sha256` 哈希为
`d3bd5e47e02103ec83aeca8d289df1d9fa336fc23d08deb11b3990acf7170963`，
与 `BUILD_INFO.json` 一致。构建后只补充测试/文档/门禁，全部实际编译输入（include、
src、lib、platformio.ini、RadioLib patch）再次逐项校验与构建时相同。
现有平台 adapter 包的依赖元数据仍为 `0.1.3-dev`；`0.1.4-dev` 为本轮应用工作树
标记，未创建发布 tag，也不作为实机版本读回证据。

可复现构建命令：

```bash
PLATFORMIO_CORE_DIR=/tmp/fanlink-pio-core-v013-preflight \
  uv run --with 'platformio==6.1.19' python3 firmware/tools/build_firmware.py \
  --tx --rx 1 --rx 16 --run-id <new-unique-run-id>
```

原始日志在 `build/radio-recovery-20260907-{platformio,runtime,sanitized,ctest,verify,driver,c6-compat}.log`。
PlatformIO 使用 Core 6.1.19、espressif8266 4.2.1、Arduino 3.1.2、RadioLib 7.7.1；
仅出现上游 elf2bin.py 的 Python 正则转义 SyntaxWarning，没有编译错误。

## C6 交接

C6 源码仍由用户指定的 ESPHome 任务维护，本仓库不复制 Wi-Fi 密钥或设备配置。
已下发：有限 UART flush 等待并处理真实返回枚举、失败全速且限频重试、成功全速
发送后再凭新鲜状态恢复控制、组件级故障注入、现有 RF/uptime/host-failsafe 诊断。
只修改/测试/编译，不 OTA、不重启、不设置生产 PWM、不添加刷机透传入口。

C6 无硬件流控，普通无线断链或对端断电不会让 UART 等待对端 ACK；无超时 flush
是明确的故障容错缺口，但尚不能定为本次数小时失联根因。`write_array` 自身的
ESP-IDF 同步写仍可能阻塞，有限 flush 超时不能被称为解决全部 UART 驱动锁死。

## 硬件与仪器

- 配置画像：JHEMCU 2.4GHz Lite RX 硬件用作 TX；BETAFPV 2.4GHz Nano 用作 RX。
- 接线沿用已有 GPIO/RF-switch 定义及 Node 01 PWM3/Tach1/低有效门极覆盖。
- 未修改频率、功率或 GPIO：TX +13 dBm，默认 RX -6 dBm PA drive（25 mW 档）。
  历史现场镜像不能由这些源码值推断，本轮没有读取实际刷入版本。
- 仪器：无。没有生产串口、RF、PWM、RPM、温度或供电测量；没有刷写任何设备。

## 尚未验证 / 风险

- 软件故障模型恢复不证明真实掉线的物理原因已经消除，也不保证 50 cm 零丢包。
- 固定频点干扰、近距离射频条件、供电/温升、硬件 BUSY/RESET 行为仍需台架数据。
- `missedPolls` 是成功清零的连续漏轮询，空节点 timeout 不能当实际节点丢包率。
- 整个 RadioLib API 无统一绝对 deadline；仍须实测精确 500 ms/10 ms 时序。
- 长时虚拟测试不等于数小时实机运行；需要重新获准刷写后再做硬件故障注入和长期测试。

## 下一步唯一动作

保持生产设备不变，等待 C6 任务回交其源码/测试结果；任何刷写需要用户另行明确安排。
