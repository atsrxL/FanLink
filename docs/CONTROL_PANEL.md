# FanLink TX 本地控制面板

控制面板是一个仅在本机监听的浏览器界面。Python 后端独占打开 FanLink TX 的
115200 baud 串口，网页通过本机 JSON API 显示状态并修改 16 路命令。

## 启动

安装/临时解析唯一运行依赖并启动：

```bash
uv run --with 'pyserial>=3.5,<4' \
  python3 pc/tools/fanlink_panel.py --open
```

浏览器默认地址为 `http://127.0.0.1:8765/`。不要用 `0.0.0.0` 将控制面板暴露到
局域网或互联网；当前 API 没有身份认证。

没有硬件时可用动态模拟数据检查完整界面：

```bash
python3 pc/tools/fanlink_panel.py --demo --open
```

## 操作语义

- 初始命令和每次新串口连接均为 16 路 `255`，即安全的全速请求；
- 连接后后台每 200 ms 发送一份完整 `duty[16]`，维持 TX 的 500 ms Host watchdog；
- 单路滑块和数字输入范围都是 `0..255`；逻辑命令 `0` 表示请求停转，`255`
  表示释放 PWM、请求全速；
- “全部全速”立即把 16 路恢复为 `255`；
- 页面断开串口或后端退出后不再发 Host 帧，TX 应在默认 500 ms 后进入
  `HOST_FAILSAFE` 并广播全速；
- RPM、实际占空比、回传年龄、miss 和状态位来自 TX 的
  `STATUS_SNAPSHOT`；射频阶段/错误码和累计量来自 `ERROR_EVENT`。
- 上行和下行 RSSI 来自 Host `0x82` `LINK_METRICS_SNAPSHOT`。其 payload 固定
  为 32 字节，按 Node 01–16 顺序，每节点依次为上行、下行两个有符号 `int8`
  dBm 值；`-127` 表示未知。面板分列显示两个方向，RSSI 快照过期时显示 `—`
  （未知），不会继续展示过期数值。

这里的“转速设定”是开环 PWM duty 命令，不是闭环 RPM 目标。真实 RPM 只作为
接收机 Tach 回传显示。在完成风扇型号、Tach 电压、PWM 波形、全速 RPM 和稳定性
测量前，不应加入未经标定的自动闭环。Node 01 的低有效门极只改变输出级电气
极性，不改变上述逻辑命令语义；`0` 仍是停转请求，`255` 仍是全速请求。

## 实机验收

1. 风扇仍保持断开，只连接 TX UART；在页面选择准确串口并连接。
2. 确认 TX 状态从 Host failsafe 变为 active，页面持续收到 uptime 和 poll ID。
3. 断开页面串口，确认下一份快照或 CLI 显示 TX 在 500 ms 后回到 Host failsafe。
4. 刷入同批 RX 固件并建立射频链路后，确认对应节点的 RPM/age/miss 开始更新。
5. 接风扇前按 `HARDWARE.md` 完成开漏级、Tach 电压与示波器验收。

控制面板成功显示模拟数据或 TX 串口快照，均不能替代 RX 指示灯、实际射频回包、
25 kHz PWM、Tach RPM 和热安全验收。

### 当前实机证据状态

截至 2026-09-04，本轮除离线 fake-serial、demo 和本地 HTTP/API 测试外，也完成了
真实 FanLink TX 串口的部分验收。所接端口为
`/dev/cu.usbmodem<device>`（公开记录已隐去设备唯一编号），USB VID:PID 为 `1A86:55D3`。后续旧 `0.1.2`
TX/RX01 已建立控制和 RPM Reply 链路；新 `0.1.3-dev` RSSI 帧仍未刷写。结果必须
按下表边界解读，不能以 demo 或 API 自身状态代替新固件行为：

| 验收项 | 当前结果 | 所需证据 |
|---|---|---|
| 枚举并打开真实 TX 串口 | **VERIFIED** | `/dev/cu.usbmodem<device>`，USB `1A86:55D3`，POST connect 返回 `connected=true` |
| 连接后的后端初始命令为全部 `255` | **VERIFIED AT API STATE** | POST connect 返回 16 路 duties 全 `255`；未抓取第一帧串口波形 |
| 每 200 ms 完整发送 `duty[16]` | **NOT TIMING-VERIFIED** | 已持续收到状态，但未捕获带时间戳的串口发送波形 |
| 接收并解析 `STATUS_SNAPSHOT` | **VERIFIED** | `host_failsafe=false`、uptime 3130 s、`next_poll_id=2`、fresh age 0.0837 s；rejected/unknown/decode errors 均为 0 |
| 接收并解析 `ERROR_EVENT` | **NOT VERIFIED** | 当前板上旧固件不含新诊断；需更新后做可控故障注入 |
| 接收并解析 `0x82 LINK_METRICS_SNAPSHOT` | **NOT HARDWARE-VERIFIED** | 当前板上旧固件不发送新 RSSI 快照；需更新 TX/RX 后分别核对上下行数值、`-127` 未知值及过期降级 |
| POST disconnect 后面板安全清空 | **VERIFIED AS PANEL STATE ONLY** | API 立即返回全 `255`/全部 stale；该结果本身不证明 TX 已 failsafe |
| TX 断开后回到 failsafe/full-speed | **VERIFIED, TIMING NOT MEASURED** | 独立 CLI 随后连续显示 `TX=FAILSAFE`，16 节点 duty 均为 `255`；未捕获精确 500 ms 边界 |
| 后端进程退出时页面安全降级 | **VERIFIED** | 下一轮询显示“本地服务不可达”、锁定全部控件，watchdog 变未知，RPM/实际 duty/age/miss 清为 `—` |
| 后端退出后的 TX failsafe | **VERIFIED, TIMING NOT MEASURED** | 独立 CLI 再次显示 `TX=FAILSAFE`、16 路 duty `255`；样本 uptime 3276 s |
| USB 拔出、串口 I/O 异常与重连 | **NOT VERIFIED** | 故障注入步骤和状态时间线 |
| 浏览器显示真实 TX 数据 | **VERIFIED** | 实际页面显示与 API 相同的 uptime、poll、failsafe 与节点数据 |
| Node 01 命令到 RX/RPM Reply | **VERIFIED ON OLD 0.1.2** | RX LED 常亮且控制有效；一次快照为 requested/applied=143、RPM=1749、age=0、miss=0、stale=false |
| 修正后的 0/255 物理端点 | **NOT VERIFIED** | 旧镜像 duty=0 时仍约 2787 RPM；需刷 `0.1.3-dev` Node 01 后验证 |

持续快照证明了真实 TX Host 串口收发；后续快照还证明旧固件 Node 01 的控制、
RPM Reply 和页面解析路径已工作。旧 TX 固件没有 `0x82` RSSI 快照，因此不能
声称独立 RSSI、修正后的 PWM 端点或 25 mW 档已经工作。
完整实机记录见 [`evidence/20260904-tx-panel-usb.md`](evidence/20260904-tx-panel-usb.md)。

已实际执行的离线检查为：

```text
python3 -m unittest discover -s pc/tests -p 'test_fanlink_panel.py' -v
  Ran 20 tests — OK

python3 -m py_compile pc/tools/*.py firmware/tools/*.py firmware/scripts/*.py pc/tests/test_fanlink_panel.py
  PASS (exit 0)

node --check pc/panel/app.js
  PASS (exit 0)
```

浏览器已人工显示真实 TX 数据，后端消失时的页面状态降级也已实机验收；USB 拔出、
串口 I/O 异常和重连仍未验收。

当前 `0.1.3-dev` 的 TX/RX01/RX16 clean 固件已完成 3/3 编译，证据位于
`../dist/20260904-v0.1.3-25mw-node01-rssi-reviewed/`。当前连接的 TX/RX 仍运行
旧 `0.1.2` 固件，因此 clean build 不能替代当前固件刷写、应用启动、修正后的
PWM 端点、`ERROR_EVENT` 或 RSSI 实机验收。
