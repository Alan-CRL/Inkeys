# P3＋P4 跨层设计与职责

## 所有权
绘制线程唯一持有文档、编辑/构造会话和revision allocator。UI只消费immutable Editor/Construction snapshot并发布意图；命令沿Product→Bridge FIFO→Host→WindowController→DrawingController，不直接读写canvas。Preview允许合并但不能越Begin/Commit/Cancel/Undo/切页屏障。UI的session/对象revision/workspace/page/display身份均验证，已成功呈现坐标用于输入，失败present不发布目标。

## 共享核心接口（先由core代理落盘再由集成/Ui消费）
shape_core.h追加21类Preset及typed Construction参数，不改原枚举值；公开能力查询以统一Shape面板/编辑条显隐。新增shape_interaction.h（纯C++，无Windows）提供EditHandle/构造状态、ConstructionSession、EditSession/参数编辑与revision分配；代理必须先发精确头文件接口给主会话。约定EditHandle稳定整型id+kind+canvas position；纯编辑从before+handle/pointer delta生成after，style变更有单独重建入口，保线宽及Arrow头语义。

## 跨层UI契约
主会话负责Draw3.ShapeInteraction.h（Windows-free值类型、可引用shape_interaction.h）：UiSnapshot含snapshot/session generation、ObjectID/revision、工作区/page身份、displayScale/canvas origin、optional编辑对象与handles、构造步骤及可Complete/Back/Cancel；Action有Begin/Preview/Commit/Cancel/CloseEditor/Style/参数/Complete/Back等，携带所见snapshot/session/对象修订。Ui入口通过现有Product facade原子获取snapshot与发布action，不新增UI线程文档所有权。产品入口、Bridge/Host由主会话接线；绘制线程会话在历史接口冻结后交core代理实施。

## 分工与最小边界
core代理拥有shared shape_core/shape_interaction/metadata、shared CPU/UInk测试；UI代理拥有SelectionOverlay场景、Window service接线、Bar prompt/目录/参数与UI资源、i18n、UI工程项；renderer代理拥有RendererShapes同材质覆盖并集/缓存API/GPU测试；renderer代理在渲染子块冻结后负责InkDocument/History/Controller历史块，再冻结Controller交core代理接入会话；主会话负责Bridge/Product/Host、业务状态、全范围集成与统一构建。禁止跨所有权回退他人改动。

## 历史与缓存
统一operation journal在CanvasPageRuntimeState保存，entries含append item或同索引replace的before/after不可变InkStroke，cursor独立于render item index。Clear引用既有interval boundary。Undo/Redo语义重建当前expectedRevision+新revision命令，不能盲套原InverseCommand恢复缓存号。编辑预览和事务失败后的恢复用旧新tile union有序回放并淘汰旧hot preimage；保普通笔追加路径。

## UI与窗口
SelectionOverlay进入WindowRole所有角色数组与popup排序；RenderPipeline共用device/frame time。公共按钮/list/圆形共享shape视觉，hit仅从成功呈现发布。主栏prompt复用indicator模式但独立state和list；不抢旧bottomDockIndicator状态。虚拟屏负坐标/Display generation与Bar zoom分离。

## 兼容风险
UInk v2严格map-size/preset上限需v3，Compound根/引用版本必须统一；旧Arrow rule须显式保留。cold history v3须含journal，读旧版本转旧追加历史。只有对象外观保存不等于冷页undo保存。UI新窗口无可见GUI测试，纯排序sink/生产场景离屏验收，真机手感另列。

## 实际实现补充

- `Draw3.ShapeInteraction.h`为产品UI门面；快照使用atomic shared_ptr，刷新回调仅唤醒既有RenderPipeline。回调清除在返回前等待在途回调，避免退出悬空。
- 同一手势Down身份被冻结，Preview更换对象revision后Commit仍验证原身份；非手势属性与构造提示操作验证当前成功快照，不能用旧提示回退新步骤。
- 最近Clear额外保留CPU前区间历史（截断递归）；只保可见UInk几何会丢失编辑操作，无法满足Clear恢复后逐次撤销。PPT存在编辑journal的warm页保留CPU历史，不能仅导出visible再卸载。
- 新增工具沿用现有图形状态槽：开放路径（含折线/圆弧）使用直线槽，闭合与空间几何使用矩形槽；填充能力按preset查询，空间几何强制描边但保留平面填充记忆。
