# 调试入口迁移与调试页内容调整

## 目标与授权
2026-10-09 用户明确要求实施已审阅的完整方案。直接当前 fluent 工作区，父任务 setting-winui3-design-rebuild 保持 in_progress。此授权不包含 GUI、Computer Use、commit、push 或任务归档。

## 需求
- 侧栏底部调试文字入口及其空间移除；标题栏顺序版本、主题、Debug、最小化、最大化、关闭。
- Debug 使用现有 Bug 图标，无文字无 tooltip；主题 tooltip 删除。Debug 有悬停/按下/选中反馈。
- Debug 始终可见，包括 IDT_RELEASE，32x32 DIP；版本/主题向左让位，caption按钮不移动；拖动、窄窗让位与系统命中正确。
- Debug 路由复用现有流程，关闭导航浮层，重复点击仍在当前页。
- Debug 选中时无侧栏蓝条，清除旧锚点/位移补间；回到普通页蓝条直接归位，之后普通切页动画保留。
- 四个控制台输出按 TouchArea、PptCOM、Draw3、Cursor 顺序移至调试页“控制台输出”，保留国际化、ID、配置和FIFO保存。IDT_RELEASE不隐藏这四项；其他诊断宏保持。
- 下方“显示器与 EDID”矩形主题卡片完整保留当前拓扑和监视器字段，来自同一Snapshot；快照不可用保持提示。
- 卡片按真实字体/换行动态高度，内部不滚动；用户确认整页可以滚动。缩放、宽度、数量变化重测。

## 验收
- ARM64原生MSBuild完整Solution Debug|ARM64构建成功，PATH workaround同一PowerShell，超时至少5分钟。
- 现有 --no-window 测试扩展覆盖几何/蓝条连续帧/EDID卡片和整页滚动，多倍率浅深模式。
- 配置迁移和IDT_RELEASE静态核对，i18n check，编码/BOM/换行与git diff --check通过。
- 不GUI、commit/push；人工拖动和视觉呈现另待用户验收，不将无窗口结果宣称为实机通过。
