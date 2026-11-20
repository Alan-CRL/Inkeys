# 调研：主栏附属属性窗口 / 底栏模式提示 / 形状步骤指示栏（小尺寸模式）

> 范围：绘制属性、橡皮属性（含灵敏度菜单）、撤回属性、形状（Geometry）属性、More 面板；
> "底栏模式"指示器（`Bar.BottomDock.cppm` + `Bar.RenderLoop.cpp`）与形状多步绘制步骤指示栏（`Bar.ConstructionPrompt.cpp`）。
> 只读调研，所有行号基于当前工作树 `refactoring/drawpad-ui`（2f2adf9b）。路径省略前缀 `Inkeys/Inkeys/UI/`。

---

## 0. 公共尺寸基线

| 常量 | 值 | 位置 |
| --- | --- | --- |
| `ButtonGapDip` | 5 | `UI3/UI3.Metrics.cppm:11` |
| `ButtonOneSideDip` | 32.5 | `UI3/UI3.Metrics.cppm:12` |
| `ButtonTwoSideDip` | 70（=32.5×2+5） | `UI3/UI3.Metrics.cppm:13` |
| `BarMainBarHeightDip` | 80 | `Bar/Bar.Metrics.cppm:32` |
| `BarMainButtonToMainBarGapDip` | 10（主按钮↔主栏水平间距，**同时被橡皮/撤回属性当作面板↔主栏竖直间距**） | `Bar/Bar.Metrics.cppm:41` |
| `BarDrawAttributeGap` | 5 | `Bar/Bar.Layout.cppm:39` |
| `BarMorePanelAnchorGap` | 12（More 按钮边缘到面板） | `Bar/Bar.Main.cpp:348` |
| `BarBottomDockIndicatorHeightDip` | 30 | `Bar/Bar.BottomDock.cppm:30` |

"属性窗口与主栏的间隙"在代码中并没有统一常量，而是分散的 4 处 10 DIP（见 §1.6 汇总），More 面板另用 12 DIP（相对 More 按钮而非主栏边缘）。

所有附属面板的展开/收拢姿态统一走 `ResolveBarAttributePanelFrame(motion, panel, anchor, compactWidth, parent)`（`Bar/Bar.EraserAttributeMotion.cppm:108`），从触发按钮中心的紧凑态（宽 60 DIP，`BarDrawAttributeCompactWidth`，`Bar/Bar.Main.cpp:233`）缩放展开。上/下方向统一由 `barState.widgetPosition.primaryBar` 决定（true=面板在主栏下方），各面板再按工作区剩余空间自行翻边。

---

## 1. 现状机制（属性窗口）

### 1.1 绘制属性（DrawAttributeBar）

- 尺寸固定 370×185：`BarDrawAttributeExpandedWidth = 370`（`Bar/Bar.Layout.cppm:38`），`BarDrawAttributeExpandedHeight = 185`（`Bar/Bar.Main.cpp:232`）；收拢为 60 宽等比（`Bar/Bar.Main.cpp:233-237`）。
- 定位：`drawAttributeBar->Inherit(Center, drawButton->button)`（`Bar/Bar.RenderLoop.cpp:10557-10558`），y 偏移在 `Bar/Bar.RenderLoop.cpp:4018-4022`：
  `y = ±(MainBar.GetH()/2 + drawAttributeBar.GetH()/2 + 10.0)`，**10.0 为硬编码**；primaryBar 决定正负（下/上）。
- 该公式假设"绘制按钮中心 = 主栏竖直中心"，因此只要小尺寸下 1×1 绘制按钮仍竖直居中于 35 DIP 主栏，面板与主栏边缘间隙自动保持 10 DIP，内部布局（颜色 5/115、粗细 75/5 等绝对坐标，`Bar/Bar.RenderLoop.cpp:4037-4038, 4550`）不受主栏高度影响。
- 结论：符合用户"绘制属性基本不改"。

### 1.2 橡皮属性（EraserAttribute + 灵敏度菜单）

纯几何在 `ResolveEraserAttributeLayout`（`Bar/Bar.EraserAttributeLayout.cppm:109-205`），渲染/动画在 `Bar/Bar.EraserAttribute.cpp`。

- **面板高度 = 主栏实际高度**：`height = input.main.Height()>0 ? input.main.Height() : BarMainBarHeightDip`（`:116`）。`input.main` 取 `MainBar` 的 inh 矩形（`Bar/Bar.EraserAttribute.cpp:198`）。因此主栏改 35 DIP 时面板高度会**自动**变 35，但内部按钮不会。
- 内部按钮高度写死为 `button = ButtonTwoSideDip`（70）（`:117`），用于：
  - `items[0]` Clear：70×70，竖直居中 `cy±35`（`:149-150`）；
  - `automatic`（A + 小三角）：宽 `autoWidth = 70 + EraserAttributeAutomaticArrowWidthDip(20) = 90`，高 70（`:117-118, 176`）；body 命中 `items[4]` 70 宽，arrow 命中 `items[5]` 20 宽（`:178`）；内部分割线 `automaticDivider`（`:181-182`）；
  - `palm`/`items[10]`：70×70（`:179`）；
  - 主分割线高 `dh = button - ButtonGapDip*4 = 50`（`:151-153`）。
  若只把主栏改 35 而不改这里，三颗 70 高按钮会上下各溢出 17.5 DIP。
- 按钮视觉：Clear/Automatic/Palm 使用 `StandardTwoTwo` 指标（图标上偏 + 下方文字）（`Bar/Bar.EraserAttribute.cpp:304, 323-329`，`large = i==0||i==4||i==10`）。小三角为 18 DIP SVG（`EraserAutomaticArrowSizeDip`，`Bar/Bar.EraserAttribute.cpp:32, 377-381`），在 `items[5]` 内居中，角度 0/180 动画。
- **左侧尺寸预览（"左侧橡皮按钮"）**：三档直径 `BaseSizePresets` = 24/32/40 DIP（`Drawing/Draw3/Draw3.SpeedEraser.h:88-89`），经 `DiameterToCanvasPx(...)/zoom` 换算为实际像素直径（`:124`），矩形/方形形状按 `ResolveEraserHalfSize` 变换（矩形半高 = r×1.085，即 40 DIP 档高约 43.4，`Draw3.SpeedEraser.h:14-20`）。
  - 水平：圆组四段留白 `EraserAttributeCircleGapDip = 16`（`:104, 130`）。
  - 竖直：`previews[i]` 以 `cy` 居中，不受面板高度约束（`:159-164`）；`previewClips[i]` 与 `items[1..3]` 命中均与 `{.., y, .., y+height}` 及 `r.panel` 求交（`:165-173`），绘制时 `PushAxisAlignedClip(previewClips)`（`Bar/Bar.EraserAttribute.cpp:399` 附近，`context->PushAxisAlignedClip(PixelRect(g.previewClips[index],zoom_))`）。
  - spec 已有合同："预览高于面板 → 仅裁可见区域，不改圆直径或增加透明命中"（`.trellis/spec/native-desktop/eraser-attributes.md:65, 82`）。**即用户要求的"允许橡皮上下超出但截断"现有机制已具备**：面板变 35 后，32/40 档预览及其选中环（外扩 3.5 DIP，`:99-103`）会被自动裁剪。
- 灵敏度菜单（`menu`）：独立尺寸 `menuWidth = 70×2 + 32.5 + 5×2`，`menuHeight = 32.5×2 + 10×2 + 5`（`:183-185`），相对**面板**而非主栏摆放，间隙 `mg = ButtonGapDip`（5）（`:186-195`）；内部行高 `ButtonOneSideDip`，与主栏高度无关。
- 定位与上下翻边：`panelGap = BarMainButtonToMainBarGapDip`（10）（`:141`）；`r.below = input.below`（来自 primaryBar），若目标侧剩余空间 < 面板高且另一侧更大则翻边，`lockPanelSide` 时不翻（`:142-146`）。水平：Clear 中心锚定主栏橡皮按钮中心 X（`:137-139`），窄区只裁剪（`:140, 147`）。
- 当前自然宽度（无手掌外扩时）：circleSide(96+64=160) + 1 + 2 + 70 + 2 + 1 + automaticSide(5+2+90+5+70=172) ≈ 408 DIP（spec 中 342 为加入手掌开关前的旧值，`eraser-attributes.md:40`，待确认 spec 是否已过期）。

### 1.3 撤回属性（RecallAttribute）

- `ResolveRecallAttributeLayout`（`Bar/Bar.RecallAttributeLayout.cppm:68-93`）：
  - 面板高度同样 = `input.main.Height()`（`:71`）；按钮 `button = ButtonTwoSideDip`（70）（`:72`）；自然宽 `5+70+5 = 80`（`:75`）；
  - `panelGap = BarMainButtonToMainBarGapDip`（10）（`:82`），翻边逻辑与橡皮一致（`:83-87`）。
- 视觉：单颗 Undo/Redo 按钮，`StandardTwoTwo` 图标+文字（`Bar/Bar.RecallAttribute.cpp:255-257`）。
- 小尺寸若主栏 35：面板 35 高但按钮 70 高 → 必须同步改。

### 1.4 形状（Geometry）属性

`BarGeometryAttributePanel::Advance`（`Bar/Bar.GeometryAttribute.cpp:373-`）：

- 结构自上而下：header（3 个分段 + 关闭，高 `buttonSide(32.5)+g`）→ 类别网格（可滚动视口）→ 分割线 → 参数行（圆/边数/约束/辅助，高 32.5+2g+1）→ 属性行（样式/线型/粗细，单行 SingleRow）（`:452-463, 547-612`）。
- **类别按钮 = 2×2**：`b.size = i < CategoryCount ? twoTwo : oneOne`（`:234`），图标 28、文字 70×25（`:242, 249`）；网格用 `ResolveBarButtonListLayout(..., GeometryGrid)`（`:420`），其中 `GeometryGrid` 强制 `columnSpan=rowSpan=2, width=height=ButtonTwoSideDip`，`FixedGrid`，**列数 = min(6, 可见数)**（`Bar/Bar.Button.cpp:2053-2056, 2079-2084`）。FixedGrid 以 `metrics.two` 为格边长（`UI3/UI3.ListLayout.cpp:20-41`）。渲染强制 `ButtonVisualLayoutKind::StandardTwoTwo`（`Bar/Bar.GeometryAttribute.cpp:767`），并按 2×2 文字槽测字缩放（`:727-729`）。
- 宽度：`targetWidth = max(categoryLayout.width + 2g, attributeMaxWidth_, parameterWidth)`（`:460`）；header 分段宽 `(categoryLayout.width - buttonSide - 4g)/3`（`:580`），关闭按钮对齐网格最后一列（测试 `Bar.GeometryAttribute.Test.cpp:291` 断言）。6 列 2×2 = 6×70+5×5 = 445 DIP。
- 定位：局部常量 `gap = 10`（`:452`）；`below = primaryBar`，若完整高度放不下且另一侧更大则翻边（`:464-470`）；`top = previousBelow_ ? main.bottom + gap : main.top - height - gap`（`:513`）；水平以 Geometry 按钮中心居中并夹取工作区（`:512-514`）。高度与主栏无关。

### 1.5 More 面板

`Bar/Bar.RenderLoop.cpp:5320-5546`：

- 布局：`ResolveBarButtonListLayout(explicitMore, GroupedGrid, forcedOverflow)`（`:5347-5348`），在 `Bar/Bar.Button.cpp:2086-2098`：子格为 1×1 单元（32.5），按钮保留原尺寸跨度（twoTwo 占 2×2 子格、twoOne 占 2×1、oneTwo 宽 10），**子列数 = 2 × min(5, ceil(sqrt(count)))**，即最多 10 子列 = 5 个 2×2；显式组与强制溢出组分组、中间可有分割线。
- 尺寸：`contentWidth = subColumns×32.5 + (subColumns-1)×5`，`panelWidth = content + 2×5 + 关闭侧栏 35`，`panelHeight = 2×5 + gridHeight`（`:5355-5363`）。
- 每按钮视觉 kind 取 `button->size`（oneOne/twoOne/twoTwo，`:5509-5512`），oneOne 时隐藏文字（`:5530`）；宽高由 placement 跨度决定（`:5487-5488`）。
- 定位：从 More 按钮中心展开，`expandedCenterY = anchorY ± (moreButton.h/2 + BarMorePanelAnchorGap(12) + panelHeight/2)`，side 取 primaryBar（`:5334, 5392-5395`）；**不做空间翻边**，只夹取到窗口（`:5396-5399`）。More 按钮本身在主栏是 `twoTwo`（`Bar/Bar.Button.cpp:788-789`）。

### 1.6 间隙汇总

| 面板 | 间隙来源 | 相对 |
| --- | --- | --- |
| 绘制属性 | 硬编码 `10.0`（`Bar.RenderLoop.cpp:4020,4022`） | 主栏边缘（经主栏半高推算） |
| 橡皮属性 | `BarMainButtonToMainBarGapDip`（`Bar.EraserAttributeLayout.cppm:141`） | 主栏边缘 |
| 撤回属性 | `BarMainButtonToMainBarGapDip`（`Bar.RecallAttributeLayout.cppm:82`） | 主栏边缘 |
| 形状属性 | 局部 `gap = 10`（`Bar.GeometryAttribute.cpp:452`） | 主栏边缘 |
| More | `BarMorePanelAnchorGap = 12`（`Bar.Main.cpp:348`） | More 按钮边缘（大尺寸下等效距主栏边 17） |
| 橡皮灵敏度菜单 | `ButtonGapDip`（5） | 橡皮面板 |

---

## 2. 现状机制（底栏模式提示 / 步骤指示栏）

### 2.1 "底栏模式"是什么

- `BarBottomDockMode { Floating, BottomDocked }` + 正交的 `BarBottomDockCenterMode { Free, Centered }`（`Bar/Bar.BottomDock.cppm:33-43`）。竖向捕获阈值 20 DIP、水平居中捕获 40 DIP、果冻视觉限值 24 DIP（`:20-23`）。
- 用户所说"底栏模式嵌入主栏"指的是**拖动时的模式提示胶囊**（文案 `UI.Bar.BottomDock.Mode`"底栏模式"/"底栏模式 · 居中"），来源任务 `.trellis/tasks/08-20-ui3-bar-bottom-dock-feedback/prd.md` R2。
- 触发：`ResolveBarBottomDockIndicatorTarget(mode==BottomDocked && dragActive && mainBarExpanded && gestureEligible)`（`Bar/Bar.BottomDock.cppm:259-265`，调用于 `Bar/Bar.RenderLoop.cpp:7445-7449`）；资格锁存 `ResolveBarBottomDockIndicatorGestureEligibility`（`:281-293`，输入侧 `Bar/Bar.Interaction.cpp:9470`）：浮动首次进入底栏或 Free/Centered 任意切换时激活，松手/取消/折叠/竖向离开时清除。白板可通过 `HideWhiteboardSnapIndicator()` 抑制（`Bar.RenderLoop.cpp:7448`）。

### 2.2 指示器如何"嵌入主栏"

- 几何：`ResolveBarBottomDockIndicatorGeometry(mainButton, mainBar, labelW, labelH, visibleMainBarTopDip)`（`Bar/Bar.BottomDock.cppm:295-334`）：
  - 高 30、内边距 =（30 − 文字高）/2，宽 = 文字宽 + 2×内边距；
  - 水平中心 = 主按钮与主栏联合外框中心；
  - **`top = visibleMainBarTopDip − 30/2`** —— 竖直中心压在主栏可见上边框，这就是"嵌入"（`:330-333`）。
- 调用：`Bar/Bar.RenderLoop.cpp:10474-10479`（当前文字宽）与 `:10495-10500`（两种文案最大宽的预留几何，用于 viewport/capacity）；`visibleMainBarTopDip = bottomDockMapping.MapY(mainBar 可见上沿)`（`:10456-10457`），水平经 `bottomDockHorizontalMapping.MapX` 与主按钮刚性抓手平移（`:10459-10473`），即跟随竖向/横向果冻。
- 外观：不共享主栏 shape，而是每帧 `emplace` 一个独立 `UiShapeClass`：深色 Surface、80% 填充、18% 边框、PointLight、圆角 6×scale（`Bar/Bar.RenderLoop.cpp:10096-10130` 附近，常量 `BarBottomDockIndicatorCornerRadiusDip`），在 `SetBaseTransform()` 下、所有面板与构造提示之上绘制（`:10094-10097`）。"共享材质"仅指颜色/透明度/光源配置复用主栏同款。
- 动画：显隐 `bottomDockTargetIndicatorProgress`（EaseOutBack / 反向），文字换字走 `AdvanceBarIndicatorContent` 三阶段（扩框→换字→缩框）（`Bar/Bar.BottomDock.cppm:189-257`，RenderLoop `:10383-10450`）。
- 脏区/viewport：`ResolveBarBottomDockIndicatorVisualEnvelope`（`Bar/Bar.BottomDock.cppm:400-433`）同时供实际 dirty（`:10528-10533`）与峰值预留（`:10508-10512`，含 Back 峰值、描边、Gaussian、抗锯齿）。
- 命中：`bottomDockIndicatorPanel`，PanelStack 顺序 11、优先级 100（最高，`Bar/Bar.Main.cppm:894, 907`），`SetCandidate(...)`（`Bar.RenderLoop.cpp:10538-10541`）；输入侧从 seqlock 快照读 `indicatorBounds`（`Bar/Bar.Button.cpp:2175-2180`）。
- 与步骤指示栏互斥：`dockTargetIndicatorVisible = !owner_.constructionPrompt.Visible() && ...`（`Bar.RenderLoop.cpp:10522`）。

### 2.3 形状步骤指示栏（BarConstructionPrompt）如何嵌入主栏

- 已经是**独立面板**：自有 `surface_`（圆角 6、主栏同款常驻材质，`Bar/Bar.ConstructionPrompt.cpp:72-76`），自有 `Panel panel_{"ConstructionPrompt"}`（`Bar/Bar.Main.cppm:405`），PanelStack 顺序 10、优先级 90（`Bar.Main.cppm:906`）；在 Rigid 变换下绘制（`Bar.RenderLoop.cpp:10093-10095`）。
- 尺寸：单行高 42.5（=32.5+2×5），按钮 1×1（`StandardOneOne`，`:88-94`），过窄时换行堆叠，高 `42.5 + ceil(n/columns)×37.5`（`:137-143`）；宽 = 文字 + 按钮 SingleRow 宽（`:136, 142`）。
- 定位（嵌入的唯一来源）：
  - 锚点 = 主按钮与主栏可见并集；折叠时只用主按钮（`:179-184`）；
  - `embed = BarBottomDockIndicatorHeightDip/2`（15）；`above = anchorTop + embed − targetHeight`；上方不足则 `anchorBottom − embed`（翻到下方并向上压入）（`:188-191`）；
  - 位置/尺寸 0.4 s 动画（`:192-193`）。
- spec 合同：`.trellis/spec/native-desktop/shapes.md:75`（"第八轮起与居中底栏提示同深度嵌入主栏"），`rendering-and-ui.md:3039`。
- 测试强绑定嵌入深度：`Bar/Bar.GeometryAttribute.Test.cpp:1770`（上方嵌入）与 `:1816`（翻到下方嵌入）。
- 命中/窗口区域：`constructionPrompt.PresentedRegions()` 经 `TranslateBarBottomDockRigidRect` 加入主栏可交互区域（`Bar/Bar.Interaction.cpp:8807-8810`）；`Pointer()` 先于主栏处理（`Bar.Interaction.cpp:8262`）。
- 脏区/窗口：`IncludeVisibleBounds(constructionPrompt.Bounds(), Rigid)` 参与窗口外框（`Bar.RenderLoop.cpp:10768`），`dirtyRegionTracker.Observe(...ConstructionPrompt...)`（`:11379-11380`）。
- `Bar.ShapeEditor.cpp` 中的编辑浮条是 SelectionOverlay 画布侧浮条，与主栏只有"避让主栏矩形"耦合（`Bar.ShapeEditor.cpp:1406-1408` 用主栏矩形算重叠分数），主栏变矮后自动生效，不属于"嵌入主栏"。

---

## 3. 小尺寸改造切入点

### 3.1 尺寸模式的注入方式（建议）

所有 Resolve 函数都是纯函数、输入是结构体，适合把"是否小尺寸 / 主栏按钮边长"作为输入字段传入，而不是读全局：

- `EraserAttributeLayoutInput` / `RecallAttributeLayoutInput` 增加 `compact`（或直接 `buttonSideDip`、`dividerHeightDip`）字段，由 `Bar.EraserAttribute.cpp:197-201`、`Bar.RecallAttribute.cpp:140-144` 填充；拖动锁存（`dragLayoutInput_`/`stableLayoutInput_`）会一并复制，需确认模式切换时重新解析。
- 面板间隙抽为独立常量（如 `BarAttributePanelGapDip = 10`），替换橡皮/撤回对 `BarMainButtonToMainBarGapDip` 的借用、绘制属性的硬编码 10.0、形状属性的局部 `gap = 10`。否则小尺寸若缩小主按钮↔主栏水平间距，会连带改变属性面板竖直间隙。

### 3.2 橡皮属性（高度 35）

`Bar/Bar.EraserAttributeLayout.cppm`：
1. `:116` 面板高度已跟随主栏，无需改；仅需保证小尺寸下主栏 `h` 确实为 35。
2. `:117` `button` 改为按模式取 `ButtonOneSideDip`（32.5）/`ButtonTwoSideDip`，影响 Clear、Automatic body、Palm 的宽高（`:149-150, 176-179`）。
3. `:151` 分割线高 `dh` 需改为小尺寸主栏分割线高度（大尺寸=50；SingleRow 分隔线为 `ButtonOneSideDip-4 = 28.5`，`Bar.Button.cpp:2065`，具体取值待与主栏小尺寸方案统一）。
4. "右侧带小三角展开的按钮"：`autoWidth = button + 20`（`:118`）→ 小尺寸 52.5×32.5；箭头 18 DIP SVG 在 20×32.5 内仍可居中（`Bar.EraserAttribute.cpp:377-381`），内部分割线沿用 `dividers[0]` 的 top/bottom（`:181-182`）自动跟随第 3 点。
5. 视觉：`Bar.EraserAttribute.cpp:304, 323-329` 的 `large` 按钮改用 `StandardOneOne`（仅图标、`name.pct=0`），图标 offset `-EraserAttributeAutomaticArrowWidthDip/2`（`:325`）保持。
6. 左侧预览：保持直径不变，现有 `previewClips` 与面板求交即实现"上下超出但截断"（`:165-173` + 绘制处 `PushAxisAlignedClip`）。命中 `items[1..3]` 也会被同时裁成 35 高，符合 spec "不增加透明命中"。
7. 灵敏度菜单（`:183-203`）独立于主栏高度，可不改；其相对面板 5 DIP 间隙不变。
8. 小尺寸自然宽度约：160 + 1 + 2 + 32.5 + 2 + 1 + (5+2+52.5+5+32.5) ≈ 295.5 DIP。

### 3.3 撤回属性（高度 35）

`Bar/Bar.RecallAttributeLayout.cppm:72` 按钮改 32.5，自然宽 `5+32.5+5 = 42.5`（`:75`）；视觉改 `StandardOneOne`（`Bar.RecallAttribute.cpp:255-257`）。面板为 42.5×35 非正方，竖直留白 1.25 而水平 5（待确认是否要改成正方或其它比例）。

### 3.4 形状属性（类别按钮 1×1，一行更多）

- `Bar/Bar.Button.cpp:2053-2056`：`GeometryGrid` 分支在小尺寸下改为 `columnSpan=rowSpan=1, width=height=ButtonOneSideDip`；`:2079-2084` FixedGrid 需传 `cellColumnSpan/cellRowSpan = 1`，并让 `ResolveListLayout` 的格边长使用 `one`（可通过 `ListLayoutMetrics{gap, one, two=one}` 或给 FixedGrid 增加格边参数，`UI3/UI3.ListLayout.cpp:20-41`）；列上限 `min(6, count)` 改为更大值（12 列 1×1 = 12×32.5+11×5 = 445，与现 6 列 2×2 同宽，待确认）。
- `Bar.BarButtonListLayout` 的调用签名目前没有尺寸模式参数（`Bar.Main.cppm:302` 的 `BarButtonListLayoutKind`），需要新增参数或新 kind。
- `Bar/Bar.GeometryAttribute.cpp:767` 类别按钮视觉 kind 改 `StandardOneOne`、隐藏文字；`:727-729` 测字缩放只在 2×2 下需要；`:249` 图标尺寸 28→20。
- header 分段宽依赖 `categoryLayout.width`（`:580`），若小尺寸网格变窄，分段会被压缩；12 列方案可避免。
- 滚动步长 `ButtonTwoSideDip+ButtonGapDip`（`:1196`）在 1×1 下应改为 `ButtonOneSideDip+ButtonGapDip`。
- 面板定位不依赖主栏高度（`:466-470, 513`），无需改。

### 3.5 More 面板（按钮 1×1，一行更多）

- `Bar/Bar.Button.cpp:2043-2049` 的尺寸→跨度映射是"布局时把 2×2/2×1 映射为 1×1"的天然切入点（与 PRD"只在布局计算时映射"一致）；oneTwo 的 10 宽特殊项（`:2057-2060`）保持。
- 列数公式 `2 × min(5, ceil(sqrt(count)))`（`:2090-2091`）是为 2×2 设计的"子列数"，1×1 下可改为 `min(N, ceil(sqrt(count)) × k)`（具体上限待确认）。
- `Bar/Bar.RenderLoop.cpp:5487-5488`（宽高由 placement 跨度推算，自动跟随）与 `:5509-5512, 5530`（视觉 kind 取 `button->size`）——后者需在小尺寸下强制 `StandardOneOne` 并隐藏文字，否则 2×2 文字仍会绘制。
- 定位 `:5392-5395` 基于 More 按钮高度，按钮变 32.5 后与主栏边缘的距离由 17 变为约 13.25（=1.25+12），若要求与其它属性窗一致的 10 DIP，需要改成相对主栏边缘计算（待确认）。

### 3.6 绘制属性

无需改布局；仅建议把 `Bar.RenderLoop.cpp:4020/4022` 的 10.0 替换为统一间隙常量。前提：小尺寸下绘制按钮竖直居中于主栏（否则 `MainBar.GetH()/2` 推算失效）。

### 3.7 "底栏模式"提示改为分离浮层

- 唯一几何入口：`ResolveBarBottomDockIndicatorGeometry`（`Bar/Bar.BottomDock.cppm:330-333`）。小尺寸下 `top = visibleMainBarTopDip − gap − 30`（gap=属性窗间隙 10）。建议给函数增加 `separated/gapDip` 参数，RenderLoop 的两次调用（`:10474, :10495`）同时传入，这样 dirty（`:10528`）、峰值预留（`:10508`）、命中（`:10533-10541`）与 viewport 自动一致。
- 水平居中、果冻跟随（`MapY`/`MapX`）、显隐/换字动画、材质与命中优先级均可原样复用；**不需要**复用属性窗的 `ResolveBarAttributePanelFrame`（指示器自有中心缩放与 Back 动画）。
- 底栏状态下主栏贴屏底，指示器始终在上方，无需翻边逻辑。

### 3.8 步骤指示栏改为分离浮层

- 唯一几何入口：`Bar/Bar.ConstructionPrompt.cpp:188-191`。小尺寸下：
  `above = anchorTop − gap − targetHeight`；上方不足 `below = anchorBottom + gap`。
- 可复用属性窗的"首选侧 + 空间不足翻边"语义（参考 `Bar.GeometryAttribute.cpp:464-470`），但当前提示固定"优先上方"，不跟随 primaryBar（待确认小尺寸是否要改为跟随 primaryBar）。
- 已有独立 surface/panel/命中/脏区，改位置不需要改输入与脏区路径；`Bounds()`/`PresentedRegions()` 自动跟随。

---

## 4. 风险与耦合

1. **`BarMainButtonToMainBarGapDip` 双重语义**：同时是主按钮↔主栏水平间距（`Bar.Metrics.cppm:58,79`、`StartupPreview/StartupPreview.State.cppm:28`）和橡皮/撤回面板竖直间隙。小尺寸改主按钮间距会误伤属性窗。
2. **测试断言**：
   - 构造提示嵌入深度：`Bar.GeometryAttribute.Test.cpp:1770, 1816`（大尺寸需保持、小尺寸需新增用例）。
   - 形状面板关闭按钮对齐最后一列：`Bar.GeometryAttribute.Test.cpp:291`；类别/控件数量 `:338`。
   - 橡皮属性离屏/几何测试（`Bar.EraserAttribute.Test.cpp`，3262 行）大量依赖 70 DIP 按钮与 342/408 宽度；spec `eraser-attributes.md:40,66,82,102` 写死"5/16 DIP 留白、342 自然宽、90×70 自动按钮"，小尺寸需扩写 spec。
3. **拖动锁存**：橡皮/撤回的 `dragLayoutInput_`/`stableLayoutInput_`（`Bar.EraserAttribute.cpp:204-216, 262-270`；`Bar.RecallAttribute.cpp:146-210`）会锁存旧输入；若尺寸模式以输入字段注入，模式切换发生在直拖中时会沿用旧模式几何（建议模式切换时强制关闭附属面板或重置锁存，待确认）。
4. **窗口包络/viewport**：spec `rendering-and-ui.md:2926` 规定"窗口上沿只在多步形状步骤栏或底栏模式提示栏出现时抬高"。分离后两者整体再上移 25 DIP（30/2 + 10），窗口上沿抬得更高；预留几何来自同一函数，理论自动覆盖，但 `fixedDragGeometry`/`fixedReleaseGeometry`（`Bar.RenderLoop.cpp:10304-10316`）在直拖中冻结容量，需确认冻结容量是否已包含新的更高位置（拖动开始时提示可能尚未出现）。
5. **点击穿透语义变化**：嵌入时指示器/提示与主栏重叠，无缝隙；分离后 10 DIP 间隙不属于任何面板，透明像素会穿透到下层窗口（与属性窗一致，但 08-20 PRD R2 关于"阻止穿透"的验收只针对指示器自身区域，需确认是否接受间隙穿透）。
6. **变换空间不同**：指示器在 Base 变换并跟随果冻 `MapY`；构造提示在 Rigid 变换（仅刚性平移）。分离后两者在果冻形变时的相对位置会出现可见差异（嵌入时被主栏边框遮掩，分离后更明显）。
7. **状态指示器胶囊**同样"竖直中心压在主按钮上边框"（spec `rendering-and-ui.md:2923`），小尺寸下 PRD 要求不显示胶囊，但飞行/同心弧路径若仍以主按钮边框为基准，需与本文件的分离方案一起核对（不在本调研范围）。
8. **More 面板无翻边**：主栏贴屏顶且 primaryBar=false 时 More 面板只靠窗口夹取；小尺寸不改变该行为，但按钮变小后面板更宽更矮，夹取更容易发生水平偏移。
9. **ListLayout 共享**：`ResolveListLayout` 是 UI3 公共组件（翻页控件等也可能使用），修改 FixedGrid 的格边语义需避免影响其它调用者；建议通过 `ListLayoutMetrics` 参数注入而非改默认值。

---

## 5. 待确认问题

1. 小尺寸下附属属性窗与主栏的间隙是否仍为 10 DIP？More 面板是否统一改为"距主栏边缘 10 DIP"（现为距 More 按钮边缘 12 DIP）？
2. 橡皮属性小尺寸主分割线高度取多少（主栏小尺寸分割线高度未定）？三颗按钮是否严格 32.5×32.5，"自动"按钮宽是否为 32.5+20？
3. 撤回属性小尺寸面板是 42.5×35（左右 5、上下 1.25）还是改为正方/其它留白？
4. 形状面板类别 1×1 时每行列数上限（建议 12，使面板宽度与现状 445 DIP 一致）？header 三个分段在网格变窄时是否允许压缩？
5. More 面板 1×1 时列数公式与上限？
6. 底栏模式提示与步骤指示栏分离后，是"始终在主栏上方"还是像属性窗一样随 primaryBar/空间翻边？步骤指示栏折叠态（只锚主按钮）是否同样保持 10 DIP 间隙？
7. 分离后 10 DIP 间隙区域允许点击穿透到下层窗口吗？
8. 小尺寸切换发生在附属面板打开或直拖期间时，是否直接关闭所有附属面板（可规避拖动锁存旧几何问题）？
9. 大尺寸下保持"嵌入"不变（仅小尺寸分离），还是两种尺寸都改为分离？（PRD 原文只针对小尺寸，按此理解，待确认。）
