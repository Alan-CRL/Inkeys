# UI3 白板技术设计

## Boundaries

- `Inkeys.UI.Whiteboard` 管理 Freeze 背景及左右两个 RenderPipeline 客户端，分页视觉由 `BarSurfaceScene` 持有，业务层只接收上一页/下一页回调。
- `IdtState` 以 `Inactive / Entering / Active / Exiting` 协调 Draw3 workspace、PPT 可见性、Bar、Whiteboard 与 Window Service。
- Draw3 bridge 通过 `Workspace { Presentation, Whiteboard }` 隔离两套文档运行时；workspace 切换等待活动 contact 收尾。
- Window Service 拥有全部 HWND、style、owner、taskbar、topmost、最小化/恢复和 click-through 状态。

## Paging Contract

`PageStateTransaction::Publish(currentPage, totalPage, switching)` 先把总页数归一化到至少 1，再把当前页限制在有效范围。首次发布即使处于 `switching=true`，也从真实输入建立稳定基线。

事务从稳定帧进入 `switching=true` 时锁存 Previous enabled 与右按钮 Add/Arrow 语义；事务中的业务页码仍可更新，但三个按钮全部不可交互。回到 `switching=false` 后才接受新的稳定边界语义。因此追加页 `3/3 -> 3/4 -> 4/4` 全程保持 Add，到末页的普通翻页则在事务完成后由 Arrow 变 Add。

页码按钮保持真实 `twoTwo` 命中和动画，但命令为 no-op。Scene 只对变化的文字或 SVG 建立过渡，未变化内容继续使用原视觉状态。

## Window And Lifecycle Contract

进入顺序：收起辅助面板并撤 capture，隐藏分页窗，执行 `EnterWhiteboardWindowMode()`，禁止 Draw3 激活并临时设置 Drawpad click-through，切换 Draw3 workspace；待 Whiteboard 首帧和 Freeze 背景就绪后显示 UI，设置 fullscreen、NOTOPMOST 和 Drawpad 可交互，最后进入 Active。

Freeze 在 Whiteboard mode 中是唯一 `WS_EX_APPWINDOW`、任务栏和 activation anchor；Drawpad 清除 `WS_EX_NOACTIVATE` 但保留 `WS_EX_TOOLWINDOW`。窗口组仍由 Freeze owner 链组织，最小化时保存成员可见性，恢复时仅恢复此前可见成员。

退出顺序：先隐藏分页窗口并停止命中，切回 Presentation workspace；待 Presentation 首帧可接管后关闭 Whiteboard、恢复 PPT、释放 Freeze ownership、恢复 Bar 折叠/dock 与 Presentation click-through。必须先 `LeaveWhiteboardWindowMode()`，再 `SetOverlayTopmost(true)`，防止 style 事务覆盖恢复后的 Z 序。

## Rendering And Resource Lifetime

分页控件通过 `BarSurfaceScene`、`BarSurfaceLayout`、`Bar.Metrics` 和 Bar animation/theme 单一来源渲染。Whiteboard 调试覆盖层与 Bar 一致：红框是业务 dirty，绿框是实际 present union；绿框本身只扩大 present damage，不写回业务 dirty。

Scene/renderer 暴露的 `ID2D1DeviceContext*` 和 `ID2D1GdiInteropRenderTarget*` 是借用指针。每次 `BeginDraw -> Render -> GetDC/ReleaseDC -> EndDraw` 事务必须用本地 `ComPtr` 租约延长其生命周期至 `EndDraw`。`GetDC`、`ReleaseDC` 或 `EndDraw` 任一失败都按同一 present 事务失败处理并重建该窗口资源。

## Failure Handling

- `EnterWhiteboardWindowMode()` 或窗口状态收敛失败：隐藏 Whiteboard UI、恢复 Presentation workspace，下一轮允许重试。
- Draw3 workspace 尚未产出可接管帧：保持当前可见 workspace，不让两个 renderer 并发提交同一目标。
- 单窗口 target/ULW 失败：仅重建对应客户端；共享 device 丢失继续走 RenderPipeline epoch 恢复。
- 退出恢复未完全成功：保持 `Exiting`，下一轮继续收敛，不发布假的 `Inactive`。

## Compatibility

- Presentation 保持既有 selection/output 逻辑；Whiteboard 强制主 Drawpad 且关闭 Selection ULW。
- 旧 A2 Pierce/Freeze 配置继续在读取时归一化为 Whiteboard/Freeze。
- legacy Freeze 线程保留，但 Whiteboard 持有 Freeze surface 时不提交；所有权切换以首帧握手避免双写。
