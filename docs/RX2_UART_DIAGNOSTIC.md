# RX02 原串口诊断固件

独立目标 `fanlink_rx_node02_uart_diag`，只用于断线定位，不是风扇控制固件。
复用 BETAFPV 2.4GHz Nano RX 射频配置、Node ID=2、原固定频点与回包规则。

- GPIO1 = UART0 TX，GPIO3 = UART0 RX；115200 8N1。
- GPIO1/3 不初始化 PWM 或 Tach，不挂测速中断，也不换到其他风扇引脚。
- 必须断开这两个脚上的风扇控制/测速电路，使用3.3V串口并共地，不能接5V信号。
- 虚拟输出固定 appliedDuty=255，RPM=0，TACH_TIMEOUT/STALL 是无传感器的预期值，
  不能当作实际风扇转速、全速保护或物理输出的证明。
- 仍只在 poll_id=2 时回包，不冒用Node01；不要同时给另一接收机使用Node02。
- 不注册 OTA updater，不带正常 OTA 身份标记；恢复生产固件必须有线刷写。
- 日志只在控制器完成一次处理后输出，固定缓冲、限速、FIFO不足时丢日志；
  不等待终端、不flush、不在ISR和回包延迟窗口内打印。
- 串口输入不解释为控制、复位、BOOT或升级命令。

```sh
PLATFORMIO_CORE_DIR=/tmp/fanlink-pio-core-v013-preflight \
  uv run --offline --with platformio==6.1.19 python3 firmware/tools/build_firmware.py \
  --rx2-uart-diag --run-id 20260908-rx02-uart-diag
```

## 对照方式

使用两个独立串口同时记录TX与RX02，并保留主机接收时间戳和设备uptime。
RX ownPolls 只代表收到并处理自己的轮询；replySuccess 只代表本地射频发射完成，
不是TX接收确认。TX侧须以Node02的RFN poll/accepted差值判断回程成功率。
有效下行持续而TX缺回包时重点查回程/接收窗口；RX下行也停止时结合CRC、I/O、
过期/旧序号拒收、射频恢复计数与reset/uptime区分。计数饱和或设备重启后不能直接
跨段相减。原始header/sequence只代表捕获字节，不等同于CRC/网络/序号全部通过。

本变体移除了PWM/Tach负载并增加少量UART工作，不能据此证明原风扇负载下的
时序/供电问题已消失。日志和两个接收机的表现只是定位证据，不是长期稳定验收。

刷写仍按既有直接ROM write流程，不预读Flash、不自动复位；成功后需解除BOOT
并正常上电才会出现115200日志。ROM上电输出可能采用不同波特率，不据此判坏。
