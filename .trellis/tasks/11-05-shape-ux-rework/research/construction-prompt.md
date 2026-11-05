# Research: 主栏构造（绘制步骤）提示仿“底栏模式”提示

- **Query**: 主栏构造提示（Bar.ConstructionPrompt.cpp）要做成与现有“底栏模式 / 底栏模式 · 居中”提示一致的样式。记录：现有底栏提示怎么画（pill 尺寸、字重、内边距、动画、锚点）；当前构造提示怎么画、差在哪（字重、间距、按钮是文字还是图标）；ShapeUiSnapshot 构造状态里有哪些步骤信息（当前步、总步数、每个工具的步骤说明）；有哪些图标资源（上一步/下一步/箭头/chevron，不能复用撤销/重做）；主栏标准按钮怎么嵌入；有没有现成的环形进度绘制；以及主栏自动折叠时提示消失的已知 bug。
- **Scope**: internal（只读源码与 spec；没有改产品代码）
- **Date**: 2026-11-05
- **Branch**: feature/shape

## Findings

### 1. 相关文件

| 文件 | 说明 |
|---|---|
| `Inkeys/Inkeys/UI/Bar/Bar.ConstructionPrompt.cpp`（143 行） | 构造提示：Initialize :35–51、Advance :52–112、Draw :113–118、Pointer/Keyboard :122–143 |
| `Inkeys/Inkeys/UI/Bar/Bar.Main.cppm:394–432` | `BarConstructionPrompt` 类声明（3 个按钮、`x_/y_` 值、`surface_`、`label_`、`opacity_`、`left_/top_/width_/height_`） |
| `Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h:27–28, 115–152, 190–229, 231–321` | 底栏提示常量、内容动作状态机、几何、缩放几何、可见包络 |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp:263–269, 643–652` | `BarBottomDockIndicatorContentPhase`；`bottomDockIndicatorWord` 等状态 |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp:6860–6946` | 提示目标、进度动画（EaseOutBack/EaseInBack）、文案选择 |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp:7597–7850` | 测字、宽度扩/换字/缩三阶段、几何、包络；`:7833` 构造提示可见时屏蔽底栏提示 |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp:11924–12010` | 构造提示 Draw 调用（:11926）与底栏提示实际绘制 |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp:7275–7279` | 每帧 `constructionPrompt.Advance(...)` 调用 |
| `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp:1024–1034` | 起笔后自动折叠（AutoCollapse.Drawing/Erasing） |
| `Inkeys/Inkeys/UI/Bar/Bar.AutoCollapse.h:8–11` | `ShouldAutoCollapse(policy, mode)` |
| `inkStrokeModelerTest/draw3/shape_interaction.h:37–43`、`shape_interaction.inl:315–457` | `ConstructionStatus` 和 `ConstructionSession` |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.ShapeInteraction.h:19–43` | `ShapeUiSnapshot`、`ShapeUiActionKind`（ConstructionComplete/Back/Cancel） |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.ShapeRuntimeActions.inl:139–145, 215–245` | 构造状态写入快照；会话开始时机 |
| `Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp:1448–1492`、`Bar.Rendering.cppm:240–245, 366` | `DrawProgressRing`（protected）/ `DrawRing`（public） |
| `Inkeys/Inkeys/UI/Bar/Bar.BusyRing.h`、`Bar.PageLoadingIndicator.h`、`Bar.Scene.cpp:803–851` | 白板翻页旋转环、页面加载环 |
| `Inkeys/src/i18n/zh-CN.jsonc:44–48, 135–147` | `BottomDock` 与 `ShapeConstruction` 文案 |

### 2. 现有“底栏模式”提示（bottomDockIndicator）的绘制方式

**常量**（`Bar.BottomDock.h:27–28`）：`BarBottomDockIndicatorHeightDip = 30.0`，`BarBottomDockIndicatorCornerRadiusDip = 6.0`。

**文案**（`Bar.RenderLoop.cpp:6895–6899`）：普通状态是 `IW(BottomDock.Mode)` = “底栏模式”；居中状态是 `Mode + L" · " + Centered` = “底栏模式 · 居中”。

**字体**（`:7604–7610`, `:11998–12003`）：`BarButtonTwoTwoLabelFontSizeDip` = **13 DIP**，**`DWRITE_FONT_WEIGHT_BOLD`**，水平/段落都居中（`DWRITE_TEXT_ALIGNMENT_CENTER` / `DWRITE_PARAGRAPH_ALIGNMENT_CENTER`），用 `D2D1_DRAW_TEXT_OPTIONS_CLIP` 绘制，颜色 `TextPrimary`。测字用 `CreateTextLayout` 加 `NO_WRAP`，宽度取 `widthIncludingTrailingWhitespace`（:7611–7628）。

**几何**（`ResolveBarBottomDockIndicatorGeometry`，`Bar.BottomDock.h:190–229`）：
```cpp
const double padding = std::max(0.0, (BarBottomDockIndicatorHeightDip - actualLabelHeightDip) / 2.0);
const double width = actualLabelWidthDip + padding * 2.0;     // 水平内边距 = 高度减文字高后的单侧余量
const double center = (min(button.left, bar.left) + max(button.right, bar.right)) / 2.0;  // 主按钮+主栏并集的水平中心
const double top = visibleMainBarTopDip - BarBottomDockIndicatorHeightDip / 2.0;          // 垂直居中压在主栏上边缘
```
- 所以 pill 高 30 DIP，宽 = 文字宽 + 2×padding（13 DIP 粗体时 padding 约等于 (30 − 行高)/2，具体取决于字体），**跨在主栏上边缘**（pill 中心 y = 主栏可见上边缘）。
- 水平/垂直坐标会经过 `bottomDockHorizontalMapping` / `bottomDockMapping` 的映射（:7766–7790），跟随底栏的果冻/刚性抓手映射。

**表面材质**（`:11960–11983`）：每帧临时构造一个 `BarUiShapeClass indicatorShape`：
- 圆角 `6 × scale`，描边 `BarButtonFrameThicknessDip(1) × scale`；
- 填充/边框颜色取 `GetThemeColor(BarThemeModeEnum::Dark, Surface/SurfaceFrame)`，并设 `themeSurface = true`、`residentSurface = true`、`lightFillOpacityScale = BarThemeMaterial::LightResidentSurfaceOpacityScale`、`lightMaterial = state.barLightMaterial.val`（也就是走主题常驻表面材质，和主栏一致）；
- `pct = BarMainBarFillOpacity(0.8) × opacity`，`framePct = frameLightPct = BarMainBarFrameOpacity(0.18) × opacity`；
- `frameRendering = PointLight`，`frameLightColor = PenWhenDrawing`，`framePrimaryLightEnabled = true`，`frameCursorLightIntensityScale = 1.0`（注释：与主栏一致）；
- `SetFrameDiffuseMaskGeometryScale(1/scale)` 包住整个绘制过程。

**动画**：
- 显隐（`:6866–6890`）：`bottomDockTargetIndicatorProgress` 用 `SetTar(target?1:0, BarUiDefaultOperationDur(0.4s), ..., curve)`，显示时曲线为 `EaseOutBack`，隐藏时为 `EaseInBack`；关闭动画时直接 SetDirect。
- 绘制时 `scale = progress`（允许 Back 曲线超过 1），`opacity = clamp(progress,0,1)`（:7826–7832）；表面按 `ResolveBarBottomDockIndicatorScaledGeometry` 绕中心缩放，文字用 `Matrix3x2F::Scale(scale*textScale)` 绕中心缩放（:11984–11995）。
- 换文案的三阶段（`ResolveBarBottomDockIndicatorContentAction` `Bar.BottomDock.h:134–152`；状态机在 `RenderLoop.cpp:7683–7760`）：新文字更宽时先 **ExpandFrame**（`bottomDockIndicatorTextWidthDip` 在 0.4s 内变宽），再 **SwapText**（`bottomDockIndicatorWord.TransitionToString(label, 0.4, 0.5, 0.8)`，缩放加淡入淡出），新文字更窄时最后 **ShrinkFrame**。完全隐藏时直接换好文字，不播放动画（:6909–6921, :7683–7690）。
- 包络（`ResolveBarBottomDockIndicatorVisualEnvelope`）会覆盖 Back 超调、按比例的描边和 Gaussian 外扩；拖动期间预留两种文案中较宽的那一个，避免扩窗（:7800–7818）。

**层级**：绘制在所有 Bar/面板内容之上，只在调试覆盖层之下（:11924–11928 先画构造提示，之后画底栏提示）。**构造提示可见时，底栏提示会被屏蔽**：
```cpp
const bool dockTargetIndicatorVisible = !owner_.constructionPrompt.Visible() && dockTargetIndicatorOpacity > 0.000001 ...   // RenderLoop.cpp:7833
```

### 3. 当前构造提示的绘制方式

**初始化**（`Bar.ConstructionPrompt.cpp:35–51`）：
- `surface_`：圆角用 `BarBottomDockIndicatorCornerRadiusDip(6)`，颜色 `GetThemeColor(Surface/SurfaceFrame)`（当前主题，**没有**设置 `themeSurface/residentSurface/lightMaterial`），`pct = 0.8`，`framePct/frameLightPct = 0.18`，`PointLight` + `PenWhenDrawing`（**没有**设置 `framePrimaryLightEnabled` 和 `frameCursorLightIntensityScale = 1.0`）。
- `label_`：`BarUiWordClass`，**12 DIP**，宽 200，高 32.5。
- 3 个按钮（完成/回退/取消）：`BarButtonSizeEnum::twoOne`，按钮 70×32.5、圆角 4，**`icon.enable = false`（只有文字）**，`name` 12 DIP，`ConfigureBarButtonVisualTargets(StandardTwoOne)` 之后再把 `name.x` 设为 0、`name.w` 设为 70（文字居中占满整个按钮）。

**布局**（`:66–85`）：
- 按钮显隐：完成按钮只在 `points`（折线/多边形）工具时显示；回退按钮在 `canBack` 时显示；取消按钮总是显示（:67）。
- `SingleRow` 布局：宽 = 5 + n×75。
- 测字：`MeasureText(label, 12, DWRITE_FONT_WEIGHT_NORMAL)`（:69）。
- `panelWidth = min(labelWidth + layout.width + 15, max(80, 工作区宽 − 10))`；放不下时改为 `stacked`，最多 3 列，每列 75（:71–75）。
- 单行时高 = `32.5 + 10 = 42.5` DIP；stacked 时高 = `5 + 32.5 + 5 + ceil(n/列数) × 37.5`。
- 锚点：只借用 `ResolveBarBottomDockIndicatorGeometry(...)` 的**水平中心**（文字宽填的是 panelWidth，高度填 12），`left` 夹在工作区 ±5 以内；垂直方向放在**主栏上方 5 DIP**（`main->inhY - targetHeight - 5`），上方放不下时放在主栏下方 5 DIP，再夹到工作区内（:77–84）。没有使用底栏的 mapping。
- 文字左对齐：`label_.x = 5 + textWidth/2`，单行时 `textWidth = width - layout.width - 10`。

**绘制**（`:113–118`）：先 `PushPanelOpacity(panel, opacity)`，再 `DrawBarBackgroundVisual(surface_)`，接着 `PushAxisAlignedClip(panel)`，然后 `renderer.Word(label_, ..., DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING)`，最后对每个按钮调用 `DrawBarButtonVisual(...)`（没有传 options，所以按 `BarButtonDrawOptions` 默认值 `primaryWeight = BOLD`，按钮文字是粗体）。

**动画**：只有 `opacity_.SetTar(open?1:0)`（默认时长）这一个淡入淡出，外加 `left_/top_/width_/height_/x_[i]/y_[i]` 的共享值动画；**没有缩放和 Back 曲线**，也没有“扩框→换字→缩框”三阶段（换字只调用了 `label_.TransitionToString(PromptLabel(status))`）。

**输入**：hit 来自共享的 `ResolveBarButtonHitSnapshot`，只有动画稳定后才允许点击（:87, :109）；点击时用 `SubmitShapeUiIntent(ConstructionComplete/Back/Cancel)` 并校验 identity（:133）；键盘 Esc 取消、Tab/←/→ 切换焦点、Enter/Space 执行（:137–143）。

**与底栏提示的差异汇总**：

| 项 | 底栏模式提示 | 当前构造提示 |
|---|---|---|
| 高度 | 30 DIP | 42.5 DIP（单行） |
| 圆角 | 6 | 6 |
| 文字字号/字重 | 13 DIP **Bold**，居中 | 12 DIP **Normal**，左对齐 |
| 水平内边距 | (30 − 文字高)/2 | 左 5 DIP，文字与按钮之间无专门间隔，按钮间隔 5 |
| 垂直锚点 | 跨在主栏上边缘（中心 = 主栏 top） | 主栏上方 5 DIP（或下方） |
| 水平锚点 | 主按钮+主栏并集中心，经底栏 mapping | 同一函数的中心，但没有经过 mapping |
| 表面材质 | Dark 基色 + themeSurface/residentSurface/lightMaterial，主光源开启，鼠标光 1.0 | 当前主题 Surface，没有 resident/material 标志 |
| 显隐动画 | 0.4s EaseOutBack/EaseInBack 缩放+透明度 | 只有透明度 |
| 换字动画 | 扩框→TransitionToString(0.4,0.5,0.8)→缩框 | label_.TransitionToString 默认参数，宽度用值动画 |
| 按钮 | 无 | 3 个 70×32.5 纯文字 twoOne 按钮（粗体 12 DIP） |

### 4. ShapeUiSnapshot 中可用的步骤信息

`ConstructionStatus`（`shape_interaction.h:37–43`）：
```cpp
struct ConstructionStatus
{
    bool active = false;
    uint32_t step = 0, confirmedSteps = 0;
    bool canComplete = false, canBack = false, finished = false;
    PresetKind preset = PresetKind::Line;
};
```
`Status()`（`shape_interaction.inl:454–457`）：
```cpp
return { active_, active_ ? (finished_ ? confirmed_ : confirmed_ + 1) : 0, confirmed_, CanComplete(), active_ && confirmed_ > 0, finished_, tool_.preset };
```
- `step`：当前步（从 1 开始；完成后等于已确认步数）。`confirmedSteps`：已确认步数。
- **快照里没有总步数字段**。可以从 `GetToolCapabilities` 推出来：`multiStage`（`preset >= Polyline`）中，非 points 的工具是 2 步（`ConfirmDrag`：第一次确认后 `confirmed_ = 1`，第二次 `++confirmed_; finished_ = true`，:410–416）；`points`（折线/多边形）步数不固定（最多 `kMaxVertices = 16384` 个点，`shape_core.cpp:15`），需要手动点“完成”。非 multiStage 工具第一次确认后 `finished_ = true`（1 步）。
- 写入快照（`Draw3.ShapeRuntimeActions.inl:139–145`）：只有 `construction->session.Status().active` 时才写入；**物理接触进行中**（`contactActive`）会把 `canBack/canComplete` 强制改为 false。
- 会话开始的时机：第一次落笔时 `session.Begin`（`:215–245`），所以第一步还在拖动时 `active = true, step = 1, confirmedSteps = 0`。
- 每个工具的步骤文案（`Bar.ConstructionPrompt.cpp:21–31` `PromptLabel`，i18n `ShapeConstruction`，`zh-CN.jsonc:135–147`）：

| 工具 | 第 1 步（confirmedSteps==0） | 第 2 步 |
|---|---|---|
| 折线/多边形（points） | NextVertex “落笔确定下一顶点”（每一步都一样） | — |
| 圆弧/扇形 | RadiusStep “确定圆心与半径” | SweepStep “确定圆弧范围” |
| 长方体/正方体 | FrontStep “确定正面” | DepthStep “确定深度” |
| 圆柱/圆锥/棱柱/棱锥（其余） | BaseStep “确定底面” | HeightStep “确定高度” |

按钮文案：`Complete` “完成”、`Back` “回退”、`CancelAction` “取消”。
- 动作种类（`Draw3.ShapeInteraction.h:30–34`）：`ConstructionComplete / ConstructionBack / ConstructionCancel`；**没有“下一步/前进(redo)”动作**。全局 Undo 在构造中会执行 Back（`ShapeRuntimeActions.inl:353–364`；spec `shapes.md` P3+P4 第 31 行）。

### 5. 图标资源（`Inkeys/src/UI/*.svg`）

grep `prev|next|back|forward|chevron|arrow|left|right|check|close|cancel|done|more|thickness` 的结果：

| 资源 | 形状 | 现有用法 |
|---|---|---|
| `barMore.svg` | 粗 chevron（^ 形，24×24，填充路径，主题色 `rgba(10,0,7,0)`） | 白板/PPT 分页的上一页/下一页：图标角度 −90（上一页）/ 90（下一页），竖排时用 0/180（`Bar.Scene.cpp:414–417`，通过 `ConfigureButtonContent(..., angle, ...)`） |
| `barThicknessAdjust.svg` | 较小的 ^ chevron | 橡皮“自动”按钮旁边的展开箭头，通过 `automaticArrow_.angle` 旋转 0/180（`Bar.EraserAttribute.cpp:158–160, 363–364`） |
| `barCloseSmall.svg` | × | 图形面板关闭按钮（`Bar.GeometryAttribute.cpp:81`）、白板操作 |
| `fluentCheckmark12Filled.svg` | ✓（viewBox 12×12，128×128） | 已登记 `Inkeys.rc:418` |
| `barShapeArrow.svg` | 箭头图形（描边 2.6） | 图形类别“箭头”，不是 UI 方向图标 |

- **禁止复用**：`barRecall.svg`（撤销）、`barRedo.svg`（重做）。
- 没有找到独立的 left/right/prev/next/back/forward/chevron 命名资源；现有的左右方向图标都是把 `barMore` 旋转得到的。
- 资源都以名称型 `UI` 资源登记在 `Inkeys/Inkeys.rc`（例如 `BARMORE`、`BARCLOSESMALL`、`BARTHICKNESSADJUST`、`FLUENTCHECKMARK12FILLED`）。

### 6. 嵌入主栏标准按钮的方式

- 标准尺寸（`Bar.Metrics.cppm:200–279`）：`StandardOneOne` 为 32.5×32.5，图标 20；`StandardTwoOne` 为 70×32.5，图标 18 在 x −21，文字 12 DIP 在 x +11.5，文字槽宽 37；`StandardTwoTwo` 为 70×70。
- 注意尺寸关系：底栏提示 pill 高 30 DIP，**小于** OneOne 按钮的 32.5 DIP。
- 图标按钮示例（`Bar.ShapeEditor.cpp:254–266`）：
  ```cpp
  p->icon.Initialization(0,0,TextPrimary,TextPrimary);
  if(!resource.empty())p->icon.InitializationFromResource(L"UI",resource);
  // 公共oneOne布局必须在载荷就绪后解析SVG比例；它默认没有文字槽。
  ConfigureBarButtonVisualTargets(*p,BarButtonVisualLayoutKind::StandardOneOne,0,1,true, ...);
  ```
- 公共 API（`Bar.Main.cppm:215–300, 336`）：`ConfigureBarButtonVisualTargets(button, layoutKind, dur, scale, direct, iconHeightOverride, fontTargetScale)`、`RetargetBarButtonInteractionVisual(button, visible, enabled, selected, dur, contentColor, iconVisible, fill, frame)`、`SetBarButtonPressedVisual`、`StartBarButtonHoverVisual/StopBarButtonHoverVisual`、`AdvanceBarButtonVisual(button, {dt, speed, animationEnabled, forceReplace, visible, hoverAllowed})`、`DrawBarButtonVisual(renderer, dc, button, inherit, BarButtonDrawOptions{primaryWeight=BOLD 默认, drawIcon, content...})`、`ResolveBarButtonHitSnapshot` / `HitTestBarButton`。禁用状态用 `state->state = BarWidgetState::Disable`，加上 `RetargetBarButtonInteractionVisual(enabled=false)`，公共内容透明度为 0.30。
- 当前构造提示已经接入这套 API（`Bar.ConstructionPrompt.cpp:105–109`），只是 icon 被关掉了（:48）。图标旋转可以参考 `Bar.Scene.cpp:315–330` `ConfigureButtonContent`（`InitializationFromResource` / `TransitionToResource` 加角度）。
- 当前 `buttons_` 数组固定 3 个（`Bar.Main.cppm` `std::array<std::shared_ptr<BarButtonClass>,3>`，`Frame::buttons` 也是 `std::array<...,3>`），Pointer/Keyboard 用 0/1/2 硬映射到 Complete/Back/Cancel（:133, :142）。

### 7. 现成的环形进度绘制

- `BarUIRendering::DrawProgressRing(dc, center, radius, strokeWidth, progress, trackColor, progressColor, trackOpacity, progressOpacity)`（`Bar.Rendering.cpp:1448–1492`，声明为 **protected**，`Bar.Rendering.cppm:366`）：先画整圈轨道，再从 12 点方向顺时针画 `360×progress` 度的弧（`ID2D1PathGeometry` + `AddArc`）；progress ≥ 0.999 时直接画整圆。
- 公开包装 `DrawRing(dc, center, radius, stroke, progress, color, trackOpacity, progressOpacity)`（`Bar.Rendering.cppm:240–245`），轨道和进度用同一个颜色。
- 现有用法：
  - 粗细/取色器的长按进度环：`Bar.RenderLoop.cpp:11480–11499`、`:11770–11785`（轨道色 = Mix(TextPrimary, Surface, 0.70)，进度色 = Mix(…, 0.35)，轨道透明度 ×0.45）；
  - 白板翻页忙碌环 `Bar.Scene.cpp:837–851`（`DrawRing`，28% 弧段整体旋转，`BusyRing.h`：延迟 0.30s，周期 0.90s）；
  - 页面加载指示 `Bar.Scene.cpp:803–832`（`PageLoadingStyle`：半径 5.4，轨道 0.6，弧宽 1.2，250° 弧）。
- 每次调用 DrawProgressRing 都会新建一个 PathGeometry（没有缓存）。

### 8. 已知 bug：主栏自动折叠时提示消失

- `Bar.ConstructionPrompt.cpp:54`：
  ```cpp
  const bool open=snapshot&&snapshot->construction.active&&!owner.barState.fold;
  ```
  只要 `fold` 为 true，提示就会淡出，`frame_.visible/input` 也随之清空（:57）。
- `Bar.Interaction.cpp:1024–1034`：每一笔被接受的起笔通知（`AcceptedStrokeBegin`）都会检查一次：
  ```cpp
  const int policy = notice.kind == ...CompletedStrokeKind::Eraser
      ? Inkeys::config.UI.Bar.AutoCollapse.Erasing : Inkeys::config.UI.Bar.AutoCollapse.Drawing;
  if (ShouldAutoCollapse(policy, barUISet.bottomDockMode.load(...)))
      FoldBarAutomatically();
  ```
  `ShouldAutoCollapse`（`Bar.AutoCollapse.h:8–11`）：`policy == 2`，或者 `policy == 1` 且处于 Floating 模式时返回 true。图形笔属于 `CompletedStrokeKind::Shape`（`Draw3.Bridge.h:67–73`），不是 Eraser，因此走 `AutoCollapse.Drawing` 策略。
- 结果：在多步构造中，第一步落笔就会触发自动折叠，`fold = true`，提示随即隐藏；而这时用户恰好需要它来完成或回退下一步。
- 相关的屏蔽：构造提示可见时，底栏提示被屏蔽（`RenderLoop.cpp:7833`）；`RenderLoop.cpp:7317` 把 `constructionPrompt.Visible()` 计入 idle-fade 保护。

## Related Specs

- `.trellis/spec/native-desktop/shapes.md`（P3+P4 合同，第 31 行）：主栏提示是 Bar 场景里的独立状态，“复用原居中提示位置/外观/动画”，包含必要的 Complete/Back/Cancel，不得覆盖原 bottomDockIndicator 状态，也不得新增 HWND；全局 Undo 在构造中执行 Back。
- `.trellis/spec/native-desktop/shapes.md`（第 53 行）：构造提示沿用原 pill 的水平锚点，但面板高度和旧提示不同，需要按实际 panelHeight 在主栏上方留 5 DIP；小空间时动作按钮保持正常尺寸并换行。
- `.trellis/spec/native-desktop/rendering-and-ui.md:999` 一带：底栏提示资格锁存，Free 显示“底栏模式”，Centered 显示“底栏模式 · 居中”。
- `.trellis/spec/native-desktop/rendering-and-ui.md:2458–2462`：UI3 公共按钮的 Configure/Advance/Draw/成功呈现 hit 合同。

## Caveats / Not Found

- 没有运行构建/GUI，pill 的实际像素宽度（取决于字体行高）没有测量。
- `ConstructionStatus` 没有 totalSteps，也没有每一步的说明字段；总步数只能由 `preset` 和 capabilities 推出（points 类不固定）。
- 业务层没有“下一步/前进”动作，只有 Complete/Back/Cancel。
- 没有专门的 prev/next/chevron 命名 SVG；现有方向图标都是 `barMore`（或 `barThicknessAdjust`）旋转后使用。
- `DrawProgressRing` 是 protected，外部组件目前通过 public 的 `DrawRing` 调用（同色轨道）。
