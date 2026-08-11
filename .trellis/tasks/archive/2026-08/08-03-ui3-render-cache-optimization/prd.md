# UI3 渲染缓存与 SVG 旋转优化

## Goal

降低 UI3 Bar 在稳定帧、尺寸/按压/强调动画和未来旋转动画中的 CPU 与 D2D 资源重建开销，同时保持现有视觉、命中区域、脏区和 PointLight 行为。SVG 控件新增与 PNG 一致的旋转参数；旋转围绕控件目标矩形中心执行，不改变布局宽高。

## Background And Confirmed Facts

- `BarUIRendering::Svg()` 当前以 `svg.w.val/svg.h.val` 和颜色作为缓存条件；尺寸动画会导致 `CacheBitmap()` 在中间帧重新解析 lunasvg 文档并光栅化（`Inkeys/Inkeys/UI/Bar/Bar.Main.cpp:2628`）。该函数已有待优化 TODO。
- `BarUiSVGClass::CacheBitmap()` 在每次缓存失效时执行 SVG 文本颜色替换、`lunasvg::Document::loadFromData()`、`renderToBitmap()` 和 D2D 位图上传（`Inkeys/Inkeys/UI/Bar/Bar.UI.cpp:325`）。内容中点替换会主动失效一次缓存。
- `BarUIRendering::Png()` 已通过保存/恢复 D2D transform，在目标矩形中心旋转 PNG；它不改变 `w/h` 和布局计算（`Inkeys/Inkeys/UI/Bar/Bar.Main.cpp:2683`）。
- `BarUIRendering::Superellipse()` 当前每次绘制都按宽高、`n` 采样点，生成 Catmull-Rom Bezier，并新建/填充 `ID2D1PathGeometry`（`Inkeys/Inkeys/UI/Bar/Bar.Main.cpp:2462`）。位置变化不应使形状路径重建。
- 设备资源重建通过 `BarUIRendering::DiscardDeviceResources()` 清理 SVG/PNG 位图和渲染缓存（`Inkeys/Inkeys/UI/Bar/Bar.Main.cpp:932`）。
- 传统 Draw2 绘制、历史、PPT 墨迹和 Draw3 接口不在本任务范围内。

## Requirements

### R1 SVG raster cache

- 将 SVG 内容/语义颜色解析与 D2D 位图缓存独立于当前绘制目标矩形的动画尺寸。
- 稳定帧在 SVG 内容、语义颜色、设备代际和有效缓存尺寸不变时，只调用 `DrawBitmap`，不得重新解析 SVG 或上传位图。
- 尺寸、内容倍率、按压缩放、强调缩放等动画过程中复用现有位图；只有当前显示尺寸相对缓存尺寸超过明确的质量阈值时才允许重新光栅化，且同一动画阶段不能逐帧重建。
- 动画结束后的首个稳定帧，如最终尺寸需要更高质量位图，只重建一次并进入待命状态。
- SVG 内容或颜色发生改变、中点替换提交新内容、缓存为空、设备资源丢失/代际改变时立即失效并在可绘制时重建一次。
- 旋转参数不得参与 SVG 位图缓存键；纯旋转动画不解析 SVG、不改变缓存尺寸。

### R2 SVG rotation

- 为 `BarUiSVGClass` 增加 PNG 风格的角度值，默认 `0` 度并支持动画驱动。
- 绕最终目标矩形中心应用 D2D 旋转并恢复原 transform。
- 旋转不修改 `x/y/w/h`、命中测试或非旋转布局尺寸。脏区/可见区域若当前契约要求覆盖实际旋转包围盒，应与 PNG 采用一致的几何计算；否则至少不得缩小原布局脏区。

### R3 Superellipse path cache

- 缓存局部坐标系中的超椭圆路径；缓存键至少包含有效宽度、有效高度、`n`、分段策略和 DPI/缩放影响形状的量，不包含屏幕位置。
- 仅位置变化时复用路径，通过平移/变换用于绘制和 PointLight/遮罩计算。
- 宽度、高度、`n`、分段策略、设备代际变化时失效并重建一次。
- 保持现有填充、边框、裁剪、脏区和第三光源结果；缓存失败时保留当前直接生成路径的可用降级路径。

### R4 Lifecycle and compatibility

- 所有新增 D2D 资源纳入 `DiscardDeviceResources()`，不得跨 device generation 使用旧 COM 资源。
- 不增加公开 API、配置字段或持久化格式；仅扩展 UI3 Bar 内部控件状态/缓存字段。
- 缓存实现需线程/渲染循环现有所有权模型兼容，不引入跨线程 D2D 调用。

## Acceptance Criteria

- [x] 连续稳定帧只绘制 SVG 缓存位图；静态检查和 instrumentation/日志可证明无重复 lunasvg parse/rasterize。
- [x] SVG 尺寸、按压、强调和内容跳变动画中不会每帧解析；超过质量阈值时最多按策略重建，动画结束稳定帧至多补建一次。
- [x] 仅旋转 SVG 时缓存命中，目标布局宽高和中心不变；旋转 0/90/180/任意角度均能恢复原 D2D transform。
- [x] SVG 内容/颜色变化和中点替换会重建新位图；设备重建后不使用旧位图且可恢复渲染。
- [x] 超椭圆仅移动位置时路径缓存命中；宽高或 `n` 改变时重建，填充/边框/裁剪/PointLight 视觉结果不回退。
- [x] `git diff --check` 通过；`InkeysRepo.sln` `Debug | ARM64` 使用 ARM64 Host MSBuild 在至少 5 分钟超时内构建通过。
- [x] 现有 Draw2/UI3 交互回归：几何面板、绘制属性、橡皮和主栏按钮动画无行为变化。

## Out Of Scope

- 不修改 SVG 资源格式、lunasvg 库、Draw2 绘制算法、Draw3 接口或传统 `IdtFloating`。
- 不实现 SVG 旋转以外的新的图标语义、填充开关或几何工具功能。
- 不做与本次缓存/路径复用无关的渲染架构重构。

## Risks And Deferred Items

- SVG 语义颜色当前在位图中烘焙；颜色动画若要求完全无重建，需要后续引入 GPU tint/多层缓存，本任务先按颜色变化失效处理。
- 缓存位图的质量阈值需要结合 DPI 和现有动画速度校准；实现中应将阈值集中为内部常量，便于后续调参。
- 超椭圆平移若采用 `ID2D1TransformedGeometry`，需确认 PointLight 和 diffuse mask 查询使用的是已平移几何及正确包围盒。
