# 穿透按钮切换实现记录（2026-10-14）

## 实现
- 新增产品按钮入口 `bool ToggleClickThroughFromButton()`，与 `ChangeStateModeToClickThrough()` 共用内部 `ChangeStateModeToClickThroughImpl(bool toggle)`。显式进入仍幂等；按钮在穿透态退出并恢复既有 `lastAnnotationMode`。
- 进入/退出方向、工具身份、Target/Echo 和一次修订在既有 `stateModeTransitionMutex` 内提交；不从锁外两次快照拼接状态。
- 恢复只修改顶层身份和既有背景对比度，具体笔型、颜色、宽度、形状及橡皮配置保持原记忆。沿既有 Freeze 关闭与 SyncDraw3State 同步链，退出不恢复定格。
- Pierce 真按钮回调改用切换入口；外层交互既有 UpdateRendering 刷新显隐和工具高亮。未修改配置、i18n、布局/动画或后端画布链。

## 回归与静态检查
- 生产状态测试追加三工具入/出/重入、一击一次修订、显式进入幂等、所有既有属性及桥接投影恢复、旧Selection/PPT回调拒绝、PPT定格不可用时切换与现有白板门禁。
- 在生产桥接命令FIFO放入Undo/Redo哨兵，完整切换后仍只消费这两条原命令，验证按钮不发布Clear或改动历史队列。早期测试未启动Host，runtime内容/历史快照不变只证明隔离路径，不代替带真实墨迹的运行期验收。
- 离屏测试直接调用已注册Pierce `clickFunc`，覆盖Pen(Highlighter)、Shape(Rectangle)、Eraser的高亮恢复与反复切换；保持原稳定按钮对象和列表。
- `git diff --check --ignore-submodules=all` 与4个源码文件UTF-8/BOM/CRLF检查通过。
- 未独立编译或执行动态测试，由主会话统一Debug|ARM64 solution构建、生产状态和离屏验证。未GUI/Computer Use/commit/push/finish/archive。
