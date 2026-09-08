# FanLink RX 射频 OTA（0.2.0-dev 候选）

## 5 KiB/s 上限批次模式

当前候选使用 `--mode fast --rate 5120`：电脑每次给TX100字节，TX本地发送十个
固定30字节RF片段，再取一次块确认。RX完整收齐才写入，丢片重发同一块，保留
CRC/MD5、全速锁定、10秒超时、显式Commit和旧10字节模式。静态缓冲100字节。
5120 B/s 是 Host 与 TX 双端镜像字节节流上限（含重传），不是实测吞吐；串口、
射频处理与 Flash 擦写仍可能令实际吞吐低于它。RF PHY 仍是 650kbps，分片间隔
仍为 1000µs；正常控制另改为 8 节点/125ms，见 `CAPABILITIES.md`。

第一次迁移：新TX配旧0.1.8 RX时，使用默认`--mode legacy`把0.1.9 RX镜像上传；
待RX升级启动确认后，才能用`--mode fast`。新TX的旧模式保持兼容；旧RX不会
执行快速片段，快速模式不会自动回退。需独占 TX Host 串口；本轮未改 C6，
但新轮询周期要求其独立适配 2500ms 遥测新鲜度。旧 RX2 诊断版首次迁移须有线。

```sh
uv run --with 'pyserial>=3.5,<4' python3 pc/tools/fanlink_ota.py \
  --port /dev/cu.YOUR_TX_UART --node 1 --firmware /absolute/path/fanlink_rx_node01.bin \
  --mode fast --rate 5120
```

本次仅开发与编译；未实测快速模式。以下为保留的初始部署说明；历史0.1.8
已另获授权完成一次同版OTA并恢复回传，见`docs/evidence/20260908-rf-ota.md`。

本功能由电脑经 TX 的 Host UART，把固件通过 SX1280 发给指定 RX。不使用 Wi-Fi，
不兼容 ExpressLRS OTA。正常 Control/RPM 包保持 v1 原布局；0.1.7 的测速修复保留。

## 首次部署与入口

旧固件没有 updater，第一次必须分别有线刷入本次 TX 和对应节点 RX 固件。
当前实际 TX 是 BETAFPV 2.4GHz Lite RX 硬件，沿用无外部 PA 的 Generic2400
配置及 +13dBm；环境名仍为 fanlink_tx_esp8285。RX 为 BETAFPV Nano；Node01
保留 PWM3/Tach1/gateActiveHigh0，Node02–16 为既有默认配置。

本轮只开发、测试和编译；没有访问 BOOT、刷写、重启或 OTA 实机。
实机写入请沿原任务已有的目标确认与 BOOT 流程操作。

上传入口是独占的 TX Host 串口：115200 8N1。现有 FanLinkC6 没有 OTA 转发，
不能把它的 ESPHome OTA URL 或 API 地址传给本工具；C6 控制程序/本地面板与
上传器不能同时占用 TX UART。切换实际连线和串口所有者后再运行。
工具不主动停止其他进程，不读 Flash，不控制 DTR/RTS 复位线。

在项目根目录检查候选镜像（不打开设备）：

```sh
python3 pc/tools/fanlink_ota.py --inspect --node 1 --firmware /absolute/path/fanlink_rx_node01.bin
```

完成第一次有线部署后，用户要执行后续无线升级时：

```sh
uv run --with 'pyserial>=3.5,<4' python3 pc/tools/fanlink_ota.py \
  --port /dev/cu.YOUR_TX_UART --node 1 --firmware /absolute/path/fanlink_rx_node01.bin
```

输入必须为本项目带 OTA 身份标记的原始 `.bin`，不能用 ZIP、gzip、其他节点或 TX
镜像。工具校验角色/节点/network/构建兼容性；RX 校验 fingerprint，防止误刷错误
引脚、极性、RF 参数或节点。更改这些构建参数会改变 fingerprint，需要有线迁移。
上传期间不会开启设备端串口日志或占用 RX 的 PWM/Tach 焊盘。

## 升级行为

1. TX 先广播完整全速 duty 至少 600ms，然后暂停普通控制/遥测；其余节点在失联
   watchdog 下保持全速。C6/面板可能显示失联，这是维护期间的预期状态。
2. 目标 RX 开始维护前锁定全速，忽略普通控制；按10字节数据块接收并确认。
3. 丢包重传相同请求，RX 不重复写入；错节点、会话、CRC、偏移或不兼容镜像拒绝。
4. 接收完整镜像并校验 MD5 后进入 Verified；只有 Commit 才写 eboot 启动命令。
5. Commit 成功 ACK 后保留 2.5s 重试窗口，再重启应用。上传器报告的完成仅代表
   收到提交 ACK，仍需要核实新版本启动、RF/RPM 和故障全速表现。

10s 无新进度会取消未提交暂存；取消/错误后重新升级使用新会话并从零开始。
TX 在最后请求10s后释放维护，清除串口半帧与残留，等待新的 Host 控制。
Ctrl-C/错误会尽力取消未提交传输；一旦发送 Commit，结果不确定时不声称撤销成功。
这个版本不支持断点续传或一键同时升级16个节点。

## 空间、校验与断电边界

运行时使用 `ESP.getFreeSketchSpace()` 和 Core `Update.begin` 双重空间检查，
拒绝装不下两份镜像的更新；512KiB 是协议上限，不代表所有运行镜像都有这些空间。
默认1MB/256KBFS linker把旧应用和暂存区放在剩余空间，未添加或格式化文件系统。

MD5校验的是传输/写入缓冲内容；它不是完整Flash读回或数字签名。network/session
和fingerprint也不是认证。本功能用于用户自行控制的设备，不能声明具备防恶意
固件或防射频攻击能力。

传输阶段不覆盖运行镜像；中断后可重新开始。Commit之后的 eboot 命令位于RTC RAM，
断电可能仍回旧镜像；最后启动复制阶段直接覆写应用，无A/B回滚，断电可能需要有线
BOOT恢复。不得把本功能描述为任意断电都安全。风扇复位时的全速还依赖现有硬件
默认电平，物理PWM/复位瞬态仍须台架验证。

协议字节定义见 `OTA_WIRE.md`，架构理由见 ADR-018。软件回归包含协议破坏、失序、
ACK丢失、重复Commit、完整镜像取消、空间/摘要错误、维护控制隔离与超时。
实际RF传输耗时、最远距离、sector写入延迟、重启恢复和持续散热尚待实机验证。
