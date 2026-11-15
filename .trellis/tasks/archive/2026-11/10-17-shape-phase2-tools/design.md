# P2 技术设计与固定合同
共享核心为纯值类型；IdtState锁内发布，Bridge完整快照，Down捕获不可变工具/样式/ID，绘制owner持有预览与提交，保存接收快照。

## 核心 API（子任务共同遵循）
namespace draw3::shapes:
enum PresetKind:uint32_t { Line, Arrow, Rectangle, RoundedRectangle, Circle, Ellipse, Triangle, Parallelogram, Rhombus, Trapezoid, RegularPolygon };
enum FillMode:uint32_t { Outline, Fill, OutlineAndFill };
struct ToolSpec { PresetKind preset=Line; CreationMode creationMode=Drag; uint32_t sides=3; float rectangleCornerRadius=4; bool operator==(...) const=default; };
ShapeObject增加optional<Construction> construction；Construction含ToolSpec tool和Vec2 start,end(局部坐标)。构造信息是真值，低级geometry/parts为派生结果，factory/Validate禁止漂移。
optional<ShapeObject> MakePresetShape(const ToolSpec&,Vec2 start,Vec2 end,const ShapeStyle&,const ObjectId&,uint64_t revision=1,std::string* error=nullptr);
style.width已是canvas宽度；rectangleCornerRadius在Bridge是DIP，仅Rectangle在Down按同代SystemScale转换一次，进入Construction后是局部canvas单位。普通矩形按该半径钳制半短边，所有样式保留旧4 DIP轮廓；v2 tool严格保存该参数，v1不变。圆心模式半径欧氏距，bbox圆取max拖距。round rect半径短边1/5；等腰三角形顶中底角；parallelogram错切宽1/4；菱形边中点；梯形上底一半；regular中心到首顶点；箭头杆按线型、头实线，同色覆盖并集。新零面积/长度返回空；旧4类退化语义保留。

## Bridge 与业务 API
Bridge::ProductState增加shapes::ToolSpec shapeTool及shapes::ShapeStyle shapeStyle(width为DIP)。追加Tool::Shape，旧值不变。Host/窗口/visualstyle同步，Down仅一次宽度转换。旧4路径保留，新增模板/样式走generic。
主会话提供IdtState.h接口：
GetShapeToolSpec(const StateModeClass&) noexcept -> shapes::ToolSpec;
GetShapeStyle(const StateModeClass&) noexcept -> shapes::ShapeStyle;
GetShapeFillMode(const StateModeClass&) noexcept -> shapes::FillMode;
IsOpenShapeTool(const StateModeClass&) noexcept -> bool;
SetShapeToolPresetIfRevision(shapes::PresetKind,uint64_t) -> bool;
SetShapeFillModeIfRevision(shapes::FillMode,uint64_t) -> bool;
SetShapeLinePatternIfRevision(shapes::LinePattern,uint64_t) -> bool;
SetShapeCircleModeIfRevision(shapes::CreationMode,uint64_t) -> bool;
SetShapePolygonSidesIfRevision(uint32_t,uint64_t) -> bool;
旧SetShapeModeSelect继续适配。Line/Arrow用直线颜色宽度槽，其余用矩形槽；闭合fill、open/closed pattern、circleMode、sides仅会话记忆，无新启动配置。

## UI 与存储
仅替换geometry子树；BarButtonClass共享按压/回弹/Advance/Draw/hit/light/damage。参数区按钮同合同。精调复用现有控件。SVG粗线/主题token/三语，轮播沿RecallChromeIdle的WM_TIMER到期只唤醒一帧模式，隐藏/选中/关闭动画/退出kill timer。
Construction写inkeysShape v2，旧对象可继续v1，读v1/v2；UInk10标准几何降级，复合一对象；白板复用codec。

## 所有权
Core：shape_core.*、shape_uink*.inl及共享core/uink测试。Draw：Draw3 Bridge/Host/WindowControl/Controller/InkPrediction/Renderer及测试。UI：Bar/资源SVG/rc/i18n/UI离屏测试。主会话：IdtState.*、项目登记、统一构建、任务/spec。新工程项报主会话，不互相覆盖。

## 输入批次例外
仅当已锁定批次工具与当前工具均属shape family，新Down使用当前完整工具/样式快照；已有contact保持Down快照。普通Pen/Eraser的原批次锁定政策不变。
