# FanLink 冻结需求规格 v0.1

文档日期：2026-09-04  
状态：Codex 接手基线。除非用户明确修改，MUST 条目不得擅自扩大、削弱或换成通用遥控框架。

## 1. 唯一产品目标

构建一个专用无线风扇总线：

```text
1 个 TX：周期广播 CH1..CH16，并循环 poll_id=1..16
N 个 RX：节点 X 只应用 CHX，收到 poll_id=X 时回传本机 RPM
```

控制丢失、主机丢失、MCU 复位和配置不可信时，系统必须优先让风扇趋向全速。

## 2. 固定系统边界

- 节点 ID：1–16；同一网络内必须唯一。
- 每个 RX：1 路 25 kHz 风扇 PWM + 1 路 Tach 输入。
- TX 每个时隙广播完整 `duty[16]`，不做增量更新。
- 每个时隙最多授权一个 RX 发射。
- 首版采用固定频点和固定射频参数；FHSS 是后续独立里程碑。
- 首版目标硬件抽象为 ESP8285/ESP8266 + SX1280，但协议与控制状态机不能依赖 Arduino 或具体射频库。
- 不兼容标准 ELRS OTA；不得导入完整 ExpressLRS 应用层。

## 3. 功能需求

### FR-001 节点与通道

系统 MUST 支持 `node_id=1..16`。RX X MUST 只读取 `duty[X-1]`。节点 0 不得作为普通 RX。

### FR-002 完整状态广播

TX MUST 在每个控制时隙发送全部 16 个 duty 字节。丢失一个下行包后，下一个有效包必须无需重传历史即可恢复最新完整状态。

### FR-003 占空比语义

- `0` 表示风扇 PWM 控制线逻辑高电平占空比 0%；
- `255` 表示逻辑高电平占空比 100%，即全速命令；
- 中间值线性映射；
- 外部开漏晶体管造成的逻辑反相只允许在 PWM 平台层处理，不能改变空口语义。

### FR-004 轮询

TX MUST 默认按 `1,2,...,16,1,...` 循环 `poll_id`。RX MUST 仅在 `poll_id == node_id` 时发送 RPM Reply。没有匹配 poll 时不得主动占用回程空口。

### FR-005 RPM 回传

RPM Reply MUST 至少携带：

- `network_id`；
- `sequence_echo`；
- `node_id`；
- RPM；
- 实际应用 duty；
- 节点状态位；
- 自上次成功回传后的估计缺包数；
- RX 接收该 Control 包时测得的下行 RSSI；
- CRC。

TX MUST 同时校验网络 ID、当前 poll 节点、当前 sequence 和 CRC 后才更新节点表。

### FR-006 序号

TX MUST 对每个控制包递增 16 位序号。RX MUST：

- 接受第一个有效序号；
- 接受正常前进和 `65535→0` 回绕；
- 记录前进跳号造成的缺包；
- 拒绝明显倒退的旧序号，且旧包不得刷新链路 watchdog；
- MAY 接受相同序号作为当前轮询重试，并再次回包。

### FR-007 网络隔离

所有空口包 MUST 带 32 位 `network_id`，不同 ID 的包必须丢弃。`network_id` 只用于逻辑过滤，不得宣称提供加密、认证、防重放或抗干扰安全。

### FR-008 25 kHz PWM

RX MUST 输出标称 25 kHz 的四线风扇控制波形，并正确支持静态 0% 和 100% 端点。实现 MAY 使用 ESP8266 Arduino Core `analogWrite`，但必须在实机用示波器验证实际频率、占空比、RF 期间抖动和复位瞬态。

### FR-009 Tach

- 默认 2 脉冲/转，编译期可改；
- 下降沿 ISR 只记录时间戳/周期；
- 除法、滤波、堵转判断和封包在主循环执行；
- Tach 超时必须回传 0 RPM 和状态位，不能让控制状态机阻塞。

### FR-010 上位机接口

TX MUST 提供本项目自定义的固定头二进制串口协议：

- `SET_ALL_DUTY`：16 字节；
- `SET_ONE_DUTY`：节点 + duty；
- `HEARTBEAT`；
- `STATUS_SNAPSHOT`：聚合 16 节点状态。
- `LINK_METRICS_SNAPSHOT`：按节点分别报告上行与下行 RSSI。

首版不要求 CRSF。未来添加 CRSF 时只能作为独立 Host Adapter，不得把完整 CRSF 帧直接塞入空口，也不得修改 FanLink v1 空口包布局。

### FR-011 无动态内存

固件时隙主路径 SHOULD 使用固定数组，不使用 Arduino `String`、递归或运行时堆分配。

### FR-012 RX 链路指示灯

BETAFPV Nano RX MUST 使用板载 GPIO16 状态灯：未收到可接受的 FanLink Control
包或链路 watchdog 超时时，按“亮 100 ms、灭 100 ms、亮 100 ms、灭 500 ms”
循环；最近 `FANLINK_RX_LINK_TIMEOUT_MS`（默认 500 ms）内至少收到一个完整通过
长度、magic、版本、类型、poll ID、应用 CRC、network ID 和 sequence 接受规则的
控制包时常亮。实现 MUST 非阻塞。`HOST_FAILSAFE/FORCE_FULL_SPEED` 表示上位机或
风扇安全状态，不改变该无线下行指示。

### FR-013 TX 本地控制面板

Host MAY 提供只监听本机回环地址的浏览器控制面板，通过 FR-010 的既有二进制
串口协议连接 TX。面板 MUST：

- 初始、断连、串口异常和重新连接时把本地 16 路请求恢复为 `255`；
- 连接后以低于 500 ms Host watchdog 的周期发送完整 `duty[16]`；
- 严格校验节点 ID 和 `0..255` duty，不把空值或非法输入静默改成 `0`；
- 显示 `STATUS_SNAPSHOT` 的 RPM、实际 duty、age、miss 和状态位；
- 分开显示 RX 报告的下行 RSSI 与 TX 测得的上行 RSSI，并把未知/陈旧值明确标出；
- 明确区分开环 PWM duty 设定与 RPM 回传，不宣称闭环转速控制；
- 默认不监听局域网地址；显式远程监听时必须警告当前 API 没有身份认证。

控制面板不得改变 FanLink v1 空口格式或 TX Host watchdog 语义。页面显示和 API
成功响应也不得替代真实 RF、PWM、RPM、温度与失效保护验收。

## 4. 空口与时序基线

- Control：固定 30 字节；
- RPM Reply：固定 30 字节；
- 小端；
- CRC-16/CCITT-FALSE；
- 默认时隙：10,000 µs；
- 默认 RX 回复延迟：1,200 µs；
- 默认 TX 回复等待：5 ms；
- 默认射频：SX1280 FLRC、固定频点；
- 具体字节偏移以 `PROTOCOL.md` 为唯一规范源。

缩短时隙或改变射频参数前，必须测量并记录：下行 airtime、TX→RX/RX→TX 切换、MCU 编解码时间、上行 airtime、RF 开关稳定时间和保护间隔。

## 5. 失效保护需求

### SR-001 RX 链路失联

RX 在默认 500 ms 内没有收到长度、magic、版本、类型、CRC、network ID 和序号均可接受的控制包时，MUST 释放风扇控制线并进入全速状态。

### SR-002 TX 上位机失联

TX 在默认 500 ms 内没有收到有效 duty/heartbeat 时，MUST 广播 `duty[16]=255`，并设置 `FORCE_FULL_SPEED | HOST_FAILSAFE`。

### SR-003 启动与异常

RX 在上电、复位、PWM 初始化失败、无线初始化失败或主循环未就绪时 MUST 让风扇趋向全速。软件措施必须由外部开漏级默认关闭的硬件状态补强。

### SR-004 电气限制

- 风扇 Pin 4 PWM MUST 通过开漏/开集电极级驱动；
- 风扇 Pin 3 Tach MUST 在确认电压安全后进入 3.3 V GPIO；
- 12 V 风扇电源不得进入 MCU GPIO；
- 风扇、RX 和控制晶体管必须共地；
- GPIO1 启动串口瞬态必须在实机测量。

### SR-005 无线遥测失败隔离

某个节点无回复、回复 CRC 错或节点掉线不得阻塞后续控制广播及其他节点轮询。

## 6. 明确非目标

v0.1 不实现：

- ELRS OTA/绑定/遥测兼容；
- CRSF 路由器、MSP、Lua、设备端 Wi-Fi/WebUI；
- 动态功率、速率扫描、模型匹配；
- 动态节点发现或自动地址；
- 通用 Servo/PWM/传感器框架；
- OTA 更新；
- 加密认证；
- 未经测量的 FHSS。

## 7. 验收标准

### AC-001 桌面测试

以下命令必须通过：

```bash
./firmware/scripts/run_native_tests.sh
python3 pc/tools/fanlink_cli.py --self-test
python3 -m py_compile pc/tools/*.py firmware/tools/*.py firmware/scripts/*.py
python3 -m unittest discover -s pc/tests -p 'test_fanlink_panel.py' -v
node --check pc/panel/app.js
./firmware/tools/run_firmware_syntax_smoke.sh
```

### AC-002 真实嵌入式编译

在确定板型后，必须用干净 PlatformIO 环境构建 TX、RX1 和 RX16。API stub 语法测试不能替代此项。

### AC-003 板级核对

每个支持的 TX/RX 板型必须有：准确 PCB 版本、原 target/原理图来源、SPI/DIO/BUSY/RESET、PA/LNA/RF 开关真值表、可用 UART pad 和恢复烧写方法。

### AC-004 PWM

示波器确认 duty `0,1,64,128,192,254,255`，并记录：频率、逻辑反相、启动瞬态、RF 收发时波形、失联恢复全速。

### AC-005 Tach

测量 Tach 高电平；以独立转速计或已知风扇对比多个转速点；断线时必须在超时后报告 0 RPM/TACH_TIMEOUT。

### AC-006 单节点闭环

一套 TX+RX 连续运行至少 30 分钟，控制、回包、sequence 匹配和 failsafe 全部通过后，才能扩展节点。

### AC-007 多节点

至少 4 个唯一 node ID 同时运行，证明只有被 poll 节点发射、缺失节点不阻塞、通道互不串扰。最终以实机或测试夹具验证完整 1–16 轮询。

### AC-008 故障注入

必须覆盖：主机进程退出、串口拔出、TX 掉电、RX 复位、CRC 错、错 network ID、Tach 断线、重复 node ID、RF 前端配置错误。

## 8. 实机适配所需输入

Codex 在硬件阶段需要集中确认：

- TX、RX 的品牌、型号、PCB 版本和照片；
- 原 ExpressLRS target 名称或硬件定义；
- MCU 与射频芯片型号；
- SX1280 的 SPI/DIO1/BUSY/RESET；
- TCXO、PA、LNA、RF switch、天线切换逻辑；
- PWM/Tach 实际焊盘与启动状态；
- 使用地区和允许频段/功率；
- 外部晶体管、电阻和 Tach 电平保护电路。
