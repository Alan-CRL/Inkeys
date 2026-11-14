# 参数化图形基础与通用渲染

【P1 基线；2026-10-17】P1只提供内部能力。后续P2与P3/P4合同见下文，范围按各轮用户批准的任务PRD。

## 模型与所有权
- 共享核心为 inkStrokeModelerTest/draw3/shape_core.h/.cpp，namespace draw3::shapes，不依赖Windows、D3D或UInk。ShapeObject的16字节ID与revision独立于contentId、undoId、历史索引。
- Kind、CreationMode、ShapeStyle独立；Compound为有序部件。冻结Registry统一Validate/BuildGeometry/BuildRenderData/HitTest/ControlPoints。
- 生产InkStroke持有optional ShapeObject；通用StoredInkType::Shape=7，旧0..6数值保留。不可变GeometryResult随最终项缓存，普通笔不走图形生成。
- InkCanvas::AppendShape仅接收Create命令，经纯ApplyCommand校验后追加一个逻辑历史项。编辑命令的用户交互/运行期历史接线留Phase3。
- 旧四形适配保留兼容渲染；所附Shape必须与旧端点/样式一致。UUID失败拒绝本次创建。超出共享核心可表示范围的旧有限坐标仍可沿原有无参数fallback读取，不冒称完整参数化编辑。
- 几何像素单位沿现有画布约定；曲线屏幕误差≤0.25px。圆只接受等比几何变换。简单凹多边形支持，无孔洞/自交填充；椭圆描边采用误差受控路径避免细长端部近似距离失真。
- 兼容例外：旧矩形Geometry.radius保存名义4 DIP，BuildGeometry按BuildOptions.displayScale转换并钳制半短边；通用矩形radius是画布像素。调用旧矩形命中/控制点查询时须传当前DPI比例。旧导出圆角为min(4×dpiScale,半短边)且文件不存dpiScale；无元数据导入按100%~400%标准缩放档位（1/1.25/1.5/1.75/2/2.25/2.5/3/3.5/4）容差匹配识别旧矩形，其余圆角保留为通用矩形（20等非档位值不得改写）。

## GPU合同
- 新shapeVertexShader/shapePixelShader/shape.hlsli独立于原ink与Win7 guarded程序。资源ID307/308；沿现有FXC临时副本/CSO/RC链。
- GpuPrimitive为64 bytes，align16，占t0中四个16-byte InkPoint槽。p0/p1/p2/p3位于0/8/16/24，pathStart/pathLength位于32/36，halfWidth/radius位于40/44，kind/flags位于48/52，padding位于56。所有偏移有C++静态断言。
- 独立b4=64 bytes，screen+bufferOffset+pass为首16bytes，color偏移16，origin/dashOffset/dashKind偏移32，rect偏移48。原b0..b3与旧32-byte primitive保持不变。
- 每材质MAX/MIN覆盖并集后有序叠加Add+Retain*Below，不对不同颜色直接MAX。临时材质层和对象聚合层在设备/尺寸变化时释放重建；同步失败前不改调用方目标。
- renderer按id/revision/displayScale缓存派生RenderData，最多128项。几何或样式变化必须增加revision；临时绑定退出时解除。NO_OVERWRITE能力不足逐批DISCARD且偏移归零。

## UInk与冷历史
- 继续UInk10标准Shape(type5)，线/折线/矩形/圆/椭圆/多边形优先标准表示，旋转使用弧度；非正交变换可标准多边形降级。
- extra.inkeysShape version1保存精确身份、revision、kind、mode、geometry、transform、style及parts。复合首部件持完整根，后续只允许严格六字段version/id/revision/reference/partIndex/partCount；wire contentId连续、共同undoId，恢复为单逻辑项。
- 支持版损坏、重复ID、引用不一致、缺部件拒绝恢复；未知版本恢复标准几何并返回诊断。旧无元数据文件首次导入生成UUID，后续保存稳定。
- 桌面/PPT/白板快照均复制Shape；白板历史写v2读v1/v2，隐藏redo、Clear fallback/continuation保留精确身份。只在候选完整验证后安装。

## 验证边界
内部--draw3-shape-core-test验证共享核心及生产文档/白板历史；--draw3-shape-renderer-test使用正式renderer、嵌入资源及离屏纹理，不创建HWND。共享测试--shape-core-only、--uink-file-only覆盖模型、协议与文件事务。完整构建和真机验证状态见本轮任务validation.md，Win11离屏测试不替代Win7/Win10实机。

## P2 预设构造与活动创建合同

- `PresetKind`为11个单笔模板；`ToolSpec`分离类别/创建模式/边数，`Construction{tool,start,end}`保存局部构造真值。`MakePresetShape`生成低级几何与复合部件；Validate拒绝构造参数与派生几何漂移。控制点/命中/渲染/导出继续消费同一几何结果。
- 圆心圆与正多边形采用CenterRadius（中心到半径点/首顶点）；外接框圆采用Drag，正方形边长取max(abs(dx),abs(dy))。正多边形仅3–12边，首顶点跟随手势保持规则约束。
- 普通矩形的`rectangleCornerRadius`在Bridge为DIP，Down时按同代SystemScale转换一次，construction中为局部画布像素，factory钳制半短边。轮廓不随样式/线型切换改变。圆角矩形独立，半径短边1/5。
- 箭头是一个Compound，杆按所选线型，头为实线Polyline。头部segment携带GpuPrimitive.flags bit16(kPrimitiveForceSolid)，同色覆盖在同一批次求并集，不能拆成source-over造成半透明接缝加深。64字节GPU布局和旧shader协议不变。
- Bridge::Tool/DrawingTool仅追加Shape，完整发布ToolSpec/ShapeStyle；DIP只在Down转一次。活动contact捕获不可变完整快照与稳定ID，预测仅替换预览端点，raw Up重新生成最终对象，一次Create进入一个逻辑历史项。新退化图形或Cancel不留项。
- 仅shape-family→shape-family允许后续Down采用当前工具，已有接触不变；普通Pen/Eraser原批次锁政策保持。不得让旧primitive枚举与新模板描述跨代组合。
- 带construction的inkeysShape写v2，原对象仍可v1，读v1/v2。复合reference版本必须匹配根。UInk维持10与标准几何降级，白板嵌套元数据复用同codec；变换应用于对象transform，不能只移动派生geometry而遗漏construction。
- 回归必须覆盖生产Coordinator/modeler/raw-Up/history/cold-redo、全部预设与样式、活动RenderData对比最终带viewport变换重放、标准文件恢复和旧四图形像素基线。仅CPU的工厂测试不能替代生产接线验证。
- 产品接线必须包括`Draw3.Product::PublishProductState`绘制属性白名单的shapeTool/shapeStyle，不能只加Bridge字段和Host消费。通用PS覆盖率使用`SV_Position.xy + shapeOrigin`像素中心，禁止以投影插值坐标代替；全画布与tile需保持逐字节一致。


## P3＋P4 当前对象编辑与多笔构造合同

- 仅刚完成的Shape自动进入编辑，不搜索/重选旧对象，普通墨迹不进入。多接触全部结束后采用最后真实Up的完成对象；空白下一笔直接沿画布创建。原`selectionMode`仍是电脑交互模式，不得改成对象选择开关。
- 绘制线程唯一持有`ConstructionSession`/`EditSession`与文档，UI通过`Draw3.ShapeInteraction.h`取不可变快照、发带身份意图。会话、对象/页面、工作区、文档、显示代次均须校验。手势Down身份被冻结；Preview更换revision后仍用同一身份Commit；独立属性/Back/Complete必须匹配当前代次。取消屏障在活动接触期间也须生效，不能等最后Up提交后才处理。
- SelectionOverlay由Window Service及已有RenderPipeline/Bar worker管理，处于Drawpad之上、Bar之下，与PPT同层；没有独立窗口线程或RTS。按成功呈现的Frame命中，零alpha热区的44DIP触达须有原生输入支撑。控制点为共享 `DrawBarSliderThumb` 的 WinUI3 Thumb（外径20DIP，内芯12/14/10，中心类点8/10/6），旋转钮外径28DIP、距 frameRect 顶边24DIP、1.5DIP连接线；外框为 frameRect 上的2DIP、r6 圆角框；立体外框角缩放点画成沿圆角的L形角标（4/5DIP）。热区至少44DIP且不可见（alpha≤1）；优先旋转，其次专属点与外框角标取最近者（相等取专属点），最后对象本体。视觉细节以 `11-05-shape-ux-edit-visual/design.md` 为准。
- 浮条复用标准按钮/独立SingleRow属性list及既有调色内容，只改当前对象；进入主栏工具配置结束编辑。三档宽度来自已有策略；不引入细调滑块/数字框/复制删除翻转组合。小工作区采用受限可滚动扩展区，保持按钮尺寸。主栏自身图形目录、属性list、底部5DIP留白不变。
- 多笔创建首笔冻结工具/样式/ID/显示上下文，真实Up确认参数，预测只重建当前未确认步骤。中间态不入InkCanvas/历史/L2持久层。固定两步最后Up自动提交；折线至少2点、多边形至少3个有效简单多边形顶点后Complete闭合，首点不重复保存。Cancel/Fatal未完成步骤不能物化成圆或矩形降级对象。
- 主栏提示是Bar场景的独立状态，复用原居中提示位置/外观/动画，包含必要Complete/Back/Forward/Cancel；不得覆盖原bottomDockIndicator状态或新增HWND。全局Undo/Redo在构造中执行Back/Forward，结束后恢复文档Undo/Redo。
- 旧Cube解码后可能仍持有非正方形raw对角；句柄、深度锚点与编辑均使用从start沿原方向取max(|dx|,|dy|)的正方形角点，零位移不得改变可见几何。Cuboid/Cube深度手势在EditSession::Begin锁定后角；Candidate的editDepthAnchor仅供句柄视图，跨x/y零点不换角，Commit清除，不入codec或journal。
- 控制点先改typed Construction真值，再统一生成几何/边界/命中/render。缩放保持线宽；圆/规则多边形/正方体保约束，非等比局部缩放不引入错切，退化编辑不提交。新箭头头长=3倍线宽，总宽同头长，短线不压缩；读取v2旧箭头保留旧规则。
- 控制点模型（11-05 edit-interaction）：`BuildEditHandles(shape, options, &frameRect)` 由 `shape_interaction.inl` 的预设表驱动（布局、是否旋转钮、外框是否有角点、等比与否），取消8点框与边中点。`frameRect` 为本地坐标中含描边包围盒外扩8 DIP（按 displayScale 和对象轴向缩放换算），UI 只绘制、不自算。只有立体图形的外框四角有缩放点（id 10/12/14/16）；旋转钮（id 1）仅矩形、椭圆、三角形、平行四边形、菱形、梯形、折线、多边形和立体图形。id 约定：0 本体移动，100 中心（整体移动），101 半径，102/103 轴或角，104 深度，105 高度，106/107 椭圆焦点 F1/F2（仅对象带 Foci 辅助位时出现），1000+ 端点/顶点/角点。无效拖动在预览中钳制到最近有效状态，`EditSession::Commit` 对仍无效的释放返回空，不写历史和 journal。圆弧/扇形角度编辑按轨迹累计（`EditSession` 保存累计角，UI 意图携带 `angleDelta`，跨 latest-wins 合并保留）；外接框模式的圆首次拖半径点时转为 CenterRadius。立方体/长方体深度手势在开始时锁定后角；旧非方形 Cube 按可见正方形角点规范化。
- 新增10模板依次追加枚举，21工具启用。Angle/Parallel/Perpendicular、坐标工具与函数15项仍禁用。圆弧扫角由真实轨迹连续展开，预测不得累积转角；空间图形只描边，可见边所选线型、隐藏边虚线，同材质段求覆盖并集。P4仍有后置条目，不能整阶段提前完成。

## 编辑历史、修订与持久化补充

- `InkCanvas::ReplaceStroke/ReplaceShape`在同索引替换，稳定对象ID、层序不变。页面统一operation journal/cursor管理Append与Replace，替换项保存语义before/after，不另建Shape专用撤销栈。普通笔追加不因编辑机制增加全页扫描。
- Preview在排除原项的相同层位重放候选；提交需候选GPU有序恢复成功，再安装CPU/journal。失败恢复原画面并保留权威恢复请求，不发布候选UI真值。旧、新及上次预览tile共同失效，淘汰创建hot preimage及不适用的composition缓存。
- revision使用页持有的不可复用运行期分配器，覆盖预览、Undo/Redo重物化及新分支；不能直接把语义快照旧revision交缓存。页面/文档权威安装必须清Shape renderer cache，关闭旧会话并推进身份；异步PPT/冷页恢复也不能绕过。
- UInk外层保持10；新增typed construction写`inkeysShape v3`并兼容v1/v2，Compound根/引用wire版本严格一致，标准几何承担可显示降级。白板历史v3保存journal/cursor/floor、隐藏redo、最近Clear前完整区间与后继continuation，兼容旧v1/v2。Clear前历史快照截断递归；恢复前候选全分支校验，损坏不改原状态。冷页items须一一引用不同strokes；未被引用的strokes是「画布已追加、历史追加失败」的孤儿，读取时丢弃并压紧索引，不得整页失败；items多于strokes、越界、重复引用仍拒绝。
- 编辑、编辑Undo/Redo即使hasContent不变也发mutation/内容修订，触发既有保存请求；PPT有编辑历史的warm页不能仅用visible导出替代CPU历史。桌面/PPT跨进程只恢复既有文件撤销边界，不额外恢复编辑框或完整编辑栈。
- 验证必须覆盖生产Controller的raw/predicted/Cancel/Fatal/多接触与同层编辑路径、真实UI3离屏及GPU，而非仅纯工厂。无HWND测试不能替代Win7/Win10/实体笔触屏与真实DWM层叠人工验收。


## P3 审查与离屏验证防回归

- Host每条scene-stamped动作都会前导/恢复场景；重复SetWorkspace/SetPresentationTarget/SetPage是声明，不能无条件结束会话或增加documentGeneration。绑定PPT→isolated即使workspace相同也是真实切换。活动接触取消屏障与正常命令共用同一真实场景判断。
- Redo Clear不能仅看`LastRedoItem()`：还有待重做的Replace时必须先重做编辑，operation cursor到尾才允许Clear continuation。冷历史actual strokes的Shape ID必须唯一，RenderItem到strokeIndex必须一一覆盖；operation before/after中重复同一ID则是合法语义快照。
- Commit/Cancel/Close等终结意图被QueueFull拒绝时不能静默丢失。UI在既有surface tick中只在pending期间续接重试，按场景/session/种类有界合并，跨页面/会话清理旧意图。Close可在同session对象的后续generation执行；Begin/属性/Back/Complete仍严格验证当前状态。
- 标准按钮应先加载SVG并启用，再Configure公共布局；否则icon槽可保持0宽高。StandardOneOne默认没有文字槽，3–14边数需明确启用正常字体/尺寸的文字槽。控件使用BarButtonPressScale/PressCurve/ReleaseCurve，不能另写局部按压比例。
- Shape颜色为0xRRGGBB，UI调色组件/ContentSolidBrush使用COLORREF，跨适配层显式转换。ContentSolidBrush返回共享缓存画刷：取下一种颜色前须完成上一种绘制，不能保存两个同源指针后才Fill/Stroke。选中沿用已有colorSelect徽标而非修改共享资源。
- 离屏fixture必须像生产一样每次呈现前Ensure当前bounds的device resources，并核ULW receipt与target像素尺寸；打开扩展区后沿用旧target会把颜色/文字裁掉。成功alpha/几何命中不证明SVG或文字可见，须检查实际内容像素并有空白/选中底块负例。固定颜色SVG的抗锯齿测试采用资源自身的真实背景，不能拿父按钮底色套混色公式。
- 构造提示沿原pill水平锚点，但新面板高度不等于旧12pt提示高度；正常工作区须按实际panelHeight留主栏上方5DIP间距，不能直接沿旧pill的top/center混用产生重叠。小空间动作正常尺寸换行，参数扩展区可滚动。

## 编辑焦点层与预览合并（2026-11-05，11-05-shape-ux-edit-performance）

- 预览合并：同一 session/gesture 的 `PreviewEdit` 在 Bridge 中按 latest-wins 覆盖；反向扫描遇到任何非图形命令即停止，不能越过 Undo、Clear、翻页等命令。绘制线程用 `ShapePreviewSlot` 每帧最多执行一次预览。Begin、Commit、Cancel、Close 与属性意图都保持 FIFO 且不丢弃。Commit 携带最终点，可以替换尚未执行的预览。
- 焦点层：刚完成并自动进入编辑的 Shape（批内最后一笔、没有其他活动接触）在抬笔时只写 CPU 文档、journal 和自动保存，像素画在 L1，不进 L2；每帧按 L2+L1 合成。预览、属性修改、Commit 只清除并重画 L1 中这一个对象，不复制画布，不清缓存，不重放 tile。多接触时退回旧路径。
- 失焦：所有触发统一走 `EndCanvasFocus(reason)`，先关闭编辑器，再落定（L1→L2 一次）或丢弃 L1。
  - 落定：新的 Down（包括橡皮）、工具或选择模式切换、Clear、对其他项的 Undo/Redo、场景/页面/工作区切换、视口平移（在 SetViewport 之前）、PPT 放映动作、Esc/CloseEditor、窗口最小化。
  - 丢弃 L1：翻页、尺寸变化、设备丢失、视口刷新。此时 L2 从 CPU 文档重建，必须把 rasterState 推进到 after 令牌。
  - 编辑中 Undo 该对象：丢弃 L1，L2 不变。
  - 抬笔时跳过的撤回快照，在落定时补拍（仅限未被编辑过的对象）。
  - 可信 L2 快照在焦点层期间不刷新，落定后立即刷新。
  - 「L2 尚未包含的对象」只由绘制线程持有（页面 GUID + 索引），必须是 Z 序后缀，当前最多一个。不能放进会被撤回快照复制的 `CanvasPageRuntimeState`。
  - 回退开关 `ShapeFocusLayerEnabled()`：L1 绘制或落定失败时，记录日志并按文档重放受影响的 tile。
- 编辑浮层：控制点和外框由 UI worker 用共享核心在本地即时计算（跟手），绘制线程回传的快照只用于校正。允许画布上的图形比控制点晚 1–2 帧。拖动期间窗口只增不减（按 256 DIP 扩大），内容不变时不 Present。重写 `Bar.ShapeEditor.cpp` 时必须保留这三点。
- 验证：`--draw3-shape-focus-hidden-test`（真实 Host、注入笔输入、整个会话零 tile 重放）；`--draw3-shape-renderer-test` 覆盖首帧和落定结果与旧路径逐字节一致，以及 4K/2000 笔基准。

## 辅助显示与 v4（edit-interaction Part B）

### 1. Scope / Trigger

对象辅助显示、工具默认值、标准降级或冷历史修改时遵守此合同；本节为代码合同，构建和运行验证由集成方执行。

### 2. Signatures

`Auxiliary : uint8_t { None=0, Center=1, Foci=2, Diagonals=4 }`；`ShapeStyle::auxiliary`；`SupportedAuxiliary(PresetKind)`、`DefaultAuxiliary(PresetKind)`；`BuildAuxiliaryGeometry(object,result,options,error,registry)`。
工具入口为 `GetShapeAuxiliary(snapshot)` / `SetShapeAuxiliaryIfRevision(bit,enabled,expectedRevision)`，经 `GetShapeStyle` 和现有 Bridge 的完整工具/样式快照锁存到新笔。

### 3. Contracts

- 圆支持Center且默认开；椭圆支持Foci且默认开；正多边形/圆弧/扇形支持Center且默认关；矩形/圆角矩形/平行四边形/菱形/梯形支持Diagonals且默认关。工厂屏蔽不支持的位。工具用 `auxiliaryChoices` 区分未选过与显式关闭，选择按位记忆；对象读取不应用工具默认值。
- 辅助几何只派生给渲染/导出，`BuildGeometry.parts` 仍只含本体，bounds包含辅助范围。点为填充Circle，半径=max(画布描边宽度,2画布像素)（下限与显示比例无关，预览、重放、导出几何确定一致），焦点位置共用 `CanonicalEllipse`；对角线沿用Dashed的4w/6w。纯填充时不派生辅助几何，但保留 auxiliary 位；切回描边恢复。辅助批次可与相邻同色同透明度的本体批次求覆盖并集，不能跨其他材质。开启矩形辅助时必须绕过旧图元路径。
- F1/F2只读取对象Foci位，不再使用临时BuildOptions开关。对象属性经 `BuildShapePropertyCandidate` 的SetStyle路径进入原Replace journal，工具默认值不受对象编辑影响。
- `inkeysShape` 非零aux写v4，根增加顶层无符号字段 `aux`（0..7），construction仍可选；零aux严格沿用原v1（无construction）/v3编码，不增加字段。读v1/v2/v3时aux恒为0。白板嵌套元数据共用此codec，外层冷历史版本不变。
- 历史BuildTiles必须同时消费本体与BuildAuxiliaryGeometry：虚线可按实线保守枚举，点按填充bounds枚举，覆盖undo与composition两种tile；不能把辅助部件加回本体HitTest。渲染dirty和焦点落定使用已含辅助范围的GeometryResult.bounds；回归以大空心矩形内部tile、跨tile圆心点及撤销/重做/冷重放逐字节像素对比验证。
- 旧四部件Cone（含0<|height|<=ry）仍按旧根的parts计数读取；标准导出先从construction生成规范对象，元数据和标准部件均取该对象，禁止用旧四部件根搭配新两部件几何。回归覆盖±ry及其内部高度的真实文件保存/重载与可见几何相等。
- UInk10按“本体部件 + BuildAuxiliaryGeometry部件”顺序写出，共同undoId、连续contentId、根/严格六字段reference版本一致。读支持版本时使用同一派生器校验总partCount，校验id/revision/index/count/version后只恢复根对象。未知版本逐标准部件恢复；无metadata的第三方标准部件也各自恢复，允许相邻共同undoId，不猜测辅助关系。

### 4. Validation & Error Matrix

v4缺aux或aux>7、重复metadata字段、引用身份/版本/计数不符、缺部件：拒绝候选；未知metadata版本：标准几何回退并返回诊断；工具过期revision或不支持的辅助位：不写工具值；旧版本缺aux：有效且保持旧外观。

### 5. Good / Base / Bad Cases

Good：椭圆Foci经SetStyle关闭，一次撤销恢复焦点与句柄，冷页也可重做。Base：旧v3圆加载后没有圆心点。Bad：读取旧文件时补默认Center，或把v4派生点另存成独立可编辑对象。

### 6. Tests Required

共享核心覆盖默认值、DIP最小点径、变换、焦点句柄、对角线虚线和本体命中排除；文件测试覆盖v1–v4、真实.uink保存读取、忽略v4后标准点/线仍存在、引用损坏拒绝；生产用例覆盖Down/Up、SetStyle单Replace/Undo/Redo、白板冷历史；renderer离屏断言交点alpha不超过单次透明度及tile一致。

### 7. Wrong vs Correct

Wrong：`loaded.style.auxiliary = DefaultAuxiliary(preset)`。Correct：旧metadata保持零值，仅新笔通过 `GetShapeStyle(snapshot)` 取得工具默认值；v4识别成功后从aux派生显示，跳过文件中的派生部件。

## 编辑浮层 V3 输入与绘制防回归

### 1. Scope / Trigger

修改 SelectionOverlay 的按压归属、Move 合并或编辑器子控件绘制时适用。

### 2. Signatures

`ShapeEditorSurface::PreservePointerTrajectory(POINT)`、`SurfaceInputEvent::preserveMoveSamples`、`ShapeEditorKeyboardOwnership::Route`、`Impl::BracketGeometry`。

### 3. Contracts

- 原生 Down 按成功呈现的命中锁定角度/旋转手柄的保留采样标记，直到捕获释放；标记 Move 不合并，仍使用容量512的同一邮箱和溢出取消。普通 Move 与 Bridge PreviewEdit 继续 latest-wins。
- 每个虚拟键首次 Down 锁定 Forward/Owned，到真实 Up 才释放；repeat/Up 不因焦点、前台或弹层变化重新判定归属。首次入队失败保持 Forward，已接管的续包拒绝也不能泄漏到前台。
- 普通按钮省略 content override，选中图标/数字跟随 Accent；色块内容保留专用覆盖。每帧将同一 lightMaterial 写入全部按钮、分隔线、色块与取色器形状；分隔线 fill/frame 每帧使用 Divider。
- 四角镜像/平移同一标准 L 路径，按圆角半径与显示倍率缓存；线宽独立传给 DrawGeometry，悬停不创建路径。设备 epoch 变化或 ReleaseDeviceResources 清缓存。

### 4. Validation & Error Matrix

积压采样跨180°：保留方向；邮箱满：现有取消流程；外部 Down 后编辑器取得焦点：repeat/Up 放行；Owned Down 后失焦：repeat/Up 消费；路径创建失败：跳过角标并在下次绘制重试。

### 5. Good / Base / Bad Cases

Good：角度原始采样保留、Bridge只留最新预览。Base：普通悬停 Move 合并。Bad：仅在 Pointer 内累计角度，却让其上游覆盖中间采样。

### 6. Tests Required

`--ui3-shape-editor-test`：暂停真实 worker 后批量提交角度采样，经 SubmitSurfaceInput/合并点重建90°至300°的210°扫角；Bridge仅余 Begin/最新Preview/Commit；Space打开真实弹层后的repeat/Up、外部Down后焦点改变；选中图标Accent、浅色按钮点光参数、原地换主题的Divider颜色及材质；稳态悬停路径创建计数不增加，资源释放后只重建一次。构建/运行须另有实际结果，源码复核不视为通过。

### 7. Wrong vs Correct

Wrong：每次按键消息重新判断焦点；每帧 CreatePathGeometry。Correct：首次Down锁归属到Up；缓存标准角标，仅用矩阵和描边线宽消费状态变化。

## 绘制步骤提示与恢复栈（11-05 construction-prompt）

### 1. Scope / Trigger
修改多笔构造、提示或绘制自动收起时适用；视觉参数以任务design.md最终稿为准。

### 2. Signatures
- `ConstructionStatus::totalSteps`：单笔1、固定多笔2、点工具0；`canForward`来自恢复栈。
- `ConstructionSession::Back()/Forward()`返回回退/恢复是否成功；`ShapeUiActionKind::ConstructionForward`追加到枚举尾部。
- `ShapeUiSnapshot::constructionContactActive`明确表达物理接触，不能从三个can字段全false反推。
- `AdvanceBarIndicatorContent`复用底栏三阶段决策；动画值与损伤仍由各渲染owner持有。

### 3. Contracts
- Back保存确认步骤；Forward后进先出恢复；确认新步骤才清空恢复栈。点工具只存被移除顶点，固定步工具保留确认候选；未确认扫角不能随恢复复活。
- Forward与Back校验当前完整身份；Cancel保留接触期屏障语义。接触期间全局Undo/Redo丢弃当前物理步骤，防止迟到Up重复确认；提示的全部按钮禁用。
- 提示与手动fold独立，fold时锚定主按钮；起笔通知在当前construction active时跳过Drawing自动收起，Eraser策略独立。
- 深浅主题沿主栏当前材质，背景和内容透明度分别消费常驻合同。提示禁用图标固定0.30；其他客户端浅色0.45不变。

### 4. Validation & Error Matrix
- 不足2个折线点/3个有效简单多边形点：Complete拒绝；不写文档或历史。
- Forward空栈、旧身份或接触中UI动作：拒绝；预览不入历史。
- QueueFull的Forward终结重试尚未接入：分类器位于本任务禁止修改的Bar.ShapeEditor.cpp，须解决范围冲突后补齐，不能宣称R2已全通过。

### 5. Good / Base / Bad Cases
Good：Back→Forward恢复同一步；Back→新确认→Forward拒绝。Base：取消后文档不变。Bad：恢复第二步预览积累的整圈扫角，或fold使提示消失。

### 6. Tests Required
共享核心和生产事务覆盖恢复/分支/闭合/一对象一历史；提示现有geometry离屏入口覆盖深浅×1/2倍率、圆弧两步、多边形点数、三阶段、显隐、键盘和手动fold；state-mode入口调用同源自动收起生产测试。截图打印绝对路径。本轮仅源码检查，未运行这些入口。

### 7. Wrong vs Correct
Wrong：用`!canBack && !canComplete`判定物理接触；把`fold`作为提示可见条件。
Correct：读`constructionContactActive`禁用全部动作；可见性取多笔会话，fold仅改变锚点。

## 实机续轮：约束、开放构造与 v5（2026-11-13）

### 1. Scope / Trigger
修改 Triangle/Trapezoid 约束、点工具、棱柱/棱锥投影/遮挡、弧跨端点或文件元数据时适用。

### 2. Signatures
- ToolSpec::constraint：PlanarConstraint { Free, Isosceles, Equilateral }；SetPlanarConstraint(before,constraint,revision,error) 产生候选，产品 SetConstraint 沿既有 Replace/journal。
- kMinRegularSides=3、kMaxRegularSides=14；SolidConstruction::baseProjection 默认 .5，有效域 [.05,1]。
- NormalizeLegacyPolygonSolid(object,error) 严格核验旧派生 parts 后返回当前规范对象。

### 3. Contracts
- 三角形默认 Free，Free/Isosceles 使用 Drag，Equilateral 使用 CenterRadius。切换固定 v1/v2 底边，只投影 apex 到中垂线；等边高度为有符号 sqrt(3)/2×底边长。后续顶点编辑持续保约束。梯形默认 Free，开启等腰固定下底、上底长度和高度并居中上底；关闭只解除约束。若编辑后局部底边倾斜，解除时把旋转归入 transform、保持所有画布顶点，并规范化回旧格式所需的局部水平底边。
- 点工具 ConfirmDrag 首拖确认 down/end 两点，首击显示零长 Line 起点；candidate 始终开放，confirmedCandidate 只承担最终校验。暂时自交不清空开放路径，Polygon 仅 Complete 闭合。Back/Forward 以物理步骤恢复首拖的两点。
- 底面投影采用未投影正多边形的局部 y 比例；Axis 位于外接椭圆前端，半径为 hypot(end.x-start.x,2×(end.y-start.y))，不能使用首顶点的纵分量，否则水平首顶点失效。改投影不改变外接圆，首顶点编辑反解投影坐标。
- 棱柱/棱锥一条边只有全部相邻面背向观察方向才用隐藏虚线；共面阈值内按可见。高度符号、大小、底面投影均进入朝向计算；不改变圆柱/圆锥约定。
- 弧/扇形首步圆周强制临时 Dashed，最终样式保留。累计角跨另一端时交换起止表示并保留原绕向；跨回可逆，Commit 消费同一累计角，零扫角/超整圆不提交。
- 非 Free 约束、13/14 边或 Prism/Pyramid 写 inkeysShape v5：tool 增 constraint，Solid 参数增 baseProjection，根仍含 aux。旧 v1–v4 默认 Free/.5；旧 v3/v4 棱柱/棱锥先按旧算法核验 parts 再规范化。未知版本仍标准几何降级；持久化枚举不重排。

### 4. Validation & Error Matrix
| 条件 | 结果 |
|---|---|
| 3–14 边、合法约束/投影 | 可创建、编辑、保存重载 |
| 15 边、跨 preset 约束、不支持的 creationMode、投影越界 | 拒绝候选 |
| 旧立体 parts 被篡改 | 拒绝，不能借规范化放宽损坏校验 |
| 点工具尚不足最小点数/暂时自交 | 开放预览保留，Complete 拒绝 |
| Fill 与辅助位同时存在 | 隐藏辅助视觉并保留位；旧混合模式仍兼容 |

### 5. Good / Base / Bad Cases
Good：自由三角形切等腰只移动 apex；水平首顶点仍可调底面轴。Base：旧三角形保持自由编辑。Bad：由初始几何恰好等腰反推模式，或直接忽略旧 parts 不匹配。

### 6. Tests Required
shape-core-only 覆盖转换/持续约束、水平底面轴、单击/首拖/开放闭合、弧跨端点和非法模式；shape renderer 实际像素覆盖起点；uink 文件/演示/自动保存覆盖 v5、旧立体规范化及标准降级；生产 shape core 覆盖单 Replace、Undo/Redo、工具默认值隔离。

### 7. Wrong vs Correct
Wrong：radiusY=2×(end.y-start.y)，或把 sides=14 写入 v3。Correct：外接圆半径派生 Axis；新增语义写 v5，旧支持版本严格核验后规范化。


## 第二轮构造、编辑与 v6（2026-11-14）

### 1. Scope / Trigger
修改形状约束、构造中移点、斜二测或闭合虚线时适用；本节替代前轮相冲突的固定顶角和新建立体投影约定。

### 2. Signatures
ToolSpec新增constraintVertex/rightSolid；SolidConstruction新增oblique/baseSize/baseRotation/projectionAngle/projectionScale/offset。MakeSolidConstruction、SolidBasePreview建立新构造；SetRightSolid走对象Replace事务。ConstructionSession::BeginPointEdit、Points发布已确认顶点。ClosedDashCoordinate(distance,perimeter,style)供CPU与标准导出共用。

### 3. Contracts
- 新立体斜二测x水平、y斜45度半长、z竖直，首笔锁定投影；模型边长与投影长度分开。Cube模型三边相等，高度只选符号。旧oblique=false不因加载改变投影。
- 三角形首次约束选择对顶点位移最小的底边，并记住constraintVertex；重复等腰循环顶角，辅助青色边仅在UI。
- 构造点编辑不增点，撤回恢复存点序列快照；物理接触取消恢复编辑前点列，不取消整个构造。每次Down从已有preview/renderData初始化，首笔Down也有可见点。
- GpuPrimitive保持64字节；flags位32表示闭合对称虚线，padding[0]为周长。四个等弧长锚点双向展开，不改变虚线周期；CPU命中与GPU相同。标准降级分八个单调半段，混合填充单独输出，根/引用partCount取实际导出数。
- v6持久化新字段及闭合虚线降级分组；v1-v5读取保留旧分组计数与严格几何核验。不能仅更新writer而遗留reader的旧partCount公式。

### 4. Validation & Error Matrix
新立体参数非有限或退化→拒绝；正形偏移非零→拒绝；Cube高宽不等→拒绝。点编辑导致非法最终多边形→保留开放预览但禁用完成。取消编辑→恢复点列；跨会话动作→拒绝。v6引用数不符→拒绝。

### 5. Good / Base / Bad Cases
Good：真实Down/Move/Up全帧有预览，最终一图形一创建历史。Base：旧立体继续按旧投影显示。Bad：只有核心Candidate非空就宣称上屏不闪；圆弧每个细分小段独立虚线相位。

### 6. Tests Required
shape-core-only、生产shape core、renderer像素、UInk/冷历史/自动保存；覆盖四侧虚线相位、GPU/CPU命中、三顶角轮换、斜形回正、Cube翻向、点编辑撤回恢复。实际执行结果见任务验证记录，不以本节作为通过证据。

### 7. Wrong vs Correct
Wrong：以屏幕深度向量冒充立方体真实边长；每个圆底细分段重新起虚线。Correct：使用模型边长派生投影；连续可见性圆弧聚合后统一计算弧长。


## 第三轮构造与私有协议合同（2026-11-14，替代冲突旧条款）

### Scope / Trigger
新建和编辑参数图形、构造中点编辑、UInk私有构造读取写入。用户明确开发期新图形未发布，删除v1–v6专门兼容；已发布直线矩形和通用标准几何路径保留。

### Signatures
`SolidConstruction { height, baseSize, baseRotation, mirrored, offset }`；`ConstructionSession::BeginPointEditAt(size_t, Vec2)`；`ShapeUiActionKind::ConstructionBeginPointEdit/ConstructionPreviewPointEdit/ConstructionCommitPointEdit/ConstructionCancelPointEdit`。

### Contracts
- v7为唯一支持的inkeysShape私有版本；solid参数数组依次height、baseSize、baseRotation、mirrored、offset。构造map四字段tool/start/end/parameters，不再写legacyArrow。未知/退役版本返回UnsupportedVersion，走标准几何，不恢复构造。
- 投影先模型Z旋转，再应用x'=x±y/(2√2)、y'=-y/(2√2)+height，height负值向上。mirrored选择横向符号；不保留俯仰和屏幕二维旋转。
- 方体首笔按外接矩形宽高分解对应短对角线：宽=|dx|+|dy|、深=2√2|dy|；镜像取dx*dy<0，四象限交换端点保持同一几何。Cube投影拟合模型正方形、高度绝对值=边长。
- 底面柄在逆投影空间同时径向等比缩放与Z旋转，长方体保持比例；圆底仅改半径。模型offset随旋转一起投影，正形锁定offset=0。
- 逐点工具的UI命中发送明确的顶点编辑动作，不依赖跨线程HTTRANSPARENT转发触控；Begin固定顶点索引和手势票据，Preview可合并，Commit/Cancel保序，调整后session仍活跃。
- 棱柱/锥3–8，正多边形3–14；三个工具会话默认6，各自记忆，不持久化工具默认值。

### Validation & Error Matrix
非有限参数、零底面/高度、Cube非等边、正形非零offset、立体二维旋转、越界棱数均拒绝。v7字段缺失或派生几何不一致为Invalid；版本非7为UnsupportedVersion。过零拖动不终止手势，退化帧不提交非法对象。

### Good / Base / Bad Cases
正常：斜棱锥Z旋转后offset同步且重载不变。基线：新立体旋转0、offset0，正形开关默认关闭。错误：为了保持旧v3外观恢复旧投影，或把圆底强行改水平长轴。

### Tests Required
核心四象限/翻向/逐边遮挡、生产Bridge动作续画、v7保存重载与历史、退役版本标准降级、已发布线矩形回归。必须分别报告CPU helper、完整控制器隐藏窗口、UI离屏及未执行的实机验证。

### Wrong vs Correct
错误：仅测试BeginPointEdit即认定触控可拖点。正确：UI呈现命中→Bridge保序→同一构造会话→实际预览→继续加点逐层断言。

## 第四轮平面方向与箱体手势（2026-11-14，替代冲突条款）

### Scope / Trigger
平行四边形首笔/拖角、箱体底面首笔、圆底投影验证。
### Signatures
沿用MakeSolidConstruction、EditShape、SolidConstruction和v7编码；无新增持久化字段。
### Contracts
- 平行四边形首末点为长对角线，上下边水平，初始水平偏斜为dx/4。拖角固定对角点，从手势before锁定两边基向量，逆对象transform后解有符号基坐标；不以屏幕X判定跨边。退化帧不替换手势参考。
- 箱体down/end是屏幕外接框角点。k=1/(2sqrt(2))；长方体水平模型边=W-H、深度=H/k；过窄框令H=min(H,max(0,W-.001))。Cube边长=min(W/(1+k),H/k)，拟合框锚定down。mirrored只取end.x<down.x，完成后底面缩放/Z旋转不变。
- 圆柱/圆锥仍使用既有半长斜二测。水平共轭直径原长，另一条45度半长；仿射后椭圆主轴倾斜本身不是错误。
### Validation & Error Matrix
零宽/零高/零面积不提交；手势继续后可恢复。Cube三边模型长度相等。窄框拟合不越出手势矩形，所有四象限相同。
### Good / Base / Bad Cases
Good：同一X在不同Y跨斜对边结果不同；Base：绕Z旋转后的立体编辑不改变；Bad：把箱体手势角点直接当内部短对角线端点。
### Tests Required
核心四象限内接/窄框/Cube等边，旋转后平行四边形两轴方向/过零回返，圆底实际折线的逆投影圆方程及共轭直径，生产renderer与保存重载。
### Wrong vs Correct
Wrong：看到椭圆长轴倾斜就强行调平。Correct：对实际轮廓验证模型圆经斜二测矩阵映射后的共轭直径。

## 第五轮圆底显示与箱体首笔（2026-11-14，替代冲突条款）

### Scope / Trigger
用户已通过可视化确认圆柱/圆锥水平长轴椭圆，短长轴比固定1/3；模型底面必须保持正圆。
### Signatures
内部SolidProjection(Construction)统一按preset返回横向剪切/纵向比例；ObliquePoint、细分、逆投影编辑、面朝向都消费同一映射。v7字段和枚举不变。
### Contracts
圆柱/圆锥先模型Z旋转，再x'=x、y'=-y/3+height；其他立体保持45度半长。圆底baseSize.x==baseSize.y；改变显示投影不改变模型圆。预览、编辑柄、隐藏边与保存重载必须一致。
箱体MakeSolidConstruction恢复e8eb2f1b短对角线首笔：x=abs(dx)，y=abs(dy)/(1/(2sqrt(2)))；baseSize=(x+abs(dy),y)，中心=(down+end)/2，mirrored=(dx*dy<0)；Cube两维=max(x,y)，height绝对值为边长。其他第四轮修复保留。
### Validation & Error Matrix
不等模型圆半径拒绝；非法/退化形状仍按现有校验拒绝。正负高度都按当前投影核判定面向，不保留旧开发期圆底投影迁移。
### Good / Base / Bad Cases
Good：实际环线满足水平3:1椭圆方程，模型半径相等；Base：棱柱/棱锥斜二测不变；Bad：把模型baseSize.y缩小以伪造水平椭圆。
### Tests Required
核心首笔/完成/半径编辑/高度/隐藏边与轮廓相切、箱体四象限旧公式；产品renderer；v7私有构造与标准部件导出重载、冷历史和自动保存。
### Wrong vs Correct
Wrong：只改ObliquePoint而仍用旧剪切逆映射或旧视线算虚线。Correct：显示/交互/可见性统一消费按preset选择的投影。


## 第六轮翻转、预测与多指编辑（2026-11-14，替代冲突条款）

### 1. Scope / Trigger
适用于完成对象的两行样式栏、翻转、多指变换、编辑预测及结束编辑恢复工具。第五轮投影、短对角线和 v7 构造保持；未完成点工具继续加点，不把下一笔当作框外退出。

### 2. Signatures
- `FlipShape(before,horizontal,revision,options,error)`；`TransformShapeGesture(before,pivot,translation,scale,rotation,revision,options,error)`。
- `EditSession::PreviewTransform(pivot,translation,scale,rotation,revision,error)`只用于整体移动柄 0/100，沿已有 Begin/Commit/Cancel。
- `ShapeUiAction::{gestureTransform,predictedGestureTransform,predictedPoint}`；`ShapeEditorSurface::Contact(message,screen,source,contactId,touch,pen,seconds,captureOwned,presentedRevision)`；`ShapeContactGesture::Key{source,id}`。
- `ShapeWidthScale(object)=sqrt(abs(det(object.transform)))`；`RestoreToolAfterShapeEditing(workspace,entryEpoch)`与`ShapeToolEntryEpoch()`防止迟到完成通知恢复新一轮工具。

### 3. Contracts
- 翻转围绕屏幕轮廓包围盒中心，水平/竖直轴明确；模型构造不变，外层 transform 承担反射。立体允许绝对值相等的有符号对角缩放，仍拒绝 m12/m21 非零；翻转后控制点继续逆同一 transform，不能重猜模型。
- 多指变换始终相对 Begin 的 before：围绕 pivot 等比缩放、平面旋转，再平移；3D 丢弃旋转分量。模型圆、正多边形和立方体约束不变；style.width 除以本次 scale，补偿渲染变换放大。浮条比较粗细用 `style.width*ShapeWidthScale`，设置预设用 `screenWidth/ShapeWidthScale`，不能把模型宽度当屏幕宽度。
- 首个有效中心区 Down 开启唯一编辑事务；额外触指只能加入该中心手势，按钮与专属控制点仍首指独占。鼠标、笔及触摸的身份不可混用，压力不参与几何或线宽。增减指重置参考，保留累计变换；非末指 Up 只预览，最后 Up 真实坐标提交一次 Replace，取消回滚且没有惯性。
- 预测继承绘制选项，真实会话先消费 raw；预测只在 EditSession/构造副本求候选，不累计真实角度，不写历史或存储。Up 清预测字段；预测到期回退真实候选，新指加入前收回旧预测。局部控件和画布使用相同预测候选。
- 点勾/已成形图形框外退出才恢复入口工具，最后绘制 Up 保留编辑；直线/箭头连续绘制。入口从穿透映射绘制；进入几何后切换类别不覆盖来源。完成通知必须匹配工作区、entryEpoch 和当前几何模式，不能覆盖用户新选工具。

### 4. Validation & Error Matrix
旧 revision、非有限变换、scale<=.001、非法构造拒绝。PreviewTransform 非 0/100 柄拒绝；退化帧可保留上个显示候选但不可 Commit。过期对象/会话/文档/显示身份取消，旧接触不能接管新手势。预测失败退回真实样本；旧入口完成通知不恢复新工具。

### 5. Good / Base / Bad Cases
Good：两指缩放后首指抬起，余指无跳变继续平移，末指 Up 仅产生一次历史；翻转后的圆底仍可调半径并保存重载。Base：预测禁用时仍正常真实拖动。Bad：第一指 Up 就提交、将预测角写入真实累计角、缩放后样式栏再次把描边放大。

### 6. Tests Required
21 预设单次镜像坐标、双次还原、翻转后控制点编辑；真实 GPU halfWidth 恒定；3D 旋转输入无效但缩放有效；完整 UInk 导出/导入保持翻转及缩放。生产动作测试覆盖预测副本/raw Up、构造移点续画、取消与单 Replace；真实 Contact/呈现接缝覆盖增减指、首指离开/末指提交、过期输入和禁用预测。工具测试覆盖选择/穿透来源及旧 epoch 通知。自动结果见第六轮验证记录，不代替真实屏幕触控验收。

### 7. Wrong vs Correct
Wrong：`style.width=screenWidth`，每指各自 Begin/Commit，或直接把 predictedPoint 传进真实会话。Correct：`style.width=screenWidth/ShapeWidthScale(object)`，整组共享一个事务，预测会话复制后只发布临时几何。
