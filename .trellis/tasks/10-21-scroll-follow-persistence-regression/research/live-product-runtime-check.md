# 最小真实产品整链：独立运行证据复核（2026-10-22）

**有限案例通过：空白Desktop页，通过真实Business启动、原Mag正式取帧、真实Tracker和Host应用，一次系统滚轮使专用内容上移16px，Viewport变为(0,+16)，正常Off保留最终位置并恢复原主窗。** 本报告只覆盖这一例，不包含有墨迹Presentation或真实笔输入。checker只离线读日志/已验证PNG，没有运行GUI、输入、截图、build或修改source；core正在独立扩有墨迹案例，不能把本报告套用到它的新版本。

## 原始运行和产物关联

当前任务 `.trellis/tasks/10-21-scroll-follow-persistence-regression`，基线HEAD10abcac3；任务继续in_progress，无commit/push/finish/archive。已按任务真实规范、前轮离线安全check、初始化/cleanup调查复核当前有限入口和最终断言。

原始文件：

- `Build/ScrollFollow/live-product-ui-ready-mta-unlocked-test.json`：PID **53500**，**exit0 / 14081ms**，stdout10295B、stderr11741B。
- stdout首行确定真实root为 **`Build/ScrollFollow/product-live-53500-213622515`**，不是其它run的tick。
- 该目录result.log3875B为夹具自身同步facts/GT/cleanup；stdout另含Host诊断，stderr含原Business/Tracker和WindowService消息，三者不是互相完整副本。
- `binary-after-run-proof.json` 保存了运行后、ink源码/build前对同路径EXE的文件核验：SHA256 **84041939f4bec23bbccb4840d312354c6efb0a81df7905c2cee84d34633ae9b2**，capturedUtc2026-10-22T10:47:23.0125453Z，lastWriteUtc10:40:42.5210434Z，buildRound=product-live-ui-ready-mta，关联本次run manifest。此记录由主会话在新build前保存，与core起跑前记录相符；**是文件hash关联，不是进程内存hash**，不拿后续current binary冒称此产物。

完整ARM64构建原日志 `motion-tracker-build-product-live-ui-ready-mta.log` 明确Build succeeded/0 Error，MSBuild内计时24.93s；仅原hashlib++3处C4267在两个TU各出现一次。wrapper外部耗时另按core记录，不混用内部计时。checker本轮不重跑build/CLI。

## 对应源码修正和证据层级

运行对应核心最小修复：

- `Business/ScrollFollow.cpp` SHA256 **ffe8124961a426d5d505d8c09219a7a2e61dbb5fcd8ea3e14e964299d87a77dc**。RefreshUi从裸UpdateRendering改用既有 `UI::Bar::RefreshToolState`；真实contentStateUpdatesReady=false时只通知/唤醒，真Bar初始化完成才完整状态/粗细更新。该入口保护Initializing/Stopping/Off早期生命周期，不fake wordMap或ready，不修改ACK。
- `Draw3.ProductLiveTest.inl` SHA256 **25725365c4da9e325ca11b906458d31dbb884b9d9b7f0e100f4d191ffa8bce93**。主线程MTA与原Main一致，保留之前GT/真实cursor/self-click及Down/Up归属、安全层立即显示撤销、最终Off GT断言。此前两次只读复核已确认该版相对35c690...仅STA→MTA。
- 没有Business TestPorts、人工Capture帧、手工Follow Target/Handoff或ACK setter。实际Tick/Frame来自原Service worker，截图来自原共享Mag pair，真实StateMonitoring负责投影/bridge发布和单例WindowService成对显隐。

历史PID72368的Initializing停滞及c0000005仍单独保留。原早期Bar空文字危险链是代码确证，MTA也忠实修正了夹具强迫RTS失败的问题；本次修复后顺利到Tracking/Off并完整退出，**不等于已取得历史AV精确调用栈或证明唯一根因**。WER只有ntdll偏移，没有dump，详cleanup调查，不将本PASS改写历史证据。

## 真实输入、捕获、追踪和窗口链

### 1. 启动与准备

本次main TID **77516**、WindowService原owner **83444**、Draw3 owner **25152**。原Mag host/child实际创建，source不在5项原角色排除列表；主屏2880×1920，原Mag裁切合同未改。没有WGC或禁止捕获属性。

source初始世界offset=(256,512)，初次paint hash=04f97d148251701f。before-product 99/99屏幕点及逐像素专用ROI通过；单次tagged OS自建source激活后，actual_cursor_hit_own和foreground_own都确认，LeftUp第一次即被OS接受。此单击在StartProduct之前，不是笔输入证据。

RTS日志实际为 `profile=Extended stage=ready hr=0 initialized=1`、MultiTouchEnabled=true、input_mode=RtsExtended/1。与旧STA版RPC_E_CHANGED_MODE不同；但本例没有画任何笔划，不能把producer初始化成功写成真实触控/笔接触已测。

Pen-ready facts：Business Off，Capture ready1/session0，真实primary输出revision2就绪、ACK1、visible=(1,0)、contentRevision0。before-toggle源GT也通过，证明本次空主窗透明结果没有污染专用内容。

### 2. 初始化与真实Tracking ACK

真实RequestToggle后日志按Initializing→Tracking推进。F0由正式Capture session1取得，原Tracker Initialize；StateMonitoring消费真实projection3/selection输出revision3，再由原WindowService应用hidden pair。

Tracking facts记录：Business2/Tracking session1，Capture session1，accepted3/rejected0/API0，projection3、actual_ack1、selection1/content0/full_clean1、requested=ready Selection输出revision3、visible=(0,0)。**空页两窗隐藏是生产selection规则，不能解释成有墨迹辅助窗已显示。**

stderr的frame2/3显示accepted0/NotInitialized，含义是Business在初始化完成后等待投影ACK，context.tracking尚false，所以没有调用Tracker::Process，输出的是默认MotionResult；Capture此时有效accepted已增长。源码Frame的分支和原始facts一致，不能把这两行算捕获黑帧或视觉连续3拒绝。

### 3. 一次真实系统滚轮和正确二维符号

输入前再次专用屏幕GT，安全/取消/失败、source foreground、实际cursor命中均经过原门。唯一滚轮日志：

`source_wheel message=522 tagged=1 delta=-120 offset=(256,528) screen=(1440,960)`

source实际收到WM_MOUSEWHEEL并把世界viewport Y增加16，随后paint sequence2/hash754b966c2eda7767，屏幕图案向上移动16px。GT只用于验收，没有写给Tracker或Follow。

frame18首先测到total≈(0,-15.9999)，后续稳定为日志四位小数(0,-16.0000)。owner按base-total应用，所以viewport=(0,+16)。motion frame18 confidence0.9976、inliers310，后续约316内点并趋于高可信；这只是专用高纹理短序列，不能当任意页面的可靠性承诺。

wheel-settled facts：Capture accepted30/rejected0/API0，latest30/applied30，total=(0,-16)、base=(0,0)、viewport=(0,16)、clamp0，Business仍Tracking、ACK1、空页pair(0,0)。代码要求两个不同生产seq均在GT误差2px内且已Applied，并至少新增3个真实Capture样本，不是仅读一个旧viewport后宣布成功。

### 4. 真实Off和最终位置

原RequestStop→Stopping→原present/reverse ACK→Off，未手工Handoff。原WindowService日志实际恢复primary。

| 阶段 | Business | Capture有效帧 | total | Viewport | 输出/就绪revision | projection/ACK | 实际pair |
|---|---|---:|---|---|---|---|---|
| pen-ready | Off | 0 | (0,0) | (0,0) | Primary2/2 | 0/1 | (1,0) |
| Tracking就绪 | Tracking | 3 | (0,0) | (0,0) | Selection3/3 | 3/1 | (0,0) |
| wheel稳定 | Tracking | 30 | (0,-16) | (0,16) | Selection3/3 | 3/1 | (0,0) |
| Off完成 | Off | 30 | (0,-16) | (0,16) | Primary4/4 | 4/1 | (1,0) |

最终事实为phase5/Complete、latest30/applied30、clamp0、contentRevision0、工具仍Pen；代码还要求real handoff、final.sequence已Applied、原工具revision不变、source_delta精确(0,16)，以及最终viewportX=baseX、Y=baseY+16且误差<=2。off之后又通过屏幕GT。

日志viewport和total格式仅保留四位小数，**不能以打印16.0000宣称浮点误差数学为零**。全精度运行断言是2px容差，离线事实精确符合已打印值。bounded_case报告end_frames27/recovery_frames0，它们是引擎计数，不是27张丢帧或27ms；空页未验证带墨迹Tile露出恢复。

## 清理实际返回

本次不是watchdog或异常退出；外层exit0，最终cleanup行：

- state_tid69144，state_returned1/state_failed0。
- mag_tid33728，mag_returned1/mag_failed0。
- product_running0/service_running0/mag_ready0/capture_occupied0。
- own_left_held0/log_failed0/result0。

原stderr在Off后有capture-drained事件和双窗destroying/destroyed，最终句柄记录为空。代码在写这行之前已真正join StateMonitoring、StopProduct/原保存workers drain、原Mag worker join、WindowService owner StopAndJoin，再销毁source/DIB、Display及COM，之后才恢复logger和关闭log，f的借用context没有提前释放。

这些证明本次列出的线程和产品停止状态正常返回，**不是GDI/USER零泄漏、所有COM引用逐个清零或MagUninitialize返回值的直接逐API证据**。Product入口没有启用旧printWindowTest的Mag API逐条跟踪，因此不能套用前次Capture的MagUninitialize=TRUE行冒充本次。没有调用强杀watchdog或留下ownLeftHeld。

本次没有文件读回断言，空页目录也没有墨迹UInk证据；StopProduct存在原保存屏障，不意味着本案例完成了Viewport文件验收。相关保存真实文件验收仍引用原四案例，不与本例混为一个整链。

## 离线PNG与原始数据核对

checker使用Pillow对专用保存ROI逐像素独立重算C++世界纹理（包含uint32溢出语义和L角点），并查看after-off本地图；没有访问桌面或其它窗口。三图均256×192、49,152像素、alpha255，预期公式的RGB最大误差 **0**。

- before-product ROI屏幕(64,64)，世界offset(256,512)。
- after-wheel和after-off ROI同屏幕坐标，世界offset(256,528)。
- after-wheel和after-off文件SHA完全相同，验证退出后专用底图仍在新位置。它们不含墨迹，不能证明可见墨迹跟随。

| PNG（本次私有root内） | SHA256 |
|---|---|
| source-before-product.png | 03d80a02f7731b0b7fdcbccb93301457a708f960a10c7201e5f0b8b08229c649 |
| source-after-wheel.png | 4eb817eef3ef901ae40f38c7f309873227c183e2bd18da5d48ab667310cad58c |
| source-after-off.png | 4eb817eef3ef901ae40f38c7f309873227c183e2bd18da5d48ab667310cad58c |

离线脚本断言留在 `Build/ScrollFollow/product-runtime-check-offline.json`：独立核最终fields、完整frame1–30连续存在、2/3初始化门、三图逐像素和日志hash。stdout有DiagnosticOutput dropped2/contention2，不能声称零诊断丢失；关键facts和cleanup由私有同步log直接写入且log_failed0，不受该队列丢条影响。

30个正式Backend::Read记录（包括初始化门期间有效Capture）min57.688 / mean81.746 / median78.228 / nearest-rank P95 112.189 / max116.377 ms。范围是几何检查/清DIB/PW(2)+GdiFlush/alpha整理，不是纯PW。没有Capture时间戳序列，不能用30/进程14.081s计算实际FPS，也不能说达20Hz；14.081s包含启动、RTS、初始化、动作及退出。

## Findings与最终边界

**本报告没有新增source修复。** 已核源码的早期UI readiness最小修复和MTA对应这次真实成功路径。未发现与空页有限PASS相矛盾的日志、最终位置或清理证据。

仍未覆盖：有墨迹Presentation可见和像素跟随、真实Pen/Touch接触、A/B编辑和这次viewport最终文件读回、较长/反向/水平/多屏relay、Pause失败场景及从暂停退出、Bar按钮/快捷键/设置/白板/PPT、Edge/Chrome/Word/PDF、显示/DPI/设备变化、旧Windows/WOW64和长期资源趋势。后续ink源码由core另行拥有，必须记录其新hash和新run，不套用本报告的840419...。

历史AV无dump仍是独立未定栈事件；这次未复现且成功清理，不能据此宣布唯一AV根因已证明或覆盖全部异常清理。

Verification：原完整ARM64build日志成功；checker本轮只离线static/日志/PNG断言pass，没有重跑build/GUI/CLI；未添加lint/type/test工具或改格式。父任务和完整功能不标完成，可进入后续有墨迹的必要实测阶段。

| 原始记录 | SHA256 |
|---|---|
| 私有root/result.log | 071e4b3ff167792766ea53766eed406b63ab1a6b5cd691b27bbdceb194ed45b4 |
| live-product-ui-ready-mta-unlocked-test.log | 1f418efad94e61d124a0c0a0d10756ef5ea89f2d85112fa8a315ebd1296b05f2 |
| live-product-ui-ready-mta-unlocked-test-stderr.log | eafde33e131604cb4c15c93961ed3ab9e09135e4284507fdccefebbcc5fe5be4 |
| live-product-ui-ready-mta-unlocked-test.json | f7ca0d8aae5cedb3d298d4e0a83343f52cc846b8450e15666d80ee9faae14d0c |
