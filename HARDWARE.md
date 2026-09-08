> 2026-09-08 用户最新确认 TX、RX 都是 BETAFPV 2.4GHz Lite RX。
> 两端采用 Generic2400：无 RXEN/TXEN、LDO、默认最高芯片 +13 dBm。
> 下方 JHEMCU/Nano 对照仅为历史画像，不再是当前 RX 构建目标。

# FanLink 硬件与接线

ADR-023 新候选支持 RX 持久化引脚角色：uart=GPIO1 TX/3 RX，pwm=GPIO1 PWM/3 Tach，
pwmswap=GPIO3 PWM/1 Tach。PWM 的板级极性不随角色改变，改变后须重新匹配外部接线。
RX01 缺省 pwmswap（原极性保留），RX2 UART 环境缺省 uart；新版 UART 也支持 RF OTA。
旧 RX2 专用诊断版本没有 OTA，首次迁移仍需有线刷入。TX Lite 默认芯片 +13 dBm，
RX Lite 同为 +13 dBm；可设范围见 `docs/CONFIG_WIRE.md`。
官方 `betafpv.rx_2400.lite` 指向 `Generic 2400.json`，功率表仅 `[13]`，
LED16，未定义外置 PA 开关或 DC-DC。来源与迁移边界见
`docs/evidence/20260908-both-lite-max13.md`。旧 Nano 设置不自动跨板型继承。

RX02 UART诊断特例（ADR-021）：独立`fanlink_rx_node02_uart_diag`使用GPIO1=TX、
GPIO3=RX，禁用实际PWM/Tach；必须断开风扇信号线路。正常RX02配置不变。
详见`docs/RX2_UART_DIAGNOSTIC.md`。

## 1. 历史目标画像（已由上方双 Lite 更正替代）

用户已指定：

- TX：JHEMCU 2.4GHz Lite，实际是把该 RX 硬件用于 FanLink TX；
- RX：BETAFPV 2.4GHz Nano RX。

本轮依据 ExpressLRS `targets` commit `c4bd7b823594c233e673828ab493a2f8319a756a`
中的 `jhemcu.rx_2400.lite`、`betafpv.rx_2400.nano` 和对应 layout 建立独立构建画像：

| 功能 | JHEMCU 2.4GHz Lite（TX 角色） | BETAFPV 2.4GHz Nano（RX 角色） |
|---|---:|---:|
| MCU / Radio | ESP8285 / SX1280 | ESP8285 / SX1280 |
| SX1280 NSS | 15 | 15 |
| SX1280 DIO1 | 4 | 4 |
| SX1280 RST | 2 | 2 |
| SX1280 BUSY | 5 | 5 |
| SPI SCK / MISO / MOSI | 14 / 12 / 13 | 14 / 12 / 13 |
| RF RXEN / TXEN | 未定义 | 9 / 10 |
| SX1280 regulator | LDO（上游未设置 `radio_dcdc`） | DC-DC（上游 `radio_dcdc=true`） |
| SX1280 构建功率 | +13 dBm（芯片安全上限，约 20 mW） | -6 dBm（板载 PA 的上游 25 mW 档） |
| UART RX / TX | 3 / 1 | 原 target 为 3 / 1，本项目改作 Tach / PWM |
| 风扇 PWM / Tach | 不使用 | Node 01 特例为 3 / 1；Node 02–16 默认 1 / 3 |
| 状态 LED | 未使用 | 上游定义 GPIO16，按上游驱动语义有效高；实板极性待确认 |

这证明的是上游 target 数据与构建宏一致，不是 PCB 实测。两块板的 PCB revision、
GPIO1/3 板载电路、TCXO、启动瞬态和 RF 前端真值表仍未由照片、原理图或仪器确认。
尤其 JHEMCU 官方条目是 RX 而不是 TX；FanLink TX 角色会使用其 UART 焊盘作为上位机接口。

通用源码的未知板型回退值仍为 SX1280 `-18 dBm`。JHEMCU 和 BETAFPV
专用 PlatformIO 环境分别覆盖为 JHEMCU 的 `+13 dBm` 和 BETAFPV PA 输入
`-6 dBm`。前者已经是 SX1280 支持的最大设定，芯片端理想值约 20 mW，无法仅靠
固件设成真正 25 mW；后者按上游 `power_values=[-10,-6,-3,1]` 的索引对应
25 mW 档。BETAFPV 板载 PA 的实际天线口输出仍必须按板型测量，不能把 SX1280
芯片输入值当作天线口功率。

当前实物 Node 01 因走线与初始假设相反，使用专用编译覆盖：PWM=GPIO3、
Tach=GPIO1、`FANLINK_GATE_ACTIVE_HIGH=0`。这只改变 Node 01；Node 02–16 仍使用
PWM=GPIO1、Tach=GPIO3 和默认外部下拉晶体管极性。空口 duty 语义没有改变：
`0` 仍是 0%，`255` 仍是释放/全速。该覆盖来自本机风扇方向观察，仍需示波器逐点
验证 0/1/64/128/192/254/255 及复位默认状态。

BETAFPV Nano 状态灯来自同一上游 target 的 `led: 16`，且 target 未设置
`led_red_invert`，因此本项目按有效高配置；实板极性仍须观察确认。未连接时采用
两次短闪后停 500 ms。最近 500 ms 内收到通过完整 envelope、poll ID、应用 CRC、
network ID 和 sequence 接受规则的 FanLink Control 包时常亮。携带
`HOST_FAILSAFE/FORCE_FULL_SPEED` 的合法包仍会令灯常亮，但风扇必须保持全速；
状态灯只证明 MCU 的无线下行判定，不能替代回程包、频谱或功率测量。

## 2. 标准四线 PC 风扇

常见四针定义：

| 风扇 Pin | 功能 |
|---:|---|
| 1 | GND |
| 2 | +12 V |
| 3 | Tach / Sense 输出 |
| 4 | PWM Control 输入 |

本项目：

- RX PWM GPIO 经外部开漏/开集电极级接风扇 Pin 4；
- 风扇 Pin 3 经确认过的 3.3 V 上拉接 RX Tach GPIO；
- 风扇电源和接收机必须共地；
- 不允许把 MCU GPIO 直接推挽接到可能被上拉至 5 V 的风扇 PWM 控制脚；
- 不允许在未测量电压时把 Tach 直接接 MCU。

## 3. 建议控制级

逻辑模型：

```text
ESP GPIO ── gate/base resistor ── N-MOS/NPN
                                  │
Fan Pin 4 PWM ─────────────────── drain/collector
                                  │
GND ───────────────────────────── source/emitter
```

设计要求：

- MCU 低电平或高阻时晶体管关闭；
- 晶体管关闭时风扇 PWM 脚被风扇内部上拉，代表全速；
- 固件因此把逻辑 duty 反相后写入 GPIO 波形；
- 建议给 gate/base 加明确下拉；
- 选择在 25 kHz 下有足够开关速度的器件。

`Esp8266FanOutput` 默认 `gate_active_high=true`：

```text
逻辑 duty=255（全速） -> GPIO duty=0 -> 晶体管关断
逻辑 duty=0（请求停转） -> GPIO duty=255 -> 晶体管导通
```

## 4. Tach 输入

推荐：

```text
3.3 V ── 4.7k~10k ──┬── ESP Tach GPIO
                     │
Fan Pin 3 Tach ──────┘
GND 共地
```

必须先验证风扇或其他设备没有把该线拉到 5 V/12 V。必要时使用分压、缓冲器、光耦或电平转换。

默认按每转 2 个脉冲计算。代码用下降沿周期计算，不在 ISR 中做除法或无线发送。

## 5. GPIO1 / GPIO3 的特殊风险

GPIO1/3 原本是 UART TX/RX：

- RX 固件不启用 Serial；
- 启动 ROM 仍可能在 GPIO1 输出启动信息；
- 外部晶体管级必须让这种瞬态不造成危险停转；
- 烧写时 USB-UART 与风扇接口可能争用，需要跳线、串联电阻或可断开连接；
- 不同板型可能在这两个 GPIO 上已有电阻、反相器或外围器件。

## 6. 射频前端核对表

为每一个实际 PCB revision 填写：

```text
板名/PCB版本：
原 ELRS target：
MCU：
Radio：
NSS：
DIO1：
RST：
BUSY：
SCK/MISO/MOSI：
RXEN：
TXEN：
PA enable：
LNA enable：
天线开关真值表：
最大安全 TX power：
供电电压：
可用 PWM GPIO：
可用 Tach GPIO：
```

板上存在 PA/LNA 时，不能只依赖 SX1280 内部功率值；必须正确控制射频前端，否则可能完全收不到、无法发射或损坏器件。

## 7. 首次通电顺序

1. 不接风扇，只验证 3.3 V 电源和 MCU 启动。
2. 不接 PA 高功率，先用最小射频功率和近距离衰减条件验证收发。
3. 用逻辑分析仪验证 poll/sequence，再接 PWM 晶体管级。
4. 用示波器和假负载验证 25 kHz。
5. 测量风扇 Pin 3/4 静态电压。
6. 接一只风扇，验证拔 TX、拔串口、复位 RX 均回全速。
7. 再加入第二节点，最后扩到 16 节点。
