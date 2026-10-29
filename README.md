# ExpressLRS Lua 串口控制台（本机）

在本机浏览器里操作刷了 ExpressLRS **TX/RX-as-TX** 固件的模块，交互逻辑等价于遥控器上运行的官方 [ExpressLRS/Lua-Scripts](https://github.com/ExpressLRS/Lua-Scripts/) `elrs.lua`。

浏览器只是界面；串口独占、CRSF 帧解析与 Lua 参数协议都由本地 Node 进程完成，服务只监听 `127.0.0.1`。

## 启动

```sh
cd /Users/at/Documents/elrs-fan
npm start
```

然后打开 <http://127.0.0.1:8598>。页面里有“演示模拟器（无串口）”选项，可先不接硬件走一遍界面与命令状态机。

提交或部署前运行自动回归：

```sh
npm test
```

## 真机连接

1. USB-TTL 接模块 UART（JHEMCU/Generic 2400 为 GPIO1=TX、GPIO3=RX，3.3V 电平，全双工不反相）。
2. 页面选择 `/dev/cu.usbmodem…`，点“连接”。程序会按 ELRS 固件 CRSF UART 顺序自动扫描：400000 → 115200 → 5250000 → 3750000 → 1870000 → 921600 → 2250000。
3. 若模块已上电超过约 20 秒且没有遥控器，ELRS 会自动进入 WiFi 更新模式，此时 Lua 串口不响应。**保持页面在扫描状态，给模块断电再上电**，启动瞬间就会被扫到并读出参数。

## 功能

- 设备发现与设备列表（TX 模块 0xEE，以及 OTA 链路可达的 Other Devices）
- 按上游 `elrs.lua` 语义读取参数：分块 PARAMETER_READ/ENTRY、FOLDER/SELECT/UINT8/INFO/COMMAND 等类型
- 数值字段进入编辑态后明确“保存/取消”，避免误写；选择项改动即发送
- 命令字段支持 执行 / 确认 / 取消 / 自动轮询状态（lcsQuery），与 Lua 弹窗状态机一致
- ELRS 坏包/好包与警告位显示、活动日志、断线自动重扫
- CRSF Telemetry 面板：链路质量/RSSI/SNR、GPS、电池、姿态、升降率、气压高度、空速、飞行模式、电芯/温度/RPM，以及未知帧原始十六进制回显
- 全部 16 个 CRSF RC 通道滑块与数值输入（1000–2000 µs），按标准 11-bit `RC_CHANNELS_PACKED` 帧以 50 Hz 输出

## RC 输出安全

- 通道输出每次启动都默认为关闭，调整滑块本身不会发送 RC 帧。
- “安全初值”为 CH1/CH2/CH4=1500，CH3 与 CH5–CH16=1000；这只是常见 AETR 起点，不能替代核对实际通道映射。
- 启用按钮有二次确认；控制页面 WebSocket 断开、串口掉线、切换会话或写入失败时，服务端都会自动停发。
- 真机启用前应移除桨叶、固定载具，并确认油门和解锁通道。

## 结构

- `server/server.js`：本地 HTTP + WebSocket
- `server/serial-host.js`：串口独占与波特率扫描
- `server/elrs-protocol.js`：Lua 参数协议（等价 elrs.lua 的 crossfireTelemetryPush/Pop 逻辑）
- `server/telemetry.js`：标准 CRSF Telemetry 解码与最近帧缓存
- `server/rc-channels.js`：16 通道状态、校验、50 Hz 输出与自动停发
- `server/sim-host.js`：离线演示设备
- `public/`：浏览器界面
- `vendor/Lua-Scripts/`：上游官方脚本（保留 commit `836d8f4`，供字段格式对照）
- `test/*.test.mjs`：协议和 HTTP/WebSocket 自动回归
- `test/smoke.mjs`：已启动服务的演示链路断言；`test/probe-serial.mjs <串口>` 仅扫握手、不改参数

## 说明

官方 `elrs.lua` 依赖 EdgeTX 提供的 `crossfireTelemetryPush/Pop` 和屏幕 API，无法脱离遥控器直接执行；本项目在串口侧实现同一套 CRSF 配置会话，而非改写脚本本身。
