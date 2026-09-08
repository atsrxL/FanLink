# 2026-09-08 TX 本地升级/重启/软件 BOOT — 0.1.10-dev

> 首轮 `20260908-tx-maintenance` 虽编译通过，但 ELF 审查发现未对齐 PROGMEM
> 描述符被普通 memcpy 读取的启动风险，不得刷写。修复为 memcpy_P 后将生成
> 独立 reviewed 批次，已再次通过 make verify、三目标 clean build 与 SHA 校验。
> 以下产物表为修复后的 reviewed 批次；首轮保留为失败审查证据，不覆盖或删除。

## 范围与结果

用户授权补齐 TX 在线升级后重启与自动进入 BOOT 升级。完成 Host-only 维护
状态机、Python 工具和测试；真实依赖 TX/RX01/RX16 clean 编译 3/3 PASS。
本轮未打开设备串口、SSH、刷写、重启或执行 BOOT；现有设备仍是 0.1.9。
C6 未改动，需要单独接入维护消息才可从其生产界面操作。

## 实现

- `ota_tx_local.hpp/.cpp`：独占本地升级，复用 Receiver 与平台 Update storage；
  独立 Host 0x12/0x13/0x91，数据不上射频；设备端流式检查唯一 TX descriptor。
- `tx_lifecycle.hpp` / `main_tx.cpp`：显式 restart/ROM boot，严格 guard、network、
  session、长度、action/CRC。600ms 全速预告；ACK 成功写入后等待250ms并flush；
  ACK 写入失败不触发操作，10s取消。普通控制、RX OTA、本地升级互斥。
- `pc/tools/fanlink_ota.py`：明确 `--target tx --node 1`；默认 RX 行为保留。
- `pc/tools/fanlink_tx_control.py`：`reboot|boot` 单次发送；未知结果不盲目重试，
  不调用刷写器、不触发 DTR/RTS、不把 ACK 当作启动验证。
- 使用固定 Core3.1.2 `ESP.rebootIntoUartDownloadMode()`；不自写 ROM 寄存器流程。

## 验证

- 改动前 native708 PASS；完整基线的功能测试 PASS，最后 manifest 检查仅因本轮
  已写 ADR 的已知文档变化未通过。日志 `build/20260908-tx-maintenance-baseline.log`。
- 最终 `make verify` exit0；日志 `build/20260908-tx-maintenance-reviewed-verify.log`。
- 新 TX 本地测试普通运行与 ASan/UBSan PASS；涵盖快速块/尾块、重复 ACK、
  无维护期 RF 调用、Commit grace、超时、新会话、Abort ACK 丢失、跨块身份、
  错/缺/重复/截断描述符、Busy、冲突操作、ACK写失败和计时回绕。
- Python：旧 OTA28 + 新 TX12 + 面板21 =61 PASS。native708、OTA8102、
  实际 storage242、原时序/测速/故障恢复及语法 smoke 均保持通过。
- CMake/CTest：4/4 PASS（`build/cmake-tx-maintenance`）。
- PlatformIO6.1.19 / espressif8266@4.2.1 / Arduino3.1.2 / RadioLib7.7.1：
  三目标 clean build PASS；日志 `build/20260908-tx-maintenance-reviewed-build.log`。
- 产物 SHA256SUMS 全部回读匹配，TX/RX01 镜像 inspect PASS，TX unique marker PASS。
- reviewed ELF 二次审查：全局构造在0x40203355调用memcpy_P(0x402106d8)，
  从PROGMEM0x402412f5复制28B到RAM；随后普通memcpy仅读RAM。TX bin唯一标记
  位于offset266997，完整身份与预期一致，首轮未对齐Flash复制风险已消除。

```sh
PLATFORMIO_CORE_DIR=/tmp/fanlink-pio-core-v013-preflight \
  uv run --offline --with platformio==6.1.19 python3 firmware/tools/build_firmware.py \
  --tx --rx 1 --rx 16 --run-id 20260908-tx-maintenance-reviewed
```

## 产物

目录 `firmware/dist/20260908-tx-maintenance-reviewed/`，保留 BUILD_INFO、原始日志与 build-input manifest。

| 镜像 | 字节 | SHA256 |
| --- | ---: | --- |
| TX | 308784 | 04c6722b09ce3f857f56cc65d05566c301eb21d421bac519c5318573bd34e014 |
| RX01 | 300240 | 63bde8b32c30575ddc3810e914367935f3d653eed55ac2a2d65bc63c3a254074 |
| RX16 | 300240 | b3f111c46907f17aedc2ff9ccc095658551bd55819b13607e88b87613c75bcea |

TX静态RAM32400/81920；链接Flash304627/761840，实际bin308784B。
TX fingerprint仍E532A96B；RX01仍9C1AF63C。两份TX镜像4KiB对齐后总计622592B，
低于0xBB000(765952B) sketch区；最终仍由运行时空间与 Update.begin 双重检查。
RX重编仅为回归，不需要为这次TX功能重新刷RX。

## 未验证与下一步

首次需要另行授权有线部署新TX，之后依次验收应用命令重启、软件BOOT+esptool
no-reset同步、同版TX在线升级及回传恢复。不得自动拿当前旧应用尝试新命令。
没有ROM同步、应用重启、升级后身份读回、断电/长期/风扇瞬态的实测证据。
OTA最终eboot复制无A/B保证；应用损坏时软件BOOT不可用，仍须实体BOOT恢复。
协议与完整操作命令见 `docs/TX_UPDATE.md`、ADR-020。未commit/push。
