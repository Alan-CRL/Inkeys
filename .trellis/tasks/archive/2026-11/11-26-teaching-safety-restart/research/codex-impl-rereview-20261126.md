## FACTS

- **确认框在业务 worker 上。** `settingTabEnum::tab9` 的三个按钮经 `QueueConfirmCrashTest` 入队，由 `SettingBusinessQueue` 的 `jthread` 执行 `Show`。仅 `Ok` 且未取消才触发。`OkCancel` 接受 `defaultResult = Cancel`，自绘焦点也按该值设置。`Setting.cpp:1031–1039,1076–1100,1186–1218,1368–1412,1514–1519,5107,5167–5180`，`MessageBox.Layout.cpp:23–42`，`MessageBox.Window.cpp:1053–1055`
- **野指针路径执行写入。** `ConfirmCrashTest` 读取 `int* volatile wildPointer = nullptr` 后解引用写入。业务 worker 外层有 `catch (...)`，所读调用路径没有 `__try`。现有 ARM64 Debug、Release 编译记录对 `Setting.cpp` 均为 `/EHsc`。`Setting.cpp:1038,1076–1112,1204–1211`，`Inkeys/Cache/ARM64/Debug/Inkeys.tlog/CL.command.1.tlog:488`，`Inkeys/Cache/ARM64/Release/Inkeys.tlog/CL.command.1.tlog:546`
- **Draw3 的正常故障链已接通。** `InjectProductDebugFatal` 调 `InjectDebugFatalForTest`，置位后 `PublishControlWake`，可唤醒已阻塞的 `WaitDequeue`。`Run` 消费标志后抛 `std::runtime_error`，异常分支在历史一致时尝试 `captureGraphicsFatalExit`，随后重抛。Host 捕获并清理、发布 `running=false`，已启用看门狗时主循环调用 `RaiseNow(Draw3Stopped)`。`Draw3.Product.cpp:95–113`，`Draw3.Host.cpp:2941–2977,3321,3731–3736`，`Draw3.ContactInput.cpp:409–436,496–500,1210–1225`，`Draw3.DrawingController.cpp:25080–25088,27977–27981,28001–28031`，`IdtMain.cpp:4290–4307`
- **UI 卡死任务在调度线程上。** `PostControl` 入队并唤醒 `renderThread`，执行 `Sleep(20000)` 前对 `UiScheduler` 调用 `Beat`。运行期看门狗每 500 ms 检查，忙碌满 15 秒可判 `ThreadHang`。调试器、已受理退出或已有崩溃处理会抑制触发。`Setting.cpp:1214–1218`，`RenderPipeline.cpp:659–688,997–1008`，`Helper.Fatal.cpp:45–80`，`Helper.Fatal.h:83–86,129–139`，`IdtMain.cpp:4331–4341`

## INFERENCES

- `/EHsc` 下，业务 worker 的 C++ `catch (...)` 不应吞掉硬件访问违规。当前源码和编译记录支持 Windows UEF 路径，实际异常码和线程仍需从 dump 核实。`Setting.cpp:1093–1100,1206–1211`，`Inkeys/Cache/ARM64/Debug/Inkeys.tlog/CL.command.1.tlog:488`，`Helper.CrashHandler.cpp:131–141,321–336`
- Draw3 的通用唤醒可能在帧中已被消费。若注入发生在帧顶检查之后、`frameWakeGeneration` 捕获之前，该帧可排空 marker 后进入无期限橡皮等待。此时等待使用已包含该次唤醒的代次，注入可能一直等到下一次输入。`Draw3.DrawingController.cpp:21893–21904,25082–25088,25984–25986,26009–26044,27983–27989,6703–6710`，`Draw3.ContactInput.cpp:1357–1388`

## UNKNOWN / 需要运行测试

本轮只读检查，未改文件，未运行构建或测试。采用 Claude 提供的结果：修后 Debug ARM64 构建通过，`--fatal-entry-only` 8/8、`--shutdown-supervisor-tests` 32/32、`--crash-filter-lifecycle-test` 通过。Release ARM64 8/8 属于修前。八入口中卡死用例直接调用 `Beat`，子进程走静默档，并调整了 Debug CRT 报告模式。这些结果不足以验收三个真实按钮。`ShutdownSupervisor.cpp:1274–1283,1317–1355,1569–1586`，`IdtMain.cpp:541–587`

- 无调试器点按三按钮，确认取消、关闭或保持确认框打开超过 15 秒均不触发。确认后分别核对 `0xC0000005` 与业务 worker 栈、`Draw3Stopped`、约 15 秒后的 `ThreadHang`，再核对三档及重启后的墨迹恢复。Draw3 覆盖静止画布和活动橡皮转入空闲两种时序。`Setting.cpp:1186–1218`，`Draw3.DrawingController.cpp:27977–27989`，`Helper.CrashHandler.cpp:321–344`，`IdtMain.cpp:4295–4304`，`.trellis/tasks/11-26-debug-abort-button/prd.md:23–35`
- 注入提示线程创建失败、系统框尚未创建时卡住、超时与普通重启并发、双线程 `SIGABRT`；复测修后 Release ARM64 和 `-CrashTry` 五分钟抑制。GPU `Map`、重页重放和设备恢复还需测单次工作耗时。`Helper.CrashHandler.cpp:117–125,499–505,714–733`，`MessageBox.cpp:74–84`，`Helper.Fatal.cpp:97–101`，`.trellis/tasks/11-26-teaching-safety-restart/design.md:97–99,176–186`

## Findings

原项按 `design.md` §9 对齐。状态描述源码复核结果。`.trellis/tasks/11-26-teaching-safety-restart/design.md:220–237`

| 原项 / 状态 | 严重度 | 文件:行 | 问题 | 最小修复 |
|---|---|---|---|---|
| B3 提示截止 / **partially** | **blocker** | `Helper.CrashHandler.cpp:679–726`、`MessageBox.cpp:74–84`、`MessageBox.Window.cpp:1305–1312,1842–1849` | 自绘首帧会置位。但报告截止先撤销，事件或线程创建失败仍调用 `Show`，系统回退又在 `MessageBoxExW` 建窗前置位。两条路径都可能失去建窗保护。 | 截止线程确认就绪后才撤报告保护并进入 `Show`。建立失败按 `Failed` 处理。崩溃提示关闭无就绪回执的系统回退。 |
| B3 超时接管 / **partially** | **blocker** | `Helper.CrashHandler.cpp:117–125,725–740`、`IdtMain.cpp:346–360` | 截止线程 CAS 输了仍强退。反过来，截止线程赢后，`Show` 返回方可先结束进程。两者都可能杀在获胜方建立重启监督之前。 | 超时与提示返回共用单次接管。输家让已受理的退出 owner 完成；只有获胜方建立监督后终止。 |
| C1.6c Draw3 注入 | **blocker** | `Draw3.Host.cpp:3731–3736`、`Draw3.DrawingController.cpp:21956–21973,25085–25088,25984–25986,26009–26044,27983–27989` | 帧中排空通用 marker 后可能再次休眠，见 INFERENCES。 | 在 `processCommandAndReconcile` 消费通用唤醒时复用帧顶的注入检查。 |
| B1 清理 publisher / **fixed** | — | `IdtMain.cpp:410–431`、`FailedCleanupDeadline.cpp:91–121`、`Helper.CrashHandler.cpp:530–568` | 运行期重启分支只报告并读截止，原 owner 继续守绝对截止。`KnownFailureCleanup` 的提示档转静默。届时 helper 若尚未建立，原截止仍会强退。 | — |
| B2 崩溃接管 / **partially** | should-fix | `Helper.Fatal.cpp:129–159,207–214`、`Helper.CrashHandler.cpp:515–528` | 跨线程接管窗口已关闭。同线程递归的合成入口仍可返回到 `TerminateProcess`，抢在已建立的截止前结束。 | 仅在已确认有独立截止时让递归线程挂起，否则保留立即终止。 |
| B4 Draw3 心跳 / **fixed** | nit | `Draw3.DrawingController.cpp:21956–21982,25905–25917,25955–25979` | 批量消费及空闲分支执行命令前均 `Beat`。`Idle` 仍早于最终等待，提前覆盖了一段准备工作。 | 将 `Idle` 贴到两个实际等待分支。 |
| B5 快照与复验 / **partially** | should-fix | `Helper.Fatal.State.cpp:59–67,113–130`、`Helper.Fatal.cpp:71–80` | 双读首次时间阻挡旧首新末组合，二次采样可撤销已观察到的恢复。最后复验与 `RaiseNow` 之间仍有日志窗口。 | 日志移到最后复验之前，以最后一次复验作为判定点。 |
| CRT 顺序及退出 / **partially** | should-fix | `Helper.CrashHandler.cpp:132–141,163–173`、`Helper.Fatal.cpp:131–159`、`IdtMain.cpp:342–368` | 安装顺序已修，观察到 Close/Restart 会挂起。首次读取意图后若退出刚被受理，后续仍可进入 core 或直接强退。 | 合成入口进入 core 时及无 core 强退前复查普通退出意图。 |
| `Draw3Stopped` 日志 / **fixed** | — | `IdtMain.cpp:4297–4304`、`Log.h:128–134` | 已用 `TryFormat`。 | — |
| Settings / **fixed** | — | `Setting.cpp:207–220,5633–5646,5714–5722` | 隐藏清槽并停止累计，仅 `S_OK` 清成功。 | — |
| Draw3 ULW / **fixed** | — | `Draw3.DrawingController.cpp:18564–18583,22219–22249,25400–25413`、`Draw3.TransparentPresentation.cpp:2920–2924` | 三个帧外失败出口已计数，清成功要求提交计数推进。 | — |
| Whiteboard / Freeze / **fixed** | should-fix | `IdtFreezeFrame.cpp:54–70,85–94,149–158,183–196`、`Whiteboard.cpp:239–246,332–393,577–600` | 原 ULW 分类和 Freeze 退休计时已修。白板释放或隐藏时仍未清 `UiWhiteboard`，旧失败可跨快速切换延续。 | 在 `ReleaseInactiveBackgroundResources` 清该槽。 |
| Bar / Selection / **partially** | should-fix | `Bar.RenderLoop.cpp:12292–12307,12644–12652`、`SelectionOverlay.cpp:108–113`、`Bar.ShapeEditor.cpp:2297–2301`、`RenderPipeline.cpp:348–356` | Bar 已修。Selection 排除了原样 DXGI 设备丢失，却仍返回 `Retry`，没有触发共享恢复。 | 两个 Selection 失败出口复用设备丢失判断，命中返回 `DeviceLost`。 |
| PageControl / **partially** | should-fix | `PageControl.cpp:1451–1455,1495–1500,1545–1547,2453–2465,2732–2736,2757–2766,2800–2812` | 四槽和无效输入过滤已修。无像素的 `Success` 会清槽，隐藏回执提交和换代也缺直接清槽。 | 以 `pixelsSubmitted` 清成功，在隐藏回执及表面换代边界清对应槽。 |
| `Report` / 时钟 / **fixed** | nit | `Helper.Fatal.State.cpp:36–43,50–52,80–88` | `reason/detail` 成对发布，时钟使用同一基准。取时异常时仍会产生值 `1`。 | 若需覆盖取时异常，跳过该样本。 |
| 并发 `SIGABRT` / **partially** | should-fix | `Helper.Fatal.cpp:97–101,234–241` | 立即重装缩短了 CRT 复位窗口。进入回调前仍有间隙，且回调重装可晚于恢复旧 handler。 | 先加并发及撤销交错用例，复现后约束重装与撤销顺序。 |
| C1.6c 投递边界 | should-fix | `Setting.cpp:1204–1218,5168–5180`、`RenderPipeline.cpp:997–1008`、`Helper.Fatal.h:129–139`、`IdtMain.cpp:4295–4307,4331–4341` | 按钮没有就绪门禁，忽略 `PostControl` 拒绝。看门狗未启用或附加调试器时，卡死按钮仍可睡满 20 秒而无预期结果。 | 在执行前复查对应运行状态，并处理 `PostControl` 返回 `false`。 |