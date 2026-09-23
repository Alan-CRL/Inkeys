# 触摸橡皮擦光标残留修复

## Goal

触摸设备使用单指或多指橡皮擦时，仅显示跟随活动触点的擦除反馈；触摸期间及最后一指抬起后，不留下系统箭头或由旧鼠标、笔悬停样本生成的小光标。真实鼠标、触控板或笔再次输入时恢复对应的正常光标行为。

## Background

- Draw3 的橡皮圆环由 `DrawingController` 为活动 Touch contact 自绘；主光标另由 `PenCursor` 按 Pen/Mouse 样本解析，系统箭头由 `WindowController::ApplyWindowCursor` 设置。
- RTS Touch Down 已通知 `WindowController` 清除旧 Mouse 样本，但现有 `CursorOwner()` 未表示触摸接管；`ShouldHideSystemDrawingCursor` 对 Touch 且非已选择 Eraser/Laser 的场景返回显示箭头。Touch 被配置为橡皮而当前选择 Pen 时，这两种工具身份可以不同。
- Touch Up 只减少活动触点计数，不能依赖 Windows 自动把最后一处系统光标隐藏。`WM_MOUSE*` 的兼容消息若被误认成真实鼠标，也会重新发布主光标。
- Windows 7 没有 `GetCurrentInputMessageSource`，必须保留现有 RTS 通知、Pointer 兼容消息签名和消息时间屏障的回退路径。

## Requirements

- R1：单指和多指 Touch 橡皮接触仍按各自有效擦除直径显示原有圆环；Touch 不生成额外的 Pen/Mouse 主光标或系统箭头。
- R2：Touch Down 接管后，最后一指 Up/Cancel 不恢复旧主光标；真实 Mouse、TouchPad 或 Pen 的新输入可以接管并恢复各自正常的系统/自绘光标策略。
- R3：优先按可用的 Windows 输入消息来源拒绝 Touch/Pen 提升的 `WM_MOUSE*`，并保留兼容签名、时间屏障和 Windows 7 无新 API 时的旧回退。真实鼠标消息不得被一段固定的触摸后延迟误拦截。
- R4：Touch 仅作为暂时的视觉归属，不写入持久 Pen/Mouse owner；既有 Touch Pan 真实鼠标接管、Pen 悬停/接触、鼠标离窗及工具光标外观语义保持正确。
- R5：最小化源码和测试改动，保留文件原编码与换行；不启动交互式 GUI，不创建 commit 或 push。

## Acceptance Criteria

- [ ] AC1：触摸配置为橡皮而当前选择其他工具时，Touch Down/Move/多指接触没有额外小主光标或系统箭头；最后一指 Up/Cancel 后没有停在触点处的残留光标。
- [ ] AC2：Touch 后真实鼠标或触控板移动、点击、滚轮，以及笔的新悬停/接触，恢复原有光标策略；Touch Pan 中已确认的真实 Mouse 接管仍有效。
- [ ] AC3：Touch/Pen 提升的 Mouse 消息不能重新生成主光标；Windows 7 缺少新消息来源 API 时，RTS Touch 及兼容签名路径仍隐藏触摸光标，真正 Pen 悬停不被永久压住。
- [ ] AC4：无窗口回归测试覆盖触摸接管、抬起后保持隐藏、Mouse/Pen 恢复、兼容消息过滤和多指视觉；ARM64 `Debug|ARM64` 完整 `InkeysRepo.sln` 构建及相关无窗口测试通过。真实设备行为留给后续硬件验收。

## Out of Scope

- 不修改擦除几何、粗细、速度/面积算法、笔迹存储或触点圆环样式。
- 不改变系统全局 `ShowCursor` 计数，也不要求 Windows 7 提供 Windows 8 之后的输入来源 API。
