# 0.2.0-dev 能力确认（2026-09-08，未刷写）

当前 ADR-025 契约：原生 8 槽 Host v2，所有外层串口帧 version2，SET_ALL8 /
STATUS54 / METRICS16 bytes；无 wire16、补齐、截断或双栈。8B header、2B CRC、
MAX_PAYLOAD128 保留给 OTA/诊断。RF v1 固定30B：8 duty + 8 reserved255。
以下较早的能力构建证据不是本轮 v2 软件门禁或硬件验收。

本轮仅开发、软件验证和本地编译；没有打开串口、重启/刷写设备或部署 C6。
历史现场射频丢包仍未归因，不以本次功能补齐声称已经解决物理稳定性。

| 用户要求 | 实现 |
|---|---|
| 最大 8 RX，125 ms 轮询 | 新增活动上限与共享非阻塞节拍；正常每秒遍历 1..8 |
| TX 运行 → 重启/刷机模式 | 已有 Host 0x14：reboot / ESP8266 ROM BOOT，保留并回归 |
| TX 在线 OTA | 已有 Host 0x12/0x13 本地 UART OTA；不是 Wi-Fi/Web OTA |
| TX 下发 RX OTA，最大 5k | 已有 RF OTA；新 Host 与 TX 双端上限 5120 镜像 B/s，含重传 |
| TX 功率 | 新增本地设置，Lite 芯片 -18..13 dBm，保存后重启生效 |
| RX 接受 OTA | 已有普通 RX OTA；新版默认 UART RX 也补齐 RF OTA |
| RX 引脚模式 | 新增 uart/pwm/pwmswap，保存后 2.5 s 重启生效 |
| RX 回传功率 | 定向持久化设置；当前 Lite 芯片范围 -18..13 dBm，Nano 档位仅为协议支持 |

5120 B/s 是镜像字节发送节流上限，不是实际完成速度承诺；不包括 RF 包头、ACK、
重试等待、擦写、校验和重启。保持原 PHY、固定频率及 1000 µs 分片间隔。
OTA/设置独占维护期间暂停正常轮询、全速保护；“一秒一轮”指正常控制运行。

## 引脚和持久化

| 模式 | GPIO1 | GPIO3 |
|---|---|---|
| uart | UART TX | UART RX |
| pwm | 25 kHz PWM | Tach |
| pwmswap | Tach | 25 kHz PWM |

UART 不控制风扇，但保留 RF、OTA、设置与诊断日志。PWM 板级极性不改变。
RX01 出厂默认 pwmswap；RX2 默认 UART 构建保持用户原 TX/RX 定义。
设置存于专用 EEPROM sector，校验 network/node/profile/schema/CRC，写入读回检查。
损坏配置不猜引脚角色：两焊盘输入、最低档 RF、安全态可远程修复。
单 sector 非断电原子：擦除瞬间断电可能恢复出厂默认，设置时不要拔电。

ACK 表示保存成功/已安排应用，不是实测成功；重复同一事务不重复写 Flash，
不延长重启宽限期。OTA 与设置互斥，修改前置全速，宽限期保持全速。
具体消息、状态、Busy/timeout 行为见 [CONFIG_WIRE](CONFIG_WIRE.md)。

## 调试命令

停止占用同一串口的面板/C6 Host，再通过 `pc/tools/fanlink_config.py --help` 查看
query、tx-power、rx-power、rx-mode。该工具不触发 BOOT、不操作 DTR/RTS。
TX 生命周期和在线升级操作继续使用 [TX_UPDATE](TX_UPDATE.md)；
RX OTA 使用 [OTA](OTA.md)。所有实际操作仍需明确目标和当次授权。

## 迁移和未验证项

- 保留 RF v1 30B 及 RF OTA/设置字节；Host 改为 v2 原生 8 槽，没有节点 9..16。
- TX/C6/PC 要协调升级所有 Host 外层帧，包含 OTA/设置/重启/BOOT。新工具不能
  向旧 v1 TX 请求 BOOT/OTA；旧 TX 只能用归档 v1 工具或另行授权有线 ROM 迁移。
  本次 Host 迁移不要求立即重刷 RX。
- C6 生产任务保留 2500 ms 遥测新鲜度、5 miss / 6 s 在线容忍，传输失信立即离线，
  不以陈旧回传重建在线；本轮未部署 C6，软件契约不等于现场已升级。
- 新功能必须刷入对应 TX/RX 候选才具备。旧 RX2 专用诊断固件没有 OTA，首次迁移
  必须有线；之后相同默认 UART target 可空中升级。运行时改模式不改变镜像指纹，
  不应借模式设置跨刷不同板型/node/default-target 的固件。
- TX/RX OTA 仍是暂存+校验+eboot copy，不是双镜像 A/B 回滚。最终拷贝掉电可能
  需要 ROM 有线恢复。没有加密签名，network/session/CRC 不是安全认证。
- 尚未实机验收：8 RX 同时在线、实际射频功率、三模式接线/波形、断电保存、
  新版 TX 软件 BOOT、本地及空中 OTA、实测吞吐、长期稳定性。

软件与编译证据见 [本轮验证](evidence/20260908-eight-node-capabilities.md)。
