# F-054：PPT Current Load 瞬态失败后的有界重试

日期：2026-09-28。Owner：Controller 子任务。依据 `ppt-f041-controller-independent-review.md` 的 F041-C1 与当前 `DrawingController::Run`、`ContactInputCoordinator::WaitForWake`、Storage request/completion 合同。只修改 `Draw3.DrawingController.cpp/.cppm` 和本记录；不改 Host、Storage、窗口服务、工程或产品入口。

## 已确认失败与最小边界

Current Load 的一次 `IoError` 回执会让 `activePresentationLoadPending=false`、`activePresentationPersistenceInitialized=false`。生产 input/Canvas gate 为避免空槽覆盖旧 UInk 会继续拒绝新笔和清屏；Host 在 targetRevision 不变时不再发布 `SetPresentationTarget`，因此不存在内生重提入口。修复必须保持这一 fail-closed 输入门，在同一绘制线程以原 observer 的 `presentationLoadRequested` 异步入队，不在回执处理或 UI/RTS 线程同步读盘。

## 设计合同（优化结果前冻结）

- 仅 Current Load 的 `IoError` 自动重试。计划与当前 `PresentationReadyIdentity`（含 targetRevision/sessionRevision）、非零 slotGeneration 一起保存；若新 target、mode、generation、workspace 或 Exit 到来，取消旧计划。一次请求未收到终态时不并发提交第二次。
- 单调 Win7 可用 `GetTickCount64` 计时，自动重提最多 4 次，间隔 250/500/1000/2000 ms。已持续失败则保持未就绪与输入 gate，输出一次低频诊断，等待新合法 target/revision 或明确的业务重试命令。`SourceChanged`、foreign、`Invalid`、严格导入或物化失败均不自动循环，也不得把空槽当恢复。
- Drawing Run 在已有 `active.empty()` 的命令消费安全点检查到期计划；采用同一个 `PresentationLoadRequest{target,kind=Current,slotGeneration}` 和 `observer_.presentationLoadRequested`。提交拒绝当成一次受限失败，后续仍受总次数与退避约束。
- 原 idle `WaitDequeue` 在存在待到期计划时用 `WaitForWake(capturedGeneration,remainingMs)`；Laser Hold 同样取其原 deadline 与重试 deadline 的最小值。唤醒后回到 Run 正常命令/完成处理，不制造帧、降帧率或忙轮询；无重试时保留原等待合同。
- 成功 `Loaded` 或正确物理轨 `NotFound` 后，仍须等生产 Present 成功才发布 ready；调度状态清除，输入 gate 按现有 `persistenceInitialized` 放开。Save 无权绕过未完成恢复。

## 验证门

先加 `RunPresentationCurrentLoadRetryProbe()`，主 agent 接隐藏 CLI `--draw3-ppt-current-load-retry-test`。探针以控制时钟和生产同一 request/scheduler/helper 的 fake observer 构造：首次 Current `IoError`、250ms 前无重提、同 targetRevision 到期恰好一次重提、第二回执为已核准 `NotFound`/`Loaded` 后 ready gate 可开放；连续失败最多四次自动重提，不高速空转；SourceChanged/foreign 永不自动宣称 ready；换目标或 generation/Exit 取消旧计划。红灯先来自当前产品 helper 无计划，不能在测试复制另一套正确算法。主 agent 串行完整 `InkeysRepo.sln Debug|ARM64` Build/显式 CLI 红→绿、Headless/F029/F031/F038/F039/F041，再独立 review。GUI、Office、Win7、Release 由父任务分别验收。

## 阶段证据

- 红版生产共用 helper 与 no-HWND probe：完整 `InkeysRepo.sln Debug|ARM64` Build exit 0；隐藏 CLI `--draw3-ppt-current-load-retry-test` pid 28548 exit 1，三项预期失败：同 targetRevision 到期不重提、在途标志不成立、正确 NotFound 后无法结束输入 gate。日志 `f054-current-load-retry-red-build-debug-arm64.log`、`f054-current-load-retry-red-explicit.{stdout,stderr}.log`。
- 绿码：`PresentationCurrentLoadRetry` 按上述固定间隔/次数调度；实际 `Run` 的 Current completion、初始/同目标请求、命令安全点及 idle/Laser Hold deadline 使用同一 helper。主 agent 完整 `InkeysRepo.sln Debug|ARM64` Build exit 0；显式隐藏 CLI `--draw3-ppt-current-load-retry-test` pid 65456 exit 0/PASS（原红 pid 28548 exit 1/三 FAIL）；F041 lane、F029 parked Desktop、F031 parked PPT、F038 loaded、F039 topology、F045 control fence 六个早期 CLI exit 0，`InkeysHeadlessTests --no-window` exit 0/PASS。日志 `f054-current-load-retry-green-build-debug-arm64.log`、`f054-current-load-retry-green-explicit.*`、`f054-post-*.{stdout,stderr}.log`。本 Controller 候选已冻结并释放写入所有权，待独立 reviewer 审 Run idle/Hold/退出时序。

## 实施和诊断

- `SubmitCurrentPresentationLoad` 是初次、明确重发和自动重试共用的 request 构造/observer 异步提交入口，保留完整 target 与 slotGeneration；无磁盘 I/O 在绘制或 UI 线程执行。
- `PresentationCurrentLoadRetry` 只在 Current `IoError`、初次/明确 Submit 拒绝时自动安排 250/500/1000/2000ms 的四次机会。按完整目标值与 generation 匹配；成功请求在途期间不重复入队，失败持续到上限后停止自动动作并写 `manual_required` 诊断。`Loaded`/已选正确 track 的 `NotFound` 清除计划，实际 UI ready 仍等成功 Present。
- `SourceChanged`、foreign、`Invalid`、严格导入/安装失败保持空槽未就绪，写含 status/generation 的低频诊断，不把空白文档写回旧索引。新合法 target/revision 或既有业务明确重发调用可以安全重新请求；没有新增可误触发的数据恢复入口。
- Run 的 idle 与 Laser Hold 仅在待到期计划存在时以 `ContactInputCoordinator::WaitForWake` 的 deadline 取代无界 `WaitDequeue`；无计划时保留原阻塞。新场景/代次、工作区切换与 Exit 取消；不制造空帧、降低刷新率或忙轮询。使用 `GetTickCount64`，未新增高于 Win7 的系统 API。

## 尚未证明

此 probe 不创建真实 Office、Drawpad HWND、RTS 或 GPU Present，也不用真实 UInk 文件；最终 `ready` 还必须等生产 Present 成功。Storage 的严格 Loaded/NotFound 与异常回执由其独立 Service 测试和后续联合验证覆盖。UI 未新增重试按钮；`SourceChanged`/foreign/永久损坏保持 blocked 并写一次诊断，用户通过新的合法放映目标/页 revision 或既有明确业务重发入口触发安全重读。若底层 I/O 永久卡住不返回，不能靠同 worker 队列重试自救；正式退出的 15 秒 supervisor 是另一条链。

## 独立 review 新发现：退出 ACK 后的迟到重试

2026-09-28 独立 reviewer 指出，`Run` 原 `window_.ExitRequested()` 只在同帧较早位置检查；`processCanvasCommands` 处理 `PrepareExitAutoSave` 并给 Host ACK 后仍会进入本帧 retry 安全点。更隐蔽的是同一个命令批次可在 barrier 后消费迟到 `Current IoError`，重新排定计划，所以只在 barrier 调一次普通 `Cancel()` 仍不够。设计追加单调 `exitAutoSavePrepared` 终态：barrier **先**置终态并清计划，再 capture 最终 Save、ACK 一次；后续 IoError completion 不可重新排期，初次/明确/自动 Current Load 都拒绝，`window_.ExitRequested()` 作为无 barrier 的兜底。此状态只属于一个 `DrawingController::Run` 生命周期，不跨 Host generation。

无 HWND probe 使用 `processCanvasCommands` 的同一个退出屏障 helper 和 Run 安全点 helper，排定刚到期计划→PrepareExit capture/ACK→迟到 IoError→再次到期，断言 ACK/最终 Save capture 各一次、没有新 Load；另测没有 barrier 但 `ExitRequested=true`。当前先保留旧行为取得红灯，主 agent 串行完整 Debug Solution/已有 CLI；绿后只在本 Controller 源最小修复。

红证：完整 `InkeysRepo.sln Debug|ARM64` Build exit 0；显式隐藏 CLI `--draw3-ppt-current-load-retry-test` pid 46652 exit 1，三项预期失败：ACK+迟到 IoError 后仍入 Load、重复 PrepareExit 产生重复 capture/ACK、ExitRequested 安全点仍能提交到期 Load。旧正常 IoError 退避断言通过。日志 `f054-exit-barrier-red-build-debug-arm64.log`、`f054-exit-barrier-red-explicit.{stdout,stderr}.log`。

绿码：`PresentationCurrentLoadRetry::PrepareExit()` 在首次 barrier capture/ACK 前设置单调 `exitPrepared_` 并取消计划；普通 `Cancel()` 不重置该终态，迟到 IoError 无法再次安排。首次 ACK 后 `processCanvasCommands` 排空但不应用任何晚到命令或 completion；Host `Draw3.Host.cpp:1325-1359` 在 ACK 后自己关闭并排空 Desktop/PPT worker，Controller 再应用 PreviousInterval completion 反而可能派生新 Save。Run 安全点以 `ExitRequested` 和终态双门拒绝到期提交；同 target/初次 Load 也核该门。最终 snapshot 捕获异常仍给 Host 一次 ACK 并记失败，交 15 秒退出兜底处理；不把 ACK 当 durable。主 agent 完整 `InkeysRepo.sln Debug|ARM64` Build exit 0，显式隐藏 CLI pid 19296 exit 0/PASS，旧 pid 46652 exit 1/三 FAIL 保留；日志 `f054-exit-green-uef-red-build-debug-arm64.log`、`f054-exit-barrier-green-explicit.{stdout,stderr}.log`。绿色 stderr 还含探针其它场景的 scheduled 与故意捕获异常诊断，必须按 probe 断言和每个场景的进程退出码判定，不能从日志行顺序推断“退出后又重新排期”。本 Controller 补丁冻结交主 agent 后续六回归/Headless与独立复审。
