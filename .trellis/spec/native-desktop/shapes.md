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
- 主栏提示是Bar场景的独立状态，复用原居中提示位置/外观/动画，包含必要Complete/Back/Cancel；不得覆盖原bottomDockIndicator状态或新增HWND。全局Undo在构造中执行Back，结束后恢复文档Undo。
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
- 标准按钮应先加载SVG并启用，再Configure公共布局；否则icon槽可保持0宽高。StandardOneOne默认没有文字槽，3–12边数需明确启用正常字体/尺寸的文字槽。控件使用BarButtonPressScale/PressCurve/ReleaseCurve，不能另写局部按压比例。
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
- 辅助几何只派生给渲染/导出，`BuildGeometry.parts` 仍只含本体，bounds包含辅助范围。点为填充Circle，半径=max(画布描边宽度,2画布像素)（下限与显示比例无关，预览、重放、导出几何确定一致），焦点位置共用 `CanonicalEllipse`；对角线沿用Dashed的4w/6w。无描边时取填充颜色/透明度。辅助批次可与相邻同色同透明度的本体批次求覆盖并集，不能跨其他材质。开启矩形辅助时必须绕过旧图元路径。
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
