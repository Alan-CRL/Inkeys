# 非白板覆盖层置顶与呈现失败收敛

## Goal

修复非白板工作区中覆盖层窗口偶发未完整置顶或未显示的问题，使进入 PPT、返回桌面和 Draw3 呈现面切换后，画布、主栏与 PPT 翻页控件能够作为同一 owner 树可靠恢复，同时保留“只对根窗口置顶”的既有设计。

## Background

- 当前 overlay owner 树为 `MagnifierHost -> Freeze -> { DrawpadPresentation, Drawpad -> [四个 PPT 窗口, Bar] }`；`MagnifierChild` 是子 HWND，`Setting` 不属于该 owner 树。对应创建关系由 `Inkeys/Inkeys/Window/Window.cpp` 和 `InkeysHeadlessTests/window_tests.cpp:120` 起的断言确认。
- 根置顶由 `Inkeys/Inkeys/Window/Window.cpp:1311` 的 `RefreshTopmost` 命令完成，只对 `OverlayRoot()` 调用 `SetWindowPos(HWND_TOPMOST/HWND_NOTOPMOST)`。Win32 会将 owned popup 随 owner 一起移入相同 band。
- 本机无可见窗口探针已确认：隐藏根窗口也能抬升整棵 owner 树；外部 topmost 窗口可暂时位于整棵树之上，再次刷新根后整棵树会整体越过它；对 Bar/PPT 使用 `HWND_TOP` 只改变树内 sibling 顺序，不会把节点独立变成 topmost。
- `Inkeys/Inkeys/Window/Window.Legacy.cpp:76` 的周期刷新间隔受配置控制，为 100ms 至 30s，且 `RequestTopmostRefresh()` 的失败结果当前被忽略。
- PPT 可见状态在 `Inkeys/IdtPlug-in.cpp:562` 每 500ms 发布，但 `Inkeys/Inkeys/UI/Ppt/Ppt.cpp:154` 当前没有在 `false -> true` 转换时主动刷新根窗口。
- `Inkeys/Inkeys/UI/PageControl/PageControl.cpp:840` 在呈现成功后忽略 `SetBounds/Show/Hide` 的返回值，导致窗口操作失败仍被当作 `Idle/Continue` 完成，渲染调度器不会重试。
- `Inkeys/IdtState.cpp:171` 忽略 `SetDrawpadSurfaceVisibility()` 结果；若失败后 Draw3 runtime revision 不再变化，画布显隐状态可能长期不再收敛。

## Requirements

### R1. 保持根窗口统一置顶

- `MagnifierHost` 继续作为 overlay owner 树唯一的 topmost 操作点。
- 不给 Freeze、Drawpad、DrawpadPresentation、Bar 或 PPT 窗口增加独立 `HWND_TOPMOST` 操作。
- Bar 与 PPT 的 `HWND_TOP` 仅用于保持树内顺序：Bar 在最上，当前 PPT 控件紧随其后。
- 不调整现有 owner 关系，不改变白板模式下 `HWND_NOTOPMOST` 的行为。

### R2. PPT 出现时立即抬升根窗口

- `PublishPresentationVisible(false -> true)` 发布 UI 状态后，立即请求一次根窗口 topmost 刷新，不等待 `TopWindow()` 的周期定时器。
- 同一段持续可见状态下，成功后不得每 500ms 重复刷新。
- 若即时刷新失败，保持一次待重试状态；后续 PPT 状态发布继续尝试，成功或转为不可见后清除待重试状态。

### R3. PageControl 窗口操作失败必须可重试

- 呈现成功后，`SetBounds`、`Show` 或 `Hide` 任一步失败，当前 surface 回调必须返回 `FrameResult::Retry`。
- 成功路径继续按动画状态返回 `Continue` 或 `Idle`；`DeviceLost` 与呈现失败的既有分类不变。
- 不因一次窗口操作失败清除目标可见性或篡改 owner/sibling 顺序。

### R4. Drawpad 呈现面切换失败必须继续收敛

- `ReconcileDraw3Presentation()` 必须区分“等待 Draw3 条件”“已应用”和“窗口操作失败需重试”。
- `SetDrawpadSurfaceVisibility()` 失败时保留 reconciliation pending；即使没有新的 runtime revision，也应在状态线程现有 250ms 等待节拍后重试。
- 只有窗口操作成功后才更新 `IdtWindowsIsVisible.drawpadWindow` 的就绪事实。

### R5. 增加面向故障的诊断

- topmost、PageControl 窗口提交或 Drawpad surface 切换失败时，记录操作名、Win32 错误码（必须在失败线程立即捕获）和相关状态。
- 窗口状态至少包含 role、HWND、owner、可见性、`WS_EX_TOPMOST` 与窗口矩形。
- Drawpad 失败诊断同时包含选择出的 surface、Draw3 requested/ready output target、requested/ready/content/presented revision、first-frame 状态和 auxiliary clean-frame 状态。
- PageControl 失败诊断包含 role、`shouldShow`、present 结果以及 `SetBounds/Show/Hide` 各自结果。
- 诊断仅在状态转换或失败时输出，不增加逐帧常态日志。

## Acceptance Criteria

- [x] AC1 (R1): 自动测试确认只有 overlay 根被代码独立置顶，非根节点没有被单独置顶，owner 关系保持不变。
- [x] AC2 (R1): 无可见窗口测试确认隐藏根刷新后，整个 owner 树位于竞争的外部 topmost 窗口之上，树中没有外部窗口插入。
- [x] AC3 (R1): 无可见窗口测试确认 Bar/PPT 使用树内排序后仍属于同一 owner 树，Bar 在目标 PPT 窗口之上且不会激活窗口。
- [x] AC4 (R2): PPT 可见性从 false 变为 true 时产生根刷新；连续 true 只在前次失败尚未恢复时重试，成功后不重复请求。
- [x] AC5 (R3): PageControl 的 bounds/show/hide 操作任一失败时返回 `FrameResult::Retry`，成功时保持原有 `Idle/Continue` 语义。
- [x] AC6 (R4): Drawpad surface 操作失败后 reconciliation 不会被误标为完成，并可在没有新 Draw3 revision 时重试；成功后就绪状态才更新。
- [x] AC7 (R5): 窗口操作失败诊断能够关联窗口层级、显隐、topmost、矩形、PageControl 提交状态和 Draw3 握手状态，且正常帧不会持续刷日志。
- [x] AC8: 使用 ARM64-host `MSBuild.exe` 构建完整 `InkeysRepo.sln` 的 `Debug|ARM64` 配置成功，随后 `Build/ARM64/Debug/InkeysHeadlessTests.exe` 无窗口测试通过。

## Out Of Scope

- 不重构 Window Service 的 owner 树或线程模型。
- 不对 owner 树节点逐个调用 `HWND_TOPMOST`，不引入新的 z-order 修复定时器。
- 不改变 PPT 布局、动画、交互与 COM 页码同步规则。
- 不改变 Draw3 渲染后端、surface 选择规则或白板窗口模式事务。
- 不尝试压过系统安全桌面、UAC、独占全屏或高完整性进程等 Win32 权限边界。

## Risks And Deferred Items

- Windows 对 topmost band 的行为受权限和系统桌面边界限制；本任务保证同一交互桌面、允许 `SetWindowPos` 的常规应用场景。
- PageControl 的真实 Win32 失败注入若需要扩大生产 API，仅保留集成断言和现有调度器 Retry 合同测试，不为测试新增宽泛的公共抽象。
- Blocking open questions: none.
