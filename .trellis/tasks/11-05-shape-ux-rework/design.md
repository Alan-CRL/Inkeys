# 图形交互与样式整改 — 技术设计

各子任务的需求见对应的 `prd.md`；本文只写跨子任务的技术边界、数据流和取舍。

## 1. 子任务边界与共享文件

| 子任务 | 主要文件 | 与其他子任务共享 |
|---|---|---|
| edit-performance | `Draw3.Bridge.*`、`Draw3.ShapeRuntimeActions.inl`、`Draw3.DrawingController.cpp`（预览路径）、`Draw3.RendererShapes.cpp`、`SelectionOverlay.cpp`、`Bar.ShapeEditor.cpp`（意图发送、跟手） | ShapeEditor 同时被 visual 和 interaction 修改 |
| edit-interaction | `shape_core.*`、`shape_presets.inl`、`shape_interaction.*`、`shape_uink_*.inl`、`uink_draw3_*`、`Draw3.InkDocument/InkHistory`（辅助标志位） | 浮条开关的 UI 由 visual 负责 |
| edit-visual | `Bar.ShapeEditor.cpp`（绘制、浮条、颜色层）、新增的 Thumb 绘制 helper（`Bar.Rendering.*`）、`Bar.RenderLoop.cpp`（主栏 Thumb 改用 helper）、`SelectionOverlay.cpp`（按键） | — |
| toolbar-category | `Bar.GeometryAttribute.cpp`、`Bar.Main.cppm`（Geometry* 索引）、`IdtState.*`（辅助开关的工具状态） | 辅助开关的状态字段与 interaction 共用 |
| construction-prompt | `Bar.ConstructionPrompt.cpp`、`Bar.Interaction.cpp`（自动收起）、`shape_interaction.*`（Forward）、`Draw3.ShapeInteraction.h` | Forward 与 interaction 的 ConstructionSession 共用 |

## 2. 编辑预览数据流（edit-performance）

```
指针 move ──► ShapeEditor（UI worker）
              ├─ 本地：用共享核心 EditShape(快照对象, 手势) 计算候选 → 立即重绘外框和控制点（R3）
              └─ 写入「最新预览槽」（每个 session 1 个，latest-wins，带 gesture 序号）
绘制线程每帧 ─► 先处理有序命令队列（Begin / Commit / Cancel / Close / 属性），再取一次最新预览槽
              └─ 轻量预览：手势 Begin 时建立「排除原项」的背景合成（一次）
                           之后每帧只在临时层重画候选对象 → 上屏
Commit ──────► 走现有的权威路径：候选 CPU / journal / tile 重放，失败则回滚
```

- **预览槽**：放在 Bridge 中，与命令 FIFO 并列。用原子交换或加锁覆盖写入。槽里带 session/gesture 身份，绘制线程取出后照常校验身份。Commit 中携带最终手势参数，即使最后一个预览被覆盖，提交也不受影响。终结意图仍走 FIFO 和现有的有界重试。
- **轻量预览**：复用现有"排除某项并在原层位回放"的 restore 能力。手势开始时把背景冻结到一张临时纹理，拖动中背景不变；每帧把背景加候选对象合成到上屏目标。被编辑对象上层有其他笔迹压着时，需要为上层另做一张纹理（"上层"纹理），保证层序正确。不再复制 `InkCanvas`，也不清空缓存。
- **缓存**：预览阶段的 revision 使用临时命名空间，不写入 128 项长期缓存。Commit 时才分配正式 revision。
- **浮层**：拖动期间窗口边界按块扩大（例如按 256DIP 取整），只增不减，手势结束后再收缩；内容不变时不调用 Present。
- **计量**：在预览槽中记录入队时间戳，绘制线程上屏时计算延迟，同时累计被覆盖的预览数，通过 RuntimeMetrics 或诊断日志输出。

## 3. 控制点模型与辅助显示（edit-interaction）

- `BuildEditHandles` 改为按 preset 的表驱动描述生成控制点：控制点种类、位置推导函数、编辑函数、是否显示旋转钮、外框是否有角点。外框统一由共享核心给出 `frameRect`（本地坐标，基于含线宽的包围盒外扩 8DIP 的间隙，按 displayScale 换算），UI 只负责绘制，保证外框线和外框角点在同一位置。
- 圆：Drag 模式下第一次拖动半径点时，把 construction 转为 CenterRadius（保持圆心和半径不变）；半径点的位置取 construction.end 的方向。Drag 模式默认放在 45°。
- 椭圆焦点：内部规范化为 a≥b（方向写入 transform 旋转），焦点控制点 id 为 106/107；拖动遵循 PRD 的 A 方案。
- 弧角编辑：EditSession 保存手势开始以来的累计角，与构造时 ObserveRaw 的累计方式相同。
- **辅助显示的模型**：`ShapeStyle.auxiliary`（位域，包括 Center、Foci、Diagonals）。
  - 渲染：`BuildRenderData` 派生附加的 segment 和 dot，沿用同材质的覆盖并集规则。辅助点用填充圆，对角线使用虚线 dash。
  - 命中：辅助元素不参与本体命中。
- **持久化**：`inkeysShape` v4 增加字段 `aux`；v1/v2/v3 读入时 aux 为 0，圆和椭圆按旧行为不显示辅助元素，不补默认值，以免旧文件外观改变。UInk10 标准降级时，把辅助元素作为额外的标准部件写出，与复合部件共享 undoId 和 reference；读回时如果识别到 v4 根，就按 aux 重建，把这些部件视为派生内容，不当作独立对象。
- 浮条和主栏的开关通过 `ShapeUiAction::SetStyle`（对象）和工具状态（下一笔）两条路径分别处理。

## 4. 浮条与颜色层级（edit-visual）

- 浮条变成独立的面板对象：`attributeList`（SingleRow）加主栏 Divider。显隐使用 0.4s timeline 和 CanJoin，隐藏成员锚定到相邻成员。整体用 `BarAttributePanelMotion` 回弹。新会话时 `ResetLayout()`，所有成员 SetDirect。
- 颜色层级在同一个 SelectionOverlay 窗口中按 z 序绘制：浮条 < 色板层 < 取色器层。每层有各自的 motion 和 hit 区，输入按 z 序从顶层开始命中；点击空白处或按 Esc 时从顶层开始逐层关闭。取色器复用 `ColorPalette`、`GetBarColorPickerColor`、`ProjectBarColorPickerColor`。标记点和预览部分新写，色板交互不依赖主栏的全局状态。
- Thumb helper：`DrawBarSliderThumb(renderer, dc, center, outerDiameter, centerDiameter, accentOpacity, lightWeight, opacity, icon?)`。主栏粗细滑块迁移到这个 helper，要求像素不变（由回归测试保护）。
- 透明度：浮条使用 residentSurface 加 `GetThemeResidentSurfaceOpacity`，内容使用 `GetThemeResidentContentOpacity`；两个颜色层固定使用 `BarDrawAttributeSurfaceOpacity`（0.95）。`SetResidentOpacity` 和主题切换会向 RenderPipeline 请求 `Client::SelectionOverlay` 重绘。

## 5. 主栏面板分类（toolbar-category）

- 目录增加 `category` 字段（Regular/Solid/CoordinateFunction），移除圆角矩形条目。`categoryLayout_` 改为在分类切换时重新解析可见子集，`slot[i]` 改用按目录下标建立的映射。顶部分段控件使用 3 个 OneOne 宽度的 segment 按钮，写法沿用橡皮的 Selected 加 frameColorOverride。
- 正多边形图标轮播去掉 SetTimer，改为由 Advance 按 deadline 推进，到期时请求一帧。
- 辅助开关进入参数行：`[构造方式按钮…] | [辅助开关]`，开关是图标型的 OneOne 按钮，处于 Selected 状态时表示开启。

## 6. 绘制提示（construction-prompt）

- `ConstructionStatus` 增加 `totalSteps`（0 表示不定步数）和 `canForward`；`ConstructionSession` 增加 redo 栈。新增 `ShapeUiActionKind::ConstructionForward`。全局 Redo 在构造过程中执行 Forward。
- 提示面板改为：环形进度（DrawProgressRing 加公开封装）、文字（BarUiWordClass，13 Bold）、按钮 list（OneOne，最多 4 个）。表面材质与底栏提示保持一致；动画复用 bottomDock 的进度曲线和三阶段换字状态机（抽成可复用的 helper）。
- 自动收起：在 `ProcessAcceptedStrokeNotices` 中，如果 `ShapeUi` 快照处于 construction active 状态，就跳过 Drawing 策略。

## 7. 兼容与回滚

- 文件：v4 只是新增字段。旧版本读取 v4 文件时，走标准几何降级，能看到辅助元素；新版本能读 v1–v3。圆角矩形的枚举保留。
- 每个子任务各自独立提交。edit-performance 可以单独回滚（预览槽和轻量预览有开关，可退回现有路径）。主栏 Thumb helper 的迁移出问题时，可单独回退主栏调用点。
- spec 更新：`shapes.md`（P3 合同的控制点、外框、辅助显示、v4、Forward）、`rendering-and-ui.md`（分类面板、浮条、颜色层级、提示）、`bar-theme-material.md`（编辑器的常驻透明度）。
