# 2026-09-07 RX Node01 经 SSH 直接刷写记录

## 授权与当前状态

TX `0.1.5-dev` 写入完成后，用户另行明确授权已接入的 RX 刷写。仅使用原
RG35XX SSH/USB UART 通道，沿用用户“直接写、不预读”的边界；不是刷写 RX16、
C6 或 RG35XX 系统。当前 TX 与 RX Node01 均有同批 `0.1.5-dev` 的成功写入记录。

用户随后报告实际控制有效，但没有回传。该报告与写入成功分别记录，不能据此
宣称双向 RF、PWM/Tach 或长期稳定性验收通过，也尚未确定缺少回传的原因。
目前仅调查原因，不再次刷写、修代码、重启或发送风扇控制命令。

## 目标镜像与写入结果

- 源镜像：`firmware/dist/20260907-link-timing/fanlink_rx_node01.bin`，290,128 bytes。
- 源 SHA-256：`5414c7e775e6558c1603409f0ad8c83d15c66967a55dff62a8995ac67affd5fb`。
- 编译目标：`fanlink_rx_node01`，Node ID 1；PWM GPIO3 / Tach GPIO1 /
  `FANLINK_GATE_ACTIVE_HIGH=0`。不是同长度但不同哈希的 Node16 镜像。
- 本地镜像、独立 `.sha256`、`BUILD_INFO.json`、本批 `SHA256SUMS` 及当前 `.pio`
  产物相互一致；构建输入 manifest 的 52 个固件文件已核对与当前源码一致。
  以上是本地文件校验，**不是设备写后读回校验**。
- esptool 5.0.2 / 115200，`--before no-reset --after no-reset --no-stub`，
  `write-flash --flash-mode keep --flash-freq keep --flash-size keep 0x0`。
- 仅擦除 `0x00000000..0x00046fff`，1.75 秒；未整片擦除。
- 源镜像按 ROM 块边界补齐写入 290,816 bytes，31.6 秒，命令 exit 0。
  原始日志最后为 `Staying in bootloader.`；这是命令结束时的状态，
  不代表用户随后使用时仍在 BOOT。

本次未预读、备份或读回 Flash，未单独执行 chip-id/flash-id，也未发送 run/reset。
任务自己的本地串口 relay 已关闭，主任务经远端 fuser 确认 `UART_RELEASED`；
未修改 C6/RG35XX 固件。没有设备读回哈希或仪器验收证据。

## 本地证据位置与后续边界

- RX01 写入日志：`build/rx01-v015-ssh-flash.ESx5Yo/direct-rom-write.log`。
  其中记录擦除范围/1.75 秒、写入 290,816 bytes/31.6 秒及 bootloader 结束状态；
  exit 0 来自写入命令的执行结果。
- 同批 TX 写入日志：`build/tx-v015-ssh-flash.qK5yGx/direct-rom-write.log`；
  TX 源镜像 295,824 bytes，源 SHA-256
  `e68763f2c9ab777947bfffcda74c2aa9b64c83f7e9d9984baf6138a266996a0c`，
  ROM 补齐写入 295,936 bytes/33.1 秒/exit 0。
- 源码/测试/构建证据：`docs/evidence/20260907-link-timing.md`；镜像、构建日志
  与 SHA 文件保留于 `firmware/dist/20260907-link-timing/`，不纳入 Git。

此前 `20260907-tx-ssh-flash.md` 中 TX 单独刷写时“RX 仍为 0.1.4-dev”的句子是
该次操作的历史快照，已由本记录中的后续 RX01 写入取代。下一步仅核对当前
TX/RX/C6 回传链路证据，不能把本次落档当作再次刷写或修改设备的授权。
