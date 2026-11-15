# Research: 主栏图形面板分类（常规 / 立体 / 函数 …）

- **Query**: 主栏图形（Geometry）展开面板过大，用户希望按“常规 / 立体 / 函数”等分类；记录当前面板尺寸常量、网格布局、36 类目录（启用/禁用、顺序、枚举）、滚动、小工作区处理、可用于分类切换的现有 UI3 模式，并给出 36 类的自然归类与圆角矩形位置（用户要求移除）。
- **Scope**: internal（只读源码与 spec；无产品代码改动）
- **Date**: 2026-11-05
- **Branch**: feature/shape（基线 b4c8d960）

## Findings

### 1. 相关文件

| 文件 | 说明 |
|---|---|
| `Inkeys/Inkeys/UI/Bar/Bar.GeometryAttribute.cpp` | 目录 `ShapeCategoryCatalog()`（:88–130）、Initialize（:180–246）、Advance 布局/滚动/fit（:277–607）、Draw（:609–685）、Execute/Pointer/Keyboard（:708–836） |
| `Inkeys/Inkeys/UI/Bar/Bar.Main.cppm:355–392` | `GeometryCategoryCount=36`、`GeometryImplementedCategoryCount=21`、派生控件索引、`ShapeCategoryDescriptor`、`GeometryAttributePresentation` |
| `Inkeys/Inkeys/UI/Bar/Bar.Main.cppm:434–500` | `BarGeometryAttributePanel` 成员：`categoryList_`/`attributeList_`、`scroll_`/`viewportHeight_`、`scrollRequest_` 原子量、`categoryFontSizes_` 等 |
| `Inkeys/Inkeys/UI/Bar/Bar.Button.cpp:2502–2537` | `ResolveBarButtonListLayout(..., GeometryGrid)`：最多 6 列、跳过 `!IsVisible()` 按钮 |
| `Inkeys/Inkeys/UI/Bar/Bar.Metrics.cppm:6–46, 200–279` | 尺寸常量与 `ResolveBarButtonVisualMetrics`（StandardOneOne/TwoOne/FourOne/TwoTwo/PageHorizontal…） |
| `Inkeys/Inkeys/UI/Bar/Bar.GeometryAttribute.Test.cpp` | 离屏测试，硬编码 36/21/59 等断言（:200、:240、:275、:767、:853 …） |
| `Inkeys/Inkeys/UI/Bar/Bar.EraserAttribute.cpp:296–320`、`Bar.EraserAttributeLayout.cppm:154–175` | 橡皮“灵敏度”子菜单：3 段等宽选中按钮（segment 模式参考） |
| `Inkeys/Inkeys/UI/Bar/Bar.Scene.cpp:413–417` | 白板/PPT 分页 prev/next 按钮用 `barMore` 旋转 ±90° |
| `Inkeys/Inkeys/UI/Bar/Bar.ShapeEditor.cpp:240–290` | 浮动编辑栏：SingleRow 属性 list、边数 `sideList`、色板 `swatches`（多个独立 list 并列） |
| `inkStrokeModelerTest/draw3/shape_core.h:34–35` | `PresetKind` 枚举（21 个，含 RoundedRectangle） |
| `inkStrokeModelerTest/draw3/shape_interaction.inl:62–79` | `GetToolCapabilities` / `DefaultCreationMode` |
| `Inkeys/IdtState.cpp:2376, 2460`、`IdtState.h:47` | 旧 `ShapeModeSelectEnum::IdtShapeRoundedRectangle1` ↔ `PresetKind::RoundedRectangle` 映射 |
| `Inkeys/src/i18n/*.jsonc` `GeometryAttributes` 节点 | 类别标签（zh-CN 为基准，共 20 个语言文件含 RoundedRectangleLabel） |
| `Inkeys/Inkeys.rc` | 名称型 `UI` 资源登记 SVG（`barShapeRoundedRectangle` 等） |

### 2. 尺寸常量与当前面板展开尺寸

`Bar.Metrics.cppm`：
- `BarButtonGapDip = 5`、`BarButtonOneSideDip = 32.5`、`BarButtonTwoSideDip = 32.5*2+5 = 70`
- `BarButtonTwoTwoIconSizeDip = 28`、`IconOffsetY = -10`、`LabelOffsetY = 20`、`LabelHeight = 25`、`LabelFontSize = 13`
- `BarButtonDisabledContentOpacity = 0.30`、`BarMainBarCornerRadiusDip = 8`

`Bar.GeometryAttribute.cpp:310–325`（Advance）：

```cpp
const double g = BarButtonGapDip, smallSide = BarButtonOneSideDip, gap = 10;
const double targetWidth = (std::max)(categoryLayout_.width + g * 3 + smallSide, attributeMaxWidth_);
const double parameterHeight = circle || polygon ? smallSide + g * 2 + 1 : 0;   // 43.5
const double fixedHeight = g * 4 + 1 + smallSide;                               // 53.5
...
const double targetViewport = (std::clamp)(availableHeight / (std::max)(0.001, widthFit) - fixedHeight - parameterHeight,
    BarButtonTwoSideDip, categoryLayout_.height);
const double targetHeight = targetViewport + fixedHeight + parameterHeight;
```

代入当前 36 项（6 列 × 6 行）：
- 类别网格：宽 `6*70+5*5 = 445`，高 `6*70+5*5 = 445` DIP
- 右侧一列 `g + smallSide`（关闭按钮 + 滚动轨道），面板宽 `445 + 15 + 32.5 = 492.5` DIP
- 属性 SingleRow（3 样式 + 分隔 + 4 线型 + 分隔 + 3 粗细）宽：`5 + 10*(32.5+5) + 2*(1+5) = 392` DIP（`attributeMaxWidth_`，:239），小于 492.5，故宽度由类别网格决定
- 面板高：无参数 `445 + 53.5 = 498.5` DIP；圆/正多边形/棱柱/棱锥带参数 `+43.5 = 542` DIP（与归档研究一致）

垂直结构（:397–452）：`top+g` 起类别视口（高 = viewport）→ `g` → 1 DIP 分隔线 → `g` → 参数行（可选，32.5 + 分隔线）→ 属性行锚定 `panel.bottom - g - smallSide`（底部 5 DIP）。关闭按钮在右上 `{left+width-g-smallSide, top+g}`；滚动轨道在关闭按钮下方同列 `{left+width-g-smallSide, top+g+smallSide+g … top+g+viewport}`（:400）。

按行数推算（供分类后参考，均为未缩放 DIP，仅代入现有公式）：

| 每类项数 | 列数(min(6,n)) | 行数 | 网格宽×高 | 面板宽 | 面板高（无参数/带参数） |
|---|---|---|---|---|---|
| 3 | 3 | 1 | 220×70 | max(267.5, 392)=392 | 123.5 / 167 |
| 4 | 4 | 1 | 295×70 | max(342.5, 392)=392 | 123.5 / 167 |
| 6 | 6 | 1 | 445×70 | 492.5 | 123.5 / 167 |
| 8 | 6 | 2 | 445×145 | 492.5 | 198.5 / 242 |
| 14 | 6 | 3 | 445×220 | 492.5 | 273.5 / 317 |
| 15 | 6 | 3 | 445×220 | 492.5 | 273.5 / 317 |

注意：GeometryGrid 的列数为 `min(6, 可见数)`，项数少于 6 时网格变窄，宽度回落到属性行 392 DIP；不同分类间切换会改变面板宽度（`width_` 有共享 SetTar 动画，:329）。

### 3. 网格布局实现（GeometryGrid）

`Bar.Button.cpp:2518–2537`：

```cpp
if (kind == BarButtonListLayoutKind::GeometryGrid)
{
    // 图形类别自有排列：标准2×2按钮按行布置，最多六列；没有更多的分组/溢出语义。
    const auto count = std::count_if(buttons.begin(), buttons.end(), [](const auto& button) { return button && button->IsVisible(); });
    const int columns = (std::min)(6, (std::max)(1, static_cast<int>(count)));
    ...
    for (const auto& button : buttons)
    {
        if (!button || !button->IsVisible()) continue;
        ... placement.x = column * twoStep + two / 2; placement.y = row * twoStep + two / 2;
        layout.placements.push_back(std::move(placement)); ++index;
    }
```

- `IsVisible()` = `userVisible && !hide`（`Bar.Button.cppm:101`）。GeometryGrid 天然支持“只排列可见子集”；`Bar.GeometryAttribute.Test.cpp:207` 已有 subset 布局测试。
- 但当前 `categoryLayout_` 只在 `Initialize()` 解析一次（`Bar.GeometryAttribute.cpp:211–212`），之后 Advance 每帧对每个按钮执行 `b.hide = !drawn`（:543），类别项 `wanted` 恒为 true（:466）。`slots[i]` 依赖 `categoryLayout_.placements[i]` 与按钮下标一一对应（:408–413）；若改为子集布局，placements 下标不再等于目录下标——这是现有代码的隐含假设。
- 属性 list 则是“每次显隐变化重新 Resolve + attributeX_ 共享动画重排”的范例（:431–453），可作为“分类切换后重排类别项”的已有模式参照。

### 4. 36 类目录（顺序、枚举、启用状态）

`ShapeCategoryCatalog()`（`Bar.GeometryAttribute.cpp:91–128`），`preset` 为 `std::nullopt` 即禁用（共享 Disable 状态、0.30 透明度、hit 不可用、键盘跳过、Execute 拒绝 :714）。

| # | key | 中文 | PresetKind | 状态 |
|---|---|---|---|---|
| 0 | line | 直线 | Line | 启用 |
| 1 | arrow | 箭头 | Arrow | 启用 |
| 2 | rectangle | 矩形 | Rectangle | 启用 |
| 3 | roundedRectangle | 圆角矩形 | RoundedRectangle | 启用（用户要求移除） |
| 4 | circle | 圆 | Circle | 启用（圆心/外接双模式参数行） |
| 5 | ellipse | 椭圆 | Ellipse | 启用 |
| 6 | triangle | 三角形 | Triangle | 启用 |
| 7 | parallelogram | 平行四边形 | Parallelogram | 启用 |
| 8 | rhombus | 菱形 | Rhombus | 启用 |
| 9 | trapezoid | 梯形 | Trapezoid | 启用 |
| 10 | regularPolygon | 正多边形 | RegularPolygon | 启用（边数参数行 + SVG 轮播） |
| 11 | polyline | 折线 | Polyline | 启用（逐点） |
| 12 | polygon | 多边形 | Polygon | 启用（逐点） |
| 13 | arc | 圆弧 | Arc | 启用（两步） |
| 14 | sector | 扇形 | Sector | 启用（两步） |
| 15 | angle | 角度 | — | 禁用 |
| 16 | parallel | 平行线 | — | 禁用 |
| 17 | perpendicular | 垂直线 | — | 禁用 |
| 18 | cuboid | 长方体 | Cuboid | 启用（两步） |
| 19 | cube | 正方体 | Cube | 启用（两步） |
| 20 | cylinder | 圆柱 | Cylinder | 启用（两步） |
| 21 | cone | 圆锥 | Cone | 启用（两步） |
| 22 | prism | 棱柱 | Prism | 启用（两步 + 边数参数行） |
| 23 | pyramid | 棱锥 | Pyramid | 启用（两步 + 边数参数行） |
| 24 | numberLine | 数轴 | — | 禁用 |
| 25 | cartesian | 直角坐标系 | — | 禁用 |
| 26 | grid | 网格 | — | 禁用 |
| 27 | coordinatePoint | 坐标点 | — | 禁用 |
| 28 | parabola | 抛物线 | — | 禁用 |
| 29 | reciprocal | 反比例 | — | 禁用 |
| 30 | hyperbola | 双曲线 | — | 禁用 |
| 31 | sine | 正弦 | — | 禁用 |
| 32 | cosine | 余弦 | — | 禁用 |
| 33 | tangent | 正切 | — | 禁用 |
| 34 | exponential | 指数 | — | 禁用 |
| 35 | logarithmic | 对数 | — | 禁用 |

合计启用 21（= `GeometryImplementedCategoryCount`，`Bar.Main.cppm:363`），禁用 15。注意归档 PRD 写的是“11 可用 / 25 禁用”，现已因 P3/P4 推进到 21/15；spec `rendering-and-ui.md:2458, 2468` 仍写“11 已实现、25 禁用占位”。

业务枚举 `PresetKind`（`shape_core.h:34–35`）：`Line, Arrow, Rectangle, RoundedRectangle, Circle, Ellipse, Triangle, Parallelogram, Rhombus, Trapezoid, RegularPolygon, Polyline, Polygon, Arc, Sector, Cuboid, Cube, Cylinder, Cone, Prism, Pyramid`。`GetToolCapabilities`：`enabled = preset <= Pyramid`；`points` = Polyline/Polygon；`multiStage = preset >= Polyline`；`regularSides` = RegularPolygon/Prism/Pyramid（决定是否出现边数参数行，:304）。

派生控件索引（`Bar.Main.cppm:363–368`）：类别 0–35，`CircleCenter=36`、`CircleBox=37`、`SidesBegin=38`（10 个，3–12 边）、`StyleBegin=48`（3）、`PatternBegin=51`（4）、`ThicknessBegin=55`（3）、`CloseButton=58`、`GeometryControlCount=59`。全部由 `GeometryCategoryCount` 派生，改目录数会整体平移。

依赖目录下标/数量的硬编码点：
- `Bar.GeometryAttribute.cpp:482`：`i == 10 && tool.preset != PresetKind::RegularPolygon` —— 正多边形轮播按目录下标 10 判定。移除圆角矩形（下标 3）后正多边形下标变为 9，此处需同步。
- `Bar.GeometryAttribute.Test.cpp:240`：`catalog.size() == 36 && GeometryImplementedCategoryCount == 21 && GeometryControlCount == 59`；`:200` `grid.subColumns == 12 && grid.totalRows == 12`；`:275`/`:767` 文案“fifteen disabled”；`:853` 按 `GeometryImplementedCategoryCount - 1` 次右移键盘导航。
- `Bar.GeometryAttribute.cpp:826–834` 键盘聚焦时把类别滚入视口，基于 `f.items[focus]` 与 `scrollOffset`。

### 5. 滚动行为（A 方案已实施）

- 视口：`targetViewport` 夹在 `[BarButtonTwoSideDip(70, 至少一行), categoryLayout_.height]`（:323–324）；`viewportHeight_` 共享 SetTar 动画。
- 偏移：`scrollMaximum = categoryLayout_.height - viewport`（:383）；worker 写原子 `scrollRequest_`，render owner 用 `scroll_` 的 `SetTar(..., EaseOutCubic)` 推进（:384–390）。
- 裁剪：`next.categoryClip = pose.Apply({left+g, top+g, left+g+gridWidth, top+g+viewport})`（:397）；Draw 内 `PushAxisAlignedClip(categoryClip)` 后只画与 clip 相交的类别（:656–659）；hit 也与 clip 求交（:549–555）。
- 滚动条：轨道 x 位于关闭按钮同列（宽 32.5 作为触控热区），thumb 宽 6 DIP、最小高 24 DIP（:398–405）；rail 4 DIP、opacity 0.08，thumb opacity 0.35（:240–244, :591–599）。
- 输入：`WM_MOUSEWHEEL` 每格滚 `70+5=75` DIP（:753–764）；轨道按下开始拖动、点击轨道跳转（:765–777）；拖动在 revision/工作区/轨道/倍率变化时失效（:737–752）；滚动未稳时禁用类别 hit（`categoriesAcceptInput`，:394, :454, :778–781）。

### 6. 小工作区处理

`Bar.GeometryAttribute.cpp:314–380`：
1. `widthFit = min(1, work.Width()/targetWidth)`。
2. 分别计算主栏上方 `aboveSpace` 与下方 `belowSpace`（主栏外 10 DIP 间距 `gap`）；默认侧 `below = owner.barState.widgetPosition.primaryBar`；完整高度放不下且另一侧更大时换侧（:319）；拖栏锁定时冻结侧（:320）。
3. 视口高度 = 可用高 / widthFit − 固定区 − 参数区，至少一行（70 DIP）；参数/属性/关闭不随类别滚动。
4. `sideWork` 为所选侧的工作区，`fit = min(1, work.Width()/width, sideWork.Height()/height)`，<1 时缩放 pose（:363–380），再 `FitEraserSurfacePose` 平移约束。
5. 换侧时 `sideSwitch_` 先半时长收起再展开（:335–345）。

### 7. 可用于“分类切换”的现有 UI3 模式

| 模式 | 位置 | 要点 |
|---|---|---|
| 橡皮灵敏度 3 段选择 | `Bar.EraserAttributeLayout.cppm:172–174`；`Bar.EraserAttribute.cpp:296–320` | 菜单宽内 3 个等宽 segment（`segment=(menuWidth-padding*2-mg*2)/3`，高 32.5）；选中项 `state=Selected`，`RetargetBarButtonInteractionVisual(..., frameColorOverride = Accent/SurfaceFrame)`，`hoverAllowed=!selected`。是当前 Bar 内最接近 segmented/tab 控件的实现。另有同菜单粗体标题 `title_`（13 DIP Bold，:368–370, :445）。 |
| 图形面板属性 SingleRow | `Bar.GeometryAttribute.cpp:221–239, 431–453` | 独立 `BarButtonListClass` + `SingleRow` 解析；显隐变化重新 Resolve，`attributeX_` 共享动画重排，分隔项（`BarButtonPresetEnum::Divider`，宽 1，高 32.5−4）。分类标签行可沿用此 list+重排机制。 |
| 图形面板参数行 | `:417–430, :676–683` | 圆模式 2 个宽按钮（twoOne 风格，图标+文字，宽 `(width-15)/2`）与 10 个边数按钮；参数区用 `parameterClip` + `PushPanelOpacity` 做高度/透明度连续展开。 |
| 浮动编辑栏多 list | `Bar.ShapeEditor.cpp:240–290, 370, 457` | `attributes`/`sideList`/`swatches` 三个独立 list，均 SingleRow；StandardOneOne 图标按钮 + 数字文字按钮。 |
| 白板/PPT 分页 | `Bar.Scene.cpp:413–417`；`Bar.Metrics.cppm:24–29` | prev/next 用 `barMore` SVG 旋转 −90/90（竖排 0/180），2×2 标准按钮；`BarWhiteboardPaginationWidthDip = 70*3+5*4 = 230`，高 `70+10 = 80`；页码 PageHorizontal/PageTwoTwo 布局。可作为“分页式分类”参照。 |
| 白板主题分组标题 | `Bar.Scene.cpp:134, 225, 422` | `headings`（2 个 `BarUiWordClass`，13 DIP）作为分区标题，非可点 tab。 |
| More 菜单 GroupedGrid | `Bar.Button.cpp:2540–2590` | 两组 + 分隔线的网格打包；spec 明确图形面板“不套用 More 的 GroupedGrid”（`rendering-and-ui.md:2461`）。 |

未找到：Bar 中没有通用的 Tab/Segmented 控件类或 `BarButtonListLayoutKind` 的 tab 布局；`BarButtonListLayoutKind` 仅 `Main, GroupedGrid, SingleRow, GeometryGrid`（`Bar.Main.cppm:296`）。

### 8. 36 类的自然归类（按几何语义 + 当前目录分段）

目录本身已按语义连续分段（归档 PRD 的 1–11 / 12–13 / 14–18 / 19–24 / 25–28 / 29–36）。按用户提出的“常规 / 立体 / 函数”及其余自然组：

| 候选分类 | 成员（目录下标） | 数量 | 启用情况 |
|---|---|---|---|
| 常规（平面图形） | 直线0、箭头1、矩形2、~~圆角矩形3~~、圆4、椭圆5、三角形6、平行四边形7、菱形8、梯形9、正多边形10、折线11、多边形12、圆弧13、扇形14 | 15（移除圆角矩形后 14） | 全部启用 |
| 作图辅助（尺规关系） | 角度15、平行线16、垂直线17 | 3 | 全部禁用 |
| 立体 | 长方体18、正方体19、圆柱20、圆锥21、棱柱22、棱锥23 | 6 | 全部启用 |
| 坐标 | 数轴24、直角坐标系25、网格26、坐标点27 | 4 | 全部禁用 |
| 函数 | 抛物线28、反比例29、双曲线30、正弦31、余弦32、正切33、指数34、对数35 | 8 | 全部禁用 |

备注（仅描述事实）：
- 常规组中直线/箭头/折线/圆弧属于开放线（`fill=false`，样式按钮隐藏，:303）；圆弧/扇形为两步构造；折线/多边形为逐点构造。
- 作图辅助 3 项也可并入“常规”或“坐标”，目录本身把它们放在平面与立体之间；坐标 4 项与函数 8 项在归档 PRD 中分别归属 P5、P6。
- 每组行数（6 列）：常规 14 → 3 行；立体 6 → 1 行；函数 8 → 2 行；坐标 4 → 1 行（4 列）；辅助 3 → 1 行（3 列）。

### 9. 圆角矩形（用户要求移除）所在位置

- UI 目录：`Bar.GeometryAttribute.cpp:95`（下标 3）。
- `ShapeToolIconResource`/`ShapeToolLabel` 的 switch 分支：`:143`、`:165`。
- 业务：`PresetKind::RoundedRectangle`（`shape_core.h:34`）及 `shape_core.cpp`、`inkStrokeModelerTestTests/shape_core_tests.cpp` 引用；`ToolSpec::rectangleCornerRadius = 4`（`shape_core.h:42`）。
- 旧状态映射：`IdtState.cpp:2376`（`IdtShapeRoundedRectangle1 → RoundedRectangle`）、`:2460`（反向）；枚举 `IdtState.h:47`。未在配置读写中发现 `ShapeModeSelect` 持久化（grep 仅命中 IdtDrawpad/IdtFloating/IdtState/Bar 交互与测试）。
- 资源：`Inkeys/src/UI/barShapeRoundedRectangle.svg`、`Inkeys.rc` 登记；i18n `GeometryAttributes.RoundedRectangleLabel`（20 个语言 jsonc + `IdtI18nKeys.g.h` + `Scripts/i18n.zh-CN.snapshot.jsonc`）。
- 若仅从 UI 目录移除：`GeometryCategoryCount` 变 35、`GeometryImplementedCategoryCount` 变 20、`GeometryControlCount` 变 58；`:482` 的 `i == 10` 轮播判断与测试中的 36/21/59 断言需同步（见 §4）。已有文档中的圆角矩形对象仍由业务层渲染/编辑（目录移除不影响 `PresetKind`）。

## Related Specs

- `.trellis/spec/native-desktop/rendering-and-ui.md:2456–2462`（P2 图形属性面板）—— 明文写“上方最多6列36类别…不增加分类导航”；分类方案属于对此合同的变更，需更新 spec。
- `.trellis/spec/native-desktop/rendering-and-ui.md:2466–2473`（后续目录与局部滚动 A 方案）—— 目录唯一来源、禁用同源资格、视口/滚动/命中/拖动失效合同、Geometry* 派生索引。
- `.trellis/spec/native-desktop/shapes.md`（P2/P3+P4 合同）—— 业务 `PresetKind` 与 UI 目录分离。
- 归档任务 `.trellis/tasks/archive/2026-10/10-17-shape-future-toolbar-ui/`（prd/design/research）—— 36 类清单来源、A/B 方案取舍、尺寸 492.5×498.5/542。

## Caveats / Not Found

- 未运行构建/测试/GUI；表中面板尺寸为按现有公式代入，未经截图验证。
- 未找到通用 Tab/Segmented 控件；最接近的是橡皮灵敏度 3 段按钮。
- 归档 PRD 与 spec 中“11 可用/25 禁用”已过期，代码现为 21/15。
- 业务层是否需要移除 `PresetKind::RoundedRectangle`（涉及文档兼容/历史对象）未在本研究中判断，仅列出引用点。
