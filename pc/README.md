# FanLink PC 控制面板

本目录包含 FanLink 的 Python 后端、浏览器面板和 Host 串口工具。后端连接 TX 的
115200 baud 串口，显示 16 个节点的 duty、RPM、上下行 RSSI 与射频诊断。
配套 TX/RX 固件位于 [`../firmware/`](../firmware/)。

## 启动

以下命令在本目录 `pc/` 中执行。使用 `uv` 临时解析串口依赖并启动：

```bash
uv run --with 'pyserial>=3.5,<4' python3 tools/fanlink_panel.py --open
```

浏览器默认打开 [http://127.0.0.1:8765/](http://127.0.0.1:8765/)。在页面中选择
TX 的实际串口并连接；后端默认仅监听本机回环地址。完整运行依赖记录在
[`tools/requirements.txt`](tools/requirements.txt)，静态网页无需 npm 安装或前端构建。

没有硬件时可启动模拟模式，不需要串口依赖：

```bash
python3 tools/fanlink_panel.py --demo --open
```

通过 `--port 8766` 可以更换 HTTP 端口；省略 `--open` 则只启动服务，不自动打开浏览器。

在仓库根目录也可以使用：

```bash
make panel       # 启动真实串口控制面板
make panel-demo  # 启动模拟面板
make panel-test  # 运行面板测试
```

## 串口 CLI

以下命令同样在 `pc/` 中执行。把示例端口替换为设备的实际端口名，例如
macOS 的 `/dev/cu.usbmodem…`、Linux 的 `/dev/ttyUSB0` 或 Windows 的 `COM5`。
使用 CLI 前先在面板中断开串口，避免两个程序争用同一设备。

```bash
# 只读取 TX 状态，不发送维持 Host watchdog 的命令。
uv run --with 'pyserial>=3.5,<4' python3 tools/fanlink_cli.py \
  --port "/dev/cu.usbmodem<device>" monitor

# 每 200 ms 发送全部节点全速请求，持续 10 秒。
uv run --with 'pyserial>=3.5,<4' python3 tools/fanlink_cli.py \
  --port "/dev/cu.usbmodem<device>" hold --all 255 --duration 10

python3 tools/fanlink_cli.py --help
```

CLI 还提供 `set`、`set-list`、`full`、`heartbeat` 单次发送命令。单次发送不会持续
维持 TX 的 Host watchdog；需要持续控制时使用 `hold` 或面板。

## 控制语义

- 初始、断连和每次重新连接时，16 路本地请求均恢复为 `255`（全速请求）。
- 连接后每 200 ms 发送完整 `duty[16]`，维持 TX 默认 500 ms 的 Host watchdog。
- duty 范围为 `0..255`：`0` 是停转请求，`255` 是全速请求；RPM 是接收机回传，
  不代表闭环转速目标。
- 模拟界面和离线测试只验证 PC 程序行为；真实 RF、PWM、RPM 与失效保护验收见
  [`../docs/CONTROL_PANEL.md`](../docs/CONTROL_PANEL.md)。

## 离线检查

在 `pc/` 执行：

```bash
python3 -m unittest discover -s tests -p 'test_fanlink_panel.py' -v
python3 tools/fanlink_cli.py --self-test
node --check panel/app.js
```

测试使用模拟串口与本机临时 HTTP 端口，不要求连接硬件。Node.js 仅用于上述
JavaScript 语法检查，运行面板不依赖 Node.js。

目录内容：

- `panel/`：浏览器 HTML、CSS 和 JavaScript。
- `tools/fanlink_panel.py`：本地 HTTP/API 服务、串口连接和发送循环。
- `tools/fanlink_cli.py`：Host 协议编解码、命令行控制与监视。
- `tests/test_fanlink_panel.py`：输入校验、状态、模拟串口、HTTP/API 与前端约定测试。

项目说明见 [`../README.md`](../README.md)，串口与空口协议见
[`../PROTOCOL.md`](../PROTOCOL.md)。
