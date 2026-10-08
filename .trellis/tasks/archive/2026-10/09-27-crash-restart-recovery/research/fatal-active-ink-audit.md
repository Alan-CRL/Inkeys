# Research: Draw3 fatal 时活动笔迹与退出持久化

- Query: F-026 P1：图形恢复失败、`DrawingController::Run` 异常和 Down/Move 未 Up 时，已完成文档、活动 contact 与退出保存的实际合同；给出触及生产逻辑的无 GUI 红测切口。
- Scope: internal
- Date: 2026-09-29
- Active task: `.trellis/tasks/09-27-crash-restart-recovery`

## Findings

### 现状及可达条件

1. **显式图形 fatal 已能排队保存一部分数据，但不包含活动笔迹。** `Draw3.DrawingController.cpp:8365-8399` 在 presenter 恢复或 GPU history 重建失败时，于 controller 仍存活时调用 `captureExitAutoSave(true, ...)`，随后 `RequestExit` 和 `break`。`captureExitAutoSave` (`:6958-7025`) 抓取 active/parked Desktop 与 dirty 的 active/parked PPT；每个 eligible 失败单独记 `not_queued`，总行明确 `durable=pending_worker`。Desktop 抓取只遍历权威 `CanvasRuntimeHistory::Items()` 的可见项 (`:1025-1113`)，PPT 快照也只遍历 history (`:1115-1235`)。活动 contact 存在于 `RuntimeStroke`/`active` (`:1533-1616`)，通常直到 Up/Cancel 后的帧尾 `:9532-9714` 才转为 `InkCanvas::AppendStroke` + `CanvasRuntimeHistory::AppendStroke`；因此 Down/Move 时图形 fatal 的快照不含该笔。激光不持久化，`StoredStyleForTool` 对 Laser 返回空 (`:942-982`)。**结论：F-026 对已完成 CPU history 的显式图形 fatal 保存已部分修复；活动可持久笔迹丢失的路径由当前源码确认，真实设备触发与丢失量尚未实测。严重性 P1 条件性数据丢失。**
2. **异常逃出 `Run` 仍绕过上述捕获。** Host `Draw3.Host.cpp:1202-1228` 在 `drawing->Run()` 的 `std::exception`/`catch (...)` 只记日志并 `window.RequestExit()`，随后 `drawing.reset()`；`Run` 栈上的 active runtime、parked slot 和 `pageRuntimeStates/history` 已在栈展开时销毁。活动 `document_` 虽是 controller 成员 (`Draw3.DrawingController.cppm:149`)，其配套 history/页身份已不完整，不能在 Host catch 中补做合法快照。`Run` 初始化早退（如 `Draw3.DrawingController.cpp:3855-3856` GUID 失败）同样不是 graphics-fatal 保存分支。`Host::Stop()` (`Draw3.Host.cpp:1321-1364`) 即使排入最终 `PrepareExitAutoSave`，等待谓词允许 `!running` 返回，并记录 `controller_stopped`，此时 controller 已不可消费命令。**结论：常见可捕获异常导致未排队已完成内存墨迹丢失为源码上的条件性缺口，尚无异常注入；不能把 catch-all 当保存成功。** 真正内存损坏、SEH/FailFast 不应走损坏对象上的补救保存。
3. **正常退出合同和 fatal 不同。** Host Stop 先在命令锁内将最终屏障排在旧命令之后 (`Draw3.Host.cpp:1325-1336`)，再 `RealTimeStylusInput::Shutdown` (`:1337-1339`)，最后等 ACK 与两 worker drain (`:1341-1363`)。RTS Shutdown 先 disable/plugin 卸载，再 `CloseAllProducerContacts` (`Draw3.RealtimeStylus.cpp:2615-2650`)；后者发 `Cancelled` (`Draw3.ContactInput.cpp:873-900`)，正常帧尾明确跳过 `runtime->cancelled` 的权威笔迹提交 (`Draw3.DrawingController.cpp:9539-9569`)。所以“所有物理未 Up 笔迹在正常退出也已保存”不成立；不能把 RTS Cancel 直接转换成完成笔迹。正常屏障仅当 `active.empty()` 才消费 Canvas 命令 (`Draw3.DrawingController.cpp:7607-7612,8091-8097`)，长期无终态可能卡住，15 秒监督器是进程终止边界，不能证明这笔已落盘。
4. **已完成提交与 GPU 失败分离，但 CPU 的 append 也可能失败。** 正常 Up 在 `Draw3.DrawingController.cpp:9544-9593` 依次 Finalize、tile preflight、`InkCanvas::AppendStroke`、`history.AppendStroke`，成功 history 后才推进 PPT mutation；随后 L2/Preimage 操作失败不会回滚 CPU 真值 (`:9600-9699`)。若 history append 失败，Canvas 中可能留下不在可见 history 的 Stroke (`:9694-9699`)，当前 Desktop/PPT 快照按 history 忽略它。`CanvasRuntimeHistory::AppendStroke` 可因无效 bounds、容量、composition tree 失败返回空 (`Draw3.InkHistory.cpp:902-934`)；`InkCanvas` 没有单笔 pop/rollback API (`Draw3.InkDocument.cpp:109-118`)。任何 fatal CPU seal 不能只凭 Canvas append 成功判定已纳入快照，必须核 history 可见且所选页身份一致。
5. **Host 意外停止已有限 containment，不能侦测绘图线程仍活着但挂住。** `IdtMain.cpp:2661-2689` 每约 100ms 见 `!ProductRunning()` 就 `SetOffSignal(1)`，双窗成对 Hidden 并全窗口隐藏，读回可见性/capture。`IdtState.cpp:378-385,413-430` 对停止 Host 拒绝旧 ready/revision 的异步显示。若 Draw3 绘制线程卡在内部锁/COM/GPU/无限等待，`running` 仍 true，此轮询不会触发；用户截图的“UI 还响应、画布卡住”本身不足以证明是上述 fatal 分支。显式关闭/重启时 `SetOffSignal` 首次 CAS 后先启动独立 15 秒监督 (`IdtMain.cpp:256-276`)，随后 Window hide 请求 (`Helper.CrashHandler.cppm:57-67`)；但后台强退可能中断 worker (`Draw3.AutoSave.cpp:1154-1163`, `Draw3.PresentationAutoSave.cpp:1511-1519`)。用户已选“15 秒无条件结束”；只能保证**最后已提交恢复点**不因本次半写被覆盖，不能保证待保存请求或当前活动笔迹。

### 可安全封口的数据边界

- 当前源中有可借鉴的**页边界合成 Up**：`sealPresentationContacts` (`Draw3.DrawingController.cpp:5725-5770`) 用 `lastInputSnapshot` 或实际已接收 `deferredUpSnapshot`、保留原 `qpc`，调用 `completeModelUp`，并隔离物理路由；它后半段调用 renderer 清层/RequestFullPresent，**不能原样搬到已故障 GPU 的 fatal 分支**。`completeModelUp` (`:5415-5498`) 为真实位置构造 Up，模型失败会以同一终点回退；`FinalizeStoredStroke` (`Draw3.StrokeGeometry.cpp:448-513`) 只从 `realPoints`/Down 起点生成持久 Stroke，明确排除 `predictedPoints`。`FinalizeStoredShape` (`:515-534`) 读 `shape.primitive`；实时 shape 可用 `ResolveShapeLiveEndpoint` 取预测末点 (`Draw3.DrawingController.cpp:9181-9208`)，fatal 封口必须先用最后真实 raw/已接受 modeled 端点覆盖它，不能把上帧预测端点保存。
- 允许的最小语义：只在 controller/CPU 状态健全且绘图 owner 线程上，对已被绘图消费者接受的、未 `cancelled`、有正确 `ContactHandle`/workspace/page generation 的可持久工具，按最后**真实**样本或实际 deferred Up 封口。一次性标记并从物理路由隔离，绝不读下一页/另一工作区的新 contact，不采信 `Predict()`、L0 `predictedPoints`、GPU 像素、未确认 Present；Laser 不进 UInk。要区分因系统 `Cancelled` 的 contact 与“图形 fatal 后主动决定保留已接受实点”的 synthetic Up，不能全局更改 Cancel 语义。Down-only 的单点笔迹可以按现有 `hasInputStartPoint` 合法落点，前提是对该工具单点语义成立。
- `ContactInputCoordinator::SetAdmissionBlocked` (`Draw3.ContactInput.cpp:836-865`) 通过 revision 拒新 Down；并不会阻止已存在 route 的 Move (`:732-763`)。fatal CPU 封口需在单一消费者线程明确冻结 cutoff：仅处理已读取的 `lastInputSnapshot`/terminal，或在封口前最多读一次稳定 mailbox 并记录 sequence；随后 `DiscardUntilTerminal` (`:815-833`) 隔离物理路由。不能在 `DiscardUntilTerminal` 之后再指望读快照。并发迟到 Cancel、已 Up 但未消费、`awaitingReconnect` 和多 contact 要逐例定义，不能用“一律补 Up”覆盖。

### 最小实施与无 GUI 红绿验证建议

1. 在 `Draw3.DrawingController.cpp` 同文件把正常 Up 路径中 `:9544-9593` 的 **CPU Finalize→footprint→Canvas/history 同一提交** 抽成窄生产 helper；GPU `DrawStoredStroke`/L2/Preimage 保留在正常路径之后。尽量使用现有 `RuntimeStroke`、`InkCanvas`、`CanvasRuntimeHistory`，不引入第二套几何算法。提交返回显式 `Committed/SkippedCancelled/SkippedTransient/Failed` 及页/handle 标识；仅 history 真的可见后标 PPT mutation。若需要完全原子性，先设计 history append 失败后的 Canvas orphan 处理，不能把“已写 Canvas”当 durable 资格。
2. 在 presenter/history-cache 两个显式 fatal 分支，于 `captureExitAutoSave(true)` **之前**阻断新 admission，CPU-only seal/提交所有合资格 active runtime，再用现有 `captureExitAutoSave`；失败逐项记 reason/计数，不把 `queued` 说成 committed。不要调用已经坏的 renderer、历史 GPU cache 或普通帧尾 GPU 处理。停止仍交 Host/主退出 owner。对于 `Run` C++ 异常，仅可在 `Run` 内状态完整的局部异常边界进行 best-effort CPU 捕获；若异常来自内存破坏/分配失败且再分配不安全，直接受控退出并报不保证，绝不可从 Host catch 访问已销毁的局部文档。
3. **首个红测**：利用现有早期无窗口产品 CLI 模式 (`IdtMain.cpp:686-717`) 与同文件测试先建真实 `RuntimeStroke`/`ActiveStroke`、`InkCanvasCollection`、`CanvasRuntimeHistory`，以 Down→Move 未 Up + 已完成旧 stroke 调现有 Desktop/PPT 生产 snapshot helper。当前 fatal 路径仅 snapshot 时应断言新活动笔迹缺失（红），旧已完成笔迹存在；再让生产共用 CPU seal helper 接入 fatal 后绿。测试需逐项覆盖 Pen/HardPen/Highlighter、固定/速度 Eraser、开放 Shape、Down-only、Move、真实 Up 已消费、Cancelled、Laser、`awaitingReconnect`、双 contact、跨 PPT page/track、tile/append 失败、重复 fatal；断言 history stroke ID 只追加一次、PPT mutation 才推进、快照只含正确页且无预测端点。单测不能复制生产 finalize 算法。
4. 独立隔离目录接真实 `DesktopAutoSaveService`/`PresentationAutoSaveService` 做快照→worker `Committed/Failed`→严格回读；用现有 fault hooks 在 UInk durable 后/index publish 前中断专用 child，并证明旧主/备 index 仍指向旧物理文件。Headless 的局部 CPU PASS 仍不能证明真实 HWND/设备失效、输入手感或 GUI 自动恢复；需用户允许的隔离 GUI 故障注入，在 Hardware/WARP 和 DComp/ULW 实际组合下观察首个失败 Present、封口统计、双窗隐藏、旧进程 15 秒边界、最后提交文件与重新启动后已开放范围的可见恢复。不要用 TerminateProcess 假装 presenter fatal 或 UEF。

## Files Found

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp`：输入消费、活动 runtime、正常 Up 的 CPU/GPU 提交、Desktop/PPT 快照与图形 fatal 分支。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp`：`Run` 异常边界、最终保存命令、RTS Shutdown、worker drain 与 controller 销毁顺序。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.cpp` / `Draw3.RealtimeStylus.cpp`：revision 门、真实/取消终态、producer 关闭。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.StrokeGeometry.cpp`：完成态只取真实点的几何合同及 shape 的预测端点风险。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.InkDocument.cpp` / `Draw3.InkHistory.cpp`：CPU 文档与 history 追加失败边界。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.AutoSave.cpp` / `Draw3.PresentationAutoSave.cpp`：worker 接受、durable/原子索引和无超时 drain。
- `Inkeys/IdtMain.cpp` / `Inkeys/IdtState.cpp` / `Inkeys/Inkeys/Helper/Helper.CrashHandler.cppm`：Host 意外停止 containment、呈现旧请求门与 15 秒关机监督接入。

## Related Specs and Task Decisions

- `.trellis/spec/native-desktop/draw3-integration.md`：Draw3 线程/RTS/worker ownership、Desktop/PPT 保存、No GUI 与真实输入验证分界。
- `.trellis/spec/native-desktop/input-and-ink.md`：RTS→mailbox→单 Draw3 owner，不能用 Draw2 行为推导当前合同。
- `.trellis/spec/native-desktop/errors-logging-and-resources.md`：受管线程退出与资源释放顺序。
- `.trellis/tasks/09-27-crash-restart-recovery/{prd,design,implement}.md`、`research/draw3-fatal-exit-design.md`、`research/shutdown-supervisor-contract.md`：P1 目标与用户明确的 15 秒强退边界。旧设计文档中“fatal 尚未捕获已完成 history”的陈述已被当前 `captureExitAutoSave(true)` 实现部分淘汰，应按本次代码事实更新状态。

## External References

- 无；本次仅审查工作区代码、Trellis 任务和项目规范，未以外部 Windows 文档代替真实设备证据。

## Caveats / Not Found

- 本研究未修改产品或测试，未运行构建、故障注入、GUI、性能采样；没有证明用户截图的卡顿由 graphics fatal 造成，也没有证明 worker 当前请求一定 durable。现有 `--draw3-parked-desktop-exit-test` 只验证 CPU snapshot helper，不覆盖活动 runtime、GPU fatal 或真实 Host 停止。
- 现场真实笔迹若已在 L0 显示但尚未被 CPU `history` 接纳，能否被安全封口取决于最后真实采样/modeler 状态与具体工具；不能承诺零丢失。软件保存计时不等于最后像素可见时刻。
- 现有规范“正常退出等待所有 worker 无硬超时”与用户后来要求的进程级 15 秒无条件强退需写成分层合同：程序尝试有序 drain；deadline 到达时优先结束旧 PID，可能失去待保存请求，但不得破坏已 durable 恢复点。
