# UI3 第三光源休眠状态机技术设计

## Architecture

- `BarUISetClass` 维护 `Dormant`、`Inside`、`Grace` 三态跟踪状态；淡入淡出属于渲染侧独立视觉状态，因此进入 `Dormant` 后可以继续使用最后坐标完成淡出。
- UI3 窗口收到自然 `WM_MOUSEMOVE` 时进入 `Inside`。该消息只会在窗口实际可接受鼠标消息时出现，休眠态不需要全局检测线程。
- `Inside` 动态注册 `RIDEV_INPUTSINK`；Raw Input 通过 `WindowFromPoint` 判断是否仍由 UI3 接受消息。首次离开转入 `Grace`，记录 `GetTickCount64()` 绝对截止时间并设置一次窗口定时器。
- `Grace` 中的后续 Raw Input 只更新邻近判定和绝对截止检查，不重置截止时间。重新进入转回 `Inside`；超时转入 `Dormant` 并用 `RIDEV_REMOVE` 注销。
- 画布输入入口通过导出的 UI3 通知函数向 Bar 窗口 `PostMessage`，由窗口线程执行休眠转换，避免 RTS/画布线程直接修改 Bar 或 D2D 状态。

## Visible Region and Distance

- 渲染线程从主按钮、主栏和绘制属性栏三个外层对象生成当前物理像素矩形，并在现有鼠标光互斥锁下发布缓存。
- Raw Input 高频路径只把客户区鼠标点与至多三个矩形计算最近欧氏距离，并转换为 `0.0..1.0` 的空间亮度系数。
- 阈值使用 `50 × zoom`；矩形内部系数为 1，外部使用 smoothstep 从 1 连续衰减到 0，超过阈值后不再因位置变化唤醒渲染。
- `WindowFromPoint` 只负责接受消息区域的生命周期状态，不能参与第三光源距离计算；5 秒期限始终从离开可接受消息区域开始。

## Rendering

- `borderCursorLightSpatialIntensity` 表示光标到可见 UI 外框的空间衰减系数，跟踪生命周期与该系数相互独立。
- `BarUIRendering::PrepareFrameLighting` 将现有单向淡入改为可逆强度动画：保存当前强度、目标强度和 elapsed，用相同 300ms smoothstep 在 0 与 1 之间连续过渡。
- 最终第三光源强度为生命周期动画强度乘以空间衰减系数；休眠时保留最后空间系数和光源点，直到生命周期淡出完成。
- 动画关闭时强度立即为 0，并请求 Bar 进入休眠，保持现有“第三光源关闭”语义。

## Timer and Failure Handling

- 窗口定时器负责鼠标静止在区域外时仍能按时休眠；每个 Raw Input 同时检查绝对截止时间，避免高频输入延迟定时消息。
- 定时器只在 `Inside → Grace` 时启动，进入和休眠时取消。
- 注销失败后逻辑状态仍视为已注销并忽略迟到 `WM_INPUT`，避免继续读取位置或唤醒渲染；错误只记录一次。
- 当前仓库只有 UI3 Bar 注册鼠标 Raw Input，因此动态 `RIDEV_REMOVE` 不会移除其他第一方消费者。

## Compatibility and Rollback

- 不新增线程、工程项、配置和链接库；继续使用 Win32 Raw Input、窗口消息、现有锁和条件变量。
- 若分层窗口实际命中与 `WindowFromPoint` 在目标系统上不一致，可回退为复用三个外层 UI 几何的区域内判定，不影响状态机其他部分。
- 若动态 Raw Input 注销存在兼容问题，可回退为保留注册但在 `Dormant` 入口处立即忽略 `WM_INPUT`；该回退只降低节能幅度，不影响视觉正确性。
