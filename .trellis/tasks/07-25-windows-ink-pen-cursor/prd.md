# Windows Ink 绘制光标

## Goal

为当前绘图窗口中的 Windows Ink Pen 和 Eraser 提供与当前工具一致的窗口级光标，使用户在落笔或擦除前预判范围，同时保持 Mouse、Touch、接触绘制和其他窗口的系统光标行为。

## Background

- 当前 RTS 插件已声明 `StylusInRange`、`StylusOutOfRange` 和 `InAirPackets` 回调，但未订阅或发布悬停状态。
- 当前 Pen 基准直径是 5px；Highlighter 的实际固定竖直笔尖是 6.25x50px。
- 当前工具颜色是红色；光标必须从现有工具外观配置取得 RGB，不能维护第二份颜色来源。
- 项目以 Windows 7 SP1 + KB2670838 为兼容目标，因此 Pointer API 必须动态解析，缺失时由 RTS 状态回退。

## Requirements

- 只对系统当前活动指针类型为 Pen 的当前窗口客户区设置自定义彩色 `HCURSOR`（包括 Eraser/倒转 Pen），不得使用 `SetSystemCursor`、窗口类全局光标或全局计数式 `ShowCursor`。
- Pen 光标是直径为 `max(5px, 5px × DPI scale)` 的圆；Highlighter 光标是 6.25x50px 的固定竖直矩形。Pen 不随实时压力变化。
- Pen/Highlighter 光标边缘使用约 0.75 个设备像素、向内占用的浅灰色 `#B8B8B8` 外框，外框透明度语义不变；内部使用当前墨迹 RGB 和 50% Alpha，并按 Windows 彩色光标要求写入 straight BGRA。
- Eraser 光标是无最小尺寸限制的白色 `EraserGripCircle`；Hover 整枚光标 Alpha 为 0.75，Contact 为 1.0，圆环和三条竖线统一使用中灰色 `#808080`，圆环宽度按直径的 4% 比例计算。
- Pen 悬停进入窗口时显示光标；Down 后通过当前窗口的 `SetCursor(nullptr)` 立即隐藏，接触 Move 期间保持隐藏，Up 后仍在范围内时恢复悬停光标；离开范围、RTS 禁用/错误/设备移除/shutdown 或窗口销毁时恢复默认箭头。禁止使用全局计数式 `ShowCursor`。
- Eraser 或倒转 Pen 悬停时显示 Alpha 0.75 的 EraserGripCircle；Down/Move 期间保持显示并切换为 Alpha 1.0，Up 后恢复 Alpha 0.75。
- Windows 8+ 以动态解析的系统 Pointer 类型为最高优先级；系统仍报告 `PT_PEN` 时低优先级 Mouse 消息不得抢占，系统切换到 Mouse 后立即恢复默认箭头。
- Windows 7 缺少 Pointer API 时，以 RTS in-range/out-of-range 和 Pen packet 状态回退，不得因此破坏启动或绘制。
- Touch 不触发自定义光标；普通 Eraser 和倒转 Pen 使用 Eraser 光标，悬停与接触状态都保留倒转信息；Mouse/Touch 或其他非 Pen authority 使用默认箭头。
- 活动 Pen 笔画使用 Down 时锁定的有效工具；无活动 Pen 时使用当前选择工具。工具切换应更新下一次适用的光标外观。
- 光标只改变窗口光标状态，不进入 D3D L0/L1/L2、墨迹模型、contact payload 或持久化数据。

## Acceptance Criteria

- [x] 自动测试覆盖圆形/矩形/EraserGripCircle 位图尺寸、中心热点、透明背景、内描边、straight Alpha、抓手竖线和 fractional 小尺寸覆盖。
- [x] 自动测试覆盖 Pen hover/contact 隐藏、Eraser hover/contact 两档 Alpha、倒转 Pen、Mouse 系统接管、Touch 忽略、工具切换、out-of-range 及 RTS reset/shutdown 状态。
- [x] RTS `DataInterest` 包含 in-range、out-of-range 和 in-air packets，且高频通知最多保留一个待处理窗口刷新消息。
- [x] `Debug|ARM64` 完整解决方案构建成功，两个 shader、C++ Modules、资源嵌入和测试工程均成功。
- [x] `inkStrokeModelerTestTests` 通过。
- [ ] 实体 Pen 验证 Pen/Highlighter 悬停显示与接触隐藏、Eraser/倒转 Pen 两档 Alpha 和抓手纹理、Up 恢复、Mouse 系统接管、Touch、窗口外恢复和其他应用不受影响。

## Out Of Scope

- Mouse/Touch 自定义光标。
- D3D/HLSL 光标覆盖层、压力驱动的实时光标尺寸和多光标显示。
