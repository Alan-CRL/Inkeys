# Research: 各图形编辑控制点交互现状与改造落点

- **Query**: 逐图形梳理当前编辑控制点（端点/顶点/专属点/外框8点缩放/旋转）、构造真值映射、圆心半径模式半径点、椭圆焦点、圆角矩形移除影响、空间图形关键点、现有编辑逻辑问题
- **Scope**: internal（附少量几何数学）
- **Date**: 2026-11-05
- **基线**: 分支 `feature/shape`，HEAD b4c8d960

## 0. 相关文件

| 文件 | 作用 |
|---|---|
| `inkStrokeModelerTest/draw3/shape_core.h` | `PresetKind`(34-35)、`CreationMode`(33)、`ToolSpec`(37-44)、`Construction`(55-62)、`VertexConstruction/ArcConstruction/SolidConstruction`(45-53)、`ControlKind`(116) |
| `inkStrokeModelerTest/draw3/shape_core.cpp` | `ExpandPreset`(54-150) 平面预设展开；`BuiltinBuilder`(259-) 几何/ControlPoints；包围盒描边padding(355-359)；`Validate`(427-)；`BuildGeometry`(486-)；`MakeConstructedShape`(647-665) |
| `inkStrokeModelerTest/draw3/shape_presets.inl` | `ValidPlanarVertices`(2-14)、`ArcPoints`(15-33)、`ExpandAdvanced`(34-159) 折线/多边形/圆弧/扇形/空间图形 |
| `inkStrokeModelerTest/draw3/shape_interaction.h` | `EditHandleKind`(7)、handle id 约定注释(15)、`EditSession`、`ConstructionSession` |
| `inkStrokeModelerTest/draw3/shape_interaction.inl` | `UniformPreset`(15-20)、`LocalShapeBounds`(30-36)、`RebuildEdited`(37-51)、`GetToolCapabilities`(62-72)、`DefaultCreationMode`(73-79)、**`BuildEditHandles`(80-148)**、**`EditShape`(149-289)**、`ConstructionSession::Generate`(331-391) |
| `Inkeys/Inkeys/UI/Bar/Bar.ShapeEditor.cpp` | 选中浮层：`Hit`(300-328，优先级+22DIP半径)、本地包围盒(336-340)、外框绘制(594-603)、控制点视觉(526-540, 605-610) |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.ShapeRuntimeActions.inl` | 发布快照：编辑中以 preview 对象重建 handles(146-159) |
| `Inkeys/Inkeys/UI/Bar/Bar.GeometryAttribute.cpp` | 主栏 36 项图形目录(88-129)、图标(136-157)、标签(158-177)、轮播 `i == 10`(482) |
| `Inkeys/Inkeys/UI/Bar/Bar.Main.cppm` | `GeometryCategoryCount = 36, GeometryImplementedCategoryCount = 21`(363) |
| `Inkeys/IdtState.h` / `IdtState.cpp` | `ShapeModeSelectEnum`(IdtState.h:37-60)、`GetShapeToolSpec`(IdtState.cpp:~2368-2400)、`SetShapeToolPresetIfRevision`(~2452-2480) |
| `inkStrokeModelerTest/draw3/shape_uink_metadata.inl` | construction 编解码(107-180)、版本 1/2/3 解码(257-307) |

---

## 1. 21 个启用工具 + 15 个禁用工具

### 1.1 枚举与目录

- `PresetKind`（`shape_core.h:34-35`）共 21 值，数值即持久化值：
  `Line=0, Arrow=1, Rectangle=2, RoundedRectangle=3, Circle=4, Ellipse=5, Triangle=6, Parallelogram=7, Rhombus=8, Trapezoid=9, RegularPolygon=10, Polyline=11, Polygon=12, Arc=13, Sector=14, Cuboid=15, Cube=16, Cylinder=17, Cone=18, Prism=19, Pyramid=20`。
- `GetToolCapabilities`（`shape_interaction.inl:62-72`）：`enabled = preset <= Pyramid`（21 个全启用）；`points` = Polyline/Polygon；`multiStage` = Polyline 及之后；`fill` = 除 Line/Arrow/Polyline/Arc 外且 `< Cuboid`；`regularSides` = RegularPolygon/Prism/Pyramid。
- `DefaultCreationMode`（`shape_interaction.inl:73-79`）：Polyline/Polygon→`Points`；`>= Arc`→`MultiStage`；Circle/RegularPolygon→`CenterRadius`；其余 `Drag`。Circle 另可由 `stateMode.Shape.circleMode` 选 `Drag`（外接框）（`IdtState.cpp` GetShapeToolSpec，`ConstructionSession::Begin` 例外 `shape_interaction.inl:320`）。
- UI 目录 36 项（`Bar.GeometryAttribute.cpp:92-127`），`preset == std::nullopt` 即禁用（`Bar.GeometryAttribute.cpp:469`）。15 个禁用项：
  - 107 angle、108 parallel、109 perpendicular（目录 idx 15-17）
  - 116 numberLine、117 cartesian、118 grid、119 coordinatePoint（idx 24-27）
  - 120 parabola、121 reciprocal、122 hyperbola、123 sine、124 cosine、125 tangent、126 exponential、127 logarithmic（idx 28-35）
- 注意：目录 idx 与 PresetKind 在 0-14 一致，15 之后错位（中间插入了 3 个禁用项）。`Bar.GeometryAttribute.cpp:482` 用硬编码 `i == 10` 识别正多边形轮播。

### 1.2 每个启用工具的构造真值与派生 Kind

`Construction{tool,start,end,parameters,legacyArrow}`（`shape_core.h:55-62`）。单笔平面预设 `parameters` 为 `monostate`，顶点被编辑后变为 `VertexConstruction`（仅 Triangle/Parallelogram/Rhombus/Trapezoid 允许，`shape_core.cpp:141-149`）。

| Preset | CreationMode | start / end 含义 | parameters | 派生 Kind / 几何 | 定义处 |
|---|---|---|---|---|---|
| Line | Drag | 两端点 | — | Line(start,end) | shape_core.cpp:84-85 |
| Arrow | Drag | 杆两端点 | —（v2 旧文件 `legacyArrow`） | Compound{1:Line杆, 2:Polyline头(实线)}；头长=3×线宽 | shape_core.cpp:86-101 |
| Rectangle | Drag | 对角 | — | Rectangle，radius=min(rectangleCornerRadius, 半短边) | shape_core.cpp:102-108 |
| RoundedRectangle | Drag | 对角 | — | Rectangle，radius=短边×0.2 | shape_core.cpp:102-108 |
| Circle | CenterRadius / Drag | CR：圆心、半径点；Drag：落笔角、对角（取 max 边的正方形） | — | Circle(center, radius) | shape_core.cpp:109-119 |
| Ellipse | Drag | 外接框对角 | — | Ellipse(start=中心, end=(a,b)半轴) | shape_core.cpp:120-121 |
| Triangle | Drag | 外接框对角 | monostate / Vertex(3) | Polygon{(cx,t),(r,b),(l,b)} | shape_core.cpp:122-123 |
| Parallelogram | Drag | 外接框对角 | monostate / Vertex(4) | Polygon{(l+w/4,t),(r,t),(r-w/4,b),(l,b)} | shape_core.cpp:124-125 |
| Rhombus | Drag | 外接框对角 | monostate / Vertex(4) | Polygon{(cx,t),(r,cy),(cx,b),(l,cy)} | shape_core.cpp:126-127 |
| Trapezoid | Drag | 外接框对角 | monostate / Vertex(4) | Polygon{(l+w/4,t),(r-w/4,t),(r,b),(l,b)} | shape_core.cpp:128-129 |
| RegularPolygon | CenterRadius | 中心、首顶点；sides 3-12 | — | Polygon(sides 顶点) | shape_core.cpp:130-139 |
| Polyline | Points | 首点/末点（冗余） | Vertex(≥2) | Polyline | shape_presets.inl:43-55 |
| Polygon | Points | 同上 | Vertex(≥3，简单多边形) | Polygon | shape_presets.inl:43-55 |
| Arc | MultiStage | 圆心、起始半径点 | Arc{sweepRadians} | Polyline（按容差采样） | shape_presets.inl:56-73 |
| Sector | MultiStage | 同上 | Arc{sweep} | Polygon（采样弧 + 圆心；整圆不加圆心） | shape_presets.inl:56-73 |
| Cuboid | MultiStage | 正面矩形对角 | Solid{depth 向量, height=0} | Compound 12 条 Polyline 边（隐藏边虚线） | shape_presets.inl:89-109 |
| Cube | MultiStage | 正面方形对角（MakeConstructedShape 规整 end，shape_core.cpp:652-657） | Solid{depth} | 同上 | 同上 |
| Cylinder | MultiStage | 底面椭圆外接框对角 | Solid{depth=0, height} | Compound{前半弧, 后半弧(虚), 顶面Ellipse, 两侧母线} | shape_presets.inl:112-134 |
| Cone | MultiStage | 同上 | Solid{height} | Compound{前半弧, 后半弧(虚), 两条到锥顶的线（从(l,cy)/(r,cy)出发）} | 同上 |
| Prism | MultiStage | 底面中心、首顶点；sides | Solid{height} | 底面正多边形纵向压缩 0.5；上底+侧棱 | shape_presets.inl:135-158 |
| Pyramid | MultiStage | 同上 | Solid{height} | 底面+到 `start+height` 锥顶的侧棱 | 同上 |

多步构造（`ConstructionSession::Generate`，`shape_interaction.inl:331-391`）：
- 第一步：Arc/Sector 以 Circle(CenterRadius) 预览；Cylinder/Cone 以 Ellipse；Prism/Pyramid 在未压缩空间生成正多边形再 y×0.5（预览对象去掉 construction，366-375）；Cuboid/Cube 以 Rectangle(radius 0)，Cube 规整为正方形（358-362）。
- 第二步：Arc/Sector 用真实轨迹累计 `sweep_`（ObserveRaw 323-330）；Cuboid/Cube `depth = end - down`（第二笔的位移向量，非绝对点，387-388）；其余 `height = end.y - down.y`（389）。
- 折线/多边形：每次 raw Up 追加一点（399-405），Complete 提交。

### 1.3 编辑控制点 id 约定与通用流程

- `EditHandleKind`（`shape_interaction.h:7`）：Move, Rotate, Scale, Center, Endpoint, Vertex, Radius, Axis, Angle, Depth, Height。
- id：`0`=本体移动；`1`=旋转；`10..17`=外框 8 点缩放（顺序 左上,上中,右上,右中,右下,下中,左下,左中，`shape_interaction.inl:132`）；`100`=中心；`101`=半径；`102/103`=轴或角；`104`=深度；`105`=高度；`1000+`=端点/顶点。
- `BuildEditHandles`（80-148）：先加专属点，再按 `LocalShapeBounds`（**只取路径点，不含线宽**，30-36）生成 8 点框（`frame`），与已有点 canvas 距离 <0.5px 的框点被去重（136）；`rotate` 时在本地框上中点沿局部 -y 方向外推 `40*displayScale`（139-145）。
- `EditShape`（149-289）：
  - `id 0` 或 `id 100`：只平移 `transform.dx/dy`（159-162）——**所有 Center 句柄都等于整体移动**。
  - `id 1`：绕本地包围盒中心旋转，复合到 `transform`（163-172）。
  - `id 10..17`：以对边/对角为锚，在本地空间算 sx/sy；`UniformPreset`（Circle、RegularPolygon、Rhombus、Cube、Arc、Sector）强制等比（182-186）；sx/sy ≤0 拒绝（不能翻转）；有 construction 时映射 `c.start/c.end/vertices`，`depth*=(sx,sy)`、`height*=sy`、`rectangleCornerRadius*=sqrt(sx*sy)`（188-196）。
  - 其它专属点按 preset 改 typed construction（206-270），之后 `RebuildEdited` 走 `MakeConstructedShape` 重新派生几何并保留 transform（37-51）。
- 浮层命中（`Bar.ShapeEditor.cpp:315-326`）：半径 `22*scale`（44DIP 直径）内，rank：Rotate=0 < 专属点=1 < Scale=2；同 rank 取最近；都未命中再做本体 `HitTest`（容差 3*scale）。
- 浮层视觉：
  - 所有非旋转 handle 同一种外观：12DIP 圆，Surface 填充 + Accent 边（`Bar.ShapeEditor.cpp:529-537`），不区分中心/顶点/缩放点。
  - 外框线：`localBounds`=`BuildGeometry(local).bounds`（336-340），即**含 `线宽/2 + 1 + 容差` padding**（`shape_core.cpp:355-359`）；绘制条件 `kind != Line && != Polyline && != Circle`（594），Accent 70% 不透明度、1DIP 线、无额外间距。
  - 编辑过程中 handles 每次由 preview 对象重建（`Draw3.ShapeRuntimeActions.inl:148-159`）。

### 1.4 每个启用图形的当前 handle 集合

| 图形 | 专属点（id/kind/位置） | 8点框 | 旋转 | 浮层外框线 | EditShape 映射 |
|---|---|---|---|---|---|
| Line | 1000/1001 Endpoint = c.start/c.end（87-91） | 无 | 有 | 无 | 端点 += delta（248-251） |
| Arrow | 同 Line | 无 | 有 | **有**（Kind=Compound） | 同 Line，头部按线宽重算 |
| Rectangle | 无 | 8 点（=矩形四角+四边中点） | 有 | 有（比缩放点外扩 padding） | 缩放映射 c.start/c.end，圆角半径×sqrt(sx·sy) |
| RoundedRectangle | 无 | 8 点 | 有 | 有 | 同上，radius 固定短边 0.2 |
| Circle | 100 Center=g.start；101 Radius=g.start+(r,0)（99-102） | 无 | **无** | 无 | 100=平移；101：c.end=指针，强制 CenterRadius（209-213） |
| Ellipse | 100 Center；102 Axis=(+a,0)；103 Axis=(0,+b)（103-107） | 无 | 有 | 有（无缩放点） | 102 只取 delta.x 改 a，103 只取 delta.y 改 b，回写 c.start/c.end=中心∓半轴（214-220） |
| Triangle | 1000-1002 Vertex（112-116） | 8 点（与顶点重合者去重：上中、右下、左下被去掉） | 有 | 有 | 顶点自由移动，写 VertexConstruction（255-266） |
| Parallelogram | 1000-1003 Vertex | 8 点（右上、左下与顶点重合去重） | 有 | 有 | 拖顶点 i 时**相邻顶点 i+1** 被重算 v[i]+v[i+2]-v[i+3]（257） |
| Rhombus | 1000-1003 Vertex | 8 点（四边中点与顶点重合去重，仅剩四角） | 有 | 有 | 中心固定，对顶点镜像，另一对角线长不变并垂直（259-265）；缩放强制等比 |
| Trapezoid | 1000-1003 Vertex | 8 点（下两角去重） | 有 | 有 | 拖顶点后同底边另一点 y 同步（258） |
| RegularPolygon | 100 Center=c.start；1000 Vertex=c.end（108-111） | 无 | 有 | 有 | 1000：c.end+=delta（同时转向+缩放）（253）；边数走 SetRegularSides |
| Polyline | 1000+ 每顶点 | 无 | 有 | 无 | 顶点移动（256-266 经 VertexConstruction） |
| Polygon | 1000+ 每顶点 | 8 点（去重） | 有 | 有 | 同上 |
| Arc | 100 Center；101 Radius=中间角处；102 Angle=c.end（起点）；103 Angle=终点（92-98） | 无 | 有 | 无（Kind=Polyline） | 101 改半径；102/103 改起止角（221-239） |
| Sector | 同 Arc | 无 | 有 | 有（Kind=Polygon） | 同 Arc |
| Cuboid | 1000/1001 Endpoint=正面对角；104 Depth=c.end+depth（117-121） | 8 点（与端点重合者去重） | 有 | 有 | 端点 += delta；104：depth+=delta（240-244） |
| Cube | 同 Cuboid | 8 点（等比） | 有 | 有 | 端点拖动后 MakeConstructedShape 再规整为正方形 |
| Cylinder | 1000/1001=底面椭圆外接框对角（不在图形上）；105 Height=底面中心+(0,h) | 8 点 | 有 | 有 | 105：height+=delta.y |
| Cone | 同 Cylinder（105 在锥顶） | 8 点 | 有 | 有 | 同上 |
| Prism | 1000=c.start=底面中心（kind 标为 Endpoint）；1001=首顶点；105 Height=中心+(0,h) | 8 点 | 有 | 有 | 1000 只移中心不移首顶点，底面会转向/变形 |
| Pyramid | 同 Prism（105 在锥顶） | 8 点 | 有 | 有 | 同上 |

ControlPoints（`shape_core.cpp:279-311`，`BuildGeometry` 对正多边形另加 Center 529-532）是旧 P1 的几何控制点，**编辑浮层不使用**，浮层只用 `BuildEditHandles`。

---

## 2. 圆（CenterRadius）半径点为何只在水平方向

1. 几何只存 `Circle{start=圆心, radius}`（`shape_core.cpp:109-119`），方向信息只在 `construction.end`（CR 模式下即真实半径点）。
2. `BuildEditHandles` 不读 `construction.end`，固定把 101 放在 `g.start + (g.radius, 0)`（`shape_interaction.inl:101`），即局部 +x 方向；圆 `rotate=false` 且 `Validate` 要求圆为相似变换，transform 通常只有平移，所以总在圆心正右方。
3. 拖动时 `EditShape` 取 `found->position + delta` 作为新 `c.end`（211-212）——**数据上已记录任意方向**，半径 = 距离；但运行期每帧以 preview 对象重建 handles（`Draw3.ShapeRuntimeActions.inl:148-159`），新 handle 又落回正右方，因此视觉上半径点"贴着水平线走"，与指针脱离。
4. Drag（外接框）模式的圆编辑 101 后被改写成 `CenterRadius`（212），原外接框语义丢失。
5. 圆心 100 等同整体平移（159-162），浮层不单独绘制"圆心标记"，只有同样外观的 12DIP 圆点。

改造落点：
- handle 位置改为 `ApplyTransform(transform, c.end)`（CR 模式）即可让半径点自由；Drag 模式需决定半径点默认放哪（如 c.end 方向投影到圆上，或固定角）。
- "显示圆心"若指画布上持久显示的标记（非编辑态也可见），需要在 Compound/渲染层增加可见点，或只在编辑浮层画；属产品问题。

---

## 3. 椭圆参数与焦点

### 3.1 现有参数
- Construction：Drag，`start/end` 为外接框对角（轴对齐于局部坐标）；无专属参数。
- Geometry：`start=中心(cx,cy)`，`end=(a,b)`，a 为局部 x 半轴、b 为局部 y 半轴（`shape_core.cpp:120-121`）。a 可小于 b。
- 旋转只在 `object.transform`（编辑旋转 163-172）。`Validate` 对 Ellipse 不要求相似变换（只有 Circle/RegularPolygon/Cube 要求，`shape_core.cpp:442-444, 474`）。

### 3.2 焦点数学
- 若 a ≥ b：焦点在局部 x 轴，F± = 中心 ± (c, 0)，c = √(a² − b²)。
- 若 b > a：焦点在局部 y 轴，F± = 中心 ± (0, c)，c = √(b² − a²)。
- a = b：两焦点重合于中心（退化为圆，c=0）。
- 画布位置 = `ApplyTransform(transform, F±)`；仅当 transform 是相似变换（平移+旋转+等比）时，该点仍是画布上椭圆的真焦点。通过 UInk 导入/非等比 transform 的椭圆，局部焦点映射后不是真焦点（需要先把 transform 合并进参数或禁用焦点功能）。
- 性质：椭圆上任一点 P 满足 |PF₁| + |PF₂| = 2·max(a,b)。

### 3.3 拖动焦点改变什么（候选语义，需产品选定）

| 方案 | 固定量 | 拖 F₁ 后 | 约束 |
|---|---|---|---|
| A 对称 | 中心、长半轴 a | c = |F₁−中心|，b = √(a²−c²)；F₂ 镜像；主轴方向随 F₁ 方向改变（写入 transform 旋转） | c < a；c→a 时退化为线段 |
| B 对称 | 中心、短半轴 b | c 同上，a = √(b²+c²) | 无上界，c→0 时变圆 |
| C 非对称（定 F₂） | 另一焦点、长轴 2a | 中心 = (F₁+F₂)/2，c = |F₁F₂|/2，b = √(a²−c²)，方向 = F₂→F₁ | |F₁F₂| < 2a |
| D 园丁法（定 F₂ 与一点 P） | F₂、远端顶点或某点 P | 2a = |PF₁|+|PF₂| | 需选哪个 P |

- 所有方案若允许焦点离开当前主轴方向，都需要把方向写入 `transform` 旋转（或给椭圆 construction 增加角度参数）；当前 typed construction 只有轴对齐外接框，没有角度/焦点字段。
- 若主轴为 y（b>a），需交换语义或先规范化到 a≥b（规范化会改变 Axis 句柄 id 的语义）。
- 显示焦点的开关：状态放在哪里（对象持久字段 → 需新 construction 字段与 inkeysShape 版本；或仅编辑 UI 临时状态 → 不持久）未定。

---

## 4. 圆角矩形（RoundedRectangle）的全部触点

| 位置 | 内容 |
|---|---|
| `shape_core.h:34` | `PresetKind::RoundedRectangle` = 3 |
| `shape_core.cpp:102-108` | 与 Rectangle 共用 case，radius = 短边×0.2 |
| `IdtState.h:47` | `ShapeModeSelectEnum::IdtShapeRoundedRectangle1` |
| `IdtState.cpp:2376, 2460` | ShapeModeSelect ↔ PresetKind 双向映射 |
| `IdtState.cpp:2563-2572` | 自测遍历 `0..Pyramid` 所有 preset 必须可选且 enabled |
| `Bar.GeometryAttribute.cpp:95` | 目录项（idx 3） |
| `Bar.GeometryAttribute.cpp:143, 165` | 图标/标签 switch |
| `Bar.GeometryAttribute.cpp:482` | 正多边形轮播用硬编码目录 idx `10`（删一项后正多边形 idx 变 9） |
| `Bar.Main.cppm:363` | `GeometryCategoryCount = 36, GeometryImplementedCategoryCount = 21`；后续控件索引全部由此派生 |
| `Bar.GeometryAttribute.Test.cpp:240` | 断言 `catalog.size()==36 && Implemented==21 && GeometryControlCount==59` |
| `Bar.GeometryAttribute.Test.cpp:753-757, 1034-1036` | `for shape < GeometryImplementedCategoryCount` 直接 `static_cast<PresetKind>(shape)` 选工具 |
| `Inkeys.rc:305` | `BARSHAPEROUNDEDRECTANGLE` → `src\UI\barShapeRoundedRectangle.svg` |
| `Inkeys/src/UI/barShapeRoundedRectangle.svg` | 图标 |
| `IdtI18nKeys.g.h:156` + `src/i18n/*.jsonc:153`（20 种语言）+ `Scripts/i18n.zh-CN.snapshot.jsonc:153` | `RoundedRectangleLabel` |
| `inkStrokeModelerTestTests/shape_core_tests.cpp:277` | 预设方向无关性用例含 RoundedRectangle |
| `Draw3.DrawingController.cpp:7539`、`Draw3.ShapeRendererTests.cpp:159` | 测试遍历 `0..RegularPolygon`，包含值 3 |
| `shape_uink_metadata.inl:110, 141` | construction 以 `uint64(preset)` 写入；v2 读上限 RegularPolygon(10)，v3 上限 Pyramid(20) |

对旧文件的含义：
- inkeysShape v2/v3 中已存的 `preset=3` 必须继续被 `ShapeEnum` 接受且 `ExpandPreset` 能展开，否则 `DecodeInkeysShapeMetadata` 的 `Validate` 失败直接 `return {}`（`shape_uink_metadata.inl:298-303`），按 spec 属"损坏拒绝恢复"，而不是降级。因此**枚举值 3 不能删除或重排**（后续 Circle..Pyramid 的数值都会错位）。
- 仅从主栏移除，需要：保留 `PresetKind::RoundedRectangle` 与 `ExpandPreset` 分支；目录项删除后修正 `GeometryCategoryCount`、硬编码 `i == 10`、测试 `static_cast<PresetKind>(index)` 的假设、`GeometryImplementedCategoryCount`（"启用 21"）与 `GetToolCapabilities.enabled`（是否仍视为可创建工具）；`IdtState` 自测遍历的 enabled 断言。
- 已有圆角矩形对象的编辑：走 Rectangle 路径（Kind=Rectangle，8 点框），没有专属点，移除工具不影响编辑。可选迁移：读取时改写为 `Rectangle + rectangleCornerRadius = 0.2·短边`——但 Rectangle 的半径为固定值、缩放后不随短边比例变化，与 RoundedRectangle 行为不完全等价。
- 白板冷历史复用同一 codec（spec "白板嵌套元数据复用同codec"），约束相同。
- `ShapeModeSelectEnum` 未发现写入磁盘配置（`IdtState.cpp:1686` 只在内存工作区快照），移除 UI 项不涉及配置迁移。

---

## 5. 空间图形当前参数、handles 与候选关键点

### 5.1 现状

| 图形 | 参数 | 当前 handles | 备注 |
|---|---|---|---|
| Cuboid | start/end=正面矩形对角；depth 向量 | 1000/1001 正面对角，104 深度=c.end+depth，8 点框，旋转 | 隐藏顶点由 depth 符号决定（shape_presets.inl:100） |
| Cube | 同上，end 被规整为方形 | 同上，8 点框等比 | 1001 自由拖后再规整，句柄会跳 |
| Cylinder | start/end=底面椭圆外接框；height | 1000/1001 在外接框角（不在线上），105 顶面中心，8 点框，旋转 | height<0（向上）时底面下半弧可见 |
| Cone | 同上 | 1000/1001，105 锥顶 | 母线从 (l,cy)/(r,cy) 出发，非真切线 |
| Prism | start=底面中心，end=首顶点；sides；height；投影比 0.5 固定 | 1000 底面中心，1001 首顶点，105 顶面中心，8 点框，旋转 | 底面在 vector=(dx,2dy) 的未压缩空间为正多边形 |
| Pyramid | 同上 | 1000、1001，105 锥顶 | |

### 5.2 候选关键点（按用户"图形上关键点调形 + 外框四角缩放"）

- Cuboid：正面四角（独立改宽高/位置，等价改 c.start/c.end 的分量）；后面一个角（改 depth 向量，现 104 已存在，可改放到与 c.end 不同的可见后角）；外框四角 = 整体缩放（映射 start/end/depth）。
- Cube：正面一个角（边长，保持方形）；后角（depth）；外框四角等比缩放。
- Cylinder：底面椭圆左右端点（rx）、底面前点（ry）、顶面中心（height）；可选顶面椭圆同样显示。
- Cone：底面左右端点（rx）、底面前点（ry）、锥顶（height；若允许左右移动则需新增 apex 水平偏移参数，即斜圆锥，当前 SolidConstruction 无此字段）。
- Prism：底面首顶点（底面大小+转角，现 1001）、顶面对应顶点或顶面中心（height）；边数走浮条；底面中心若保留则应等于平移而非改形。
- Pyramid：底面首顶点、锥顶（height；斜棱锥需新增水平偏移字段）。
- 外框四角：Cylinder/Cone 映射 start/end 与 height 可正确非等比；Prism/Pyramid 非等比缩放无法由"正多边形+中心+首顶点"表示（见 6.5），需决定外框是否强制等比或引入独立 x/y 尺度参数。

---

## 6. 现有编辑逻辑问题（仅记录事实）

1. **圆半径点**：见第 2 节，handle 固定在正右方，与指针脱离（`shape_interaction.inl:101` vs `211-212`）。
2. **箭头外框不一致**：Arrow `frame=false`（90）但 Kind=Compound，浮层仍画外框线（`Bar.ShapeEditor.cpp:594`）；同理 Ellipse、RegularPolygon、Sector 有外框线却没有缩放点。
3. **外框线与缩放点不重合**：外框线用含线宽 padding 的 `geometry.bounds`（`Bar.ShapeEditor.cpp:339`，`shape_core.cpp:355-359`），缩放点用无 padding 的 `LocalShapeBounds`（`shape_interaction.inl:30-36`），点落在框线内侧 `线宽/2+1+容差` 处；目前没有设计"间距"。
4. **圆弧/扇形角度拖动**：每次 preview 都以 `before` 为基准，`change = remainder(angle − old, 2π)` 落在 [−π, π]（235），同一次拖动超过半圈时 sweep 会翻向（例如终点从 90° 拖到 300°，sweep 变为 −60° 而非 300°）；构造时用 ObserveRaw 累积，编辑时不累积。|sweep| > 2π 或 → 0 时 `ArcPoints` 拒绝（shape_presets.inl:19-20），编辑直接失败。整圆扇形时 102/103 两点重合。
5. **Prism/Pyramid 非等比缩放**：映射 c.start/c.end 后底面按新向量重新生成正多边形（shape_presets.inl:136-144），结果不是原图形的拉伸、外框不跟手；`height *= sy`。
6. **Prism/Pyramid 句柄 1000**：标为 Endpoint，实为底面中心，拖动只移中心不移首顶点，底面同时旋转缩放（248-251）。
7. **Cylinder/Cone 句柄 1000/1001**：在底面外接框角，不在图形上；且可能与 8 点框角重合被去重。
8. **Cube 端点**：1001 自由移动后在 `MakeConstructedShape` 规整（shape_core.cpp:652-657），句柄位置跳变。
9. **Cone 母线**：起点固定在 (l,cy)/(r,cy)，不是锥顶到椭圆的切点；|height| 小于 ry 时锥顶落在底面椭圆内，母线穿过底面（shape_presets.inl:128-132）。
10. **Cylinder/Cone 可见性约定**：height>0（向下延伸）时基面上半弧可见、下半弧虚线，延伸面整椭圆实线，相当于"仰视"；height<0 时相反。需产品确认是否符合教学约定。
11. **去重导致缩放点消失/不可达**：顶点与框点距离 <0.5px 才去重（136）；Triangle 去掉上中/右下/左下、Rhombus 去掉四边中点、Parallelogram/Trapezoid 去掉部分角点。顶点稍被编辑后两者相距 1-数 px 不再去重，但命中优先级让专属点永远胜过缩放点（`Bar.ShapeEditor.cpp:320`），缩放点不可达。
12. **平行四边形**：拖顶点 i 时被联动的是相邻顶点 i+1（257），而非保持对边平行的其他直觉方式（如移动整边）；需产品确认。
13. **梯形**：仅同步同底另一点的 y（258）；把上底拖过下底会自交，`SimplePolygon` 拒绝后本次编辑静默无效。
14. **三角形/多边形**：顶点拖成共线或自交均被 `Validate` 拒绝（shape_core.cpp:209-210），无提示。
15. **菱形**：等比缩放（UniformPreset），非等比被禁；四边中点框点因去重消失，只剩四角。
16. **矩形圆角随缩放增长**：`rectangleCornerRadius *= sqrt(sx*sy)`（195）对所有 preset 执行；普通矩形放大后圆角变大，缩小后再被半短边钳制。
17. **Line 有旋转句柄**：rotate 默认 true（86），Line/Arrow/Polyline/Arc 都有旋转钮。
18. **所有 Center(100)=平移**：与拖本体相同（159-162），不存在"只移圆心改形"的语义（Prism 的中心反而用 1000 改形，语义不统一）。
19. **椭圆 Axis 句柄**：只在 +x、+y 两侧，拖动只用投影分量（217）；无外框缩放、无焦点。
20. **所有 handle 同一外观**（`Bar.ShapeEditor.cpp:529-537`），中心/顶点/缩放点/深度点无视觉区分。

---

## 7. 汇总表

| 图形 | 当前 handles | 用户期望（本次） | 待定产品问题 |
|---|---|---|---|
| Line | 2 端点 + 旋转；无外框线 | 仅 2 端点；画带间距外框，框上无点 | 是否保留旋转（期望明示"只有两个端点"→去旋转）；外框间距 DIP 值；外框对斜线是否旋转对齐线方向 |
| Arrow | 2 端点 + 旋转；有外框线无缩放点 | 未单列，推测同 Line | 是否完全同 Line |
| Rectangle | 8 点框（即四角+四边中）+ 旋转；外框线贴边 | 四角 4 个点；外框比矩形大一圈，框上无点 | 是否保留旋转；四角拖动是否自由非等比、是否允许翻转；四边中点是否完全取消 |
| RoundedRectangle | 同 Rectangle | 从工具栏移除 | 旧文件对象保持 RoundedRectangle 还是迁移为 Rectangle；编辑仍按矩形处理 |
| Circle | 圆心(=平移) + 半径点(固定正右) ；无旋转无外框 | 带间距外框；CR 模式显示圆心，半径点自由 | 圆心标记是编辑态还是常驻；拖圆心=平移还是改形（圆心移动时半径点固定？）；Drag 模式圆的半径点放哪；外框是否有缩放点 |
| Ellipse | 中心 + 两半轴点 + 旋转；外框线无缩放点 | 可选显示焦点（开关按钮），焦点可拖 | 焦点拖动语义（表 3.3 A/B/C/D）；开关是否持久化（影响 inkeysShape 版本）；焦点离轴时是否旋转椭圆；a<b 时处理 |
| Triangle | 3 顶点 + 部分 8 点框 + 旋转 | "其他图形逻辑问题修复" | 是否保留 8 点框；是否改为同矩形的"外框四角" |
| Parallelogram | 4 顶点（联动相邻点）+ 部分框 + 旋转 | 同上 | 拖哪个点联动哪个点 |
| Rhombus | 4 顶点 + 四角框（等比）+ 旋转 | 同上 | 是否允许非等比 |
| Trapezoid | 4 顶点 + 部分框 + 旋转 | 同上 | 是否限制上下底不交叉 |
| RegularPolygon | 中心(=平移) + 首顶点 + 旋转 | 同上 | 首顶点与旋转钮功能重叠是否保留旋转 |
| Polyline | 各顶点 + 旋转 | 同上 | 是否加外框 |
| Polygon | 各顶点 + 8 点框 + 旋转 | 同上 | 同上 |
| Arc / Sector | 圆心(=平移)+半径点+起止角点 + 旋转 | 同上 | 角度拖动是否累积（修复跨半圈翻向）；是否允许 >2π；是否加外框 |
| Cuboid | 正面两对角 + 深度点 + 8 点框 + 旋转 | 图形上关键点调形 + 外框四角缩放 | 关键点取哪些（5.2）；四角缩放是否等比；是否保留旋转 |
| Cube | 同 Cuboid（等比） | 同上（推断） | 同上 |
| Cylinder / Cone | 外接框两角 + 高度点 + 8 点框 + 旋转 | 同上（推断） | 是否支持斜圆锥（需新参数）；可见性约定 |
| Prism / Pyramid | 中心 + 首顶点 + 高度点 + 8 点框 + 旋转 | 同上（推断） | 外框非等比如何表示；投影比 0.5 是否可调 |

## Caveats / Not Found

- 未运行任何测试或构建，结论均来自源码阅读。
- 行号基于 HEAD b4c8d960 工作区；`IdtState.cpp` GetShapeToolSpec/SetShapeToolPresetIfRevision 行号为近似（~2368-2480）。
- 未找到 `ShapeModeSelectEnum` 落盘配置；若存在其它配置序列化路径未覆盖。
- "其他图形逻辑问题"用户未逐项指明，第 6 节为代码层面可观察到的行为差异，是否属缺陷需产品确认。
- 第 1 项要求的"15 个禁用"仅存在于 UI 目录（`std::nullopt`），共享核心无对应枚举。
