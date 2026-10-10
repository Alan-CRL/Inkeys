# F-054 PPT Current Load 有界重试独立复审

日期：2026-09-28。只读审最新 `Draw3.DrawingController.cpp/.cppm` 实际代码、`ppt-current-load-retry-f054.md`、F041 Controller/Storage 冻结合同与原始红绿日志。未改产品/测试/父账本，未执行新构建、GUI 或性能采样。本报告是 **F054 退出边界修补前** 的结论；父任务刚取得的 Release|ARM64 Rebuild 也属于这一补丁前版本，修补后须重验。

## 发现

### F054-R1｜P2 条件性退出时序缺陷：退出屏障后仍可能新发 Current Load

- 生产可达顺序：`PrepareExitAutoSave` 在 `DrawingController.cpp:7984-7989` 只捕获退出快照并回报 command，没有取消 `currentLoadRetry`。`Run` 对 `window_.ExitRequested()` 的检查及 Cancel 在 `8634-8637`，早于本轮 `processCanvasCommands`；该批次消费 exit 命令后，`8721-8741` 的 `active.empty()` 安全点仍可在已到期时调用 `TrySubmit`。Host 直到退出屏障 ACK 后才继续停机时，这个时间窗会把新的只读 Load 放入同一 persistence worker，延长正常 `CloseAndDrain`；若读 I/O 卡住，最终由独立 15 秒 supervisor 兜底。此路径不会直接覆写 UInk，但不满足“Exit 取消旧重试计划”。
- 仅在 exit command 中 `Cancel()` 仍不够：`processCanvasCommands` 在 `7505-7507` 连续消费命令；exit barrier 后若又消费旧 Current Load 的 `IoError` completion，`7715-7732` 可再次 `OnIoError` 排期，再于同帧 `TrySubmit`。当前 no-HWND probe `2924-2928` 是直接调用 `Cancel()`，未经过生产 `PrepareExitAutoSave` 分支，因此不覆盖这个顺序。
- 最小修正：在 Run 内维护被正式 `PrepareExitAutoSave` 设置的 `exitAutoSavePrepared` 状态，设置时 Cancel；Current completion 只有该状态为 false 才可安排 IoError 重试，重试安全点与初次/显式 Current Submit 也核 `!exitAutoSavePrepared && !window_.ExitRequested()`。停止路径无须新线程或同步 I/O。生产共用 command 探针先红再绿：安排已到期重试→处理 Exit barrier→随后旧 IoError completion→检查总 SubmitLoad 计数不增、退出 ACK 恰好一次；在 barrier 前的普通同目标失败仍保留原四次退避合同。

### F054-R2｜P2 发布可观察性/恢复边界，未证明可自动消除

四次 IoError 之后 `PresentationCurrentLoadRetry::OnIoError`（`737-747`）只写一次 `manual_required` 到 stderr 并停自动计划；`SourceChanged/foreign/Invalid` 走 `OnTerminalFailure`（`7718-7732`）。Controller 正确保持 `persistenceInitialized=false` 和物理输入/画布命令 gate，防止错误地用空槽覆盖旧数据。用户停留在同一放映目标时，Host `Draw3.Host.cpp:900-911` 不会自发重发相同 targetRevision；目前只有日志，没有用户可见的错误与明确的同目标重试操作证据。它对永久磁盘/冲突错误仍表现为“画布不能写”。这不是允许无条件解门的理由；发布门禁需把该状态作为可观察故障或明确人工处置项，并验证新的合法 target/revision、既有明确业务重发能安全触发重读。没有真实 Office/UI 证据，不能声称长期失败的用户体验已解决。

## 主要合同核对

| 合同 | 实际代码和结论 | 验证边界 |
|---|---|---|
| 异步请求及上界 | `SubmitCurrentPresentationLoad`（`696-709`）统一构造 Current request 并 echo 完整 target/generation，observer 异步入队，无绘制线程磁盘 I/O。`PresentationCurrentLoadRetry`（`711-810`）只调度 250/500/1000/2000 ms 四次；`TrySubmit` 在调用前增加计数、若 Submit 拒绝仍按下一档退避，到 4 次后停止。`loadPending` 真时不重复入队。 | helper 与生产 `Run` 共用；CLI 用控制时钟验证，尚无真实磁盘延迟分布 |
| 身份和取消 | `Matches`（`796-802`）用完整目标值含 target/session revision 与非零 generation；目标/模式/代次变化在 `7849`、workspace 切换 `7908`、`ExitRequested` `8636` 调 Cancel。迟到 completion 先由 F041 `RoutePresentationCompletion` 的 lane/generation/source 门截断。**正式 PrepareExit 屏障遗漏见 R1。** | 静态已核；CLI 目标/代次/手动 Cancel 通过 |
| 错误类别 | Current `IoError` 安排重试（`7718-7724`）；SourceChanged、CrossProcessConflictDeferred、Invalid/未严格核准 Loaded 与安装失败进入 terminal 状态（`7725-7752`），不把空槽写回旧 index。接受的 Loaded 或合法物理轨 NotFound 才置 `persistenceInitialized=true`、取消计划并准备 ready（`7754-7760`）。 | helper/静态已核；Storage 严格物理轨另有独立测试 |
| idle/Laser Hold | `Run:8721-8741` 只在 `active.empty()` 命令安全点尝试到期重提。普通 idle `8841-8847` 与 Laser Hold `8800-8827` 使用 `currentLoadRetryWait` 的有限 deadline，原输入/命令 wake 仍走 `ContactInputCoordinator::WaitForWake` 代次；无计划时保持 `WaitDequeue`。`RemainingWaitMilliseconds` 到期时最少 1ms，下一循环先 `TrySubmit`，不形成永久零超时循环。活动帧的原 target FPS/输入样本路径未改。 | 静态成本/唤醒路径核对；probe **未实际执行 Run idle/Hold**，无真机帧/CPU 证据 |
| 成功 Present 和输入 | `stageWorkspaceReady` 仅在已核准 Loaded/NotFound 后（`7754-7760`）；`publishWorkspaceReadyAfterPresent()` 仍只在 `presentSucceeded && !viewportRecoveryPending && !viewportRefreshPending` 时调用（`9888-9894`）。Host `RefreshPresentationInputGateLocked`（`Host.cpp:256-277`）在 `readyPresentationTarget` 匹配 desired 与 UI ready 前继续阻止输入。Controller 的本地 unresolved gate 在 Load 成功后解除，但 Host 的独立 Present/ready gate 保持。 | 生产调用链静态已核；CLI 只检查 helper gate，未测真实 GPU 成功 Present |

## 红绿原始证据

- 红 `TestResults/release-hardening/f054-current-load-retry-red-explicit.stderr.log` 有三项指定 FAIL：同 revision 到期不重提、在途去重不成立、正确 NotFound 后 gate 不结束。主 agent 记录完整 `InkeysRepo.sln Debug|ARM64` Build exit0、显式隐藏 CLI pid28548 exit1。
- 绿 `f054-current-load-retry-green-explicit.stderr.log` 有固定四档 schedule、上限 `manual_required` 和 `[Draw3PptLoadRetry] PASS`；主 agent 记录同完整 Solution Build exit0、显式 CLI pid65456 exit0，F041 lane、F029 parked Desktop、F031 parked PPT、F038 loaded、F039 topology、F045 control fence 六个相关 CLI exit0，`InkeysHeadlessTests --no-window` exit0。日志 `f054-current-load-retry-green-build-debug-arm64.log`、`f054-post-*` 与代码相符。`git diff --check` 对 Controller 两文件无报错。
- 这些证据验证 helper 时序和 ARM64 Debug 编译/无窗口回归；不证明真实 Run 的 timer 到期、Office 窗口、RTS 输入、可见 Present、Win7 SP1+仅 KB2670838、Win32/x64 或 Release。父任务的 Release|ARM64 完整 Rebuild exit0 发生在 R1 修补前，不能沿用为修补后 PASS。

## 交接

先让 Controller owner按 R1 生产 Exit command 路径取红→绿，并对 `PrepareExitAutoSave` 后迟到 IoError/到期计划及正常同目标恢复分别验证；随后串行重跑 Debug/Release 主 Solution、六个相关 CLI、Headless 和最终 diff 检查。R2 保持发布门禁中的人工/产品可观察性项，不以日志文件或四次重试代替用户验收。
