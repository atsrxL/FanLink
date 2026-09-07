# FanLink 固件

本目录是独立的 ESP8285/ESP8266 + SX1280 PlatformIO 工程，包含 TX、RX Node 01–16、
公共协议与控制器、ESP8266 平台层、桌面测试和固件构建工具。PC 串口控制面板位于
[`../pc/`](../pc/)。协议、硬件说明和失效保护要求分别见仓库根目录的
[`PROTOCOL.md`](../PROTOCOL.md)、[`HARDWARE.md`](../HARDWARE.md) 和
[`SAFETY.md`](../SAFETY.md)。

## 测试

在仓库根目录运行桌面测试：

```bash
make test
```

也可以在仓库根目录单独检查固件公共层和平台层语法：

```bash
./firmware/scripts/run_native_tests.sh
./firmware/tools/run_firmware_syntax_smoke.sh
```

## 构建

从仓库根目录进入 `firmware/`，构建三个基线环境：

```bash
cd firmware
pio run -e fanlink_tx_esp8285 -e fanlink_rx_node01 -e fanlink_rx_node16
```

或从仓库根目录指定工程目录：

```bash
pio run -d firmware -e fanlink_tx_esp8285 -e fanlink_rx_node01 -e fanlink_rx_node16
```

构建环境分别为 `fanlink_tx_esp8285`（JHEMCU 2.4GHz Lite 硬件用作 TX）、
`fanlink_rx_node01` 和 `fanlink_rx_node16`（BETAFPV 2.4GHz Nano RX）。
RX Node 02–15 使用对应的 `fanlink_rx_node02` 至 `fanlink_rx_node15` 环境。

需要逐目标 clean 构建并收集日志、依赖记录和 SHA-256 时，从仓库根目录运行：

```bash
python3 firmware/tools/build_firmware.py --tx --rx 1 --rx 16
```

该工具先校验仓库根目录的源码清单，输出到 `firmware/dist/<run-id>/`。
PlatformIO 缓存位于 `firmware/.pio/`；native 测试输出位于 `firmware/build/native/`。
桌面测试、stub 语法检查和真实依赖编译不代表镜像已经刷入或通过实机验收。
