# 共用定格 Mag 的正式取帧与验证边界

## 当前范围与授权

原独立 Capture-Only/runtime lease 方案已删除，不得恢复。正式入口为 `Inkeys::Graphics::FreezeCapture`，复用原定格 Mag、窗口对、初始化和 readyPromise。2026-10-21 用户批准全部后续实施；本轮测试只能无窗口或完全不在屏幕显示的 HWND，禁止 Computer Use、显示测试内容和推前台。必要可见验收时停止，不自动 commit/push。

上一轮 `--freeze-mag-printwindow-test` 已证明实际原 Mag 在不登记 callback 的冷启动、静止和变化图案中能由 PrintWindow(2) 取图。它是可见诊断，当前轮不能直接运行；它位于 HWND owner，不是当前 MagnifierThread 正式读回的实屏证据。

## 线程、运行库与停止

- WindowService Overlay STA owner 保留 `PrepareMagnifierWindow -> Host/Child Created -> readyPromise`，初始化及窗口创建在同一原 owner。不另建 Mag HWND，不增加 MagInitialize、全局 lease 或 owner RPC。
- 原 `MagnifierThread` 串行执行正式 Capture 的 Begin/Update/Read/End 和普通 Freeze 事务。Coordinator 只增 timed wait/external wake；监督线程只持短状态锁，不操作 HWND/DIB/Mag，不运行消费者。
- Capture Start 必须在原窗口就绪、Freeze 不 active 且原 Freeze revision 已处理、旧占用完全排空后才接受。Freeze 请求先撤销 Capture 发布，原 worker 在当前 Read/回调自然返回并 Drain 后执行 Freeze。
- Stop 只撤销后续发布并唤醒，不在 UI 等待同步 PrintWindow 或已准入的回调。回调捕获对象必须 shared lifetime 保活，DIB 借用持续到该回调返回。旧 Read/回调/End 尚未返回时保持 occupied，禁止重启。
- 正常退出保持 `StopMagnifierCoordinator -> join 原 worker -> WindowService StopAndJoin`，由原 owner MagUninitialize。同步 PW 永久不返回时不能声称 join 有界，不强杀线程或提前释放资源。
- 异常 Host 销毁/Shutdown 通过 DisableWindowAccess 关门，不等待 PW；inFlight 时跳过 MagUninitialize、标记 runtime abandoned 并禁止后续初始化/捕获，保留至进程退出。此窄接口不能证明拦截 Windows 已开始的 Child 销毁，系统边界须明确保留。
- `magnificationCreateReady` 跨 owner/worker 读取必须 atomic。监督 stop callback 需在同一短状态锁内通知条件变量，避免检查谓词与 wait 之间丢通知。

## 公共接口与帧寿命

`Graphics/FreezeCapture.h` 提供 `Start(FrameCallback) -> {status, session}`、`Stop(StopReason)`、`GetSnapshot()`。Session/Backend 是无 HWND 的状态调度测试边界，不是第二套产品采样者。

Frame 为 top-down BGRA/alpha255 的借用 span，宽高/stride、真实 primaryBounds/sourceRect/workArea/displayGeneration、sourcePixelsPerPixelX/Y、steady_clock timestamp、session/递增 sequence、质量与统计均按值。span 只在同步回调内有效，不能留存。timestamp 为 Read 返回时刻，不能当作 compositor 最新帧完成凭据。

Snapshot 公布 ready/publishing/occupied/inFlight、session/sequence、固定几何、reason、quality、attempts/accepted/rejected/apiFailures/skipped/lateResults 和读回耗时。Start/Stop/截图失败不能伪造 accepted 或 completed。

## 几何、更新与图像

Start 从同一个 Display snapshot 固定主屏和 workArea，读取原 child client；当前支持主屏原点(0,0)及原 source/client 少一像素布局，不悄悄改定格窗口尺寸。每次 backend 操作核对原 HWND、Display generation/几何、系统主屏与 client，更新后核对实际 MagGetWindowSource；变化即结束，不在会话内重选。

**source/client 宽比不是缩放因子。** 后端通过 MagGetWindowTransform 检查完整 identity 矩阵，其他矩阵拒绝；此时 Frame 映射明确1:1。少一像素是原裁切边界，不能据此乘 sourceWidth/clientWidth。Tracker 的降采样按真实处理尺寸还原一次；Draw3 当前输入/stored/viewport 都是 client/canvas px，不能再除96/DPI。

Begin 复用原 PrepareHidden：先 alpha=0，再保留可绘制生命周期，不采用最小化、SW_HIDE 或屏外替代。Update 复用原 BuildCurrentFilterList 的角色窗口排除、固定 Source、Invalidate/Redraw；不宣称已经枚举未知同进程所有顶层窗。Read 用原 child 的 PrintWindow(2)/GdiFlush，不设置禁止捕获属性，不依赖 callback。

Analyze 至多64x64分布采样；近黑为 maxRGB<=2 的比例>=.995，最大RGB方差<2为低信息。暗纹理可有效，同图静止可持续有效；API成功、hash不变/变化、质量统计均不能替代完整像素、排除或真实内容更新的验证。

## 调度、失败与消费

目标50ms/20Hz，首次 Update 后等待800ms，有限重试100ms，稳定更新至读回等待10ms。只有一个 DIB，没有 FIFO；慢调用跳过错过时隙。该目标不是实测帧率，固定等待/Redraw/DwmFlush 不能保证最新整帧已完成。

Start 起5秒内必须 accepted F0；此后1秒无有效 accepted 结果就撤销。无效或API失败不能续期限；Pump 在提交前也检查期限。后续服务应同时观察 rejected 和 apiFailures 新计数/quality：Begin/Update失败只增加 apiFailures，不能只看 Read rejected 或等待超时。严重无效暂停，迟到结果不发布。

## 可选 callback 与历史诊断

上轮按用户要求动态解析并实际尝试 MagSetImageScalingCallback/MagGetImageScalingCallback，保存/恢复原值，只用于同步候选，图像仍取 PrintWindow。回调只做直通与计数，不保存系统借用图像、不等待、不输出日志。登记失败或不触发需有界等待，不关闭DWM，不增加Win7图像导出。原API废弃及DWM限制不能当成跳过实测的理由；本机触发/返回也不能当作所有系统最新整帧完成保证。正式路径不登记此回调，无callback可用是必要要求。

## 验证门槛与兼容

当前正式实现通过无 HWND fake backend 测试、完整 ARM64 solution 及 `--no-window`，顺序 check 修复 atomic/监督通知问题。具体证据见捕获任务 `research/production-capture.md` 与 `production-capture-check.md`。纯离线 Tracker 可以继续，不能把假图、上一轮 owner 读回或通过构建称为正式跨线程实屏验证。

必要的正式路径真实变化像素/排除/持续帧率/长期资源、普通 Freeze 前后、桌面穿透和墨迹跟随仍须单独验收。旧 Windows、触控/笔、锁屏、浏览器/Word/PDF等未测项须分别列明，不标全功能通过。功能门只限Win8.1+、非WOW64且Mag能力有效，不能提高全程序Win7系统门槛或增加不可用API硬导入。

实际构建用完整 InkeysRepo.sln Debug|ARM64、vswhere 定位 ARM64 原生 MSBuild，同一 PowerShell 规范 Path/PATH并禁 node reuse。sandbox MSVC C1902先核对正常宿主，不改工程/PDB/依赖绕过。保持原BOM/EOL，验证工程XML、diff、物理编码，新增定向测试纳入 `--no-window`。

参考：[PrintWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-printwindow)、[MagSetWindowSource](https://learn.microsoft.com/en-us/windows/win32/api/magnification/nf-magnification-magsetwindowsource)、[MagSetWindowTransform](https://learn.microsoft.com/en-us/windows/win32/api/magnification/nf-magnification-magsetwindowtransform)、[MagSetImageScalingCallback](https://learn.microsoft.com/en-us/windows/win32/api/magnification/nf-magnification-magsetimagescalingcallback)。
