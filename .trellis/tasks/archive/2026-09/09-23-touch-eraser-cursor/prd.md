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
- R5：最小化源码和测试改动，保留文件原编码与换行；不启动交互式 GUI，后续提交和推送仅按当前对话授权执行。
- R6：用户复测仍看到触摸期间额外的按下圆环及抬起后在原光标位置出现的悬停圆环。本阶段只增加默认关闭的光标诊断开关，在实验选项中保存，并于下次启动向控制台输出输入来源、归属变化、系统光标决策以及实际提交的 Pen、Mouse、Touch、荧光笔和激光笔光标来源与外观；不修改现有显隐策略。
- R7：用户诊断日志确认三次 Touch Up 后，来源未知、无 promoted 标记且停在最后触点的 `WM_MOUSEMOVE` 被误当成真实 Mouse，重新生成主悬停橡皮圆环。仅抑制触摸视觉归属尚在时的这种原位 Move；来源明确的 Mouse/TouchPad、按键/滚轮和实际移到新位置的鼠标仍可接管；已确认的系统注入 Move 按 R10 拒绝。

## Acceptance Criteria

- [x] AC1：触摸配置为橡皮而当前选择其他工具时，Touch Down/Move/多指接触没有额外小主光标或系统箭头；最后一指 Up/Cancel 后没有停在触点处的残留光标。
- [x] AC2：Touch 后真实鼠标或触控板移动、点击、滚轮，以及笔的新悬停/接触，恢复原有光标策略；Touch Pan 中已确认的真实 Mouse 接管仍有效。
- [x] AC3：Touch/Pen 提升的 Mouse 消息不能重新生成主光标；Windows 7 缺少新消息来源 API 时，RTS Touch 及兼容签名路径仍隐藏触摸光标，真正 Pen 悬停不被永久压住。
- [x] AC4：无窗口回归测试覆盖触摸接管、抬起后保持隐藏、Mouse/Pen 恢复、兼容消息过滤和多指视觉；ARM64 `Debug|ARM64` 完整 `InkeysRepo.sln` 构建及相关无窗口测试通过。真实设备行为留给后续硬件验收。
- [x] AC5：实验选项中的“光标调试信息”默认关闭，开启后下次启动自动打开控制台；有界日志包含消息来源和接受结果、Touch/Pointer/Mouse/Pen 状态、主光标与逐触点光标、系统箭头显隐、最终帧是否呈现及丢弃计数，足以区分额外圆环的生产者。用户提供复现日志前不据此推断最终根因。
- [x] AC6：来源未知的原位 MouseMove 不再使 Touch 抑制从 1 变 0，也不在最后一指抬起后呈现 `source=primary` 悬停圆环；真实鼠标移动、已识别 Mouse/TouchPad 输入和 Pen Hover 仍恢复正常。用户设备实测前仅把构建与无窗口测试记为通过。

## Out of Scope

- 不修改擦除几何、粗细、速度/面积算法、笔迹存储或触点圆环样式。
- 不改变系统全局 `ShowCursor` 计数，也不要求 Windows 7 提供 Windows 8 之后的输入来源 API。

## 已完成阶段：诊断增强

- R8：保留现有光标策略，补充来源 API 的可用性、调用结果、错误码、originId 和同步发送状态；按消息关联过滤前后状态与实际判定依据。为末触点位置记录时间、完整 Pointer ID 是否可得及主触点属性。
- R9：在既有 Bar Raw Input 入口记录鼠标设备、移动/按键和接收启停，不新增注册或改变 Bar 生命周期；没有 Raw Input 输出不能证明没有真实鼠标输入。
- AC7：提供直接检查产品日志完整事件链的脚本；能够检出已提供的两份失败日志，对有明确鼠标/触控板/笔接管的序列不误报，日志丢失或截断时报告证据不完整。此脚本不替代窗口或设备实测。

## 本轮授权：按系统来源修复

- R10：来源查询成功且 device=IMDT_UNAVAILABLE、origin=IMO_SYSTEM 的 WM_MOUSEMOVE，在触摸仍拥有光标时不得接管或发布 Mouse 样本；判定不依赖按键位、坐标或固定延时。明确 Mouse/TouchPad/Pen 的接管路径保持可用；API 缺失/失败不伪装成已确认系统来源。
- AC8：生产代码与测试共用完整来源过滤入口，覆盖带按键系统 Move、Touch Up 后迟到 Move、不同触点坐标、真实设备恢复及 API 不可用回退；设备实测仍单独验收。

## 结案依据

用户于 2026-09-25 确认人工验收通过并批准结案；最终日志覆盖单指、五指及后续 Mouse 接管。勾选项结合无窗口测试和用户验收，不代表 Pen/Win7 已有硬件日志证据；详见 validation.md。
