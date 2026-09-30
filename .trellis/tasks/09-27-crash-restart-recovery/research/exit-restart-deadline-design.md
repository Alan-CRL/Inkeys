# 退出与主动重启的有限截止保护：历史证据与安全设计

> 后续用户决策（2026-09-28）：用户选择 15 秒**无条件优先结束**，允许到期时丢失已接受但未 durable 的请求；下文原“只在 storageDrained 后强杀”的安全默认分析保留为历史取舍说明，不再是实施策略。强杀仍必须保持此前已 durable 的 UInk/index 最后有效组合，因此 F-044 的 PPT 跨文件事务修补/故障注入是看门狗激活前置条件。正式实施以 `exit-restart-deadline-decision.md` 为准，不把 pending 请求的丢失写成保存成功。

日期：2026-09-28。只读设计；未改源码/规范/父账本，未构建或启动 GUI/测试进程。适用首发的正常关闭、用户/设置/更新主动重启和受控故障退出；UEF `-CrashTry` 是另一条崩溃链，不由本机制伪装或重复启动。

## 历史对象核对：未找到真正的“15 秒后强制进行”生产实现

| Git 对象 | 实际代码与时间 | 结论 |
| --- | --- | --- |
| `e5871f0d`（2024-05-15），旧 `智绘教/IdtMain.cpp` | 主线程在 `offSignal` 后以 `WaitingCount<20`、每次 500 ms 检查旧线程状态，约 10 秒后只打印“强制结束线程”；Release 另有对 `offSignalReady` 的无界 500 ms 等待。 | 既没有取消/结束那个卡住的线程，也没有给 COM/文件 I/O/join 设置真正硬截止。 |
| `94faedd1`（2024-07-03） | 把 Release 的 `offSignalReady` 等待改为同样 `20×500 ms`，超时日志称“强制退出”，然后继续函数尾部。 | 这是有界**轮询**与日志，不是 `TerminateProcess` 或真正阻止主线程随后卡住的保护；数值约 10 秒，并非 15 秒。 |
| `2fe05ebd`（2024-11-07 21:39） | 从旧 `IdtMain.cpp` 删除 `offSignalReady`/崩溃助手等待，删除 `IdtUpdate.cpp` 的文件心跳启动/循环；Restart 改为主清理后直接 `ShellExecute`。旧 `20×500 ms` 线程状态日志仍保留。 | 这是旧助手从运行调用链移除的关键提交。 |
| `ccacf0a2`（2024-11-07 22:03）与 `928e601a`（2024-06-19） | 前者删除 `智绘教CrashedHandler/main.cpp` 工程源码；后者更早删除 `智绘教CrashedHandlerClose/main.cpp`。旧助手通过 `open.txt` 文件轮询/同名进程枚举；Close 助手按可执行文件名寻找并 `TerminateProcess`，缺少精确进程句柄。 | 两个助手均不在当前发布主工程。不能恢复旧的文件心跳、按名称杀进程或无界 `MessageBox`；它们会误伤同名进程、引入路径/并发风险。 |

本次已查 H0 可用 `git log --all`、旧路径对象与 `.trellis` 记录；未在可用历史中定位“15 秒后真实强制关闭/重启”的对应 SHA。若用户指的是另一个 Inkeys2 源仓或未取得的历史 ref，不能将上表冒称其唯一来源。设计沿用户给出的 **15 秒新目标**，不照抄旧 10 秒注释。

## 当前受控退出链及卡住位置

1. `CloseProgram/RestartProgram` 位于 `Inkeys/Inkeys/Helper/Helper.CrashHandler.cppm:57-70`。Setting 业务按钮/重置、主栏关闭确认、强制更新分别调用它们（`Setting.cpp:248-255,1570-1589`、`Bar.Interaction.cpp:3344`、`Net.Update.cpp:639-657`）。二者现在**先同步** `Window::Service::HideAllUserWindows()`，再 `CrashHandler::Shutdown()`、`SetOffSignal(1/2)`；`Window.cpp:1121-1142` 的跨线程 `Submit` 用无界 `future.get()`，所以窗口 owner 卡住时可能连 offSignal 都发布不了。
2. `SetOffSignal` (`IdtMain.cpp:255-264`) 先在 CrashHandler 过滤器锁内 Shutdown，随后 `InterlockedExchange` 发布 managed/native 槽、release-store C++ offSignal，再停放大镜协调器、唤醒 UI3。早期启动失败、Host 意外停止与烟测路径也直接调用 `SetOffSignal(1)`；统一保护若只放在 `CloseProgram` 会漏这些入口。设置/UI 用户取消发生在调用前，不应启动任何退出 watchdog。
3. 主 `wWinMain` 循环看到 offSignal 后依次：注销/停 Preview、Whiteboard/Setting Shutdown、逐个 join UI3 初始化/冻结/状态/放大镜/PPT/TopWindow，停 Draw3 Host，再停 Window Service/RenderPipeline/Display，做旧线程状态 `20×500 ms` 轮询、COM/activation/module 释放、关闭 `launchMutex`，最后仅 signal=2 才 `ShellExecuteW(GetCurrentExePath(),"-Restart")`（当前 `IdtMain.cpp:2672-2726`）。`std::jthread::join()` 不带超时；旧 10 秒状态轮询排在这些无界 join **之后**，不能保护它们。
4. `Setting::Shutdown` 在 RenderPipeline control 已投递时无界等 `settingDrainCondition`（`Setting.cpp:8495-8523`）。`Window::Service::StopUnlocked` request_stop/wake 后无界 join 两个 owner 线程（`Window.cpp:675-700`）。PPT business 线程可在 Office COM/modal 边界停住；`PPTLinkageMain` join 由主线程执行，`GetPptState` 另有 detached worker，不能假设其返回仅需 100 ms。Draw3 `Host::Stop` 先关 bridge、停 RTS、无界等退出快照回执，再让 Desktop/PPT worker `CloseAndDrain()` 无界 join，最后 join 绘制线程（`Host.cpp:1303-1359`；AutoSave `:1154-1163`、PresentationAutoSave `:1188-1196`）。任何一个等待都会阻止当前 `ShellExecute` 重启。
5. 当前 `launchMutex` 只在 Release 常规入口建立；`-Restart/-WarnTry/-CrashTry` 都跳过已有实例检查（`IdtMain.cpp:759-829`）。若在旧进程结束前直接启动新 `-Restart`，新旧可能同时访问配置、UInk/index 和双画布。当前正常重启在关闭 mutex 后启动，但仍早于旧进程真正退出；超时保护不能进一步扩大这种重叠。

## 推荐的控制面：精确父进程句柄的独立 helper，同一 EXE 早期内部模式

**所有权。** 关闭/重启意图经进程级 `ShutdownCoordinator::Request(Close|Restart)` 原子仲裁，首次被接受时、调用任何 `HideAllUserWindows`、CrashHandler 锁、COM、join 或 worker I/O **之前**启动一个独立 supervisor。`CloseProgram`/`RestartProgram` 只是用户入口；直接 `SetOffSignal(1/2)` 要走同一 request/arm 路径。第一次有效意图最多创建一个 helper；确认框取消不调用 Request。受控故障 Close 可在 helper 尚未 relaunch 前取消 Restart 意图；若没有这个反向状态渠道，应明确规定 first accepted wins，不能同时允许两条 launcher 各自启动。

**实现位置与身份。** 不恢复旧额外 exe；同一 `Inkeys.exe` 增一个只用于内部 supervisor 的早期分派，位于配置、互斥体、CrashHandler、UI/COM/Draw3 初始化之前。启动参数/握手必须验证来自本进程的真实可继承句柄，而非仅凭 PID 或文件名。父进程以 `DuplicateHandle` 制作仅具 `SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION` 的真实自身进程句柄（不可继承 `GetCurrentProcess()` 伪句柄），再以 `STARTUPINFOEXW` 的 `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` 只继承它及少数状态 event；**不得继承** launchMutex、文件/日志句柄或所有 handle。helper 用继承句柄 `GetProcessId`/`QueryFullProcessImageNameW` 核父 PID 与同一可信 EXE，绝不按进程名枚举杀机。仅以绝对 `GetModuleFileNameW` 路径为 `CreateProcessW.lpApplicationName`，命令行把自身路径正确加引号，固定工作目录为可信 EXE 目录；不依赖 PATH/current working directory，不执行 shell、不提升权限。内部模式不接受普通用户给的裸 PID 去终止进程，验证失败立即返回且不创建窗口。

上述句柄列表和 `STARTUPINFOEX` 在 Win7 可用，无须引入高于 Win7 SP1+仅 KB2670838 的 API；参考 [Microsoft handle inheritance](https://learn.microsoft.com/en-us/windows/win32/procthread/inheritance)、[UpdateProcThreadAttribute / HANDLE_LIST](https://learn.microsoft.com/en-us/windows/desktop/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute)。`CreateProcessW` 传非空 `lpApplicationName` 可避免带空格/中文路径被误解析，见 [Microsoft CreateProcess security remarks](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw)。`TerminateProcess` 对他进程返回可先于真正终止，必须随后等待旧进程句柄 signaled 才可启动新实例，见 [Microsoft TerminateProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminateprocess)。对 Win7 的实际 helper、Win32/x64/ARM64 编译和运行仍须分别验收；不能仅凭 API 文档当 PASS。

**截止与 launcher 唯一性。** helper 在确认握手后持有从意图接受时计算的单调 15,000 ms 截止，等待精确父进程 handle。旧进程 15 秒内正常退出，等待返回即自然取消“强杀”；Close 模式 helper 退出，Restart 模式 helper在确认旧进程死亡后**唯一一次**以原 EXE 绝对路径、单个既有 `-Restart` 参数启动新进程，然后退出。旧 `IdtMain.cpp:2722` 的直接 ShellExecute 应被这一 helper launcher 取代，不能保留双启动。到截止旧进程仍在、且下节的保存安全门已完成时，helper `TerminateProcess(parentHandle, distinctForcedExitCode)`，有界等待旧 handle 真正 signaled，成功后才执行同一 Restart/Close 分支。若不能确认旧进程已死亡，**不启动新实例**，避免同目录 UInk 和 HWND 并发；记录错误状态，人工处理。helper 自身不调用业务对象、Window Service、COM、异步 logger、UEF、正常 Draw3 保存或线程 join，不把硬终止记录为“正常退出/保存完成/崩溃捕获”。若 `CreateProcessW` 建立 helper 或握手本身阻塞/失败，当前入口没有独立守护者可保证从用户点击起严格 15 秒；必须把它记为 arm 失败，而不能回退到“旧线程自己睡 15 秒”后宣称保护有效。绝对截止若是产品硬要求，需要在正常启动期预备独立守护者并测其资源/生命周期成本。

**单实例交接。** helper 不继承旧 `launchMutex`；它只在旧进程句柄 signaled 后启动新 EXE。受控 `-Restart` 新实例应像普通启动一样参与当前同路径 mutex 仲裁（保留 `-WarnTry/-CrashTry` 的独立旧合同并另审），因为“旧已退出”不再需要无条件 bypass；若另一个用户实例抢先取得 mutex，新 restart 必须失败安全而不能双开。新进程不接受 helper 内部 handle 参数，不转发旧 `-Restart` 重复链；继续使用当前的 `GetCurrentExePath()` 与一次 `-Restart` 语义。旧实例退出前即使已 `CloseHandle(launchMutex)`，helper仍等**进程**终止，防止它在静态析构/COM 尾部与新进程并存。用户关闭/重启首次确认之后才允许 helper 启动；没有后续“取消”UI 时不推断用户撤销，若增加明确取消则通过唯一 cancel event 禁止 relaunch。

## 15 秒与已接受 UInk 保存请求之间的硬冲突（发布门禁）

当前 Desktop/PPT worker 的 `Submit` 接受的是内存队列，`CloseAndDrain` 通过无界 join 等待每项最终处理。若 worker 在磁盘 I/O 中超过 15 秒、或主线程卡在到达 `PrepareExitAutoSave` 之前，外部 helper 在整 15 秒强杀进程会丢失尚未 durable 的**已接受保存请求**或最后尚未捕获的 CPU 文档。用户已明确禁止用丢弃已接受请求换取等待缩短。UInk 文件临时写、原子替换与 index 主/备并不是一个跨文件原子事务：Desktop 每次新建独立 UInk 文件，旧 index 指向的旧文件可保留；PPT 对既有同一路径文件先 `SaveUInkFile(SaveExistingLogicalFile)`，后更新 index。`uink_file.cpp:1012-1103` 使用 `ReplaceFileW` 生成前版 `.bak`，但在单文件 revision 自检后即删除 backup，**早于** `PresentationAutoSave.cpp:792-802` 提交新 index。若在两步之间硬杀/断电，旧 index 仍持旧 revision、同路径 UInk 已替换且 `.bak` 可能被删，`LoadPresentation(:868-875)` 直接 `SourceChanged`；进程内 `pendingIndexEntries` 不能跨硬杀保留。这是最后有效组合可能不可读的独立 P1 风险，不能用“UInk 与 index 各自原子”声称解决。须以单独故障注入确认并修正 PPT 的跨文件提交/恢复协议（例如新版本文件路径先 durable，index 后切换且旧文件留到新索引安全提交），而不是只加 watchdog。因此现有架构下无法同时承诺“从任意退出意图起绝对 15 秒强杀/重启”、“所有已接受保存请求不丢”和“任意 I/O 边界最后已提交 PPT 均可读”。跳过卡住的 `jthread.join()` 并继续析构也不安全，会令后台线程访问已释放的 Window/COM/Document。

在**不改变用户数据合同**的最小安全默认下，helper 还需观察独立的 `exitBarrierPrepared` 与 `storageDrained` 状态（由 Draw3 owner 在 `PrepareExitAutoSave` 尝试完成、两个 worker `CloseAndDrain` 返回并已记录失败/成功结果后单调发布；未启动 Draw3 可明确 `noDocument`）。仅当该状态表明所有已接受请求已被 worker 完成/明确失败处理时，15 秒截止才能硬终止**其后**仍卡住的 Window Service、RenderPipeline、Display、COM 或程序尾部。当前正常退出顺序中 Setting Shutdown 和六个 `jthread.join()` 都排在 `Host::Stop/CloseAndDrain` **之前**，故这个安全门无法保护这些早期等待；若它们卡住，保存屏障可能永远未执行，helper 只能报告截止已过而不能安全强杀。要保护这些等待需先把完成文档的捕获/worker 排空与 UI 退出解耦并前移，必须独立审查所有线程/窗口/RTS 依赖，不能简单调换两行 Stop 顺序。若存储本身卡死，helper同样在 15 秒**记录 deadline 未满足并继续等存储安全门**，不能悄悄杀掉 worker。若 barrier 报 `bridge_closed/controller_stopped`，也不能将 `storageDrained` 简化为“已保存”：F-026 已完成文档快照可能只尝试入队，活动 contact 仍未保证。这一政策只对安全门之后的清理阶段有有限保护，**不满足无条件有限退出**。若发布必须无条件 15 秒，需另设计 durable 请求日志或独立存储进程，在告知并获产品取舍后才能允许故障时终止；此更大工作和磁盘/三架构/Win7/原子性验证不能由几行 watchdog 冒充完成。

旧进程被硬终止时 UEF 与正常析构都不会执行；Desktop 的前次已提交 UInk/index 与 PPT 的跨文件窗口须分别故障注入并严格回读。在 PPT 跨文件修正/验证之前，连“前次已提交 PPT 文件必可读”也不能承诺。新进程是否把内容画到可见画布是另一条验收，当前跨进程 PPT 自动恢复未开放。不得在 helper 触碰/清理未知用户文件或“补写”业务数据。

## 最小验证方案与状态口径

**无 GUI、隔离子进程。** 可将同一生产 supervisor 模块编入已有 `InkeysHeadlessTests`，由显式 `--shutdown-supervisor-test` 只创建本测试自己的 child，临时数据在忽略目录；产品内部 helper mode 必须在 `wWinMain` 最早期拒绝缺少合法继承句柄/握手的请求，不暴露普通用户可误触发的“杀进程”入口。测试不终止用户已有 Inkeys、Office 或任何未知 PID。

1. 隔离 close child 在 15 秒内自然退出：parent handle signaled，helper 取消强杀且不 relaunch；重复 Close/Restart 请求最多一个 supervisor。用户确认取消前不创建 helper。另测 restart child 自然结束：旧 PID 已终止/互斥量释放后仅启动一个 `-Restart` child，路径含空格/中文，内部参数不转发，失败不无限重试。
2. 子进程在 `HideAll`、Setting drain、WindowService `Submit`、PPT/COM 包装等待与非存储 join 的对应模拟点各卡住（显式测试进程信号，不调用真实 GUI/Office）：已 armed helper 不依赖卡死线程。对当前顺序中**安全门前**的 HideAll/Setting/join，验证 15 秒到期只报告 blocked 而不丢请求；对**安全门后**的 WindowService/COM，真实 15 秒用例才允许返回 distinct forced exit code。快测可用测试专属短 deadline 验证状态机，但不能把短测冒充生产 15 秒。
3. Draw3 隔离保存 worker 使用现有受控 I/O 延迟/失败夹具：在 deadline 前未 `storageDrained` 时 helper 不杀，最后有效 UInk/index 字节和 GUID/页身份仍严格可读；worker 完成/明确失败后才允许终止卡在另一 join 的旧进程。若需求改为无条件 15 秒，此用例应先红并触发产品决策，不能删去失败记录。测试 `TerminateProcess` 与真实 UEF 异常分开，后者按 `-CrashTry` 现有路径另验。
4. 测错误握手、失效/非同 EXE 进程 handle、supervisor 创建失败、`TerminateProcess` 拒绝或父句柄不 signaled、新实例启动失败、两次同时请求/close 覆盖 restart：不会杀其它进程、不会双开、没有超时循环，日志/退出码能分辨 `graceful_close`、`graceful_restart`、`forced_after_drain`、`deadline_blocked_by_storage`、`launcher_failed`。普通构建仍为完整 Solution Debug|原生架构和 Release/矩阵附加验收。

**真机人工。** 在用户另行授权 GUI 后，专用测试配置与文档下分别从主栏、设置、强制更新触发 Close/Restart；检查两窗先隐藏、RTS/Office/Setting 退出、旧实例确实终止、新实例仅一个且可落笔；用工具制造绘制中、PPT COM 忙、Worker I/O 延迟、DComp/ULW/设备丢失与旧启动前崩溃。Win7 SP1+仅 KB2670838 的 Hardware FL11.0/无 FL11.0→WARP 与 FLIP、两种 DWM 禁用保持原合同，helper 不加入 Win8+ API。自动“helper 强杀/重启成功”、已提交文件完整、新进程可见恢复、人工体验四项分别记状态；未取得 GUI/磁盘故障证据不升级发布结论。
