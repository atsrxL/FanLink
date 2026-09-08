# 2026-09-07 TX 经 SSH 刷写记录

## 最新：0.1.5-dev TX 优化版直接写入成功

用户再次单独授权 TX：`tx已接入 刷`。只通过原 RG35XX SSH 通道检查
主机/USB sysfs 与串口占用，不预读目标 Flash；本地独立复核新 TX 镜像及
构建输入 SHA 后，直接执行以下写入（不是下面历史 0.1.4 镜像）：

- 源镜像：`firmware/dist/20260907-link-timing/fanlink_tx_esp8285.bin`，295,824 bytes。
- 源 SHA-256：`e68763f2c9ab777947bfffcda74c2aa9b64c83f7e9d9984baf6138a266996a0c`。
- esptool 5.0.2 / 115200 / `--before no-reset --after no-reset --no-stub`，
  `write-flash --flash-mode keep --flash-freq keep --flash-size keep 0x0`。
- 本次仅擦除 `0x00000000..0x00048fff`，1.83 秒；不是整片擦除。
- ROM 补齐写入 295,936 bytes，33.1 秒，exit 0，最后 `Staying in bootloader.`。
- 日志：`build/tx-v015-ssh-flash.qK5yGx/direct-rom-write.log`。
- 没有备份、预读/读回 Flash、独立 chip-id/flash-id 或启动/复位命令；
  RX 保持上轮 0.1.4-dev，C6/RG35XX 固件未动。只确认写入，不确认启动或 RF 改善。

下一步：用户解除 TX BOOT 后正常上电；RX 优化版须另行授权刷入。
以下为旧批次历史记录，不应据其“当前/唯一”措辞选取旧镜像。

## 授权与边界

用户明确授权：已在 BOOT 模式的 FanLink TX 接到其 RG35XX 任务中的 USB 串口，
直接经 SSH 刷 TX。这次授权不包括 RX、C6 或 RG35XX 系统镜像。

## 当前状态：TX 直接 ROM 写入成功，运行未验收

用户随后重新使 TX 进入 BOOT，并明确要求不要再读取、直接写入。按此最新边界，
只执行现成 `0.1.4-dev` TX 镜像的 ROM 写入，没有再次备份、执行 `read-flash`、
独立执行 `chip-id` / `flash-id`，或发送 `run` / reset 命令。

- 使用 esptool 5.0.2，参数为
  `--chip esp8266 --baud 115200 --before no-reset --after no-reset --no-stub`
  `write-flash --flash-mode keep --flash-freq keep --flash-size keep 0x0`，
  镜像见下文；DOUT / 40 MHz / 1 MiB 逻辑布局保持原样。
- 本次擦除范围为 `0x00000000` 至 `0x00047fff`（含），耗时 1.81 秒；不是整片擦除。
- 源文件 294,224 bytes，ROM 传输按块补齐至 294,912 bytes。
  原始日志记录 `Wrote 294912 bytes at 0x00000000 in 30.7 seconds`，命令 exit 0，
  最后为 `Staying in bootloader.`。
- 只确认写入命令成功；**没有设备读回哈希、应用启动或 RF/长期稳定性验收证据**。
  RX、C6 和 RG35XX 固件均未改动。
- SSH 串口隧道已关闭，远端确认 `UART_RELEASED`。

当前下一步：用户解除 TX 的 BOOT 状态后正常上电，观察启动与链路。
不再自动读取 Flash、发送启动/复位命令，或执行下述历史脚本。

## 上一轮历史：完整备份成功，随后握手失败，未擦除或写入

- 当时实时核对 RG35XX 已运行 Debug BSP V3。
  新版 SSH host key 使用本任务独立文件固定，没有清空或覆盖全局 known_hosts。
- USB `1a86:55d3` / `cdc_acm` / `/dev/ttyACM0` 已确认空闲。
  使用 SSH stdio + tty inode 排他 flock + 115200 8N1 raw/no-echo/no-flow/no-hangup
  串口通道；没有新开 raw TCP/RFC2217 监听，也没有修改掌机系统配置。
- ESP ROM 识别为 ESP8285H16；SPI Flash ID `a1:4015`，检测容量 2 MiB。
  烧录镜像仍按已构建的 DOUT / 40 MHz / 1 MiB 逻辑布局原样写入，不修改镜像头。
- esptool 5.0.2 stub 成功读完 `[0x000000, 0x200000)`，2,097,152 bytes，耗时 191.3 秒。
  完整备份已复制到本地 Git 忽略的 `build/tx-flash-rg35xx.YsEdGp/tx-before-full-2mb.bin`，
  文件权限 0600，复制前后逐字节相同，SHA-256：
  `5458b7f6a7325bbac670e8af961c835ec02e4e59486eeaaf7f67ad0088493f51`。
- 备份命令使用 `--after no-reset`；退出后虚拟串口因 slave 关闭而出现 EIO。
  已在后续脚本中保留 slave fd 并使用 `--after no-reset-stub`，避免工具之间关闭通道
  或退出 stub。但重新握手时 TX 不再响应；USB/SSH、串口参数和占用检查仍正常。
  写入命令在连接阶段退出，**没有执行擦除或固件写入**。
- 当时已请用户保持 BOOT、仅给 TX 断电再上电，RG35XX 不必重启。
  后续用户重新进入 BOOT 后的成功写入见上方当前状态。

## 已写入的唯一产物

- 文件：`firmware/dist/20260907-radio-recovery/fanlink_tx_esp8285.bin`
- 版本：`0.1.4-dev`，已验证的 radio recovery 构建。
- 长度：294,224 bytes；写入地址 `0x0`，不整片擦除。
- SHA-256：`29835dd437a9db467b8e3e9de79b86ca74897b0dd3cb1fbb919bb5dd00976a6d`。

上述 SHA-256 是本地源镜像的哈希，不是设备写后读回哈希。上一轮提出的“保持 stub、
读回镜像、核对后启动”步骤已被用户最新的直接写入要求取代，不能作为当前操作入口。
写入、读回、应用启动和长期 RF/PWM 验收分别记录，不能互相代替。

## 本地操作证据

成功写入日志为 `build/tx-flash-rg35xx.YsEdGp/direct-rom-write.log`。
同目录保留上一轮完整备份、写入前失败日志、`flash-and-check.zsh` 和
`passive_check.py`。这些旧脚本包含先前计划的读回/启动操作，仅保留为历史，
**不得按当前授权自动重跑**。该目录不纳入 Git。
SSH 认证资料没有写入本仓库或刷写日志。
