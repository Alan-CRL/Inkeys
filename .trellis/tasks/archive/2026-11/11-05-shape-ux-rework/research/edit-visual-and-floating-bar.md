# Research: 图形编辑框视觉与浮动属性条（复用主栏标准）

- **Query**: 记录 ShapeEditor 编辑框/控制点/旋转按钮/浮条的当前实现；主栏分隔线、按钮显隐动画、绘制属性取色与取色器分层、屏幕适配；主题色与浅色处理、已有 WinUI3 滑块 Thumb；主栏 GeometryAttribute 属性 list；常驻透明度；给出复用映射。
- **Scope**: internal
- **Date**: 2026-11-05
- 路径前缀：`Bar/` = `Inkeys/Inkeys/UI/Bar/`。行号以 feature/shape 当前工作树为准。

---

## 0. 宿主与渲染管线（复用前提）

| 文件 | 说明 |
|---|---|
| `Inkeys/Inkeys/UI/SelectionOverlay/SelectionOverlay.cpp` | 编辑器宿主：独立 layered HWND（`WS_EX_LAYERED|NOACTIVATE|TOOLWINDOW`，:136-137），注册为 `RenderPipeline::Client::SelectionOverlay`（:148），与 Bar 共用同一渲染线程/DeviceEpoch |
| `Bar/Bar.ShapeEditor.cpp` | `ShapeEditorSurface::Impl`：全部按钮、动画、绘制、命中、输入 |
| `Bar/Bar.Scene.cppm:180-230` | `ShapeEditorHit` / `ShapeEditorSurface` 声明 |

- `Render`（SelectionOverlay.cpp:41-70）：每帧取 `ProductShapeUiSnapshot()`、按对象中心找显示器 `workArea`、读主栏窗口矩形 `bar`，`Configure → Advance → EnsureDeviceResources → Present(ULW) → SetBounds/Show → CommitPresented`。窗口边界 = `ShapeEditorSurface::Bounds()`，**每帧随内容变化**（编辑器自己算 bounds，Present 整幅重画，无脏区）。
- `ShapeEditorSurface` 自持 `BarUIRendering renderer`（ShapeEditor.cpp:195），即与主栏同类的渲染器，可用 `Shape/Svg/Word/ColorPalette/ContentSolidBrush/PushPanelOpacity/DrawBarButtonVisual/DrawBarBackgroundVisual`。不持有 `BarUISetClass`，**不能访问主栏 `shapeMap/svgMap/barState`**。
- 场景对象在 `Acquire()` 时创建一次（SelectionOverlay.cpp:144），跨多次编辑会话复用（影响下文 `initialized` 语义）。
- 主题：`GetThemeColor()` 读全局 `currentBarThemeStyle`（Bar.Theme.cppm:135-141），生产中由 `Bar.Initialization.cpp:309 SetThemeStyleSource(&barUISet.barStyle)` 绑定，故编辑器在同渲染线程上读到的主题端点与主栏一致（但没有主栏的连续材质权重，见 §3）。

---

## 1. 当前 ShapeEditor 绘制（Bar/Bar.ShapeEditor.cpp）

### 1.1 ID 与成员

- ID 常量（:41-43）：`Color=1, Style=2..4, Pattern=5..8, Thickness=9..11, Sides=12..21, Swatch=22..32, Tone=33, Rotate=34, Count=35`；`PaletteHit=100, ScrollHit=101`。
- 资源（:115-123）：Style→`barShapeOutline/Fill/OutlineFill`，Pattern→`barShapeSolid/Dashed/Dotted/DashDot`，Rotate→`barShapeRotate`，Tone→`colorSun`（运行时切 `colorMoon`，:480），Swatch→`colorSelect`（绿勾徽标）。
- `Impl` 成员（:193-244）：`BarButtonListClass attributes, sideList, swatches`；`buttons[Count]`（`BarButtonClass`）；`dividers[2]`；面板形状 `toolbar, parameterPanel, colorPanel, scrollRail, scrollThumb, rotateSurface`；每个按钮独立 `x[id], y[id]`（`BarUiValueClass`）；`dividerX/dividerY`；`parameterExtent, colorExtent, toolbarWidthValue, toolbarHeightValue`；`opacity`（`BarUiPctClass`）；`HandleVisual{id, circle, press}`（:242）。

### 1.2 共享形状初始化 `InitShape`（:51-59）

```cpp
p.Initialization(0,0,1,1,radius,radius,BarButtonFrameThicknessDip,
    GetThemeColor(Surface),GetThemeColor(SurfaceFrame));
p.pct.SetDirect(opacity); p.framePct.emplace(BarMainBarFrameOpacity); p.frameLightPct.emplace(BarMainBarFrameOpacity);
p.frameRendering = PointLight; p.frameLightColor = PenWhenDrawing;
```

- 浮条/参数区/颜色区：`InitShape(...,BarMainBarCornerRadiusDip=8,BarMainBarFillOpacity=0.8)`（:248-250）。注意：主栏弹出面板（DrawAttribute/Geometry）用 `BarDrawAttributeSurfaceOpacity=0.95`（Bar.Main.cpp:75），编辑器浮条用常驻主栏的 0.8。
- 未设置 `themeSurface`、`lightMaterial`（默认 0）——见 §3.4。

### 1.3 对象外框（"青色框"）

`Draw()` :592-604：

- 仅当 `localBounds.valid && kind != Line/Polyline/Circle` 才画。
- 4 条 `DrawLine`，端点 = `ApplyTransform(object.transform, localBounds 四角)`，即**紧贴对象本地包围盒，无外扩间距**。
- 画刷：`ContentSolidBrush(GetThemeColor(Accent), .70)`；线宽 `scale`（=1 DIP）；直角（逐边 DrawLine，无圆角、无连接）。
- Accent：Dark `RGB(88,255,236)`（Bar.Theme.cppm:105-106），Light 走 `BarThemeMaterial::LightColor(Accent)` = `#006A63`（spec bar-theme-material.md）。
- 输入热区：`candidate.input` 时以 alpha 1/255 的 Surface 色填 `bounds±3*scale` 矩形和每个控制点 22*scale 半径圆（:581-590），仅为让 layered 窗口接收输入。

### 1.4 控制点

`Advance()` :526-540，`Draw()` :605-610：

- 每个非 Rotate 手柄一个 `HandleVisual`，`InitShape(v->circle, 6, 1)` 后 `framePct=1, frameLightPct=1`（:529）。
- 直径 `12 * press.val` DIP（:536），`rw=rh=d/2`；填充 `Surface`、边框 `Accent`（:535），边框宽 `BarButtonFrameThicknessDip=1`（来自 InitShape）。即"表面色实心圆 + 1DIP 主题色描边"，**无内点**。
- 按压：`press.SetTar(BarButtonPressScale=0.95, BarUiDefaultOperationDur, ..., Press/ReleaseCurve)`（:532-533）。
- 悬停：`frameLightPct.SetTar(hovered ? BarButtonPressedLightOpacity(0.5) : 0, BarButtonHoverTransitionDuration=0.24)`（:534）——只改变点光边缘光，不改尺寸/颜色。
- 绘制：`renderer.Shape(dc, circle, ...)`（:609）。
- 命中：`Hit()` :315-323，半径 `22*scale`（≈44DIP 触达），优先级 Rotate(0) < 专属点(1) < Scale(2)，同级取最近。
- spec shapes.md P3+P4：圆形视觉默认 12DIP，旋转 32.5DIP，热区至少 44DIP。

### 1.5 旋转按钮

- 手柄位置来自共享核心 `inkStrokeModelerTest/draw3/shape_interaction.inl:139-145`：本地包围盒上边中点沿对象 y 轴反向外移 `40 * displayScale`。
- 视觉（:516-525）：`buttons[Rotate]` 为标准 `BarButtonClass`（oneOne 32.5DIP，`rw=rh=OneSide/2` 圆形，:274），图标 `barShapeRotate`；底板 `rotateSurface = InitShape(OneSide/2, 0.8)`（:251）作为 `options.underlay` 画在按钮变换内（:549）。悬停/按压走 `Start/StopBarButtonHoverVisual`、`SetBarButtonPressedVisual`。
- 因此旋转按钮 = 32.5DIP 主题表面圆 + 1DIP SurfaceFrame 边 + 图标，与控制点（12DIP Surface+Accent 边）风格不统一。

### 1.6 浮条布局

`Advance()` :363-468：

- 显隐规则（:364-369）：`capabilities = GetToolCapabilities(preset)`；`showStyle = capabilities.fill`，`showStroke = style.stroke || !showStyle`；Color 恒显，Style 组随 showStyle，Pattern/Thickness 随 showStroke；`dividers[0].hide = !(showStyle&&showStroke)`，`dividers[1].hide = !showStroke`。
- 列表：`attributes` = `[Color, Style×3, Divider0, Pattern×4, Divider1, Thickness×3]`（:276-288）；`sideList` = 10 个边数按钮；`swatches` = 11 色块。
- 行布局：`ResolveBarButtonListLayout(attributes.Snapshot(), SingleRow)`（:370），再按可用宽 `available = workWidth/scale - 10` 手动换行（:376-384，`rowHeight = 32.5+5`）。工具条宽高 `toolbarWidthValue/HeightValue`（:373,386）。
- 参数区（边数 3–12）：`regularSides` 时出现，`sideColumns = clamp((toolbarWidth-42.5)/37.5, 1, 10)`，高 `ceil(10/cols)*37.5+5`（:388-390,453-459）。**位于浮条下方的独立扩展面板**（parameterPanel），手动网格，不经 list layout（sideLayout 被解析但未使用，:457）。
- 颜色区：`colorOpen` 时出现，`colorColumns ≤ 6`，高 `ceil(12/cols)*37.5 + 133`（:391）；11 个色块 + Tone 按钮网格（:465-466），下方 `palette` 矩形（:467）画 `renderer.ColorPalette(dc, p, darkTone, true)`（:624）。**与浮条同一窗口内向下展开，不是新层**。
- 扩展区总高受 `work.bottom - toolbar - 20` 限制，超出则出现滚动轨道/拇指（:394-397, 443-451）；滚轮/拖动（:691-705）。
- 定位（:399-410）：水平居中于对象 bounds，`screenY = bounds.bottom + 16*scale`，下方放不下则翻到对象上方；`clamp` 到 workArea（5*scale 边距）；与主栏矩形相交则改到主栏上/下方。
- 窗口 bounds = 浮条 ±8*scale ∪ 每个 handle ±26*scale ∪ 对象 bounds ±8*scale（:411-424）。bounds 变化时对按钮 x/y 做 `ShiftOrigin`（:425-433）。

### 1.7 浮条使用的控件

- 按钮：标准 `BarButtonClass` + `ConfigureBarButtonVisualTargets(StandardOneOne, 0, 1, true, swatch?15:nullopt)`（:266-267），状态由 `RetargetBarButtonInteractionVisual(p, wanted, true, selected, 0.4, TextPrimary, iconVisible)`（:479），推进 `AdvanceBarButtonVisual`（:491），绘制 `DrawBarButtonVisual`（:575）。
- 自定义内容（:550-573）：
  - Thickness：-45° 旋转的胶囊（直径 24*z，宽=预设宽），同 Geometry 面板画法。
  - Color 入口与 Swatch：**圆形** `FillEllipse` 半径 10*z + `SwatchFrame` 1*z 描边；Swatch 选中时叠加 `colorSelect` SVG（:569）。主栏绘制属性色块是 30×30 圆角 4 的**圆角矩形**（§2.3）。
- Tone（色系）按钮：`colorSun/colorMoon`，`darkTone` 为编辑器本地 atomic（:216,636），非主栏 `colorPickerDarkTone`。
- 背景：`DrawBarBackgroundVisual(toolbar)`（:611），参数/颜色面板同理（:619,622）。

### 1.8 分隔线（与主栏差异的根源）

初始化 :279-285：

```cpp
d->preset = Divider; InitShape(d->button, .5, .30);   // radius .5, pct .30
d->button.framePct->SetDirect(0);
```

推进/放置 :500-509：

```cpp
const double dx = x[following].val - OneSide/2 - 3;
dividerX[di].SetTar(dx); dividerY[di].SetTar(y[following].val);   // 无时长 → 速度型
d.button.pct.SetTar(wanted?.30:0); d.button.frameLightPct->SetTar(wanted?1:0);
PlaceShape(d.button, dividerX.val, dividerY.val-(OneSide-4)/2, 1, OneSide-4);
d.button.fill->SetDirect(GetThemeColor(SurfaceFrame));
```

绘制 :614：`renderer.Shape(dc, d->button, ...)`（在工具条 clip 内、按钮之后）。

与主栏/Geometry 面板的差异（事实罗列）：

| 项 | ShapeEditor | 主栏 Divider（Bar.Button.cpp:225-250, RenderLoop.cpp:2716-2733） | Geometry 属性 list 分隔项（GeometryAttribute.cpp:225-235, 560-576） |
|---|---|---|---|
| 颜色 | `SurfaceFrame` | `BarThemeColorEnum::Divider`（Light `#A7A7A7` 预混 30% 下≈10%黑） | `SurfaceFrame` |
| frameLightColor | `PenWhenDrawing`（InitShape） | `Frame` | 默认 `Frame` |
| framePrimaryLightEnabled | 默认 true | false | false |
| frameCursorLightIntensityScale | 默认 1.0 | `BarGeometryAttributeDividerCursorLightIntensity`(=0.30) | `BarUiDividerCursorLightIntensity`(0.30) |
| 圆角 | 0.5 | 0.5（`BarUiDividerRadius`, Bar.Main.cpp:79） | 0.5 |
| 位置 | 跟随后继按钮 `x-OneSide/2-3`，**dividerX/Y 速度型 SetTar** | list placement + 批次时长 | list placement（宽1、高 OneSide-4、居中，Button.cpp:2600-2609），x 用 `attributeX_[member]` 定时长 0.4 |
| 显隐时长 | pct 默认 Pct 时长 | `operationDur`（批次） | `BarButtonHoverTransitionDuration`（0.24） |
| 主栏尺寸 | 1×28.5 | 1×50（主栏 oneTwo，Button.cpp:2640-2642） | 1×28.5 |

### 1.9 动画

| 动画 | 实现 | 位置 |
|---|---|---|
| 整体出现/消失 | 仅 `opacity.SetTar(visible?1:0, 0.4)`（BarUiPctClass 默认曲线 EaseOutSine），绘制时 `PushPanelOpacity` 整幅 | :352, :591 |
| 输入门禁 | `next.input = requestedVisible && opacity.IsSame() && opacity.val>.99` | :437 |
| 工具条宽/高 | `toolbarWidthValue/HeightValue.SetTar(target)` **无时长** | :373, :386 |
| 按钮位置 | `x[id]/y[id].SetTar(cx/cy)` **无时长**；仅 `!initialized` 时 SetDirect | :484-490 |
| 按钮显隐 | `RetargetBarButtonInteractionVisual(..., wanted, ..., 0.4)`：button.pct 0.4s；icon/name pct 用 `BarButtonHoverTransitionDuration=0.24`（Button.cpp:2061-2068） | :479 |
| 参数/颜色区展开 | `parameterExtent/colorExtent.SetTar(target)` **无时长**，裁剪区随高度增长自上而下露出 | :392, :615-628 |
| 滚动 | `scroll.SetTar(requested)` 无时长 | :397 |
| 分隔线 | 位置速度型；pct 0.30↔0 | :503-506 |

`BarUiValueClass::SetTar` 无 `durT` 时：`dur = distance / des`，`des = BarUiDefaultDes = 600 DIP/s`（Bar.Animation.cppm:836；Bar.Animation.cpp:19），默认曲线 EaseInOutCubic。即编辑器几何动画是**按距离计速、无共享时间轴/批次**；主栏/属性面板是 **固定 0.4s 批次 + EaseOutBack/EaseOutCubic + CanJoin**（§2.2）。

### 1.10 "从左到右加载"的成因（代码事实）

1. **分隔线从局部原点飞入**：`dividerX/dividerY` 初值 0（:211），从不 `SetDirect`，首次出现即以 600DIP/s 从窗口局部 (0,0)（左上）移动到目标（:503-504）。
2. **首帧未需要的按钮坐标为 0**：`!initialized` 时只对 `wanted` 的按钮 `SetDirect`（:488）；当时不需要的按钮（Swatch/Tone——colorOpen 默认 false；Sides——非正多边形；被隐藏的 Style/Pattern/Thickness）`x/y` 保持 0（只经 `ShiftOrigin` 平移）。之后变为 wanted 时从左上角速度型移入。
3. **`initialized` 跨会话保持**：只在 DPI scale 变化时置 false（:351）；场景对象跨编辑会话复用（SelectionOverlay.cpp:144）。新对象打开时 `toolbarWidthValue`/`toolbarHeightValue` 从上次值速度型过渡，`x/y` 按旧 bounds→新 bounds `ShiftOrigin` 后再 SetTar，按钮从上一会话的相对位置滑到新位置。
4. **工具条 clip 随宽度增长**：按钮绘制被 `PushAxisAlignedClip(candidate.toolbar)` 裁剪（:612），而 toolbar 宽为动画值 → 宽度变大时内容自左向右被逐步露出。
5. 关闭时 `Configure(null)` 不清 `snapshot`（:348-349），opacity 0.4s 淡出期间仍按旧布局推进；重开时只有 opacity 从当前值回升，无锚点/缩放入场。

---

## 2. 主栏可复用标准

### 2.1 分隔线

- 主栏模板：`Bar.Button.cpp:225-250`（oneTwo、`Divider` 色、pct 0.30、`framePct 0`、`frameLightPct 0`、PointLight、`frameLightColor=Frame`、`framePrimaryLightEnabled=false`）。
- 每帧目标：`Bar.RenderLoop.cpp:2716-2733`（w/h/rw/rh/ft/framePct/frameLightPct/pct 全部 `SetTar(..., operationDur)`，颜色 `Divider`，`frameCursorLightIntensityScale=BarGeometryAttributeDividerCursorLightIntensity`），:2765-2775 不参与按钮状态机（pressScale=1、无 hover）。
- 主栏 Divider 是 `AdvanceBarButtonVisual` 的特例（Button.cpp:2445-2450：停 hover/press）。
- 绘制属性内分隔线：粗细区 `RenderLoop.cpp:3707-3725`（Divider 色、pct 0.30）、笔型扩展分隔 :5475-5520（颜色 `MixColor(Dark Divider, LightColor(Divider), lightMaterial)`）。
- 更多面板分隔：`MorePanelDivider` 用 `SurfaceFrame`（:1567），pct `0.30*opacityProgress`（:4637）。

### 2.2 主栏按钮出现/消失（批次与锚点）

- 批次时间轴 `state.mainBarTimeline`（`BarUiTimelineClass`, Bar.Animation.cppm:458-491）：`RenderLoop.cpp:2600-2650`
  - 布局变化 → `Restart(operationDur=0.4)`；展开曲线 `EaseOutBack`、收缩 `EaseOutCubic`、折叠 `EaseInBack`；换边 `EaseInOutCubic` + 中间关键帧。
  - 批次 `progress ≤ 0.5` 新目标 `CanJoin` 共享剩余时长（spec rendering-and-ui.md「UI3 动画批次加入与关键帧中点」）。
  - pct 曲线：展开 `EaseOutSine`，收缩 `EaseOutCubic`（:2643-2646）。
- 位置提交 `SetButtonPositionTar` → `BarUiSetLayoutPositionTarget(value, target, operationDur, newBatch, syncedValueCurve)`（:2665-2680；Animation.cppm:858-865）。
- 隐藏按钮锚点 `AnchorHiddenBarButton`（RenderLoop.cpp:915-951；调用 :2810-2830）：隐藏成员位置收拢到前一可见成员（无则后继），显示时从该来源位置展开；退场成员先画、可见成员覆盖（:10950-10963 两遍绘制）。
- 显隐视觉：`RetargetBarButtonInteractionVisual(*temp, visible, enabled, selected, operationDur)`（:2745），并 `SyncValueDuration/SyncPctDuration` 让按钮所有部件在同一批次时长到达（:2780-2797）。
- 主栏单行属性 list 的规范版本就是 Geometry 面板 attributeList（§4），`attributeX_[index].SetTar(placement.x, BarUiDefaultOperationDur)`，隐藏项保持 list 位置并由 pct 淡出。

### 2.3 属性面板出现/消失（Popup 回弹）

`Bar/Bar.EraserAttributeMotion.cppm`：

- `BarAttributePanelMotion::Retarget(open, duration, parent)`（:53-63）：geometry `EaseOutBack/EaseInBack`、opacity `EaseOutSine/EaseInSine`，可加入父时间轴。
- `ResolveEraserSurfacePose(panel, anchor, geometry, compactWidth)`（:22-30）：从触发按钮中心、紧凑宽（60DIP）缩放到完整面板。
- `ResolveBarAttributePanelFrame`（:84-100）；`FitEraserSurfacePose`（:31-37）把姿态夹进工作区；`ResolveEraserPopupEnvelope`（:39-52）回弹外扩包络。
- 消费者：GeometryAttribute.cpp:337-380、EraserAttribute、RecallAttribute；纯 header 模块，无 BarUISetClass 依赖，**编辑器可直接使用**。

### 2.4 绘制属性颜色选择（"绘制属性"面板）

结构：固定 `shapeMap/svgMap/pngMap` 枚举（Bar.Main.cppm:502-560），非 list/非独立类。

| 部件 | 初始化 | 布局/动画 | 交互 |
|---|---|---|---|
| ColorSelect1..11 | Bar.Initialization.cpp:444-637：`BarUiShapeClass(0,0,30,30, r=4, ft=1, PresetColor, SwatchFrame)` **30×30 圆角矩形**；勾 `colorSelect` SVG 15×15；PointLight、`framePct 0`、`BarColorSwatchCursorLightIntensity=0.5`、`frameLightOpacitySource=ObjectPct`（:639-650） | RenderLoop.cpp:3086-3385：6 列×2 行，x=5..180 步 35，`colorTopY/BottomY` 随 primaryBar 上下翻转；选中 = `CompereColorRef(GetPenColor, fill.tar)` → svg pct | Interaction.cpp:7893-7945：按下即 `SetInteractionPenColor`，支持拖选切换色块 |
| ColorSelect12（自定义入口） | Initialization.cpp:584-635：30×30 **圆形**（r=15），PNG 色轮 `colorCustom` 铺满，内芯 20×20 当前色，右下 `colorSelect` 勾 | RenderLoop.cpp:3388-3455：位于第 6 列下行；按压缩放 `drawAttributeColorPickerEntryPressScale` | Interaction.cpp:5856-5899：Up 时切换 `colorPickerOpen`，`ProjectCurrentColorPickerPoint()` |
| 取色器面板 ColorPickerPanel | Initialization.cpp:665-680（Surface/SurfaceFrame、r=8、PointLight、`framePrimaryLightEnabled=true`、`themeSurface=true` 见 :1306） | 进度 `drawAttributeColorPickerProgress.SetTar(open, 0.40, EaseOutBack/EaseInBack)`（RenderLoop.cpp:1852-1862）；几何 :6330-6420：从入口锚点紧凑态（`BarColorPickerCompactScale`）缩放展开，尺寸 300×209、调色板 290×132 @(5,40)、顶部 Tone/Close 30×30 | 色板拖动、Hold 锁定提示、键盘（Interaction.cpp:4845-5180） |
| 绘制 | — | RenderLoop.cpp:11515 起："简易颜色选择器作为同窗顶层内容绘制"；色板 `BarUIRendering::ColorPalette`（Rendering.cpp:3467-3480，hue 渐变 + tone 渐变，圆角 4） | `GetBarColorPickerColor/ProjectBarColorPickerColor`（Layout.cppm:506-537，纯函数） |

分层方式：主栏窗口 `floating_window` 是全屏 layered 窗口，取色器是同窗口内后绘的"顶层"内容（RenderLoop.cpp:6329 注释"不创建或移动额外 HWND"），即"同窗口新层"。绘制属性面板本身是一层，取色器是其上一层。

屏幕适配：
- 取色器**不按屏幕边夹紧**：RenderLoop.cpp:6361 注释"不按屏幕边夹紧，可越出可视区"，只按 primaryBar 决定向下/向上。
- 粗细预览弹窗有 `ClampPopupCenter`（RenderLoop.cpp:9122-9130）夹到主栏窗口逻辑尺寸——可作为夹紧写法参考。
- Geometry 面板使用 `FitEraserSurfacePose` + fit 缩放 + 换边（GeometryAttribute.cpp:315-380）。

可复用性评估：
- 可直接复用（无 BarUISetClass 依赖）：`BarUIRendering::ColorPalette`、`GetBarColorPickerColor/ProjectBarColorPickerColor/IsBarPresetColor`、`GetPresetColor`、`colorSelect`/`colorSun`/`colorMoon` SVG、`colorCustom` PNG（`BarUiPNGClass` 资源）、`BarAttributePanelMotion`/`ResolveEraserSurfacePose`/`FitEraserSurfacePose`。
- 不可直接复用：色块布局、取色器几何/动画、Hold/键盘/拖动交互全部写在 `BarRenderLoopState` + `barUISet.barState.drawAttributeBar` 全局状态与 `shapeMap` 中，并直接写画笔颜色（`SetInteractionPenColor`），编辑器需要的是 `ShapeUiAction::SetStyle`。编辑器当前已复用 `ColorPalette` + `GetBarColorPickerColor`（ShapeEditor.cpp:624,646），但没有标记点、预览气泡、Hold、RGB 数值。

### 2.5 共享画刷注意

`ContentSolidBrush`→`GetFrameSolidColorBrush`（Rendering.cppm:98-99, 353）返回帧内复用的可变画刷，下一次调用会原地改色（spec rendering-and-ui.md「复合视觉切换」与 shapes.md P3 审查）。ShapeEditor.cpp:566-568 已按"先消费再取下一色"写法。

---

## 3. 主题、浅色与 WinUI3 Thumb

### 3.1 主题角色

`Bar/Bar.Theme.cppm:22-38` `BarThemeColorEnum`：Surface, SurfaceFrame, TextPrimary, Accent, PressedFill, SubtleFill, SwatchFrame, DockTarget, IconPrimary, SelectedFill, Divider, EdgeLight, ShadowKey, ShadowAmbient, TopHighlight。

- Dark 值：:96-118（Surface RGB(24,24,24)；SurfaceFrame/Divider/SwatchFrame/EdgeLight = 255 白；Accent/SelectedFill `RGB(88,255,236)`；PressedFill/SubtleFill 127 灰）。
- Light 值：`BarThemeMaterial::LightColor`（Bar.ThemeMaterial.h:41 起）；spec `bar-theme-material.md` §3：Surface `#FBFBFB`、IconPrimary `#2B2B2B`、TextPrimary `#1A1A1A`、Accent `#006A63`、SurfaceFrame `#8B8B8B`、Divider `#A7A7A7`、PressedFill `#BCBCBC`、SubtleFill `#E1E1E1`、SwatchFrame 黑。
- 入口：`GetThemeColor(role)`（:135-141）按当前 `darkStyle`；`GetThemeColor(BarThemeModeEnum, role)` 取指定端点；`GetThemeLightWeight()`（:186-190，0/1）；`GetThemeSurfaceFrameOpacity`（:179-184）；`GetThemeDisabledContentOpacity`（:192-196）。
- 预设色 `GetPresetColor`（:198-217），11 色。

### 3.2 材质系统（连续深浅）

- `BarUiShapeClass::lightMaterial / themeSurface / lightFillOpacityScale / residentSurface`（Bar.UI.cppm:255-260）。
- 主栏：初始化把 themeSurface 面板 fill/frame 设为 **Dark 端点**，由 renderer `BarThemeMaterial::Resolve(lightMaterial, fill, frame)` 解析（Initialization.cpp:1296-1323；Rendering.cpp:2833-2835, 2866-2885）。每帧 `state.barLightMaterial.val` 写入所有 shapeMap 形状和按钮（RenderLoop.cpp:4790-4800, 5217）。
- 主栏外的表面（PageControl `Bar.Scene.cpp:699-705`）用 `GetThemeLightWeight()` 每帧写 `lightMaterial`；GeometryAttribute 面板 surface_ 直接 `SetDirect(GetThemeColor(Surface))` 且不写 lightMaterial（GeometryAttribute.cpp:579-583）。
- ShapeEditor：所有形状 `fill->SetTar(GetThemeColor(Surface))` 每帧（:511-515）、手柄 `Surface/Accent`（:535），**不设置 lightMaterial（=0）与 themeSurface** → 浅色时颜色能切到浅色角色值，但边缘光/投影按 Dark 端点规则解析，无 Light ambient 投影。离屏测试覆盖了 dark/light 两端（ShapeEditor.cpp:777）。

### 3.3 已有 WinUI3 Slider Thumb（可作控制点/旋转按钮样式依据）

主栏绘制属性粗细滑块 Thumb，绘制代码 `Bar.RenderLoop.cpp:11845-11921`（内联 lambda，属 BarUISetClass 渲染函数）：

- 几何：外径 `BarThicknessSliderThumbDiameter=20`（Layout.cppm:38）×面板缩放×`thumbScale`；外圈描边 = 外半径；外圈填充 = 外半径 − 1DIP；中心圆直径 `BarThicknessSliderThumbCenterDiameter=12`，悬停 14，按下 10（Bar.Main.cpp:125-127），且 ≤ 外半径×0.70。
- 颜色（:11885-11906）：
  - 中心 = `MixBarUiColor(surface, Accent, accentOpacity)`，accentOpacity 正常 1.0 / 悬停 0.90 / 按下 0.80（RenderLoop.cpp:2343-2369）。
  - 外圈填充：Dark = `Mix(Dark Surface, Dark TextPrimary, 0.20)`；Light = 纯白（ControlSolidFillColorDefault）。
  - 外圈描边：Dark = `Mix(darkOuterFill, Dark TextPrimary, 0.12)`；Light = `Mix(Light Surface, Light TextPrimary, LightSliderThumbBorderOpacity=0.12)`（ThemeMaterial.h:159-160）。
  - 两端按 `state.barLightMaterial.val` 连续混合。
- 动画：中心直径/accent 强度 `BarThicknessSliderPressAnimationDur=0.12`、EaseOutCubic（Bar.Main.cpp:129；RenderLoop.cpp:2341-2369）；"外圈不缩放，只让强调色强度和中心圆尺寸平滑切换"（:2360 注释）；整体出现 `ThumbOpacity/ThumbScale(0.75→1)` 0.28s（:2319-2326, Main.cpp:128）。
- 复用性：逻辑是 RenderLoop 内联 lambda + `BarRenderLoopState` 成员（`drawAttributeThicknessSliderAccentOpacity/CenterDiameter`），**未抽成独立函数**；但只依赖 `GetFrameSolidColorBrush/FillEllipse`、`GetThemeColor(mode, role)`、`MixBarUiColor`、`BarThemeMaterial` 常量，迁移成纯绘制 helper 所需输入仅为 center、外径、中心径、accentOpacity、lightWeight、opacity。
- 设置窗口 ImFluent 对照（不可在 D2D 直接复用）：`additional/imfluent/imfluent.cpp:3418-3430`，`SliderThumbRadius=9`、`SliderThumbInnerRadius=6`（:349-350），悬停 +SpacingXSmall、按下 −SpacingXSmall，颜色 ControlSolidFillDefault + ControlStrokeDefault 描边 + AccentFillDefault 内点。

---

## 4. 主栏 GeometryAttribute 属性 list（Bar/Bar.GeometryAttribute.cpp）

- 项序 `AttributeItems`（:29-30）：`Style×3, -1(分隔), Pattern×4, -2(分隔), Thickness×3`；**无 Color 入口**（主栏图形颜色走绘制属性/笔色）。
- 初始化（:180-246）：
  - 面板 surface_：r=8、`BarDrawAttributeSurfaceOpacity=0.95`、framePct/frameLightPct 0.18、PointLight、PenWhenDrawing。
  - 按钮：`size oneOne`（类别 twoTwo），背景 `PressedFill`/`SurfaceFrame`，r=4，framePct/frameLightPct 0，`framePrimaryLightEnabled=false`，`frameCursorLightIntensityScale=BarButtonCursorLightIntensity`，图标 20×20（:189-209）。
  - 分隔项：`preset=Divider, size=oneTwo`，`1 × (OneSide-4)`、r=`BarUiDividerRadius`、`SurfaceFrame`、pct 0.30、frameLightPct 1、`BarUiDividerCursorLightIntensity`（:225-234）。
  - `attributeList_.Replace(...)` + `ResolveBarButtonListLayout(..., SingleRow)`（:237-239），`attributeMaxWidth_` 为最大宽。
- 推进（:431-453, 560-576）：
  - 可见性改变（showStyle/showStroke）时才更新 hide 并重新 `ResolveBarButtonListLayout`（:431-438）。
  - 每项 `attributeX_[index].SetTar(placement.x, BarUiDefaultOperationDur)`；首次 `SetDirect`（:442-446）。隐藏项保留 list 中的打包坐标（SingleRow 中不可见成员不前进 x，`p.x` 即落在前一可见项后，Button.cpp:2603-2610），由 pct 淡出。
  - 按钮显隐 `RetargetBarButtonInteractionVisual(b, wanted && pf.visible, available, selected, BarButtonHoverTransitionDuration)`（:511）；首次打开直接 SetDirect 透明度（:512-517）；退场按钮继续推进直到透明（:537-543 "退出中的控件继续按同一帧时钟推进"）。
  - 分隔项 pct/frameLightPct `SetTar(wanted?0.30/1:0, 0.24)`，位置取 `attributeX_[member]` 居中于行（:560-576）。
  - 输入门禁：位置/透明度稳定后才可点（:541-559）。
  - 整体姿态 `pf.pose.Apply` 统一缩放（回弹入场）。
- 绘制（:609-685）：`PushPanelOpacity` + `DrawBarBackgroundVisual(surface_)` + clip；按 `AttributeItems` 顺序 `DrawBarButtonVisual`，分隔项也走 `DrawBarButtonVisual`（:666-675）；粗细项自定义 -45° 胶囊（:626-647）；焦点框 TextPrimary 0.8 圆角 3（:649-654）。
- 列表布局函数：`ResolveBarButtonListLayout`（Button.cpp:2493-2648），种类 `GeometryGrid / GroupedGrid / SingleRow / Main`；SingleRow 分隔项宽 `BarButtonFrameThicknessDip`、高 `one-4`、垂直居中于 `gap+one/2`。

---

## 5. 常驻透明度（Resident opacity）

### 5.1 数据流

- 设置页：`Setting.cpp:3706` → `Bar::SetResidentOpacity(backgroundPercent, contentPercent)`（Bar.Main.cpp:355-366）：写 `barStyle.requestedBackgroundOpacity/requestedContentOpacity` 原子量，`UpdateRendering(false)` 并 `RenderPipeline::Request(PptPageMask())`。**未唤醒 `Client::SelectionOverlay`**。
- 主栏渲染线程帧首落地 requested → applied（RenderLoop.cpp:1581-1582 起），`currentBarThemeStyle->backgroundOpacity/contentOpacity` 为已应用值。
- 读取：
  - `GetThemeResidentBackgroundOpacityOverride()`（Theme.cppm:149-153）：绝对背景透明度，0=跟随主题。
  - `GetThemeResidentContentOpacity()`（:165-169）：内容倍率，默认 1。
  - `GetThemeResidentSurfaceOpacity(darkOpacity)`（:172-177）：`darkOpacity × ResidentFillOpacityScale(lightWeight, LightResidentSurfaceOpacityScale, override)`。
  - `GetThemeDefaultResidentBackgroundOpacity`（:156-160）：Dark 0.80 / Light 0.92。

### 5.2 主栏如何应用

- 表面：`residentSurface=true` 的形状（MainBar、MainButton、底部吸附指示；Initialization.cpp:1314-1321, RenderLoop.cpp:11958-11961）在 renderer 中 `fillPct` 由 `ResidentFillOpacityScale(..., GetThemeResidentBackgroundOpacityOverride())` 解析，并经 `ResolveIdleOpacity` 叠加 PPT idle 淡化（Rendering.cpp:2866-2885；超椭圆 :3045）。pct 0↔0.8 仍作为显隐可见度乘子。
- 内容：`BarUIRendering::SetResidentContentOpacity(GetThemeResidentContentOpacity())` 只在绘制常驻按钮段打开，绘制后恢复 1（RenderLoop.cpp:10950-10964 主栏按钮；:11051-11053 主按钮 Logo；Rendering.cppm:180-183）。renderer 在 SVG/Word 绘制时乘该倍率（Rendering.cpp:3131, 3245, 3319）。
- 弹出面板（属性栏、菜单、提示、取色器）**不使用**常驻透明度（Theme.cppm 注释 :163-164, :170；spec bar-theme-material.md "弹出面板不使用"）。

### 5.3 非主栏客户端的做法（PageControl，Bar.Scene.cpp）

- 初始化 root 底板 `GetThemeResidentSurfaceOpacity(BarMainBarFillOpacity)` + `root.residentSurface=true`，菜单/主题浮层用 0.95（:210-215）。
- 每帧：主题表面色变化时重设 `root.pct.SetTar(GetThemeResidentSurfaceOpacity(...))`（:689）；`lightMaterial = GetThemeLightWeight()`（:699-705）；比较 `appliedResidentFillOpacity/ContentOpacity`，变化则 `root.pct.SetTar(residentFill)` 并 `FullDamage()`（:706-714）。
- 绘制分页按钮时 `SetResidentContentOpacity(GetThemeResidentContentOpacity())`，结束恢复 1（:1137-1141）。

### 5.4 编辑器/浮条/构造提示当前状态

- ShapeEditor：浮条/参数/颜色/旋转底板 pct 固定 `BarMainBarFillOpacity=0.8`（ShapeEditor.cpp:248-251），无 `residentSurface`、不调用 `SetResidentContentOpacity`、未比较已应用透明度；`SetResidentOpacity` 也不唤醒 SelectionOverlay 客户端。
- 构造提示 `Bar.ConstructionPrompt.cpp`：surface_ `pct = BarMainBarFillOpacity`（:38-39），每帧仅 `fill/frame SetTar(GetThemeColor(Surface/SurfaceFrame))`（:89），绘制 `PushPanelOpacity(frame_.opacity)`（:115）；同样未接常驻透明度。它在主栏渲染函数内绘制（RenderLoop.cpp:11923 `owner_.constructionPrompt.Draw`），故主栏帧会随透明度设置重绘，但其本身 pct 不变。
- 若要遵循：需要按"常驻 vs 弹出"归类决定。若归为常驻表面，需要 (a) 底板 `residentSurface=true`（若要材质解析还需 themeSurface+Dark 端点+lightMaterial）或直接 `pct=GetThemeResidentSurfaceOpacity(BarMainBarFillOpacity)`；(b) 常驻内容段包裹 `SetResidentContentOpacity`；(c) 记忆已应用值并在变化时整幅重画；(d) 编辑器在另一客户端，`SetResidentOpacity`/主题变化需额外 `RenderPipeline::Request(Client::SelectionOverlay)`。若归为弹出面板，则按现有合同不使用常驻透明度，弹出面板标准 pct 为 0.95。

---

## 6. 复用映射（组件 → 现有实现 → 缺口）

| 编辑器组件 | 现有实现 | 缺口（事实） |
|---|---|---|
| 控制点（外圈+内点） | 粗细滑块 Thumb：RenderLoop.cpp:11845-11921，尺寸 Main.cpp:125-129，Light 外圈 ThemeMaterial.h:159-160；ImFluent 参照 imfluent.cpp:3418-3430 | 现为内联 lambda+RenderLoopState 动画值，无独立 helper；编辑器手柄现为 12DIP Surface 圆+Accent 1DIP 边（ShapeEditor.cpp:529-537），无内点、无 hover 中心径变化 |
| 旋转按钮 | 同上 Thumb 风格 / 现 `BarButtonClass`+`rotateSurface`（ShapeEditor.cpp:251,274,516-525,549） | 两种风格并存；手柄距离 40*scale 由共享核心决定（shape_interaction.inl:139-145） |
| 对象外框 | 无现成"选择框"组件；`BarUiShapeClass` 支持圆角+`ft` 描边（可画轴对齐圆角框）；旋转对象需变换后绘制 | 现为 4 条 1DIP DrawLine、Accent .70、紧贴 localBounds（ShapeEditor.cpp:594-603），无间距、无圆角 |
| 浮条背景/入场 | `BarAttributePanelMotion` + `ResolveEraserSurfacePose` + `FitEraserSurfacePose`（EraserAttributeMotion.cppm），GeometryAttribute.cpp:337-380 用法 | 编辑器只有 opacity 淡入（:352）；无锚点、无回弹 |
| 单行属性 list | GeometryAttribute `attributeList_` + `ResolveBarButtonListLayout(SingleRow)` + `attributeX_` 定时长（GeometryAttribute.cpp:221-239, 431-453, 560-576） | 编辑器手写 x/y 速度型 SetTar、首帧不完整 SetDirect、跨会话 initialized（§1.10）；手动换行 |
| 分隔线 | 主栏 Divider（Button.cpp:225-250, RenderLoop.cpp:2716-2733）/ Geometry list 分隔项（GeometryAttribute.cpp:225-234, 560-576） | 编辑器颜色、光源参数、位置动画均不同（§1.8 表） |
| 按钮显隐/伸缩 | 主栏批次 `mainBarTimeline`+`CanJoin`+`AnchorHiddenBarButton`（RenderLoop.cpp:915-951, 2600-2830）；Geometry list 的 pct 淡出 + 固定 0.4s 位置 | `AnchorHiddenBarButton` 绑定 `BarButtonSetClass/preset`，不可直接对编辑器 list 调用；时间轴类 `BarUiTimelineClass` 可复用 |
| 颜色入口（圆角矩形） | 主栏 ColorSelect 30×30 r=4 + `colorSelect` 勾（Initialization.cpp:444-650） | 编辑器为圆形 FillEllipse（ShapeEditor.cpp:563-569）；主栏色块是 shapeMap 固定对象，需要在编辑器内重建同参数 Shape |
| 颜色层（同窗新层） | 主栏绘制属性色块区 + 取色器同窗顶层（RenderLoop.cpp:6329-6420, 11515-）；`ColorPalette`、`GetBarColorPickerColor`、`ProjectBarColorPickerColor`、`IsBarPresetColor`、`colorCustom` PNG | 主栏实现绑定全局 barState/画笔色；编辑器现为浮条下方扩展区，非层；无标记点/预览/Hold/RGB |
| 取色器上层 | 同上 ColorPickerPanel（300×209，EaseOutBack 0.4s，从入口锚点紧凑态展开） | 主栏取色器**不夹紧屏幕**（RenderLoop.cpp:6361）；夹紧参考 `ClampPopupCenter`（:9122）与 `FitEraserSurfacePose` |
| 屏幕适配 | 编辑器已有 workArea 夹紧/翻转/滚动（ShapeEditor.cpp:399-410, 394-397）；Geometry 面板 fit+换边（GeometryAttribute.cpp:315-380） | 编辑器窗口 bounds 每帧随内容变；多层弹出需各自纳入 bounds |
| 浅色主题 | `GetThemeColor`、`GetThemeLightWeight`、`BarThemeMaterial`；PageControl 每帧写 lightMaterial 的模式（Scene.cpp:699-705） | 编辑器形状 lightMaterial=0、无 themeSurface（无 Light 投影/边缘光规则） |
| 常驻透明度 | §5 | 编辑器/构造提示未接入；SelectionOverlay 未被 `SetResidentOpacity` 唤醒 |

## Related Specs

- `.trellis/spec/native-desktop/shapes.md` — P3+P4 编辑合同（12DIP 点、32.5DIP 旋转、44DIP 热区、浮条复用标准按钮/SingleRow/既有调色内容、ContentSolidBrush 规则）。
- `.trellis/spec/native-desktop/rendering-and-ui.md` — 公共按钮/固定面板、批次 CanJoin、复合面板收起关键帧、按钮悬停/按压衔接、边框光影约定。
- `.trellis/spec/native-desktop/bar-theme-material.md` — 深浅角色值、材质权重、Slider Thumb WinUI3 端点、常驻表面规则。
- `.trellis/spec/native-desktop/configuration-i18n-and-assets.md#ui3-常驻透明度配置合同` — 常驻透明度配置（未在本次展开阅读）。

## Caveats / Not Found

- 未找到独立可调用的"WinUI3 thumb"绘制函数；唯一 D2D 实现是 RenderLoop 内联代码。
- 未找到主栏现成的"图形选择框/控制柄"组件；Draw3 选择态辅助表面未在本次调研范围。
- 主栏取色器内 RGB/不透明度数值、Hold 提示的完整绘制段（RenderLoop.cpp 11515 之后）只做了定位，未逐行记录。
- 行号可能随后续提交漂移；`Bar.RenderLoop.cpp` 为 13k 行单文件。
