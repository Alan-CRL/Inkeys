# 第六轮设计

## 修改边界
问题分别属于共享形状几何、Draw3 绘制/编辑事务、UI3 SelectionOverlay 输入/呈现以及主栏业务工具状态。只修改本轮八项必需入口；合并 dev 带来的上游变更单独识别，不主动清理或重构其他功能。

## 远程合并
远程已 fetch 到 c754e200。相对共同祖先共有 914 个文件变化，其中包含大量 Trellis 记录；UI/Draw3 有 73 个变化文件。实施先合并并逐项解决共享控制器、Bar 与测试的冲突，再开发第六轮。保留第五轮几何和前轮样式/存储修复。合并前记录工作树和 HEAD；不覆盖用户改动，不绕过签名，不 push。

## 输入、预测与事务
SelectionOverlay 目前 ShapeEditorSurface::Pointer 接收单指鼠标式消息；真实编辑通过 ShapeUiAction 到唯一 Draw3 owner，本地 EditSession 仅服务立即显示。沿该身份/代次/Begin-Preview-Commit-Cancel 链扩展输入，真实样本与预测呈现分离。检查绘制、构造中移点及控件拖动是否使用同一预测选项和时域，避免预测结果进入历史、累计角和存储。
多指接触保留来源及身份。参考 dev CanvasPanInput/CanvasPanDynamics 的有序接触、增减指重设基线和取消语义；不要把平移惯性或目前隔离的触控板 DM 前端直接套入形状编辑。中心区单指平移，多指以接触中心/距离/夹角产生连续变换；拓扑变化重置参考但保留累计变换。3D 丢弃多指旋转分量。约束图形保持规则，线宽不随手势压力和缩放变化。一次手势只做一次 Replace。

## 样式栏与翻转
Bar.ShapeEditor 的布局、呈现、命中、键盘次序同步改两行；勾右下，两个翻转操作有专属图标和可访问名称。水平/竖直翻转以屏幕轴和对象中心定义，沿已有对象动作/revision/Replace 历史；共享核心处理几何与构造一致性，所有预设（含 3D）均验证保存重载和两次翻转还原。现有换边粘性、16 DIP 间距和跟随/换边动画合同保留。

## 面板与状态恢复
GeometryAttribute 的专属参数行同时检查当前工具和浏览分类。字体用目标字号与实测文本，检查 TransitionContent 对首次资源/标签替换的尺寸影响，不用延迟计时掩盖。
在真实进入几何状态的业务边界锁存来源；完成编辑通知由 Draw3 确认后驱动工具恢复，不能让选中对象和原工具互相清会话。点击框外和勾共用结束路径，直线/箭头维持连续绘制。未完成构造与已完成编辑使用不同状态，不吞折线加点/两步图形的下一笔。

## 文件范围（按证据收敛）
- Inkeys/Inkeys/UI/Bar/Bar.ShapeEditor.cpp、相关输入接线/头：两行布局、多指输入、预测呈现。
- Bar.GeometryAttribute.cpp 及对应测试：分类参数与文字首次展开。
- Draw3.ShapeInteraction.h、ShapeRuntimeActions/ShapeRuntime、DrawingController 及产品状态边界：动作、预测、退出通知。
- inkStrokeModelerTest/draw3/shape_interaction.inl、共享类型及既有测试：翻转/手势变换及约束。
- 现有 SVG 资源/i18n 登记：两个翻转按钮。
- 原有测试与本任务文档：跨层验证，不建立新构建系统。
具体路径以 dev 合并后的真实实现为准；编码、BOM、换行保持原样。

## dev 停机路由集成补充
隐藏焦点回归在全部功能断言通过后仍以 0xE143001D 退出。追踪确认 origin/dev 的 StopProduct 已置 productStopping，而 Host::StopInputProducer 仍需向窗口 owner 发送 TouchpadNavigation stop 并等待确认；Product.cpp 的两道消息门只放行 MouseInputOwner stop，遗漏前者。修复范围增加精确停止消息分类与两道门的放行，原 owner/attach 身份校验、超时及 FailUnprovenProducerStop 保护不变。普通导航更新不能借此通过停止门。该修复属于本轮 dev 合并后的生命周期验证，不改白板运动模型。
