# 参数化图形基础与通用渲染

【P1 基线；2026-10-17】P1只提供内部能力。后续P2与P3/P4合同见下文，范围按各轮用户批准的任务PRD。

## 模型与所有权
- 共享核心为 inkStrokeModelerTest/draw3/shape_core.h/.cpp，namespace draw3::shapes，不依赖Windows、D3D或UInk。ShapeObject的16字节ID与revision独立于contentId、undoId、历史索引。
- Kind、CreationMode、ShapeStyle独立；Compound为有序部件。冻结Registry统一Validate/BuildGeometry/BuildRenderData/HitTest/ControlPoints。
- 生产InkStroke持有optional ShapeObject；通用StoredInkType::Shape=7，旧0..6数值保留。不可变GeometryResult随最终项缓存，普通笔不走图形生成。
- InkCanvas::AppendShape仅接收Create命令，经纯ApplyCommand校验后追加一个逻辑历史项。编辑命令的用户交互/运行期历史接线留Phase3。
- 旧四形适配保留兼容渲染；所附Shape必须与旧端点/样式一致。UUID失败拒绝本次创建。超出共享核心可表示范围的旧有限坐标仍可沿原有无参数fallback读取，不冒称完整参数化编辑。
- 几何像素单位沿现有画布约定；曲线屏幕误差≤0.25px。圆只接受等比几何变换。简单凹多边形支持，无孔洞/自交填充；椭圆描边采用误差受控路径避免细长端部近似距离失真。
- 兼容例外：旧矩形Geometry.radius保存名义4 DIP，BuildGeometry按BuildOptions.displayScale转换并钳制半短边；通用矩形radius是画布像素。调用旧矩形命中/控制点查询时须传当前DPI比例。

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
- SelectionOverlay由Window Service及已有RenderPipeline/Bar worker管理，处于Drawpad之上、Bar之下，与PPT同层；没有独立窗口线程或RTS。按成功呈现的Frame命中，零alpha热区的44DIP触达须有原生输入支撑。圆形视觉默认12DIP，旋转32.5DIP，热区至少44DIP；优先旋转、专属点、通用缩放、对象本体。
- 浮条复用标准按钮/独立SingleRow属性list及既有调色内容，只改当前对象；进入主栏工具配置结束编辑。三档宽度来自已有策略；不引入细调滑块/数字框/复制删除翻转组合。小工作区采用受限可滚动扩展区，保持按钮尺寸。主栏自身图形目录、属性list、底部5DIP留白不变。
- 多笔创建首笔冻结工具/样式/ID/显示上下文，真实Up确认参数，预测只重建当前未确认步骤。中间态不入InkCanvas/历史/L2持久层。固定两步最后Up自动提交；折线至少2点、多边形至少3个有效简单多边形顶点后Complete闭合，首点不重复保存。Cancel/Fatal未完成步骤不能物化成圆或矩形降级对象。
- 主栏提示是Bar场景的独立状态，复用原居中提示位置/外观/动画，包含必要Complete/Back/Cancel；不得覆盖原bottomDockIndicator状态或新增HWND。全局Undo在构造中执行Back，结束后恢复文档Undo。
- 控制点先改typed Construction真值，再统一生成几何/边界/命中/render。缩放保持线宽；圆/规则多边形/正方体保约束，非等比局部缩放不引入错切，退化编辑不提交。新箭头头长=3倍线宽，总宽同头长，短线不压缩；读取v2旧箭头保留旧规则。
- 新增10模板依次追加枚举，21工具启用。Angle/Parallel/Perpendicular、坐标工具与函数15项仍禁用。圆弧扫角由真实轨迹连续展开，预测不得累积转角；空间图形只描边，可见边所选线型、隐藏边虚线，同材质段求覆盖并集。P4仍有后置条目，不能整阶段提前完成。

## 编辑历史、修订与持久化补充

- `InkCanvas::ReplaceStroke/ReplaceShape`在同索引替换，稳定对象ID、层序不变。页面统一operation journal/cursor管理Append与Replace，替换项保存语义before/after，不另建Shape专用撤销栈。普通笔追加不因编辑机制增加全页扫描。
- Preview在排除原项的相同层位重放候选；提交需候选GPU有序恢复成功，再安装CPU/journal。失败恢复原画面并保留权威恢复请求，不发布候选UI真值。旧、新及上次预览tile共同失效，淘汰创建hot preimage及不适用的composition缓存。
- revision使用页持有的不可复用运行期分配器，覆盖预览、Undo/Redo重物化及新分支；不能直接把语义快照旧revision交缓存。页面/文档权威安装必须清Shape renderer cache，关闭旧会话并推进身份；异步PPT/冷页恢复也不能绕过。
- UInk外层保持10；新增typed construction写`inkeysShape v3`并兼容v1/v2，Compound根/引用wire版本严格一致，标准几何承担可显示降级。白板历史v3保存journal/cursor/floor、隐藏redo、最近Clear前完整区间与后继continuation，兼容旧v1/v2。Clear前历史快照截断递归；恢复前候选全分支校验，损坏不改原状态。
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
