# 第三轮 runtime：构造中顶点输入

## 改动与根因边界

上一轮核心已有 BeginPointEdit，生产画布 Down 也调用它，但构造阶段的不透明控制点由 SelectionOverlay 绘制；ShapeEditor Hit 却直接返回空命中，依赖 HTTRANSPARENT 继续把输入交给另一个画布入口。触控窗口本身仍接收 WM_TOUCH。只验证 canvas contact helper 不能证明点柄输入路径。

本轮将控件输入明确纳入既有 ShapeUiAction / Bridge FIFO：ConstructionBeginPointEdit / Preview / Commit / Cancel。按已呈现的 handleId 选择准确顶点，Down 保留指针相对位移，后续预览发布 generation 改变仍接受同一 Down 票据，跨场景、跨会话、错柄和重复终结拒绝。Move 复用同一 ConstructionSession，更新开放预览及 renderData，不写入文档；Up 产生会话内一个点调整步骤，取消只恢复点列。空白处的物理输入仍继续加点；物理 contact 与覆盖层手势互斥。

预览复用 Bridge 和 controller 的 latest-wins 槽，Commit 自带终点并替代旧预览，沿用原队列背压和身份屏障。核心新增 BeginPointEditAt(index,down)，避免重叠顶点被第二次最近点命中选错。主栏 Undo/Redo 打断控件手势时清理票据，迟到 Up 不会增点。

## 验证

扩展现有 RunShapeInteractionProductionTest：真实 StateBridge 入队/消费，调用与生产 dispatcher 同一动作处理函数，覆盖热区偏移、preview 合并、发布 generation 推进后的 Commit、renderData 不空、调整不 append、会话 Undo/Redo，以及迟到 Commit 拒绝；既有真实 ContactInputCoordinator 继续验证移点后加点、取消和最终单一对象提交。

尚待根任务统一 ARM64 solution 编译及运行现有无窗口测试。没有启动 GUI，也未把静态代码路径当作实机触控通过证据。

## 撤回可用性排查（未证实根因）

普通 shape 的文档提交在 DrawingController.cpp 调用 publishCurrentPageContent，早于 pendingObject / 编辑器发布；Host ObserveHistoryAvailability 立即交换 canUndo 并发布 runtime revision。主栏点击主动读 RuntimeSnapshot，未等待颜色动画。Controller 命令只等待 active.empty；结束 contact 同帧退休后再次处理命令，没有等待编辑控件动画或焦点层的条件。因此不能以“漏发布”或“等待焦点结束”为由加入无证据的时序修复。

有可疑边界但尚未复现：IdtState 把 Recall 通知放在窗口准入/可见性协调之后，白板 RuntimeSnapshot 使用 whiteboardCanUndo 副本。须结合真正失效时间序列区分可用性发布、按钮命中、命令接受和执行；不以假启用或固定等待掩盖问题。

## 完整 Host / Controller 隐藏窗口回归

扩展既有 --draw3-shape-focus-hidden-test（SF27–44），复用现有Window Service和真实Host/Run，不新增测试架构。MakeHiddenSpec为WS_POPUP、visible=false、屏外位置；style callback只更新extended style，测试没有Show入口，结束自动StopProduct+StopAndJoin，并断言窗口始终不可见。

SF27–34在editor发布同步回调读取Host.canUndo，避免只等最终true；桌面/白板分别在保留编辑器和立即CloseEditor后紧接Undo，断言命令实际执行、Redo可用和编辑器关闭，记录实测耗时。SF35–44通过真实输入Down/Up建立多边形，控件新动作经Bridge进入Run，随后Undo/Redo、继续物理加点、Complete提交。是否通过以根任务运行结果为准。

## 已确认的历史可用性修正

publishHistoryAvailability 原先只读文档历史，而 processCanvasCommands 对活动构造优先执行 ConstructionSession::Back/Forward；空白页加点/移点已有可撤回步骤时，主栏仍错误显示不可撤回。本轮统一准入来源：活动构造取 canBack/canForward，否则取既有文档历史；publishShapeUi 在状态改变时同步Host和白板快照后再发布，不递增虚假的文档revision。SF41/42检查与主栏相同的Host.canUndo/canRedo门。

首轮完整隐藏测试中Desktop立即Undo两例均成功，观测约10ms。Whiteboard失败日志admission=2048，定位为fixture遗漏真实IdtState承担的whiteboardStyle token确认；补同一token ACK及真实input-ready等待，并为隐藏测试提供构建产物旁独立持久化目录。未改产品准入条件、未放宽测试。

## 首拖等腰提示

ShapeUiSnapshot新增drawingPreview只读标志，正在绘制的等腰Triangle从active runtime发布对象和3个顶点，仅供UI绘制双斜杠；不冒充多步ConstructionStatus、不占用编辑输入、不写额外几何。SF45–47检查真实Down/Move期间提示快照可达、Up恢复普通编辑器。
