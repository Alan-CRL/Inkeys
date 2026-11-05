# 编辑拖动性能

## Goal

图形绘制完成后，拖动控制点（缩放点、端点、顶点等）或旋转按钮时，图形和控制点要跟手。目前的表现是：事件堆积，图形缓慢地追着指针移动。

## 根因（详见 ../11-05-shape-ux-rework/research/edit-performance.md）

1. **预览意图不合并，按 FIFO 堆积。**
   - 每个指针移动都会发出一个 `PreviewEdit`（`Bar.ShapeEditor.cpp:711`）。
   - 这些意图进入容量 256 的 FIFO（`Draw3.Bridge.cpp:165-181`）；队满时丢掉的是最新的一个（`Bar.ShapeEditor.cpp:142,150`）。
   - 绘制线程一帧内把队列里所有预览逐个执行完（`Draw3.DrawingController.cpp:16113-16148`）。
2. **每次预览都等同一次完整文档编辑。**
   - 整页深拷贝 `InkCanvas` 和 `CanvasPageRuntimeState`（`Draw3.ShapeRuntimeActions.inl:50`）。
   - 丢弃合成缓存和撤销缓存（`:70`）。
   - 新旧区域内的 256px tile 全部重新光栅化。
   - 另外 `EnsureShapeLayers` 在 tile 尺寸和全屏尺寸之间切换时会重建图层（`Draw3.RendererShapes.cpp:101-119`）。
3. **浮层控制点只跟随绘制线程回传的快照**，绘制线程的延迟会直接表现为"控制点跟不上"。
4. **浮层每帧开销偏高。**
   - 重建两次几何（`Bar.ShapeEditor.cpp:336-339`）。
   - 窗口尺寸变化导致 D2D target 重建（`Bar.Rendering.cpp:312-352`）。
   - 每帧整窗回读并调用 ULW，没有脏区（`SelectionOverlay.cpp:68-81`）。
5. **几个放大因素。**
   - 预览 revision 会挤掉 128 项 renderer 缓存。
   - 每次快照发布都会触发主栏重绘。
   - 每条命令前都会再插一条场景前导。
   - 按下事件按呈现代次严格比对，导致按下被吞（`Bar.ShapeEditor.cpp:690`）。

## Requirements

- R1 预览合并（latest-wins）：同一编辑会话、同一手势在队列中最多保留一个待处理的 Preview，绘制线程每帧最多执行一次，且总是执行最新的那个。Begin、Commit、Cancel 等终结意图仍按顺序处理，不能丢。
- R2 轻量预览：拖动期间不复制整页，也不清空合成缓存和撤销缓存。
  - 手势开始时建立一次"排除被编辑项"的背景。
  - 之后每帧只在临时层重画被编辑对象。
  - 只有 Commit 才进入文档、journal 和 tile 的权威重建。
  - 现有的失败回滚语义保持不变。
- R3 控制点本地跟手：浮层在拖动时根据指针和本地几何先行更新控制点和外框的位置，绘制线程回传的快照只用来校正。
- R4 浮层降开销：
  - 拖动期间窗口尺寸保持稳定，或按块扩容，避免每帧重建 target。
  - 内容没变时不 Present。
  - 每帧只构建一次几何。
- R5 放大因素收敛：
  - 预览 revision 不污染 renderer 长期缓存。
  - 快照发布不无条件触发主栏重绘。
  - Down 命中按"成功呈现过的帧"判断，不要求呈现代次严格相等。
- R6 可观测：增加从意图入队到上屏的延迟计数，以及被合并的预览数，用于实机验证。

## Acceptance Criteria

- [ ] 4K、高笔迹量页面（≥2000 条普通笔迹）上，拖动控制点或旋转按钮时，每帧最多执行 1 次预览，队列中的预览不超过 1 个（由测试或计数器验证）。
- [ ] 拖动期间不出现整页 `InkCanvas` 复制，也不清空合成缓存（由测试钩子或计数器验证）。
- [ ] Commit、Cancel、Undo、Redo 的结果像素与当前实现一致：`--draw3-shape-renderer-test` 全部通过，并新增覆盖"大量连续 Preview 后再 Commit"的用例。
- [ ] `--ui3-shape-editor-test` 新增连续高频 move 的合并和顺序用例，覆盖终结意图不丢失。
- [ ] 实机（由用户验收）：笔或触屏拖动时控制点不明显滞后，松手后没有继续"追赶"的动画。

## Out of Scope

- 新的控制点集合与样式由 edit-interaction / edit-visual 子任务负责；本任务只保证数据流和渲染路径的性能，需同时适配新旧控制点。

## Dependencies

- 与 edit-interaction / edit-visual 共用 `Bar.ShapeEditor.cpp` 和 `shape_interaction.inl`。建议先落地本任务的数据流（R1/R2/R5），再进行视觉和交互改造，以减少冲突。
