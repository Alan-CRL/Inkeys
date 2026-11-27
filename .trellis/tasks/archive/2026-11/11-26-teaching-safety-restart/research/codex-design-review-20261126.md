## FACTS（源码核实）

| # | 严重度 | 发现与证据 |
|---|---|---|
| 1 | blocker | **`/EHsc` 无法证明自定义 SEH 一定到达 UEF。** `Inkeys.vcxproj:89–110,175–408,1425–1426` 未显式设 `ExceptionHandling` 或 `/guard`，仍导入工具链及可选用户属性。仓库属性表的 `Sync` 仅作用于 Modeler 源文件（`ink_stroke_modeler.props:16–33`）。现存 Win32 Release 编译记录对主程序及过滤器显示 `/EHsc`，未见 `/EHa` 或 `/guard`（`Inkeys/Cache/Win32/Release/Inkeys.tlog/CL.command.1.tlog:154,168–172`）。自有产品代码未搜到 VEH 注册或 `__try/__except`。找到的 UEF 写入是生产安装/恢复、生命周期测试及隔离子进程（`Helper.CrashHandler.cpp:107–143`、`IdtMain.cpp:528–553`、`ShutdownSupervisor.cpp:1457–1472`）。主循环注入点在 C++ `catch` 外。现有真实 SEH 测试也刻意在 `catch` 外执行（`IdtMain.cpp:4260–4290`、`ShutdownSupervisor.cpp:3039–3044`）。 |
| 2 | should-fix | **Draw3 改走 UEF 会跳过受控清理。** 丢失成对隐藏、owner 释放捕获、状态读回及退出码 `6`，也跳过 `StopProduct()` 的触控板/鼠标停产、`stylus.Shutdown()`、保存排空，以及正常日志落盘（`IdtMain.cpp:4265–4290,4370–4375,4417–4421`、`Window.cpp:4620–4635`、`Draw3.Host.cpp:3197–3215,3236–3289`）。UEF 只关闭启动命令和窗口显示准入、建立监督、限时排日志。`CrashRestart` 的 `ParentExit` 交接确实等旧进程死亡，保留单实例契约（`Helper.CrashHandler.cpp:479–508,614–616`、`Window.cpp:328–335`、`ShutdownSupervisor.cpp:624–648,653–693`、`IdtSingleInstance.h:142–159`）。 |
| 3 | blocker | **忙碌时长与无进展时长不同。** UI3 即使连续完成回调也可永不进入空闲。`takeControl()` 在循环顶部、事件 reset 后及 wake 后执行，`RawMarkIdle()` 后仍有诊断处理。帧节拍虽设置了到期 timer，底层等待参数是 `INFINITE`（`RenderPipeline.cpp:681–695,713–755,898–913`、`Bar.FramePacing.cppm:390–406,592–596`）。Draw3 的无限等待见下表。主循环进入时异步启动线程已创建，仍可随后发布启动失败（`IdtMain.cpp:4222–4251,4260–4265,4340–4354`）。 |
| 4 | blocker | **`Retry` 混合延期、退避和故障。** 见下表。批次先将所有结果填成 `Idle`，遇 `DeviceLost` 或 `Stop` 可跳过后续回调。注册立即请求一帧。以批次结果清零可把未执行当成功，也可把尚未提交当成功（`RenderPipeline.cpp:812–838,856–879,898–913,943–951`）。Draw3 拟用的错误帧入口仅覆盖空 contact 或橡皮空闲，活动帧与前置恢复还会走其他分支。Freeze 有多处丢弃提交结果（`Draw3.DrawingController.cpp:25341–25378,27620–27624,27863–27881`、`IdtFreezeFrame.cpp:187–195,227–258`）。 |
| 5 | blocker | **模式 0 的降级晚于可阻塞步骤。** `CriticalNoWait` 只对弹窗准入 `try_lock`，自绘框仍同步等待，系统 fallback 默认开启并调用 `MessageBoxExW`。崩溃请求无 owner、无取消 token，默认关闭按钮返回 `Ok`。`Dismissed` 可由带 token 的请求取消产生，当前关闭竞争通过迟到的 CAS 决定。报告阶段超时会直接杀进程，构造提示字符串也在 `Show()` 之前（`MessageBox.cpp:46–82,184–193,277–298`、`MessageBox.cppm:89–95,106–124`、`MessageBox.Window.cpp:681–701,1771–1796,1927–1933`、`Helper.CrashHandler.cpp:48–58,519–553,619–657`）。 |
| 6 | should-fix | **意图槽适合监督已受理的退出。** `GetOffSignalInteropPointer()` 指向 `offSignalInterop`，`0` 未占、`1` Close、`2` Restart 或 UpdateRestart、`3` CrashRestart。显式启动失败和主循环异步失败都走 `SetOffSignal(1)`。更早的启动错误和启动阶段更新替换可直接返回，模式 0 等提示时槽也可仍为 `0`（`IdtMain.cpp:122,222–227,340–366,425–427,2206–2210,2829–2835,4260–4265`、`ShutdownSupervisor.cpp:2957–2962`）。 |
| 7 | should-fix | **隔离测试路径正确，CRT 生命周期缺覆盖。** `SuppressTestWer` 先安装，随后 `Initialize()` 覆盖它，再实际 `RaiseException`。现有生命周期测试校验重复初始化/关闭和空、非空原 UEF。`manual-uef-cancel-hold` 在弹窗前短路，断言零重启（`ShutdownSupervisor.cpp:1249–1252,1456–1472,1686–1696,2792`、`IdtMain.cpp:528–553`、`Helper.CrashHandler.cpp:107–143,619–626`）。 |
| 8 | blocker | **实施表漏掉已知失败强退。** PRD 要求接入，S4 未列。当前 `FailedCleanupDeadline` 仅在强退链调用 publisher，`PublishFatalFailedCleanupNoWait()` 抢 `Close`，随后走 `TerminateProcess`。普通关闭的保存排空已有 15 秒监督（`prd.md:31,35`、`implement.md:22–26`、`FailedCleanupDeadline.cpp:60–79,91–121,185–213,227–231`、`IdtMain.cpp:340–373,408–421`、`Draw3.Host.cpp:3259–3281`）。 |

**Draw3 `Run()` 无上限等待**

| 路径 | 证据 |
|---|---|
| 正常输入空闲 | Hold 期限计算失败时及普通空闲时，两处 `WaitDequeue(record)`。有输入后同一轮立即执行 `processCommandAndReconcile(record)`。橡皮无期限时，每秒等待循环可一直持续。`WaitForWake(..., 0)` 本身立即返回。`Draw3.DrawingController.cpp:25995` 之外的实际入口为 `25903–25905,25941–25942,27940–27950`，实现见 `Draw3.DrawingController.cpp:6679–6709`、`Draw3.ContactInput.cpp:1210–1225,1357–1388`。 |
| 保存及析构 | 正常/致命封存走白板两处 `Drain()`，PPT 经 `ObservePresentationDrain` 排空。`Run()` 内局部 `whiteboardStore` 析构调用 `Close()`，含 worker `join` 和待完成唤醒等待。`Draw3.DrawingController.cpp:2098–2117,22882,23048–23066,23183–23187,24501–24508`、`Draw3.Host.cpp:1794–1808`、`Draw3.PagedInkStore.cpp:1275–1278,1326–1356`。 |
| 驱动等待 | `CompleteUlwBeforeIdle`、`ServiceUlwBeforeFrameWait` 和 `BeginUlwFrame` 可走阻塞 `Map`，调用点无应用层期限。交换链 `PrepareFrameLatency` 则有超时。`Draw3.DrawingController.cpp:22206–22225,25343–25372`、`Draw3.TransparentPresentation.cpp:978–992,1745–1754,2806–2837`。 |

**非故障 `Retry`，含与故障共用的出口**

| 客户端 | 分支与证据 |
|---|---|
| Bar | 失败退避期间跳过本帧，等待原生直移退休，松手交接失效或延期，候选被新输入作废，保留表面越界后重准备，native lease/receipt 未就绪、形态或任务栏边界延期。`Bar.RenderLoop.cpp:9412–9424,12182–12211,12266–12286,12875–12883,13160–13178,13268–13285`。同一 stage 出口也上报真正的 `EndDraw`/source 错误。 |
| Setting | `DXGI_STATUS_OCCLUDED` 的探测续帧。`Setting.cpp:5823–5834`。 |
| PageControl | 等 pair move、未知 native 快照、`try_lock` 竞争、版本更新、已有失败的退避、等待 owner 接纳像素、native lease/版本变化导致 `presentResult.deferred`、隐藏时回滚拖动、候选发布被拒或已发待回执。`PageControl.cpp:1626–1636,2551–2566,2593–2605,2626–2632,2706–2712,2762–2780,2819–2824`、`Window.cpp:486–501`。 |
| Whiteboard | 失效的 inactive/绑定事务、未知 native 快照、发布及等回执、lease 竞争、显隐/请求身份变化、视口通道竞争。测试还可故意扣住回执。窗口换代可在 ULW 前后把 `presented` 置假，与 API 失败汇入同一出口。`Whiteboard.cpp:248–250,293–300,331–396,444–470,532–533,575–601,631–659`。 |
| StartupPreview / SelectionOverlay | Preview 的 owner 消失或尺寸变化可在取像素前后触发 `Retry`（`StartupPreview.cpp:313–327,728–734,746–769`）。SelectionOverlay 未发现专用延期 `Retry`，隐藏和无脏时走 `Idle`/`Continue`，但 `SetBounds`/`Show` 拒绝也落入通用失败出口（`SelectionOverlay.cpp:99–113`、`Window.cpp:2096–2117`）。 |

## INFERENCES（建议逐条改设计）

1. **§1、§3.5** 删除“一定到达”。主线程与独立 watchdog 是优先验证点，CRT 回调需单独验收。若任一入口收不到 UEF，让该入口走同一个崩溃处理核心与显式终止。保留系统 UEF 入口和原有意图仲裁。
2. **§3.4、§3.7** 保留“不先 `SetOffSignal(1)`”。交接只依赖旧进程死亡。显式记录会跳过隐藏、停产、正常排空及退出码 `6`。如需在模式 0 等待时撤下失效画布，只做有界的 owner 请求。恢复承诺改为“恢复已持久化的数据”，与 C1.6b 的会话记录及读旧写新规则对齐（C1.6b `prd.md:25–36`）。
3. **§3.1–3.2、§6** UI3 在真实 wait 紧邻处标闲，wake 后先标忙再取任务，在任务、回调和批次完成时刷新进展。Draw3 在实际输入等待前后切换忙闲，先恢复忙再处理 dequeued 命令。GPU、封存及析构保留忙碌监测。删除“冷页约 0.3 s”作为上限。读盘和导出在 worker，绘制线程仍有同步物化、重放和排空，UI3/Draw3 均同步重建设备。PPT COM 轮询另起线程（`Draw3.DrawingController.cpp:4508–4526,19212–19218,24808–24822`、`Draw3.PagedInkStore.cpp:813–819`、`RenderPipeline.cpp:208–241,254–272,779–783`、`Draw3.TransparentPresentation.cpp:2530–2535`、`IdtPlug-in.cpp:623,1621–1622`）。
4. **§3.3、S4** 保持调度返回语义，在客户端已有的失败/确认点发布计时状态。以可见、待提交和实际故障为条件，按注册代次清理，退避延续已有故障，等待回执或取消单独处理。Draw3 记实际光栅/呈现失败，Freeze 接齐提交结果。共享设备恢复单独计时，避免把同一故障重复归给客户端。
5. **§2** 保留提示返回后的 CAS，关闭/重启先赢则沿用该意图。把自动降级定义为“无法建立提示”，覆盖报告及提示构造失败，并明确已显示后允许等待用户的期限。若沿用报告超时直接退出的现状，把这一例外写入验收。
6. **§3.1** 用 `InterlockedCompareExchange` 读同一槽，触发前复查，最终仍由 CAS 决胜。把启用点绑定到启动门禁完成。模式 0 尚未占槽时，由崩溃处理中状态抑制重复触发。明确启动早退不受 watchdog 监督。
7. **§3.5、§5** 只在首次成功安装时保存各 CRT 旧值，处理 `signal` 安装失败及部分安装。先还原 CRT 回调，再恢复旧 UEF，回调入口仍检查退出竞争。保留真实 UEF 测试并扩展生命周期测试，另加真实 CRT 隔离用例。现有 hold 用例只证明等待超过 15 秒，不能替代实际 `Dismissed`/`Failed` 验收。
8. **§3.7、S4** 增列运行期已知失败入口，在 `BeginKnownFailure` 附近安排有界接力，给即时 producer-stop 失败写明策略。保留原强退截止及无等待 publisher。启动失败和已受理的用户退出继续遵守其原意图。

## UNKNOWN / 需要运行验证

- **#1、#7** 分别在 Debug/Release ARM64、无调试器的隔离子进程验证 watchdog、主线程、`SIGABRT`、worker `std::terminate`/`noexcept`、invalid parameter、purecall，记录 UEF 命中和实际拉起次数。C1.6c 三按钮不覆盖 CRT，也不能替代报告超时和关闭竞争测试（C1.6c `prd.md:12–16,23–30`、`ShutdownSupervisor.cpp:1456–1472,1686–1696`）。
- **#2–#5** 复测旧进程死亡后触控/笔输入及墨迹，重页物化、设备重建和保存排空的最坏耗时，以及模式 0 的弹窗准入失败、关闭竞态和低内存降级（`Draw3.Host.cpp:3197–3215,3274–3289`、`Draw3.DrawingController.cpp:4508–4538`、`RenderPipeline.cpp:779–811`、`Helper.CrashHandler.cpp:619–657`）。

本次仅静态读取，未改文件，未运行构建、进程或 GUI 测试。