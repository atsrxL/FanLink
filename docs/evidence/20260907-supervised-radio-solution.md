# 2026-09-07 联合无线回传修复方案 — 0.1.6-dev 未刷写候选

## 最终唯一候选批次（取代下方初候选）

使用 `firmware/dist/20260907-supervised-radio-final/`，不是旧 `20260907-supervised-radio-solution/`。
旧目录完整保留用于比较/回退；设备仍为已写入的 0.1.5-dev，本次未刷写。
本次仅将 TX 遥测处理年龄向旧取整：`ceil(processingAgeUs/1000)`，避免
观察4999 µs、返回6501 µs被记成5 ms而非4 ms。普通、micros回绕、millis回绕
回归在旧取整下 RED 3/708，修正后 native 与 ASan/UBSan 均708 PASS。
联合1804、RX509、PWM2657、PC21、虚拟12小时及 `make verify` 复验通过；
最终门禁日志 `build/supervised-radio-final-make-verify.log`，退出码0。
真实 clean TX/RX01/RX16 3/3；构建时间2026-09-07T13:41:50.062421Z。
最终7/7清单与构建输入全部firmware文件均本地哈希匹配；无设备读回。
`SOURCE_MANIFEST.build-input.sha256` 与 BUILD_INFO 记录相同：
`7172ebcb8eeec62e3a168013a3ba7dadb60e3771749627635c8a68f71efffe35`。

| 最终文件 | bytes | SHA-256 |
|---|---:|---|
| fanlink_tx_esp8285.bin | 296848 | cbb8e9443847cee64aa951859e0695d68bb68469e102fa073ae67cab9a85ca44 |
| fanlink_rx_node01.bin | 290848 | 7536699c73b0d7cdbda5e73ca590a280868c9c5b3f29cd4b8750e06c627c5489 |
| fanlink_rx_node16.bin | 290848 | f5669cb9a02222b783b18bcee2ae7254fe97c1d65ad29fdcd88e3b1095f092b4 |

以下保留初候选的软件证据和通用验收/回退步骤；其中初候选路径、SHA、693项
测试及构建时间是历史，最终操作必须使用上表批次。

## 结论与权限边界

已修正并用回归证明三个软件缺陷：TX 将接收处理结束误作到包时刻、RX 在处理后
叠加完整 guard、FLRC sync-only 错误未显式唤醒 DIO 的接收状态缺口。
**没有证明用户现场唯一根因就是 PWM NMI，也没有宣称电气干扰已排除。**
候选真实依赖 clean 编译 3/3 完成；当前 TX/RX01 写入记录仍为同批 0.1.5-dev。
本轮不刷写、不重启、不发送 duty、不 OTA C6，不读取 Flash 或占用 BOOT 串口。

## 现场事实（与模型区分）

- 用户最新复现：RX 重插恢复；PWM=100% 有回传但偶断；中间 PWM 实际调速仍有效，
  C6 显示断开且无回传。这支持存在与 PWM 状态相关的回程问题，但不唯一指向软件。
- 20:58:28.090–20:58:54.359 CST：Node01 p 1813→1975、o 121→121、
  t 1692→1854；UART/CRC/I/O 无窗口增量。不能把累计计数当作窗口错误。
- 后续 21:14:16.082–21:14:37.307 已是另一计数 epoch：全局 t +2095、o=0。
  C6 UART 仍正常；期间计数回落意味着状态重置，不能与上一 epoch 直接相减。
- ESP8266 中间 duty 使用 25 kHz 软件 PWM；端点为静态输出。重复写入缓存不能
  消除持续中断负担，但真实 PWM NMI 占时、供电波动和射频干扰均未量测。

## 已改代码与最小设计

1. `firmware/include/fanlink/interfaces.hpp` 与
   `firmware/lib/fanlink_platform_esp8266/src/fanlink/platform/esp8266/radiolib_sx1280_port.*`：
   返回首次 DIO 高电平的软件观察时间及有效标记，另保留函数完成时刻；微秒接口
   保留不足 1 ms 的剩余预算。它不是硬件沿时间，CPU 晚观察仍会晚。设置 sync IRQ
   映射成功后才 SetRx，sync-only 出口消费事件并重开单包接收；普通软件超时保持接收。
2. `firmware/src/app/tx_controller.cpp`：观察 offset 必须在原 5 ms 内，未来/前槽
   时间戳仍拒绝；合法包虽处理结束超过窗口，可继续完整身份/CRC 校验后接受。
   无元数据保持保守返回时间判断。过期、错 network/node/sequence 不能更新遥测。
3. `firmware/src/app/rx_controller.cpp` 与 `firmware/include/fanlink/rx_controller.hpp`：
   guard 仅等待 `max(0,1200-elapsed)`；到调用发送已 `>=5000 µs` 则跳过回复，
   不因此撤销新鲜控制。超过 500 ms 的控制不能解除保护，处理耗时不延长 watchdog。
   `main_rx.cpp` 将最大年龄与 `build::kReplyTimeoutMs * 1000` 关联，避免默认漂移。
4. TX `tx_diagnostic_text.hpp`/`main_tx.cpp` 增加 RFT，保留原 RF/RFN 和二进制布局。
   25 kHz、GPIO、射频功率/频点、10 ms slot、Host/RX 500 ms 保护与 C6 离线阈值不变。

TX 观测帧示例（示例数字不是现场测量）：

```text
RFT n1 d100 l2 h3 a4000 z5500 r4
```

`d` 带时间戳的 Ok 结果数，`l` 时间拒收数，`h` 被接受且处理结束越过窗口数；
`a/z` 是最近观察/处理结束相对窗口起点的 µs，未知观察为 UINT32_MAX。
`r` 最近拒收原因：0无、1envelope、2network、3node、4sequence、5观察迟到、
6不可信时间、7无元数据且返回迟到。成功不清 r；不能把历史 r 当作当前包结果。
每秒最多一条，默认轮转曾见节点；冷启动无 seen 仍输出 Node01。
RX RAM 计数包括 ownPolls、malformed/wrongNetwork/stale/expiredControls、
replyAttempts/Successes/Failures、lateReplyCalls/expiredRepliesSkipped、last/maxReplyCallAgeUs。
“调用年龄”不是 RF 发射沿。RX 没有新增 Serial，也未挪用 reserved；**C6 目前读不到
RX RAM 计数**，需要仪器或另行设计安全调试接入，不能承诺单靠 RFT 唯一定位 RX 发射失败。

## 回归和构建证据

- TX 精确新用例在旧完成时刻判定下 RED：649 项中 6 失败；修正后 native 693 PASS。
  `./firmware/scripts/run_native_tests.sh`；ASan/UBSan 693 PASS。
- `bash firmware/scripts/run_rx_reply_timing_tests.sh`：509 PASS，ASan/UBSan 同 509。
  覆盖 duty 0/1/64/128/192/254/255、读取处理注入 0/400/1300/2500/3600 µs、
  中间 duty PWM 写入注入 700 µs、wrap、legacy fallback、过期/非法/非 own-poll、
  重复包不续 watchdog、500 ms 失效与发送失败。
- 精确 RX RED：`/tmp/fanlink-rx-timing-red.jEFFEs/rx_controller_old_delay.cpp`
  仅恢复固定完整延迟，测试 line 93 turnaround 断言失败（exit 1）；当前实现通过。
  注入 3600+700 µs 时，新调用年龄 4300 µs，旧公式 5500 µs；**这些不是实测中断开销**。
- 实际 RadioLib 7.7.1 的 SPI/GPIO 模拟全部 PASS，原始日志
  `build/supervised-radio-adapter-tests.log`：sync-only/rearm、映射失败不启动RX、
  4999/5000 µs、fractional/zero budget、处理延迟、wrap、元数据/SPI/CRC、BUSY/TX_DONE。
  模拟实际库不等于实际硅片测量。
- 主任务门禁：PWM 2657、PC 21、真实 C6 header 协议 1277、CTest 1/1 PASS；
  虚拟 12 小时 4,320,000 slots PASS。虚拟时间不替代真实连续运行。
- 本地 clean 三目标 3/3 exit 0。PlatformIO 6.1.19、espressif8266 4.2.1、
  ESP8266 Arduino 3.1.2、RadioLib 7.7.1。`BUILD_INFO.json` 时间为
  2026-09-07T13:34:13.040767Z，`hardware_verified=false`。
- 联合端到端注入测试 1804 PASS，ASan/UBSan 同 1804 PASS。SHA256SUMS 列出
  的 7 项本地回读均匹配；构建输入筛选的 29 个编译相关文件 29/29 匹配。
- 最终 `make verify`：PASS exit 0，原始日志 `build/supervised-radio-make-verify.log`。
  本项结果补入后再刷新文档 manifest，JSON 与 diff 检查通过；生产输入未再修改。

产物目录：`firmware/dist/20260907-supervised-radio-solution/`。同目录含逐目标
`.build.log`、`BUILD_INFO.json`、`SHA256SUMS`。构建输入 manifest 原记录在
`build/SOURCE_MANIFEST.supervised-build-input.sha256`，已复制为产物目录的
`SOURCE_MANIFEST.build-input.sha256`；其 SHA-256 为
`39888ad71d622abee6c19c558f51eea88eabce7c0aa74c7cc13b903e72b43dc5`，与 BUILD_INFO
一致。最终文档 manifest 由主任务刷新；本地文件校验不是设备 Flash 读回。

| 文件 | bytes | SHA-256 |
|---|---:|---|
| fanlink_tx_esp8285.bin | 296816 | c3a8529892d7e9e523a92ec6235a2907a10ebdf69d5afe9e5e82828a2208bb18 |
| fanlink_rx_node01.bin | 290848 | 27056a3a5c642cb6073289c45318666032d6d6e830a9dbcfcbad0186e4ef0c9d |
| fanlink_rx_node16.bin | 290848 | ed2cb2ce687b634a97587d212bdfc982fca2dea26415d95745661afd9aa72c92 |

可复现构建（新批次名防止覆盖证据；依赖使用已有本地缓存）：

```bash
PLATFORMIO_CORE_DIR=/tmp/fanlink-pio-core-v013-preflight \
  uv run --offline --with 'platformio==6.1.19' python3 firmware/tools/build_firmware.py \
  --tx --rx 1 --rx 16 --run-id <new-unique-run-id>
```

## 当前已刷版本与回退

现有直接 ROM 写入的 0.1.5-dev 批次为 `firmware/dist/20260907-link-timing/`：

- TX SHA `e68763f2c9ab777947bfffcda74c2aa9b64c83f7e9d9984baf6138a266996a0c`。
- RX01 SHA `5414c7e775e6558c1603409f0ad8c83d15c66967a55dff62a8995ac67affd5fb`。

回退是重新写入这些保存的镜像，不是承诺该版本无线稳定。风扇安全优先，异常时
应由用户用已验证的本地安全方式保持全速/独立散热。没有 Flash 备份或设备读回。

## 现场验收与下一步唯一动作

下一步是用户确认**当前连接目标及 BOOT**后，先单独刷 TX 候选，保持 RX 0.1.5
作分阶段对照；再经新的 RX01 目标/BOOT 确认刷 RX01。不复用旧“已接入”消息。
不同时改变 C6/PWM频率/硬件接线，保留可比较的 A/B 结果。C6 任务独立提供观测
兼容性候选，本轮没有部署；旧 C6 日志统一限流可能隐藏 RFT，缺行不等于未发送。

刷写遵守已知 SSH 串口直写流程：本地核验目标 bin SHA，目标和串口占用身份确认
不打开串口预读；115200、esptool 5.0.2、`--chip esp8266 --before no-reset
--after no-reset --no-stub write-flash --flash-mode keep --flash-freq keep
--flash-size keep 0x0 <exact-target.bin>`。不做 read-flash/flash-id/chip-id 独立探测、
不备份/读回、不自动 run/reset；失败即停止，待用户重新确认 BOOT，不循环乱读。
记录写入退出码/字节/日志并释放任务自己的隧道；用户解除 BOOT 正常上电后才算进入运行阶段。

验收顺序（占空比变化和断电注入均须用户现场授权且有独立散热保障）：

1. 固定原来距离、天线、电源和负载，100% 收集不少于 60 秒 Node01 RFN/RFT。
   记录 uptime/计数 epoch，计算同 epoch 的 Δo/Δp；不要用空节点全局 t 算丢包。
2. 逐点 duty 0/1/64/128/192/254/255，各至少 60 秒，记录实际转速响应、RPM/RSSI、
   age/miss、RFT d/l/h/a/z/r。零占空比只在安全负载下进行。
3. 用逻辑分析仪/示波器同步测 RX DIO1、BUSY、PWM及电源。观察低 PWM 时 IRQ
   到处理/发送的时间和供电；不要把 RAM 调用计数当成实际 PA 发射成功。
4. 若低 PWM 仍无回传且 TX d 不增：继续测 RX own-poll 到实际 RF 发送及 TX 接收状态；
   若 l 增则量出真正时序；若 h 增且 o 恢复，说明修复覆盖了完成迟到路径。
   RX RAM 不可远程取得是明确诊断限制，不能凭无计数断言 RX 没收到。
5. 单独验证 Host 失联、TX 断电、坏包、重复旧包、RX 重启的全速保护（默认 500 ms
   加实际有界循环延迟需实测）。不能为了维持“在线”解除保护。
6. 每个目标点回传不再持续消失后，运行至少 30 分钟再 8 小时，报告节点数量、
   时长、丢包率、最大 age、复位/恢复计数和波形；100% 偶断也需统计和解释。

若异常明显加重，停止测试，保证全速并在新 BOOT 确认后回退已保存镜像。
只有这些物理结果支持改善后，才能称为现场问题解决；目前是源码/编译完成的候选方案。
