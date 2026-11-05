# Research: 图形编辑拖动（缩放点/旋转钮）跟手性能

- **Query**: 真机上图形绘制后拖动缩放控制点或旋转按钮非常慢、跟不上手指/笔，事件堆积、图形慢慢追随指针。端到端追踪输入→意图→绘制线程→覆盖层重绘，定位根因并估算成本；对比普通笔迹 RTS 路径与主栏/PageControl 拖动；给出理想架构方向、覆盖测试与可用度量。
- **Scope**: internal
- **Date**: 2026-11-05
- **Branch**: feature/shape

---

## 0. 结论速览（按影响排序）

| # | 根因 | 证据 | 粗略成本 |
|---|---|---|---|
| R1 | **PreviewEdit 无"最新者胜"合并**：输入 worker 每处理一个 Move 就发布一条 PreviewEdit 到 Draw3 Bridge FIFO（容量 256），绘制线程按 FIFO **逐条完整执行**，同一帧内把积压全部重放一遍，只在最后 Present 一次 | `Bar.ShapeEditor.cpp:711`、`Bar.ShapeEditor.cpp:134-151`、`Draw3.Bridge.cpp:165-181`、`Draw3.Host.cpp:1884-1934`、`Draw3.DrawingController.cpp:16113-16148` | 延迟上限 ≈ 队列深度 × 单次预览成本（最坏 256 × 单次成本，秒级）|
| R2 | **单次预览是"文档级事务"**：每条预览深拷贝整页 `InkCanvas` + `CanvasPageRuntimeState`，丢弃全部 GPU composition 缓存与 undo 热像，再对 旧∪旧预览∪新 足迹的所有 256px tile 从零重建合成树 | `Draw3.ShapeRuntimeActions.inl:42-111`（尤其 50、70-75）、`Draw3.InkHistoryGpu.cpp:1358-1514`、`1553-1560`、`442-530`、`920-1010` | 拷贝 O(页面总点数)；每 tile 重新栅格化所有覆盖该 tile 的笔迹 + 重新 CreateTexture2D 合成页 |
| R3 | **覆盖层位置依赖绘制线程回包**：控制点/外框/旋转钮位置取自 Draw3 发布的 `ShapeUiSnapshot`，不是指针位置；因此 R1/R2 的全部延迟直接表现为"控制点追不上手" | `SelectionOverlay.cpp:48,56`、`Bar.ShapeEditor.cpp:413-419,516-539,605-610`、`Draw3.ShapeRuntimeActions.inl:125-162` | 视觉延迟 = 输入 worker + Bridge 排队 + 预览 N 次 + Draw3 Present + UI 帧 |
| R4 | **覆盖层每帧重建与全窗口 ULW**：几何每帧（identity 每次变）重建两次；缩放/旋转时 bounds 尺寸每帧变 → 重建整个 D2D DeviceContext+GDI 兼容位图；`GetDC(COPY)` + `UpdateLayeredWindowIndirect` 覆盖 图形包围盒∪工具条 的整窗 | `Bar.ShapeEditor.cpp:329-343,411-425,658-662,665-681`、`Bar.Rendering.cpp:312-352`、`SelectionOverlay.cpp:56-69` | 大图形时每帧数 MB 的 CPU 读回+ULW；尺寸变化时设备资源重建 |
| R5 | 次要放大器：预览修订号每次递增导致 `shapeRenderCache_`（128 条，`erase(begin)`）被预览修订挤满，页面上其他图形的 RenderData 被逐出；每次 Snapshot 发布还额外请求 Bar 重绘；每条 Bridge 命令前还会插入一条场景前缀命令 | `Draw3.RendererShapes.cpp:154-170`、`SelectionOverlay.cpp:41`、`Draw3.Host.cpp:1893` | 页面图形多时 tile 重建还要重新 BuildRenderData |

ULW 呈现模式下（`Draw3.TransparentPresentation.cpp:536-541`）主画布 Present 会 `Map` staging 纹理同步等待 GPU，R2 的 GPU 重建成本会直接计入绘制线程 CPU 帧时间。

---

## 1. Findings

### 1.1 Files Found

| File Path | Description |
|---|---|
| `Inkeys/Inkeys/UI/SelectionOverlay/SelectionOverlay.cpp` | 编辑覆盖层窗口：WndProc 收集指针、RenderPipeline 客户端 Render、输入回调转发到 `ShapeEditorSurface` |
| `Inkeys/Inkeys/UI/Bar/Bar.ShapeEditor.cpp` | `ShapeEditorSurface`（布局/命中/绘制/Present/Pointer）、`ShapeIntentSender`、`SubmitShapeUiIntent`、离屏测试 `RunShapeEditorOffscreenTest` |
| `Inkeys/Inkeys/UI/Bar/Bar.InteractionWorker.h` | `InteractionMailbox`（512 容量 FIFO，仅合并相邻同接触 Move）、`SurfaceInputEvent` |
| `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp` | `SubmitSurfaceInput` 合并谓词、注册/唤醒 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.ShapeInteraction.h` | `ShapeUiIdentity/ShapeUiSnapshot/ShapeUiAction` |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Product.cpp` | `PublishShapeUiAction`、`PublishShapeUiSnapshot`（触发 refresh 回调）|
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp` | `Host::PublishShapeAction`（预约 command wake）、`PumpOneBridgeCommand` |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Bridge.cpp/.h` | `StateBridge::PublishShapeAction`、FIFO 容量 256 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.cpp` | `TryReserveCommandWake`（`kCommandWakeCapacity = 258`）|
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` | `processCanvasCommands` 主循环、帧节拍 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.ShapeRuntimeActions.inl` | `replayShapeEdit`、`processShapeUiAction`、`publishShapeUi` |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.ShapeRuntime.inl` | `ShapeEditorRuntime`、`PublishShapeRuntimeSnapshot`、身份校验 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.InkHistoryGpu.cpp` | `RestoreComposition`、`DiscardCompositionCache`、`GetOrBuildNode`、`RasterItem`、`CreateOperatorPage` |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RendererShapes.cpp` | `EnsureShapeLayers`、`DrawShapeObject`（RenderData 缓存）、`DrawShapeRenderData` |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.StrokeGeometry.cpp` | `DrawStoredStroke`（tile 栅格化时 `SetScreenSize(256,256)`）|
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cpp` | 主画布 Present（DComp 或 ULW+CPU 读回）|
| `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cpp` | UI 单渲染线程、按帧率节拍批处理客户端请求 |
| `Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp` | `BarUIRendering::EnsureDeviceResources/RecreateDeviceResources` |
| `Inkeys/Inkeys/UI/PageControl/PageControl.cpp` | 对照：PPT 翻页控件拖动（纯平移直移 HWND + 替换 pending）|

### 1.2 端到端管线（线程与队列）

```
[覆盖层窗口线程] WM_MOUSEMOVE / WM_TOUCH→合成 WM_MOUSEMOVE
   SelectionOverlay.cpp:120-129  组装 SurfaceInputEvent(presentedRevision=PresentedGeneration)
   └─ Bar::SubmitSurfaceInput → sharedInputMailbox(FIFO 512)        Bar.Interaction.cpp:1963-1997
        合并条件：相邻 & 同窗口 & 都是 MOVE & presentedRevision/captureTicket/contactId/wParam 相同
[交互 worker 线程] Pop → SelectionOverlay::Input → ShapeEditorSurface::Pointer
   Bar.ShapeEditor.cpp:706-714  gesture 中每个 MOVE → ShapeUiAction{PreviewEdit} → Emit
   └─ SubmitShapeUiIntent → ShapeIntentSender::Send → PublishShapeUiAction   Bar.ShapeEditor.cpp:134-151,185-188
        → Host::PublishShapeAction (预约 command wake, cap 258)            Draw3.Host.cpp:3291-3299
        → StateBridge::PublishShapeAction (FIFO cap 256, 无合并)            Draw3.Bridge.cpp:165-181
[Draw3 绘制线程] 每个 command marker → PumpOneBridgeCommand                Draw3.Host.cpp:1884-1934
        → 先 EnqueueCommandScene(场景前缀) 再 Enqueue ShapeInteraction
   帧内 processCanvasCommands: while(active.empty()) 逐条 TryDequeue        DrawingController.cpp:16113-16148
        → processShapeUiAction → gesture.Preview → replayShapeEdit(完整事务) → publishShapeUi
   帧尾 Present（DComp 或 ULW 读回）
   publishShapeUi → PublishShapeUiSnapshot → refresh 回调 Request(SelectionOverlay|Bar)  Draw3.Product.cpp:27-32, SelectionOverlay.cpp:41
[UI 渲染线程 RenderPipeline] 按帧率节拍                                     RenderPipeline.cpp:741-760
   SelectionOverlay::Render: Configure(新快照)→Advance→EnsureDeviceResources→Present(GetDC+ULW)→SetBounds/Show→CommitPresented
```

### 1.3 输入层：是否每个 move 都排队、是否有界、是否合并

1. **覆盖层 WndProc**（`SelectionOverlay.cpp:118-131`）：每个 `WM_MOUSEMOVE` 都 `TrackMouseEvent` 并 `SubmitSurfaceInput`；`WM_TOUCH` 逐点递归转成鼠标消息（`:102-117`），不做批合并。`e.presentedRevision=target->PresentedGeneration()`（`:127`）。
2. **Mailbox 合并**（`Bar.Interaction.cpp:1975-1985`）：只合并 **队尾相邻** 的同接触 MOVE，且要求 `presentedRevision` 相同。覆盖层每次成功 Present 后 `CommitPresented` 使 generation+1（`Bar.ShapeEditor.cpp:682`），所以合并窗口被覆盖层帧切断；在 worker 及时消费的情况下几乎每个 MOVE 都单独送达 worker。容量 512，溢出时清空并标记 overflow（`Bar.InteractionWorker.h:88-89`）。
3. **presentedRevision 严格检查**（`Bar.ShapeEditor.cpp:690`）：
   ```cpp
   if((expectedPresentation&&expectedPresentation!=frame.presentationGeneration&&!p.gesture&&!p.scrollDragging)||...){CancelPointer();return true;}
   ```
   手势进行中（`p.gesture`）**豁免**此检查，因此它不是拖动慢的原因；但它只对非手势 Down/Move 生效，Down 时若覆盖层刚好又 Present 了一帧会被取消（与本症状无关，记录备查）。
4. **Move → PreviewEdit**（`Bar.ShapeEditor.cpp:709-713`）：每个 MOVE 无条件生成一条 `PreviewEdit`（含 `point=CanvasPoint(presented frame, point)`），无节流、无"已有未处理预览则替换"。
5. **ShapeIntentSender**（`Bar.ShapeEditor.cpp:134-151`）：
   - `pending_` 只保存 **终结意图**（Commit/Cancel/Close/ConstructionComplete/Cancel，`:166-170`）。
   - 非终结（PreviewEdit）直接 `publish_`；若 `pending_` 非空则直接 `return false` 丢弃（`:142`）；若 Bridge 返回 `QueueFull` 也直接丢弃（`:150`）。
   - 即：PreviewEdit **不合并、不替换**，只有"队满后丢弃新的"这一种背压——保留的恰是**最旧**的预览，丢掉的是**最新**的。
6. **Bridge**（`Draw3.Bridge.h:293,321` 容量 256；`Draw3.Bridge.cpp:171`）与 command wake 预约（`Draw3.ContactInput.cpp:32` `kCommandWakeCapacity = 258`；`:399-408`）：纯 FIFO。

### 1.4 绘制线程：PreviewEdit 的消费方式

- **逐条、同帧全部消费**：`processCanvasCommands`（`DrawingController.cpp:16113-16148`）在 `active.empty()` 时 `while` 循环取尽 `TryDequeueCanvasCommand`，`ShapeInteraction` 直接 `processShapeUiAction(...); continue;`。调用点在帧内两处（`:17729`、`:18810`），均在 Present 之前。没有"只取最新 PreviewEdit"的跳过逻辑。
- **每条 Bridge 命令变成两条画布命令**：`PumpOneBridgeCommand` 先 `EnqueueCommandScene(command)`（`Draw3.Host.cpp:1893`，插入 `SetWorkspace/SetPresentationTarget` 前缀），再入队 `ShapeInteraction`（`:1898-1899,1933`）。同场景前缀被 `ShapeCommandChangesScene` 判为非变化（`Draw3.ShapeRuntime.inl:92-103`），但仍逐条走 `processCanvasCommands` 的通用分支（`DrawingController.cpp:16118-16143`）。
- **processShapeUiAction**（`Draw3.ShapeRuntimeActions.inl:255-317`）每条预览：
  1. `shapeScene()`（`:2-27`，读 `ProductVisualStyleSnapshot`、`ClientToScreen`）。
  2. `nextShapeRevision`（`:299`）——**每条预览分配新修订号**。
  3. `gesture.Preview(action.point, revision)` → `Candidate()`（`:307`）。
  4. `replayShapeEdit(candidate, dirty, false)`（`:312`）。
  5. `publishShapeUi()`（`:316`）：对象变了就 `BuildGeometry` + `BuildEditHandles`（`:154-159`），`PublishShapeRuntimeSnapshot` → 新 generation → `PublishShapeUiSnapshot` → refresh 回调。

### 1.5 `replayShapeEdit` 单次成本拆解（`Draw3.ShapeRuntimeActions.inl:42-111`）

| 行 | 操作 | 成本特征 |
|---|---|---|
| 50 | `InkCanvas candidate = *canvas; CanvasPageRuntimeState candidateRuntime = runtime;` | **深拷贝整页**：`InkCanvas::strokes_` 为 `std::vector<InkStroke>`，每个 `InkStroke` 持有 `std::vector<StoredInkPoint> points_` + `optional<ShapeObject>`（`Draw3.InkDocument.cppm` 中 `strokes_`/`points_` 定义）；`CanvasPageRuntimeState`（`DrawingController.cpp:525-545`）含 `CanvasRuntimeHistory history`（每项 `compositionTiles` 向量 + CompositionTree）、`operations`、`beforeStates/afterStates`、`optional<Draw3UInkCanvasSnapshot> boundaryFallback` 等。成本 O(页面总点数 + 历史项数)，页面墨迹多时每次 MB 级分配+拷贝 |
| 54-55 | 为旧预览构造 `InkStroke` 仅为求 footprint | `InkStroke` 构造对 Shape 会 `Validate` + `BuildGeometry`（`Draw3.InkDocument.cpp:78-100`）|
| 59-63 | 构造替换 `InkStroke`（再一次 BuildGeometry）、`BuildStrokeTileFootprint`、`ReplaceStroke`、`UpdateItemGeometry` | 几何重建 ×2 |
| 70 | `historyGpuCache.DiscardHotPreimages(); DiscardCompositionCache(); compositionMaintenance.clear();` | **每次预览清空全部 GPU 合成缓存**（`InkHistoryGpu.cpp:1553-1560` 清 `residentNodes/compositionPages/planner`）和 undo 热像（`:1349-1356`）。`compositionPages` 是 GPU `Texture2DArray`（256×256×最多 64 slice，BGRA8 + R16F 两份，每 slice 各一个 SRV/RTV，`:442-497`），之后 `EnsureCompositionSlot` 按需**重新 CreateTexture2D**（`:514-530`）|
| 71 | `InvalidateTrustedL2Snapshot()` | 失效 L2 快照 |
| 72-75 | `RestoreComposition(tiles = 原始 ∪ 旧预览 ∪ 新)` | `InkHistoryGpu.cpp:1358-1514`：对每个 tile 先 `ClearL2Tile`，再 `DecomposeRange` + `GetOrBuildNode`。因缓存刚被清空，`GetOrBuildNode`（`:920-1010`）递归**重建所有覆盖该 tile 的节点**，叶子走 `RasterItem`（`:733-751`）逐笔重新栅格化，再 `ComposeOperator`/`ApplyOperator` |
| 92 | `ClearOperatorLayer(L0/L1)` | 两张全画布层清除 |
| 103-109 | metrics 剪枝/失效 | metrics 开启时有额外工作 |

**tile 数量估算**：`kCompositionTileSize = 256`（`Draw3.InkHistory.cppm:22`）。一个 800×600 px 图形覆盖约 4×3≈12 tile；旋转/缩放时 旧∪新 足迹并集通常 15–30 tile。每个 tile 的成本 ≈ 覆盖该 tile 的笔迹数 × (栅格化 + 合成)。若背后有大量墨迹（白板常见），单次预览即可达到数十毫秒量级；乘以 R1 的积压条数即是用户看到的"拖尾"。（以上为结构推算，未实测，见 Caveats。）

**`EnsureShapeLayers` 重建（`Draw3.RendererShapes.cpp:101-119`）**：检查 `shapeMaterialLayer_`/`shapeCompositeLayer_` 尺寸是否等于当前 `viewportWidth/Height`，不等则重建两套 `OperatorLayerResources`（每套 BGRA8 + R16F 两张纹理及 RTV/SRV，`Draw3.Renderer.cpp:338-363`）。tile 栅格化时 `DrawStoredStroke` 调 `renderer.SetScreenSize(256,256)`（`Draw3.StrokeGeometry.cpp:1083-1084`），随后 Shape 笔迹 → `DrawShapeObject` → `DrawShapeRenderData` → `EnsureShapeLayers`，尺寸 256≠画布 → 重建为 256×256；任何画布尺寸的图形绘制（活动图形 `DrawActiveShapePrimitives`、构造预览 `renderPendingShapeConstruction`、`ReplayRangeDirect` 回退路径 `InkHistoryGpu.cpp:753-780` 用画布宽高）又会重建为全画布尺寸。稳态拖动中若只走 composition 路径，层会停在 256 不再重建；**震荡发生在**：(a) 绘制图形→首次预览，(b) 预览→下一次画布尺寸图形绘制，(c) 同一次 RestoreComposition 中某 tile 从 composition 回退到 direct replay（`InkHistoryGpu.cpp:1491-1504`）时 256↔画布交替。全画布尺寸重建一次（例如 2560×1600：BGRA8 16 MB + R16F 8 MB，×2 套）代价很高。

**RenderData 缓存挤出（`Draw3.RendererShapes.cpp:154-170`）**：缓存键 `id+revision+displayScale`，容量 128，满时 `erase(begin())`。每条预览新修订 → 必然 miss 并插入一条；拖动产生 >128 条预览后，页面上**其他图形**的缓存条目全部被挤出，tile 重建时这些图形要重新 `BuildRenderData`。另外 `ClearShapeRenderCache()` 在失败回滚时调用（`ShapeRuntimeActions.inl:83`）。

**主画布 Present**：`TransparentPresentation.cpp:536-541` ULW 模式下 `CopySubresourceRegion` 脏区到 staging 后 `Map(D3D11_MAP_READ)`，同步等 GPU 完成 → 本帧所有预览的 GPU 工作计入 CPU 帧时间；脏区为所有预览 tile 的并集（`UnionRectInPlace(dirty, restored.dirty)`，`ShapeRuntimeActions.inl:77`）。DComp 模式走 `Present1`（`:1123`）。

### 1.6 覆盖层重绘（SelectionOverlay）

- **触发**：每次 Draw3 发布新快照 → `Refresh` → `Request(SelectionOverlay)` **且** `Request(Bar)`（`SelectionOverlay.cpp:41`）；每个输入事件处理后也 `Request()`（`:79`）。RenderPipeline 单线程按帧率节拍批处理请求（`RenderPipeline.cpp:741-760`），因此覆盖层本身是"每 UI 帧至多一次"，不是每事件一次。
- **每帧工作**（`SelectionOverlay.cpp:42-70`）：
  1. `ProductShapeUiSnapshot()` + `Display::GetSnapshot()` + `GetWindowRect(Bar)`。
  2. `Configure`（`Bar.ShapeEditor.cpp:329-356`）：`same=snapshot->identity==next->identity`，预览每次 generation/objectRevision 都变 → 每帧 `BuildGeometry` **两次**（世界坐标一次、去变换局部一次，`:336-339`）；还会 `GetStateModeSnapshot` + 三次 `GetBarThicknessPresetMetrics`（`:354-355`）。
  3. `Advance`（`:357-543`）：完整工具条布局、所有按钮动画、遍历 handles、`ResolveBarSurfaceFrameLightingSnapshot`。
  4. `EnsureDeviceResources(bounds 宽高)`（`:658-662`）→ `BarUIRendering::EnsureDeviceResources`（`Bar.Rendering.cpp:312-322`）只要宽高变就 `RecreateDeviceResources`：**新建 D2D DeviceContext + GDI 兼容目标位图**并 `DiscardDeviceResources()`（`:324-352`）。缩放/旋转时 `bounds`（图形包围盒 ∪ 工具条 ∪ 控制点外扩 26px，`Bar.ShapeEditor.cpp:411-425`）几乎每帧变化 → 几乎每帧重建。
  5. `Present`（`:665-681`）：`BeginFrameDraw` → `Draw`（含整块图形包围盒 alpha=1/255 命中填充 `:583-589`、外框 4 条线、所有控制点、工具条与面板）→ `GetDC(D2D1_DC_INITIALIZE_MODE_COPY)`（GPU→CPU 读回整张目标）→ `UpdateLayeredWindowIndirect` **整窗无 prcDirty**（`:674-676`）。
  6. `service.SetBounds` + `Show`（`SelectionOverlay.cpp:68`）——bounds 变化时移动/调整窗口。
- **控制点位置的来源**：`Advance` 中 `PlaceShape(... snapshot->canvasOriginX+h.position.x ...)`（`Bar.ShapeEditor.cpp:516-539`），外框由 `localBounds` 经 `object.transform` 变换（`:594-604`）。全部来自 Draw3 回发的快照，**覆盖层没有用指针位置做本地预测**。

### 1.7 对照 1：普通笔迹为何低延迟（RTS 生产者路径）

- 笔/触点由 RealtimeStylus 回调直接写入 `ContactInputCoordinator` 的无锁队列（`Draw3.ContactInput.cpp:410-430` 中同一 `queue.try_enqueue(ingressProducerToken, ...)`，及 `SignalWake()` `:456`），绘制线程在帧内批量取出所有点。
- 绘制只做**增量**：活动笔迹画到 L0/L1 运算层（`DrawingController.cpp:18783-18802` `DrawL0LiveComposite`、`DrawActiveShapePrimitives`），不拷贝文档、不清合成缓存、不重建 tile；落笔结束才一次性提交 CPU 文档。
- 有活动接触时帧按 `target_fps` 节拍（`DrawingController.cpp:19115-19123` `WaitForFrameDeadline(1000/target_fps - workMs)`），每帧消费"当前全部点"并 Present 一次——天然的 latest-wins。
- 注释也说明设计意图：`ShapeRuntimeActions.inl:41` "仅复制低频图形编辑候选，普通画笔仍走原追加热路径"——即预览路径是按"低频"设计的，而拖动是高频。

### 1.8 对照 2：主栏 / PageControl 拖动如何合并

- **Mailbox 层**：同上，相邻 MOVE 合并（`Bar.Interaction.cpp:1975-1985`）。
- **视觉反馈层**：`QueueInteractionVisualFeedback`（`Bar.Interaction.cpp:1804-1834`）按 `(identity, slot)` **替换**同一条反馈，`RenderPipeline` 中至多保留一个 drain 请求（注释 `:1817` "高频 Move 只更新同一条反馈"）——这是现成的 latest-wins 模式。
- **PageControl 拖动**（`PageControl.cpp:1880-2000`）：每次 Move 计算候选布局；若是纯平移（`directTargets[i].pureTranslation`）直接 `SetBoundsDirect` 移动 HWND，不重绘内容（`:1970-1972`）；发布带 `replacedPending/replacedRevision`（`:1983-1990` 日志字段）表明新位置**替换**未完成的旧位置；非纯平移才回退 `RequestDragPair`（`:1916-1923`）。日志 `TraceDrag` 前缀 `[PageControlDrag]`（`:193-206`）。

### 1.9 理想架构（仅记录方向，供 design 阶段参考）

1. **Latest-wins 合并**：PreviewEdit 在 UI 侧用单槽"最新预览"代替 FIFO（类似 `QueueInteractionVisualFeedback` 的替换语义），或 Draw3 侧在 `processCanvasCommands` 中对同 gesture 的连续 PreviewEdit 只执行最后一条；Commit/Cancel 等终结意图仍保序。目标：每个 Draw3 帧至多一次预览。
2. **轻量预览层**：拖动期间不改文档、不拷贝 `InkCanvas`/runtime、不清合成缓存。做法：开始编辑时一次性把"除被编辑对象外"的 tile 内容恢复好（或用 `excludedItem` 的 `RestoreComposition`，`InkHistoryGpu.cpp:1386-1396` 已支持排除项），之后每帧只在临时层（类似 L0 / 构造预览 `renderPendingShapeConstruction` 的做法，`ShapeRuntimeActions.inl:329-344`）绘制候选对象；Commit 时再走一次现有文档事务。
3. **覆盖层本地预测**：覆盖层在 Pointer 时用本地 `EditSession`（`draw3::shapes`，纯 CPU 函数）按指针位置立即计算候选的 handles/外框并绘制，Draw3 快照仅用于校正；覆盖层 bounds 采用拖动期间不缩小的稳定外框（或手势开始时按工作区预分配），避免每帧 `RecreateDeviceResources`；ULW 使用 `prcDirty`。
4. **缓存键**：预览修订不进入 `shapeRenderCache_`，或预览使用固定临时 revision，避免挤出其他图形。

### 1.10 覆盖这些路径的测试

| 入口 | 函数 | 覆盖内容 | 不覆盖 |
|---|---|---|---|
| `--ui3-shape-editor-test`（`Inkeys/IdtMain.cpp:1793-1798`）| `Bar::RunShapeEditorOffscreenTest`（`Bar.ShapeEditor.cpp:748-970`）| `ShapeIntentSender` QueueFull 时终结意图保留/合并（`:761-774`）；每个 preset 的 handle 拖动"Begin+1 Preview+Commit"（`:881-890`，`actions.size()==3`）；ULW 提交、glyph 像素、色板、滚动、设备重建 | 无时延/吞吐断言；不经过真实 Bridge/Draw3；未测连续多 Move 的合并或丢弃策略 |
| `--draw3-shape-renderer-test`（`IdtMain.cpp:1766-1771`）| `RunShapeRendererTests`，含 `Draw3.ShapeRendererEditTests.inl`（`ShapeRendererTests.cpp:465`）| 编辑同层替换的像素正确性：预览/取消/提交/拒绝回滚，`replay` lambda 同样先 `DiscardHotPreimages+DiscardCompositionCache+ClearShapeRenderCache` 再 `RestoreComposition`（`ShapeRendererEditTests.inl:71-74`）| 无性能断言；128×128 小画布 |
| `--draw3-shape-core-test`（`IdtMain.cpp:1772-1777`）| `RunShapeCoreTests + RunShapeDocumentProductionTest`，含 `Draw3.ShapeRuntimeTests.inl`（`DrawingController.cpp:7840`）| 身份校验（gesture 冻结 identity、Close 会话语义）、`EditSession` Begin/Preview/Commit、journal undo/redo | 无队列/时延测试 |

### 1.11 现有可用于测量延迟的度量/日志

- **Draw3 RuntimeMetrics**：`RuntimeMetricsFrameReason::Command` 等标志（`Draw3.RuntimeMetrics.cppm:36-45`），每帧 `metricsState_->Finish(canvasFrameElapsedMilliseconds)`（`DrawingController.cpp:19101`），以及 `BeginRasterAttempt/RasterReturned`（`ShapeRuntimeActions.inl:74-76`）。仅当 `HostStartOptions::enableRuntimeMetrics`（`Draw3.Host.h:275`）为真时启用；产品代码中只有 `Draw3.HiddenWindowTest.cpp:3908` 设置，产品默认关闭。
- **活动帧计时** `LogFrameTiming` / `metrics_->RecordActiveFrame`（`DrawingController.cpp:19116-19137`）：只在有物理接触的帧记录，**编辑拖动（active.empty()）不会进入该分支**。
- **ULW 呈现诊断**：`StartupEnvironmentDiagnosticsEnabled()` 为真时记录 copy/rows/ULW 耗时（`TransparentPresentation.cpp:534-591` `LogPixelDiagnostic`），开关 `SetStartupEnvironmentDiagnosticsEnabled`（`Draw3.Diagnostics.cpp:977-980`）。
- **UI 侧**：`UiDiag::Log("surface", ..., "queue_reject", ...)`（`Bar.Interaction.cpp:1973`）记录输入被拒；RenderPipeline 有 `diagnosticsSink`/`EmitDiagnostics`（`RenderPipeline.cpp:712-731`）；PageControl 有 `[PageControlDrag]` TraceDrag 作为格式参考。
- **缺口**：没有任何"PreviewEdit 入队时间→Draw3 处理时间→快照发布→覆盖层 Present"的打点；`ShapeUiAction` 无时间戳字段（`Draw3.ShapeInteraction.h:35-43`）；`ShapeIntentSender` 丢弃预览时不计数。

### Related Specs

- `.trellis/spec/` 下未检索到专门约束图形编辑预览性能的条目（未逐一通读，仅按目录结构判断；需要时可在 design 阶段用 `trellis-before-dev` 拉取 native/native-desktop 规范）。

## Caveats / Not Found

- 所有成本均为**结构性推算**，未在真机上测量；单次预览的毫秒数强依赖页面墨迹量、图形尺寸（tile 数）、GPU 与呈现模式（ULW 读回 vs DComp）。
- 未逐行验证 `EditSession::Preview`/`BuildEditHandles`（`inkStrokeModelerTest/draw3/shape_interaction.h` 一侧）的成本，推测为轻量 CPU 计算。
- `EnsureShapeLayers` 的 256↔画布尺寸震荡是否在稳态拖动中每帧发生，取决于是否触发 direct replay 回退或同帧存在画布尺寸图形绘制；静态阅读只能确认"可能"，需实测（可在 `EnsureShapeLayers` 重建分支加计数）。
- 同场景的 `SetWorkspace/SetPresentationTarget` 前缀命令在 `processCanvasCommands` 中的具体成本未细查（`DrawingController.cpp:16614` 之后的分支），推测较小。
- Bar 主栏因 `Request(Client::Bar)` 每快照重绘的实际成本取决于其脏区跟踪（`Bar.RenderLoop.cpp:7268-7284` 读取快照），未量化。
- 覆盖层 `CommitPresented` 使 `presentedRevision` 递增，从而切断 mailbox 合并——这一点是读代码得出的推论，未实测合并比例。
