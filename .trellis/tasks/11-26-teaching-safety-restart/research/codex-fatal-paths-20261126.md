## FACTS（静态核实）

路径简写：`Helper/`、`UI/`、`Window/`、`Other/` 位于 `Inkeys/Inkeys/`，`Draw3/` 位于 `Inkeys/Inkeys/Drawing/`，`Idt*.cpp` 位于 `Inkeys/`。

### 现有崩溃重启

- **当前有一处 UI 与实现对调。** `TeachingSafetyMode` 默认 `0`。`0` 弹窗确认后重启，`1` 先抢占重启意图再生成报告，`2` 写报告后返回 `EXCEPTION_EXECUTE_HANDLER`，`3` 写报告后返回 `EXCEPTION_CONTINUE_SEARCH`。中文 UI 把 `2` 写成“交由系统崩溃过滤器处理”，把 `3` 写成“直接关闭软件”。本地报告截止为 15 秒，确认框打开前撤销该截止。`Other/Other.Config.cppm:486`，`Helper/Helper.CrashHandler.cpp:42–58,477–514,619–666`，`Helper/ShutdownSupervisor.cpp:2957–2962`，`Inkeys/src/i18n/zh-CN.jsonc:734–742`
- **意图与进程交接。** `SetOffSignal(1/2)` 用同一槽竞争 `Close`、`Restart` 或 `UpdateRestart`，受控退出会恢复原 SEH 过滤器。UEF 的 `TryClaimCrashRestartIntent` 将槽从 `0` 改为 `3`，关闭启动命令和窗口显示入口，建立 `Intent::CrashRestart` 监督。监督进程是同一 EXE，通过 `--inkeys-internal-shutdown-supervisor-v1`、原进程句柄、握手事件、截止及 `X` 启动。默认 15 秒，超时终止原进程，确认其句柄已 signaled 后才拉起一次。外部监督失败时，本地兜底只结束进程。`IdtMain.cpp:340–398`，`Helper/Helper.CrashHandler.cpp:479–502`，`Helper/ShutdownSupervisor.h:8–25`，`Helper/ShutdownSupervisor.cpp:37,454–490,520–524,615–693,2957–2962`
- **新进程能识别崩溃重启。** 参数为 `-CrashTry`，有租约时附 `--inkeys-startup-handoff=`，使用 v1 `ParentExit`。监督器持有安装目录占位，新进程 `Launch::Adopt()` 验真后等监督器退出并接管租约。无交接走常规单实例准入。`LaunchState::crashTry` 调用 `CrashHandler::IsSecond(true)`，五分钟内再次进入 UEF 直接退出，跳过报告、提示和重启。只有普通 `-Restart` 可在旧窗口隐藏后提前交接。`Helper/ShutdownSupervisor.cpp:590–601,615–693`，`Inkeys/Launch/IdtSingleInstance.h:86,127–160`，`IdtMain.cpp:2238–2308,3221`，`Helper/Helper.CrashHandler.cpp:122–126,463–470`
- **启动提示有两条路径。** UEF 在准入和目录检查之后安装，直到读取配置并调用 `SetFlag` 前使用默认 `0`。显式初始化错误经 `PublishFatalStartupFailure` 或主循环先请求 `Close`，再显示提示，独立于配置；更早的文件名、权限和准入错误可直接提示后返回。`IdtMain.cpp:167–180,222–242,2209–2210,2273–2354,3455–3457,4260–4268,4340–4354`，`Helper/Helper.CrashHandler.cpp:103–115`
- **设置。** 「常规 → 基础 → 安全与恢复」由 `RenderGeneralBasic` 渲染。标题键 `SettingsUI.Design.SafetyRecoverySection`，选项键 `SettingsUI.Regular.Tentative.SafeMode`。UI 用 `std::clamp(..., 0, 3)`，配置读取只接受可表示为 `int` 的 JSON 整数，越界或类型不符回 schema 默认 `0`，例如 `99` 读成 `0`。编辑后 `QueueConfigWrite()` 经 worker 调用 `config.Write()`，并立即 `CrashHandler::SetFlag()`。`UI/Setting/Setting.Pages.cpp:1377,1412–1417,1893`，`UI/Setting/Setting.cpp:3344–3350,3468–3472,1370–1374,1125–1127`，`Other/Other.Config.cpp:747–754,1100–1111`，`Inkeys/src/i18n/zh-CN.jsonc:370,734–742`

### 家族 1：异常终止入口

| 入口 | 当前接线与模式覆盖 |
|---|---|
| SEH 未处理异常 | `SetUnhandledExceptionFilter(UnhandledExceptionHandler)`；收到回调才执行上述四档。重入返回 `EXCEPTION_CONTINUE_SEARCH`，受控关闭恢复原过滤器。`Helper/Helper.CrashHandler.cpp:107–115,136–143,463–480,665–666` |
| `std::terminate`、worker 未捕获异常、`noexcept` 违约 | 未发现 `set_terminate`。明确调用点为 `PublishReservedCommandWake` 队列不变量失败和 `SettingSessionCoroutine::promise_type::unhandled_exception`。生产 Bar 交互线程直接调用 `Interact()`；Draw3 的 `Run()` 异常在 Host 截住。无独立模式接线。`Draw3/Draw3.ContactInput.cpp:450–462`，`UI/Setting/Setting.cpp:1519–1527`，`UI/Bar/Bar.Initialization.cpp:253`，`Draw3/Draw3.Host.cpp:2940–2959` |
| pure call、invalid parameter | `_set_purecall_handler(nullptr)`、`_set_invalid_parameter_handler(nullptr)`，没有 Inkeys CRT 回调。若最终抵达 UEF，才适用四档。`Helper/Helper.CrashHandler.cpp:107–115` |
| `abort` / `SIGABRT`、栈溢出、堆损坏、fail-fast | 未发现专用 signal 回调、栈保留或 fail-fast 接线。当前 UEF 在故障线程执行文件路径、分配及 dump 操作，源码无法保证这些故障的模式覆盖。`Helper/Helper.CrashHandler.cpp:107–115,463–514,519–578` |
| C# `PptCOM` | 进程内 CLR COM 类库，未发现全局托管异常回调。`PptComService` 捕获 `Exception` 后记录并清理；原生 `GetPptState` 捕获 `_com_error`，重置后继续轮询。这些已捕获路径不读崩溃模式。真正逃出 CLR 的进程级异常待验证。`PptCOM/PptCOM.manifest:7–13`，`PptCOM/PptCOM.cs:1503–1513,1547–1565`，`IdtPlug-in.cpp:594–646` |

### 家族 2：错误退出、卡死和静默降级

| 路径 | 当前结果 |
|---|---|
| Draw3 `Run()` OOM、其他异常、空文档创建失败、窗口意外销毁 | OOM 重抛且不排队保存；其他异常由 Host 捕获；GUID 或空页失败直接返回。`WM_DESTROY` 请求结束绘制，只有自有窗口投递 `PostQuitMessage`。Host 完成资源清理后才置 `running=false`，主循环发现后 `SetOffSignal(1)`、隐藏双画布、退出码 `6`。若先卡在清理，主循环尚看不到停机。`Draw3/Draw3.DrawingController.cpp:18763–18777,27955–27993`，`Draw3/Draw3.WindowControl.cpp:3535–3548`，`Draw3/Draw3.Host.cpp:2940–2977`，`IdtMain.cpp:4265–4290` |
| Draw3 设备或 history cache 恢复失败 | `RecoverFromRuntimeFailure()` 或 `historyGpuCache.Initialize()` 失败后尽力封存、`RequestExit()`、`break`，汇入上述关闭。单次 WndProc 异常记录后走默认处理；触控板与激光粒子有局部 OOM 降级。`Draw3/Draw3.DrawingController.cpp:25133–25182,26620–26635`，`Draw3/Draw3.Product.cpp:371–395`，`Draw3/Draw3.WindowControl.cpp:2975` |
| UI3 Bar 与共享渲染 | Bar 资源创建失败保留脏区，返回 `Continue` 映射为 `Retry`，设备丢失则交共享管线。`RecoverDevice` 失败持续重试且该批不执行客户端；回调的 `catch (...)` 也转为 `Retry`。这些路径没有终止预算。调度器还支持 `FrameResult::Stop` 结束线程，Bar 自己的停止结果映射为 `Idle`。`UI/Bar/Bar.RenderLoop.cpp:9327–9371,12281–12286,13281–13285`，`UI/RenderPipeline/RenderPipeline.cpp:254–272,346–364,779–811,856–861,878–922` |
| Setting、白板、分页、选区和启动预览 | Setting 资源、字体、交换链错误归为 `Retry` 或 `DeviceLost`。白板背景、分页、选区、预览也有持续失败后续帧的出口，可只坏一个可见表面，也可触发共享复建。`UI/Setting/Setting.cpp:209–225,5716–5739,5770–5774,5820–5842`，`UI/Whiteboard/Whiteboard.cpp:431–439,583–596`，`UI/PageControl/PageControl.cpp:2598–2615,2769–2773`，`UI/SelectionOverlay/SelectionOverlay.cpp:105–113`，`UI/StartupPreview/StartupPreview.cpp:704–769` |
| Freeze 与白板工作区切换 | Freeze 运行时 DIB 分配失败每 20 ms 再试，部分 `submitSurface` 结果丢弃。工作区切换已有停滞跟踪和 `FailWorkspaceRequest`，达到预算后停止自动尝试、发布局部失败。`IdtFreezeFrame.cpp:171–195,227–248`，`IdtState.cpp:312–321,474–506,4788–4897`，`UI/Setting/Setting.cpp:5907` |
| 原生 owner 和输入线程 | `Window::RunGroup` 的 `WAIT_FAILED` 跳出外层，`WM_QUIT` 仅跳出内层取消息循环。键鼠 hook 的等待错误或 `WM_QUIT` 结束各自线程，启动失败记录功能降级。`Window/Window.cpp:1271–1305,1332–1377`，`Inkeys/Inkeys/Input/KeyboardShortcuts.cpp:189–211`，`Inkeys/Inkeys/Input/MouseHook.cpp:76–108`，`UI/Bar/Bar.Initialization.cpp:235–243` |
| 同步启动错误 | DPI、共享管线、COM、PptCOM、字体、窗口服务、Draw3 Host、Setting、Whiteboard、Draw3 首帧、窗口层级、选区初始化失败走启动提示和 `Close`。Draw3 启动有重建窗口后再试 ULW 的回退。`IdtMain.cpp:1444–1511,3083–3097,3247–3251,3531–3594,3625–3678,3987–3993,4118–4214` |
| 异步启动错误与等待 | Bar `B001–B004`、Freeze `F001–F003`、PPT UI `F101`、`TopWindow` 首帧约 20 秒超时 `F201` 进入启动失败汇合点。无 tracker 的历史分支可用 `-WarnTry`。更早的窗口 future、Draw3 初始化条件变量、Setting 初始化条件变量没有超时参数。`UI/StartupPreview/StartupPreview.cpp:1016–1031`，`IdtFreezeFrame.cpp:101–114,149–163`，`IdtPlug-in.cpp:1608–1618`，`Window/Window.Legacy.cpp:98–148`，`IdtMain.cpp:4260–4268,4340–4354`，`Window/Window.cpp:283–301`，`Draw3/Draw3.Host.cpp:3012,3143`，`UI/Setting/Setting.cpp:5947–5963` |
| 强退及旁路 | `FailedCleanupDeadline` 覆盖已知失败后的清理、RTS 停止无法证明及监督准备失败，生产 publisher 抢 `Close`，随后按截止 `TerminateProcess`。另有 UEF 报告超时、关闭监督及其本地兜底。提权 helper 在 `RevertToSelf()` 失败时直接自杀；交接失败可杀替代进程。产品主路径未发现 `ExitProcess`、`_exit`、`quick_exit` 调用。检出的 `PostQuitMessage` 属窗口销毁或局部消息泵退场。`Helper/FailedCleanupDeadline.cpp:45–46,60–121,185–255`，`Draw3/Draw3.RealtimeStylus.cpp:2980–2999`，`IdtMain.cpp:408–421`，`Helper/Helper.CrashHandler.cpp:48–58`，`Helper/ShutdownSupervisor.cpp:147–157,642–693,2937–2953`，`Inkeys/SuperTop/IdtToken.h:305–318`，`UI/MessageBox/MessageBox.Window.cpp:678,1386,1572`，`UI/StartupPreview/StartupPreview.cpp:169` |

**现有检测边界。** UI3 实际渲染在共享 `Scheduler`，Bar 初始化线程注册回调后等待关闭。现有主循环只查 Draw3 是否已停；`StatusGuard` 是生存布尔值，键鼠 heartbeat 用于输入路由新鲜度。没有找到 UI3/Draw3 运行期进展 watchdog。`UI/Bar/Bar.Initialization.cpp:244–257`，`UI/RenderPipeline/RenderPipeline.cpp:657,713–741`，`IdtMain.cpp:4260–4268,4385–4395`，`Helper/Helper.Thread.cppm:15–42`，`Inkeys/Inkeys/Input/KeyboardShortcuts.Transport.h:285`，`Inkeys/Inkeys/Input/MouseHook.Transport.h:154–158`

## INFERENCES（最小接入点）

| 接入点 | 建议 |
|---|---|
| 崩溃策略与强退 | 共用现有意图槽与 `CrashRestart` 监督，保留启动错误提示。运行期 Draw3 在两个设备恢复失败出口及 Host 的 `Run()` 异常/提前返回处先发布故障，再让主循环消费；`WAIT_FAILED` 可在 owner 退出前发布。已知失败清理应在 `BeginKnownFailure` 附近预备接力，保留 `PublishFatalFailedCleanupNoWait` 的无等待约束。`Helper/ShutdownSupervisor.cpp:2957–2962`，`IdtMain.cpp:408–421,4265–4268`，`Draw3/Draw3.DrawingController.cpp:25146–25182`，`Draw3/Draw3.Host.cpp:2940–2974`，`Window/Window.cpp:1351–1354` |
| 重试与卡死 | 在共享复建失败和客户端持续出错处计故障时长，成功提交清零。Bar、Setting、白板、分页、选区应以**可见且有待提交工作**为前提；Freeze 用 `Ensure`/提交结果。用主循环监督运行期进展，UI3 在批次或回调边界发布忙闲及进展，Draw3 在循环及等待前后发布。启动握手卡死需从等待之前计时，主循环尚未运行。`UI/RenderPipeline/RenderPipeline.cpp:713–741,779–861,898–913`，`UI/Bar/Bar.RenderLoop.cpp:9350–9371`，`IdtFreezeFrame.cpp:171–195`，`Draw3/Draw3.DrawingController.cpp:25057,27872`，`IdtMain.cpp:4260–4268` |
| 设置与墨迹 | 三档用 `0/1/2`，同步收紧读取与 UI 范围；按给定的旧配置策略沿用 `config.Write()`。默认开启的新开关应与 `-CrashTry` 一起在 Draw3 桌面初页装载处判断。当前 `Run()` 创建空桌面页，桌面 `.uink` 走异步发布，现有 `Load` 用于 Clear 后冷 Undo；先明确恢复范围，再接入已成功发布的快照。OOM 和强退可能拿不到本次最新墨迹。`Other/Other.Config.cpp:747–754`，`UI/Setting/Setting.cpp:3350,3468–3472`，`IdtMain.cpp:2251,4047–4053`，`Draw3/Draw3.DrawingController.cpp:18763–18777,24568–24577,27955–27960`，`Draw3/Draw3.DesktopInkSnapshots.cpp:93–101,395–410,675–697` |
| 循环与双实例 | 所有新入口复用五分钟抑制和单次意图竞争。保留 `X` 等旧进程确实死亡的契约，避免两代同时持有窗口或写墨迹。对可能绕过进程内回调的退出，要兑现自动重启仍需外部存活观察；当前 helper 在收到意图后才创建。`Helper/Helper.CrashHandler.cpp:465–488`，`Helper/ShutdownSupervisor.cpp:454–480,615–693,2957–2962` |

## UNKNOWN / 需要运行验证

- **回调可达性**：分别注入 worker `terminate`、pure call、invalid parameter、`abort`、栈溢出、堆损坏、fail-fast、真正未处理的 CLR 异常。记录 UEF 是否命中及监督能否建立，尤其是低栈和低提交额度。`Helper/Helper.CrashHandler.cpp:107–115,479–514`，`Helper/ShutdownSupervisor.cpp:432–480`
- **卡死与恢复**：区分持续 `Retry`、GPU/COM 调用卡住、owner 已死和正常空闲；验证启动前后期限、旧进程迟迟未死亡、重启即失败、恢复开关关闭及快照损坏。现有代码可定位这些边界，现场复现和验证仍待执行。`UI/RenderPipeline/RenderPipeline.cpp:713–741,779–861`，`Helper/ShutdownSupervisor.cpp:674–693`，`Draw3/Draw3.DesktopInkSnapshots.cpp:413–467`

本次仅静态读取，未更改文件或运行测试。