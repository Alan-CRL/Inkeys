# F-026/F-027 初步 containment 与 F-031 PPT retained 独立复核

日期：2026-09-28。只读复核当前未提交产品 diff 及现行调用链；本 reviewer 未参与这些实现，未改产品/测试、未启动 GUI/构建/故障注入。范围只含 `Draw3.DrawingController.cpp/.cppm` 中 F-026/F-031 改动，`IdtMain.cpp` Host 停止协调与 `IdtState.cpp` 可见性 guard；同文件其它 Draw3 raster 改动未借本报告宣布通过。任务设计和实施摘要见 `draw3-fatal-exit-design.md`、`fatal-completed-document-fix.md`、`parked-ppt-retained-identity-design.md`、`parked-ppt-retained-identity-fix.md`。

## 需修正的问题

### CR-01 / P1 / confirmed：PPT 活动槽加载后未接收 retained map

`materializePresentationSlot` 从合法的 `snapshot.retainedCanvases` 构造 `loaded->retainedSlides`（`Draw3.DrawingController.cpp:5505-5612`）。同一函数在 Rebind 场景明确将 rebuilt map 移到 `activeRetainedSlides`（`:5690-5724`）。但持久化 completion 是当前活动文稿时，`:6355-6365` 只移动 `loaded->document/pageRuntimeStates/fileGuid/revisions`，**遗漏** `loaded->retainedSlides`。后续 `capturePresentationAutoSave()` 把活动 `activeRetainedSlides` 传给生产 builder（`:5346-5353`），切场景时 `swapActiveDocument` 又把这个空/旧 map 换进停放 slot（`:5229-5243`）。因此一个已有 retained 页的 PPT 文件加载到 active 后，再发生编辑/保存/切换，可能在新快照中漏掉这些页；如果旧 map 与现槽来源不同，还存在污染风险。条件是当前已开放的进程内 Presentation load completion 被安装且 `activePresentationMutationRevision==0`；无需假设跨进程自动恢复已开放。

最小修正：在活动 completion 的 installed 分支中与 document/runtime 同时 `activeRetainedSlides = std::move(loaded->retainedSlides)`，仅在整组安装条件成立时执行；保留 target/key、revision 和未安装分支。测试应触及**生产** materialize→活动安装→builder 路径：隔离的 StableSlideId 快照含 active 页、EndScreen、retained SlideID 102；完成后新保存仍保留同 pageGuid/Stroke，切 A/B 后各自 map 不串，旧 completion/无效 snapshot 不覆盖当前槽。现有 F-031 A/B CLI 直接构造两个 map 后调用 builder，未执行加载安装，不能覆盖本缺口。此为现行代码发现的问题，是否由本次 F-031 diff 首次引入不作无证据判断。

### CR-02 / P2 / confirmed diagnostic gap：HideAll 的 true 不证明双画布已隐藏

Host 意外停止后主循环先 `SetOffSignal(1)`，然后同步提交 paired Hidden 与 `HideAllUserWindows()`，记录 `pairedHidden/allHidden`（`IdtMain.cpp:2248-2263`）。成对 `ApplyDrawpadSurfaceVisibility(Hidden)` 负责释放主画布 capture，且失败时尝试双隐藏并读回（`Window.cpp:1653-1733`）。然而 fallback `HideUserWindowsInGroup(false)` 只是对一组 HWND 调用 `ShowWindow(SW_HIDE)`，随后无条件 `return true`，无 `IsWindowVisible` 或 capture 读回（`Window.cpp:1624-1651`）；`HideAllUserWindows()` 的 `overlayHidden` 因而可能为 true 而残留窗口实际仍可见。故 `pairedHidden=false, allHidden=true` 不能作为“不会拦截用户输入”的 PASS 证据。

最小修正/门禁：窗口 owner 执行 fallback 后读回 Primary、Presentation 的可见性及 Primary capture，分别记录；已销毁 HWND 可按安全状态处理。读回不安全时保持失败并尽快销毁，而不是把 `HideAll` 返回值升级为输入穿透已验收。另 `Window.cpp:1121-1142` 的跨线程 `Submit` 等待 `future.get()` 无超时；如果窗口 owner 本身卡住，第一条 Hidden 可能使 fallback 永远不可达。该故障注入和退出时序仍需隔离 GUI 实测，静态代码无法证明所有 owner 状态均能完成隐藏。

## 符合设计的当前路径

### F-026：已完成 CPU 文档的 fatal 保存尝试

- 当前 `captureExitAutoSave(fatal,reason)` 在 `Draw3.DrawingController.cpp:5356-5422` 依序选 Desktop active/parked 明确源、当前 Presentation、`presentationSlots` 中各 parked slot；正常 `PrepareExitAutoSave` 分支（`:6619-6624`）在同一捕获函数返回后才 `reportCommand`。与 HEAD 旧路径的 Desktop→当前 PPT→parked PPT→回执顺序相同。`reportCommand` 是原 worker 入队屏障，不是 durable 文件回执。捕获函数目前无 GPU API、同步磁盘 I/O 或跨线程等待。
- `:6891-6905` 的 presenter 恢复永久失败和 `:6919-6925` 的 GPU history cache 重建失败，均在 `window_.RequestExit()` 与 `Run` break 之前调用 fatal 捕获；这时 `Host.cpp:1197-1223` 仍持有 `drawing` 和 CPU document，随后才 reset controller。两条已知 fatal 图形分支的调用位置正确。
- Desktop `CaptureDesktopAutoSaveForScene` 仍按 Desktop policy、选定 document/page runtime/history 生成仅可见笔迹的按值快照（`DrawingController.cpp:543-643`）；PPT builder 从实际 `InkCanvas` 与 `CanvasRuntimeHistory::Items()` 的可见项构造（`:647-765`）。未 Up/Cancel 的 `RuntimeStroke` 不在这些文档/history 中，当前修补没有把 active contact 假称已保存。fatal 的 `eligible/source/not_queued` 记录能指出有资格但入队失败的 slot；`queued` 表示 observer/worker 接受，不表示 worker commit，`durable=pending_worker` 应按此解读。
- `Host::Stop` 先关 bridge 接受与投递退出 final command，再停 RTS、等屏障、`autoSave/presentationAutoSave.CloseAndDrain`，最后请求绘制线程停止并 join（`Host.cpp:1303-1359`）。若 fatal 时 controller 已结束，`exitAutoSavePrepared` 仍为 false，现有 `controller_stopped` 日志应保留；之前按值入队的请求仍由 CloseAndDrain 排空。此条只证明源码顺序，不证明文件最终成功；需要隔离持久化状态/严格回读与真实故障进程证据。

### F-027：Host stopped 的窗口与旧 ready 门禁

- Host 绘制线程在 `Run` 返回后释放 controller、presenter、renderer，再 release-store `running=false` 并通知等待者（`Host.cpp:1216-1226`）。`ProductRunning()` 还含 `productStopping` 门（`Draw3.Product.cpp:32-50`）。`IdtMain` 在启动线程后每轮约 100 ms 检查意外停止，先 `SetOffSignal(1)` 阻断业务/状态线程继续发布，再要求双画布 Hidden、全窗口 Hide，并走既有线程 join→`StopProduct`→Window Service 停止流程（`IdtMain.cpp:2245-2335`）。正常退出已有 offSignal 时不进入该分支；主动重启的 signal=2 亦不因本分支变成崩溃重启。代码没有把图形故障伪装成 UEF `-CrashTry`。
- `IdtState.cpp:378-385` 在 reconcile 入口拒绝 Host stopped；已排队的可见性命令由 `bridgeGuard` 再核 `ProductRunning()` 与 bridge/mode revision（`:413-430`），Window Service 在 owner 线程执行前检查 `stillDesired`（`Window.cpp:1443-1453`）。旧 `firstFrameReady`、revision 即使还在 runtime snapshot 中，也不能通过这一正式 reconcile 路径重显旧 Drawpad。
- 仍只有**静态 containment**。Host 的 `running=false` 出现在资源清理后，主循环观察至多按 100 ms 轮询但会受其它主线程工作及同步窗口 Submit 影响；不能称为即时输入隔离。真实 HWND 显隐、capture、RTS producer 关闭、Windows 故障、Win7 HARDWARE/WARP 与正式 ULW/DComp Presenter 都未在本审查运行。`ProductFirstFrameReady` 在 Host 意外退出至 StopProduct 之间仍可能为旧 true，但目前主循环可见性路径另受 ProductRunning 门保护；后续调用者不得单独拿 ready 判存活。

## F-031 retained 值源调用点逐项复核

| 用途 | 生产调用位置 | 当前来源/结论 |
| --- | --- | --- |
| 活动文稿常规保存与离开当前文稿 | `capturePresentationAutoSave :5346-5353`，`SetPresentationTarget :6410`，`SetWorkspace :6528` | `document_ + pageRuntimeStates + activePresentationTarget + activeRetainedSlides` 同源；前提是 CR-01 加载安装修正。 |
| 活动 Clear 边界 | `:6582-6594` | 活动 document/history 和 `activeRetainedSlides` 同槽，Clear GUID/ordinal 仍由 builder 从请求中求。 |
| 稳定 SlideID Rebind | `:5630-5647,5684-5729` | 直接 builder 传 active map；重排后 rebuilt map 移回 active。 |
| 正常/致命 Exit 的 parked slot | `:5403-5416` | `RetainedSlidesForSave(active,&slot)` 返回 `slot.retainedSlides`，与 `slot.document/pageRuntimeStates/target/fileGuid/revision` 同槽；不会取 active map。 |
| 迟到 PreviousInterval completion | `:6253-6330` | completion 先按 key/slot 可复用性归属 active 或 parked；parked 安装其 page 后以同 `parked->retainedSlides` submit。活动则经常规捕获；具体 Office 迟到时序未执行。 |
| 冷 load/binding migration completion | `:6335-6398` | parked load 可整体安装 `loaded` slot，再用 parked map submit；active load 未移动 retained map，为 CR-01。 |
| slot 交换 | `:5229-5243` | document/history/retained/target/file/revisions 同组 swap，正常 A/B 切换保持 map 同源。 |

F-031 CLI 直接调用**生产** `BuildPresentationSaveRequest` 与 `RetainedSlidesForSave`。已读取主 agent 的红日志：CLI 两个 A/B 断言 FAIL（exit 1 由实施报告给出）；绿日志同测试 `PASS: production A/B retained source identity`（exit 0 由实施报告给出），原始路径在 `TestResults/release-hardening/draw3-ppt-retained-{red,green}-debug-arm64.stderr.log`。实施报告还记 Debug|ARM64 全 Solution、旧 PPT service/UInk 测试退出 0。本 reviewer 未重跑；这些证据证明 builder/selector 的 A/B 值源，不等于真实 Office 多文稿、迟到 completion、retained 文件 durable 写入/严格导入或 CR-01 活动加载安装通过。检视当前四个目标文件的 `git diff --check` 退出 0；其它未提交改动归属其它工作单元。

## 剩余验收边界

F-026 的活动 contact CPU-only 封口、Run 抛异常后的保存、fatal 后入队请求最终 durable 状态仍未解决/未验证；目前只能记“已完成文档在两条明确图形 fatal 分支尝试保存”。F-027 仍需隔离故障进程核双 HWND 真正隐藏、capture、RTS 停止和无死锁，不能凭 `ProductRunning` 或 Hidden 函数存在宣称通过。F-031 在 CR-01 修正并回归前保留发布阻塞；其后仍需实际 Office/WPS 多文稿、SlideID 重合、EndScreen/末页、旧索引/严格 UInk 回读及磁盘故障测试。Win7 SP1+仅 KB2670838 的 `FLIP_SEQUENTIAL` 保持，不通过禁用动态效果/改 DWM 模式制造测试通过；Hardware FL11.0/无 FL11.0→WARP 和 DComp/ULW 实机组合仍为人工门禁。
