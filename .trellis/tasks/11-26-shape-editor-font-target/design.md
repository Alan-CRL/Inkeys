# 设计：形状编辑浮条字号取整目标取展开态

## 1. 现状链路（HEAD dc13ae10）

- `ShapeEditorSurface::Impl` 自带 `UIRendering renderer{...}`（`Bar.ShapeEditor.cpp:605`）。全文件没有 `BeginFontFrame`、`SetFontTargetScale`、`SetFontTargetZoom`、`FontTargetScope`。
- 宿主 `PresentShapeEditorFrame` 在调用 `Draw` 前把 DC 变换设为单位矩阵（`:2573`）；`Draw` 只调用 `renderer.SetFrameZoom(scale)`（`:1886`）。
- 各层绘制前设 DC 变换为当前姿态 `Matrix3x2F(pose.scale, …)`（`:2045`），pose 来自布局阶段的 `poses[0..3]`（`:1440-1471`）：
  - 0 工具栏：`FitEditorPose(Compose({toolbarFit,x,y}, barFrame.pose), shell, safeWork)`
  - 1 参数、2 色板：`layerPose` = `FitEditorPose(Compose(base{fit}, frame.pose), panel, safeWork)`
  - 3 取色器：`ResolveEditorPickerPose(colorPicker.Geometry(), …)`
- `ResolveAnimatedFontSize`（`UI3.Renderer.cpp:202`）在此情形下：
  - `fontFrameReady == false` → 用立即上下文 `{0,1,false,true}`，修正系数每次 `SetDirect`，没有动画；
  - `fontTargetTransformReady == false` → 目标像素倍率取**当前 DC**，即当前 `pose.scale`；`fontTargetZoom == 0` → 取 `frameZoom`。
- 结果：物理字号 = `floor(nominal × zoom × 当前 pose.scale)`，逐帧阶梯取整（spec 已废弃的初版行为），开合中比等比小 0–1 px。

## 2. 决策

沿用主栏契约：**取整目标取各层展开态姿态（几何进度 = 1，含 fit）**，修正系数在一次开合中恒定。

### 改动点（只改 `Bar.ShapeEditor.cpp`）

1. 布局阶段与 `poses` 同处计算 `std::array<double,4> fontTargetScales`：
   - 0：`FitEditorPose(ComposeEraserPose({toolbarFit, toolbarX.val, toolbarY.val}, barFrame.expandedPose), shell, safeWork).scale`
   - 1/2：`layerPose` 同时返回 `FitEditorPose(ComposeEraserPose(base, frame.expandedPose), panel, safeWork).scale`
   - 3：`ResolveEditorPickerPose(1.0, …).scale`（取色器可见时）
   - 存入候选帧（与 `next.poses` 同处，新增字段）。
2. `Draw` 开头：`renderer.BeginFontFrame(steady_clock::now(), {0, 1, false, true}, scale)`——保持现有“立即落定、无修正动画”的语义不变，只是每帧重置目标变换状态并把目标缩放设为 `scale`。
3. 每层绘制内容前：`renderer.SetFontTargetScale(candidate.fontTargetScales[panel])`。DC 原始变换为单位矩阵，所以目标 Y 向量 `{0, 展开态缩放}` 与展开时的实际 DC 变换一致。

### 审阅后修订（v2）

Codex 审阅指出 v1 的取色器展开倍率用的是当前帧（正在收缩）的工具栏/色板矩形，窄工作区走 `bestScale` 回退时会随整体收起逐帧变化；工具栏 fit 也取 `max(目标, 当前)` 宽高。v2 改为**展开态链**：

- 工具栏：`toolbarRestFit = min(1, available/targetWidth, available/targetHeight)`，`FitEditorPose(Compose({restFit,x,y}, barFrame.expandedPose), {0,0,targetWidth,targetHeight}, safeWork)`，冻结时同样平移到冻结位置。
- 参数/色板：`layerPose` 拆出放置基准 `layerBase`（数值不变），展开态以工具栏展开态矩形为父级，再组合各自 `expandedPose`。
- 取色器：`ResolveEditorPickerPose(1.0, …)` 用工具栏与色板的展开态矩形求空闲区域。
- 当前姿态计算保持原样。位置变化（拖动、停靠残差）会改变静止布局本身，目标随之更新属预期；只有开合几何进度不进入目标。

### 为什么保留立即模式

- 目标在一次开合内恒定，系数不需要动画；只有窄屏 fit 或 DPI 变化时目标才变，立即落定与现状一致。
- 不引入修正动画，就不需要给浮条补“修正尾段续帧 / 脏区”调度，改动最小。

### 不选

- 给浮条接入完整的字号动画帧（`BeginFontFrame` 传真实 dt + `FontAnimationsActive` 续帧）：本问题不需要，属额外机制。

## 3. 用户展开倍率约定

后续若给形状编辑浮条也加用户展开倍率，同样乘在展开态姿态上；本任务的落点是 `fontTargetScales` 的计算处（`barFrame.expandedPose` / `frame.expandedPose` / `ResolveEditorPickerPose(1.0, …)`）。

## 4. 影响与风险

- 静止展开：T 与原先当前姿态一致，画面不变（golden 应不变）。
- 开合中间帧：字号由阶梯变为连续，略大（回到等比）。
- 工具栏宽度动画导致 `toolbarFit` 变化时（仅窄屏），目标随之立即更新，与现状同类。
- 其余 `Draw` 内容（输入覆盖、编辑点、滑块）在面板之前绘制，`BeginFontFrame` 后目标变换未就绪，仍按当前 DC，与现状一致。

## 5. 测试

- `--ui3-shape-editor-test`：打开浮条，开合动画逐帧离屏绘制，断言一个带文字的工具栏按钮 `name.fontCorrection.Target()` 全程不变且等于 `floor(T_expanded)/T_expanded`；收起同样。
- 变异：去掉 `SetFontTargetScale` 调用后该用例必须失败。
- 门禁：完整构建、shape-editor / eraser-offscreen / geometry-attribute / host-offscreen / attribute-dirty / headless、golden 对比。
