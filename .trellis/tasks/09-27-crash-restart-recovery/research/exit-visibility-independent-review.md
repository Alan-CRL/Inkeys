# F-027 退出可见性门最终独立复审

日期：2026-09-29。只读审当前 `Window.cpp/.cppm`、`IdtMain.cpp::SetOffSignal`、`Helper.CrashHandler.cpp` 的 UEF 意图分支、`window_tests.cpp` 的真实 Window Service 隐藏 HWND 用例及原始红绿日志。没有修改产品/测试，没有启动 GUI、构建或性能采样。依据 `exit-visibility-gate.md` 与当前工作树实际代码；下文状态覆盖该研究文件写成时的冻结结论。

## 已核对的合同与证据

- `SetOffSignal:256-277` 只接受 1/2，首次 `offSignalInterop` CAS 成功后立即调用 `Window::GetService().BeginShutdown()`，然后建立独立 15 秒监督、发布 `offSignal` 并请求两组 HideAll；重复请求不会重开显示门或监督器。`CloseProgram/RestartProgram` 还重复请求一次 HideAll，不改变 CAS 首意图语义。UEF `CrashHandler.cpp:361-387,520-524` 仅自动模式抢到意图后、或手动模式用户 OK 且抢赢意图后调用包含 `BeginShutdown` 的 `armCrashRestart`；手动确认前、取消、竞争失败及模式 2/3 不会错误关闭门。`GetService()` 返回进程级 `processService`，`BeginShutdown` 自身只做 atomic release-store，无 Window owner 锁/磁盘/GPU 操作。
- `Window::Service::Start:153-159` 在 gate 已关闭时拒绝重启同一服务，`ResetState` 不清 gate。owner `Execute:1405-1419` 在执行旧队列命令时拒绝 `Show/Create/SetWhiteboardWindowMode/RestoreWhiteboardGroup/非 Hidden 成对切换`；`Hide/Hidden/HideAll/Destroy` 保持可用。`CreateWithSpec:1016-1019`、`Show:1448-1497`、`SetDrawpadSurfaceVisibility:1510-1522` 及 `ApplyDrawpadSurfaceVisibility:1793-1835` 对执行中的显示事务再次核 gate，DWP 成功后的双画布也会成对撤下。白板恢复 `:1352-1384` 在入口、各步和末尾核 gate，样式回滚 `RestoreVisibility:1227-1253` 关闭后只隐藏。
- owner `HideUserWindowsInGroup:1693-1739` 对 overlay 组先核当前线程 capture 是否属于本组，再 `ReleaseCapture`，逐个 `ShowWindow(SW_HIDE)` 后以 `IsWindowVisible` 读回；失败不再一律报成功。这个合同覆盖 Drawpad、DrawpadPresentation、Freeze、放大镜、PPT 控件和 Bar；Setting 是另一 owner 的单窗隐藏分支。`RequestHideAllUserWindows` 向两个 owner 发命令，不能把排队成功当作桌面已实际隐藏。
- 红证：`f027-visibility-raw-red-build-debug-arm64.log` 记录完整 Debug|ARM64 Solution 构建 exit 0；以 gate 暂置 no-op 的定向隐藏 HWND 测试 exit 3，`f027-visibility-raw-red-debug-arm64.log` 原始值为晚到 Show `show=1 primaryVisible=1`、执行中 callback `applied=1 primaryVisible=1`。恢复 gate 后 `f027-visibility-raw-green-build-debug-arm64.log` 构建 exit 0，定向测试 `f027-visibility-raw-green-debug-arm64.log` exit 0，两个观测均为 0 且 `PASS exit visibility gate`；最终严格 `--no-window` 日志 `f027-visibility-raw-green-no-window-debug-arm64.log` exit 0。更早最终定向测试三轮 exit 0。定向测试使用自建的屏幕外 16×16 HWND 与真实 Service，`--no-window` 是另一套严格无窗回归，不应把二者混称。
- 全量有窗口 Headless 在 `f027-visibility-full-window-debug-arm64.log` exit 1：`message_box_test` 的 GDI 对象基线失败 1 项，日志出现于本单元 gate 用例之前。**这是已确认的全量回归失败、归因未确认**，不能记全套 PASS，也不能未经 H0 同配置对照将其归咎于 F-027。当前工作树文件 `git diff --check` 无空白错误。

## 独立发现与实际边界

| ID / 状态 | 证据、触发和影响 | 建议 |
| --- | --- | --- |
| F027-R1 **P2 条件风险：无等待 HideAll 对 owner 调用并非无等待** | `Window.cpp:1128-1148` 的 `SubmitNoWait` 在调用线程恰是该组 owner 时直接 `Execute(command)`；`RequestHideAllUserWindows:333-337` 先 overlay 后 Setting。若正式关闭入口在 overlay owner 上调用，第一步可同步进入多窗 `ShowWindow/ReleaseCapture/IsWindowVisible`，owner/Win32 回调卡住时第二组命令甚至尚未入队。15 秒监督已在 `SetOffSignal` 中先建立，因而进程最终退场仍有兜底；但“任何调用方都立即只排队、两组都可继续执行”的注释/验收合同不成立。现有测试从非 owner 调用，没有 owner 直接调用的故障注入。 | 单独加 owner 线程内调用 `RequestHideAllUserWindows` 的定向用例与卡住 callback 观察；若要求真正无等待，对 owner 也只发布其队列/唤醒事件，并验证无自锁及队列生命周期。失败不能降格成“已隐藏”。 |
| F027-R2 **P2 验收缺口：UEF 关门不撤下已可见窗口/capture** | `CrashHandler.cpp:365-385` 的获胜分支只调用 `BeginShutdown` 与 `ArmShutdownSupervisor`，没有安全地向 Window owner 发布 HideAll。gate 能阻止晚到显示，**不会隐藏此前已显示的双画布或释放已经持有的 capture**；dump/report 期间旧进程仍活着，已建立的监督至多在截止时终止旧进程，监督建立失败时不能承诺该时间。隔离真实 UEF 进程测试没有 HWND，F-027 隐藏 HWND 测试又只直接调用 gate，所以这条组合没有动态证据。`ApplyDrawpadSurfaceVisibility`/白板 in-flight 在最后一次 gate 检查之后到返回之间仍有窄竞态，正式 Close 的后续 HideAll 可兜住，UEF 没有该排队。 | 明确崩溃路径可安全使用的预分配/无阻塞 owner 隐藏通知合同并作隔离 GUI 故障注入；若异常上下文不能安全通知，保留“UEF 到旧进程退出前仍可能拦截输入”的发布门禁限制，不能用此 gate 的单测宣称画布已经消失。不能在 UEF 中等待 HWND owner 或业务锁。 |
| F027-R3 **未验证边界** | 定向测试只注入 Primary capture，未注入 DrawpadPresentation/Setting capture；`HideAll` 对 overlay capture 的代码覆盖二者，但无动态证据。`SetOffSignal` 在 gate 后先同步握手监督器，才发布 `offSignal`/HideAll，最慢握手可达其既有 2.5 秒等待；期间现有画布可能仍可见，SR-04 点击到隐藏延迟尚未量测。`HideAll` 若 owner 卡在 Win32/驱动调用，也不能保证 15 秒内物理隐藏，只有外部监督终止旧进程。 | 与正式 Close/Restart、自动/手动 UEF 分开测 owner 停顿、两画布 capture、点击→实际隐藏及旧进程结束时间；不把 headless 软件计时当作真机桌面输入恢复。 |

**结论：** 当前 gate 的晚到 Show/在途 callback/成对画布回撤，在定向隐藏 HWND 范围红→绿，未发现这些路径新引入的确定 P1。R1/R2 为仍需处置或明确发布边界的条件性 P2；全量有窗口回归另有一项确认失败且归因未定。正式 GUI 的桌面穿透、崩溃时 capture、Win7 SP1+仅 KB2670838、FLIP_SEQUENTIAL 及 DComp/ULW、FL11.0 HARDWARE/WARP 仍未由本审查证明。
