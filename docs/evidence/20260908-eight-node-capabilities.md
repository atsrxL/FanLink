# 2026-09-08：8 节点及维护能力补齐（0.2.0-dev，未刷写）

## 本轮目标与实现

按用户要求支持最多 8 RX、正常轮询 125 ms（一秒完整周期），保留 TX 软件
重启/ROM BOOT、本地串口 OTA 和 RX 空中 OTA；补齐 TX/RX 持久化功率设置、
RX uart/pwm/pwmswap 与 5120 B/s OTA 双端上限。引脚定义已获用户明确确认。
入口/权限/迁移边界详见 `../CAPABILITIES.md`、`../CONFIG_WIRE.md`、ADR-023。

实现文件：protocol/controller/poll_cadence/build_config；`main_tx.cpp` 的协作式
循环与 settings bridge；`settings*.hpp`、实际 settings_store/runtime_rx_pins、
统一 `main_rx.cpp`；OTA TxBridge 和 PC uploader 限速；PC config 工具及八节点面板。
代码仍在原有脏工作树中，未广泛 stage/commit/push，也没有删除其它用户改动。

## 软件验证

- 起始 native 基线：1433 checks PASS。最终 native 及 ASan/UBSan：2744 PASS。
- `make verify` 最终 exit0：`build/20260908-eight-node-capabilities-final-verify.log`。
  settings/service/Flash 失败注入、运行时四模式×两极性全部 PASS；TX settings
  ACK FIFO 保留、未写 ACK 不重启、重复不重写、不延期、身份错误/超时无假成功 PASS。
- OTA receiver 8102 checks；实际 storage adapter 242 checks；TX/RX OTA integration
  包含 5120 B/s、重传、micros 回绕、闲置无突发额度、Host 不可绕过限制，全部 PASS。
- PC 76 tests PASS：config12、OTA29、TXupdate12、panel23；Python 编译、CLI selftest、
  JavaScript 语法 PASS。config 同帧有限重试，严格匹配 Host sequence 与 RF身份。
- 实际 PWM 平台 2657 checks、Tach19场景、RX回复时序509、TX/RX联合924 PASS。
- 旧显式10ms配置的虚拟12小时恢复回归：4,320,000 slots，10次micros回绕、
  65次控制序号回绕 PASS；这不是新125ms配置的真实12小时运行证据。
- 新正常 cadence 测试以5ms真实模拟回包等待，验证slot按start+125ms启动，
  多个维护/普通调用共享节拍，八节点1000ms完整循环，迟到不集中补发。
- 实际 RadioLib7.7.1 simulated SPI/GPIO recovery PASS，日志
  `build/20260908-eight-node-radiolib.log`；并非物理 RF 测量。
- CMake/CTest 4/4 PASS；syntax smoke PASS。
- 用实际 C6 `fanlink_protocol.h` 进行旧报文兼容测试1277 PASS，文件SHA256：
  `4de80abe35d75a2ebd0cc958d77265100f7099ab4e1b0c4703c2842485cbfb3a`。
  只证明既有序列化格式，**不证明 C6 新8节点/2500ms状态策略或设置界面已适配**。
- 第一轮总验证遇到旧 UART 诊断测试仍轮询1..16，被新的1..8校验正确拒绝；
  改测试使用kNodeCount及125ms后单项与总验证通过，未放宽协议校验。

## 真实编译与产物

执行（本地，未触碰设备）：

```sh
PLATFORMIO_CORE_DIR=/tmp/fanlink-pio-core-v013-preflight \
  uv run --with 'platformio==6.1.19' python3 firmware/tools/build_firmware.py \
  --tx --rx 1 --rx 8 --rx2-uart-diag --run-id 20260908-eight-node-capabilities
```

PlatformIO6.1.19、espressif8266@4.2.1、Arduino Core3.1.2、RadioLib7.7.1。
四目标均先 clean 后完整构建，全部 PASS，目录 `firmware/dist/20260908-eight-node-capabilities/`。
`SHA256SUMS` 所有二进制、日志、BUILD_INFO 校验通过；四镜像各自只有一个有效 OTA
身份标记，角色/node/network 校验通过（`fanlink_ota.py --inspect`，无串口访问）。

| Target | Bin bytes | Static RAM | SHA256 |
|---|---:|---:|---|
| TX Lite |314240|32624|fd2ab968acb84b5428911ae651ac97c5d387102196aa707c2dc4c570e9c1e69d|
| RX01 Nano |308800|30900|bbcda6f08a73e7a0e6713aa7b00a361645506dcd0944566d12fa4a7e43040b82|
| RX08 Nano |308800|30900|da661e31b52453942632079683244e06080f768f65409329a073bd09bc719df7|
| RX02 Nano 默认UART |308800|30900|e7b4bd44f90a11aa1509aee9bcb0d165d0c0e06f2f387eb1c40ba8d9836184cc|

构建输入清单 `build/20260908-eight-node-capabilities-source-manifest.sha256`，
SHA256 `af87f3203379386122d7cf38e1f847a2f7e436131ada30de8804e4c28377d7b2`。
构建后的变化仅交接文档及上述UART桌面测试，固件编译输入没有改动。

## Flash 空间与掉电边界

实际 ELF `_FS_start=0x402BB000`，`_EEPROM_start=0x402FB000`。
Core Updater.cpp 用 FS_start 之前区域放当前镜像和暂存镜像：TX镜像扇区对齐315392B，
两份630784B，小于可用0xBB000=765952B，余135168B；
RX对齐311296B，两份622592B，余143360B。当前同目标镜像有 OTA 暂存空间。
设置写原始地址0xFB000专用EEPROM扇区，OTA暂存不会覆盖它。

存储每次检查擦除、写入和读回；不是双扇区原子日志。擦除之后瞬间掉电可能留下
全FF，从而在下次启动回到编译默认角色/功率；不能声称任意掉电场景保持旧设置。
OTA最终eboot copy也没有A/B自动回退保证，掉电可能需要有线ROM恢复。

## 硬件和未完成验收

本轮没有打开串口、SSH设备、刷机、重启、调整实际功率或部署C6。
未使用仪器，没有新的RF距离、功率、PWM/Tach波形、吞吐或连续运行实测数据。
既有回程丢包仍未被本轮功能改动证明解决；生产约30cm条件保留，不要求拉开到1m。
旧RX02无OTA诊断版须先有线迁移；新增RX02默认UART镜像才具有无线OTA。
待另行授权：分阶段部署、8节点实测、模式/接线确认、设置重启与断电读回、
TX软件BOOT/本地OTA/RXOTA完整路径、实测速率及长期稳定性，以及C6独立适配。
