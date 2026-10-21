# Research: Desktop 载入 A 后仅改变 Viewport 的退出保存

- Query: Desktop 已从 `.uink` 恢复 A，不新画 B，仅滚动改变 Viewport 后正常 Exit，现有捕获、dirty、去重和 UInk 链是否保存新 Viewport；确认载入入口边界及真实隐藏用例所需接口。
- Scope: internal（当前共享工作区源码与 spec 静态分析；未编译、未运行 CLI/GUI）
- Date: 2026-10-21

## Findings

### 结论

**在 A 已成功进入 Desktop 当前页且仍有可见 history item、Desktop 自动保存开启、worker root 有效、最终 Viewport 已被 owner 接受的前提下，当前链会在正常退出捕获并保存新的 Viewport；不需要再画 B，也不依赖 Desktop dirty revision 增长。** 这是源码结论，尚未运行该场景的真实持久化隐藏用例。

理由是 Desktop 保存资格只看 `enabled + Desktop + history.LastVisibleItem()`；退出直接读当前 CPU `InkCanvas::Viewport()` 的 `x/y/scale`，每次生成新 file GUID，再生成新 save request ID。没有发现基于“笔迹内容没变”或“恢复后未新增笔画”的跳过逻辑。现有去重针对同一 request ID 的幂等；Viewport 不会在该处被忽略。

**需要与此分开记录：Host 当前没有任意既有外部 `.uink` 的 Desktop 冷载/替换命令。** 本进程内 Clear 成功保存后，再 Undo 经真实 worker 读回 A，是已有产品路径；跨进程/重新启动后导入任意 A 的场景不能用这条路径冒充。本研究不建议为满足测试新增产品冷载功能。

### Files found

| 文件 | 相关职责 |
| --- | --- |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` | Desktop capture、Clear 恢复、Viewport owner 应用、正常退出屏障 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cppm` | 真实 Controller observer/API；document 为 private，没有公开导入/替换快照方法 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp`、`.h` | 保存 worker 所有权、保存/加载 observer、Follow End→Exit 顺序、隐藏计数与入口 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.AutoSave.cpp`、`.cppm` | Desktop 保存资格、请求幂等、新文件提交、同 service 已提交记录回读、严格磁盘 fixture reader |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.ScrollFollow.h` | 最后目标冻结与 Shutdown 的屏障语义 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Bridge.h`、`.cpp`、`Draw3.WindowControl.cppm` | 已有产品/Controller 命令；没有通用 Desktop Import/Load/Replace 命令 |
| `inkStrokeModelerTest/draw3/ink_document.cpp`、`ink_history.cpp`、`canvas_navigation.cpp` | CPU Viewport 赋值、history 可见项、平移及裁剪；这些不重写 A 的点 |
| `inkStrokeModelerTest/draw3/uink_draw3_export.cppm`、`uink_model.cppm` | 快照及 UInk Viewport 字段定义 |
| `inkStrokeModelerTest/draw3/uink_draw3_export.cpp`、`uink_draw3_import.cpp` | Viewport 与原始点的导出/导入 |
| `inkStrokeModelerTest/draw3/uink_codec_encode.cpp`、`uink_codec.cpp`、`uink_file.cpp` | `canvas.viewport` MessagePack 字段、回读与文件完整性 SHA |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.HiddenWindowTest.cpp` | 当前严格不可见 Follow Host 测试；尚不构成保存场景验证 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.FailedCleanupFixture.cpp` | 测试快照的完整值编码/哈希 receipt；其所谓 geometry digest 也包含 Viewport 与 file GUID |
| `inkStrokeModelerTestTests/desktop_autosave_tests.cpp` | 已有 worker 层 Committed Clear→SubmitLoad 回归；不是完整 Host 退出保存测试 |

### Code patterns：Viewport-only 保存链

1. **恢复 A 后保存资格成立。** `materializeCanvasPage` 逐笔复制 source points，并通过 `runtime.history.AppendStroke(..., true)` 建立可见 history（`Draw3.DrawingController.cpp:12730`、`:12773`）。`LastVisibleItem()` 返回已缓存的最后可见 history item，不按当前屏幕/Viewport 交集过滤（`ink_history.cpp:936`）。因此 A 被滚到屏幕外也不会因“屏幕无像素”失去该项资格。
2. **滚动改变的是实际 CPU 画布。** `applyFollowTarget` 计算 `target = baseViewport - absoluteContentDelta`，裁剪后经 `ApplyCanvasContentTranslationChecked` 与 `canvas->SetViewport({next.x,next.y,1})` 更新 Canvas，再回显 Applied（`Draw3.DrawingController.cpp:13711`）。`InkCanvas::SetViewport` 只校验并赋值 `viewport_`（`ink_document.cpp:97`）；它不移动 Stroke points。原 `TranslateViewport` 分支同样写回 Canvas（Controller `:14475`）。
3. **Desktop 不依赖 PPT 的 dirty。** `markPresentationMutation` 对 Desktop 不增加任何 mutation（Controller `:9113`），但 Desktop capture 根本不比较 mutation/queued/committed revision。`DesktopAutoSavePolicy::ShouldCapture` 仅返回 `enabled && hasVisibleContent && workspace==Desktop`（`Draw3.AutoSave.cpp:950`）；`CompleteDesktopClear()` 为空实现，没有“已经保存 A 后退出不再保存”的状态锁。
4. **退出 capture 含最新 Viewport。** 实际 helper 名称是 `CaptureDesktopAutoSaveForScene`，当前源码未找到精确符号 `CaptureSaveSnapshot`。helper 读取当前页 `canvas->Viewport().x/y/scale` 到 `outputCanvas.viewport`（Controller `:1362`、`:1401`），并从 history 的可见项复制 A 的 style 和原始 `(x,y,width)`（`:1403`）。每次 capture 新建 file GUID（`:1384`），复用 document 的 workspace GUID 与 page GUID。局部 `captureDesktopAutoSave` 只是活动/parked Desktop 的路由包装（`:9366`）。
5. **Exit 没有“恢复后未画 B”过滤。** `captureExitAutoSave` 重新以当前/parked Desktop 的 `LastVisibleItem()` 计算资格，调用 Exit capture（Controller `:12570`、`:12604`）。`PrepareExitAutoSave` 经 `ProcessPresentationExitBarrier` 调用它（`:14366`）。该通用退出屏障同时服务 Desktop/PPT/Whiteboard，名称不表示 Desktop 走了 PPT dirty 规则。
6. **最终目标先于退出快照。** `Host::Stop` 在 `commandPublishMutex` 内先 `EnsureFollowEndLocked(Shutdown)`，再 `StopWithFinalCommand(PrepareExitAutoSave)`（`Draw3.Host.cpp:2276`）。End 会冻结 `final=latest`（`Draw3.ScrollFollow.h:69`）；Controller 消费 `FollowEnd` 时先 `applyFollowTarget(true)`（Controller `:13770`）。Shutdown 的 `BarrierReady` 不等待可见窗口 ACK（`Draw3.ScrollFollow.h:116`），因此后继 Exit 能捕获已写回 CPU 的最终值。正常 restore End 的显示/窗口握手是另外的验收内容。
7. **Host 收到快照后直接交给 worker。** `ObserveDesktopAutoSave` 直接调用 `autoSave.Submit(trigger,std::move(snapshot))`，没有内容比较（`Draw3.Host.cpp:1166`）。`Stop` 等待最终捕获回执后 `autoSave.CloseAndDrain()`，再退出并 join 绘制线程（`:2293`、`:2308`）。捕获 ACK 仅表示已交 worker；真正 durable 还需 worker Committed 与磁盘回读。

### Code patterns：去重、比较与哈希分别做什么

| 位置 | 实际依据 | 对本场景的含义 |
| --- | --- | --- |
| `DesktopAutoSaveService::Submit`，`Draw3.AutoSave.cpp:1304` | 每次创建新的 `saveRequestId`、时间戳及递增 sequence | Exit 是新请求，即使笔迹等于 A 也会入队 |
| `Impl::SubmitLocked`，同文件 `:1214` | `records.find(request.saveRequestId)` | 仅相同 request ID 的幂等，不是按 stroke/Viewport 哈希去重 |
| `ExistingUInkMatches`，同文件 `:743` | 目标文件完整可读且 `header.guid == request.snapshot.fileGuid` | 解决同请求/候选路径提交幂等；capture 的新 file GUID 不会误命中载入 A 的旧文件 |
| `NamedMutexGuard::Acquire`，同文件 `:254` | FNV-1a 哈希规范化 root 与日期 | 仅命名每日索引 mutex，与快照内容或 dirty 无关 |
| `CommitUInk` / `ProcessRequest`，同文件 `:751`、`:789` | 导出该次快照、create-new UInk、提交每日索引 | 生成新的 Exit 文件/索引项，旧 A 文件保留 |
| `uink_file.cpp` 的 SHA/source revision | 编码字节完整性、自验证与事务身份 | 不是“内容未变便跳过退出保存”的策略 |
| `FixtureValueEncoder::Canvas/Snapshot`，`Draw3.FailedCleanupFixture.cpp:250`、`:275` | 哈希包含 Viewport 的三字段，也包含 workspace/file/page GUID、style、points 等 | fixture 的 `geometrySha256` 不是纯 Stroke 几何哈希；Viewport-only Exit 的新 file GUID 与新 Viewport 都应让其变化 |

所以测试不能只比较整文件 SHA 或完整 `SnapshotReceipt.geometrySha256` 来宣称“A 的几何没变”。应直接比较 A 的 Stroke/style/每个点，再单独断言 GUID 与 Viewport。

### Code patterns：UInk 字段不会丢失 Viewport

- `Draw3UInkCanvasSnapshot::viewport` 是 `UInkViewport`（`uink_draw3_export.cppm:58`）；该类型持有 float `x`、`y`、`scale`（`uink_model.cppm:164`）。它不藏在可省略的临时诊断字段中。
- `ExportDraw3SnapshotToUInk` 要求三值有效、`scale==1`，然后直接写 `UInkCanvas.viewport = sourceCanvas.viewport`（`uink_draw3_export.cpp:354`、`:406`）。普通 Pen 的点原样复制 `(x,y,width)`，不把 Viewport 加到点上（同文件 `:137`、`:167`）。
- codec 写 `canvas.viewport` 对象的 `x/y/scale` 三个 float（`uink_codec_encode.cpp:724`）；读者读回对应三字段并检验 finite/positive（`uink_codec.cpp:1163`）。
- `ImportDraw3UInkDocument` 把已解码的 `canvas.viewport` 原样放入 snapshot（`uink_draw3_import.cpp:247`、`:253`），普通 Pen 的点也原样导入（`:33`、`:52`）。文件层已有保存/回读表达能力，无需改 UInk 格式。

### 既有 `.uink` 进入 Host 的准确边界

| 层级 | 现有入口及约束 |
| --- | --- |
| 共享文件/codec | `ReadUInkFile(path)` → `ImportDraw3UInkDocument(document)` 得到值快照；不自动替换产品 Host 的 document |
| Desktop worker | `DesktopAutoSaveService::SubmitLoad(fileGuid)` 只扫描当前 service 的 `records`，要求该记录为 Committed、GUID 匹配且有 `committedPath`（`Draw3.AutoSave.cpp:1339`）；不是任意路径冷载 |
| Host→Controller | `ObserveDesktopLoad` → 真实 worker → `PumpDesktopCompletions` 投递 `CanvasCommandType::DesktopPersistenceCompleted`（`Draw3.Host.cpp:1184`、`:1744`） |
| Controller 安装 | completion 必须匹配当前 `desktopClearRecovery`、file GUID、pending 状态、页面/历史代次，才调用 `restoreCurrentPageFromSnapshot`（Controller `:13816`）。不能随便塞一份 imported snapshot 的 completion 来绕过这些资格 |
| 正式可触发的恢复 | Desktop Clear 的 Save completion 成功后设 `diskReady` 并释放 CPU fallback（Controller `:13823`）；Undo 到 floor 时触发 `desktopLoadRequested`（`:14386`）；worker 真读文件，Loaded 后安装并消费恢复点 |
| 非公开内部实现 | `materializeCanvasPage` / `restoreCurrentPageFromSnapshot` 是 `DrawingController::Run` 内部 lambda；`DrawingController::document_` private（`Draw3.DrawingController.cppm:188`），没有公开 snapshot import/replace setter |
| 产品命令 | `Bridge::CommandType` 没有通用 Import/Load/Replace；`Save`/`SuperRecovery` 仍返回 Unsupported（`Draw3.Bridge.cpp:145`）。`SetWorkspace` 切已有 slot，`SetPresentationTarget` 是 PPT lane，并非 Desktop 导入 |

**特殊但既有语义：** `restoreCurrentPageFromSnapshot` 虽先按 snapshot 构建 page，随后明确把新 Canvas Viewport 设为当前 Canvas 的 Viewport（Controller `:13665`、`:13679`）。这是 Clear Undo 保持当前视角的恢复路径，不能用它证明“任意冷载文件后恢复了文件里的旧 Viewport”。本问题的后半段——已恢复 A 后再滚动，再保存新 Viewport——不受此影响。

### 真实隐藏保存用例所需接口与验真条件

当前 `RunScrollFollowInvisibleHostTest`（`Draw3.HiddenWindowTest.cpp:3715`）证明 owner 平移与 End/FIFO/Shutdown，但它没有给 `HostStartOptions.autoSaveRoot` 赋值，`ProductState.autoSaveEnabled` 保持默认 false；首段 End 之后还真实 Undo 移除了 A。第二段 Shutdown 只核对 `channel->Read().viewportX/Y`（`:3797`、`:3803`），**不能算 UInk 落盘通过**。

可沿已有产品路径新增一个独立隐藏用例，无需产品冷载能力：

1. 沿用严格不可见 HWND Host 启动方式；给 `HostStartOptions` 设置独立仓库短私有 `autoSaveRoot`、`enableHiddenTestContactInjection=true`。发布 `Bridge::ProductState{workspace=Desktop, autoSaveEnabled=true, selectionMode=false,...}`，等待正式输出 ready。空 root 或开关关闭属于明确不保存的负例。
2. 通过现有 `kDraw3HiddenTestContactMessage` 的 Down/Move/Up 形成基准 A，完成 contact 收尾；用 `PublishProductCommand(Clear)` 走正式 Clear 保存。等待真实 worker Committed，严格读取该次 Clear 文件 A 的 workspace GUID、PageGUID、Viewport、Stroke/style/完整点数组，保留基准值。
3. 等到对应 Committed completion 已被 Controller 消费、CPU 恢复快照已释放，再 `PublishProductCommand(Undo)` 触发 `.uink` 磁盘读取并安装 A。当前源码的释放与安装分支见上述行号。`HiddenPersistenceSnapshot().desktopCommitted` 是 worker 计数，更新在 completion 发布之前（`Draw3.AutoSave.cpp:1193`、`:1210`），**只等这个计数不能独立证明 Undo 已走磁盘分支**。fixture 必须安排或取得该分支的可靠证据，不能用固定 sleep 或仅 `currentPageHasContent=true` 代替；本研究不预设必须新增 Loaded counter，优先复用既有 test 内部控制/观察接缝，确需补证时仅作 hidden gated receipt，不开放新的产品导入功能。
4. A 安装后记录 `inputDownPublished/inputTerminalPublished` 等基线，此后不再发送任何新 contact。通过 `Host::BeginFollow`、`FollowChannel()->Read/Publish(Target)`、`Host::WakeFollow` 驱动真实 owner 更新；使用 `Read().identity` 和 `baseX/baseY`，以同一 scene 的绝对累计内容位移计算期望 `viewport = base - total`。这验证保存链的 Host 接缝，不能代替真实 Capture/Tracker 的端到端滚动验收。
5. 不再画 B，也不 Undo 掉 A；直接使用 `StopProduct()`/`Host::Stop()` 的正式退出序列。应包含一次“Publish 最后目标后不预等 Applied，立即 Stop”的情形，证明冻结的最终 CPU Viewport 位于退出 capture 前。
6. Stop 返回后，使用已有 `ReadLastCommittedDesktopAutoSaveFixture(root, localDate)` 严格回读最终每日索引及所指文件（`Draw3.AutoSave.cppm:111`、`Draw3.AutoSave.cpp:869`）。核对 `status==Loaded`、`trigger==Exit`、新 file GUID/sequence、Desktop workspaceType、恰一页；跨午夜时按实际索引日期定位，不凭文件修改时间猜最后项。
7. **核心断言：** 最终 workspace GUID、PageGUID 与基准 A 相同；A 的 Stroke 数、style、点数及每点 `(x,y,width)` 与 A 逐项一致；无 B/新 Down；最终 `canvas.viewport.{x,y}` 等于 owner 期望值（考虑 float/裁剪），`scale==1`，且确实不同于滚动前。最终 receipt/文件 SHA 变化只作辅助证据，不能代替这几项。

已有 `RunParkedDesktopExitAutoSaveTest` 用真实 `CaptureDesktopAutoSaveForScene` 捕获 active/parked Desktop，并核对 workspace/PageGUID 与 A 的坐标（Controller `:7731`、`:7817`）；可在其同 TU 的 CPU helper 测试中增加 Viewport-only capture 断言作为小范围单测。`TestCommittedClearCanReload`（`desktop_autosave_tests.cpp:847`）只覆盖 worker 保存后回读；两者均不能替代上述真实 Host 退出落盘用例。

### 最小修复点判断

- 当前未发现导致 Viewport-only Exit 被去重/dirty 吞掉的生产缺口，**无需为此改 Desktop dirty、请求 ID、UInk schema 或新增产品冷载功能**。
- 当前明确缺少的是该场景的真实保存回归证据；最小后续范围是独立隐藏用例及必要的有限观察接缝。是否需要额外 Loaded/install receipt 应由测试能否使用现有确定性证据决定，不预先强制加 API。
- 如果后续保存优化引入按内容跳过 Exit，比较/哈希必须覆盖 `canvas.viewport.x/y/scale`，并确认导入 A 后的清洁状态不会屏蔽 Viewport 变化。该项是未来防回归要求，不是本次发现的现有错误。

### Related specs

- `.trellis/workflow.md`：research 只写当前 task/research，不实施代码修改。
- `.trellis/spec/native-desktop/draw3-integration.md:118`：Desktop Clear/Exit 门控、owner 值快照、独立 worker、退出 drain；PPT dirty 合同不套用于 Desktop。
- `.trellis/spec/native-desktop/draw3-integration.md:196`：PPT mutation/revision 是另一条保存事务，不能用它推定 Desktop 漏存。
- `.trellis/spec/native/quality-and-validation.md`：独立 helper/worker 与产品 Host 验证分开记录。
- 当前 task `design.md:17`、`:23`：owner 基准 Viewport、绝对目标和 Shutdown 最终 CPU Viewport 必须先于 ExitAutoSave。

### External references / versions

本题仅依据当前仓库的 C++/UInk 实现，无需外部文档或依赖版本结论。文件行号对应本次读取时的共享工作区，Core 后续编辑可能使行号移动。

## Caveats / Not Found

- 没有运行任何编译、CLI、GUI 或 Computer Use；这里的“会保存”是有明确前提的源码结论，不是该隐藏场景实测通过。
- 没有找到精确名为 `CaptureSaveSnapshot` 的现有符号、Desktop 按内容哈希去重、公开 Host/Controller 任意 Desktop 快照导入/替换方法。
- 任意既有文件的冷载能力与本进程 Clear Undo 磁盘恢复必须区分；共享 codec 能读出快照，不等于产品已把它安装进 Host。
- 保存关闭、空 history、无 worker root、最终目标身份被拒绝、文件/索引 I/O 失败仍可合法或明确失败地不产生新的 Committed 文件；这些条件不应被“仅 Viewport 改变”结论掩盖。
- 本研究只新增此报告，保留 `storage-baseline-current.md` 原样，没有修改/回退 Core 或其他工作，也没有重复运行独立 storage 基线。
