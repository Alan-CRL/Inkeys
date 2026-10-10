# F-015 / F-016 与启动更新半部独立复审

审查日期：2026-09-27。只读核当前 `IdtConfiguration.cpp`、`Setting.cpp`、`Helper.CrashHandler.cpp`、`Window.Legacy.cpp`、`IdtMain.cpp` diff，追 `Helper.CrashHandler.cppm`、更新生产者 `Net.Update.cpp`、`UpdatePathSafety.h` 和现有无窗测试日志。本轮未启动应用/GUI、未注入异常、未编译。行号对应审查时工作区；其它 agent 正在修改更新链，最终交付需再核最后 diff。

## 结论与优先级

| 项目 | 代码审查结论 | 未验证范围 |
| --- | --- | --- |
| F-015 TeachingSafetyMode 越界 | 生产配置和 Setting UI 两层已收口；有效 0..3 语义不变，源码问题为已修复待验证 | 坏配置实际打开 Setting、Win7/GUI 未运行 |
| F-016 UEF 卸载/二次重启 | `PreviousFilter == nullptr` 可恢复；5 分钟内 `-CrashTry` 不再拉起；一次 handler 后门闩不重置。CR-1 在后续 diff 已修，见文末复审 | 真未处理异常、退出/重启交接、五分钟计时均未运行 |
| 更新 `update.json` | 两阶段指令有 64 KiB/深度 32 与词法路径/hash 约束；第一阶段保留旧 EXE 直到 stage 验证并备份，启动失败尝试 rollback；第二阶段不再递归删 installer | 真文件替换、断电/磁盘满/ACL、Win7、实际新进程 ready 未运行；F-014 来源认证仍未完成 |

## F-015：配置与设置下标

- `IdtConfiguration.cpp:148–155` 只将 JSON int 0..3 写入 `setlist.regularSetting.teachingSafetyMode`；无效值保持 `IdtMain.cpp:1307` 已设的默认 0。`Setting.cpp:1232–1237` 在本地快照再次 `std::clamp(...load(),0,3)`，再于 `:3657` 访问四元素选项向量。因此手改负数/大整数不会再作为 `vec[]` 下标。有效值未映射或重命名，现有四个选项语义不变；设置 UI 选择仍把 0..3 写回原字段。
- 此处没有把无效 JSON 强制持久化为 0；下次原有写配置过程会按当前内存值写回，保持最小改动。`SetFlag` 读取的也是已受约束字段。当前 headless 测试没有真正加载坏 `deploy.json` 并打开 Setting，故运行状态仍是待人工/隔离 GUI。

## F-016：过滤器、重入与 CrashTry

- `CrashHandler::Initialize:37–45` 用 `g_filterRegistrationMutex` 和独立 `g_filterInstalled` 使注册幂等；`Shutdown:60–68` 在同一锁下不论 `PreviousFilter` 是否为 null 都调用 `SetUnhandledExceptionFilter(PreviousFilter)`，解决 H0 “没有前 filter 就不卸载”的条件。`CloseProgram/RestartProgram` 在 `Helper.CrashHandler.cppm:56–70` 先隐藏窗口，再 Shutdown，后发布退出信号；`Window.Legacy.cpp` 删除首帧的 `IsSecond(false)`，不再过早放开崩溃循环保护。未发现本次修改在 UEF 回调中持注册 mutex 或反向锁顺序。
- `IdtMain.cpp:423` 识别 `-CrashTry` 时调用 `IsSecond(true)`；新 `g_secondCrashStartTick` 于 `IsSecond:52–57` 取 `GetTickCount64()`。`UnhandledExceptionHandler:344–358` 在该进程连续运行不足五分钟时返回 `EXCEPTION_EXECUTE_HANDLER`，不再拉起；五分钟后允许按当前用户模式尝试一次。`g_isGeneratingDump` 在到达正常 handler 结尾时不再清零 (`:496`)，同一旧进程的并发后续异常不能第二次发起子进程。无法取得当前路径时的旧早退分支仍把门闩复位并 `CONTINUE_SEARCH`，该分支不会启动子进程，因此未证实会重复拉起。
- 过滤器安装前的初始化故障、CRT purecall/invalid-parameter、`std::terminate`、FailFast、强杀、断电仍没有本补丁保证的自动重启。ShellExecute 返回值/new PID/新进程 ready 未核，`-CrashTry` 仍可作为外部命令行标志绕过 Release 条件单实例门；这是现有交接合同风险，不能把“5 分钟抑制”写成整个崩溃重启链通过。崩溃 dump 依旧在损坏进程内生成，已提交 UInk 完整与新进程可见恢复也必须分别验收。

### CR-1 [P2，原 diff 静态确认；后续已修复待验证] 受控启动失败退出未卸载 UEF

`CrashHandler::Shutdown()` 的生产调用只在 `CloseProgram/RestartProgram` (`Helper.CrashHandler.cppm:61,68`)；`IdtMain.cpp:1972,2057,2068,2085,2103,2129,2207,2224` 的启动失败、smoke 和主循环 fatal 路径却直接 `SetOffSignal(1)` 并排空资源。过滤器在 `IdtMain.cpp:427` 已安装，若 `TeachingSafetyMode=1` 且这些**受控失败的清理期**另有未处理异常，仍可能启动 `-CrashTry`，违背“正常/受控退出不误触崩溃重启”。这不是 null 前过滤器恢复 API 本身的问题，而是其调用覆盖不足。最小修正是在所有共享退出清理开始处幂等 `CrashHandler::Shutdown()`，并复核 `CloseProgram/RestartProgram` 先卸载的既有顺序；真实故障交错未运行。

## 两阶段更新、删除与旧版本

- 第一阶段（下载的候选 EXE 自身）`IdtMain.cpp:503–565` 在读 `update.json` 前限制大小为 1..64 KiB，JsonCpp 深度 32，检验 `representation`/`old_name` 为单个合法 `.exe` 名与 MD5/SHA-256 形式。当前 EXE 的实际哈希须等于指令，再等 `old_name` 进程离开。`main_path` 下 `IKU` 临时文件先从候选 EXE 复制并再次核两哈希 (`:603–632`)，旧 `Inkeys.exe` 在 stage 完成前不删除；若目标存在，先在同卷 `IKB` 临时文件备份 (`:633–649`)，再 `MoveFileExW(...REPLACE_EXISTING|WRITE_THROUGH)` (`:650–655`)。ShellExecute 同步报告失败时尝试以备份回滚 (`:657–671`)，回滚失败明确提示并保留 `IKB` 供人工恢复。成功时保留备份；这保存旧版本，但 ShellExecute 的 `>32` 只证明 Shell 接受启动请求，不能证明新进程已初始化/首帧成功。
- 第二阶段（现有安装 EXE）`IdtMain.cpp:704–762` 对 `installer/update.json` 同样限制大小/深度、词法相对 EXE 路径与哈希。格式/校验失败时 `:811–815` 只删该指令文件，不再 `remove_all(installer)`，不会顺带清未知下载物或用户文件。`UpdatePathSafety.h` 拒绝斜杠/盘符/ADS/设备名/尾点和 `../`；这只是路径字符串边界，没有证明 reparse point、目录 ACL 或安装权限隔离。
- 审查期间主 agent 又修了第一阶段失败分支：`IdtMain.cpp:684–697` 不再执行可写 `update.json` 自报的 `old_name`，只尝试固定主目录 `Inkeys.exe`、其次 `智绘教.exe`；二者均不存在则退出并保留文件，避免恶意安全 basename 导致任意主目录 EXE 被执行。**兼容例外**：若旧程序使用自定义 EXE 名，失败时不再自动拉起它；若同目录有标准名程序，可能拉起标准名而非刚才的自定义程序。旧文件本身未由失败路径删除，需人工入口/发布说明明确这一取舍。
- **仍须处理的安全来源边界（既有 F-014）**：两阶段 JSON 中的 hash 与候选包来自同一更新元数据/可写指令，重新核 MD5/SHA-256 证明相对指令的一致性，不能独立证明官方来源。签名/固定发布者身份与下载 HTTPS/ZIP 防护必须按 F-014 单列，不由本次 stage/backup 合同替代。stage 复制、哈希与后续按路径 MoveFileEx 之间也无句柄级防替换证明；本审查未验证安装目录 ACL、reparse 或本地竞争者的实际可达性，不将其升级为已复现漏洞。
- **容量/故障边界**：`GetTempFileNameW` 受 MAX_PATH/目录权限限制时安全返回，旧 EXE 未动；`copy_file` 或 stage hash 失败清理本次 `IKU`，旧 EXE 未动。`MoveFileExW` 失败分支会删本次 stage 和备份 (`:650–655`)，按 API 正常失败合同旧 target 应保持；但没有实际磁盘/电源故障注入可证明所有异常边界或备份 durable。第一次替换成功后如果 child 启动后立即崩溃，备份虽保留，却不会自动回滚；这是明确的“ShellExecute 接受”与“健康新版本”验收差别。`IKB*.tmp` 成功后长期保留，需在交付中告知其来源；本任务未授权清理这些备份。

## 验证证据与状态

- `TestResults/release-hardening/work4-update-json-final-debug-arm64.log` 记完整 `InkeysRepo.sln` Debug|ARM64 `Build succeeded`、6 warnings/0 errors；对应的 `work4-update-json-final-headless-debug-arm64.log` 末尾 `PASS animation correctness`，且 `InkeysHeadlessTests/update_security_tests.cpp` 已被 headless 入口调用。这些测试只覆盖 `UpdatePathSafety.h` 的词法函数、HTTPS URL 基础逻辑等；不执行 `IdtMain` 两阶段文件替换、真实 UEF 或 Setting GUI。
- 最后一次第一阶段失败分支改动发生于上述两个日志之后；**这些 PASS 不能直接归给当前最终 diff**。主 agent 须在这一修补后至少重跑完整 Debug|ARM64 Solution 和 `--no-window`，并另保留真机/故障注入门禁。`git diff --check` 本审查退出码 0。
- 真实正常退出、主动重启、受支持 UEF、五分钟二次崩溃、同卷备份回滚、Win7 SP1+仅 KB2670838、UIAccess/权限和已提交 UInk 新进程可见恢复均未由本 reviewer 运行；不得由编译或纯函数测试标 PASS。

## 后续两处修正的独立复核（最新状态）

1. **CR-1 源码已修复待验证。** `IdtMain.cpp:245–253` 的 `SetOffSignal(signal != 0)` 现先调用幂等 `CrashHandler::Shutdown()`，再发布 interop/C++ 退出状态并唤醒 RenderPipeline。这样 `IdtMain` 启动失败、smoke、主循环 fatal 及 `Window.Legacy.cpp:145` 的直接调用都覆盖；`CloseProgram/RestartProgram` 原先的 Shutdown 调用会再次进入，但 `g_filterInstalled=false` 时立即返回。`SetOffSignal` 不在 UEF handler 内调用；新调用发生在发布 `offSignal` 前，使用独立注册 mutex，当前调用链未见该 mutex 与 Window/Render 的反向锁顺序。早于 `CrashHandler::Initialize()` 的 SetOffSignal 会因未安装直接返回，不会恢复未知 filter。真实清理交错和 Win7 未测。
2. **第二阶段 update.json 原位截断风险已修复待验证。** `IdtMain.cpp:791–838` 先校验旧 EXE 名、序列化后的指令大小和 installer 为非 reparse 目录；以同目录 `IKJ` 临时文件写入、`FlushFileBuffers`、关闭句柄，再 `MoveFileExW(...REPLACE_EXISTING|WRITE_THROUGH)` 发布。写/flush/移动失败只删本次临时文件，旧 `installer/update.json` 不被原位截断；旧 EXE 未动。后续只在成功发布后尝试 `ShellExecuteW` 候选；无效指令不再递归清理 installer。按路径检查与 GetTempFileName→CreateFile/MoveFileEx 之间仍无句柄级 reparse/替换竞态证明，需按 F-014 的本地目录权限模型判断实际攻击前提。此改动的成功/失败文件事务尚无专用隔离测试。
3. **验证时序**：当前 `IdtMain.cpp` 修改时间晚于已读 `work4-update-json-final-debug-arm64.log` 与对应 headless 日志，二者不能作为这两处最新修正的构建/运行证据。`git diff --check` 本轮退出码 0；最后一次修补后须按项目规则复跑适用构建/无窗测试，并将真实 UEF、更新替换和 Win7 保持人工/隔离验证状态。
