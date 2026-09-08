# TX 本地在线升级与启动维护（0.1.10-dev）

本功能走 TX 的既有 115200 8N1 Host UART，不走 SX1280，不开启 Wi-Fi。
生产 C6 的转发/按钮接入尚未实现；目前由独占 TX 串口的 Python 工具调用。
新固件首次部署仍需要有线刷入；旧 0.1.9 TX 不认识这些命令。

## 在线升级并重启

```sh
uv run --with pyserial==3.5 python3 pc/tools/fanlink_ota.py \
  --target tx --node 1 --port /dev/cu.YOUR_TX_UART \
  --firmware /absolute/path/fanlink_tx_esp8285.bin --mode fast
```

只接受 TX 角色、正确 network/profile 的原始镜像。数据暂存于空闲 sketch 空间，
完整长度、身份和 MD5 校验后才能 Commit。Commit 后保留 ACK 重试窗口再重启。
Commit ACK 只说明提交被接受，不是新应用运行证明，升级后仍需核对状态和回传。
断传不覆盖运行应用；最后 eboot 复制不是 A/B，复制时掉电可能需要实体 BOOT 恢复。

## 手动命令：重启或进入 ROM 下载模式

```sh
uv run --with pyserial==3.5 python3 pc/tools/fanlink_tx_control.py reboot \
  --port /dev/cu.YOUR_TX_UART --network 0x4B4E4C46
uv run --with pyserial==3.5 python3 pc/tools/fanlink_tx_control.py boot \
  --port /dev/cu.YOUR_TX_UART --network 0x4B4E4C46
```

两项操作均是显式维护请求，不根据普通
串口字符、esptool 同步片段或控制超时自动触发。ACK 后才调用 Core 的 restart 或
rebootIntoUartDownloadMode；进入 BOOT 不会自动擦除、上传任何固件。

软件进 BOOT 需要应用健康运行。应用损坏/卡死时仍保留实体 BOOT 恢复方式。
软件进入的下载模式不应再被硬复位；随后 esptool 使用 `--before no-reset`，
沿用 `--flash-mode keep --flash-freq keep --flash-size keep`。Core 官方说明这种
进入方式并非硬复位，esptool 晶振报告可能不准确，不能据此改写 Flash 参数。
命令工具只发送一次，ACK 超时必须先核实状态，不盲目重复。命令接受后工具释放
UART；不会自动调用 esptool。下载期间无应用级自动退出定时器。未开始写入时可重新上电退出；已开始写入须
等待明确完成或失败后按恢复流程处理。

## 维护与安全

- TX 本地升级、RX 空中 OTA、重启/BOOT 操作互斥。
- 维护前至少 600 ms 广播全速，之后正常控制不可解除保护。
- 传输取消/超时恢复全速默认值并清除残留 Host 半帧；只接受新的控制请求。
- 命令有严格长度、network、非零 session、固定确认字段和 CRC 校验，防误触发；
  这些不是认证，UART 必须由可信且独占的调用者持有。
- 不自动停止 C6/面板进程，不操纵 DTR/RTS，不预读或读回设备 Flash。
- 软件测试/编译不能替代实际升级、BOOT 同步、重启恢复和风扇瞬态测量。

## Host wire 扩展

ADR-025 后所有外层均为 Host v2 的 `FL` 帧：8B header、最多 128B payload、
序号与 CRC16 保留；下列维护 payload 字节不变。新 v2 工具不能与旧 v1 TX
交互或请求 BOOT/OTA；旧 TX 仅能使用归档 v1 工具或另行授权有线 ROM 迁移。

| Host type | 内容 |
| --- | --- |
| 0x12 | TX 本地 OTA 请求：原有 30B OTA 编码，操作 1..6 |
| 0x13 | TX 本地 OTA 快速块：与 0x11 相同的 30..120B payload |
| 0x91 | TX 本地 OTA 回复：30B OTA ACK，回显 Host sequence |
| 0x14 | TX 生命周期请求：固定 16B，见下文 |
| 0x92 | 生命周期回复：请求 16B 原样回显，末尾 1B status |

0x14 payload：offset0 network u32LE；offset4 session u32LE 非零；offset8
action u8（1 重启，2 ROM UART BOOT）；offset9..15 ASCII `FLTXCTL`。
0x92 status：0 接受，1 忙，2 不支持。长度、guard、network、session 或 CRC 错
不执行操作。相同 session/action 的重试不得重置延时或重复执行操作。
接受回复在全速预告后发送，成功写出回复后保留至少 250 ms，再 flush UART 并执行。

OTA 内层节点是 TX 构建身份节点（当前 1），不是空口 RX 寻址；只在上述 Host
type 下表示 TX，不给普通 RF node0 或 reserved 字段添加新语义。
TX 本地消息不会转发到射频；旧 RX OTA 的 0x10/0x11/0x90 和正常控制完全保留。
