# Research: R3 编辑框外接触（点击只结束编辑 / 拖动结束编辑并同笔起画）

- **Query**: Draw3 能否延迟 Begin + 回放样本；入口工具快照；固定位如何到达 Draw3；推荐方案
- **Scope**: internal（只读）
- **Date**: 2026-11-15
- 路径前缀：`D3=Inkeys/Inkeys/Drawing/Draw3/`，`DC=D3/Draw3.DrawingController.cpp`

## 1. 新接触的工具在哪里决定；beginShapeContact 返回 false 的后果

调用链：`processCommand`（DC:18227）→ 准入/白板移动/调试探测判定后 `initializeStroke(handle)`（DC:18268）→ `initializeStroke`（DC:16867）。

- Down 快照不可变：`handle.record->DownSnapshot()`（DC:16870）。
- 工具与样式在 Down 时读取：`downTool = window_.ActiveTool()`、`downVisualStyle = window_.ProductVisualStyleSnapshot()`（DC:16954-16955）；同批次活动接触可改写 `batchTool`（`ResolveContactDrawingTool`，DC:2685-2690、16995）；笔尾/右键橡皮覆盖得到最终 `tool`（DC:17017）。
- 锁存到 RuntimeStroke：`selectedTool/tool/visualStyle`（DC:17423-17425），直径 `CanvasDiameterForTool(tool, visualStyle)`（DC:287、17452），widthMode（DC:17043-17055）。
- 顺序（同一 lambda 内）：Pen 的触觉 `BeginHapticContact`（约 DC:16895-16917）→ touch `touchContacts.TryRegister`（DC:16948）→ metrics `Register`（DC:17032）→ 模型 `Reset`（DC:17505）→ `InitializeRuntimeShape`（DC:17541）→ **`beginShapeContact`（DC:17551-17555）** → 模型 Down `Update`（DC:17569）→ `active.push_back`/`observePalm`（DC:17647-17648）→ `acceptedStrokeBegan`（DC:17649-17657，UI 用于主栏自动收起 `Bar.Interaction.cpp:952/10348`）。
- 返回 false 时（DC:17553）：`RejectStrokeInitialization` = `input_.DiscardUntilTerminal(handle)`（DC:5330-5335），runtime 归还池。**该物理接触的后续 Move/Up 绘制线程永远看不到**（producer 持有路由到终态后自回收）。不入 active、不建模、无预测、无历史、不发 acceptedStrokeBegan。
  - 事实性附注：此路径没有调用 `touchContacts.Release`（对比 DC:17381 的拒收路径会释放）也没有 `EndHapticContact`（仅终态处理 DC:17899/22151 调用）；metrics 已 `Register` 但未 `Invalidate`。实现时需确认这三项在新路径中的归宿。

## 2. 能否延迟 Begin 并回放样本；现有先例与阈值

**输入模型**：`ContactInputCoordinator`（`D3/Draw3.ContactInput.cppm:215-245`）中 Move 是"覆盖最新"（`PublishMove` 注释"覆盖最新 Move；写门闩争用时允许丢弃"），绘制线程每帧只读一次最新快照 `TryReadSnapshot`。正常笔划同样如此：`consumeLatestSnapshot`（DC:17831-17840）在帧循环 DC:22090-22093 中逐 runtime 调用。因此"缓存原始样本"只能是 **Down 快照 + 待定期间每帧读到的最新快照**，其采样密度与正常笔划一致；没有更细的 raw 历史可回放。

**时间轴可延迟**：模型时间取 `snapshot.qpc - runtime.qpcOrigin`，`qpcOrigin = down.qpc`（DC:17523）。晚几帧再以 Down 快照起笔，时间戳仍是真实值。注意 Pen/HardPen `stroke.useDisplayTime=true`（DC:17482），显示时间路径对突发多样本的影响未实测。

**先例（跟踪接触但不建笔）**：
- 画布调试探测 `registerCanvasDebugProbe` / `pumpCanvasDebugProbes`（DC:16274-16300）：不建 RuntimeStroke，路由保持 Producing，每帧 `TryReadSnapshot` 刷新，终态时 `input_.Recycle`。这是"待定接触"最贴近的模板。
- 手势接触 `addGestureContact`（DC:16241）：同样只跟踪到终态。
- 手掌观察 `palmObservationOnly`（DC:17487-17500、17856-17864）+ 中途提升 `PromotePalmRuntime`（DC:2844-2941）：runtime 已在 active 但不建模，满足条件时在当前样本处就地重建模型并改 `tool=Eraser`。这是"同一接触中途决定工具"的现成先例，但它从提升时刻的样本起笔，不回放更早样本。
- 通用图形点击取消：`if (runtime->shape.generic && !runtime->shape.object) runtime->cancelled = true;`（DC:22777-22778，注释"点击/零面积 Up 是正常取消，既不写历史也不触发完成回调"）。

**`consumeLatestSnapshot` 是从 input_ 读快照的闭包**，没有"喂给定快照"的入口；真正回放中间样本需把其主体拆出 `consumeSnapshot(runtime, snapshot)`。最小替代：起笔后下一次 `consumeLatestSnapshot` 读到最新位置，与 Down 点连成直线段——起笔段不丢，只丢阈值半径内的弯曲细节。

**现有阈值常量**：
- `kCanvasPanTouchSlopDip = 15.0f`（`D3/Draw3.CanvasNavigation.cppm:22`，画布平移触摸 slop）。
- 系统拖动矩形 `GetSystemMetrics(SM_CXDRAG/SM_CYDRAG)`（`UI/PageControl/PageControl.cpp:2917/3100`、`Bar.Interaction.cpp:5181` 调色板点击）。
- `BarThicknessPreviewTouchSlopDip = 5.0`（`Bar.Interaction.cpp:134`，UI 内）。
- `kRawMoveThresholdPx = 0.25f`（DC:214）、`kIdleMoveThresholdPx`（`Draw3.StrokeGeometry.cpp:242`）是去噪，不是点击判定。
- Draw3 内没有现成的"画布点击 vs 拖动"常量；显示比例可从 `shapeScene().displayScale`（ShapeRuntimeActions 中已使用）换算 DIP→像素。

## 3. 入口工具快照

- UI 侧：`ChangeStateModeToShape`（`Inkeys/IdtState.cpp:2454-2473`）首次进入几何时记录 `shapeReturnMode`（只存 `StateModeSelectEnum`，穿透映射为 Pen）、`shapeReturnWorkspace`、`shapeReturnEntryEpoch = ShapeToolEntryEpoch().fetch_add(1)+1`（IdtState.cpp:46-48、2460-2462）。笔型子模式（`Pen.ModeSelect`/`laserActive`）进入几何时不被改写，恢复模式即恢复子模式。
- `RestoreToolAfterShapeEditing(workspace, entryEpoch)`（IdtState.cpp:2475-2496）：epoch/工作区/当前仍为 IdtShape 均匹配才恢复，然后 `SyncDraw3State()`。
- Draw3 侧：`ShapeToolEntryEpoch()` 只是计数器（`D3/Draw3.ShapeInteraction.h:21`）；编辑器打开时锁存 `editor.toolEntryEpoch`、`editor.tool = window_.ActiveTool()`、`editor.toolSpec = ...shapeTool`（`D3/Draw3.ShapeRuntimeActions.inl:381-383`）；`finishShapeEditing`（同文件 :327-335）把 session/epoch/workspace 写入 `shapeUi.finishedEditing*`，经 `publishShapeUi` 进入 `ShapeUiSnapshot`（:6）；`SelectionOverlay.cpp:129-140` Tick 发现新 (session,epoch) 后排队调用 `RestoreToolAfterShapeEditing`。
- **Draw3 当前不知道入口工具的种类与样式**。发布路径：`PublishDraw3StateLocked`（IdtState.cpp:613-650）按当前 stateMode 计算 `tool = CurrentDraw3Tool()`、`colorRgba = GetPenColor()`、`widthDip = GetPenWidthMetrics().resolvedWidthDip`、`shapeTool/shapeStyle` → `Bridge::ProductState`（`D3/Draw3.Bridge.h:252-272`）→ Host（`D3/Draw3.Host.cpp:1818-1851`）`window.SetActiveTool(tool)` + `SetProductVisualStyle(color,width,ctx,shapeTool,shapeStyle)` → `ProductVisualStyle`（`D3/Draw3.WindowControl.cppm:95-102`，单一 color/width）。
- 样式重合情况（IdtState.cpp:1926-1940、2056-2072）：几何模式的颜色/粗细取 `Brush1`。入口为 软笔/硬笔 时 color/width 与几何下发布的值相同，只差 `DrawingTool`；入口为 荧光笔（Highlighter1 色/宽/Family）、激光（Laser 色/宽）时不同；橡皮：`SetEraserToolPolicy/SetEraserWidthMode` 对 ConfiguredEraser 与 Shape 均得 ByEntry/Fixed（Host.cpp:1826-1849），橡皮样式不依赖 color/width；入口为 Selection 时不是绘制工具。
- 结论：要不等 UI 恢复就用入口工具起笔，需要 UI 在进入几何时（或每次发布时）额外下发"入口工具快照"{epoch, Bridge::Tool, colorRgba, widthDip}；可在 `PublishDraw3StateLocked` 中对 `stateMode` 副本把 `StateModeSelect` 设为 `*shapeReturnMode` 后复用 `CurrentDraw3Tool/GetPenColor/GetPenWidthMetrics` 计算（这三个函数现以全局 `stateMode` 或快照为参数，`CurrentDraw3Tool` 读全局，需要注意）。

## 4. 方案比较与推荐

**(B) Down 立即按预定工具起笔，Up 未超阈值则取消**
- 优点：完全复用起笔路径，无回放问题；固定时点击取消已有先例（DC:22778）。
- 风险：点击期间会短暂出现笔点并在 Up 后消失；Pen 触觉在 Down 已开始；`acceptedStrokeBegan` 在 Down 发出导致主栏自动收起；未固定时必须在 Down 就知道入口工具（同第 3 节需求）；激光点击会产生粒子/层；手掌/续接判定以该工具身份进行（`InterruptedStrokeReconnectIdentity` DC:17057-17064）。

**(A) 待定接触 + 超阈值后起笔（推荐）**
- Down：维持现有 `finishShapeEditing` + `endCanvasFocus(NewContact)`（先落定 L1→L2，保证新笔可用 L0/L1），但不 `DiscardUntilTerminal`，而是登记到一个仿 `debugProbeContacts` 的 `pendingOutside`（handle、Down 快照、固定位、入口工具快照、可选的每帧样本）。
- 每帧（与 `pumpCanvasDebugProbes` 同位置）：读最新快照；Up/Cancel 且未超阈值 → 点击：未固定则发 finishedEditing（现有恢复），固定则不发；`input_.Recycle`。超阈值 → 以覆盖工具调用起笔逻辑，Down 点为起点，随后由常规 `consumeLatestSnapshot` 接续（Down→当前点的直线即为起笔段）；若要求精确回放，再拆 `consumeSnapshot` 逐个喂入缓存帧样本。
- 风险：
  - RTS/输入：待定期间路由仍在 producer，需在终态 `Recycle`，不能 Discard；多个触点同时待定、白板移动/准入闸门关闭期间要一并清理。
  - 预测：起笔帧一次性喂入多样本，预测器可能出现一帧跳动；Pen 显示时间（useDisplayTime）需要验证。
  - 历史/撤销：笔划只在 Up `CommitRuntimeStoredStrokeCpu`（DC:22779-22786）写历史；图形 revision 在 `beginShapeContact` 中通过 `nextShapeRevision` 分配，起笔时才调用即可，两条历史各自独立（编辑提交在先，新笔在后）。
  - L0/L1：`endCanvasFocus(NewContact)` 必须在新笔进 active 前执行（现有顺序已满足）；DC:22760-22762 每帧清 L0/L1 并由 `RebuildActiveLayers`（DC:6097）重建。
  - 迟到恢复：拖动时发出 finishedEditing 后，UI 异步恢复入口工具；这笔已锁存 `runtime->tool`（DC:17424），恢复不影响这笔。若用户在此期间改选工具，`RestoreToolAfterShapeEditing` 的 epoch/模式检查（IdtState.cpp:2480）会拒绝，这笔仍是入口工具——与 UI 不一致但不会覆盖用户选择。
  - 手掌：待定期间未 `observePalm`，手掌橡皮判定会推迟到起笔后。
  - 起笔时 `initializeStroke` 内部重新读 `window_.ActiveTool()`（DC:16954），因此需要显式传入覆盖工具/样式参数，否则 UI 已恢复或未恢复会导致结果不确定。

**最小改动草图（A）**
1. `Bridge::ProductState` / `ProductVisualStyle` 增加 `bool shapePinned` 与 `ShapeEntryTool{epoch, Tool, colorRgba, widthDip}`；`PublishDraw3StateLocked`（IdtState.cpp:613）填充；Host.cpp:1850 `SetProductVisualStyle` 透传。
2. `finishShapeEditing`（ShapeRuntimeActions.inl:327）增加"是否发恢复通知"参数（固定时不写 finishedEditing*）。
3. `beginShapeContact`（:497）编辑器打开时返回第三态（如 enum：Begin / Swallow / Pending），由 DC:17551 处理 Pending：登记待定，不 Discard。
4. `initializeStroke`（DC:16867）接受可选覆盖 {tool, visualStyle}，替换 DC:16954-16955 的两次读取。
5. 帧循环在 DC:22090 附近增加待定接触推进（阈值判定 → 点击收尾或调用 4 起笔）。
6. 可选：从 `consumeLatestSnapshot`（DC:17831）拆出按快照消费的主体以精确回放。

## 5. 固定位如何到达 Draw3

- `ToolSpec`（`inkStrokeModelerTest/draw3/shape_core.h:46-56`）带默认 `operator==`，并被用于构造会话匹配（`shapeUi.construction->tool != runtime.shape.tool`，ShapeRuntimeActions.inl:513-514）及 `ResolveContactDrawingTool`；把 `pinned` 放进 ToolSpec 会改变这些比较语义且属于共享 shape core。
- 发布通道已存在：`PublishDraw3StateLocked` → `ProductState.shapeTool` → `SetProductVisualStyle(..., shapeTool, shapeStyle)`（Host.cpp:1850，WindowControl.cpp:1296-1308）。在 `ProductState` 与 `ProductVisualStyle` 并列加 `shapePinned` 字段即可与 ToolSpec 同锁、同修订到达绘制线程；`IsShapeToolPinned` 在 UI 的模式锁内计算（Line/Arrow 或 `shapePinnedPreset == spec.preset`）。
- 判定时机：编辑器锁存的是打开时的 `editor.toolSpec`（:383）；固定可能在编辑期间被长按改变，框外 Down 时宜读当前 `window_.ProductVisualStyleSnapshot().shapePinned`。

## Caveats / Not Found

- 未实测 Pen 显示时间路径在突发多样本时的表现；未逐行确认橡皮实时渲染是否完全只走 L0/L1（依据 DC:22760-22762 与 `RebuildActiveLayers`）。
- `CurrentDraw3Tool()` 读全局 `stateMode`，不接受快照参数，计算入口工具需要小幅改造或局部重写映射。
- Draw3 中没有现成的画布"点击 vs 拖动"阈值常量，需要选定（如 `kCanvasPanTouchSlopDip` 或系统 SM_CXDRAG）。
