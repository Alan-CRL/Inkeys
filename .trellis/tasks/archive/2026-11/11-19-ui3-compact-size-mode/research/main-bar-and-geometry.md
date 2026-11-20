# 调研：主栏按钮布局 / 主栏几何与吸附 / 悬停提示（小尺寸 UI 模式）

> 范围：只读调研，未改任何源码。行号基于当前 `refactoring/drawpad-ui` 工作树（HEAD `2f2adf9b`）。
> 结论中凡是无法从代码直接确定的，均标注"待确认"。

---

## 一、主栏按钮布局与尺寸计算

### 1.1 现状机制

**尺寸单一来源（UI3 网格）**

- `UI3.Metrics.cppm:11-14`：`ButtonGapDip=5`、`ButtonOneSideDip=32.5`、`ButtonTwoSideDip=32.5*2+5=70`。主栏、分页、More、属性面板都从这里取格边长。
- `Bar.Metrics.cppm:31-41`：`BarMainBarWidthDip=80`、`BarMainBarHeightDip=80`、`BarMainButtonWidthDip/HeightDip=80`、`BarMainButtonToMainBarGapDip=10`、圆角 8、填充 0.8、边框 0.18。
- `Bar.Metrics.cppm:42-69`：默认展开宽度 `5 + 5*75 = 380`、启动预览总宽 `80+10+380 = 470`，带 `static_assert`，供 StartupPreview 在 Bar 未初始化前使用。

**按钮尺寸枚举与存储（预设 vs 运行时有效尺寸）**

- `UI3.Button.cppm:22-28`：`ButtonSizeEnum { twoTwo, twoOne, oneTwo(仅分割线), oneOne }`。没有 4×1 枚举；`StandardFourOne` 只是白板操作行的视觉档位（`UI3.Metrics.cppm:108-118`，140 DIP），不进入主栏列表。
- `UI3.Button.cppm:106`：`ButtonClass::size` 是**运行时有效尺寸**。
- `Bar.Button.cppm:77`：`BarButtonClass::baseSize` 是**基准（预设）尺寸**；注释明确"基准元数据由注册/Load 更新；运行时有效值只由统一主栏投影提交"。
- `Bar.Button.cppm:113`：注册表 `BarButtonRegistrationClass::defaultSize`（注册时写死）。
- 配置层：`Other.Config.cppm:89-110` `BarButtonSizeKind` + `BarFixedButtonLayoutEntry::Size`；默认 A1 全 `TwoTwo`、A2 为 Whiteboard/Freeze `TwoOne` + EndShow `TwoTwo`（`Other.Config.cppm:244-270`）；`DefaultSizeForBarButtonId`（`:174-180`）。
- **统一投影点**：`Bar.Button.cpp:1023-1091` 的 `apply` lambda 从 `baseSize` 出发，按场景改写 Whiteboard/Freeze/Pierce/Recall/Redo 的 2×2↔2×1（A2 投影、撤回/重做展开），最后在 `Bar.Button.cpp:1087` 写入 `button->size = size;`。之后所有布局/绘制只读 `size`。

**列表组装与分组（A1 / B(More) / A2 / 分割线）**

- `Bar.Button.cpp:1571-1660` `Load()`：`A1 固定区` → 运行时 Redo 插在 Recall 前（两个 2×1 叠成一格）→ `AppendBoundaryDivider(0)`（A1|B）→ 旧扩展按钮最多 2 个 + `moreButton` → `AppendBoundaryDivider(1)`（B|A2）→ `A2 固定区`；CanvasDebug 插在 Clean 后，Pierce 依附 Freeze。超过 2 个的扩展按钮和 Setting 进入 More 快照（`forcedOverflow` / `explicitMore`）。
- 分割线为 `oneTwo`，`BarButtonPresetEnum::Divider`。

**布局计算（真正的"CalculateButtonLayoutWidth"）**

- 代码里已没有名为 `CalculateButtonLayoutWidth` 的函数（只在 `Bar.Metrics.cppm:52` 注释中残留）。实际入口为 `Inkeys::UI::Bar::ResolveBarButtonListLayout`（声明 `Bar.Main.cppm:302-320`，实现 `Bar.Button.cpp:2027-2120`），四种 kind：`Main / GroupedGrid(More) / SingleRow(构造提示、形状属性行) / GeometryGrid(形状类别)`。
  - `Bar.Button.cpp:2033`：`spec.height = BarMainBarHeightDip;`（SingleRow/PairedRows 结果高度直接取 80）。
  - `Bar.Button.cpp:2043-2051`：**`size` → `columnSpan/rowSpan` 与宽高的映射**（oneOne 1×1、twoOne 2×1、oneTwo 1×2、默认 2×2）。这是把 2×2/2×1 映射成 1×1 的天然位置之一。
  - `Bar.Button.cpp:2069-2075`：Main 中的分割线转为 `columnBoundary`（宽 1、高 50，不占格，只重置游标）。
  - `Bar.Button.cpp:2061-2067`：SingleRow 中分割线为宽 1、高 `32.5-4`、`centerHeight=32.5` 的居中竖线（占 1+5 宽）。
  - `Bar.Button.cpp:2087-2099`：More 的 GroupedGrid 列数 `2*min(5, ceil(sqrt(count)))`（单位为 1×1 子列），第一组 `endRowAlignment=2`（按 2×2 成对对齐）。
  - `Bar.Button.cpp:2100-2103`：Main 使用 `GroupedGrid + PairedRows`。
- `UI3.ListLayout.cpp:97-147` PairedRows（双行流）：x、y 游标从 `gap` 起步；1×1 先上后下两行排一列，2×1 上下叠，2×2 占满两行；返回的 `placement.x/y` 是**按钮中心**（相对主栏左上），`layout.width` 即主栏宽度（含左侧 5 DIP 起步、每格后 +5）。SingleRow（`:107-112`）单行流：`p.y = gap + centerHeight/2`，**不读 `spec.height`**。
- `Bar.RenderLoop.cpp:2792-2794`：主栏取 `ResolveBarButtonListLayout(frame.mainButtons, Main)`，`layoutTotalWidth = mainListLayout.width`。
- `Bar.RenderLoop.cpp:3626-3707`：遍历 placement：`button.x/y` 目标 = placement 中心（x 按图标排序策略镜像）；`Bar.RenderLoop.cpp:3677-3685` 按 `temp->size` 选 `ButtonVisualLayoutKind`（oneOne→StandardOneOne、twoOne→StandardTwoOne、其他→StandardTwoTwo），More/形状模式下 Geometry 的 2×2 用 34 DIP 大图标；`:3697` 1×1 时文字 `name.pct→0`。
- More 面板：`Bar.RenderLoop.cpp:5347-5540` 用 GroupedGrid 结果，`:5512-5537` 同样按 `size` 选 kind，1×1 隐藏文字。

**主栏宽高的派生**

- 宽：`Bar.RenderLoop.cpp:3914` `mainBar->w.SetTar(totalWidth)`；x：`:3917-3928` `±(mainButton.GetW()/2 + mainBar.w.tar/2 + 10.0)`（10 为硬编码间隙，应为 `BarMainButtonToMainBarGapDip`）。
- 高：`Bar.Initialization.cpp:377-379` 用 `BarMainBarWidthDip, BarMainBarHeightDip` 构造 `MainBar` 形状后，**运行时从未再设置 `mainBar->h`**（全仓 grep 无 `mainBar->h.SetTar`）；`h.mod=Variable` 已就绪（`:388-389`）。
- 主栏通过 `mainBar->Inherit(UiInheritEnum::Center, *mainButton)`（`Bar.RenderLoop.cpp:6152/6168/7551/10330`）与主按钮**垂直中心对齐**；按钮通过 `button.Inherit(CenterFromTopLeft, *mainBar)`（`Bar.RenderLoop.cpp:1820`）。
- 绘制属性面板竖向位置已读主栏实际高：`Bar.RenderLoop.cpp:4020-4022` `±(MainBar.GetH()/2 + panel.GetH()/2 + 10)`。橡皮/撤回属性面板高度直接取主栏高：`Bar.EraserAttributeLayout.cppm:116`、`Bar.RecallAttributeLayout.cppm:71`（但内部按钮仍是 70 DIP，见风险）。

**80 DIP 硬编码 / 派生清单（主栏 + 主按钮）**

| 位置 | 内容 | 性质 |
|---|---|---|
| `Bar.Metrics.cppm:31-41` | 主栏/主按钮 80、间隙 10 | 常量来源 |
| `Bar.RenderLoop.cpp:933` | `static constexpr double mainButtonBaseSize = 80.0;`（单值，宽高共用） | **需改为运行时宽/高** |
| `Bar.RenderLoop.cpp:2680-2706` | 主按钮 w/h 目标 = `mainButtonBaseSize`，点击脉冲 ×1.05 | 派生 |
| `Bar.RenderLoop.cpp:2805` | 展开方向判定 `mainHalfWidth = base/2`、`gapDip = 10.0` 字面量 | 硬编码 10 |
| `Bar.RenderLoop.cpp:3649-3650` | 折叠时按钮收拢到 `(40, 40)`；换边关键帧中点 `40.0`（`:3654-3655`） | **硬编码 80/2** |
| `Bar.RenderLoop.cpp:3902/3910/3913` | 折叠 `mainBar->w→80`、换边中点宽 80 | **硬编码** |
| `Bar.RenderLoop.cpp:3919/3921` | 主栏 x 偏移 `+10.0` | 硬编码 10 |
| `Bar.RenderLoop.cpp:10295-10299` | 窗口横向容量 `max(80, …) + base/2 + 10` | 硬编码 |
| `Bar.Initialization.cpp:325-326, 358, 370` | 兼容默认位置 `-80-50`、主按钮超椭圆 80×80、Logo 高 80 | 硬编码 |
| `Bar.Zoom.cppm:98-99` | 同上默认位置 | 硬编码 |
| `Bar.StatusIndicatorModel.cppm:934` | 状态指示器模型默认 `mainWidth=80, mainHeight=80` | 默认值（运行时会被覆盖，待确认） |
| `Bar.Interaction.cpp:9335` | 拖动 `bodyHeightDip = 80.0` | **硬编码，见第二节** |
| `Bar.RenderLoop.cpp:7280-7282, 7373, 7547` | 底栏竖向映射基准高 = `mainButtonBaseSize` | 派生 |
| `StartupPreview.State.cppm:29` | `DefaultStartupBarHeightDip = BarMainBarHeightDip` | 派生（启动预览） |
| `Display.cppm:241-247` | `ResolveUiScaleBase(..., baseHeightDip = 80.0)` 任务栏基准 | **耦合，见风险** |

**ButtonVisualLayoutKind 与"仅图标"版本**

- `UI3.Metrics.cppm:36-46` 定义 `Custom / StandardOneOne / StandardTwoOne / StandardFourOne / StandardTwoTwo / PageHorizontal / PageVertical / PageTwoTwo`；`ResolveButtonVisualMetrics`（`:87-166`）给出按钮宽高、图标尺寸/偏移、主副文字槽。
- `StandardOneOne`（`:93-97`）：32.5×32.5、图标 20、**无文字槽**——即现成的"仅图标 1×1"版本；配合 `Bar.RenderLoop.cpp:3697` / `:5537` 把 `name.pct` 归零，文字内容（`name.content`）仍保留，可供悬停提示读取。
- `StandardTwoOne`：图标 18 左偏 -21，文字 12 号 37 宽；`StandardTwoTwo`：图标 28 上偏 -10，文字 13 号下偏 20。
- 各属性面板（橡皮/撤回/形状）自己调用 `ResolveButtonVisualMetrics(StandardTwoTwo)`（`Bar.EraserAttribute.cpp:106/304`、`Bar.RecallAttribute.cpp:82/258`、`Bar.GeometryAttribute.cpp:423`），不经主栏投影。

**主按钮（MainButton）**

- 形状：`Bar.Initialization.cpp:358` `UiSuperellipseClass(mainX, mainY, 80, 80, n=3, ft=1)`，x/y 为**中心**；Logo SVG 高 80（`:370`），Logo 基准宽高在 `Bar.RenderLoop.cpp:935-936` 首帧锁存。
- 收起/展开：`Bar.RenderLoop.cpp:2708-2725` 折叠时超椭圆指数 `n→3`、填充 0.6；展开 `n→10`、填充 0.8；点击脉冲 `:2688-2699`（`mainButtonScale=1.05`，`EaseOutBack/EaseInBack` 关键帧）；底栏锁定时直接 `SetDirect` 防止脉冲越过 dock 线（`:2680-2687`）。
- 主栏收展批次：`Bar.RenderLoop.cpp:3535-3598` `mainBarTimeline`，折叠/换边/布局变化重启批次（展开 EaseOutBack，收起 EaseOutCubic/EaseInBack）。

**全局"尺寸模式"可注入点（现状无，但已有可复用模式）**

- 现有主题/透明度注入模式完全可复制：
  1. 配置：`Other.Config.cppm:590-591` `Experimental.Inkeys3.UI3` 组（`ThemeMode/BackgroundOpacity/ContentOpacity`）→ 新增 `SizeMode`。
  2. 设置页：`Setting.cpp:3487-3499` Appearance 区 `events.themeModeChanged` → 写配置 → `Inkeys::UI::Bar::SetThemeMode()` → `QueueConfigWrite()`。
  3. Bar 侧发布：`Bar.Main.cpp:498-505` `SetThemeMode` 只写 `barStyle.requestedDarkStyle` 原子并 `UpdateRendering`；`Bar.State.cppm:113-127` `BarStyleClass` 持有 `requested*`。
  4. 渲染线程帧首应用：`Bar.RenderLoop.cpp:2549-2600` `SubmitTargetsAndLayout` 开头比较 requested 与 applied，ForceFullDamage + `RequireVisualRetry` + 唤醒分页（`RenderPipeline::Request(PptPageMask())`）。
- 因此推荐：`BarStyleClass::requestedSizeMode`（atomic）+ applied `sizeMode`，在 `SubmitTargetsAndLayout` 帧首落地；所有布局读 applied 值（渲染线程单一所有者）。

### 1.2 关键代码位置速查

- 预设/有效尺寸：`Bar.Button.cppm:77`、`UI3.Button.cppm:106`、`Bar.Button.cpp:1087`
- 列表装箱：`Bar.Button.cpp:2027-2120`、`UI3.ListLayout.cpp:97-147`
- 主栏消费：`Bar.RenderLoop.cpp:2792-2794`、`:3626-3707`、`:3897-3934`
- More 面板：`Bar.RenderLoop.cpp:5347-5540`、`Bar.Main.cpp:346-354`（`BarMorePanelGap/Padding/CompactWidth`）
- 主按钮：`Bar.RenderLoop.cpp:932-936`、`:2676-2725`；`Bar.Initialization.cpp:356-372`

### 1.3 小尺寸改造切入点（最小侵入建议）

1. **尺寸映射放在统一投影 `Bar.Button.cpp:1087`**（推荐）：`if (compact && size != oneTwo) size = oneOne;`。
   - 优点：`baseSize`/配置/注册 `defaultSize` 一律不动（满足"预设尺寸不改"）；`MainLayoutSnapshot()`（`Bar.Button.cpp:996`）读 `size`，切换模式会自然判定 `buttonLayoutChanged` 并重启主栏布局批次（`Bar.RenderLoop.cpp:3539-3561`），按钮宽高/图标/文字随 `ConfigureButtonVisualTargets` 走 0.4 s 过渡——**热切换动画基本免费**；RenderLoop `:3677` 与 More `:5512` 的 kind 选择、`:3697/:5537` 的隐藏文字自动生效，More 面板也随之变 1×1（符合"更多面板按钮改 1×1"）。
   - 未来"2×1 副本"尺寸：在 `ButtonSizeEnum` 新增如 `twoOneBoth`，投影时保留为 twoOne 即可；本任务不实现。
   - 备选：只在 `ResolveBarButtonListLayout:2043-2051` 映射——但 RenderLoop `:3677`、`:5512` 还会按旧 `size` 选 2×2 视觉，需三处同步，不推荐。
2. **主栏列表 kind**：Main 在 compact 时改用 `ListLayoutKind::SingleRow`（`Bar.Button.cpp:2100-2103` 分支），分割线走 SingleRow 已有的居中竖线分支（`:2061-2067`）而非 `columnBoundary`。
   - SingleRow 的 `p.y = gap + h/2` 固定按 5 DIP 上边距居中（`UI3.ListLayout.cpp:109`），35 DIP 高需要 `y=17.5`：建议给 `ListLayoutSpec` 增加可选 `rowCenterY`（默认空，保持现有 SingleRow 调用者——构造提示、形状属性——行为不变），compact 主栏传 `35/2`。不要改 `spec.height` 语义（现有 SingleRow 调用方都传 80 但不消费）。
   - More 的 GroupedGrid 列数公式（`Bar.Button.cpp:2092`）在全 1×1 时偏保守（例如 4 个按钮只排 4 子列=2 列宽度），compact 需独立列数（如 `min(10, ceil(sqrt(count)*?))`）且 `endRowAlignment=1`。具体列数待确认（见问题）。
3. **主栏高度**：在 `Bar.RenderLoop.cpp:3897-3934` 主栏块中加 `mainBar->h.SetTar(sizeMode 高度, operationDur, …syncedValueCurve)`，与 w 同批次推进。`Bar.Metrics.cppm` 增加 `BarCompactMainBarHeightDip = 35` 等常量（以及 compact 主按钮宽高）。
4. **主按钮缩小**：把 `mainButtonBaseSize`（`Bar.RenderLoop.cpp:933`）改为运行时 `mainButtonBaseWidth/Height`（状态指示器还要"主按钮变宽"，宽高必须分离），Logo 基准尺寸同步按比例（`:935-936`）。
5. **替换硬编码**：`:3649-3655` 折叠锚点 `(40,40)` → `(mainButtonW/2, mainBarH/2)`（折叠时主栏宽=主按钮宽，详见第 6 点）；`:3902/:3910/:3913` 的 `80.0` → 当前主按钮宽；`:2805/:3919/:3921/:10298` 的 `10.0` → `BarMainButtonToMainBarGapDip`；`:10295` `max(80, …)` → 主按钮宽。
6. 折叠形态：`:3901-3902` 折叠时主栏 `x→0, w→80`（收在主按钮下方、全透明）；compact 下改为主按钮宽高，避免收起动画从 80 宽跳变。
7. **启动预览**：`Bar.Metrics.cppm:42-69` 的默认宽、`StartupPreview.State.cppm:29` 高度需要 compact 版本（预览在 Bar 初始化前运行，需从配置读 SizeMode），属于 startup-preview spec 范围，建议单列子项。

---

## 二、主栏位置、拖动、居中与吸附

### 2.1 现状机制

**坐标体系**

- 主按钮 `x/y` 为中心，单位 DIP（相对显示器原点 `state.monitorOrigin`，再乘 `zoom` 得像素）。`displayCenterX/Y`（像素、相对显示器）是根位置的权威值，每帧 `mainButton->x/y.SetDirect(displayCenter/zoom)`（`Bar.RenderLoop.cpp:2526-2528`）。
- 主栏垂直中心=主按钮中心（Center 继承）；水平在主按钮左/右 `GetW()/2 + 10 + w/2`。
- HWND 是按内容动态预约的容量窗口：横向 `Bar.RenderLoop.cpp:10294-10301`（主栏最大宽（Back 曲线额外 ×1.06）+ 主按钮半宽 + 10 + 属性面板半宽 + 24，再 ×2），纵向按可见/在途内容预约（`:10302`起）；`WindowGeometry.cppm` 负责容量/视口决策与像素换算。

**底栏（竖向）吸附**

- 常量：`Bar.BottomDock.cppm:20-31`：竖向捕获 `BarBottomDockThresholdDip=20`；水平居中捕获 `BarBottomDockCenterThresholdDip=40`；竖向视觉保护（果冻上限）`BarBottomDockVisualLimitDip=24`；弹簧 ω=18、ζ=0.72；底栏提示高 30。
- dock 线：`ResolveBarBottomDockLine`（`:458-485`）= 任务栏/工作区底或白板 inset。
- 吸附中心：`ResolveBarBottomDockCenterScreenY(dockLine, bodyHeightDip, stroke, zoom)`（`:499-509`）= `dockLine - (bodyHeight+stroke)*zoom/2`。所有调用方传入的 bodyHeight 都是 `mainButtonBaseSize`（80）：`Bar.RenderLoop.cpp:2316-2318`（换屏）、`:2391`、`:2455-2458`（任务栏动画）、`:2522-2526`（稳定 Docked 每帧 `SetDirect`）、`:3833-3835`（首次放置），以及 `Bar.Interaction.cpp:9377`、`:9607`、`:9740`（拖动）。
- 拖动协程 `BarUISetClass::Seek`（`Bar.Interaction.cpp:9192`）：`:9335` `const double bodyHeightDip = 80.0;`——竖向阈值判断、`VisibleHalfHeightScreen`、上下夹边（`minimumCenterY/maximumCenterY`）、抓点 `grabOffsetScreenY` 全部基于该常量。`BarBottomDockDragTracker`（`Bar.BottomDock.cppm:801-940`）比较"悬浮可见底边"与 dock 线的距离是否 ≤20 DIP×zoom；吸附后在 ±20 内保持，越过脱离；视觉偏移夹在 ±24。
- 竖向果冻：`Bar.RenderLoop.cpp:7279-7369` 以主按钮 `y ± (mainButtonBaseSize+stroke)/2` 为基准顶/底，`ResolveBarBottomDockAnchoredVerticalMapping`（`Bar.BottomDock.cppm:1006-1025`）求 `scaleY`；主按钮、Logo、主栏共用该竖向仿射（spec `rendering-and-ui.md:1102`）。**80 高 ±24 → scaleY 0.7..1.3；35 高 ±24 → 0.31..1.69，形变会非常夸张**。

**水平居中**

- 只在竖向已 `BottomDocked` 且主栏展开时生效（`ShouldCaptureBarBottomDockCenter` `Bar.BottomDock.cppm:600-613`、`BarBottomDockCenterDragTracker` `:628-786`）：主按钮+主栏联合外框中心距显示器中心 ≤40 DIP×zoom 即捕获，严格越界脱离。
- 稳定居中时主按钮 X 每帧由联合外框反推（`ResolveBarBottomDockCenteredRootPlacement` `:1115-1140`，门禁 `ShouldDeriveBarBottomDockCenteredRoot` `:1096-1112`，同文件 `:1256` 处有组合调用）——**主栏宽度动画期间中心自动连续**。
- 展开后自动居中：`Bar.RenderLoop.cpp:7604-7620`，要求 `!mainBarTimeline.IsActive()` 后联合中心在 40 DIP 内。
- 展开方向：`Bar.MainLayoutPolicy.cppm:55-82` `ResolveBarMainExpansionSide`（用目标布局宽度判断两侧是否放得下，`gapDip` 由调用方传入 10）。

**底栏模式提示（"底栏模式 · 居中"）**

- `ResolveBarBottomDockIndicatorGeometry`（`Bar.BottomDock.cppm:296-335`）：高 30，水平居中于联合外框，**`top = visibleMainBarTop - 30/2`**（`:330`），即一半压在主栏上沿、"嵌入"主栏。调用：`Bar.RenderLoop.cpp:10474-10501`。

**持久化**

- **不持久化主栏屏幕坐标**。只保存 `UI.Bar.StartupPlacement` 枚举（`Other.Config.cppm:397`；语义见 `StartupPlacement.cppm:18-81`）、`UI.Bar.Zoom2`、启动预览 `CachedStartupBarWidthDip`（`Other.Config.cppm:596`）。启动时由 `ResolveStartupPlacementBounds`（`StartupPlacement.cppm:86-125`）按可见宽高（`visibleWidth`, `mainButtonBaseSize + stroke`，`Bar.RenderLoop.cpp:3842-3854`）一次性放置。
- 因此切换尺寸模式**不需要坐标换算**；只需保证运行时位置连续与启动放置使用 compact 高度。

**显示过渡 / 缩放 / 自动收起**

- `Bar.DisplayTransition.cppm:24-49` 换屏时按 `targetHalfHeight` 夹 Y（调用方传主按钮半高）；`Bar.Zoom.cppm:104-128` `ClampMainButtonToScreen` 读 `mainButton->GetW/GetH`（已是动态值）；`Bar.Zoom.cppm:183-227` `FitInitialAfterMainBarLayout` 用 `mainButton->GetW()+10+mainBarWidth` 与 `max(mainButton.GetH, mainBar.GetH)` 判断首次 fit。
- `Bar.AutoCollapse.cppm`：只按 `BottomDockMode` 决策，不依赖几何。

**最近的水平果冻提交（83e41105 / c2470ae3）与尺寸动画的关系**

- `rendering-and-ui.md:1103` 新合同：水平果冻（近端/远端弹簧从上一成功像素播种）**只属于手势**（按住、刚松手、drag session 变化）；缩放、显示快照、场景接管等程序性变化不播种，远端在非居中时随根刚性移动，居中时远端目标取"居中姿态下主栏自身远端"。
- 尺寸模式切换属于程序性变化：主栏宽度经 `mainBarTimeline` 批次动画变化，近/远端都随布局值走，`scaleX` 应保持 1；居中时根 X 每帧由联合外框反推，远端目标随之更新。只要不在拖动中切换，与果冻机制无冲突（待实机确认）。

### 2.2 关键代码位置速查

- 阈值常量：`Bar.BottomDock.cppm:20-31`
- 竖向中心：`Bar.BottomDock.cppm:487-509`；调用 `Bar.RenderLoop.cpp:2316, 2391, 2455, 2522, 3833`；`Bar.Interaction.cpp:9335-9400`
- 竖向果冻基准：`Bar.RenderLoop.cpp:7279-7282, 7373, 7547-7549`
- 居中：`Bar.BottomDock.cppm:600-786, 1096-1140`；`Bar.RenderLoop.cpp:7568-7620`
- 提示条：`Bar.BottomDock.cppm:296-335`；`Bar.RenderLoop.cpp:10474-10501`
- 首次放置：`Bar.RenderLoop.cpp:3791-3894`；`StartupPlacement.cppm:86-125`；`Bar.Initialization.cpp:322-344`

### 2.3 小尺寸改造切入点

1. **单一"主体高度"来源**：把 `mainButtonBaseSize`（`Bar.RenderLoop.cpp:933`）替换为运行时 `bodyHeightDip`（= max(主按钮高, 主栏高)，compact 下预计 35，待确认主按钮高），并提供一个**随尺寸批次动画的 UiValue**（如 `state.mainBodyHeight`，与 `mainBarTimeline` 同批次，不受点击脉冲影响）。所有 `ResolveBarBottomDockCenterScreenY(..., mainButtonBaseSize, ...)` 和竖向果冻基准 `nextBaseTop/Bottom`、`outerHeightDip` 改读该值的 `.val`。
2. **拖动协程**：`Bar.Interaction.cpp:9335` 的 `80.0` 改为从呈现快照读取（`BottomDockPresentedSnapshot` 已有 `mainButtonVisibleHeightDip`，`:9353-9356` 已在用）或同一运行时主体高度。
3. **竖向吸附阈值**：`BarBottomDockThresholdDip=20`（输入捕获带）与 `BarBottomDockVisualLimitDip=24`（视觉形变上限）建议按主体高度比例或 compact 独立常量（例如视觉上限约 `24*35/80≈10.5`）。用户要求"竖直方向吸附区域因主栏变矮调整"，具体数值待确认。水平 40 DIP 居中带与高度无关，可保持。注意 spec（`rendering-and-ui.md:1089/1104/1151/1165`）与 headless 用例按 20/24/40 断言，改阈值需同步 spec 与测试。
4. **底栏提示条/构造提示不再嵌入主栏**：`Bar.BottomDock.cppm:330` 在 compact 下改为 `top = visibleMainBarTop - gap - 30`（gap 与属性面板一致取 `BarMainButtonToMainBarGapDip=10`），保留函数签名，新增参数或 compact 分支即可；预约包络 `ResolveBarBottomDockIndicatorVisualEnvelope` 自动跟随。构造提示（`Bar.ConstructionPrompt.cpp:115-175`）由另一调研主题负责定位，此处仅提示同理。
5. **热切换的位置连续性方案**：
   - Floating：主按钮中心不变，主按钮/主栏高度围绕中心收缩（上下各收 22.5 DIP）。若希望底边不动（更符合"占用更少屏幕"直觉），需在切换帧对 `displayCenterY` 做一次 `SetTar(+Δh/2)` 与高度同批次——属于产品选择，待确认。
   - BottomDocked：稳定帧 `displayCenterY` 每帧 `SetDirect(dockLine - bodyH/2)`（`Bar.RenderLoop.cpp:2522-2526`）。若 bodyH 用目标 35 而主按钮 h 仍在 80→35 动画中，可见底边会瞬间越过 dock 线约 22.5 DIP 再回弹——**必须用第 1 点的动画值**，这样底边始终贴 dock 线、顶边下沉，天然连续。
   - Centered：根 X 每帧由联合外框反推，宽度动画自动保持居中；展开方向锁存在居中态下不重判（`LockCenteredBarExpansion`）。
   - 主按钮点击脉冲在 dock 锁定时 `SetDirect` 到基准（`:2680-2687`），基准改为动画值后同样适用。
   - 拖动中切换：竖向映射 `verticalHandoff` 条件（`Bar.RenderLoop.cpp:7301-7305`）不含"基准高度变化"，建议尺寸模式切换在 `bottomDockDragActive` 时延后到松手后应用（待确认是否必要）。
6. **方向判定与容量**：`expansionInput` 的 `mainHalfWidthDip`、`gapDip`（`:2802-2811`）与容量 `:10298` 改读运行时值，避免 compact 下仍按 80 宽预约/判边。
7. **启动放置**：`Bar.RenderLoop.cpp:3842-3854` 与 `Bar.Initialization.cpp:333-338` 的可见高改用 compact 主体高。

---

## 三、悬停提示（小尺寸专属 tooltip）

### 3.1 现状机制

- **UI3 没有通用 tooltip/悬停浮窗设施**。现有相关件：
  - 悬停视觉状态机：`UI3.Button.cppm:131-145`（`Start/StopButtonHoverVisual`、`ButtonHoverStageEnum{None,Showing,Fading}`），悬停背景 5 s 自然淡出（`UI3.Metrics.cppm:31` `ButtonHoverFadeDurationSeconds=5.0`；spec `rendering-and-ui.md:1549-1561`）。
  - 通用命中状态：`UI3.Interaction.cppm:137-185` `ButtonPointerEvent/ButtonPointerResult`（hover/pressed 跟踪、点击后同坐标抑制悬停）。
  - 绘制属性的"标注/溢出说明弹窗"（`AnnotationTooltip/OverflowTooltip` 面板，`Bar.Main.cppm:797, 893, 903-904`；绘制登记 `Bar.RenderLoop.cpp:1573-1610`；悬停宽限计时器 `Bar.Interaction.cpp:108-110`、`ProcessBarTooltipTimer` `:2934-2990`）——是"徽标点击/悬停弹出的说明卡片"，有关闭按钮与命中，不是轻量文字提示，但其**在 Bar 自身窗口内绘制 + PanelStack 登记 + 视觉登记表 dirty + WM_TIMER 宽限**的组合可作为实现范式。
  - 构造提示已准备 `tooltips` 文案数组（`Bar.Main.cppm:398`、`Bar.ConstructionPrompt.cpp:217`），**只存数据、未绘制**。
  - 设置窗口 ImGui tooltip 不在讨论范围。
- **主栏悬停事件来源**：`Bar.Interaction.cpp:5224-5383`，`WM_MOUSEMOVE` 时遍历 `buttonList.Snapshot()`（及展开的 More 快照），用 `HitPresentedButton`（`:4306-4312`，按**已呈现帧**的命中快照）找到 `currentHoveredButton`，变化时 `Stop/StartMainBarButtonHover`（`:4341-4360`，经 `QueueInteractionVisualFeedback` 交给渲染线程）。跳过 Divider、Disable、Selected、颜色块。折叠/隐藏时清除（`:5210-5216`）；按下/拖动/面板切换处清除（`:6018, :6214, :8265-8301`）。
- **鼠标 vs 触摸/笔**：
  - 触摸消息带 `wheel` 标记，`IsBarTouchPointerMessage`（`Bar.Interaction.cpp:414-427`）可识别；入队时 `sample.source` 为 `Pointer::Source::Mouse/Touch`（`:1448-1467`，枚举 `Input/UiPointerInput.h:12` 含 `Mouse/Touch/Pen`）；PPT 空闲淡出已用 `mouse = !IsBarTouchPointerMessage && source==Mouse`（`:771-772`）作为"真实鼠标"判定，可直接复用。
  - 笔：`Inkeys::Message::IsPenGeneratedMouseMessage(message, extraInfo)`（`Message.cppm:43`）可识别笔提升的鼠标消息，PageControl 已用（`PageControl.cpp:2957`）。Bar 的 `ExMessage` 是否携带 extraInfo、笔悬停（未接触）是否会产生进入悬停分支的 `WM_MOUSEMOVE`——**待确认**。
  - 当前悬停分支（`:5224` 起）**没有显式排除触摸消息**；是否存在触摸来源的悬停取决于触摸 Move 只在接触期出现且被按压协程消费——待确认。tooltip 必须显式加"仅真实鼠标"门禁。
- **文字来源**：每个主栏按钮的 `name`（`UiWordClass`），2×2/2×1 显示的标签即它；i18n 在 `BarButtonSetClass::RefreshLocalizedText`（`Bar.Button.cpp:1111-1160`）原位替换，Draw/Eraser/Geometry/Select/Whiteboard 等动态标签由 `TransitionContent`（`Bar.Button.cpp:1104, 1242-1304`）更新。1×1 时只是 `name.pct=0`，`name.content` 仍是最新标签，可直接取 `name.content.GetTar()`。注意 Select/Pierce 的英文长标签含 `\n`（`Bar.Button.cppm:66-71`），tooltip 需把换行转空格或支持多行。扩展按钮也有 `name`；颜色块类按钮 `name` 以 `__color` 开头需排除。

### 3.2 浮窗绘制位置：Bar 自身窗口 vs 独立窗口

- **建议画在 Bar 自身窗口内**：
  - 已有先例：底栏模式提示条（`Bar.RenderLoop.cpp:10474-10540`，含 `ResolveBarBottomDockIndicatorVisualEnvelope` 包络、dirty key `dockTargetIndicatorKey` 和容量预约）、绘制属性说明弹窗、More 面板都在同一 HWND 内画在主栏外部。
  - 需要做的：新增一个视觉登记项（`registry.Add`，范式见 `Bar.RenderLoop.cpp:1589-1610`），把 tooltip 包络并入可见内容/纵向容量（`:10302` 起的纵向预约），在 `PanelStack`（`Bar.Main.cppm:895-908`）登记为**不吃输入**的最高层或直接不登记输入（避免抢命中）；坐标映射用 `BarVisualMapping::Rigid`（不随主体果冻形变，参考 More 的 Rigid 映射 `Bar.RenderLoop.cpp:1826-1829`）。
  - 优点：共享主题、透明度、DPI/zoom、ULW 呈现事务，无需新的窗口组与层级管理。
  - 代价：HWND 纵向需要为 tooltip 扩展（35 DIP 主栏 + 间隙 + 约 24-28 DIP 提示），首次出现可能触发容量扩张（capacity epoch）；建议在 compact 下常驻预约 tooltip 的纵向空间，避免频繁扩窗。
- 独立窗口：需新的 layered window、置顶/层级、跨屏 DPI、与 Bar 同步呈现，成本高，不推荐（除非 tooltip 需要越出 Bar HWND 的屏幕范围，目前 Bar 容量是动态的，不存在此限制——待确认）。

### 3.3 小尺寸改造切入点

1. 在 `Bar.Interaction.cpp:5379-5383`（`currentHoveredButton != hoveredMainBarButton`）旁发布"tooltip 目标按钮 + 进入时间"，仅当 `compact && 真实鼠标 && !pressed && !拖动 && !fold` 时有效；清除点与现有 `StopMainBarButtonHover` 调用点一致（`:5046, :5130, :5214, :6018, :6214, :8265-8301`）。
2. 延迟显示：复用 `WM_TIMER` 范式（`Bar.Interaction.cpp:108-110` + `ProcessBarTooltipTimer`）新增一个 timer ID，或在渲染线程按时间戳判断（推荐渲染线程，避免跨线程状态）。延迟时长待确认（Windows 默认约 500 ms）。
3. 渲染线程：读取目标按钮当前呈现位置（`button.inhX/inhY` 或已呈现命中快照）与 `name.content`，测字（`state.spec.MeasureText`，与 dock 提示同一套字体格式），计算浮窗矩形。
4. 边缘避让：默认放在主栏**外侧**（与属性面板展开方向相反的一侧或固定上方，待确认），水平以按钮中心对齐并夹在显示器/工作区内（More 面板的 `clamp(anchorX, padding+w/2, windowW-padding-w/2)` 写法可直接借用，`Bar.RenderLoop.cpp:5388-5399`）；竖向空间不足时翻到另一侧（参考 `Bar.RecallAttributeLayout.cppm:83-86` 的上下翻转判断）。需要避开已打开的属性面板/More 面板（是否允许重叠待确认）。
5. 隐藏条件：按下、开始拖动、主栏布局批次进行中（按钮在移动）、属性面板/More 打开时该按钮、切回大尺寸、折叠。

---

## 四、风险

1. **35 DIP 与现有网格冲突**：现有 1×1 按钮 32.5 + 上下 5 DIP 边距 = 42.5（正是 `BarPptCompactShortSideDip`，`Bar.Metrics.cppm:16-17`）。35 DIP 高意味着上下边距仅 1.25 DIP，或按钮缩小为 25 DIP。需用户确认（见问题 1），否则所有"左 5 DIP 起步、+5 间隙"的公式都要改。
2. **全局常量被多处依赖**：`ButtonOneSideDip/ButtonGapDip` 被分页、属性面板、More、构造提示共用；compact 不能直接改这些常量，只能在 compact 分支传入独立 `ListLayoutMetrics`/偏移。
3. **橡皮/撤回属性面板**：高度直接取主栏高（`Bar.EraserAttributeLayout.cppm:116`、`Bar.RecallAttributeLayout.cppm:71`），但内部按钮是 70 DIP（`ButtonTwoSideDip`），主栏变 35 后会立即溢出——即使暂不改橡皮，也会被动受影响。形状面板（GeometryAttribute）的 GeometryGrid 强制 2×2（`Bar.Button.cpp:2053-2056`），需单独 compact 分支。
4. **任务栏 UI 基准耦合**：`Display.cppm:241-247` 的 `ResolveUiScaleBase(..., baseHeightDip=80)` 把 80 DIP 主体对齐任务栏厚度（spec `rendering-and-ui.md:2597`）。compact 若仍传 80，则小尺寸主栏约为任务栏高的 35/80；若传 35 则整体 zoom 变大，小尺寸失去意义。需确认（问题 6）。
5. **测试与 spec 耦合**：`Bar.Metrics.cppm:66-69` 的 `static_assert`（380/470）；`Bar.EraserAttribute.Test.cpp` 9 处 80/380/470 相关引用；`Bar.Interaction.cpp:1980-2160` 内置拖动/展开回放用例使用 `mainButton->h`、`80.0*zoom`；`Bar.MultiTarget.Test.cpp` 的居中/果冻 `scaleX==1` 断言；spec 中 20/24/40 DIP、80 DIP 名义高、"主栏 80"的描述（`rendering-and-ui.md:441, 1089, 1102-1165, 2597`）。改动需同步 spec。
6. **布局批次与果冻**：尺寸切换走 `mainBarTimeline`，若用户在拖动中/底栏捕获恢复中切换，竖向映射基准高变化未纳入 `verticalHandoff`（`Bar.RenderLoop.cpp:7301-7305`），可能出现形状跳变；建议切换延后到无活跃手势。
7. **dirty/容量**：mainBar 高度此前恒定，部分包络/容量逻辑可能隐含"主栏高=80"（如纵向容量预约、`MapBarVisualBounds` Body 映射）——需在实现时对 compact 做整窗/局部对照（`--bar-eraser-offscreen-test` 类回归）。
8. **More 面板列数/锚点**：`expandedCenterY` 用 More 按钮高（`Bar.RenderLoop.cpp:5392-5395`），compact 下自动变近；但 `BarMorePanelCompactWidth=60`（`Bar.Main.cpp:354`）等紧凑展开动画尺寸是按 2×2 设计的。
9. **启动预览**：在 Bar 初始化前运行，只能读配置；若 SizeMode 未在预览路径读取，会先显示 80 高预览再切到 35 高主栏。
10. **状态指示器**：`Bar.StatusIndicatorModel.cppm:934` 及放射扇形位置依赖主按钮 80×80，"主按钮变宽"要求主按钮宽高分离（与本节 `mainButtonBaseSize` 改造同一改动点），由状态指示器调研主题细化。

---

## 五、待确认问题

1. 35 DIP 主栏内 1×1 按钮是保持 32.5 DIP（上下边距 1.25 DIP）还是缩小为 25 DIP（保持 5 DIP 边距）？水平按钮间距是否仍为 5 DIP？
2. 小尺寸主按钮的目标尺寸：35×35？还是略大于主栏（如 40×40）？Logo 是否同比缩小？
3. 小尺寸 More 面板一行最多几个 1×1 按钮（列数上限）？
4. 小尺寸竖向吸附阈值与果冻形变上限的具体数值（当前 20 DIP 捕获 / 24 DIP 视觉上限），是否按 35/80 比例缩放？
5. 悬浮态切换尺寸时，主栏保持中心不动（上下同时收缩）还是保持底边不动？
6. 任务栏高度作为 UI 基准（`UseTaskbarHeightAsScaleBase`）时，小尺寸是否仍以 80 DIP 名义高对齐任务栏？
7. 悬停提示：延迟多久显示？显示在主栏上方固定、还是与属性面板相反的一侧？是否也覆盖 More 面板内按钮、翻页控件按钮、主按钮本身？笔悬停（未接触）是否需要显示？
8. 底栏模式提示 / 构造提示与主栏的间隙是否与属性面板一致取 10 DIP？
9. 拖动中切换尺寸（例如设置窗口与主栏同时操作）是否需要支持，还是延后到松手后生效即可？
