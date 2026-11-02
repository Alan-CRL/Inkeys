# Design

## 白板稳定态与过渡态
- `IdtState` 新增 `WhiteboardSwitching() noexcept`：白板功能启用且（阶段为 Entering/Exiting，或请求值与“阶段==Active”不一致）。`WhiteboardTransactionActive()` 语义不变，其它调用方不动。
- 快捷键 owner：`modal = hardModal || WhiteboardSwitching() || (WhiteboardActive() && 白板窗口组最小化)`。稳定白板不再是 modal，Matcher/owner 按普通批注规则认领。

## 业务入口
- `ChangeStateModeForShortcutIfRevision`：把 `WhiteboardTransactionActive()` 门禁改为 `WhiteboardSwitching()`，稳定白板允许恢复批注与进入 Selection（与按钮路径同一 `ApplySelectionTransitionLocked`）。
- owner 的 X：分离式清空分支加 `!WhiteboardActive()` 条件，白板内直接 `Select`，与 `ChangeStateModeToSelectionFromButtonImpl` 一致。
- Undo/Redo/Clear 经 Draw3 Shortcut Scope；白板页作用域已由 controller 发布（workspace=Whiteboard、页索引、加载中不可用），不改 Draw3。
- Ctrl+Q 保持 Freeze 不可用 → 消费无动作。菜单 Escape 不变。

## 风险
- 白板最小化期间 Ctrl 组合需交还系统：最小化计入 modal。
- 工作区切换中的旧意图：路由 generation 随 modal 变化递增并 Retire，沿用现有机制。
