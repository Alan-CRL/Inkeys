# UI3 白板技术设计

## Boundaries

- `Inkeys.UI.Whiteboard` 只负责三个 RenderPipeline 客户端、固定布局、输入命中和显示状态；页面业务通过回调异步提交。
- `IdtState`/白板协调器负责工作区事务、PPT 同步门控、Freeze 状态和 Bar/Window Service 联动。
- Draw3 bridge 用显式 `Workspace` 字段区分 Presentation 与 Whiteboard；DrawingController 维护两套文档运行时。
- Window Service 继续拥有全部 HWND；Freeze HWND 在白板期间由 UI3 独占呈现，旧 Freeze 线程通过共享所有权门控。

## Data Flow

1. Bar Whiteboard 按钮或关闭按钮只发布 `RequestWhiteboardActive(bool)`。
2. StateMonitoring 进入 `Entering`/`Exiting` 阶段，暂停/恢复 PPT UI 和页码发布。
3. Draw3 `ProductState.workspace` 改变后，DrawingController 在无 active contact 时交换文档、页索引和页面运行时缓存，并发布新的 runtime snapshot。
4. runtime 确认目标 workspace 后，协调器显示/隐藏 Whiteboard UI、设置 Drawpad 呈现目标、切换 Bar 底栏专用状态，先标记 Freeze 全屏再切换 Window Service topmost 模式。
5. Whiteboard 翻页回调提交现有 NextPage/PreviousPage command；UI 页码只使用 Draw3 已完成切换的 snapshot。

## Public Contracts

- `Bridge::Workspace { Presentation, Whiteboard }`。
- `Bridge::ProductState.workspace` 与 `HostRuntimeSnapshot.workspace`。
- Window Service 增加 `SetOverlayTopmost(bool)`，`RefreshTopmost` 读取持久化目标并对 owner root 使用 `HWND_TOPMOST`/`HWND_NOTOPMOST`。
- Window Service 增加 `SetOverlayFullscreen(bool)`；刷新时对 Freeze HWND 调用 `ITaskbarList2::MarkFullscreenWindow`，让无焦点全屏窗仍能让任务栏退出工作区。
- Whiteboard 模块提供 `Initialize`, `Shutdown`, `WindowProc`, `PublishActive`, `PublishPageState`，以及上一页/下一页业务回调。
- Bar 提供白板激活状态和专用底栏进入/解除接口；所有 UI 线程请求必须异步化。

## Compatibility

- Presentation 工作区继续沿用现有 selection/output 逻辑；仅在 Whiteboard 中强制 PrimaryDrawpad 并关闭 selection ULW。
- 旧 A2 Pierce/Freeze 配置在读取时归一化为 Whiteboard/Freeze 两个 `twoOne` 项。
- 旧 Freeze 定格线程保留原功能，但在白板工作区跳过任何 legacy surface 提交；切换期间使用共享互斥避免与 UI3 对同一 HWND 并发提交。
- 新窗口角色加入 OverlayReady、HideAll、owner/role 映射和主线程创建顺序，不改变 Setting 生命周期。

## Failure Handling

- Draw3 workspace switch 只排队，不取消 active contact；若设备/文档创建失败，保持旧 workspace 并将 UI 回滚到旧状态。
- UI3 client 或窗口创建失败时初始化返回失败，主线程按现有 UI3 启动失败路径退出。
- topmost 模式变更失败记录日志，但不修改已确认的 workspace 状态；下一次 refresh 重试。
