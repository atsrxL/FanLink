# FanLink 固件

当前 ADR-025 使用原生 8 槽 Host v2：所有串口外层帧 version 2，SET_ALL/
STATUS/METRICS payload 分别 8/54/16 bytes；无 wire16、补齐、截断或双栈。
帧头 8B、CRC 2B、最大 payload 128B 保留给 OTA/诊断。RF 仍为 v1 固定 30B，
Control 的 8 个 duty 后为 8 个发送 255 的 reserved 字节，不是额外通道；
RF OTA/设置字节、125ms 轮询、500ms watchdog 及 Host 在线/新鲜度策略不变。
TX/C6/PC 必须协调升级；新工具不能触发旧 v1 TX 的 BOOT/OTA，旧 TX 迁移
只能用归档 v1 工具或另行授权有线 ROM 刷写。RX 无需立即刷；本轮不刷设备。

本目录是独立的 ESP8285/ESP8266 + SX1280 PlatformIO 工程，包含 TX、RX Node 01–08、
公共协议与控制器、ESP8266 平台层、桌面测试和固件构建工具。PC 串口控制面板位于
[`../pc/`](../pc/)。协议、硬件说明和失效保护要求分别见仓库根目录的
[`PROTOCOL.md`](../PROTOCOL.md)、[`HARDWARE.md`](../HARDWARE.md) 和
[`SAFETY.md`](../SAFETY.md)。

## 测试

0.2.0-dev 新能力、默认 UART RX 和 C6 迁移要求见
[`../docs/CAPABILITIES.md`](../docs/CAPABILITIES.md)。构建 RX09–16 已不再支持。

在仓库根目录运行桌面测试：

```bash
make test
```

也可以在仓库根目录单独检查固件公共层和平台层语法：

```bash
./firmware/scripts/run_native_tests.sh
./firmware/tools/run_firmware_syntax_smoke.sh
```

跨实现核对生产 FanLinkC6 的实际协议头文件（不复制 ESPHome 私有配置进仓库）：

```bash
bash firmware/tools/run_c6_wire_compat_tests.sh /path/to/components/fanlink
```

该检查覆盖两个实现的逐字节编解码、状态/双向 RSSI/最大诊断帧、CRC 拒绝及
Host 序号回绕，不代表 UART 硬件时序或实机长期运行验收。

运行 12 小时虚拟时钟/生产 200 ms 指令周期及自动恢复故障模型，或用实际
RadioLib 7.7.1 依赖执行模拟 SPI/GPIO 故障检查：

```bash
make recovery-test
make fan-output-test
python3 firmware/scripts/run_radiolib_recovery_tests.py
```

前两者已加入 `make verify`，最后一项在 CI 真实固件构建后运行（需要 `.pio/libdeps`
中的固定版本依赖；测试只修改临时副本，不修改安装缓存）。

PWM 测试直接编译生产平台源码，验证双极性所有 duty、重复 duty 不重写 PWM、
初始化仍实际建立全速输出。RadioLib 测试还覆盖跨软件等待保留 RX/FIFO、CRC 后
重新接收、元数据/FIFO SPI 错误和微秒回绕；均不代表 RF/PWM 实测。

## 构建

从仓库根目录进入 `firmware/`，构建三个基线环境：

```bash
cd firmware
pio run -e fanlink_tx_esp8285 -e fanlink_rx_node01 -e fanlink_rx_node08
```

或从仓库根目录指定工程目录：

```bash
pio run -d firmware -e fanlink_tx_esp8285 -e fanlink_rx_node01 -e fanlink_rx_node08
```

构建环境分别为 `fanlink_tx_esp8285`（Lite 硬件用作 TX）、
`fanlink_rx_node01` 和 `fanlink_rx_node08`。硬件环境与用户已确认的 Lite 板型
必须按当前 `platformio.ini` 核对，不能按历史 Nano 名称选板。
RX Node 02–07 使用对应的 `fanlink_rx_node02` 至 `fanlink_rx_node07` 环境。

需要逐目标 clean 构建并收集日志、依赖记录和 SHA-256 时，从仓库根目录运行：

```bash
python3 firmware/tools/build_firmware.py --tx --rx 1 --rx 8
```

该工具先校验仓库根目录的源码清单，输出到 `firmware/dist/<run-id>/`。
PlatformIO 缓存位于 `firmware/.pio/`；native 测试输出位于 `firmware/build/native/`。
桌面测试、stub 语法检查和真实依赖编译不代表镜像已经刷入或通过实机验收。
