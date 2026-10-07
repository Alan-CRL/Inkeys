# E02 A/B 启动失败边界实施记录

日期：2026-09-30。Active task：`.trellis/tasks/09-27-integration-and-release-check`。唯一源码写入者：`fatal_startup_hardening`。本记录与 `startup-failure-boundary-harness.md`、已审 `startup-failure-boundary-design*.md` 配套；主会话独占构建、运行、共享账本和 spec。

## HARNESS_READY，尚未修 A

只新增 B 的私有 copied-child 夹具。`PublishFatalStartupFailure`、原 D005/D003 fatal logger 和 Main 的 `ActiveSnapshot.failed` 退场顺序仍是旧代码，以便主会话取得真实红测。E01 已修的 `SetOffSignal` Failed-only guard 保持原样，E03 初始化身份修补没有改动。

源码变动：

- `ShutdownSupervisor.h/.cpp`：新 `--inkeys-internal-startup-failure-child-v1` 和现有 suite 下的 `--startup-failure-only [D004|D005|D003|B002]`。新固定 128 字节 observation 含独立 magic/version/size、site、真实初始化结果、目标 failure/gate、Arm 结果/原截止、意图快照和计时，三架构尺寸/偏移 static_assert；以 Interlocked 发布。与 E01 的 48B packet 严格隔离。
- 新 child 在任何产品初始化前核继承的 parent PID/HANDLE、ack、mapping，父 EXE volume/file-index 同一性，私有 root/bin/EXE 非 reparse、当前镜像同一性。未授权立即非零退出；有效授权才继续真实 `wWinMain`。父进程只在红测上限后终止自己持有的精确 child HANDLE。
- `IdtMain.cpp`：鉴权 child 的 `globalPath` 由 copied EXE 自动指向私有 bin；显式跳过旧/新更新指令、SuperTop、注册表自启、shortcut/DDB、实际 PPT 联动/Office、更新线程及启动业务线程，CrashHandler 不重启。B002 窗口移到屏幕外、默认隐藏。正式产品条件默认 false。
- D004 在真实 `CoInitializeEx` 返回后只对本 child合成 fatal；D005 在实际 embedded PptCOM DLL验证/activation/load 后合成；D003 在真实字体初始化结果后合成；B002 经 `SetBarStartupState(ClientRegistrationFailed)`→真实 Main `ActiveSnapshot`。B002 只证明 Main 消费，不声称 Bar Register 自然失败。
- D005/D003 的 gate 在原 logger 前，D004/B002 的 gate 在原 `ShowStartupMessage` 前。旧顺序目标已发布、意图仍0时会停住；将来 A Arm-first 之后目标在同一点应被产品原15秒监督退场。每个 child 的 durable sentinel 提前 Write+Flush，只证明私有测试文件完好，不能代替 UInk/index 恢复。

隔离限制：D005 本轮只 `LoadLibraryW` 私有嵌入 DLL，不创建 PptCOM 服务或连接外部 Office；若审查证实 DLL 加载自身有外部绑定，则暂停 D005 子场景。`B002` 跑到 Draw3/Window 初始化，已把私有窗口坐标移到屏幕外并跳过常见外部线程；实际自然 Bar Register 失败、Window/Host 内部卡住属于另一项故障注入。无 hold 的原生对话框确认反例尚未自动化，本阶段红测不冒充其通过。

静态检查：`git diff --check` 三个源码 exit0；`IdtMain.cpp` UTF-8 BOM + 全 CRLF；`ShutdownSupervisor.h/.cpp` UTF-8 无 BOM + 全 CRLF。构建/CLI、目标 site 可达性、真实进程退出码：**未验证，等待主会话独占完整 Debug ARM64 build 与私有 red**。建议先 `Build/ARM64/Debug/Inkeys.exe --shutdown-supervisor-tests --startup-failure-only D004`；红测自然 parent exit63，raw packet 应见目标 failure/gate1、acceptedIntent0、arm0、child25s未死亡，随后仅本 child 被父进程清理。若目标没有达到，不把它记为成功红测，先定位私有初始化的先行错误。

后续闸门：主会话确认至少一项真实红灯并发 `GREEN_IMPLEMENT` 后，才改 A 的最小 Arm-first 顺序。随后由主会话构建/绿测和独立 reviewer 审实际差异，按最后源码补复验；此处不预记动态 PASS。C 跨 Host/Window/RTS 的失败清理 lease 尚未批准、未触碰。

## 主会话 D004 实际红测（A 修复前）

主会话完成完整 `InkeysRepo.sln Debug|ARM64`，`TestResults/release-hardening/e02-ui3-harness-red-debug-arm64-build.log` 末尾 0 Error、144 Warning；构建进程退出码由主会话验证记录。主会话以仓库根运行单站点 `--shutdown-supervisor-tests --startup-failure-only D004`，状态文件 `e02-startup-d004-red-debug-arm64.status.txt` 记 `exit=63 pid=14912`；原始 stdout/stderr 同名前缀。

真实目标 child PID 3924：`authorized=1 failure=1 code=53252(0xD004) real_result=0x00000000`（真实 `CoInitializeEx` 成功后夹具合成 fatal），`gate=1 boundary=1`（原 Show 前），`intent_at_gate=0 arm=0 arm_state=0 deadline=0`；25,000ms 后旧 HANDLE 仍活、`exit_code=STILL_ACTIVE(259)`，私有 durable sentinel 保持1。父 suite 仅在红测上限后结束精确 child，测试标 FAIL，证明的是原生产顺序无监督；不能把父清理写成产品成功。错误父身份 child PID27620 exit84、无继承句柄 child PID11512 exit83 均拒绝且相应 case PASS。此处不外推 D005/D003/B002 的可达性或外部副作用，三者还未获准运行。

这些 raw 已由 implementer 读取，未由 implementer 自行构建/运行。下一步仍须主会话显式 `GREEN_IMPLEMENT`，不能跳过 A 前的红灯冻结点。

## A PATCH_READY（主会话 GREEN_IMPLEMENT 后）

主会话基于上述 D004 真红证据允许 A 的最小实施。`IdtMain.cpp::PublishFatalStartupFailure` 第一项业务操作现在是 `SetOffSignal(1)`，然后仍执行原 ReportFailure、350ms 红帧预算、原文本的 Show 和淡出/Stop。早期 D001/D002 改走此 helper，保持 `return 1` 与提示文字；D004 原调用不变，auth-only CoInitialize 成功时的 test COM 释放保留。

D005 在已确认失败的分支第一时间捕获 `GetLastError`，再 `SetOffSignal(1)`，后续 logger 始终记录捕获值；test 目标发布/gate 在正式意图之后。较晚 D002、D003 在 logger 前先 Arm。原 F063 六处 D101/D201/D301/D401/D202/D102 已有 Arm，只将可能阻塞的 logger 或 observer 清理挪到它之后，不改变返回码、提示、Window/COM 资源释放、白板 gate。Main 对 B001/B002 的异步失败在 100ms sleep 后、配置写回等业务工作前先接受意图；末尾 `ActiveSnapshot.failed` 分支也在失败帧等待/消息构造/Show/Preview.Stop 前 Arm（重复 SetOffSignal 由首 CAS 幂等），不丢后续新发布的失败快照。

此时源码尚未 green 构建/运行/独立复审，状态是**已修复待验证**。三文件 `git diff --check` exit0；Main UTF-8 BOM/CRLF，Shutdown h/cpp 无 BOM/CRLF，裸 LF 均0。源码 SHA256：Main `0ddd1aa5b85cede42e970490faf9a5bbc33f30ee42133a181389ff384a663dfe`，Shutdown.cpp `ff08da091d2d620bced0b12ce1f7e8c4119e6fb0e7deb006ed3daa7057821149`，Shutdown.h `f95e3e305d91b738c7886dbdc7eea7d62e74dfd7e36a68714eca51b70c073ab1`。B 的 D005/D003/B002 仍未运行/安全放行，不能仅由 D004 green 外推。C 跨模块失败清理租约未写入。

### A 首轮复审后修正（当前 PATCH_READY 身份）

独立 reviewer 指出原 D005 的提取/校验错误日志先于统一 Arm，而且结束处的 `GetLastError` 可能已被 `CloseHandle` 覆盖。现在 `RecordPptComFailure` 仅在当前主线程保存第一次已确认失败的 Win32 error，并立即 `SetOffSignal(1)`；它在 embedded DLL 发布失败、验证失败、`CreateActCtx` 失败、`ActivateActCtx` 失败和 `LoadLibraryW` 失败各自的返回点调用，均先于该分支的 logger 或后续清理。最终 critical 使用首错误快照；仅测试强制 D005 且真实加载全部成功时，结尾记 synthetic failure 并先 Arm。没有关闭原日志、变动 DLL 来源/激活/释放顺序或更新链。

主循环异步 B001/B002 的早读仍存在理论上“早读之后另一个线程刚发布失败而同步 config.Write 已开始”的时间窗口；不能把早读写成所有 interleaving 的证明。现有源码表明 config.Write 的触发值只在 Bar 成功首帧 `NotifyBarFirstCommittedFrame` 发布，紧邻 `SetBarStartupState(FirstFrameCommitted)`；实际 B002 注册失败位于首帧之前，且状态机拒绝 FirstFrameCommitted 后的低序失败。因此需独立复审这条生产可达性与 B002 私有测试，而不是在 Bar render 回调里未经设计同步调用可能进入业务清理的 `SetOffSignal`。此假设保持待确认；若证实另有可达生产者在 config.Write 前/期间发布 B001/B002，由 root 冻结 StartupPreview/Bar owner 及非阻塞 fatal 入口后单独修补。

当前 Main SHA256 `fe2914e6b1de84c815dea3a2220c0eb04e6c535752e50aeaeb69dd520bda6769`，`git diff --check` exit0，BOM/CRLF 未变。上一 SHA 是首轮复审前历史检查点，不代表当前源码。Main 修补后还没有 green 构建、运行或独立最终复审，不得沿用先前结果。Shutdown h/cpp 的上述 hash 未改。

进一步核实际 helper 后，`PublishEmbeddedResourceFileAtomically` 和 `OpenVerifiedEmbeddedResourceFile` 均只返回 bool/HANDLE，若路径属性、大小、内容 hash 或内部 catch 失败，不承诺保留对应 Win32 `LastError`。两者失败和 auth-only 全成功时的合成失败因此明确锁存 `ERROR_GEN_FAILURE` 通用码；仅 `CreateActCtx`、`ActivateActCtx`、`LoadLibraryW` 这三个真实 Win32 API 失败时原位保存 `GetLastError`。不把 `CloseHandle` 后的陈旧码伪称真实来源。当前 Main SHA256 更新为 `af3293987f2e59fcaa18796f92acd660284e63f190331b1b524c50a169a8974d`；`git diff --check` exit0、BOM/CRLF 不变，仍是待 green/独立复审状态。

## 主会话 D004 最终源码 green

主会话在 Main 冻结为 SHA256 `af3293987f2e59fcaa18796f92acd660284e63f190331b1b524c50a169a8974d` 后重新完整构建 `InkeysRepo.sln Debug|ARM64`，日志 `TestResults/release-hardening/e02-ui3-final-source-debug-arm64-build.log` 末尾 0 Error（4 Warning）；构建实际进程退出码由主会话验证记录。前一份 `e02-ui3-green-debug-arm64-build.log` 与 Main 最终改动交错，只可作诊断，**不是本源身份的验证**。

隔离 D004 green 的 `e02-startup-d004-green-debug-arm64.status.txt` 记 parent PID29656 自然 `exit=0`；stdout 中目标 child PID30200：`authorized=1 failure=1 code=0xD004 real_result=S_OK`，Show前 `gate=1 boundary=1`，`intent_at_gate=1 arm=1 arm_state=2`，原绝对 `deadline_tick=4747578`，精确旧 HANDLE 已自然 signaled，`exit_code=3779264534 (0xE1430016)`，约 15172ms；私有 durable sentinel=1。测试 case PASS，parent未强杀作为绿色。错误 parent PID27356 exit84、无继承句柄 PID3632 exit83，两个拒绝用例 PASS。以上 raw 由 implementer 读取，未自行执行程序。

此 green 只关闭 D004 的真实 wWinMain 失败提示先 Arm 验证，不等于 D005/D003/B002 已运行、不等于真实 UI 交互或 Win7 验证，也不覆盖未批准的 C 启动失败清理租约。D005 修补后的独立复审、完整 supervisor/strict Headless/PptCOM/Release 矩阵和无 hold 自然退出反例仍以主会话后续记录为准。

独立 checker 已在 `startup-failure-boundary-code-review.md` 对最终 D005 源码给出静态 GREEN，并将 B001/B002 与该处 `config.Write()` 的竞态假设排除：同步写的宽度值只由 Bar 首次完整成功帧发布，B001 的 WindowMissing 与 B002 的 Register 失败均在首帧前；`FirstFrameCommitted` 后单调状态拒绝晚到失败。此前本报告的“理论交错待确认”保留为历史分析，不再列该生产调用链的已知缺陷。其它未调查的异步故障不由此结论覆盖。
