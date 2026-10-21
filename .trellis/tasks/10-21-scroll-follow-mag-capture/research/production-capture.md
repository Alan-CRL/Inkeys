# 原 Freeze Mag 正式异步取帧入口

日期：2026-10-21。本轮用户授权继续正式取帧和后续产品阶段，但测试只允许无窗口或完全不在屏幕显示的 HWND。本阶段没有启动普通 Inkeys.exe、没有运行 `--freeze-mag-printwindow-test`、没有创建可见测试内容、没有 Computer Use、没有 commit/push。

## 本阶段结果和证据边界

已完成共用原 Mag 的正式异步入口、串行调度、质量判定和监督/取消合同，并通过两次完整 Debug|ARM64 solution 构建及定向无窗口测试。旧 `research/freeze-printwindow.md` 证明的是上一轮原 owner 上的真实截图诊断；本轮正式读回发生在既有 MagnifierThread，**尚未进行该跨线程路径的真实屏幕像素验收**，不能用旧诊断代替。测试中生成的纹理只是假 backend 的质量/状态输入，不是实际截图证据，也没有实测 20FPS。

生产/测试源码完成后已交主会话顺序检查；本报告描述实现阶段交接状态，后续审查修订及其验证应另行记录。

## 产品接口

`Inkeys/Inkeys/Graphics/FreezeCapture.h` 中的 `Inkeys::Graphics::FreezeCapture` 提供：

- `Start(FrameCallback) -> StartResult { status, session }`：只接管原窗口。尚未就绪、已有占用、普通 Freeze active/尚未处理完或无有效 Display 几何时拒绝。
- `Stop(StopReason)`：撤销后续发布并唤醒原协调线程；不等待同步 PrintWindow 或已经准入的消费者回调。
- `GetSnapshot()`：公开 ready/publishing/occupied/inFlight、session/sequence、固定几何、质量、统计和终止原因。`occupied=false` 才说明旧会话已经完成实际清理，可考虑再启。

Frame 借用同一个 top-down BGRA DIB，alpha 归一为 255，只在该次回调内有效。包含 width/height/strideBytes、primaryBounds/sourceRect/workArea/displayGeneration、sourcePixelsPerPixelX/Y、steady_clock 返回时间、session/sequence、质量与统计。消费者必须在回调内完成使用，或者自行复制到受管资源；Stop 不等待已准入回调，回调捕获的对象必须持续存活。回调异常被捕获并停止发布。

timestamp 是 backend Read 返回时刻，不能作为 compositor 最新帧的完成凭据。lastReadDuration/maxReadDuration 测量整个 backend Read（几何核对、PrintWindow/GdiFlush、alpha 处理等），不是只有 PrintWindow 的独立基准。

## 原线程与共享控件合同

保留 WindowService Overlay STA owner 的 `PrepareMagnifierWindow -> Host/Child Created -> readyPromise`；没有另一次 MagInitialize、runtime lease、第二套 Mag HWND、owner RPC 或新的采样线程。普通 Freeze 的事务步骤仍使用原 Win32MagnifierOperations。

唯一既有 MagnifierThread 同时串行执行普通 Freeze 和 Capture backend 的 Begin/Update/Read/End。Coordinator 仅新增 external Wake 和 timed Wait；无捕获时仍可无限事件等待，捕获 active 时按下一时隙唤醒。独立监督线程只读写短状态，不操作 HWND、DIB 或 Mag，不运行消费者。

Freeze 状态观察首先取消捕获发布，原协调线程等当前 Read/回调自然返回，Drain 旧占用，然后执行原 Freeze。新 Start 也检查原 Coordinator 的 visible/processed revision，防止旧 Freeze Conceal 尚未处理就接管。纯测试用阻塞假 Read 覆盖 Freeze 到来后不发布、旧 Read/真实 End 返回前不能运行普通 Freeze 的交错；实际 GUI 操作顺序仍待验收。

## 几何与图像

Start 从同一个 Display snapshot 固定主屏 bounds、workArea 和 generation，并读取原 child client。接受的原合同是 primary 原点 `(0,0)`、child 宽为主屏宽且高度少 1、source 宽高少 1。每次 backend 操作核对当前窗口对、Display generation/bounds/workArea、系统主屏尺寸和实际 client；更新后还核对 MagGetWindowSource。变化立即取消，不在活会话内重新映射。

**矩形宽比不是缩放因子。** Backend 实际调用 MagGetWindowTransform 并逐项要求 identity；其他矩阵拒绝为 GeometryChanged。Frame 映射比例固定 1:1，保留 source/client 少 1 像素的原始尺寸用于审计，不人为乘 `sourceWidth/clientWidth`，也不重复乘 DPI。纯测试覆盖 source 63/client 64 时 identity 仍保持 1:1。

Begin 复用原 PrepareHidden：先 alpha=0，再保留 Host/Child 可绘制生命周期。Update 复用 BuildCurrentFilterList（原 10 类角色、有效 HWND 过滤）、固定 source、Invalidate 和 Redraw。没有设置禁止捕获属性，没有改变普通定格高度/尺寸规则。Read 只用原 child 的 PrintWindow(2)/GdiFlush；正式路径不登记 scaling callback，因此没有 callback 也是完整可用设计，但本轮没有实屏运行来证明该生产线程上的像素结果。

Analyze 至多取 64x64 分布点，以黑像素比例和 RGB 方差拒绝近黑、纯色/低信息结果；暗纹理有效。API 成功不绕过质量判定。静止同图可以重复有效，没有 hash 不变即失效规则。质量检查本身不能证明完整像素正确、排除确实生效、内容最新或画面确实发生预期变化，这些仍需受控屏幕图案验收。

## 调度、失败和资源寿命

目标时隙为 50ms（20Hz）；第一轮 update 后等待 800ms，首次无效后有限重试采用 100ms，成功后 update 到 read 采用 10ms 等待。只有一个 DIB，无捕获 FIFO；慢调用跳过已经错过的绝对时隙。该策略是上限目标，不保证实际捕获帧率。

Start 起固定五秒内必须有 accepted F0。黑/低信息/API 拒绝不能刷新启动期限。取得 F0 后，只有有效 accepted 才推进 lastResult，连续一秒无有效结果就撤销发布。Pump 自身也检查期限，监督线程即使尚未被调度，迟到读回仍不能提交。Stop、Freeze 和超时均不会强杀线程或提前释放正在借用的 DIB。

后续 Lost 检测可以立即观察 `quality` 和统计变化，无须只等超时：

- 黑/低信息 Read：`rejected` 增加，quality 保留 NearBlack/LowInformation。
- Read API 失败：`apiFailures` 与 `rejected` 增加，quality=InvalidBuffer。
- Begin/Update API 失败：`apiFailures` 增加，quality=InvalidBuffer；Read attempts/rejected 不伪增。消费者需同时观察 apiFailures 新计数。
- 几何变化/窗口失效/消费者异常：停止发布并给出明确 reason。

正常退出保持原 `StopMagnifierCoordinator -> MagnifierThread join -> WindowService StopAndJoin`。DIB 和回调对象只在实际 Read/回调返回后排空，原 MagUninitialize 仍由原 owner 执行。若 PrintWindow 永远不返回，Stop 仍返回，但不能据此声称 join 或系统调用具备有界退出；没有新增强杀办法。

异常 Host WM_DESTROY 和 ShutdownMagnifierWindow 使用只关门的 DisableWindowAccess，原子禁止新 backend 操作。若仍有 inFlight，标记本进程 runtime abandoned，跳过 MagUninitialize、记录错误并禁止新初始化/捕获；DIB 保留到旧调用自然返回，后续 End 不再操作已销毁 HWND，runtime 保留至进程结束。没有阻塞 Overlay owner 等待 PW，也没有广泛 Window 生命周期框架。该窄接口没有拦截 Windows 已在执行的 HWND 销毁，不声称已证明异常系统销毁与阻塞 PW 的全部行为；这仍是审查和后续现场边界。

## 实施阶段验证

构建均通过 vswhere 定位当前 ARM64 原生 MSBuild，并用 require_escalated 正常宿主执行完整 `InkeysRepo.sln /m:1 /nr:false /p:BuildInParallel=false /p:Configuration=Debug /p:Platform=ARM64 /p:PreferredToolArchitecture=arm64`。同一 PowerShell 规范单一 Path，设置 MSBUILDDISABLENODEREUSE=1、TEMP/TMP=Build/ScrollFollow/TestTemp；异步等待，不以几十秒无输出作为超时。没有更改 SDK、依赖、PDB 或工程工具链。

| 验证 | 结果 | 原始证据（Build/ScrollFollow） |
| --- | --- | --- |
| 完整 solution 首次 | exit 0，36.00s，3 既有 hashlib++ C4267，0 error | production-capture-build-arm64.log / -console.log |
| 完整 solution 最终增量 | exit 0，20.72s，3 同类既有 warning，0 error | production-capture-build-arm64-final.log / -console.log |
| `InkeysHeadlessTests --freeze-capture-only` 最终 | exit 0，PASS；无 HWND，fake pixels | production-capture-only-final.log |
| `InkeysHeadlessTests --no-window` | exit 0，新增 capture PASS，末尾 PASS animation correctness | production-capture-headless.log |
| 工程 XML / diff / 编码 | 三份 XML 解析通过；实现文件 diff --check 通过；原 BOM/CRLF 保留（Coordinator 原无 BOM） | 实施调用输出；顺序检查另留结果 |

新增无窗口测试覆盖：几何非法/未知代、source 少 1 与 identity 映射、缓冲截断、黑/纯色/暗纹理、800ms 无 callback 首帧、同图静止、单 buffer、慢帧跳过、五秒首次 accepted 期限、一秒有效结果期限、API 拒绝即时统计、Freeze 版本/工作区/忙碌/清理后再启、阻塞 Read 下 Stop/迟到结果拒绝/保活、异常 window access 禁止释放、真实独立监督线程处理阻塞消费者、消费者异常、几何失效与清理失败。监督使用真实 steady_clock 的一秒期限，但 backend 仍是假实现。

未执行其他架构、Win7/8.1、真实主程序启动/退出、正式跨线程 Mag 像素/排除/持续帧率/长期资源增长、浏览器/Word/PDF/视频、实际笔触控或桌面操作。本阶段不接 Tracker/Draw3/Bar；完整滚动批注是否完成由父任务后续阶段和必要可见验收决定。
