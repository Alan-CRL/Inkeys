# F-026 活动墨迹 fatal CPU 封口独立复审

截止 2026-09-28 UTC；Closing 修补明确冻结后的 `Draw3.DrawingController.cpp` blob 为 `83a0e4e7b08a544c980c36fedc7dd7c33697b720`，长度 467573 字节。只读追了生产 Controller、ContactInput、InkDocument/InkHistory、StrokeGeometry、Host、IdtMain 的实际调用链，比较 H0 正常 Up 片段并核新增 CLI 断言。未修改产品/测试/共享任务文件，未运行构建、GUI、性能或故障注入。本报告是 F-026 增量审查，不把先前 H0→HF 指纹当最终 HF。

## Findings (fixed)

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:1833-1837,7446-7518`：首轮发现 fatal 快照入队后还调用同步 `DiscardUntilTerminal`；当 producer 正处于 Closing 时，绘制线程可无限等待。实施者保留 `StopFatalInputConsumer` 只设置 admission gate，在 fatal 捕获后直接 break/重抛，不再等待 route。普通页边界和正常完成路径仍使用原 `DiscardUntilTerminal`。该局部改动关闭了**fatal 绘制线程自身**在 Closing 上的等待；未宣称 RTS/COM Shutdown 不会卡住。
- `Draw3.DrawingController.cpp:3297-3313`：原测试在读取 raw Move x55 后手工把 `realPoints` 也推进到 x55，未测 modeler 落后时的补点。新测试让真实尾停在 Down x40，已消费 mailbox Move 为 x55，仍断言持久快照尾点 x55，预测 500/600 不进入。

## Findings (not fixed)

### P1／条件性残余：普通页边界与正常回收仍会等待 Closing

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:6145-6224,10182-10194` 的页边界 gesture 清理、被拒 Down 和正常完成笔画仍调用 `DiscardUntilTerminal`。`Draw3.ContactInput.cpp:815-832` 遇 Closing 无限 `YieldProcessor`，而生产 Up/Cancel 在 `:558-594` 先置 Closing、等 writer latch、再转 ConsumerOwned。因此 producer 若长期卡在 Closing，**普通**绘图/切页也可停住；这是源码条件链，尚无针对正常路径的故障注入，不能归因用户截图。
- 不能把 `DiscardUntilTerminal` 的 Closing 全局改为直接返回：普通调用者随即清除 handle；非 Quarantined 的 producer 完成 Close 后会留下 ConsumerOwned，`CloseAllProducerContacts` 只处理 Producing/Quarantined，直到下次 Host Reset 才清槽，反复发生会耗尽固定槽池。正常运行要保持等待或设计由 producer 完成后的明确延后回收；这个跨 ContactInput 合同不属于 fatal 局部修补。

### P2／已知失败边界：Canvas 与 history 追加不原子

- 共用 helper `Draw3.DrawingController.cpp:1793-1828` 先做 style/finalize/tile 预检，在 `:1805-1809` 标 `cpuCommitAttempted` 并 `InkCanvas::AppendStroke`，再于 `:1810-1817` 调 `history.AppendStroke` 并核可见条目。`Draw3.InkDocument.cpp:109-113` 的 Canvas 无单笔 rollback；`Draw3.InkHistory.cpp:902-933` 的 Append 可返回空或分配抛错。若 Canvas 已增而 history 未成功，留下不在可见 history 中的 orphan Stroke；fatal 闭包记录 failed、不会推进 PPT mutation，Desktop/PPT snapshot 按 history 也不会误报该笔已保存，重复封口被 `cpuCommitAttempted` 拒绝。这是**失败关闭而非完整事务**，数据仍可能失去，但未发现把孤儿当成功写盘的代码。
- 该分支尚无真实 `history.AppendStroke` 故障注入；建议在同一生产 helper 下注入 preflight 后的 history 拒绝/分配失败，断言可见集合、redo、PPT mutation、重复 fatal 与 UInk 快照。若要求失败后能重试保存，需先设计 Canvas/history 原子提交或 rollback API，属于超出本次局部复审的接口判断。

### P2／测试仍未覆盖真实双 contact 与完整 Host 链

- 新无窗口 CLI 调用了生产 helper、Desktop 捕获、PPT builder 与真实 ContactInput。modeler 落后 raw Move 的补点现已覆盖；但其余工具用例仍复用同一个 `ContactHandle`（`Draw3.DrawingController.cpp:3328-3343`），未执行两个独立 contact 在同一 fatal 闭包内按顺序封口。PPT 测试只更换 workspace/page GUID，未用真实 lane/slot generation 交错。
- Closing 测试 `:3482-3556` 暂停 Move writer、并发 PublishUp，红版 CLI 仅在「fatal route finish must return before Closing producer resumes」失败，绿版该项通过；这是有价值的生产状态机交错。其 `closingObserved = !upDone && !PublishMove` 判据也可能因暂停 writer 持有 latch 而让 `PublishMove` 失败，未直接读出 route 已 CAS 到 Closing；红灯的旧同步 Discard 等待支持当次确实达到危险交错，绿灯的预条件仍有调度歧义。建议增加可控 Closing 状态探针和重复运行，另测 Cancel 在 cutoff 前/后。CLI 未执行实际 `DrawingController::Run` 图形 fatal/普通异常、Host→RTS Shutdown→worker durable 或 15 秒全链，不能作为这些路径的 PASS。

## 已核对的生产合同

| 链路 | 静态结论 |
| --- | --- |
| 正常 Up 复用 | `DrawingController.cpp:10027-10068` 的正常完成笔画使用 `CommitRuntimeStoredStrokeCpu(...NormalUp)`，成功后才 `markPresentationMutation`，GPU raster 在 CPU history 之后。与 H0 的 Finalize→Canvas→history→GPU 顺序一致；新增 owner/footprint 预检使错误归属拒绝追加。 |
| 真实样本与预测 | fatal helper `:1763-1801` 只用绘图消费者的 `lastInputSnapshot` 或已消费的 deferred Up；形状把预测 end 覆写为 raw 端点，普通 Stroke 的 `FinalizeStoredStroke`（`Draw3.StrokeGeometry.cpp:448-513`）只用 `realPoints` 与 raw fallback，不序列化 `predictedPoints`。迟到而未被消费者读取的 Move/Up 不在本次保证内。 |
| Cancel/Laser/Whiteboard | fatal 闭包 `:7455-7470` 探测 cutoff 前已到达的 Cancel，已取消 runtime、Laser、Whiteboard 不提交；`StoredStyleForTool` 也拒 Laser。cutoff 后的 Cancel 与 synthetic Up 竞争仍需真实输入交错验证。 |
| 页身份与重复 | helper `:1748-1764` 核 handle generation、workspace GUID、page index/GUID、history/state 数量；成功后 `cpuCommitAttempted` 与 fatal 闭包的一次性标志 `:7446-7451` 防重复。没有单独核 PPT 物理 track/slot generation；依赖先前页切换围栏及 GUID 身份，此次只读未找到可达同 GUID 跨代串页反例，真 Office/迟到输入仍需测试。 |
| 两个 graphics fatal | `:8860-8894` 的 presenter 恢复失败与 history cache 重建失败均在 `RequestExit`/break 前调用同一 fatal 闭包，controller 与局部 active/history 仍存活。 |
| 普通 C++ 异常 | `:10600-10628` 仅在 while 内捕获 `std::exception`，先核 active document 页数和 history/state 长度，再尽力 fatal 捕获并重抛给 `Host.cpp:1202-1228`。`bad_alloc`、未知异常、早期初始化及损坏状态明确不补救；没有异常注入证明这一分支实际完成保存。 |
| Host 与 worker | Host catch 后销毁 controller、置 running=false；`Stop():1325-1363` 即使 final barrier 因 controller 已停失败，仍调用 Desktop/PPT `CloseAndDrain` 排空先前接受的请求。`queued` 不是 `Committed`，进程级 15 秒强退可截断未 durable 请求。 |

### fatal 路由和 record 寿命复核

- `StopFatalInputConsumer`（`DrawingController.cpp:1833-1837`）只改 admission revision，不把旧 record 设 Free、不回收槽位、不销毁其内存。fatal 闭包末尾 `:7513-7518` 只排快照；两个显式图形分支随后 `RequestExit`/break，普通 `std::exception` catch 则重抛，不再由绘制线程读取旧 route。RTS producer 在这段间隔仍可能更新 mailbox/排新 Down；队列和槽池有固定容量，新 Down 带被阻断的 admission revision，不会进入已退出的 controller。
- record 由 Host 的 `ContactInputCoordinator` 持有固定 `ContactBlock`（`Draw3.ContactInput.cpp:319-353`），并不随 `DrawingController::Run` 局部 active 析构。`Host.cpp:1202-1228,1325-1363` 在 drawing 返回后先置 `running=false`；主协调器观察后执行 Stop，先 `stylus.Shutdown()`，后 drain worker/join。RTS Shutdown 在 `Draw3.RealtimeStylus.cpp:2615-2650` 先禁用/卸载回调，再 `CloseAllProducerContacts`；该函数（`Draw3.ContactInput.cpp:873-900`）关闭仍 Producing/Quarantined 的路由。只有停机后的下一次 `Host::Start` 于 `Host.cpp:1042` 调 `ResetForNextRun` 清旧队列/record。按这条顺序，fatal 跳过 Discard 不会让旧 record 在仍可写时提前复用或悬空；旧 ConsumerOwned/Closing 最多占用本次 Host 的有界槽位，直到安全重置。
- 此静态证明依赖 Host 确实进入 Stop；若 RTS COM disable/卸载或 producer 自身卡死，仍可能拖延 Stop，由进程级 15 秒强退兜底，不能承诺本次请求 durable。它也不证明真实双 HWND 隐藏、输入捕获释放或新进程可见恢复。

## Verification 与独立结论

- 本 reviewer 只执行静态源码/任务规范读取、冻结 blob 复核和 `git diff --check H0 -- Draw3.DrawingController.cpp`（exit 0）；没有运行 lint、type-check、构建或测试，遵守本次分工。实施记录中的 Closing 红 exit1→绿 exit0、Debug|ARM64 Solution、生产 CLI、Headless/standalone 退出码属于实施者证据，不能冒充本轮独立复跑或最终 HF 结果。
- CPU helper 对**已消费且身份匹配**的活动真实点具备明确 best-effort 保存路径，正常 Up 与 fatal 共享文档/history 提交，未见成功路径把预测、Laser 或 Cancelled 写入 UInk。首轮发现的 fatal Closing 无限等待已由不回收/不等待的局部修补静态关闭；普通路径的同类等待、P2 Canvas/history 失败边界、真实图形 fatal、Run 异常、两个物理 contact、worker durable、Win7/Office/GUI 均未通过本轮验证。F-026 不能凭局部 CLI 整体标 PASS，最终发布状态仍取决于这些门禁和主任务决定。
