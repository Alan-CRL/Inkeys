# 穿透按钮再次点击恢复记忆工具（2026-10-14）

继续feature/mode原任务，基线d444f6a7，工作区干净。本轮不commit/push/finish/archive，不GUI/Computer Use；保持原编码/BOM/CRLF和最小diff。

## 最新用户需求
- 在穿透状态再次点击穿透按钮，取消穿透，恢复已记忆的那个工具模式。覆盖原PRD中重复点击穿透无动作的按钮合同。
- 已有记忆是lastAnnotationMode的Pen/Shape/Eraser，具体笔型、橡皮/图形属性继续使用既有字段；退出保留墨迹，不恢复进入前的Freeze。
- 不改变合并式/分离式切换映射、选择清空、PPT/白板门禁、隐藏按钮/Recall动画及配置/i18n。
- 需求针对用户按钮。显式进入穿透的程序调用若用于幂等期望，应保留其语义；按钮toggle使用统一状态入口与锁/修订/过期回调保护，避免两次快照导致竞态。

## 实施与检查
- 实现代理拥有必要IdtState/Bar接线与现有生产状态/离屏测试，可新写research/clickthrough-toggle-implementation.md；主会话拥有本轮任务文件/spec/统一构建运行。
- 覆盖Pen、Shape、Eraser进入→再点退出→再点重入；既有具体属性不丢；墨迹/历史不被Clear；Freeze保持关闭；实际按钮接线与桥接selectionMode/版本刷新正确。
- 使用现有无窗口生产状态机与Bar离屏回归，不引入测试专用公共API或新构建系统。
- 代码冻结后trellis-check审查，主会话完整Debug|ARM64 solution构建、运行必要无窗口测试，保存结果。设置/i18n未改变时不重复设置回放。
