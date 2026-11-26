# UInk 1.1：Draw3 对格式的需求整理（给规范作者）

- 整理日期：2026-11-24
- 来源：Draw3 代码只读盘点（行号以当日 `refactoring/draw` 为准）、历史任务记录、用户决定。标【码】为已在代码中核实，【推】为推断。
- 用法：规范作者先读 §A 的要求清单（这是 1.1 需要满足的结论）；§1–§6 是对应的现状与依据，§7 是用户决定的原文记录。
- 路径缩写：`D3\` = `Inkeys/Inkeys/Drawing/Draw3/`，`UK\` = `inkStrokeModelerTest/draw3/`。UInk 1.0 规范存档：`.trellis/tasks/archive/2026-09/09-01-uink-file-persistence/research/uink-v10-spec.md`。

## 0. 用户已定的原则

1. 桌面、PPT、白板**同一套 UInk 逻辑**；白板与 PPT 一样是**一个文件**，单独放在自己的文件夹；各场景只扩展自己不同的部分（例如白板未来的图层）。
2. 需要的私有片段升为规范，不需要的删除。**文件读不出来就报错，不做完整性校验**（SHA-256 等全部去掉）。
3. 图形：文件里存直接可画的内容，元数据存图形属性，Inkeys 导入后可继续编辑。
4. 1.1 尽量兼容 1.0，但**允许破坏性修改**；不兼容 Inkeys3 之前的白板路径与文件（正式版未发布，PPT 跨进程恢复、历史画布预览尚未实现）。

## A. 给规范作者的要求清单

### 文件与页面

- **R1 一个逻辑文件 = 多个页（画布）**，桌面、PPT、白板共用。
- **R2 按页追加**：同一页的新版本作为完整块追加到文件末尾，读取时以该页最后出现的块为准。文件中的页顺序不保证，以页面元数据里的页码为准，由软件排序（通常有序）。导出或整理时整文件重写成有序、紧凑的文件。这改变了 1.0 §15“只能向最后一个 Canvas 追加”的规则，属于破坏性修改。中期重构第3步之后还要支持实时写入与崩溃自动恢复（<https://www.inkeys.top/standard/incremental.html>），追加块的设计需与之衔接。
- **R3 页面元数据（官方字段）**：
  - 通用：页码（排序依据）。
  - PPT：SlideID、绑定方式（按 SlideID / 按页码）、页状态（活动 / 保留，用于已删除幻灯片）、页类型（普通 / 结束页，结束页可没有 SlideID）。
  - 白板：背景颜色与图案（现为 `rgb` uint 与 `pattern` 0–2）。
  - 预留扩展，例如白板未来的图层。
- **R4 工作区类型**：白板现写 `workspaceType = 1` 外加私有标记 `inkeysWorkspace = "whiteboard"`，PPT 写 `workspaceType = 2 + hostId`，PPT 页码退化写 Inkeys 私有 `workspaceType = 128`。1.1 应让白板只靠官方类型值识别；页码退化改为 PPT 类型加 R3 的绑定方式字段，不再用私有类型值。
- **R5 页内容是 Stroke / Shape / Clear 的有序日志**，Clear（Type 6）列为正式类型。Inkeys 保证每页最多一个 Clear（保留上一个区间供撤回，更早的丢弃）；规范只需允许这种日志，不必限定数量。
- **R6 不保存重做与编辑历史**。保留 `undoId` 分组，撤回粒度由软件决定。
- **R7 不要求完整性校验**：读不出来就报错。读取库去掉全文件 SHA-256 与保存后回读比对（见 §4）。

### 笔迹

- **R8 笔尖描述（每笔）**：形状（圆 / 矩形）、宽高比、方向（固定竖直 / 跟随书写方向）、点宽度值的含义（直径 / 高度 / 等面积圆直径）。每个点仍只存一个宽度值。Draw3 的取值：

  | 笔 | 形状 | 宽:高 | 方向 | 宽度值含义 | 端帽 |
  |---|---|---|---|---|---|
  | 普通笔 | 圆 | — | — | 直径 | 圆 |
  | 荧光笔 | 矩形 | 1:8 | 固定竖直 | 笔尖高度（半高下限 0.5px） | 平头 |
  | 圆形橡皮 | 圆 | — | — | 直径 | 圆 |
  | 矩形橡皮 | 矩形 | 1:1.5 | 固定竖直 | 等面积圆直径 | 平头扫掠 |
  | 方形橡皮 | 矩形 | 1:1 | 固定 | 等面积圆直径 | 平头扫掠 |

  橡皮形状可直接用笔尖描述表达，是否另设 `eraserShape` 由规范作者决定。
- **R9 端帽与连接**：墨迹与图形描边都需要 `lineCap`（圆 / 方 / 平）与 `lineJoin`；写明零长度 dash 依赖圆帽才画得出点。
- **R10 坐标单位**：现为画布局部物理像素，文件里不记 DPI 缩放，导入时只能猜（旧矩形圆角按 4 DIP 写入）。1.1 需要记录 DPI 缩放或统一单位。

### 图形

- **R11 标准层基本不变**：每个图形写成标准 Shape 项，复杂或未知图形写成一组 Line / Polyline，任何读取器都能画。规范补写：dashArray 与 dashOffset 的单位、点线依赖圆帽、闭合虚线拆分的含义、同一对象的多项共享 `undoId`。
- **R12 对象元数据官方化**：只放在首项，含 `id`、`revision`、`preset`、`construction`、`style`、`aux`、`transform`、`partCount`；去掉重复的 `geometry` 与 `parts`，由构造派生；后续项只存对首项的引用与序号。附录写明 21 个预设的参数、立体投影常量、闭合虚线相位规则、弧细分容差（见 §2）。
- **R13 兼容**：未知键一律忽略；元数据无法识别或与标准层对不上时退回标准层，不拒绝整页。

### 删除或改为官方字段的私有片段

`inkeysWhiteboardHistory`（删除，背景主题进 R3）、`inkeysWorkspace`（→R4）、`inkeysPageState` / `inkeysBindingMode` / `inkeysPageKind`（→R3）、`inkeysEraserShape`（→R8）、`inkeysShape`（→R12）。详见 §3。

### 留给规范作者决定

- 追加块的页目录形式（文件尾目录或扫描）、废弃块与整理的约定、追加后 Header 计数的语义（1.0 已允许滞后）。
- 页删除如何表示：Draw3 目前没有删页功能，PPT 删除幻灯片用“保留页”表达。
- 兼容层（Lite / Inkeys / Full）如何划分。
- 中期重构第3步的多画布需求（共享内容源、视图绑定，见父任务 `research/第3步 …多画布与窗口架构…md` §5）尚未定案，可预留扩展位，不必在 1.1 实现。

## 1. 需要新规范支持的笔迹能力

| 能力 | Draw3 的实际语义【码】 | 现在怎么存 | 1.1 需要 |
|---|---|---|---|
| **橡皮形状** | Circle / Rectangle / Square（`D3\Draw3.SpeedEraser.h`）。同一笔内形状不变。 | 私有键 `inkeysEraserShape`（uint8，非圆形才写，`UK\uink_codec_encode.cpp:800`）；模型注释已写“1.1 升为官方字段”（`UK\uink_model.cppm:224`）。第三方按圆形擦除。 | 官方 `eraserShape` 枚举，缺省圆形。 |
| **矩形/方形橡皮尺寸** | 点 width 是“等面积圆的直径”。矩形半宽 = r×0.7236、半高 = r×1.0854（竖向，高宽比 1.5）；方形半边 = r×0.8862（r = width/2）。轴对齐、不随方向旋转，按扫掠覆盖。 | 不存，靠代码常量推导。 | **已定**：用笔尖描述表达（见 §7）。 |
| **荧光笔笔尖** | 轴对齐的竖直矩形笔尖：半高 = max(0.5, width/2)，半宽 = 半高 / 8（`kHighlighterNibAspectRatio = 8`，`D3\Draw3.HighlighterNib.h`）；平头、不随方向旋转；同一笔内近重合点（0.25px）去重。 | 只写成 1.0 的 Highlighter 类型（圆头），导出时记为“近似”（`approximatedHighlighterNibCount`）。 | **已定**：每笔一个笔尖描述（形状、宽高比、方向、宽度含义），见 §7。 |
| **端帽（cap）** | 普通笔与圆形橡皮是圆帽胶囊；荧光笔与矩形/方形橡皮是平头扫掠（`D3\Draw3.Renderer.cppm:92-96`）。 | 不存。图形描边也没有 cap/join 字段；点线图形的 dashArray 是 `{0, 3w}`，零长度段**依赖圆帽**才画得出点。 | 墨迹与图形描边都需要 `lineCap`（round/square/butt），最好也有 `lineJoin`；写明零长度 dash 的画法。 |
| 点宽语义 | x/y/width 为 float32，width 是完整直径、>0；实时笔锋已烘进每点 width，不存参数。 | 官方字段。 | 按笔类型分别写清 width 含义（普通笔 = 圆直径，荧光笔 = 笔尖高度，橡皮 = 等面积圆直径）。 |
| **Clear 区间** | 画布内容是 Stroke 与 Clear 的有序日志；当前只显示最后一个 Clear 之后的区间，之前的区间用于跨 Clear 撤回。 | Type 6 `UInkClear`（1.0 存档只列 0–5，【推】后来才补进）。区间序号由 Clear 个数推出。 | 列为正式类型。Inkeys 保证每页最多一个 Clear（见 §7）。 |
| **撤回/重做历史** | 白板保存完整 redo 与操作日志（含图形编辑的前后版本）；桌面/PPT 只有 `undoId` 分组、不存 redo。 | 白板塞在私有 JSON（见 §3）。 | **已定：文件不保存重做**，撤回粒度由软件决定（见 §7）。 |
| 虚线样式 | Solid / Dashed / Dotted / DashDot。 | 标准层只有 dashArray（`{4w,6w}`、`{0,3w}`、`{4w,5w,0,5w}`），样式枚举只在私有元数据。 | 规定 cap 后用 dashArray 表达即可（R9、R11），或另设官方 `linePattern`。 |
| 不透明度、fallbackRgb、纹理 | 块级 opacity；只写 sRGB fallback；texture 恒为 0。 | 官方字段。 | 不变（纹理保留占位）。 |

## 2. 图形

### 现状【码】

- **写入的不是普通墨迹笔画，而是 1.0 的标准 Shape 项**（Line / Polyline / Polygon / Rectangle / Ellipse / Circle，带 stroke/fill/dashArray），任何支持 Shape 的读取器都能画出外观（`UK\shape_uink_standard.inl:116-166`）。
  - 一个图形对象拆成多个标准项，共享同一个 `undoId`：箭头 = 杆（Line）+ 头（Polyline）；闭合虚线拆成 8 段单调折线（保证虚线相位对称），有填充再加 1 个填充项；立体图形的每条边、隐藏边（虚线）都是独立项。
  - 正交变换下写成原生 Rectangle/Ellipse/Circle，否则写成折线/多边形。
- **私有元数据**挂在首项的 `extra.inkeysShape`（msgpack map，`version = 7`）：`id`（GUID）、`revision`、`kind`、`creationMode`、`legacyKind`、`geometry`、`transform`（6 元）、`style`（stroke/fill/颜色/透明度/width/linePattern/dashOffset/aux）、`parts[]`、`construction{tool{preset, creationMode, sides, rectangleCornerRadius, constraint, constraintVertex, rightSolid}, start, end, parameters}`、`partIndex/partCount`。其余项只写 6 键引用 `{version, id, revision, reference, partIndex, partCount}`。
- 共 21 个预设：Line、Arrow、Rectangle、RoundedRectangle、Circle、Ellipse、Triangle（自由/等腰/等边）、Trapezoid、Parallelogram、Rhombus、RegularPolygon（3–14 边）、Polyline、Polygon、Arc、Sector、Cuboid、Cube、Cylinder、Cone、Prism、Pyramid（3–8 边，可直棱）；另有旧版四种（实线、虚线、空心矩形、实心矩形）。坐标轴、角度、平行、垂直、函数等工具不存在。
- 构造参数（`construction.parameters`）是变体：无 / 顶点列表 / 弧扫角 / 立体参数（height、baseSize、baseRotation、mirrored、offset）。**构造是编辑的真值**，几何与部件都由它重算。

### 现有问题【码】

- **冗余**：根元数据里的 `geometry` 与 `parts` 又存了一份几何（立体的每条边、弧的细分点），读取时还要与构造重算结果逐值比对（误差 4ε）。
- **过严**：`construction` 必须恰好 4 键、`tool` 7 键、引用 6 键，多一个键就判无效；extra 里出现 `inkeysShape` 以外的键也判无效；无元数据时 dashArray 不是三种之一就无法导入。这些都会让**整页导入失败**。
- **单位不一致**：坐标是画布局部物理像素（viewport.scale 恒为 1），但旧矩形圆角按 4 DIP 写入，文件里没有 dpiScale，导入时靠 DPI 档位猜。
- **只在代码里的约定**：立体投影常量（棱柱/棱锥斜二测 k = 1/(2√2)，圆柱/圆锥 y×1/3）、mirrored 与隐藏边判定、闭合虚线的 4 锚点相位展开、弧细分容差 0.25px。别的实现无法据此重建。

### 规范片段的方向（已定方向，细节由规范作者定）

1. **标准层**（任何读取器都能画）：保留“每个图形写成可直接绘制的内容”。规范写明：dashArray 与 dashOffset 的单位；点线依赖圆帽；闭合虚线拆分的含义；同一对象的多项共享 `undoId`。
2. **对象层**：升为官方字段（例如 `shapeObject`），只放在首项：`id`、`revision`、`preset`、`construction`、`style`、`aux`、`transform`、`partCount`；**去掉根里的 `geometry` 与 `parts`**，由构造派生。后续项只写 `{ref: id, partIndex}`。投影常量、相位规则等写进规范或附录。
3. **兼容规则**：未知键一律忽略；未知预设或版本、派生数量或几何不符时，退回标准层（当作普通图形），**不拒绝整页**。
4. **编辑历史**：不需要。文件不保存图形编辑的前后版本（R6）。

**用户已确认（2026-11-24）**：沿用 1.0 的标准 Shape 项作为“任何软件都能画”的一层——基础图形用对应的标准类型；复杂或读取器不认识的图形写成一组连续或不连续的线（Line / Polyline），保证都能画出来。支持再编辑的软件读取元数据。**1.1 的工作重点是扩展和规范化元数据**，标准层本身基本不变。

立体图形的现状【码】（`UK\shape_presets.inl`）：立体是 Compound 对象，按当前投影计算每条边的可见性。直边写成 Line 部件，可见边为实线，被遮挡的边带 Dashed 样式；圆柱、圆锥的底面、顶面圆周按可见性切成几段 Polyline（细分弧），同一可见性的连续弧合并成一段，避免虚线相位在细分弦上反复归零。导出时每个部件就是一个标准 Line / Polyline 项，被遮挡的边带 dashArray `{4w,6w}`。所以第三方读取器看到的就是一组实线加虚线，外观完整；投影参数、底面尺寸、高度、边数、是否直棱等可编辑信息只在元数据里。

## 3. 私有片段清单与去留建议

| 片段 | 写入 → 读取 | 内容 | 建议 |
|---|---|---|---|
| `inkeysEraserShape` | `UK\uink_codec_encode.cpp:800` → `UK\uink_codec.cpp:1341` | 每条非圆形橡皮 1 字节 | 升为官方 `eraserShape`（§1）。 |
| `inkeysShape` | §2 | 图形对象元数据 | 升为官方对象层并精简（§2）。 |
| **`canvas.extra.inkeysWhiteboardHistory`** | 写 `D3\Draw3.DrawingController.cpp:17738`，读 `:17808` | **紧凑 JSON 字符串**：背景 rgb/pattern、**全部笔画（与正文完全重复）**、历史项、操作日志（每条带完整前后笔画副本）、redo、floor、区间、上一区间的递归历史。实测 98 笔约 961 KB，解析约 90 ms（Debug）。【推】解码有 8 MiB 字符串上限，大页可能存得进去却读不回来。 | **删除**。背景主题进页面元数据（R3）；重做与操作日志不再保存（R6）；笔画只存正文一份。 |
| `canvas.extra.inkeysPageState` | `UK\uink_draw3_export.cpp:62` 所有场景的每个画布 | `"active"` / `"retained"`；PPT 用它保留已删除幻灯片的页 | 进页面元数据（R3）。 |
| `inkeysBindingMode` | PPT 画布 | `"slide-id"` / `"page-index"` | 进页面元数据（R3）。 |
| `inkeysPageKind` | PPT 结束页 | `"end-screen"`，允许结束页无 SlideID | 进页面元数据（R3）。 |
| `workspace.extra.inkeysWorkspace = "whiteboard"` | 白板 | 小标记 | 删除，只用官方类型值（R4）。 |
| PPT 官方字段用法 | — | `workspaceType = 2`、`hostId = PresentationKey`（由来源路径派生的 UUID）、`Canvas.slideId` | 保留，写进 PPT 场景说明。 |

文件外的旁路数据：

- 桌面：`index.json` / `.bak`（schema 1），记 trigger、sessionId、dailySequence。
- PPT：每条轨一份 `index.json` / `.bak`（schema 2），entry 带 sourceRevision（含 SHA-256）与 slideIds。
- 白板：`current` 文本文件（当前版本文件名），经临时文件原子替换。
- 公共：`SaveUInkFile` 写 `.tmp`，覆盖保存还会留 `.bak`。

已定：PPT 的 `index.json` 与多轨、白板的 `current` 与版本文件都去掉（§7）。桌面的 `index.json` 去留在 C1.4 设计阶段定。

## 4. 校验与完整性：去留建议

| 机制 | 位置与代价【码】 | 建议 |
|---|---|---|
| `UInkSourceRevision` 里的**全文件 SHA-256** | 每次 `ReadUInkFile` 都算（`UK\uink_file.cpp:1032-1062`）；白板 `Revision()` 同理 | **删除**。revision 中的卷序列号 + fileIndex + 长度 + mtime 用于检测外部改动或并发，可保留。 |
| 保存后回读自校验 | `SaveUInkFile`：编码 → 哈希 → 写临时文件 → 全量回读比对（`UK\uink_file.cpp:1160-1204`）；桌面之后还再读一次 `ExistingUInkMatches`。一次保存 2–3 遍全量解码 | **删除**。桌面的幂等重试身份识别改为只读 header GUID。 |
| PPT 保存前的旧版本比对 | 每次保存先完整读一遍旧文件并与索引 revision 比对（`D3\Draw3.PresentationAutoSave.cpp:1238-1245,1325-1338,1389-1400`），加载也比对（`:1572-1584`） | PPT 改为单文件追加写入后不再需要：每页最多一个 Clear，Clear 前的区间直接在页块里；跨进程恢复未实现，冲突靠命名 mutex。整条机制随统一封装删除。 |
| 白板多页文件 | 一次页保存约 4 次全文件哈希加整文件复制；每次读一页也算全文件 SHA；`TryRemoveOwnedUInkPageFile` 用 SHA 证明所有权 | 随白板改单文件追加写入整体删除（不再有多版本文件，也就不需要所有权证明）。 |
| provenance 标志（`containsInvalidCompleteBlocks`、`contentSequenceRecovered`） | 很便宜 | 映射为“读不出来就报错”。 |
| 追加计划摘要 `plan.bytesSha256` | `UK\uink_file.cpp:1672,1721`，产品未使用追加路径 | 随追加路径一起评估，未用可删。 |
| 命名 mutex、事务锁 | 桌面索引 mutex、PPT 根目录 mutex、`NamedTransactionGuard` | **保留**（纯并发控制）。 |
| 白板历史 JSON 的严格结构校验 | 解析时逐键校验；journal 整页可逆演算实测约 3 ms | 随历史 JSON 删除。 |

## 5. 文件位置与布局

### 现状【码】

- **根目录**：`<exe 所在目录>\Inkeys\AutoSave`（`Inkeys/IdtMain.cpp:4011`），不读配置、不用 `%LOCALAPPDATA%`。
- **PPT 放映期间**：`<根>\presentation\` 下分几条轨：
  - 基础轨：`presentation\`
  - 旁路轨：`presentation\slide-id\`、`presentation\page-index\`
  - 会话轨：`presentation\sessions\<会话GUID>\{slide-id|page-index}\`

  每条轨有自己的 `index.json` / `.bak`，墨迹文件在 `files\<fileGuid>_<transactionGuid>.uink`（旧 schema 1 为 `files\<fileGuid>.uink`）。一份演示文稿对应一个逻辑文件，全部幻灯片（含空页、retained 页、结束页）是同一文件里的多个画布。**每次保存都写一个新版本文件**，再原子更新索引。来源身份为 `"path:" + 规范化路径`（无路径时为 `process:…`）。保存时机：Clear、换页、离开放映、退出。
- **桌面**：`<根>\desktop\YYYY-MM-DD\HHmmssfff_<请求GUID前8位>[_n].uink`，同目录 `index.json` / `.bak`。每次 Clear 或退出写一个快照文件。
- **白板**：`<根>\whiteboard\<每进程会话GUID>\<序号>.uink` 加 `current`。一个多页文件，每页一个画布；每次页保存都生成完整的新版本文件（未改动的页按字节复制），只保留当前版与一份前版。新进程不读取旧会话。

### 统一后的方向

- 三个场景共用同一种“一个逻辑文件 = 多个画布（页）”的读写封装。
- 白板一个 .uink，放在白板自己的文件夹；PPT 一份演示文稿一个文件夹、一个 .uink；桌面各自目录。
- 白板与 PPT 按页追加写入；导出时整文件重写（见 §7）。去掉 PPT 的 `index.json`、多轨与白板的 `current`、版本文件。

## 6. 统一封装必须覆盖的场景差异

现状的差异（统一后大部分消失，留作 C1.4 设计的输入）：

1. **粒度**：桌面一次 Clear 或退出写一个快照文件；PPT 与白板改为一个文件按页追加（R2）。桌面是否也改为单文件追加，C1.4 设计时定。
2. **历史**：三者统一为“可见内容 + `undoId` + 每页最多一个 Clear”（R5、R6）；白板的重做与操作日志删除。
3. **身份与绑定**：PPT 的 SlideID、绑定方式、保留页、结束页，白板的背景主题与未来图层，统一进页面元数据（R3）；桌面的 trigger、sessionId、dailySequence 目前在索引 JSON 里，去留随桌面方案定。
4. **并发**：保留命名 mutex；PPT 的跨进程冲突检测、白板的所有权证明随多版本文件一起删除。
5. **队列策略**：都是单后台线程；PPT 的 Clear 边界保存需严格先进先出，普通保存可只保留最新。

## 7. 已定的决定（2026-11-24）

1. **图形**：沿用标准 Shape 项作为人人能画的一层（复杂或未知图形写成一组 Line / Polyline），重点扩展与规范化元数据（见 §2）。
2. **撤回与重做**：文件**不保存重做**，撤回方式和粒度由软件自己决定（文件只提供可见内容、`undoId` 分组与 Clear 日志）。
   - **每页最多保留一个 Clear**：白板或 PPT 清空后可以撤回到上一轮，所以 Inkeys 在该页写入一个 Clear，保留它之前的上一个区间；Inkeys 保证最多撤回到上一个 Clear 区间，更早的内容丢弃。每页要么没有 Clear，要么只有一个。这是 Inkeys 特有的撤回保证，规范只需允许“页内 Stroke 与 Clear 的有序日志”。
   - **手动导出**（以后支持时）只保存最后一个 Clear 之后的内容。
3. **PPT**：一份演示文稿一个文件夹、一个 .uink。绑定方式、SlideID、页码、已删除幻灯片的保留页、结束页都写进**页面元数据**，由软件自己排序和处理。去掉 `index.json`、旁路轨与会话轨。
4. **白板**：同样一个 .uink，放在白板自己的文件夹。PPT 与白板一样，对 .uink **追加写入**。
   - **UInk 1.1 允许按页追加**：同一页的新版本可以追加到文件末尾；**不保证页在文件中的顺序**，以页面元数据中的页码为准，由软件排序（通常仍是有序的）。
   - **导出时整文件重写**成紧凑、有序的文件，便于其他软件快速读取。
   - 中期重构第3步之后，UInk 读取库还会支持实时写入并在崩溃后自动恢复（见 <https://www.inkeys.top/standard/incremental.html>）。
5. **笔尖描述**：每笔存笔尖描述——形状（圆 / 矩形）、宽高比、方向（固定竖直 / 跟随书写方向）、点宽度值的含义（直径 / 高度 / 等面积圆直径）；每个点仍只存一个宽度值。荧光笔、矩形与方形橡皮都用它表达，也能表达以后不同比例的荧光笔。

### 白板翻页（已定，2026-11-24）

- **翻页丢弃重做**：白板离开一页后照常释放这一页，不为重做保留内存；翻回来后这一页没有可重做的内容。图形编辑的“改之前的版本”同样不保留，翻回来后撤回按文件里的 `undoId` 分组进行（软件决定粒度）。
- 因此白板不再需要私有历史 JSON 里的重做栈与操作日志。

### 仍需在 C1.4 设计阶段确认

- 追加写入的整理时机（废弃块占比阈值、空闲时整理）与读取时的页目录方式，随规范定稿一起设计。
