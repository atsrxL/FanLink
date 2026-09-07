# FanLink Protocol v1

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
- 节点 ID：`1..16`；
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
| 10 | 1 | poll_id | `1..16` |
| 11 | 1 | flags | 见下表 |
| 12 | 16 | duty[16] | Node 1..16，`0..255` |
| 28 | 2 | crc16 | 对 offset `0..27` 计算 |

控制 flags：

| Bit | 名称 | 含义 |
|---:|---|---|
| 0 | FORCE_FULL_SPEED | RX 忽略 duty，立即全速 |
| 1 | HOST_FAILSAFE | TX 因上位机超时而进入 failsafe |
| 2..7 | Reserved | 必须发送 0 |

RX 只有在长度、魔数、版本、类型、CRC、network_id 和 sequence 顺序均可接受时，才刷新链路超时。序号按 16 位模空间判断：向前差值 `1..32767` 接受；明显倒退值拒绝；`65535→0` 视为正常前进。相同 sequence 只在整个 Control 内容与上次接收的包完全一致时作为重试接受；相同 sequence 却更改 `poll_id`/flags/duty 的包必须拒绝。链路 watchdog 超时后清除旧 sequence 基线，使 TX 重启或长时间失联后的下一个有效包能重新建立基线。

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
| 10 | 1 | node_id | `1..16` |
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
T = 10 ms      下一个 Control slot
```

RX 在收到属于自己的 poll 后等待默认 `1,200 us` 再发回复。该数值只是起点，必须根据真实模块的 DIO/BUSY、包 airtime 与 RF switch 实测调优。

每个时隙只允许一个节点回传，不做竞争退避。

## 5. 上位机串口协议

串口默认：`115200 8N1`。

帧结构：

| Offset | Size | 字段 |
|---:|---:|---|
| 0 | 1 | magic0 = `0x46` (`F`) |
| 1 | 1 | magic1 = `0x4C` (`L`) |
| 2 | 1 | version = `1` |
| 3 | 1 | message_type |
| 4 | 2 | payload_length uint16 LE，最大 128 |
| 6 | 2 | sequence uint16 LE |
| 8 | N | payload |
| 8+N | 2 | CRC16，对 offset `0..7+N` 计算 |

### 5.1 Host → TX

| Type | 名称 | Payload |
|---:|---|---|
| `0x01` | SET_ALL_DUTY | 16 bytes，对应 Node 1..16 |
| `0x02` | SET_ONE_DUTY | `node_id:uint8, duty:uint8` |
| `0x03` | HEARTBEAT | 0 bytes |

上述三种有效帧均刷新 TX 上位机 watchdog。

### 5.2 TX → Host

| Type | 名称 | Payload |
|---:|---|---|
| `0x80` | STATUS_SNAPSHOT | 固定 102 bytes |
| `0x81` | ERROR_EVENT | UTF-8 短文本，最多 128 bytes；首版可不发送 |
| `0x82` | LINK_METRICS_SNAPSHOT | 固定 32 bytes；独立上下行 RSSI |

STATUS_SNAPSHOT：

| Offset | Size | 字段 |
|---:|---:|---|
| 0 | 1 | tx_flags，bit0=host failsafe |
| 1 | 1 | next_poll_id |
| 2 | 4 | uptime_seconds uint32 LE |
| 6 | 96 | 16 个 NodeRecord，每个 6 bytes |

NodeRecord：

| Offset in record | Size | 字段 |
|---:|---:|---|
| 0 | 2 | rpm uint16 LE |
| 2 | 1 | status |
| 3 | 1 | applied_duty |
| 4 | 1 | telemetry_age_100ms，饱和 255，未见过也为 255 |
| 5 | 1 | missed_polls，饱和 255 |

LINK_METRICS_SNAPSHOT 不改变既有 `STATUS_SNAPSHOT` 的 102-byte 布局。其 payload
固定为 32 bytes，由 16 个 2-byte NodeLinkMetric 顺序组成：

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

## 6. 兼容性规则

- 新增字段只能通过新 message type 或提升 version；
- 不能利用 reserved 字段改变 v1 语义；
- 应用层 CRC 即使底层射频启用 CRC 也必须保留；
- FHSS 可以新增同步机制，但不得悄然改变 v1 数据包字段。
