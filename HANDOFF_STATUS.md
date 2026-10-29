# 2026-09-11 RX1 legacy升级成功，测速诊断发现大量异常边沿

311664B/1651.331s，FINISH与COMMIT明确ACK，整次6次超时重试，无BadOffset。
重启后Query确认pwmswap/+13/flags0。新诊断3次raw304272→409270→506645，
valid0，reject18→32→44，age0/9/7ms；约61k原始ISR下降沿/秒，非正常风扇测速。
不是无输入，但尚不能区分噪声/异常信号/引脚线路/中断配置原因。
透传已关闭，35s恢复验收通过，连接全部释放。TX未重复刷写/模式功率未改。
证据docs/evidence/20260911-rx1-legacy.md。以下失败记录为历史，RX1现已升级。

## 2026-09-11 允许慢传后再试：RX1未提交失败，已恢复

取消低速门槛后持续1214.748s，113500B(36.42%)确认；下一块2次BadOffset
加10次超时耗尽12次尝试。未Finish/Commit，Abort明确ACK成功。透传false、
RX1online/miss0/PWM100/RPM0/pwmswap/QueryConfirmed，全部连接已释放。
TX未重复刷写，RX1仍旧应用。证据docs/evidence/20260911-rx1-slow.md。

## 2026-09-11 RX1带统计重试：10KB门槛主动取消，恢复正常

仅PC上传器增加逐次ACK/BadOffset/timeout/RTT统计，OTA36/TX12测试通过。
RX1短窗口10000B/115.365s约86.7B/s，100块307次发送、101缺片回复、
106超时、无首发成功；超时耗87.22s。达到低速/高重试门槛后主动Abort，
收到明确ACK，未Finish/Commit/重启。TX未重复刷写。
35s恢复确认透传false/RX1online/PWM100/RPM0/pwmswap/QueryConfirmed/miss0，
全部连接释放。RF丢失还是FIFO/rearm时序尚未区分，不能宣称根因修复。
证据docs/evidence/20260911-rx1-probe.md。未提交推送。

## 2026-09-11 部署：TX成功，RX1空中升级未提交失败

用户授权C6透传先TX后RX1。TX316672B、225.617s收到CommitACK，重启uptime7s
及RX1查询确认通过，保留+13/flags0。RX1传输约92-95B/s，73000B(~23%)后
BLOCK_FINISH连续12次无匹配ACK，829.48s失败，commit_sent=false。未盲目重试。
已退出透传并35s恢复验收：RX1online/pwmswap/QueryConfirmed/PWM100/RPM0/miss0；
TCP/API全部释放。RX1仍旧应用，无新测速计数；TX新镜像已部署。
证据docs/evidence/20260911-tach-deploy.md。未切引脚/改功率，未提交推送。

## 2026-09-11 测速诊断候选构建（随后仅TX部署）

用户确认风扇实际旋转，要求新增原始边沿/有效估计/滤波拒绝/最后边沿年龄
诊断。ADR-027新增独立RF0x42/43、Host0x16/94，只读查询不重启、不改模式、
不写配置、不刷新控制watchdog；现有Control/RPM/STATUS布局与滤波不变。
加入PC读取工具、严格匹配/超时/安全回归及平台计数测试。完整make verify
通过；TX/RX1/RX8/RX2UART四目标真实clean编译通过，SHA清单校验全部OK。
批次firmware/dist/20260911-tach-diagnostics；TX316672B，RX各311664B。
本轮未连接设备、未切换引脚、未OTA；不能从新代码推断当前故障原因。
说明：docs/TACH_DIAGNOSTICS.md；证据docs/evidence/20260911-tach-diagnostics.md。

## 2026-09-08 无效设置回退修复：代码及四目标构建通过，C6已OTA

ADR-026取消旧/损坏/不可读EEPROM引发的运行Safe停用模式，使用已验证的
编译默认模式和功率；有效保存值保留，不自动写Flash。invalid标志仅提示
使用默认值，显式设置后可持久化。错误编译配置的拒绝运行保护、失联全速、
watchdog和OTA校验保留。C6支持旧Safe254恢复和默认值持久化，修复设置死锁。
make verify与TX/RX1/RX8/RX2UART四目标clean构建通过；批次
`firmware/dist/20260908-settings-fallback`。C6已正常OTA并45秒API复核，
新构建09:15:02UTC。RX1空中升级310272B、1020.184秒Commit ACK成功；
重启后Query确认pwmswap/+13dBm/flags2，新默认回退已生效。C6单次显式
同模式持久化45秒Query确认通过，RPM已恢复上报约1572；最终原始Query
确认pwmswap/+13/flags0，透传false、无维护连接。恢复后收到新的43%请求，
最终Applied43.1373/RPM838/misses0；未覆盖该外部请求。末次helper的100%
断言因此失败，不是升级失败。设置重启期间曾有一次TX状态超时后自动恢复，
仍有sequence_gap/RF诊断，不能宣称全部无线问题消失。
TX/RX2/RX8本批仅编译，未刷写。RX2当前离线，未改UART定义。
射频CRC根因未解决，实体风扇控制仍待修复后验收。
证据：`docs/evidence/20260908-settings-fallback.md`。未提交/推送。

## 此前RX1 ACM0 ROM刷写成功，仍停留BOOT（历史）

用户最新确认ACM0=RX1、ACM1=RX2；本次仅操作RX1。写入同批
fanlink_rx_node01.bin（310256字节，SHA256
ddb250bc8c4342b4c3ad28ea8ccef1fcdc52da3a7f17da869ac7e78de062fb26）。
ROM报告补齐写入310272字节、36.3秒、exit0；不预读/读回Flash，不自动复位。
独立确认ACM0已释放、bridge running0。需移除BOOT短接并重新上电，
应用运行、保存设置及无线回传尚未验收。RX2本次未操作。
日志：build/20260908-all8-rx1-rom.log。

## 此前RX2 ROM尝试：连接失败，未擦写

用户授权ACM1手动BOOT直写同批RX2 UART镜像。首次被旧USB路径守卫拦截，
未打开远端UART；只读核实新拓扑后重试，ROM同步返回Invalid head0x52/exit2，
未进入擦除或写入。无Flash预读/读回/复位；ACM1已释放、bridge0。
需用户重新确认BOOT/串口接线；此前OTA证据保留，当前应用运行未重新验收。
日志：build/20260908-all8-rx2-rom-retry1.log。

## 此前：全8节点构建通过，TX/RX2/C6 OTA完成

真实clean构建覆盖TX、RX01..RX08、RX2 UART共10目标，make verify通过；
批次`firmware/dist/20260908-all8-upgrade`，10项大小/SHA及验收边界见
`docs/evidence/20260908-all8-upgrade.md`。其他7台RX不可达，仅构建未刷写。

RX2 RF OTA310256字节，380.205s收到Commit ACK；35秒恢复及额外8秒复核
通过，末段新鲜状态快照33/32（不是独立RF包数），parser拒包0。Query确认
Lite profile1/UART0/+13dBm/flags0。未直接验证RX uptime复位或Flash读回；
证据仅支持完整传输Commit ACK、短时恢复和配置保留，不是实体风扇验收。
RX操作后双UART经fuser确认释放、bridge running0；不代表后续TX操作状态。

TX UART OTA315472字节，115.413s收到Commit ACK；35秒后uptime由5466降至32，
Host v2/STATUS54及单调检查通过，tail32新鲜快照；额外8秒uptime40/tail32。
双Query v2确认TX Lite13/flags0、RX2 UART13/flags0，8节点/125ms/5120Bps。
158.506s升级恢复通过，161.511s relay关闭；独立再次确认双UART释放/bridge0。
生产C6 compact8/pinmode已OTA完成，新编译2026-09-08 07:12:24 UTC，
45秒API连接观察通过；真实HA注册与状态确认8个启用的config select，
每个都有uart/pwm/pwmswap，另有8个启用的设置状态sensor。
当前C6全部Connected=false、模式unknown，尚未收到在线RX Query；
C6↔TX物理连接及端到端设置验收待用户接线确认，本轮未切引脚/功率。
RF CRC根因与长期/物理安全验收仍未解决；本轮未提交或推送。

## 前次原生8槽 Host v2源码候选（历史，以下未部署指该次工作）

用户要求去掉16槽封装和冗余代码。当前源码Host全部外层帧版本2，
SET_ALL=8、STATUS=54、METRICS=16；TX/PC/C6均移除16槽数组、补位和截断。
RF仍v1固定30字节，Control为8 duty+8 reserved255；没有更改射频参数。
OTA节点上限统一8；125ms轮询、500ms硬件watchdog及既有Host容错策略不变。
本轮不接设备、不刷写、不OTA、不修改HA、不提交推送。源码候选不是已部署状态。
TX必须与C6/PC协调升级；旧TX使用归档v1工具或有线ROM进入v2，不能用v2工具
直接控制旧TX。RF字节不变，因此RX不必为串口迁移立即重刷。
证据：docs/evidence/20260908-compact8-host-v2.md（含验证和构建结果）。
最终native2902（普通+ASan/UBSan）、PC111、C6/TXwire1105通过；
本任务独立复跑C6 sanitizer425990通过。TX/RX01/RX08/RX2UART真实clean构建
通过；最终TX产物在firmware/dist/20260908-compact8-host-v2-final，
RX候选保留在firmware/dist/20260908-compact8-host-v2。

## 前次DIY安全评估及Host离线容错候选完成，未部署（历史）

仅有条件适用于非关键DIY开环风扇控制：独立过温保护、可靠风扇供电，且需要
实体故障注入确认掉电/复位/失联默认全速。RX2 UART采集不能验收实体风扇。
历史30分钟19段遥测过期按新规则重放不触发离线（最大连续miss4/age4.8s）；
最新5分钟1段也不触发，但陈旧数据仍未知，不能宣称RF丢包已修复。

PC候选保持连接需miss<5且报告age+快照等待<6s；第五次漏回包或6s即离线。
2.5s或bit7起RPM/实际PWM/RSSI不显示为当前；真正离线需fresh/miss0恢复。
Host失信仍按原期限离线，不套6s宽限。生产C6由原任务独立实现同策略，
并额外保守检查age下降/非零miss清零作为恢复证据；失信后的首张只建基线。
C6原750ms安全期限/200ms控制/全速flush保留；wire16和活动8节点不变。
100ms量化/UART时延使6s不是精确物理边界，同相采样可能延迟C6恢复。

FanLink最终make verify PASS：build/20260908-diy-safety-verify-final.log，
含PC33项、JS执行渲染/语法、原固件native/恢复/OTA/时序/安全回归。
C6最终miss0初连/恢复门槛已收紧，独立安全复核PASS；本任务直接复跑
native360144检查（含liveness3454）、ASan/UBSan与三套生产接线检查PASS。
原任务完成回报遇到平台错误，不影响上述本机复核结果。
本轮TX/RX固件、空口、功率、引脚、watchdog均未变；无需重刷两端。
未访问设备、未OTA/重启/刷写、未构建新ESPHome镜像，未提交/推送。
证据：docs/evidence/20260908-diy-safety.md。以下为既有部署和诊断历史。
另：C6任务随后报告用户新授权去掉后8实体并部署；已回传只读协议契约：
当前源码及最新已刷记录仍wire16/active8，SET_ALL16、STATUS102、METRICS32。
新实体/HA清理和部署由C6任务另行执行，不属于上述已验证候选和本轮部署结论。

## 此前双端软件射频诊断已部署，5分钟同步采集完成

用户无逻辑分析仪，授权软件诊断。固定容量事件环、延后UART输出和离线分析器
已完成；make verify通过，四目标真实clean构建通过，产物
`firmware/dist/20260908-lite-radio-trace`。RX2空中OTA与TX串口OTA均COMMIT并
恢复遥测；两端Lite/+13dBm/flags0，RX2 UART，8节点/125ms保持不变。
最终采集300.071s（含清理306.084s），证据
`docs/evidence/20260908-radio-trace.md`。排除初始3s、截到300s的可比窗口：
296轮询/267有效/29超时/29CRC（CRC与超时重叠），1186/1188状态新鲜，
1段短暂过期，最大age2800ms。未见两端重启，RX回复失败/IO/恢复增量均0。
83个完整事件快照，无解析错误；CRC均有芯片IRQ和PacketStatus双重证据。
未捕获清IRQ失败、DIO/BUSY卡死或异常重挂接收返回值；不能排除物理短脉冲。
TX好包/坏包RSSI均约-16.5dBm；RX坏包RSSI显著低，但坏包来源无法确认。
射频根因仍未证实，不能宣称修复。下一候选为有界读取CRC坏包FIFO作诊断，
尚未实现。C6/功率/引脚/空口参数未改，无Flash预读/读回，未提交或推送。
两UART已确认释放，bridge running0，当前无采集或自动任务。以下是历史记录。

## 此前30分钟长测、两端修复版升级和5分钟复测

12:09:52..12:39:52CST完整采集1800.118s；1796轮询、1582有效回包、214超时、
207 CRC（与超时重叠），19段遥测过期，最大age4800ms；未见两端复位。
证据`docs/evidence/20260908-lite-30min.md`，独立报告
`build/20260908-lite-longtest-baseline-analysis.json`，parser rejects0。
已修复可选诊断饥饿和RadioLib TX_DONE误判，verify/真实库模拟I/O/ASan通过，
四目标clean构建产物`firmware/dist/20260908-lite-longtest-fixes`。并未证实CRC根因。
RX2 RF OTA308848B/410.087s，TX UART OTA314160B/103.098s均COMMIT ACK、
重启后恢复遥测，Query两端仍Lite13/flags0、RX2 UART。RX首轮脚本在COMMIT后
因内部维护保活误判停止；独立60s验证恢复后仅继续TX，没有重传RX。
13:00:46CST起300.026s有效双端数据，排除初始3s后296轮询/270有效/26超时/
24 CRC，1184/1188快照新鲜、2段过期后均恢复，最大age2800ms；未见两端重启。
RX本地回复296/296完成，失败/过期跳过0；已有恢复计数55但本窗口不增长。
五类可选诊断各297条、最大聚合间隙1.427s，饥饿改善；CRC仍有IRQ+PacketStatus
双重证据，不能宣称无线根因修复或长期稳定。独立报告
`build/20260908-lite-longtest-postfix-analysis.json`。
两UART已fuser确认释放，bridge running0；heartbeat `fanlink-lite-30`已删除。
C6/功率/引脚/占空比未改，无Flash预读/读回，未推送仓库。当前无运行中采集。

## 此前60秒运行验证

用户正常上电后Query确认TX Lite13/flags0，RX2起初Safe254/-18/flags2。
已依次设置RX2 UART与13，各等待自动重启后Query确认，最终两端Lite13/flags0，
RX2 UART日志恢复。60秒采样239/239快照新鲜，无过期/重启，最大age1900ms；
排除初始缓存后56轮询46有效回包、10超时/10CRC错误（约17.9%漏回包），仍未修复
RF丢包。两串口已释放；本次未改TX设置、占空比、C6或固件，未推送仓库。
以下BOOT状态是此次正常上电之前的刷写记录。

用户更正 TX/RX 都是 BETAFPV Lite，已移除 RX Nano 外置 RF 开关/DC-DC配置，
使用 Lite settings profile，两端默认最高芯片+13dBm。make verify、十环境板型
回归、四目标 clean build与SHA全通过。批次 `firmware/dist/20260908-both-lite-max13/`。
TX=ACM0：运行Query确认Lite13/flags0，软件BOOT ACK后ROM直写成功，314368补齐
字节/35.6s/exit0，保持BOOT；无Flash预读/读回/复位，应用运行尚未验收。
RX2=ACM1：用户手动BOOT后ROM直写成功，309248补齐字节/35.4s/exit0，保持BOOT；
无Flash预读/读回/复位，未擦设置区，此次TX未动。正常上电后需明确恢复UART和13，
并逐项重启Query确认；旧Nano设置会被Lite拒绝为Safe254/-18，应用/RF尚未验收。
证据 `docs/evidence/20260908-both-lite-max13.md`；C6未动，未推送仓库。

## 上一批0.2.0运行记录（RX为旧Nano配置，已更正硬件判断）

最新用户授权恢复三项设置完成：TX+13dBm、RX2 UART/-6dBm，各自动重启后
Query flags0确认。60秒复测239/239快照新鲜、RX串口恢复、无过期；排除启动
缓存后TX56轮询/45有效回传、11超时/11CRC类事件，仍有丢包，未宣称修复RF。
TX/RX运行时间连续，无恢复事件；UART均释放，C6/占空比/固件未改。详情见
`docs/evidence/20260908-v020-runtime-safe-settings.md`顶部。下面安全回退为此前状态。

最新运行检查：两端均回应新Query协议，但flags2配置不可信，TX为-18dBm，
RX2为Safe254/-10dBm（无UART/PWM）。60秒日志19/59有效回包、37CRC类事件，
RX2串口0字节；正常125ms调度已观察到。未修改设置，待用户确认恢复预定值。
详见 `docs/evidence/20260908-v020-runtime-safe-settings.md`。

随后用户授权RX2：同一新适配器ACM0写入默认UART版本成功，309248补齐字节/35.3s，
exit0，保持BOOT。GPIO1TX/3RX为默认，保留OTA/配置；未读回、未复位、尚未验证运行。
串口已释放。TX/RX2均完成本批写入，下面“RX2未动”仅指之前TX那一次操作。

用户更换串口适配器后，TX经新ACM0直接ROM写入成功：314368补齐字节/35.5s，
exit0，保持BOOT，串口已释放。未预读/读回/复位，尚未验证启动和RF；RX2未动。
日志和精确身份见 `docs/evidence/20260908-v020-dual-rom-attempt.md`。
下面“未刷写”说明仅指此前功能开发阶段，不代表最新TX写入状态。

用户确认 uart=GPIO1 TX/3 RX，pwm=GPIO1 PWM/3 Tach，pwmswap=GPIO3 PWM/1 Tach。
8 RX /125ms 共享非阻塞调度、5120 B/s Host+TX OTA 上限、持久化 TX/RX 功率和
RX 引脚模式已实现；TX 本地 UART OTA/软件重启/ROM BOOT 保留。所有 RX 模式
共用 OTA/设置，旧 RX2 无 OTA 诊断版首次升级须有线。正常8-node周期1s，维护独占
暂停轮询并全速。C6 需独立改8活动节点/2500ms遥测新鲜度，当前没有部署。

真实 clean TX/RX01/RX08/RX02默认UART四目标编译完成，产物
`firmware/dist/20260908-eight-node-capabilities/`。能力、验证和安全边界见
`docs/CAPABILITIES.md`、`docs/evidence/20260908-eight-node-capabilities.md`。
本轮未开串口、未复位/刷机、未推送仓库；新增固件功能不代表当前设备已具备。

## 上一轮历史：0.1.11-dev TX 回传错误诊断候选（未刷写）

本轮只修复错误分类与时序证据缺失，不宣称已修复实机断联。新候选保留此前
TX本地维护能力，RF参数、协议、C6新鲜度和RX02 UART定义不变；不需重刷RX02。
测试、构建与剩余验收见 `docs/evidence/20260908-tx-rf-evidence.md`。
用户实际工作条件约30cm，不再要求拉开到1m；不能把过载作为已确认原因。
make verify、native/ASan/UBSan各1433、实际RadioLib模拟I/O、C6兼容1277及CTest4/4
通过；clean TX/RX01/RX16/RX02诊断4目标编译通过，SHA全校验通过。新TX310416B，
静态RAM33428B；产物 `firmware/dist/20260908-tx-rf-evidence/`。RX产物仅构建回归，
当前部署动作只针对TX且需用户新的BOOT确认；本轮未开串口、复位或刷机。

最新双端120s只收日志：RX02=ACM0、TX=ACM1，用户距离10cm。RX ownpoll/本地
回包均+732，TX接受461/732（62.98%）、CRC+252；出现1次回传过期而RX仍L1。
未见RX重启/恢复或回复超时跳过，指向回程路径但物理根因未定；两路已释放。
后续核实全局CRC类事件+255，其中Node02窗口+252；该指标混合同步/CRC错误。
下一步在原位置部署TX错误分类诊断（需另行授权刷写），不改功率。原始证据
`docs/evidence/20260908-dual-rx02-tx.md`。

RX02 UART诊断变体已按用户确认完成构建（GPIO1TX/GPIO3RX，无PWM/Tach/OTA），
产物 `firmware/dist/20260908-rx02-uart-diag-reviewed/`。ACM0首刷未响应、未擦写；
用户授权重试后09:24CST直接ROM写入成功：源295520B、补齐295936B/34.9s，
exit0、保持BOOT，未预读/读回/自动复位。UART已释放，ACM1未操作。
证据 `docs/evidence/20260908-rx02-uart-diag.md`；后续双端采样已见诊断应用持续运行，
但断联根因与正常风扇负载验收未完成。

新增 TX 在线升级后重启、显式重启和软件进入 ROM UART 下载模式。使用独占 Host
串口，不走 RF，不改 C6。消息、工具、操作顺序和安全边界见 `docs/TX_UPDATE.md`；
构建/测试记录见 `docs/evidence/20260908-tx-maintenance.md`。TX/RX01仍为0.1.9；
RX02另行写入上述诊断版。软件 BOOT 与 TX 在线升级需先有线部署新 TX 后另行授权验收。

以下保留 0.1.9 及以前的独立历史证据。

## 0.1.9-dev 3KiB/s目标候选

最新快速OTA实测完成：同版RX01 300240B，Begin至Commit ACK232.16s，实际
1293.16B/s（1.263KiB/s），比旧模式约3.81倍，**未达到3KiB/s有效速度目标**。
随后100个状态快照中99个新鲜、parserreject0，TXuptime持续推进，exit0且UART
已释放。日志`build/20260908-ota-fast-live.log`；详情见本批evidence顶部。未做
Flash读回、断电或长期稳定性验收，不能据此认证版本或测速精度。

最新RX部署（02:16CST）：用户另行授权RX刷写及重试后，直接ROM写入本批RX01
成功，源300240B、补齐301056B/32.9s，erase1.84s，exit0；ESP8285H16、MAC
[device MAC redacted]匹配当前RX。日志`build/20260908-rx-v019-rom-write-retry.log`。
首次`build/20260908-rx-v019-rom-write.log`为同步失败，未erase/write。成功后未读回、
未自动reset，仍在bootloader，UART已释放。两端均有0.1.9写入记录；启动、RPM与
fast模式实测仍未验证，需要解除BOOT正常上电后另行验收。

随后用户明确授权且确认TX已BL：原任务直接ROM写入本批TX成功，源299088B、
补齐300032B/32.3s，erase2.26s，exit0，MAC [device MAC redacted]匹配Lite TX。
日志`build/20260908-tx-v019-rom-write.log`。未读回/未自动reset，仍在bootloader，
UART已释放。当时RX仍0.1.8；后续另行授权的RX升级见顶部。

100字节Host块中继/固定30字节RF片段/累计块ACK已实现，makeverify、OTA8102、
真实storage242、Python28、TX/RX集成及clean三目标编译通过。新批次
`firmware/dist/20260908-ota-3k/`；SHA/空间/边界见`docs/evidence/20260908-ota-3k.md`。
3072B/s是目标/上传限速上限，不是实测保证；快速模式尚未验证。旧RX迁移仍用默认
`--mode legacy`，待两端运行新版后才能`--mode fast`。C6/引脚/功率/测速算法不变。
此处为源码/构建状态，设备状态以下独立部署证据为准。

第二次授权OTA已完整传输并收到Commit ACK，Begin至ACK883.11s（14分43秒），
随后99个新鲜RX01状态，串口已释放。日志 `build/20260908-rf-ota-live-retry.log`。
这是同版镜像测试，不是独立Flash读回/版本验证；RPM仍0/status6，未证明测速正常。
用户随后授权开发并编译约3KiB/s目标提速版，后续另行授权的TX部署见顶部。

首次RF OTA实测未完成：同版RX01传输超过21%，213.45s时DATA ACK重试耗尽，
未发送Commit。SSH超时断开，掌机重新登录时uptime仅35s并重新枚举USB；原因未定。
取消与恢复状态未确认，不据此判定RF算法故障或升级成功。日志
`build/20260908-rf-ota-live.log`；没有自动重试，等待传输主机稳定。

最新RX部署：用户确认RX已BOOT后直接ROM写入同批RX01成功，源299760 bytes，
补齐300032 bytes/32.9s，exit0；MAC [device MAC redacted]匹配当前RX。
日志 `build/20260908-rx-v018-rom-write.log`。未读回/自动复位，仍在bootloader。
TX/RX01均有0.1.8写入记录，解除BOOT并上电后需验收RPM及RF OTA；以下“RX旧版”为历史。

最新TX部署：用户确认TX已BL后，直接ROM写入0.1.8 TX成功，源298320 bytes，
补齐299008 bytes/32.3s，exit0；MAC [device MAC redacted]匹配BETAFPV Lite TX。
日志 `build/20260908-tx-v018-rom-write.log`。未读回/自动复位，仍在bootloader；
RX保持旧版，尚不能执行空中升级。启动和OTA实机未验证。

本地真实依赖clean编译TX/RX01/RX16已3/3通过；无需编译修复。产物位于
`firmware/dist/20260908-rf-ota/`，RX01/RX16 inspect通过、TX/错误节点拒绝。
RX每份299760 bytes，两份4KiB对齐后606208<=765952；余量159744 bytes。
完整SHA/RAM/标记与日志见本次evidence。尚未刷写/OTA实机；首次两端仍需有线部署。

用户授权开发TX→RX射频OTA并向任务01a06bca-02ba-7ea1-adfa-d067e4b10732
注入要求编译。实现与使用见docs/OTA.md，字节契约docs/OTA_WIRE.md。
已有0.1.7测速修复完整保留；未操作实机。软件检查/构建证据见
docs/evidence/20260908-rf-ota.md；以下均为历史记录。

# FanLink hand-off status

## 2026-09-08 最新：RX Tach 0.1.7-dev 修复与编译完成，尚未刷写

- 用户更换 TX 为 BETAFPV 2.4GHz Lite RX 硬件，继续承担 TX 角色；已直接写入
  0.1.6 final TX（SHA cbb8e9443847cee64aa951859e0695d68bb68469e102fa073ae67cab9a85ca44），
  ROM MAC [device MAC redacted]，补齐写入296960 bytes/32.3s，exit0；无读回/自动复位。
  原日志 `/tmp/fanlink-lite-tx-flash.WXhhig/direct-rom-write.log`。串口已释放。
- 用户报告换 TX 后基本不再跳断联，但 RPM 在0至几万之间跳变，偶尔约1400。
  C6只读事件流确认PWM约70%、在线，RPM出现1490/1531/20746/29211/0；仍有RF超时。
- 本轮仅授权修代码与编译，不刷写、不调整C6控制值或在线阈值。
- 基线 native 708 PASS；详细修复与最终测试/产物见
  `docs/evidence/20260908-tach-filter.md`。不得把软件测试当作测速波形/长期RF验收。
- 实际 Tach 类19场景/14665检查，普通与ASan/UBSan通过；make verify通过，
  TX/RX01/RX16真实依赖clean编译3/3通过。新批次 `firmware/dist/20260908-tach-filter/`。
  仅需另获授权更新RX01；TX/C6不需要因测速修复更新。校验值见证据与BUILD_INFO。

## 2026-09-07 最新：TX 与当前 RX01 均已写入最终 0.1.6-dev

- 用户确认TX已BOOT；直接ROM写入最终TX镜像，源296848 bytes，SHA
  `cbb8e9443847cee64aa951859e0695d68bb68469e102fa073ae67cab9a85ca44`。
- ROM识别MAC [device MAC redacted]，与此前TX一致；擦除0x0..0x48fff用1.82s，
  写入补齐296960 bytes用76.2s，exit0。日志 `build/tx-v016-final-flash.rXC2MS/direct-rom-write.log`。
- 未预读/读回或自动复位，结束Staying in bootloader；隧道退出，UART_RELEASED。
  当前RX01上一轮已写最终版；须用户解除BOOT并上电后验收，尚未证明回传问题解决。

## 2026-09-07 最新：当前接入 RX 已写入最终 RX01 0.1.6-dev，TX 尚未升级

- SSH恢复后USB仍为相同适配器/by-path，但bus由3-1变2-1；本次临时隧道更新
  已核对的sysfs身份路径后成功连接ROM。此检查失败不解释此前所有无回应。
- 最终 `fanlink_rx_node01.bin` SHA `7536699c73b0d7cdbda5e73ca590a280868c9c5b3f29cd4b8750e06c627c5489`。
  源290848 bytes；擦除0x0..0x47fff用1.82s，ROM补齐写291840 bytes用31.7s，exit0。
- 写入命令识别ESP8285H16，MAC [device MAC redacted]，与此前RX写入记录不同；
  本次以用户最新确认的接入RX为目标，不把两次记录认作同一块物理板。
- 日志 `build/rx-v016-retry3.qSDa20/direct-rom-write.log`。未预读/读回、未自动复位，
  命令结束Staying in bootloader；隧道退出并确认UART_RELEASED。
- 用户需解除BOOT并重新上电。尚未运行/RF/PWM验收；TX最终候选仍未成功刷入。

以下失败尝试为历史记录，不代表当前SSH/USB状态。

- 最新 RX 重试前检查：私有刷写宿主 SSH连接超时，未打开串口或启动刷写。
  待确认掌机供电、Wi-Fi及当前IP；未扫描其他地址。

## 2026-09-07 RX01 最新重试：USB 串口未枚举，未写入

- `rg35xx-serial list` 返回 No USB serial ports found，`/dev/ttyACM0`不存在。
  受保护隧道提前退出；esptool无法配置本地端口，exit 2，未连接ROM或擦写。
- 日志 `build/rx-v016-retry.DTgo5X/direct-rom-write.log`。需重新接好USB OTG适配器。

## 2026-09-07 RX01 最终候选刷写尝试：无 ROM 回应，未写入

- 用户确认拆掉回环短接、接RX且BOOT；RX01最终bin SHA核验匹配。
- 直接no-reset/no-stub write-flash返回exit 2 / No serial data received，未开始
  擦除或写入，未预读/读回或复位。日志 `build/rx-v016-ssh-flash.C7wf6R/direct-rom-write.log`。
- 隧道退出，UART_RELEASED；TX和RX01已写记录仍为0.1.5-dev，不能视为已升级。

## 2026-09-07 TX 最终候选刷写尝试：未建立 ROM 连接，未写入

- 第三次独立授权尝试亦无串口数据，exit 2，未擦写；日志
  `build/tx-v016-retry2.S2x9oK/direct-rom-write.log`。隧道退出并确认UART_RELEASED。

- 用户再次授权重试，同样 exit 2 / No serial data received，未擦除/写入。
  重试日志 `build/tx-v016-retry.RTgGYi/direct-rom-write.log`；隧道退出、UART_RELEASED。

- 用户最新确认 TX 接入且 BOOT；最终 bin SHA 已本地核验。RG35XX SSH、USB
  枚举与串口空闲检查成功，仅直接启动 no-reset/no-stub write-flash。
- esptool exit 2：`Failed to connect to ESP8266: No serial data received.`。
  未进入擦除/写入阶段，未预读/读回或发送 reset；不更改已写版本记录。
- 日志 `build/tx-v016-ssh-flash.rqNwgg/direct-rom-write.log`；隧道已退出，
  远端确认 `UART_RELEASED`。待用户重新确认 BOOT/供电接线后再试，不自动循环。

## 2026-09-07 当前：0.1.6-dev 联合修复候选，尚未刷入

- 最终唯一批次 `firmware/dist/20260907-supervised-radio-final/`，旧 solution 批次保留。
  尾审仅修正 TX 时龄向旧取整；RED 3/708 → GREEN 708（含两种时钟回绕），
  ASan/UBSan、联合1804、make verify、clean三目标3/3及最终SHA校验通过。

- 用户复现：RX 重插后恢复，PWM=100% 有回传但偶断；中间占空比调速有效而回传消失。
- 已修复可重复的软件问题：TX 不再因读包完成越过 5 ms 而拒收窗口内观察到的合法包；
  微秒等待保留最后不足 1 ms 的预算；RX 不再额外叠加固定 1200 µs，过期回复不发；
  显式将 FLRC sync error 映射到 DIO，配置成功后才 SetRx，错误后有界重开接收。
- DIO 时间是首次软件观察，不是物理边沿。保留 network/node/sequence/CRC 与
  500 ms 全速保护；未降低 PWM 频率、关闭安全保护或延长 C6 离线阈值。
- TX RED 6/649 → GREEN 693；RX 独立测试 509 PASS，旧固定延迟精确回退测试失败；
  实际 RadioLib 模拟 SPI/GPIO 测试 PASS。构建、SHA 和现场边界以联合证据文件为准。
- 新增 RFT 时序/拒收原因；RX own-poll/发送结果只在 RAM 诊断接口可取，当前不能从
  C6 读取。C6 的兼容性/观测候选由其任务独立负责，本任务不 OTA。
- 当前 TX/RX01 已写版本仍为 0.1.5-dev。候选编译与物理修复不可混称；真实 NMI
  开销、电源/电气干扰和 100% 偶发丢包仍需按步骤现场验收。
- 详细根因边界、测试、产物、回退及下一步操作：
  `docs/evidence/20260907-supervised-radio-solution.md`。不自动刷机，等待最新目标和 BOOT 确认。

## 2026-09-07 历史：TX / RX Node01 均已写入 0.1.5-dev，回传故障调查

- 用户先后单独授权 TX 和 RX Node01，二者均已直接 ROM 写入同批
  `firmware/dist/20260907-link-timing/` 的 `0.1.5-dev`，不是旧 `0.1.4-dev`。
- RX01 源镜像 290,128 bytes，SHA-256
  `5414c7e775e6558c1603409f0ad8c83d15c66967a55dff62a8995ac67affd5fb`；
  擦除 `0x00000000..0x00046fff` 耗时 1.75 秒，ROM 补齐写入 290,816 bytes
  耗时 31.6 秒，exit 0，命令结束为 `Staying in bootloader.`。
- RX 沿用 115200 / `--no-stub` / no-reset / flash 参数 `keep`，未预读、备份或
  读回 Flash，也未发送 run/reset。任务自己的串口隧道已关闭，远端 fuser 确认
  `UART_RELEASED`。写入日志：`build/rx01-v015-ssh-flash.ESx5Yo/direct-rom-write.log`；
  记录见 `docs/evidence/20260907-rx-ssh-flash.md`。
- 用户随后报告实际控制有效但没有回传。目前只调查原因，不据此宣称双向 RF、
  PWM/Tach 或长期稳定性验收通过；没有设备读回哈希。`Staying in bootloader`
  仅描述写入命令结束时状态，不代表用户后续使用时仍在 BOOT。
- 只读诊断：20:58:28.090–20:58:54.359 CST，Node01 `p` 1813→1975、
  `o` 121→121、`t` 1692→1854，`a` 165162→191434 ms；CRC/invalid/I/O
  计数无增量，C6 UART/STATUS/解析错误亦无窗口增量。用户确认现在改变占空比，
  实际转速仍跟随：下行仍通，故障集中于回包链路，不是仅保留旧转速。
- 尚未定案：TX 在 adapter 完成读包及重新监听后才判 5 ms 截止，合法包晚返回
  也只记 timeout；现有 RFN 无法区分它与真正无包。另有单包 RX 结束但软件
  armed 未清的模拟覆盖缺口，但 TX 每槽重开/RX 恢复机制与持续下行使其不足以
  单独解释现场长时间无回传。下一步需区分 RX 回包尝试、TX RxDone 与处理耗时；
  本轮没有修固件或部署，模拟成功不构成真实时序验收。
- 下一步唯一动作：核对当前 TX/RX 与 C6 的回传链路证据、定位原因；本阶段不修代码、
  不再次刷机/OTA、重启或发送风扇控制命令。下方逐轮状态均为历史，不构成新操作授权。

## 2026-09-07 上一轮历史：仅 TX 写入 0.1.5-dev，当时 RX 为 0.1.4-dev

- 用户单独授权 TX。经 RG35XX SSH/USB UART 直接 ROM `--no-stub` 写入
  `firmware/dist/20260907-link-timing/fanlink_tx_esp8285.bin`，源镜像 295,824 bytes，
  SHA-256 `e68763f2c9ab777947bfffcda74c2aa9b64c83f7e9d9984baf6138a266996a0c`。
- ROM 按块写入 295,936 bytes，耗时 33.1 秒，exit 0，最后为
  `Staying in bootloader.`。保留 DOUT/40MHz/逻辑1MiB 镜像头，地址 0x0；
  不备份、不预读或读回 Flash，不发 run/reset。原始日志：
  `build/tx-v015-ssh-flash.qK5yGx/direct-rom-write.log`。
- 当次未改 RX/C6/RG35XX 固件和风扇命令；当时未有 TX 应用启动、读回或 RF 验收。
  当时下一步为用户解除 TX BOOT 后正常上电，RX 仍需另行授权；
  不能将本次 TX 写入成功视为间歇断联已解决。

## 2026-09-07 上一轮：0.1.5-dev 间歇遥测断联优化（当时未刷入）

- 修复每次软件 receive timeout 关闭 RX 的跨 loop 盲区，改为 SetRx(0) 单包无
  硬件超时且保持监听；普通等待仍有界返回以服务 watchdog。成功/CRC 后立即
  rearm，发射/复位/硬错丢弃旧状态；逐次检查 IRQ/长度/offset/status/FIFO SPI。
- 相同 duty 不再每 10 ms 重写 analogWrite；初始化和真实 duty 变化仍立即写。
- 新增每节点 `RFN`（最大 113 bytes），STATUS/METRICS 优先，原二进制协议、
  功率/频率/GPIO、10/5/1.2 ms 时序与全部安全阈值不变。
- 新接收测试在旧实现上失败、新实现上通过。native/ASan/UBSan 各 627 项、PWM
  2657 项、C6 协议 1277 项、PC 21 项、CMake/CTest、12 小时虚拟运行与 make verify
  全通过；TX/RX01/RX16 clean 构建 3/3，通过产物 SHA 回读。详细证据见
  `docs/evidence/20260907-link-timing.md`。本轮没有刷机、重启、控制风扇或提交推送。
- 该源码优化阶段开始时，TX/RX01 已写入上一批 `0.1.4-dev`。C6 原任务另行完成 OTA，
  API build 2026-09-07 07:53:00 UTC；其 19:49:55–19:50:17 CST 观察发现一次
  Node01 missed=5、约 290 ms 日志离线/恢复，期间 UART/STATUS timeout/CRC/格式错误为零。
  这证明遥测短暂不新鲜，不证明下行风扇控制同时中断，也不证明新优化已实机生效。
- C6 已只读确认 RFN 兼容，但原统一 1 秒 ERROR_EVENT 日志节流可能丢掉文本，
  不能将 API 日志中的缺行解释为没有发送。本轮没有改 C6。
- 该阶段计划在用户单独授权后部署 TX/RX 优化版并进行同条件短时及长时对照，
  不能靠延长 C6 离线阈值验收。下方是各轮历史，不是当前刷机指令。

## 2026-09-07 历史：TX / RX Node 01 已写入 0.1.4-dev，运行未验收

- RX 重试已将 `0.1.4-dev` 的 `fanlink_rx_node01.bin` 从 `0x0` 直接写入，
  保持 115200、ROM `--no-stub`、`--before no-reset --after no-reset` 和三项 flash
  参数 `keep`。源镜像 289,200 bytes，SHA-256：
  `6866e610a7d1034d27f7f6d8310f2402d8b4971be8a8ac7fc718d2347317b303`。
- 日志确认 ROM 补齐后写入 289,792 bytes，耗时 30.0 秒，exit 0，最后为
  `Staying in bootloader.`；日志：`build/tx-flash-rg35xx.YsEdGp/rx01-direct-rom-retry.log`。
- 本次不备份、不读回、不发送 run/reset；TX 沿用上一轮成功写入，本次未动，
  C6 与 RG35XX 固件未改。写入成功不代表读回、启动、RF/PWM 或长期稳定性验收。
- 串口已释放（`UART_RELEASED`）。当前下一步仅是用户解除 RX BOOT 后正常上电，
  观察启动与链路；下方为逐轮历史。

## 2026-09-07 RX 首次直接写入尝试（历史）：连接失败，未擦写

- 用户接入 RX 并说明已进入 BOOT；沿用不备份、不读回、不自动复位的要求，
  直接尝试写入已构建的 `fanlink_rx_node01.bin`（`0.1.4-dev`）。
- 下载握手返回 `No serial data received`，exit 2；没有开始擦除或写入。
  SSH/USB 节点仍正常。当时等待用户确认 RX 的 BOOT 上电状态和串口接线；
  后续重试成功结果见上方当前状态。
- 本地失败日志：`build/tx-flash-rg35xx.YsEdGp/rx01-direct-rom-write.log`。
  TX 上一轮写入成功的状态不变；C6 未刷写。

## 2026-09-07 历史：0.1.4-dev TX 写入成功，运行未验收

- 用户重新使 TX 进入 BOOT，并明确要求不再读取、直接写入；已通过 RG35XX
  SSH/USB UART，以 esptool 5.0.2 `--no-stub` ROM 模式写入 `0.1.4-dev` TX 镜像。
- 源镜像 294,224 bytes，SHA-256 为
  `29835dd437a9db467b8e3e9de79b86ca74897b0dd3cb1fbb919bb5dd00976a6d`；
  地址 `0x0`，保留镜像 flash mode/frequency/size。ROM 块补齐后写入 294,912 bytes，
  耗时 30.7 秒，命令 exit 0；日志最后为 `Staying in bootloader.`。
- 本次直接写入没有重新备份、读取 Flash、独立执行 chip-id/flash-id，或发送
  run/reset 命令；没有读回哈希、应用启动或 RF 验收证据。RX、C6、RG35XX 固件未改。
- SSH 串口隧道已关闭、UART 已释放。当前下一步仅是用户解除 BOOT 后正常上电并
  观察 TX 启动与链路；不能把写入成功当作运行或长期稳定性通过。
- 详细记录与原始日志入口见 `docs/evidence/20260907-tx-ssh-flash.md`。

## 2026-09-07 TX 经 SSH 刷写准备（上一轮历史）

- 用户随后单独授权刷 TX；RX、C6 和 RG35XX 系统仍不在刷写范围内。
- 已确认 RG35XX Debug BSP V3 USB 串口接入 ESP8285H16，并成功备份完整 2 MiB Flash。
- 备份退出后 TX 不再回应下载握手；写入命令在连接阶段失败，未擦除或写入。
  当时等待用户保持 BOOT、仅给 TX 重新上电；后续直接写入结果见上方当前状态。
- 详细证据、备份 SHA 和待刷 TX 产物见 `docs/evidence/20260907-tx-ssh-flash.md`。

## 2026-09-07 可靠性修复阶段（刷写授权前）

- 当前 `0.1.4-dev` 修复 radio-only 自动恢复、启动失败退避、TX 原回包窗口内
  错帧后继续接收、重复旧包保护和射频前端错误清理；频率/功率/GPIO/两端 v1 格式不变。
- 生产 Host 为 FanLinkC6 ESPHome，每 200 ms 完整 SET_ALL_DUTY；PC 仅调试。
  C6 的 UART 超时/失败全速/恢复门槛/诊断要求已下发给用户指定的原任务；
  C6 源码和编译由该任务处理，本任务没有改动或 OTA C6。
- 本地 native 与 ASan/UBSan 各 507 checks、面板兼容 21 tests、实际 C6 协议
  交叉编解码 1277 checks、CMake/CTest 1/1、真实 RadioLib 模拟故障全部通过。
- 12 小时**虚拟**时钟：4,320,000 slots、micros 回绕 10 次、空口序号回绕 65 次、
  200 ms 全量 Host 帧 216,000、Node 01 accepted replies 269,986；在注入 I/O 故障及
  一次 begin 失败后自动恢复。另验证双端启动失败、静默 Timeout 和 Host 丢失恢复。
- TX/RX01/RX16 clean PlatformIO 3/3 通过，所有产物和日志 SHA-256 回读一致；
  `firmware/dist/20260907-radio-recovery/` 内有 BUILD_INFO、SHA256SUMS 和
  构建时 SOURCE_MANIFEST.build-input.sha256。后续只补充测试/文档/门禁，已核对
  所有实际固件编译输入仍与构建快照相同。
- 原始日志：`build/radio-recovery-20260907-{platformio,runtime,sanitized,ctest,verify}.log`；
  完整证据与限制见 `docs/evidence/20260907-radio-recovery.md`。
- 本轮源码未提交/推送；该修复阶段没有刷写、重启、改变生产 PWM 或进行
  RF/PWM/温度测量。后来单独授权的 TX 写入见上方当前状态。
  下方旧实机版本/下一步仅是历史记录，不是当前刷机指令；C6 任务回交另行跟进。

## 2026-09-07 源码入库

- Git 根目录为独立 FanLink 源码树；仓库为
  `https://github.com/atsrxL/FanLink`，分支为 `main`。
- `pc/` 包含 FanLink Python 串口控制台，`firmware/` 包含 TX/RX 固件与构建工具；
  早期 ELRS Node.js 控制台不纳入本仓库。根目录保留文档与统一校验入口。
- 纳入固件、控制面板、工具、测试、文档和现有 GitHub Actions；各层级 `.pio/`、
  `build/`、`dist/`、Python 缓存及外层历史压缩包/备份保留在本地。
- `make verify` 通过：native 269 项、面板 20 项、CLI、Python/JavaScript、
  firmware stub smoke 和 source manifest。
- 源码打包脚本已补充排除 `.git` 元数据；本次未修改固件行为。
- 迁移后 TX/RX01/RX16 真实依赖 clean 构建 3/3 通过，固件 SHA-256 回读一致；
  产物与构建时源码清单位于 `firmware/dist/20260907-git-layout/`。
  `SOURCE_MANIFEST.build-input.sha256` 保留构建输入，之后仅补充发布说明文档。
- 本轮日志：`build/git-publication-20260907-layout-verify.log` 与
  `build/git-publication-20260907-platformio.log`（仅本地保留）。
- 下方是 2026-09-04 的历史构建与实机交接快照。本次只完成源码入库相关工作，
  没有重新读取设备状态，不能据此判断目前实际刷入的固件。

## 2026-09-04 历史交接快照

最后更新：2026-09-04  
当前工作树：Unreleased / `0.1.3-dev`  
状态：所请求的软件修改、离线回归、sanitizer、CMake/CTest、源码门禁及真实依赖
TX/RX01/RX16 clean 构建均通过。新固件尚未刷写；当前实机仍运行 `0.1.2`。

## 本轮配置

- Air protocol 仍为 v1，Control/RPM Reply 均固定 30 bytes。
- Host protocol 仍为 v1；新增 `0x82 LINK_METRICS_SNAPSHOT`，payload 固定
  32 bytes，每节点依次为 RX→TX 上行 RSSI、TX→RX 下行 RSSI 两个 `int8`；
  `-127` 表示未知。原 `0x80` 102-byte 状态帧不变。
- JHEMCU 2.4 GHz Lite TX 保持 `+13 dBm`。该板是裸 SX1280 配置，约 20 mW，
  是 RadioLib/官方 target 上限，不能由固件真实提升到 25 mW。
- BETAFPV 2.4 GHz Nano RX 设为 `-6 dBm` PA drive，对应官方 25 mW 功率档。
- 仅 RX Node 01 使用 PWM GPIO3、Tach/Sensor GPIO1、低有效门极；Node 02-16
  仍为 PWM GPIO1、Tach GPIO3、默认门极极性。
- Host/空口逻辑不反转：`0=0%/停转请求`，`255=100%/全速请求`。Node 01
  平台层反相用于纠正当前走线。

## 当前源码验证

```text
native C++                              PASS: 269 checks
native ASan/UBSan                       PASS: 269 checks
Python CLI self-test and -O self-test  PASS
Python compile / JavaScript syntax      PASS
control-panel unittest                  PASS: 20 tests
firmware stub syntax smoke              PASS
CMake + CTest                           PASS: 1/1
source manifest / make verify           PASS
PlatformIO clean TX/RX01/RX16           PASS: 3/3
```

正式构建证据：
`dist/20260904-v0.1.3-25mw-node01-rssi-reviewed/`。其中 `BUILD_INFO.json`、
`SHA256SUMS`、各固件校验文件及完整日志是产物大小和 SHA-256 的唯一依据。

## 已刷 `0.1.2` 的实机证据

- TX Host 端口 `/dev/cu.usbmodem<device>`（公开记录已隐去设备唯一编号）可持续连接，Host watchdog 正常；
- RX GPIO16 链接状态为常亮，用户确认控制有效；
- 双向 RF Reply 已工作：实时快照曾显示 Node 01 requested/applied=`143`、
  RPM=`1749`、age=`0`、missed=`0`、stale=`false`；
- 另一观察中 requested/applied=`0` 但 RPM 约 `2787`，确认这块 Node 01 的物理
  0/255 方向相反，是本轮平台极性修正的依据；
- TX 诊断累计 accepted reply 持续增长。总 timeout 主要包含不存在的 Node 02-16，
  不能直接当作 Node 01 丢包率。

这些实机证据属于旧 `0.1.2`。它不证明新功率、Node 01 新脚位/极性或 `0x82`
双向 RSSI 已工作。

## 尚未验证与风险

1. `0.1.3-dev` TX/RX01 尚未刷写、启动和相互通信。
2. TX 不能达到用户期望的真实 25 mW；当前硬件安全上限是 `+13 dBm`，约 20 mW。
3. BETAFPV 的 25 mW 只是官方功率档配置，未用功率计验证天线口输出/EIRP。
4. Node 01 的 GPIO3 PWM、GPIO1 Tach、0/255 端点、25 kHz 波形和 failsafe
   必须在刷写后实测；Tach 高电平与转速标定仍缺仪器证据。
5. 新上下行 RSSI 数值、过期变未知、远距离表现、airtime/turnaround/PER 尚未实测。
6. GPIO16 搜索态“双闪+500 ms 停顿”尚未完整观察；只观察到链接常亮。
7. PCB revision、射频功率法规、精确 watchdog/Host 周期及长期热稳定仍未完成。

## 历史下一步唯一动作（2026-09-04，不作为当前指令）

让 BETAFPV RX Node 01 进入 ESP8266 ROM BOOT；随后刷入
`dist/20260904-v0.1.3-25mw-node01-rssi-reviewed/fanlink_rx_node01.bin`，重启后先
确认 LED、PWM 0/255 方向与 RPM，再刷 TX 并验收两列 RSSI。
