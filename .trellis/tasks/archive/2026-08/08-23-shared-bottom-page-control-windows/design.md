# 共享底部分页窗口技术设计

## Architecture And Boundaries

- `WindowRole::PptBottomLeft/PptBottomRight` 保留为共享宿主角色，以最小化 PPT 连续枚举、布局和配置代码的改动；删除 `WhiteboardLeft/WhiteboardRight` 角色及其窗口规格。
- `Inkeys.UI.Ppt` 继续拥有共享宿主的主 WndProc，并在 Window Service 已进入 Whiteboard mode 时把共享左右窗口消息转交 `Inkeys.UI.Whiteboard::WindowProc()`。
- `Inkeys.UI.Ppt` 与 `Inkeys.UI.Whiteboard` 保留各自 RenderPipeline 客户端、per-window target 和状态。共享调度线程串行执行回调，窗口模式门禁保证只有当前 owner 可以向共享 HWND 提交。
- Whiteboard Freeze 客户端、PPT 中部左右客户端、PPT 结束放映客户端和 Bar 客户端不变。

## Shared Host Ownership Contract

共享宿主有三个可观察阶段：

1. **Presentation owner**：`WhiteboardWindowMode()==false`。PPT 底部 renderer 与 Ppt WndProc 可使用共享宿主，Whiteboard 控制 renderer 只清理自己的 Scene 后 idle，不得隐藏 PPT 窗口。
2. **Transition hidden**：`WhiteboardWindowMode()==true && Whiteboard::Active()==false`。共享宿主保持隐藏；PPT 与 Whiteboard 均不得提交或消费分页输入。
3. **Whiteboard owner**：`WhiteboardWindowMode()==true && Whiteboard::Active()==true`。Whiteboard renderer/WndProc 使用共享宿主；即使 PPT 配置或页码产生迟到请求，PPT renderer 也只能 idle，不得隐藏或覆盖白板帧。

窗口过程按 `WhiteboardWindowMode()` 分发而不是只按 `Whiteboard::Active()` 分发，使 Entering/Exiting 阶段的迟到消息进入 Whiteboard 的 inactive 丢弃路径，而不会触发 PPT 命令。

## Lifecycle Data Flow

### Enter

1. 收起辅助面板并撤销当前 capture。
2. 发布 PPT 不可见，隐藏 `PptBottomLeft/PptBottomRight` 共享宿主。
3. `EnterWhiteboardWindowMode()` 建立 Transition hidden 门禁，再切换 Draw3 workspace。
4. Whiteboard workspace 首帧和 Freeze 背景就绪后发布 `Whiteboard::Active(true)`；Whiteboard 客户端设置共享窗口 bounds、提交首帧并显示。

### Exit

1. 撤销 Whiteboard capture；`Whiteboard::PublishActive(false)` 与正在执行的 Whiteboard present 事务同步，随后隐藏共享宿主。
2. 切回 Presentation workspace，并等待 Draw3 Presentation 帧及 Whiteboard Freeze 关闭。
3. 完成 `LeaveWhiteboardWindowMode()`、topmost、fullscreen 和 click-through 恢复。
4. 最后按 COM 当前状态发布 PPT 可见性，唤醒 PPT renderer 接管共享宿主。失败重试期间 PPT 保持不可见，不发布假稳定态。

## Rendering And Input Details

- Whiteboard `ControlRoles` 改为共享 `PptBottomLeft/PptBottomRight`；`SurfaceIndex` 也按共享右窗口句柄区分左右。
- Whiteboard inactive 回调不得再无条件隐藏 ControlRoles；显隐由进入/退出事务和当前 owner renderer 管理。
- PPT 底部回调在 Whiteboard owner 阶段不修改 HWND；在 Transition hidden 阶段只收敛自身输入/动画状态并保持窗口隐藏。PPT 中部与退出放映仍按 `presentationVisible` 正常收起。
- `CancelPointerCapture()` 的 Window Service 角色集合改为 Drawpad、共享左右底部分页窗口和 Bar；共享 WndProc 根据当前模式清理对应输入状态。
- 共享 HWND 的尺寸允许按 owner 动态变化：PPT 沿用配置布局，Whiteboard 沿用固定 BarSurfaceScene presentation bounds。

## Compatibility And Migration

- 不迁移或重命名 PPT 配置字段；共享窗口在 Presentation owner 下仍由现有 `PptBottom*` 配置控制。
- `WindowRole` 是进程内枚举，没有持久化句柄兼容要求；删除 Whiteboard 两个角色时同步更新连续范围、创建/销毁顺序和测试。
- 保留 RenderPipeline 的 Whiteboard 控制客户端，避免把 BarSurfaceScene 绘制细节并入 PPT renderer；独占性由显式 owner gate 保证。

## Failure And Rollback

- mode、workspace 或背景握手失败时保持 Transition hidden，禁止任一 renderer 抢先显示共享宿主。
- renderer 在提交前后都应重新确认 owner；若 owner 已变化，放弃本次结果并由事务隐藏窗口。
- 若实现回归，可恢复独立 `WhiteboardLeft/Right` 角色与窗口规格；PPT/Whiteboard renderer 本身没有合并，因此回滚不涉及视觉和业务状态迁移。
