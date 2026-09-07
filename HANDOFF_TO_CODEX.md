# FanLink → Codex Hand-off

交接日期：2026-09-04  
当前工作树：Unreleased / `0.1.3-dev`  
已归档证据基线：`0.1.1`  
交接性质：当前工作树新增独立上下行 RSSI、BETAFPV 25 mW 档及 Node 01 专用
PWM/Tach/极性覆盖，已通过 manifest、`make verify` 和 TX/RX01/RX16 clean
PlatformIO 3/3 编译并生成可追溯产物；这些新功能仍未刷写和实机验收。

## 1. Owner intent

用户要的是一个极简专用无线风扇总线，而不是删减到难以维护的完整 ELRS fork：

```text
TX 每个时隙广播 duty[1..16]，并循环 poll_id=1..16
RX X 只应用 duty[X] 的 25 kHz PWM
RX X 只有在 poll_id=X 时回传 RPM
所有异常默认全速
```

不要恢复 ExpressLRS 的 Lua、MSP、板载/Wi-Fi WebUI、通用 RC、通用遥测、模型匹配、
动态功率等功能；本项目的本地 Host 控制面板不运行在 TX 上，不属于该非目标。

## 2. 当前源码架构

### 2.1 可移植公共层

`firmware/include/fanlink/`、`firmware/src/common/`、`firmware/src/app/`：

- CRC-16/CCITT-FALSE；
- 固定 30 字节 Control/RPM 包编解码；
- 固定帧头二进制 Host 协议，保留状态帧并新增独立上下行 RSSI 帧；
- TX 16 通道、poll、host watchdog、遥测表；
- RX 通道选择、序号排序、link watchdog、堵转状态、按需回复；
- `IRadioPort`、`IClock`、`IFanOutput`、`ITachometer` 平台边界。

公共层不包含 Arduino、SPI、RadioLib 或具体 GPIO，可直接在桌面测试。

### 2.2 ESP8266 平台层

`firmware/lib/fanlink_platform_esp8266/src/fanlink/platform/esp8266/`：

- `fan_output.*`：调用 ESP8266 Arduino Core PWM API，逻辑层固定 25 kHz，处理外部开漏级反相；
- `tachometer.*`：下降沿 ISR 只记录周期，主循环计算 RPM；
- `radiolib_sx1280_port.*`：RadioLib 7.7.1、SX1280 FLRC、固定频点、固定 30 字节收发、按 target 选择 LDO/DC-DC；
- `status_led.*`：BETAFPV Nano GPIO16 链路状态灯；
- `arduino_clock.hpp`：时钟接口。

### 2.3 Arduino 入口

- `firmware/src/main_tx.cpp`：Host 帧解析、TX 时隙循环、状态快照；
- `firmware/src/main_rx.cpp`：单节点 RX，无 Serial 初始化，避免占用 GPIO1/3。

### 2.4 工具与测试

- `firmware/tests/native_tests.cpp`：公共协议和状态机测试；
- `firmware/tests/stubs/`：只供平台代码语法 smoke；
- `pc/tools/fanlink_cli.py`：Host 二进制协议参考实现；
- `pc/tools/fanlink_panel.py` + `pc/panel/`：仅本地监听的 TX 浏览器控制面板；
- `pc/tests/test_fanlink_panel.py`：fake-serial、demo 和 HTTP/API 离线测试；
- `firmware/tools/build_firmware.py`：PlatformIO 逐目标 clean 构建，以唯一批次收集 `.bin`、日志、依赖和 SHA-256；
- `firmware/tools/simulate_cluster.py`：16 节点轮询计数模型；
- `firmware/tools/network_id.py`：从短语生成可重复 network ID，非安全机制。

## 3. 已归档的 `0.1.1` 检查

```text
./firmware/scripts/run_native_tests.sh
  -> PASS: 209 checks

python3 pc/tools/fanlink_cli.py --self-test
  -> PASS: fanlink_cli self-test

PYTHONOPTIMIZE=1 python3 pc/tools/fanlink_cli.py --self-test
  -> PASS: fanlink_cli self-test

python3 -m py_compile pc/tools/*.py firmware/tools/*.py firmware/scripts/*.py
  -> PASS

./firmware/tools/run_firmware_syntax_smoke.sh
  -> PASS: firmware syntax smoke (stub APIs only)

cmake -S firmware -B build/cmake-firmware -DCMAKE_BUILD_TYPE=Debug
cmake --build build/cmake-firmware --parallel
ctest --test-dir build/cmake-firmware --output-on-failure
  -> PASS: 1/1

PlatformIO Core 6.1.19 clean build with espressif8266@4.2.1,
Arduino ESP8266 Core 3.1.2 and RadioLib 7.7.1:
  fanlink_tx_esp8285    -> PASS
  fanlink_rx_node01     -> PASS
  fanlink_rx_node16     -> PASS
```

以上 `209 checks` 和 3/3 clean build 只覆盖 `0.1.1` 证据快照，不覆盖当前
Unreleased 工作树。该历史测试覆盖了：

- CRC 标准向量；
- Control/RPM round-trip 和 CRC 拒绝；
- 两种空口包固定 30 字节；
- Host parser 重同步；
- poll 1→16；
- Host failsafe；
- 回包 node/sequence 匹配；
- RX 通道选择；
- 只在本节点 poll 时回包；
- RX link failsafe；
- FORCE_FULL_SPEED；
- sequence gap；
- 明显旧序号拒绝；
- 相同序号重试；
- 16 位序号回绕；
- 错 network ID 和坏 CRC 拒绝。

## 4. 当前证据边界

- 当前 Unreleased 源码的 native 与 ASan/UBSan 各 269 项、面板 20 项、CLI 双模式、
  Python/JavaScript、firmware-smoke、CMake/CTest 1/1、source manifest 和
  `make verify` 均已通过；匹配源码的 TX/RX01/RX16 clean build 为 3/3 PASS；
- 控制面板通过 `/dev/cu.usbmodem<device>`（USB `1A86:55D3`；公开记录已隐去设备唯一编号）连接真实 TX；
- 实机快照显示 `host_failsafe=false`、uptime 3130 s、`next_poll_id=2` 且解析错误
  计数为 0；浏览器显示同一数据；
- POST disconnect 只证明面板状态立即清为全 `255`/stale；TX failsafe 的实机证据
  来自随后独立 CLI 连续显示 `TX=FAILSAFE` 和全部 duty `255`，未量出精确 500 ms；
- 后端进程退出后，页面下一轮询显示“本地服务不可达”、锁控件，并把 watchdog、
  RPM、实际 duty、age、miss 清为未知；独立 CLI 再次显示 `TX=FAILSAFE`/全 `255`；
- 未捕获精确 200 ms 串口波形，旧 TX 固件没有新 `0x82`，也未做 USB 拔出、
  串口 I/O 异常/重连；
- 后续已刷 `0.1.2` 的 TX/RX01 建立了 RF 控制和 RPM Reply：RX 灯常亮，用户确认
  控制有效，一次快照为 requested/applied=143、RPM=1749、age=0、miss=0；旧镜像
  duty=0 仍约 2787 RPM，证明 Node 01 物理极性反向；
- 已按 ExpressLRS targets commit `c4bd7b823594c233e673828ab493a2f8319a756a`
  建立 JHEMCU 2.4GHz Lite RX 硬件改 TX 和 BETAFPV 2.4GHz Nano RX
  配置，但未核对用户实物的 PCB revision、原固件 target 和刷写恢复路径；
- 未用实板确认 SX1280/SX1281、SPI、BUSY、DIO1、TCXO、DCDC 电感和 PA/LNA/RF switch；
- 未实测新 `0.1.3-dev` 的功率、双向 RSSI、Node 01 脚位/极性；
- 未测量 10 ms 时隙是否有足够余量；
- 未用示波器验证 25 kHz PWM；
- 未测量 GPIO1 启动输出；
- 未测 Tach 电压或 RPM 精度；
- 未做多节点碰撞、长时间或法规测试。

当前实机最终运行的是 `0.1.2` TX/RX01。虽无设备 MD5/readback，但上述应用行为
证明旧版本已启动；不得把它外推为当前 `0.1.3-dev` 二进制验收。

当前源码门禁和实机记录见 `docs/evidence/20260904-tx-panel-usb.md` 与
`docs/evidence/20260904-v013-power-node01-rssi.md`。

禁止把 stub smoke、桌面测试或 clean 编译描述为“刷写/硬件已通过”。

## 5. 关键设计决定

1. **独立 FanLink 协议，不复制 ELRS 源码。**
2. **每包完整 duty[16]。** 下行无需确认或增量恢复。
3. **显式单节点 poll。** 同一时隙只有一个合法回复者。
4. **上下行都固定 30 字节。** 简化所有节点的固定长度接收。
5. **二进制 Host 协议。** CRSF 暂不实现；将来只能作为适配器。
6. **固定频点优先。** 先解决板级 RF 闭环，再单独设计 rendezvous/FHSS。
7. **异常全速。** 软件 watchdog 与开漏硬件默认状态共同保证。
8. **有界阻塞 RadioLib 作为 bring-up 基线。** SPI/BUSY/TX_DONE
   硬上限为 25 ms；实测证明 10 ms 时隙不足时再仅替换适配层。
9. **控制面板 duty 是开环命令。** `255` 表示释放外部开漏级并请求全速；RPM
   只是 Tach 遥测，不把 duty 伪装成闭环转速目标。

完整理由见 `docs/DECISIONS.md`。

## 6. 已完成的真实依赖嵌入式构建

当前 `0.1.3-dev` 同源码 clean 批次：

```text
dist/20260904-v0.1.3-25mw-node01-rssi-reviewed/
fanlink_tx_esp8285.bin
fanlink_rx_node01.bin
fanlink_rx_node16.bin
```

`BUILD_INFO.json`、`SHA256SUMS`、逐文件校验和与完整 build log 已生成并回读。
精确尺寸和 digest 以该目录内文件为准，不在 manifest 覆盖的交接文档重复硬编码；
不同 clean 批次可能包含不同构建元数据。该结果不证明任何镜像已刷入或运行。

只对应 `0.1.1` 历史基线的可复核 clean 批次位于
`dist/20260904-jhemcu-lite-betafpv-nano-reviewed/`。重建命令：

```bash
PLATFORMIO_CORE_DIR=/tmp/fanlink-pio-core \
  uv run --with 'platformio==6.1.19' python3 firmware/tools/build_firmware.py \
  --tx --rx 1 --rx 16 --run-id <new-unique-run-id>
```

以后处理依赖/API 差异时：

- 保持 `IRadioPort` 公共接口不变；
- 不改变 30 字节 wire format；
- 不删除应用 CRC；
- 不用“编译方便”为理由引入完整 ELRS；
- 将每个修复写入 `CHANGELOG.md` 和 `HANDOFF_STATUS.md`。

升级 RadioLib 时必须重新核对：

- `Module` SPI 构造函数；
- `beginFLRC` 参数类型；
- `setSyncWord`；
- `setCRC`；
- `receive(data, len, timeout)` 的 timeout 单位和固定长度行为；
- `setRfSwitchPins` 是否足以覆盖目标板前端。

## 7. 当前第一优先级：刷入 RX01，再刷 TX 验收双向 RSSI

保持风扇断开，按 `docs/CONTROL_PANEL.md` 继续补齐：

1. 让 BETAFPV RX Node 01 进 ESP8266 ROM BOOT，刷当前 final RX01 bin；
2. 重启后确认 GPIO16、GPIO3 PWM、GPIO1 Tach，并实测 duty 0/255 方向与 RPM；
3. 让 JHEMCU TX 进 ROM BOOT，刷 final TX bin；
4. 重启后确认原状态帧兼容并验收 `0x82` 两列 RSSI、未知和过期行为；
5. 再补精确 Host/watchdog 时序、RF 功率/PER、PWM/Tach 波形与长期热测试。

现有实机数据证明旧 `0.1.2` Host、Node 01 控制和 RPM Reply 路径；不得外推为
新功率、RSSI、脚位/极性、精确时序或仪器测量通过。

## 8. 第二优先级：用实物确认硬件 target

产品名已确认，但任何后续可验收刷写前仍必须为 TX/RX 实物分别核对：

```text
板名与 PCB revision
原 ELRS target / hardware.json / 原理图来源
MCU、flash、晶振
SX1280/SX1281 型号
NSS / DIO1 / RESET / BUSY / SPI
TCXO 控制
RXEN / TXEN / PA enable / LNA enable
天线 switch 真值表
最大安全发射功率
GPIO1/GPIO3 与 USB-UART/板载器件
可恢复刷写方式
```

当前 `15/4/2/5` 是对应上述两个官方 target 的配置基线，但在
实物 PCB revision 核对前仍不能视为用户手上商品板的结论。

## 9. 第三优先级：单节点 bring-up

推荐顺序：

1. 不接风扇，最小功率验证 TX/RX SPI 和固定包收发；
2. 只做 TX→RX，暂时禁止 RX 发射；
3. 用调试脚/逻辑分析仪证明 `sequence` 和本节点 duty；
4. 接开漏控制级，用示波器测 25 kHz；
5. 验证 0/1/64/128/192/254/255；
6. 验证 TX 掉电、RX 复位、坏 CRC 均回全速；
7. 测 Tach 高电平后才连接 GPIO；
8. 验证 RPM；
9. 开启一个 RX Reply，测实际 turnaround；
10. 调整 `replyDelayUs`、`replyTimeoutMs` 和 `slotPeriodUs`。

## 10. 多节点前必须解决的事项

- 每个节点 ID 唯一且有物理标签；
- 确认未被 poll 的 RX 不发射；
- 缺失节点只增加 missed poll，不延迟下一时隙；
- 轮询周期、状态快照和 Host 串口输出不会破坏时隙；
- 重复 node ID 的碰撞现象被记录；
- 至少 4 节点通过后再扩展到 16。

## 11. 安全关键事项

- 标准风扇 Pin 3 是 Tach，Pin 4 是 PWM；
- PWM 必须通过外部开漏/开集电极级；
- Tach 必须确认是 3.3 V 安全电平；
- GPIO1 在 ESP8266 ROM 启动阶段可能输出串口信息，必须测量；
- 外部晶体管 gate/base 必须有安全默认下拉，使 MCU 复位时释放风扇 PWM 线；
- 无线控制不能替代设备自身的过温保护和关机机制。

## 12. 不要在 P0 做的工作

- 板载 Wi-Fi/WebUI（本地 Host 控制面板除外）；
- Lua/MSP/标准 ELRS兼容；
- 完整 CRSF；
- 动态节点发现；
- OTA；
- 动态功率/速率；
- 通用传感器框架；
- 代码风格大重构；
- 在固定频点闭环前实现 FHSS。

## 13. 下一次回交用户必须包含

- 精确支持的 TX/RX 板型与 revision；
- 完整可复现构建日志；
- TX 控制面板真实串口、状态/错误帧和 watchdog 验收记录；
- 固件 `.bin` 与 SHA-256；
- 最终引脚/RF 前端真值表；
- 接线图与器件值；
- 25 kHz 示波器测量；
- Tach 电压与 RPM 对比表；
- RF 时隙/turnaround 测量；
- failsafe 故障注入结果；
- 节点数量、运行时长、丢包/复位统计；
- 所有仍未验证和明确不支持的事项。

接手提示词见仓库根目录 `codex_prompt.txt`。
