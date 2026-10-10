# 手动/不自动重启模式的 UEF 报告阶段截止

设计与红证时间：2026-09-29。状态：**红测已确认失败，绿候选待构建/运行**。本单元只碰 `Helper.CrashHandler.cpp/.cppm` 与现有显式无 GUI `ShutdownSupervisor.cpp/.h` 测试；不改 IdtMain、UI、Draw3、Storage、工程/工具链。独立审查 `win32-uef-dump-independent-review.md` 的静态风险：默认 TeachingSafetyMode=0 在 dump/report 完成且用户点 OK 前不占退出意图、也没有 15 秒监督；微软 [MiniDumpWriteDump 文档](https://learn.microsoft.com/en-us/windows/win32/api/minidumpapiset/nf-minidumpapiset-minidumpwritedump)提醒崩溃进程内写 dump 可能因 loader 状态死锁。模式 1 已有外部 CrashRestart helper，在报告前建立且最多一次拉起，不能为它再启第二个 watchdog。

## 状态机与退出语义

| 模式/阶段 | 意图槽 | 报告时钟 | 结束行为 |
| --- | --- | --- | --- |
| mode 1 自动 | CAS0→3 后外部 helper 已 arm | 不额外建立 | 原 helper 到 15 秒只杀精确旧进程，旧 death 后唯一 CrashTry；报告可能因到期未写完。 |
| mode 0 手动，报告写入中 | 保持 0；其他线程正式 Close/Restart 仍可争取自己的意图 | 低层独立 Win32-only 自身截止，15 秒；线程 ready 后才进入可能无界的文件/DbgHelp/符号处理 | 报告线程在期限前完成则取消；卡住则 `TerminateProcess(GetCurrentProcess(), 专用码)`，不弹框、不自动重启。若受控 Restart 已先争槽，其现有外部 helper仍可在旧 death 后按该正式请求重启。 |
| mode 0，报告完成→人工确认 | 仍保持 0，先取消报告时钟再显示框 | 已取消；等待用户选择不计作报告挂起 | Cancel/关闭不占槽、不重启；OK 后才 CAS0→3 并建立 CrashRestart helper。若正式 Close/Restart 先到，迟到 OK 不重复拉起。 |
| mode 2/3 不自动重启 | 保持 0 | 与 mode 0 相同，只保护报告阶段 | 报告结束取消时钟；mode 2 返回已处理异常，mode 3 保持原 CONTINUE_SEARCH。 |

报告时钟只使用 `CreateEventW`、`CreateThread`、`WaitForSingleObject`、`GetTickCount64`、`SetEvent`、`TerminateProcess(GetCurrentProcess())`，不调用 CRT、文件、业务对象、COM、渲染线程或锁。UEF 最先创建 cancel/ready event 与 deadline 线程，bounded 等其 ready（例如 500 ms）；线程启动失败或 ready 超时就跳过**可能无界的报告路径**，仍按 mode0 原确认语义或 mode2/3 原异常返回，不让一个没有监督的 dump 调用卡住。全局静态原子/Win32 HANDLE 保证线程延迟启动后不会引用已退出的栈对象；如线程已创建但 ready 超时，先 signaled cancel，可能留两个事件 HANDLE 到崩溃进程退出（一次性门闩约束最多一组）。正常报告完成或任一早退都由局部 RAII guard `SetEvent(cancel)`，明确早于 mode0 MessageBox。若整个进程被暂停、内核不调度、CreateThread 与已有退出 helper 都不可用，不能宣称物理时间绝对保证；因此失败时必须不进入重型报告，而不能伪造 PASS。用户手动思考/选择时间不受这 15 秒报告时钟限制。

## 文件事务与半成品

当前 `GenerateMiniDump` 对最终 `YYYYMMDD_PID.dmp` 用 CREATE_ALWAYS。若报告 watchdog 在写到一半时硬终止，最终路径会留下看似有效的部分 dump，甚至覆盖同名旧文件。改为仅在本次同 basename 的 `.dm_` 精确新建临时文件，现有高保真→有条件 `MiniDumpNormal` 只在该 HANDLE 上重试；成功且非零、Flush、Close 后以**同目录无替换**的 `MoveFileExW(MOVEFILE_WRITE_THROUGH)` 发布最终 `.dmp`。报告 `.txt` 使用对应 `.tx_` pending 并检查完整 Write/Flush/Close，再无替换发布。`.dm_/.tx_` 与正式扩展名等长，避免在 Win7 深目录比原路径额外越过 MAX_PATH；CREATE_NEW 碰到残片则失败闭合，绝不覆盖同名已有文件。失败只删本次 pending；被硬杀的 pending 留在忽略/Crash 目录，但最终 `.dmp/.txt` 不会因本次半写而冒充有效。已存在的最终同名有效文件不覆盖；发布失败明确报告失败。不能扫描或删除未知文件。

## 隔离红→绿验证

沿现有 `--shutdown-supervisor-tests` 只创建其自己的 TestResults 唯一 root/bin/Inkeys.exe。新增 **仅继承父 HANDLE/期望 PID/私有 root 验证通过**的测试模式；复制 child 安装真实 `CrashHandler::Initialize`，SetFlag(0)，SetErrorMode 抑制 OS 弹框，并在生产 `GenerateMiniDump` 已创建本轮文件后启用 test-only 挂起钩子（先写少量 `PARTIAL`，然后 Win32 无限等待）。普通用户运行内部参数但缺继承句柄会在 RaiseException 前返回；默认生产没有 hook 或额外上传。

红版实际：完整 `InkeysRepo.sln Debug|ARM64` Build exit0；显式隐藏 suite pid58524 exit62，原 auto UEF child pid3716 仍生成高保真 dump 52,888,996字节/report/restart1并 PASS。新增 mode0 child pid66424 经继承父HANDLE/PID授权后触发真实 UEF，在生产 `GenerateMiniDump` 最终路径写7字节 `PARTIAL` 后挂起；25秒测试上限仍未死、无报告或restart，宿主只以自己持有的 child HANDLE终止，子例 FAIL。`exit_code=0` 是观察时 child 未死留下的测试输出占位，**不是正常退出码**。唯一私有根 `TestResults/release-hardening/Inkeys Shutdown Test UEF 测试 58524 243093687 0/` 保留 `bin/Inkeys/Crash/20260929_041702_66424.dmp` 7字节残片、无 `.txt`，证明发布文件事务缺口。原始 `manual-uef-report-red-build-debug-arm64.log` 与 `manual-uef-report-red-debug-arm64.{stdout,stderr}.log` 保留，无残留测试 Inkeys 进程。

绿候选首轮完整 Debug|ARM64 Solution Build exit0；显式隐藏 suite pid53424 exit0。mode0挂起 child pid24832 与 mode2挂起 child pid36212 都在约15秒以 `0xE1430017` 结束，正式 `.dmp/.txt` 为0、非零 pending为1、restart0；mode0正常报告后无GUI等待16秒 child pid42960 以原异常码结束、有效dump52,858,558字节/report573字节、pending0/restart0，证明确认前取消时钟；mode1 auto child pid36004 仍保留高保真dump52,889,018字节/report/restart1，旧15秒Close子例约14938ms。原始 `manual-uef-report-green-build-debug-arm64.log` 和 `manual-uef-report-green-debug-arm64.{stdout,stderr}.log` 保留。**首轮绿码使用较长的 `.dmp.pending.*` pending 名；静态复审后改为同长度 `.dm_/.tx_` 以保 Win7深目录边界，因此旧绿码不能当最终源码证据。**

等长扩展名最后补丁后的完整 `InkeysRepo.sln Debug|ARM64` Build exit0；显式隐藏 suite pid60104 exit0。mode0挂起 old pid24152 和 mode2挂起 old pid10080 均以 `0xE1430017` 退出，`dump=0/report=0/pending_dump=1/restart_count=0`，唯一私有根分别保留 7 字节 `20260929_043458_24152.dm_` 与 `20260929_043530_10080.dm_`，**无同 PID 正式 `.dmp/.txt`**。mode0报告正常完成后无 GUI 16 秒确认等待 old pid66548 按原测试异常码退出，正式高保真 dump 52,858,574 字节/report573字节，pending0/restart0；mode1 auto old pid36096 高保真 dump52,897,210字节/report573字节、restart1；原 15 秒 Close 实测 marker 后14953ms。完整日志 `manual-uef-report-final-build-debug-arm64.log` 与 `manual-uef-report-final-debug-arm64.{stdout,stderr}.log`。测试结束未见遗留 Inkeys/POWERPNT/MSBuild 进程，复制 EXE已按确切自建路径删除；私有残片、dump/report、marker只保留在忽略的 TestResults 供复核。

**当前自动化结论仅限本机无 GUI ARM64 Debug 的真实 UEF 报告阶段。** 尚待此最终源码的 Release ARM64/Win32/x64 构建与适用suite回归；真 Windows 7 SP1+仅 KB2670838、正式 Office/绘图中故障、真实用户 OK/Cancel/对话框被 UI 线程阻塞、启动阶段 loader lock、设备断电/FailFast/外部强杀与崩溃后可见恢复均未测。Ready/线程创建失败时跳过重型报告而保留手动选择；整机暂停或 Win32 内核不调度时不能保证物理15秒。两个报告事件 HANDLE 在崩溃进程存活期间保留，以避免延迟线程引用已关闭对象；一次性 UEF 门闩把该残留限制为每进程一组。挂起故障的 `.dm_` 残片可能占用磁盘且含私有内存，未获所有权证明前不自动扫描删除或上传。真实用户 OK/Cancel仍需手工 GUI。FailFast、外部强杀、断电不走普通 UEF，不能借测试结果宣称支持。

### 静态追出的磁盘满一致性风险（待处理/复验）

`GenerateMiniDump` 已成功发布本轮正式 `.dmp` 后，若 `WriteCrashReportTxt` 因磁盘满失败，当前 UEF 仍会调用 `CleanupOldCrashFiles(crashDir, 0)`。该函数枚举并删除 Crash 目录的全部 `.dmp` 配对，**包含刚发布的本轮 dump**，但其后重试 `.txt` 时继续传入旧 `dumpGenerated=true`；若报告重试成功，便可能在 `DumpGenerated:true` 的报告里指向已删除文件。这个条件由当前调用链直接确认，尚未做磁盘满隔离注入；需在清理旧文件时保护本轮 `dumpFilePath`、并故障复验，不能把正常磁盘的绿灯扩展为磁盘满正确。主 agent 已获独立通知，本文件暂保持风险状态，不虚构 PASS。

故障注入设计：仅在已经通过继承父 HANDLE/PID 与唯一 TestResults copied EXE 验真的真实 UEF child 设置 `ReportDiskFullOnce` 测试模式。生产 `WriteCrashReportTxt` 在**本轮 dump 已发布后**第一次被调用时返回 `ERROR_DISK_FULL`，第二次照真实代码写；没有填满磁盘或修改真实配置。红版原 `CleanupOldCrashFiles(crashDir,0)` 会把本轮刚发布的 `.dmp` 删除，报告第二次可成功却写 `DumpGenerated:true`，私有测试门要求非零 `MDMP` 与报告旧 PID/异常码/`DumpGenerated:true` 同时成立，因此必须红。绿版只给清理函数加可选 `protectedDump` 精确对象，并只在 `txt` 失败分支传本轮 `dumpFilePath`；函数遍历同一 Crash 目录时大小写不敏感比较 basename，跳过该文件，其余旧 pair 按原排序/keepPairs 处理，不扩大删除范围。绿灯需以同一真实 UEF child 证明 dump/report/唯一 restart marker 都有效；Win7真磁盘满/权限错误仍需人工，注入不能冒充真正存储满。

红版完整 `InkeysRepo.sln Debug|ARM64` Build exit0；显式隐藏 suite pid36516 exit62。私有真实 UEF child old pid44144 的 `RaiseException(0xE143C001)` 后出现 `report=1/restart_count=1`、但 `dump=0`；573字节 `20260929_045644_44144.txt` 明写 `DumpGenerated: true` 并引用已被清理删掉的同 PID `.dmp`。唯一私有目录为 `TestResults/release-hardening/Inkeys Shutdown Test UEF 测试 36516 245475937 0/`，只留本轮报告与测试 marker；其它原套件均 PASS。原始 `manual-uef-diskfull-red-build-debug-arm64.log`、`manual-uef-diskfull-red-debug-arm64.{stdout,stderr}.log` 留存。此注入证明当前调用链的不一致，不证明真实磁盘被填满。

最小绿修：`CleanupOldCrashFiles` 增可选 `protectedDump`，仅从**报告写失败**分支传入本轮 `dumpFilePath`；同一目录枚举 `.dmp` 时大小写不敏感排除该 basename，其他旧 pair清理完全照原实现。Office隔离烟测结束后完整 `InkeysRepo.sln Debug|ARM64` Build exit0；显式隐藏 suite pid43872 exit0。私有真实 UEF child old pid48420 触发同一首次 `ERROR_DISK_FULL`→旧pair清理→二次正常报告路径，`dump=1/dump_bytes=52897226`、`report=1/report_bytes=572`、`restart_count=1`，子例 PASS；唯一 `TestResults/release-hardening/Inkeys Shutdown Test UEF 测试 43872 246881578 0/` 同时保留 `20260929_052010_48420.dmp` 与同名 `.txt`，报告旧 PID、`ExceptionCode: 0xE143C001`、`DumpGenerated: true` 与真实 MDMP 一致。其余 mode0/2截止、手动报告后取消、auto UEF及15秒Close均 PASS。原始 `manual-uef-diskfull-green-build-debug-arm64.log`、`manual-uef-diskfull-green-debug-arm64.{stdout,stderr}.log` 留存，红版失败与错误报告仍保留。**这是本机私有进程中的模拟报告磁盘满已验证通过；真正磁盘满、无权限与Win7仍未验证**，不能凭该故障注入宣称所有存储失败可恢复。

验收需串行完整 `InkeysRepo.sln Debug|ARM64` 主 Solution build 与显式 no-GUI suite 红→绿，再按用户发布矩阵在 Release ARM64/Win32/x64 尽可得复验。Win7 SP1+仅 KB2670838 真机 UEF/DbgHelp/HANDLE/API 仍需要人工；FLIP_SEQUENTIAL、DComp/ULW、HARDWARE/WARP 与此文件链无关且不可由本测试推导。

### mode 1 自动意图竞争的条件窗口（新增独立复审）

当前 `automaticDeadlineReady=true` 默认值只有 mode1 自己 CAS0→3 成功时才可能变成 Arm 失败；若另一个线程已在 `SetOffSignal` 做 CAS0→1/2，却尚未抵达 `ArmShutdownSupervisor`，mode1 UEF 的 CAS 会失败、默认仍 true，于是它直接进入可能无界的 DbgHelp 报告。受控线程的外部 helper 和自身兜底**此时均可能尚未创建**。这是短竞态但真实调用顺序可达，不能以 mode1 独立 UEF 绿证覆盖。

红测只在**已继承父HANDLE/期望PID和唯一TestResults根验真后的 copied child**中，先对现有生产 `GetOffSignalInteropPointer()` 做 `InterlockedCompareExchange(0→1)`，模拟正式 Close 意图已占但 Arm 未到；不调用 `SetOffSignal`，因此没有外部 helper。随后用 mode1 真实 `CrashHandler::Initialize`/`RaiseException`，并在同一生产 `GenerateMiniDump` 测试钩子写7字节 `.dm_` 后挂起。预期旧版25秒测试上限仍活、仅宿主自己持有的 child HANDLE 可清理，suite 红；不启动正式 GUI，不碰用户进程。

最小绿改只调整 UEF 报告监督选择：**mode1 CAS赢且外部 Arm成功/自身fallback成功**继续沿现有单一 CrashRestart监督；mode1 CAS输则与mode0/2/3一样尝试本地可取消的报告deadline，ready失败跳过重型报告，既不改 `offSignalInterop` 也不建第二个 restart helper。控制线程若稍后为原 Close/Restart arm helper，可与本地仅自杀报告时钟并存；旧死亡后是否由原控制意图重启由唯一原 helper 决定。绿测要求旧 copied child约15秒以专用码`0xE1430017`退出、只有 `.dm_` 临时残片、正式dump/report0、restart marker0；已有 mode1 CAS赢、mode0/2、diskfull和15秒Close必须重跑。真实 GUI 线程交错/Win7仍未测。

红版完整 `InkeysRepo.sln Debug|ARM64` Build exit0；显式隐藏 suite pid45640 exit62。私有 copied child old pid45684 在现有 `offSignalInterop` 做 CAS0→1 而不Arm，随后 mode1 真实 UEF 在生产 `GenerateMiniDump` 钩子写7字节 `.dm_` 后挂住；25秒测试上限仍未自然退出，宿主只终止自己持有的精确 child HANDLE，观察行 `exit_code=0` 是未死占位，不代表正常返回。唯一 `TestResults/release-hardening/Inkeys Shutdown Test UEF 测试 45640 248340328 0/` 留 `20260929_054429_45684.dm_` 7字节，正式dump/report0、restart0；子例 FAIL，其它既有子例均 PASS。原始 `manual-uef-auto-lost-red-build-debug-arm64.log` 与 `manual-uef-auto-lost-red-debug-arm64.{stdout,stderr}.log` 留存。该用例只放大 CAS已赢而Arm未到的窗口，**不模拟完整多线程 SetOffSignal/窗口/GUI交接**。

绿修只把报告监督选择改成 `needsLocalReportDeadline = crashMode != 1 || !earlyCrashIntent`；CAS输时使用已有可取消报告截止，CAS赢仍沿唯一原外部helper。完整 `InkeysRepo.sln Debug|ARM64` Build exit0；显式隐藏 suite pid18676 exit0。目标私有旧 child pid24648 预先 CAS0→1 且不Arm，随后 mode1 真 UEF 在生产 dump 钩子挂起，现于约15秒以专用 `0xE1430017` 退出、正式dump/report0、非零 `.dm_` pending1、restart0；唯一私有根 `TestResults/release-hardening/Inkeys Shutdown Test UEF 测试 18676 249146218 0/` 留 `20260929_055755_24648.dm_` 7字节，复制EXE按确切自建路径清理。mode1 CAS赢旧pid56132仍高保真dump52,884,938字节/report/restart1；mode0/2、报告首次磁盘满一致性、真15秒Close14938ms都 PASS。原始 `manual-uef-auto-lost-green-build-debug-arm64.log` 与 `manual-uef-auto-lost-green-debug-arm64.{stdout,stderr}.log` 留存。**状态仅为本机无GUI窗口放大故障注入已验证通过；真实多线程 SetOffSignal→helper交接、正式GUI、Win7仍未验证。**
