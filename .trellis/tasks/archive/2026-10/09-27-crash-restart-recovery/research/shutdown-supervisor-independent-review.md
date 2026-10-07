# F-043 15 秒退出监督器独立复审

日期：2026-09-28。范围：新 `ShutdownSupervisor.h/.cpp`、`IdtMain.cpp::SetOffSignal`/最早分派/旧尾部拉起删除、`Helper.CrashHandler.cpp/.cppm`、Window Service 的异步 Hide、工程登记、子任务 PRD/design/implement 与 supervisor 合同。仅静态复审实际工作区代码和 diff；未改产品或测试、未运行 GUI/构建。首轮 `InkeysRepo.sln Debug|ARM64` 构建由主 agent 执行，因新 cpp 的 `std::numeric_limits::max` 与 Windows `max` 宏冲突出错，不能记为通过；主 agent 已补 `NOMINMAX`，复编结果待记。

## 结论和优先修正

| ID / 状态 | 证据与影响 | 建议和门禁 |
| --- | --- | --- |
| SR-01 P1，**已修复待验证**：受控 Restart 与 UEF 双启动 | 初版 `IdtMain.cpp:260-272` 在首次 CAS 后需完成 helper 握手、发布 offSignal、卸载 UEF。此间另一线程 UEF 可在 `Helper.CrashHandler.cpp:459-494` 直接 `ShellExecuteW(-CrashTry)`；helper 随后又在旧进程结束后 `CreateProcessW(-Restart)`。主 agent 已使 UEF 在 `Helper.CrashHandler.cpp:359-362` 用同一 `offSignalInterop` CAS 抢意图槽，受控退出先到则不再 ShellExecute；反向竞争会阻止 SetOffSignal 重复拉起。静态检查该原子顺序自洽，`PptCOM` 只按槽非零判断停止。 | 最后修改后完整 Solution 重编；隔离进程对 CAS 两个顺序做确定性竞态测试，真实 UEF 与受控重启交错仍需授权 GUI/异常注入。不要仅凭静态审查标 PASS。 |
| SR-02 P1，**当前存在**：独立 UEF 仍提前启动新实例 | `Helper.CrashHandler.cpp:490-499` 在旧进程仍运行（还在异常过滤器内）时直接异步 ShellExecute `-CrashTry`。`IdtMain.cpp:810` 让 `-CrashTry` 绕过单实例互斥，故新实例可以在旧进程 dump、对话框或退出尚未完成时进入，违反“旧进程结束后启动/至多一个新实例”。新增 CAS 只排除它与受控监督器并发时的双启动，不解决单独 UEF 的旧/新交接。 | 崩溃重启应使用精确旧进程 HANDLE 的外部唯一 launcher，待旧句柄 signaled 再启动 `-CrashTry`；不要在 UEF 中进入业务退出/锁。以真实 UEF 隔离注入记录旧 PID/新 PID、互斥体、窗口与启动完成；当前不应宣称自动崩溃重启已通过。 |
| SR-03 P1 条件触发，**当前存在**：监督器未建立后无 15 秒保护 | `ShutdownSupervisor.cpp:295-329` 对 CreateProcess/握手超时等失败将 `g_armState` 永久置 3；`IdtMain.cpp:262-273` 仅记日志，仍继续关闭，若既有 jthread/Window/保存随后卡住，可无限期滞留。2.5 秒握手超时也可在缓慢启动/杀毒扫描下出现；测试目前只让错误 PID 握手失败，未从真实 `SetOffSignal` 证明 fallback。OS 禁止创建子进程时无法承诺外部重启，必须如实限制能力。 | 给 `SetOffSignal` 的 arm-failure 路径一个明确、可验证的有界后备（至少 Close 自身的本进程计时强退），Restart 若外部 helper 不可用需明确失败语义/发布门禁；注入 CreateProcess、握手和 helper 早退，核 15 秒及旧 PID 结束。不可将“写了错误日志”视为兜底成立。 |
| SR-04 P2，**当前存在/需测可见延迟** | `CloseProgram`/`RestartProgram` 先同步进入 `SetOffSignal`，`ArmCore` 最多等待 2500 ms 真实子进程握手（`ShutdownSupervisor.cpp:295-296`），之后才调用 `RequestHideAllUserWindows`（`Helper.CrashHandler.cppm:57-67`）。新可见 Hide 延迟可达到 2.5 秒或更久；用户已经报告点击退出视觉响应但实际不退出，任务不允许引入可见延迟。 | 衡量正常/慢启动点击→Hide 与请求→deadline。若慢启动可达，分开快速的可见撤销与可靠的已接受意图/监督握手，同时不能把可能卡住的 Window 队列锁置于监督器启动前。现有异步 Hide 只保证不等 owner completion，队列 mutex 和 owner 线程内 `Execute` 仍可能阻塞，但已在 helper 成功启动之后。 |

## 静态正确性与安全边界

- **精确对象**：父进程通过 `DuplicateHandle` 创建非伪 HANDLE，只继承 `SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION`；`STARTUPINFOEX` handle list 只含父进程和握手 event（`ShutdownSupervisor.cpp:252-291`）。子进程验证可继承 flag、`GetProcessId(parent)` 与参数 PID、父/自身 EXE 文件身份后才 `SetEvent`（`:347-358`）。到期仅对该 HANDLE 调 `TerminateProcess`，再等 signaled；不按进程名杀用户进程（`:362-381`）。未见提权、Shell 命令解析或未知句柄继承路径。
- **时钟/路径**：截止 `GetTickCount64()+15000` 在 CreateProcess 之前计算；helper 按剩余单调时间等旧进程，不因握手重置 deadline（`:247-248`, `:345-374`）。自然 Close 不强杀；自然/强制 Restart 均待旧进程 signaled 后用同一 EXE 的绝对 `lpApplicationName`、带引号 `argv[0]`、固定 EXE 目录、`bInheritHandles=FALSE` 启动一次 `-Restart`（`:182-195`, `:375-384`）。同文件身份校验在 EXE 路径被重命名/替换或只读不可打开时会保守拒绝；此时落入 SR-03 的无保护分支。
- **Win7 静态 API**：此模块的 `InitializeProcThreadAttributeList`、`UpdateProcThreadAttribute`/HANDLE_LIST、`QueryFullProcessImageNameW`、`GetTickCount64`、`CompareStringOrdinal`、`GetProcessId` 文档最低客户端不高于 Win7；无需 KB2670838 之外额外补丁的*源码 API* 迹象。参考 [InitializeProcThreadAttributeList](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-initializeprocthreadattributelist)、[UpdateProcThreadAttribute](https://learn.microsoft.com/en-us/windows/desktop/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute)、[QueryFullProcessImageNameW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-queryfullprocessimagenamew)、[GetTickCount64](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-gettickcount64)、[CompareStringOrdinal](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-comparestringordinal)、[GetProcessId](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocessid)。这不替代最终 EXE x86/x64 导入表与 Win7 SP1+仅 KB2670838 真机测试。此单元未改 `FLIP_SEQUENTIAL` 或 DComp/ULW 选路；FL11.0 Hardware 与无 FL11.0→WARP 仍属独立真机矩阵。
- **测试作用域**：`--shutdown-supervisor-tests` 在 `wWinMain` 配置/单实例/HWND 前分派；本测试创建的 parent 子进程再开 helper，`TestChildGuard` 仅以自己持有的子进程 HANDLE 强退（`:54-66`, `:536-655`）。坏 PID/坏握手会立即返回。真实 15 秒只覆盖强制 Close；强制 Restart 仅 3 秒状态机样本。Unicode/空格路径测试只验证 `Quote`/`CommandLineToArgvW`，未从真实中文/空格 EXE 路径完成 CreateProcess。用 marker 证新实例至多一次，未证产品 ready、GUI 或保存成功。
- **资源/构建**：工程及 filters 均登记新 `.h/.cpp`；`git diff --check` 对当前受审已跟踪文件退出 0。新 cpp 初轮宏错误已由主 agent 加 `NOMINMAX`，仍需记录最新编译退出码。未执行 Release/Win32/x64 构建、严格 Win7 import、真实退出/自动重启/旧 durable UInk 新进程可见恢复，不能把这些升级为 PASS。F-044 版本化 PPT 文件故障注入独立于本 reviewer；只有其绿测和最后已提交主/备索引可读闭环后，15 秒无条件强退的存储底线才可成立。

## 建议最小复验

1. 最新 `InkeysRepo.sln Debug|ARM64` 与 Release 构建、`--shutdown-supervisor-tests`（记录每例 old PID、exit code、elapsed、restart count）；第一次 `max` 宏失败保留原记录。
2. SR-01/03 隔离测试：CAS 两种先后、错误 PID、helper 握手慢/失败、CreateProcess 失败、同一 intent 重复、Close→Restart 与 Restart→Close；证明失败时实际旧进程处理结果。
3. SR-02 在获准 GUI/异常注入后用本任务专用配置/进程触发真实 UEF，记录旧实例是否已结束才见新实例及单实例/HWND 状态；外部强杀另测，不冒充 UEF。
4. 目标 Win7 SP1+仅 KB2670838 x86/x64 运行 helper 的正常/超时 Restart，检查导入与权限；同时按用户要求另测 FL11.0 Hardware、无 FL11.0→WARP、DComp/ULW/FLIP，不能从本监督器静态检查推导图形通过。

状态：**监督器静态设计方向成立，但 SR-02/SR-03 和真实目标环境证据未闭环；自动重启/15 秒全入口保护目前不宜判定发布通过。**

## 2026-09-28 F-046/F-047 后续独立复审（覆盖上文冻结时状态）

此节审最终已读源码 `ShutdownSupervisor.h/.cpp`、`Helper.CrashHandler.cpp`、`IdtMain.cpp`、工程登记与 CLI。前节 SR-02/SR-03 的“当前存在”只描述首次冻结时状态；以下为本次新结论。本 reviewer 仍只读产品/测试，没有自行编译、注入异常或运行 GUI。主 agent 的连续两轮完整 Debug|ARM64 Solution build exit 0、显式隐藏且精确 WaitForExit 的 `--shutdown-supervisor-tests` exit 0/0，可在 `TestResults/release-hardening/f047-supervisor-race-fixed-build-debug-arm64.log` 和 `f047-supervisor-race-green-r{1,2}.stdout/stderr.log` 复核；早期红版及首次绿候选失败日志保留，不能统称全程 PASS。

| ID / 本次状态 | 实际调用链和风险 | 最小修正/验证 |
| --- | --- | --- |
| SR-02 原提前 `-CrashTry`，**已修复待真实 UEF 验证** | `Helper.CrashHandler.cpp:365-385,517-519` 改为选择后 `ArmShutdownSupervisor(Intent::CrashRestart)`；没有直接 ShellExecute。`ShutdownSupervisor.cpp:316-317,374-425` 以 `X` 模式走同一继承 HANDLE/PID/EXE 身份握手，旧 HANDLE signaled 后才以绝对 EXE 路径启动一次 `-CrashTry`。`CrashHandler` 不进入 `SetOffSignal`/业务锁。无窗子进程 `crash-natural/crash-hung-short` 绿，但并未触发真实 UEF、用户确认、dump、Office 或新实例 ready。 | 真 UEF 独立进程注入和 Win7 运行保留人工门禁；强杀旧进程不算捕获崩溃。 |
| SR-03 原 helper 创建/握手失败无限卡住，**条件范围已修复待真机** | `ShutdownSupervisor.cpp:272-277` 在 CreateProcess 前用同一单调 deadline 启动原始 Win32 `CreateThread`，ThreadProc `:34-45` 只 Sleep/GetTickCount64/TerminateProcess(GetCurrentProcess())，无业务/CRT；失败返回 `FallbackArmed`（`:359-361`）且绝不自行重启。两轮无窗日志中创建拒绝、坏 PID 握手、ack 后 helper 早退均由此线程以 `0xE1430016` 结束，Restart 失败 marker 0。`CreateThread` 与 helper 都失败、线程被系统暂停/loader lock 卡住时仍不能绝对保证 15 秒。 | 不把 helper 失败下的 Close 强退记为 Restart 成功。Win7 x86/x64 导入表与实际进程测试仍待做；[CreateThread](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createthread) 文档最低 Windows XP，只支持源码 API 静态判断。 |
| SR-05 P1，**新发现、待修**：手动崩溃确认提前占用退出意图 | `Helper.CrashHandler.cpp:360-364` 在所有 crashMode 的 dump/report 和用户对话框前立即 CAS `offSignalInterop:0→3`；`crashMode==0` 直到 `:517-519` 用户点 OK 才 Arm。若对话框或 dump 卡住，Setting 另一线程的正式 Close/Restart 调 `IdtMain.cpp:260-261` 见 3 即返回，连自身 15 秒兜底都不建立。用户不确认时也已占用意图槽；`crashMode==2/3` 不重启却同样占槽。 | 自动模式 1 可在报告前 CAS+Arm；需确认模式 0 在 OK **之后**才 CAS0→3，并只在自己获胜时 Arm；不重启模式不要预占槽。补无窗原子竞态用例：UEF 手动确认阶段挂起→另一线程 Close/Restart 胜出、后点 OK 不双拉起。真实 MessageBox 与 UEF 仍须人工。 |
| SR-06 P2，**新发现、待修**：迟到死亡后漏重启 | `ShutdownSupervisor.cpp:399-407` 到期请求 `TerminateProcess(parent)` 后仅等 `kDeathWaitMilliseconds=5000`；如果旧进程 pending I/O 在第 5 秒以后才真正 signaled，helper 返回 27/28 并永久放弃 Restart/CrashRestart。自身兜底与 helper 同时 Terminate 的首候选红灯已由等待修正，但仍有 5 秒截断。Microsoft [TerminateProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminateprocess) 明确对其他进程异步，终止需待未决 I/O 完成/取消。 | 超过 5 秒可记录诊断但继续等待**同一精确旧 HANDLE**直至 signaled，再唯一启动；旧进程尚未结束绝不能提前拉新实例。注入延迟 signaled 的隔离测试，确认超 5 秒后仍仅一新实例。新实例 ready 不能承诺严格 15 秒。 |
| SR-07 单实例绕过，**已修复待 Release 验证** | 原 `IdtMain.cpp` 的 `-Restart/-CrashTry` 例外在 helper 后置启动下已不需要。主 agent 把 `#ifdef IDT_RELEASE` 内条件改为只保留既有 `-WarnTry`/SuperTopComplete 例外（当前 `:812-845`）；全仓仅新 helper 生成前两个参数。新进程在旧 HANDLE signaled 后正常获取命名 mutex；手动用这两个参数启动也不应绕过。 | 当前 F-047 Debug build/CLI **不包含 Release 单实例代码**，不证明此修补。最新 Release 构建和隔离的双实例启动时序/真实产品窗口仍需验证；`-WarnTry` 历史提前拉起例外独立留在原路径。 |

### 其它边界与复核

- F-047 初绿候选因 helper/本进程同 deadline 竞争而 `crash-restart-forced` 无新实例；最终 `ShutdownSupervisor.cpp:404-407` 无论自己的 Terminate 返回值都等旧 HANDLE signaled，再启动一次。两轮日志分别有 `0xE1430015` 与 `0xE1430016` 谁先完成的交错，restart count 均为 1。这只覆盖测试创建的精确旧子进程，不代表真实绘图/磁盘/窗口卡死。
- `ArmCore` 的首次 `g_armState` CAS 和 UEF/SetOffSignal 共用 `offSignalInterop` 使受控意图先到时 UEF 不另拉起、UEF 自动意图先到时受控请求不另拉起；**SR-05 的未确认手动模式预占是例外**。UEF 的 `g_isGeneratingDump` 门闩阻止重复 handler 发第二次 Arm；五分钟 `-CrashTry` 启动崩溃抑制仍在最前。两者的真实触发/回调并发没有动态证据。
- `RunSupervisorChild` 仍校验可继承父 HANDLE 与 event、PID、同一 EXE 文件身份；测试 `TestChildGuard` 只杀本次自建 child。`--shutdown-supervisor-tests` 由 `wWinMain:636-639` 在配置、单实例与 HWND 前分派，测试的 15 秒 Close 约 14.95/14.97 秒是宿主见 marker 后的软件观察，不是点击→进程消失的真实 GUI 时延；强制 Restart/CrashRestart 仍只用 3 秒状态机样本。中文/空格路径仍只做 argv 往返，非实际复制 EXE 后启动。
- 用户 Win7 SP1+仅 KB2670838、保留 FLIP_SEQUENTIAL、只用 DComp/ULW 以及 Hardware FL11.0/无 FL11.0→WARP 的真机矩阵均未由本单元验证。SR-04 点击→隐藏可见延迟也未因自身 deadline 线程消除。F-044 最后已提交 UInk/索引存储证据、新实例可见恢复与本监督器进程证据应继续分开记录。

本轮结论：**SR-02/SR-03 在列明的本机隔离子进程范围转绿；SR-05 与 SR-06 尚需修，SR-07 需 Release 验证。真实 UEF、用户截图画布卡死、Win7 与产品恢复仍未验证。**

## 2026-09-28 SR-05/SR-06 最终源码复审（再次覆盖上述冻结状态）

本 reviewer 只读最新 `ShutdownSupervisor.h/.cpp`、`Helper.CrashHandler.cpp`、`IdtMain.cpp` 的实际代码及红绿日志，未修改产品/测试、未构建、未运行 GUI。主 agent 的 `sr05-sr06-red-build-debug-arm64.log` 与 `sr05-sr06-f041-policy-green-build-debug-arm64.log` 记录完整 Debug|ARM64 Solution 均 exit 0；显式隐藏进程 suite `sr05-sr06-red-debug-arm64.stdout.log` 从新增两项 FAIL/进程 exit 62 转为 `sr05-sr06-green-debug-arm64.stdout.log` 全项 PASS/进程 exit 0。红版 `manual-pending-close-wins` 是宿主精确 HANDLE 在约 11 秒清理；绿版由本进程 timer 在约 5.94 秒强退。红版 `late-death-restart` 旧进程约 9.53 秒自然退出但 marker 0；绿版相同迟到死亡后 marker 1。`git diff --check` 对受审已跟踪文件退出 0。

| ID / 最终状态 | 独立读代码结论与证据界限 |
| --- | --- |
| SR-05 **已修复待真实 UEF/GUI 验证** | `ShutdownSupervisor.cpp:800-805` 的生产 `TryClaimCrashRestartIntent` 只在 `crashMode==1` 或 `crashMode==0 && userAccepted` 时对共享槽 CAS0→3。UEF `Helper.CrashHandler.cpp:360-384` 在报告前仅自动模式能先占槽并 Arm；手动模式先保留0，在 `:485-521` 报告后若当前仍为0才显示确认框，OK 后再次 CAS，只有获胜者 Arm。取消/失败、模式2/3均不占槽；并发 Close/Restart 可先 CAS0→1/2 和建立 deadline，迟到 OK 不双启动。`g_isGeneratingDump` 仍于 `:355-358` 阻止并发 UEF 重入；`-CrashTry` 的五分钟重崩抑制仍先于意图仲裁。无窗红绿用例 `RunTestParent:482-499` 用**同一生产策略函数**模拟手动 pending，并用另一线程按 `SetOffSignal` 的 CAS0→1 争槽后调用生产 `ArmCore`；未真正执行 UEF、MessageBox 或整条 `SetOffSignal`。 |
| SR-06 **已修复待真实 I/O/Win7 验证** | `ShutdownSupervisor.cpp:405-436` 到截止先只对已校验、精确继承的旧进程 HANDLE 发 Terminate；`WaitForSingleObject(parent,5000)` 若超时仅作一次低频 `OutputDebugStringW`，随后 `WaitForSingleObject(parent,INFINITE)` 等**同一 HANDLE** signaled，才唯一 `CreateProcessW(-Restart/-CrashTry)`。无任何裸 PID 重开、提前并存或新增权限；若旧进程永不 signaled，helper 等待且不误拉新实例。`late-death-restart` 夹具在测试内部跳过真实 Terminate/自身 timer，让自建旧 child 在原 3 秒 deadline 后再活约 6.5 秒；helper 仍走生产等待和 launcher 代码。它证明超 5 秒后不会放弃启动，**没有**证明磁盘 I/O 下 Terminate 真正可使旧进程死亡。 |
| SR-07 **源码修复，Release 仍未验证** | `IdtMain.cpp:812-845` 的 mutex 条件已移除 `restart/crashTry` 例外，helper 等旧 HANDLE signaled 后发这两标志；`#ifdef IDT_RELEASE` 使本轮 Debug 编译/无窗 suite 不触发单实例门。应以最终 Release 构建及隔离启动/互斥观察验证；`-WarnTry` 和 SuperTopComplete 保留的历史例外未由此单元审计。 |

**本轮未发现 SR-05/SR-06 补丁新增的确定 P1/P2。** 已知 SR-04 点击到 Hide 的最长握手等待仍未量测/修复；`CreateThread` 和 helper 同时创建失败、系统无法调度、旧进程内核 I/O 永不 signaled 时，不存在可诚实承诺的绝对退出/重启时间。测试没有真实 UEF、窗口卡死/输入遮挡、旧进程 pending I/O、磁盘持久化恢复、正常产品新进程 ready、中文路径实际进程启动、Win7 SP1+仅 KB2670838 x86/x64 或 Hardware FL11.0/无 FL11.0→WARP 的 DComp/ULW/FLIP 呈现。Win7 本次新增 `CreateThread`、原有 `WaitForSingleObject`/`TerminateProcess` API 静态最低要求满足，但需检查最终导入表与真机运行。不能把这些未测项写成 PASS 或发布就绪。

## 2026-09-29 真实 UEF 最终复审（再次覆盖上文“真实 UEF 未验证”）

本节只读当前 `ShutdownSupervisor.cpp/.h`、`Helper.CrashHandler.cpp`、`IdtMain.cpp` 的最早分派及工程登记，对照 `real-uef-process-restart-test.md`、原始 suite 日志和保留的 dump/report/marker。未修改产品或测试，未自行构建、启动进程或运行 GUI。**本机 Win11 ARM64 的隔离子进程已验证真实 UEF；正式产品 GUI 崩溃重启及数据恢复仍未验证。**

### 已证实的链路与严格边界

- `IdtMain.cpp:639-642` 在配置、单实例、HWND 前分派精确内部参数；`TryRunShutdownSupervisorEarly:1020-1025` 将真实 UEF child 分支放在 C++ `try/catch` 外。`RunRealUefTestChild:839-869` 核显式参数、可继承父进程/ack 句柄、父 PID、测试目录和 copied child 的 `bin/` 路径，设置自动模式 `CrashHandler::SetFlag(1)` 并安装**生产** `CrashHandler::Initialize()`，随后以 `EXCEPTION_NONCONTINUABLE` 调用 `RaiseException(0xE143C001)`。`CrashHandler.cpp:360-384` 自动模式先仲裁退出意图并调用同一生产 `ArmShutdownSupervisor(CrashRestart)`，再走生产 dump/report。它不是 `TerminateProcess` 模拟异常捕获。
- 测试宿主 `RunRealUefProcessTest:873-947` 只从当前含 `InkeysRepo.sln`、`.trellis` 的仓库根创建 `TestResults/release-hardening/` 下的唯一目录；`TestResults`、`release-hardening` 明确拒绝 reparse，复制当前 EXE 到该目录 `bin/Inkeys.exe`。仅以 `STARTUPINFOEX` HANDLE_LIST 继承真实父进程与 ack 两个有限权限句柄；清理 guard 只对本轮自建 child 的 HANDLE 终止。测试结果只尝试删除复制的 EXE，保留本轮 dump/report/marker，不扫描或删未知文件。当前三个产物均位于 `.gitignore:42` 忽略的唯一测试目录，复制 EXE 已不存在。
- 红版完整 `InkeysRepo.sln Debug|ARM64` 构建记录 exit 0，显式隐藏 suite pid 32904 exit 62；旧 child pid 64160 的异常退出码 3779313665 (`0xE143C001`)，授权 1、dump/report 0、early marker 0、restart 0，`real-uef-auto-restart FAIL`，其余含约 15 秒 Close 通过。绿版同配置构建 exit 0，suite pid 41648 exit 0；旧 child pid 58840 同异常退出码，授权 1、dump/report 1、early marker 0、restart 1，新增例及其余例通过。原始日志为 `TestResults/release-hardening/f054-exit-green-uef-red-build-debug-arm64.log`、`real-uef-red-debug-arm64.{stdout,stderr}.log`、`real-uef-green-build-debug-arm64.log`、`real-uef-green-debug-arm64.{stdout,stderr}.log`。suite PID/退出码来自实施者的显式 WaitForExit 记录；本 reviewer 直接核对了日志中的 child 观察值和 PASS/FAIL，不把 stdout 当作独立的 suite 退出码证明。
- 绿版唯一目录 `TestResults/release-hardening/Inkeys Shutdown Test UEF 测试 41648 228043156 0/` 中，marker `restart-56696.txt` 原文为 `56696 58840 2`（新 PID、旧 PID、CrashTry 类型）。同目录 report 为 522 字节，明确 `ProcessId: 58840`、`DumpGenerated: true`、`ExceptionCode: 0xE143C001`，堆栈含 `RaiseException`；dump 为 52,901,322 字节。测试的 `early_restart=0` 是旧 HANDLE 尚未 signaled 时以 20 ms 周期观测 marker，不能仅凭该采样证明零时间窗口；生产 helper `RunSupervisorChild:420-455` 本身先等待**精确旧 HANDLE signaled**才启动唯一测试重启进程。marker 证明测试新进程运行到写标记，不证明正式 `-CrashTry` 完成配置、单实例、HWND 或绘图就绪。
- 核验所用新 Win32 API 的源码最低客户端没有高于 Win7 的明显要求：[InitializeProcThreadAttributeList](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-initializeprocthreadattributelist)、[UpdateProcThreadAttribute/HANDLE_LIST](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute)、[QueryFullProcessImageNameW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-queryfullprocessimagenamew)、[GetProcessId](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocessid) 的微软文档均列 Vista 或更早。`Inkeys.vcxproj` 已登记 `.cpp/.h` 与 CrashHandler module；本机 Debug ARM64 完整 Solution 实际编过。最终 x86/x64 导入表、Release 和 Win7 SP1 **仅** KB2670838 真机运行仍未验证。

### 独立发现与补强建议

| ID / 分级 | 当前实际代码与影响 | 最小建议 / 状态 |
| --- | --- | --- |
| UEF-R1 **P2 测试授权合同缺口，已静态确认** | `RunRealUefTestChild:856-860` 检查 child 所在 `testDirectory/bin`，但对父 EXE 只用 `parentImage.find(L"Inkeys.exe")`；没有证明父图像与 child 复制源为同一构建/字节，且 `IsTestDirectory:229-261` 只凭可创建的 `InkeysRepo.sln`、`.trellis` 标记推断仓库。显式参数、继承句柄/PID 和私有目录足以排除**普通误触发**，却弱于文档声称的“同 EXE/本仓库”授权；同权限构造者可伪造该测试前提。未见远端可达入口或任意提升权限，故不报 P1 可利用漏洞。 | 将测试 child 授权绑定到父图像的精确来源及复制品内容（例如验证父图像和传入源的文件身份、child 与源的字节摘要），并将测试根锚定实际允许的仓库；增加错误父图像/伪仓库负例。正式生产 supervisor child 的 `SameExecutableFile` 校验是独立、较强的合同，不应与本测试 child 混写为同等强度。 |
| UEF-R2 **P2 自动验收门过宽，已静态确认** | `HasCrashArtifact:822-831` 仅看 PID 匹配文件名及非目录，未检查 dump 非零、report 的 `DumpGenerated: true`、PID、异常码；`RunRealUefProcessTest:932-947` 因而可能让空/失败 dump 文件通过测试。当前保留实物已独立核 52.9 MB、报告内容正确，**本次绿结论仍有这份额外证据**；问题是未来回归的自动门可能假绿。 | 对本轮唯一目录内产物检查非零大小和 report 的预期 PID/异常码/成功标志；不要解析或上传 dump 本体。保留原始文件供独立复核。 |
| UEF-R3 **未验证的清理/时序限制** | `MakeUefTestDirectory:805-819` 创建私有目录后，若 CopyFile/继承句柄/启动失败，早退可留下复制 EXE/目录；它们仍在忽略根内，不会递归清理用户文件。`TestChildGuard` 只管本轮 child，不持有 helper/restarted process 的生命周期；250 ms marker 复查和 20 ms early 采样不能证明任意调度下的严格“仅一次且必在旧进程死亡后”动态顺序，静态 helper 句柄等待提供主要合同。 | 将残留明确记为测试工件，失败后仅清理由本轮创建且已核身份的文件；必要时让 marker/测试记录可证明旧 HANDLE 状态。无需把采样局限升级成已确认产品缺陷。 |

本轮没有发现生产 UEF→监督 helper 的新增确定 P1；上述两个 P2 是**测试授权与测试门**问题，已在隔离样本范围内说明实际风险。真实画布/渲染中异常、正式自动重启新进程 ready、已提交 UInk 可见恢复、Release 单实例门、Win7/GPU 矩阵与启动即崩的五分钟抑制仍分别需要证据；不能把本轮隔离绿灯外推为“崩溃链路全部通过”或“可以发布”。
