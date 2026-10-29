# FanLink Protocol — Host v2 / RF v1

## Read-only tach diagnostic query (2026-09-11, ADR-027; candidate)

Separate fixed30-byte RF types0x42 request/0x43 reply, encapsulated unchanged
in Host v2 types0x16/0x94. No Control/RPM reserved bytes are repurposed. Header
offset0..3 is F1 A5 01 TYPE, network uint32LE@4, nonzero session uint32LE@8,
node1..8@12, CRC16LE@28 over0..27. Request13..27 must all be zero.

Reply13 is flags (bit0=tach enabled; bits1..7 zero). rawEdges uint32LE@14,
validPeriods uint32LE@18, filterRejects uint32LE@22, raw lastEdgeAgeMs uint16LE@26.
Age65535 means never observed; known age floors microseconds to milliseconds
and saturates65534. Counters saturate32 bits since boot; no query clears them.
rawEdges includes ISR queue overflow; validPeriods counts newly trusted
estimator updates, not snapshot reads; rejects excludes warmup. Extra local
overflowDrops instrumentation is not included in this minimal wire reply.

TX matches exact envelope/network/node/session and echoes Host sequence; bad
or late responses never refresh normal RPM/node presence. Requests are explicit,
limited to one exchange per second, no automatic RF retry, and at most20ms
reply observation after transmission. TX starts after a normal control slot;
normal control and heartbeat parsing remain live. OTA/settings ownership excludes
diagnostics. RX diagnostic replies do not change duty, pin mode, persistence,
control watchdog timestamps or sampling state. Existing fail-safe still applies.
Old firmware need not understand this optional query; timeout is unknown, not
zero. Both TX and RX upgrades are required; this change is not deployed.
Usage and interpretation: docs/TACH_DIAGNOSTICS.md.

## Current compact8 contract (2026-09-08, ADR-025)

Host 串口所有外层帧统一 `VERSION=2`：控制、状态、RSSI、诊断、OTA、设置及
重启/BOOT 均使用同一 v2 封装。仅有 8 个节点槽：SET_ALL payload 为 8 bytes，
STATUS 为 54 bytes（6 + 8×6），METRICS 为 16 bytes（8×2）。不保留 wire16、
补齐、截断或 v1/v2 双栈。8-byte header、2-byte CRC 和 `MAX_PAYLOAD=128`
保持不变，以容纳 OTA 和诊断；payload 上限不是冗余通道。

RF 仍为 version 1、固定 30 bytes：Control 有 8 个 duty 和 8 个发送 255 的
reserved 字节，后者不是节点槽。RF OTA/设置及维护 payload 字节不变。
正常轮询 1..8、每 125 ms 一槽、一秒一轮；RX/Host watchdog 仍为 500 ms。
ADR-024 的 5 miss / 6 s presence 与 2.5 s freshness 不变。ADR-025 替代
ADR-023 中保留旧 16 路 Host envelope 的决定，不改写旧构建/现场证据。

TX、生产 C6 和 PC 必须协调升级。当前 v2 工具不能与旧 v1 TX 交互（包括请求
BOOT/OTA）；旧 TX 只能使用归档 v1 工具迁移，或经用户另行授权有线 ROM 写入
v2。RX 的 RF 字节兼容，无需为本次 Host 迁移立即刷写。本轮不刷设备，协议/
软件验证不代表硬件验收。

## Host connection tolerance (2026-09-08, ADR-024)

Connection presence is distinct from telemetry freshness. A previously online
active node remains online only with a trusted, fresh Host STATUS transport,
age !=255, missed_polls<5 and combined reported age plus snapshot age<6000ms.
Either five consecutive missed own-node polls OR age>=6000ms makes it offline.
Never-seen/inactive nodes and an untrusted/lost Host transport are offline.
Initial connection or recovery requires a fresh non-stale sample with miss=0,
not merely another old STATUS within the tolerance interval. Receiving a STATUS
does not itself constitute a new RX reply. Old data must not revive the latch.

Telemetry remains stale at the Host when age>=2500ms or status bit7 is set;
RPM/applied duty/RSSI must not be presented as current in the tolerance window.
Existing TX bit7 and age quantization are unchanged (TX uses >2500ms on its
exact clock; Host uses conservative >=2500ms combined age). Misses are counted
by TX once per failed own-node poll, never per STATUS or per CRC event. The
existing accepted-reply validation alone resets misses. ADR-024 alone required
no wire or RX/TX image change; ADR-025 above separately changes Host wire/TX.
C6 implements presence separately; old hosts remain stricter.
RX/Host500ms watchdogs, full-state125ms broadcasts, and radio recovery remain
unchanged. Presence tolerance is not confirmation of current fan rotation.
STATUS age is rounded down in100ms units; the6s threshold is a reported-age
bound with quantization/transport latency, not an exact physical deadline.

## Optional software radio trace (2026-09-08)

TX uses existing ERROR_EVENT text; RX emits a newline-terminated UART line only
in UART pin mode. Neither changes the air packet or C6 control/status envelopes.
Grammar: `RTE gGEN kKIND iINDEX cCOUNT dDROPPED wOVERWRITTEN tMICROS eEVENT vVALID pPINS aVALUE rRESULT`.
All fields decimal except VALUE (eight uppercase hexadecimal digits). INDEX is
zero-based, COUNT<=32. GEN skips zero on wrap; group by port and boot epoch, not
GEN alone. KIND1=CRC,2=I/O,3=sampled successful receive (every64). Snapshots are
immutable while draining; DROPPED counts later freeze triggers skipped because
one snapshot was pending. OVERWRITTEN is cumulative rolling-history replacement,
not lost UART bytes. Both counters saturate; values are frozen at snapshot time.
VALID bits:1 timestamp,2 pins,4 value,8 result. PINS bit0 DIO1, bit1 BUSY.
MICROS and GPIO levels are software samples, not edge times or synchronized
TX/RX clocks. Results are clamped signed16 driver return codes.

EVENT1..18: TX begin/end, RX arm begin/stage-end/launch-begin/end, DIO observed,
standby end, IRQ read, RX-buffer-status read, packet-status read, FIFO read, IRQ
clear begin/end, RX result, TX context, RX context, fault cleanup. Context VALUE
packs packet type[31:24], node/poll[23:16], sequence[15:0]; raw context is not proof
of authenticated/accepted identity. IRQ VALUE is16bits; buffer status packs
length then offset; packet status packs bytes1..4 low-to-high. A successful clear
return is not IRQ-zero readback. A GPIO low sample cannot rule out short pulses.

Each port drains at most one trace record per25ms, outside maintenance. TX
normal status/metrics and six existing text families keep priority/fairness; RX
regular logger owns its due loop. No UART flush or extra SPI for tracing. Full
32-record output takes at least800ms and is sampled evidence, not an exhaustive
bus capture. Missing indices, restart, parser errors and initial buffered data
must be reported before drawing timing conclusions.

0.2.0-dev / ADR-025：活动节点 1..8，正常轮询间隔 125 ms。保留 RF Control 30B，
Host v2 使用 SET_ALL 8B、STATUS 54B、METRICS 16B，不存在节点 9..16。
新增 Host 0x15/0x93、RF 0x40/0x41 设置协议见 `docs/CONFIG_WIRE.md`。
单节点遥测新鲜度 2500 ms；下行与 Host watchdog 仍 500 ms。

2026-09-08 长测修复：可选 RFR/RFN/RFT/RF/RFC/RFE 改为有界公平轮转，
STATUS/METRICS 仍优先。FIFO 暂不足不消耗该族周期、保留轮转位置等待；
成功或永久不可发送才消耗 1 秒周期，最多每 loop 尝试一帧。总长 >=128B
的文本仍丢弃，但不会挡住其他族。以下旧“跳过且保持节流/旧诊断优先”
仅描述历史实现；字段、grammar 和空口协议未变。

0.1.10-dev 新增 TX 本地维护 Host types 0x12/0x13/0x14 与回复 0x91/0x92；
字节定义和操作边界见 `docs/TX_UPDATE.md`、ADR-020。现有 RF 与 Host 类型不变。

本文件定义字节级协议。未经提升 `version`，不得改变字段偏移、长度、字节序或 CRC 范围。

## 1. 通用约定

- 字节序：小端；
- 应用层 CRC：CRC-16/CCITT-FALSE；
- 多项式：`0x1021`；
- 初值：`0xFFFF`；
- RefIn/RefOut：false；
- XorOut：`0x0000`；
- 校验向量：ASCII `123456789` → `0x29B1`；
- 网络 ID：32 位编译期值；
- 节点 ID：`1..8`；
- `0` 不是有效节点，可作为“不轮询”。

## 2. 空口控制包

固定长度：**30 bytes**。

| Offset | Size | 字段 | 说明 |
|---:|---:|---|---|
| 0 | 1 | magic0 | `0xF1` |
| 1 | 1 | magic1 | `0xA5` |
| 2 | 1 | version | `0x01` |
| 3 | 1 | type | `0x10` Control |
| 4 | 4 | network_id | uint32 LE |
| 8 | 2 | sequence | uint16 LE，逐包递增 |
| 10 | 1 | poll_id | `1..8` |
| 11 | 1 | flags | 见下表 |
| 12 | 8 | duty[8] | Node 1..8，`0..255` |
| 20 | 8 | reserved | 发送 255；接收忽略，不是额外通道 |
| 28 | 2 | crc16 | 对 offset `0..27` 计算 |

控制 flags：

| Bit | 名称 | 含义 |
|---:|---|---|
| 0 | FORCE_FULL_SPEED | RX 忽略 duty，立即全速 |
| 1 | HOST_FAILSAFE | TX 因上位机超时而进入 failsafe |
| 2..7 | Reserved | 必须发送 0 |

RX 只有在长度、魔数、版本、类型、CRC、network_id 和 sequence 顺序均可接受时，才应用新控制并刷新链路超时。序号按 16 位模空间判断：向前差值 `1..32767` 接受；明显倒退值拒绝；`65535→0` 视为正常前进。

相同 sequence 只在链路仍新鲜、整个 Control 内容与上次包完全一致时允许重回包；
它不刷新 500 ms 新控制 watchdog，也不重复应用旧 duty。相同 sequence 却更改
`poll_id`/flags/duty 的包必须拒绝。

冷启动接受第一个有效序号。链路超时或射频重初始化后保留最后序号基线并保持全速：
严格前进的新帧可以恢复；重复包不能解除保护。若 TX 重启后的序号落在旧基线后方，
RX 必须先已超过旧控制的 watchdog，再在一个 watchdog 周期内收到两个严格前进的
候选包，第二包才建立新基线并应用控制。候选重复/倒退不刷新期限。这是 ADR-014
的失效恢复语义，不改变 v1 字段，也不提供认证或防重放安全。

## 3. 空口 RPM 回包

固定长度：**30 bytes**。控制包与回包等长，使所有节点始终使用同一固定接收长度。

| Offset | Size | 字段 | 说明 |
|---:|---:|---|---|
| 0 | 1 | magic0 | `0xF1` |
| 1 | 1 | magic1 | `0xA5` |
| 2 | 1 | version | `0x01` |
| 3 | 1 | type | `0x11` RPM Reply |
| 4 | 4 | network_id | uint32 LE |
| 8 | 2 | sequence_echo | 回显本次控制包 sequence |
| 10 | 1 | node_id | `1..8` |
| 11 | 1 | status | 见下表 |
| 12 | 2 | rpm | uint16 LE |
| 14 | 1 | applied_duty | 实际使用的逻辑 duty |
| 15 | 1 | missed_downlinks | 自上次成功回包后估计的缺包数，饱和 255 |
| 16 | 1 | downlink_rssi_dbm | int8 二补码 |
| 17 | 11 | reserved | 发送 0；v1 接收端忽略 |
| 28 | 2 | crc16 | 对 offset `0..27` 计算 |

RX status：

| Bit | 名称 | 含义 |
|---:|---|---|
| 0 | LINK_FAILSAFE | 当前/最近进入过下行失联保护 |
| 1 | TACH_TIMEOUT | Tach 暂无有效周期 |
| 2 | STALL | 有明显驱动命令但超过宽限期仍为 0 RPM |
| 3 | SEQUENCE_GAP | 检测到下行 sequence 跳变 |
| 4 | RADIO_ERROR | 最近有射频 I/O 错误 |
| 5 | CONFIG_ERROR | node_id 或配置非法 |
| 6 | Reserved | 0 |
| 7 | TELEMETRY_STALE | 仅由 TX 在上位机快照中合成，不由 RX 发送 |

TX 接受 RPM 回包必须同时满足：

```text
network_id == local_network_id
node_id == current_poll_id
sequence_echo == current_control_sequence
CRC valid
```

否则丢弃，不误更新其他节点。

## 4. TX 时隙

首版默认：

```text
T = 0 us       TX 发 30-byte Control
T = tx_done    TX 转 RX
T < 5 ms       等待 30-byte RPM Reply
T = 125 ms     下一个 Control slot（间隙持续处理 Host；八节点一秒一轮）
```

RX 在收到属于自己的 poll 后等待默认 `1,200 us` 再发回复。该数值只是起点，必须根据真实模块的 DIO/BUSY、包 airtime 与 RF switch 实测调优。

`0.1.6-dev` 候选将此等待明确为从首次 DIO 高电平的**软件观察**起算的剩余量，
不再把 FIFO/metadata/rearm/PWM 时间之后额外加满 1200 µs。它不是物理 RX_DONE
边沿时间，也不是实际 RF 发射开始时间。观察到准备调用 transmit 已达 5000 µs
时跳过过期回复（仍可应用新鲜控制）；该上限与 TX build reply timeout 共用配置。
TX 以观察 offset `<5000 µs` 为时间门槛，即使读取处理完成已超窗口也可接受，
但必须继续通过全部 envelope/network/node/sequence/CRC 检验。无观察元数据的
适配器保守按返回时间判定。精确微秒等待不丢弃最后不足 1 ms 的预算；时隙、
空口布局与 500 ms 安全保护不变。软件观察本身若迟到仍会拒收，不能用它证明
硬件边沿准时。sync-only FLRC IRQ 显式映射到 DIO，成功配置后才启动 RX。

`0.1.5-dev` 适配器使用 `SetRx(0)` 单包接收且无硬件超时，调用者仍以软件
deadline 有界返回。普通软件 Timeout 不关闭 RX，下一调用可取走期间完成的包；
成功/CRC 包处理后立即重新开启下一次接收。TX 发射前丢弃旧 RX 状态，原有当前
poll/sequence 校验不变。不是 `SetRx(0xFFFF)` 连续模式，不改变上述时间参数。

每个时隙只允许一个节点回传，不做竞争退避。

## 5. 上位机串口协议

串口默认：`115200 8N1`。

帧结构：

| Offset | Size | 字段 |
|---:|---:|---|
| 0 | 1 | magic0 = `0x46` (`F`) |
| 1 | 1 | magic1 = `0x4C` (`L`) |
| 2 | 1 | version = `2` |
| 3 | 1 | message_type |
| 4 | 2 | payload_length uint16 LE，最大 128 |
| 6 | 2 | sequence uint16 LE |
| 8 | N | payload |
| 8+N | 2 | CRC16，对 offset `0..7+N` 计算 |

### 5.1 Host → TX

| Type | 名称 | Payload |
|---:|---|---|
| `0x01` | SET_ALL_DUTY | 固定 8 bytes，对应 Node 1..8 |
| `0x02` | SET_ONE_DUTY | `node_id:uint8, duty:uint8` |
| `0x03` | HEARTBEAT | 0 bytes |

上述三种有效帧均刷新 TX 上位机 watchdog。

### 5.2 TX → Host

| Type | 名称 | Payload |
|---:|---|---|
| `0x80` | STATUS_SNAPSHOT | 固定 54 bytes |
| `0x81` | ERROR_EVENT | UTF-8 短文本，最多 128 bytes；首版可不发送 |
| `0x82` | LINK_METRICS_SNAPSHOT | 固定 16 bytes；独立上下行 RSSI |

STATUS_SNAPSHOT：

| Offset | Size | 字段 |
|---:|---:|---|
| 0 | 1 | tx_flags，bit0=host failsafe |
| 1 | 1 | next_poll_id |
| 2 | 4 | uptime_seconds uint32 LE |
| 6 | 48 | 8 个 NodeRecord，每个 6 bytes |

NodeRecord：

| Offset in record | Size | 字段 |
|---:|---:|---|
| 0 | 2 | rpm uint16 LE |
| 2 | 1 | status |
| 3 | 1 | applied_duty |
| 4 | 1 | telemetry_age_100ms，饱和 255，未见过也为 255 |
| 5 | 1 | missed_polls，饱和 255 |

LINK_METRICS_SNAPSHOT 与 54-byte `STATUS_SNAPSHOT` 独立。其 payload
固定为 16 bytes，由 8 个 2-byte NodeLinkMetric 顺序组成：

| Offset in record | Size | 字段 |
|---:|---:|---|
| 0 | 1 | uplink_rssi_dbm，int8 二补码；TX 接收本节点 RPM Reply 时测得 |
| 1 | 1 | downlink_rssi_dbm，int8 二补码；RX 在 RPM Reply 中报告 |

`-127` 表示尚无有效样本。两项只在 TX 接受同时通过 RF CRC、应用 CRC、network ID、
node ID 和 sequence 校验的 RPM Reply 后更新。RSSI 是接收端瞬时指标，不是发射功率，
也不能单独证明链路可靠性。

当前 TX 每秒最多发送一个以 `RF ` 开头的 `ERROR_EVENT` 射频诊断摘要，并且只有
诊断 generation 改变时才发送。为保证所有 32-bit 饱和计数仍能装入 128 bytes，
字段采用紧凑、空格分隔格式：

```text
RF b1 B0 f4 e-6 X0 R-6 n1 s123 x0 t100 c0 i0 v0 o0
```

其中 `b/B` 为 radio begin 成功位/原始码，`f/e` 为最近一次保留故障及原始码，
`X/R` 为最近 TX/RX 驱动错误码，`n/s` 为该故障的节点和空口序号，
`x/t/c/i/v/o` 依次为 transmit failure、RX timeout、RX CRC、RX I/O、invalid reply
和 accepted reply 饱和计数。`f` 取值：`0` 无、`1` begin、`2` encode、
`3` transmit、`4` receive timeout、`5` receive CRC、`6` receive I/O、
`7` invalid reply。
成功回包只增加 `o`，不得清除最近故障。错误码文本显示会夹紧到有符号 16-bit，
controller 内仍保留完整 `int` 值。该文本属于 Host 诊断，不改变 FanLink 空口格式。

射频恢复计数改变时，TX 另发一个最多每秒一次、仍属于 `0x81 ERROR_EVENT` 的摘要：

```text
RFR attempts=3 successes=2 failures=1
```

`attempts/successes/failures` 是饱和 uint32 的重新初始化尝试/成功/失败次数，不含首次启动。
`successes` 仅表示 radio 初始化成功，不能替代合法 RPM 回包计数或节点在线判定。
原 `RF ` 格式及 `0x80`、`0x82` 的长度/字段完全不变；不理解该文本的 Host 可忽略。

默认恢复策略为：连续 3 次 TX 或 RX 硬错误，或 TX 全网 5 s 无合法 Reply /
RX 2 s 无新鲜合法 Control 时重新初始化射频；RX 在触碰射频前置全速。
失败重试间隔为 1/2/4/8 s（封顶），成功后恢复正常间隔。只有部分节点缺失时，
剩余节点的合法 Reply 会维持 TX 链路进展，因此 7 个空节点不会触发周期性复位。
TX 在原 5 ms 回包预算内继续处理 CRC/错帧，重试不重新授予完整窗口。

`0.1.5-dev` 在同一 `0x81` 类型内增加可选的每节点文本：

```text
RFN n1 p100 o95 m5 a1001 S136 x0 t5 c0 i0 v0
```

`n` 是节点 ID；`p` 是该节点 poll 已完成 TX 发射次数，`o` 是合法 Reply 次数；
`m` 为连续 missed polls，`a` 为即时遥测 age 毫秒，`S` 为与 STATUS 相同规则
合成 stale 位后的十进制原始状态。未见节点 age 为 UINT32_MAX，但默认只轮转
曾见过的节点（包括后来失联的），每秒最多一条 RFN。所有计数饱和 uint32。

`x/t/c/i/v` 是该节点 poll 窗口的 TX failure、未收到 Reply 且未被 I/O 中止、
CRC、I/O、invalid reply 计数。`t` 包括窗口用尽和 32 次防卡死上限，不等同于
每次芯片 Timeout；CRC 等窗口事件不能认证坏包的物理发送方。仅用同一节点、
同一 TX 运行期且未饱和的 `Δo/Δp` 计算已发送轮询的回复成功率；`p` 不能证明
RX 收到下行，亦不包括 TX 失败。不能使用 15 个空节点主导的全局 timeout 算 Node 01 丢包率。

最长 RFN 为 113 bytes，不理解该文本的 v2 Host 可忽略。STATUS、METRICS 优先于
RFR、RFN、RF 文本，每槽最多输出一帧；原 STATUS/METRICS 周期保持 250 ms。
C6 现有统一 1 秒日志节流可能抑制 RFN 文本，API 日志缺少 RFN 不代表 TX 未发送；
该文本不改变 C6 的在线、LED、PWM 或保护判定。

## 6. 兼容性规则

Host v2 接收端必须拒绝 version 1、SET_ALL 非 8-byte、STATUS 非 54-byte、
METRICS 非 16-byte 帧，不能靠补齐、截断或兼容适配器接受旧格式。其他消息的
payload 定义不变，但外层仍必须 version 2；CRC 范围包含版本字节。忽略未知
可选诊断文本不等于支持旧版 Host framing。节点范围仅 1..8。

`0.1.11-dev` 新增独立可选 `0x81` 文本（原 RF/RFN/RFT 不改）：

```text
RFC n2 e252 s0 c252 p252 h0 y0 u0 j950
RFE n2 k1 s12345 v15 f6 q0042 p10 y01 r-26 a2940 b950
```

RFC：`n` 为 poll 窗口节点；`e` 为 CrcError/IoError 事件总数；`s` 为无RxDone
的同步错误，`c` 为IRQ CRC，`p` 为PacketStatus CRC，`h` 为IRQ header错误，
`y` 为IRQ同步错误，`u` 为没有可信分类的事件。一个事件可以同时计入多项，
不得把这些项相加作为丢包数。计数均为饱和uint32。`j` 是最近成功发射的poll中
**第一次** receive调用所报告的SetRx命令完成offset（µs）；普通Timeout也采集，
后续错误重收/rearm不覆盖它。未知或不可信时间显示UINT32_MAX。

RFE保存最近CrcError/IoError，成功包和普通Timeout不覆盖：`k`为0无事件/1CRC类/
2IO类；`s`是当时poll序号；`v`有效位bit0=IRQ、bit1=PacketStatus、bit2=事件
观察时间、bit3=SetRx完成时间。`f`为原因掩码bit0=sync_without_rx_done、bit1=
irq_crc、bit2=packet_crc、bit3=header、bit4=irq_sync。`q/p/y`分别为十六进制
原始IRQ/PacketStatus错误字节/同步字节，须结合`v`判读；无效字节0不表示成功。
`r`为错误事件RSSI，仅有有效RxDone包状态时可用，未知为-127；不是合法节点遥测。
`a/b`分别为该错误的事件观察/SetRx命令完成offset，均相对当时poll接收窗口起点。
无效时间为UINT32_MAX。记录软件时间不等于实测DIO边沿或射频收发切换完成时间。

RFC/RFE各最多每秒一条，轮转曾见或出现错误的节点；全无记录时报告Node01。
STATUS/METRICS及旧诊断优先，每槽最多一帧；诊断帧须确认串口FIFO有整帧加1字节
余量，否则跳过且保持节流。不放宽坏包拒收、当前poll/sequence校验或离线阈值。
上述示例只说明字段，不是实机记录或错误来源认证；计数属于poll窗口，不证明发送方。

`0.1.6-dev` 的可选 `0x81` 文本新增（原 RF/RFN 不改）：

```text
RFT n1 d100 l2 h3 a4000 z5500 r4
```

`n` 节点；`d` 为返回 Ok 且带观察时间的次数（不等于合法身份回包）；`l` 为
时间拒收次数；`h` 为观察及时、全部校验接受、但完成处理已过截止的次数；
`a/z` 为最近 Ok 包的观察/完成 offset（µs，相对该 poll 的 TX 接收窗口起点）。
无观察元数据时 `a=UINT32_MAX`。`r` 保留最近拒收原因，成功不会清零：0 无、
1 envelope、2 network、3 node、4 sequence、5 观察迟到、6 时间戳不可信、
7 无元数据且返回迟到。计数均饱和 uint32；它们归属 poll 窗口，不认证发送者。
RFT 每秒最多一条，轮转曾见节点；冷启动全未见时仍报告 Node01，以免无回包时
完全没有诊断。STATUS/METRICS 优先，RFT 不刷新遥测新鲜度或降低离线条件。
RX 的 own-poll、malformed/network/stale/expired、reply attempt/result、调用年龄
计数只在 `RxRadioDiagnostics` RAM 接口，未上空口、未占 reserved、未启动 Serial，
当前 C6 无法取得，必须通过另行设计的安全调试接入或仪器补证据。

- 新增字段只能通过新 message type 或提升 version；
- 不能利用 reserved 字段改变 v1 语义；
- 应用层 CRC 即使底层射频启用 CRC 也必须保留；
- FHSS 可以新增同步机制，但不得悄然改变 v1 数据包字段。

## 7. 可选 RX 固件维护通道（0.1.8-dev）

0.1.9-dev新增Host0x11块中继与OTA操作7/8；正常Control/RPM及既有OTA操作1..6
保持原字节格式。每100字节一次块确认、固定30字节RF片段、失败重传/完整校验
细节见`docs/OTA_WIRE.md`及ADR-019。3KiB/s为目标/节流上限，未实测速率保证。

新增 RF type 0x30/0x31 和 Host type 0x10/0x90。字节规范为
`docs/OTA_WIRE.md`；不复用原包 reserved，不改变 Control/RPM 长度或语义。
维护期由 ADR-018 明确暂停正常轮询并保持全速。
