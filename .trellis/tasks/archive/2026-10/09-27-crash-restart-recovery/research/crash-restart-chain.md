# 崩溃、重启、退出与恢复生产链静态调查

- 时间：2026-09-27。依据当前 `chore/publish` 工作区源码；本子调查只读产品代码，没有启动 GUI、注入异常或操作用户进程。并行任务正在修改其它模块，最终应以 HF 复审本表行号和结论。
- 状态词：**静态确认**表示由编译登记与调用链直接证明；**风险**表示可执行路径存在但缺隔离运行复现；**需要人工**表示 AGENTS 禁止默认启动产品 GUI；**不适用**表示当前产品明确未开放。
- 编译登记：`Inkeys/Inkeys.vcxproj:914-915` 编译 `Helper.CrashHandler.cpp/.cppm`；`Window.Legacy.cpp` 为 `ClCompile`（`:867`），旧 `IdtWindow.cpp` 为 `None`（`:1171`），不能把旧窗口线程分支当作生产证据。

## 实际 owner 与端到端链

| 情况 | 当前代码链 | 静态结论 / 未验证点 |
| --- | --- | --- |
| 普通启动 | `IdtMain.cpp:265` `wWinMain` → `:290-353` EXE 目录/权限与名称预检 → `:354-424` 启动标志与条件单实例门 → `:427` `CrashHandler::Initialize()` → UI3/Window Service/Draw3 Host/Setting 初始化 → `:2128` 主线程等待 | 预检前发生的异常不受自定义过滤器保护；显式启动失败返回不等于崩溃重启。实机初始化与首帧未由本调查执行。 |
| 正常关闭 | 设置、主栏等调用 `CloseProgram()`（`Helper.CrashHandler.cppm:57-63`）→ Window Service 同步隐藏 → `CrashHandler::Shutdown()` → `SetOffSignal(1)` → `IdtMain.cpp:2187-2204` 依序停 UI3/Setting/Draw3/窗口 → `:2220-2241` COM 与 mutex 清理、返回 | `Host::Stop` 在控制器存活时送最终保存屏障并 drain（下文）。`offSignal=1` 不走 `:2237` ShellExecute；但 Shutdown 的空旧过滤器缺陷使“关闭期间绝不崩溃重启”尚不成立。等待与真实文件完整性需要人工/隔离验证。 |
| 用户主动重启 | `RestartProgram()`（同模块 `:64-70`）按关闭路径置 `offSignal=2`；主线程完成清理并关闭 mutex 后，`IdtMain.cpp:2237` `ShellExecuteW(..., GetCurrentExePath(), L"-Restart", ...)` | 触发者是**旧进程主线程**，在正常清理之后发起新进程。未检查 ShellExecute 返回值/新 PID/新进程 ready；静态不能写“重启成功”。用户取消确认时 `Setting.cpp:228-249` 不调用 RestartProgram。 |
| 受支持未处理异常 | `CrashHandler::Initialize`（`Helper.CrashHandler.cpp:30-35`）用 `SetUnhandledExceptionFilter` 注册 → `UnhandledExceptionHandler`（`:331-484`）有 `g_isGeneratingDump` 并发门 → 路径、dump/report → 按 `teachingSafetyMode` 选择提示/直接 `ShellExecuteW(..., L"-CrashTry")` 或不重启 → 返回 `EXCEPTION_EXECUTE_HANDLER`/`CONTINUE_SEARCH` | **自动拉起 owner 是崩溃进程中的未处理异常过滤器**，不是外部 watchdog。`-CrashTry` 新进程在 `IdtMain.cpp:423` 设置 `IsSecond(true)`；`Window.Legacy.cpp:159` 首帧 ready 后清除。没有实际异常注入、进程交接或 ready 证据。 |
| 首帧等待失败 | `Window.Legacy.cpp:119-146` 先向 Startup tracker 报故障；tracker 缺失时才旧 fallback `ShellExecuteW(..., L"-WarnTry")`，第二次显示提示并退出 | `-WarnTry` 在 `IdtMain.cpp:370` 识别。正常 tracker 路径由 `IdtMain.cpp:2171-2185` 显示致命启动错误、`offSignal=1` 安全退出，不能把旧 fallback 当常规重启行为。 |
| 硬终止/断电/FailFast/C++ terminate | 无专门 watchdog、WER Restart Manager 注册或跨进程 supervisor；仅 UEF 注册 | `TerminateProcess`、断电不走普通清理/捕获；`std::terminate`、CRT invalid-parameter/purecall 在此代码没有单独可靠重启保证。`Draw3.Host.cpp:1151-1185` 会把绘制线程的 C++ 异常捕获为启动失败/局部停止，不能当成 UEF 自动重启测试。 |

`teachingSafetyMode` 来源为 `IdtConfiguration.cpp:148-149`，在 `IdtMain.cpp:1533` 装入过滤器状态，设置变更在 `Setting.cpp:3668-3671` 更新。代码语义：0 显示确认框，用户 OK 才尝试 `-CrashTry`；1 直接尝试；2 生成报告后不尝试；3 生成报告后 `EXCEPTION_CONTINUE_SEARCH`。当前工作区已由父任务并行修复配置范围，HF 应重核。新进程的 `-CrashTry` 若在首帧前再次异常，过滤器在 `Helper.CrashHandler.cpp:333` 立即返回 `EXECUTE_HANDLER`，连第二次 dump/report 都不生成。

## 已证实缺陷与风险优先级

1. **P1，静态确认：Shutdown 不一定卸载过滤器。** `Helper.CrashHandler.cpp:32` 将 `SetUnhandledExceptionFilter` 返回的前一个过滤器存入 `PreviousFilter`，`:48-54` 仅当它非空时才恢复。先前没有过滤器时返回 `nullptr`，`CloseProgram/RestartProgram` 调用了 Shutdown 但本过滤器仍在进程里；保存/线程排空期间若发生未处理异常，可进入崩溃重启路径，违背正常关闭不意外拉起的合同。最小修复：单独记录安装状态，恢复前一个值时允许 `nullptr`；不要用“前过滤器非空”代理“已安装”。可用真实模块的隔离进程验证 `Initialize→Shutdown` 后 Windows 过滤器恢复值，且正式关闭期间 fault 不生成新实例；本调查未运行。
2. **P1，风险：重启没有完成回执与单实例交接。** `IdtMain.cpp:2237` 和 `Helper.CrashHandler.cpp:474-476` 丢弃 `ShellExecuteW` 返回值；`IdtMain.cpp:390-422` 的 mutex 门只在 `IDT_RELEASE` 编译，而 `IdtMain.h:20` 注释该宏，vcxproj Release 的 `PreprocessorDefinitions` 也未显式定义。若外部属性表未增加该宏，当前 Release 连普通双开都不阻止；即使定义，`-Restart/-WarnTry/-CrashTry` 也直接绕过检查。两进程覆盖层、同目录保存及初始化交叠需隔离实测；存储层 named mutex 减少 index 并发写风险，不保证 UI/业务只有一实例。修复前先确认实际 CL 命令宏和产品启动约束。
3. **P2，风险：同一崩溃进程可重复发起子进程。** `Helper.CrashHandler.cpp:335-339` 的 `g_isGeneratingDump` 只防同时进入；第一次 handler 在 ShellExecute 后、原进程结束前于 `:478` 重置 false。另一工作线程此时发生异常可再发 `-CrashTry`。`currentUserIsSecond` 只保护带 `-CrashTry` 的新进程首帧前，且 `Window.Legacy.cpp:159` 首帧后清零。建议一次性 crash-relaunch latch 与统一、有限的跨 `-WarnTry/-CrashTry` 重试合同；不可在异常 handler 中做完整正常保存或无限等待。
4. **P2，恢复能力边界：新进程可见恢复当前未开放。** Desktop worker `Draw3.AutoSave.cpp:1052-1085` 每次 Start 建新 session，并清空进程内 records；`SubmitLoad(fileGuid)` 在 `:1123-1141` 只搜索当前 records 的 committed path。唯一生产调用 `Draw3.Host.cpp:512-517` 来自 `Draw3.DrawingController.cpp:6058-6080` 的**同进程 Clear 撤销**。`IdtMain.cpp:1953-1956` 仅提供保存根；没有启动时扫描 Desktop 索引并 materialize 旧画布的生产路径。PPT `ProcessSessionId()`（`Draw3.PresentationAutoSave.cpp:955-963`）每进程新 GUID，`LoadPresentation` 在 `:857-867` 拒绝 foreign session；规范 `.trellis/spec/native-desktop/draw3-integration.md` 声明跨进程 PPT 恢复不开放。故“最后已提交 UInk/index 仍可读”和“重启后画面恢复”必须分项；后者按当前入口为**不适用/未开放**，不能写 PASS，也不应为验收擅自开启。
5. **P2，潜在隐私/稳健性：handler 在损坏进程内做重工作。** `Helper.CrashHandler.cpp:344-437` 使用 filesystem、动态分配、symbol API、磁盘清理；`:441-476` 还调用 i18n 读锁与 MessageBox；`GenerateMiniDump` 于 `:518-525` 选 `MiniDumpWithPrivateReadWriteMemory`。已使用 try-lock i18n、并发门和 best-effort 清理，但堆损坏、OOM、业务锁被崩溃线程持有时不能保证成功。dump 位于 EXE 旁 `Inkeys/Crash`，可能含墨迹、文稿路径或其它进程内敏感数据；无上传路径证据。本任务应核对默认访问权限、告知/保留策略，不能把 dump 存在视为恢复成功。

## 路径、参数、权限及 Win7 边界

- `CrashHandler::GetExeDirectory()`（`Helper.CrashHandler.cpp:57-69`）实际返回完整 EXE 路径，名称/注释不准；调用处先取其 parent 作 crash 目录，却将完整路径传给 ShellExecute，因此不是误把目录当 lpFile。路径来自 `GetModuleFileNameW`，启动参数是 ShellExecute 的独立 lpParameters，空格/中文**目录**无需自行拼引号；未显式使用 `runas`。`GetModuleFileNameW` 失败时仅 dump 位置尝试 current directory，ShellExecute 的 lpFile 仍为空，不能声称回退能重启。
- `IdtMain.cpp:333-338` 明确拒绝非 ASCII 的 EXE **文件名**，中文 basename 在安装 handler 前退出；这与中文目录不同，若产品要求完整中文路径运行/重启则是兼容缺口。Win7 SP1+KB2670838 的上述 Win32 基础 API 可做静态候选，但没有目标真机证明；还要测路径带空格/中文目录、标准权限与 UIAccess/SuperTop 等实际启动环境，不把 ARM64 Win11 结果外推 Win7。
- `-Restart/-CrashTry/-WarnTry` 都是用户可传命令行标志，当前未有旧进程颁发的 handoff token；若要严格一实例，不能只信任标志。代码未显式提权，但真实用户权限与 manifest/包装器由发布产物人工复核。

## Draw3 保存与数据恢复的准确保证范围

`Host::Stop`（`Draw3.Host.cpp:1274-1331`）关闭 Bridge 生产端并追加固定容量之外的 `PrepareExitAutoSave` 最终命令，停 RTS producer，等待绘制线程回执 `exitAutoSavePrepared` 或绘制线程死亡，随后依次 `autoSave.CloseAndDrain`、`presentationAutoSave.CloseAndDrain`，最后请求绘制线程停止并 join。控制器在 `Draw3.DrawingController.cpp:6031-6046` 先同步捕获 Desktop 当前非空页与 Presentation active/parked dirty slot，入保存 worker 队列，然后回执。Desktop 在 `:1689-1760` 只对 `Desktop + saveSetting.enable + 可见内容` 捕获；PPT worker独立，不读 Desktop 开关。

Desktop `ProcessRequest`（`Draw3.AutoSave.cpp:627-695`）先 `SaveUInkFile(CreateNewLogicalFileWithIdentity)`，成功并严格回读 file GUID 后才写每日索引；索引在 named mutex 下重读主/备、写全新临时文件并校验，再 `ReplaceFileW/MoveFileExW`（`:491-579`）。索引失败返回失败并保留 UInk/旧索引；`CloseAndDrain`（`:1154-1162`）关闭接受端、join worker。PPT 同样在命名 mutex 下处理 UInk 与 index，并在 `PresentationAutoSave.cpp:397-433` 原子替换 index；`LoadPresentation` 需匹配 session、key、binding 与 source revision（`:805-879`）。这证明**设计路径**具有最后有效索引/孤儿隔离策略，不证明断电、磁盘满、无权限、并发实例或崩溃中间态已经实机通过。UInk 自身的 `SaveUInkFile` 在 `inkStrokeModelerTest/draw3/uink_file.cpp` 执行临时文件/替换与恢复协议，需用生产同一代码的故障注入复核 `Committed` 以后内容可读。

自动崩溃路径**不执行** Host::Stop/最终捕获，也不调用业务对象或保存 worker；只有已经提交的恢复点可期望保留，崩溃前未提交墨迹不承诺零丢失。`Draw3.Host.cpp:1151-1185` 的绘制线程 C++ 异常被捕获并局部停止，旧进程可能仍运行；这应单列为“绘制能力停止/功能失效”测试，不能用它替代真实 UEF 异常注入。显式启动失败在 `IdtMain.cpp:2012-2066` 直接按失败状态停止资源并返回，属于受控失败，不等于自动重启。

## 可执行验证矩阵（本调查均未运行 GUI）

| 场景 | 隔离步骤与验收观测 | 当前状态 |
| --- | --- | --- |
| 保存文件完整与身份 | 在隔离临时根运行实际 `DesktopAutoSaveService` 的 `inkStrokeModelerTestTests.exe --desktop-autosave-only`；覆盖 UInk 成功/index 失败、主备索引损坏、双 writer、跨日期、错误 path 与完全回读。PPT 运行实际 service 的测试或补一个专用无窗 CLI，核对 foreign session/SlideID/结束页隔离；记录命令、退出码、文件 hash。 | **未由本子调查运行**；现有自动化入口存在，父任务可引用实际运行结果。 |
| 普通退出与主动重启 | 专用副本 EXE+空白配置+独立 AutoSave 根，仅一台验收机；先确认无已有 Inkeys/Office 用户进程被操作。记录旧 PID、所有子线程/辅助进程、隐藏 HWND、最终保存 completion、文件 hash/索引、旧进程退出、新 PID/ready、确认取消无新 PID。主动重启必须区分 ShellExecute 尝试与新实例 ready。 | **需要人工**，AGENTS 禁止默认启动产品主窗口。 |
| UEF idle、输入中、保存边界 | 只在显式启用的测试构建/隔离进程，用受控未处理 SEH 异常；各测 mode 0（确认/取消）、1（自动）、2/3（不重启），核对 dump/report、旧 PID 结束、新 PID 一次且 ready、最后已提交 UInk/index 内容。保存边界分别注入 UInk 前、UInk 已提交但 index 前、index 已提交后。不要在真实配置或用户文稿中注入。 | **需要人工**；不能用 TerminateProcess 代替 UEF。 |
| 二次崩溃/启动循环 | 隔离复制品让新进程在首帧前再故障，确认最多一次新进程；另测首帧后立刻重复故障及 `-WarnTry/-CrashTry` 交错，限定总重试和报告留存。 | **需要人工**；现有 `IsSecond` 仅首帧前且第二次无 dump。 |
| 非 UEF 故障 | 单独对外部强杀、`std::terminate`/工作线程异常、初始化受控失败、断电/磁盘拔出建立能力矩阵；强杀只验证最后已提交文件，不期待异常报告/自动拉起。 | **需要人工/不适用**，按异常类别分别记账。 |
| Win7 SP1+KB2670838 | 仅在目标 x86/x64 真机或等效指定环境、FL11.0 有/无与 HARDWARE/WARP 路径上验证上述进程、dump 和保存；UI3/Draw3 presenter 仅 DComp/ULW，保持 `FLIP_SEQUENTIAL` 用户实测合同。 | **需要人工**；本机 Win11 ARM64 不可冒充。 |

## 交接

- 优先由唯一 `Helper.CrashHandler.cpp/.cppm` owner 修复 `PreviousFilter == nullptr` 卸载问题，并独立审查实际 diff；本研究未改产品代码。
- 下一层审查应检查 Release 实际 CL 宏、手工 `-Restart` 双开与 ShellExecute 失败反馈；再决定是否引入窄范围进程交接/有限重试。跨进程可见恢复是当前未开放功能，按上文能力边界交集成门禁。
- 任务最终记录需把“异常捕获/自动拉起/新进程 ready/文件可读/画面可见恢复”五项分别标状态。本研究本身不是运行通过证据。
