# 补充：WM_TOUCH、兼容鼠标与 PPT 长按

## 消息关系

“兼容鼠标”指系统把笔/触摸动作表示为 WM_LBUTTONDOWN/WM_MOUSEMOVE/WM_LBUTTONUP，并通过 GetMessageExtraInfo 签名标注来源；不是必须由应用制造的假点击。Message.cpp:61/68/74 按签名及 Touch 位区分来源。主栏队列入口和 WndProc 丢弃这类消息，是为了避免与 WM_TOUCH 转译后的业务输入重复，但隐含假设每个笔设备都有可用 WM_TOUCH 流。

微软 WM_TOUCH 文档允许触点来自 finger 或 pen；这不构成所有笔驱动/所有输入模式一定给目标窗口发送 WM_TOUCH 的保证。系统不会因为应用丢弃兼容鼠标消息就补发 WM_TOUCH。主栏确实处理 WM_TOUCH；如果设备实际提供完整且有效的该流，还需继续查触点选择/来源租约等，不能仅凭过滤判定根因。

官方资料：
- https://learn.microsoft.com/en-us/windows/win32/tablet/system-events-and-mouse-messages
- https://learn.microsoft.com/en-us/windows/win32/wintouch/wm-touchdown

## 白板/PPT 翻页已支持两类入口

- PageControl.cpp:2965 处理 WM_TOUCH 并转译到共享按钮状态机。
- PageControl.cpp:2844 另行接纳笔兼容鼠标消息，Source::Pen，建立独立 Session/capture；这不是物理 Raw Mouse。
- 因此它们既非“只支持鼠标、不支持触摸”，也非“没有支持笔兼容消息”；恰好是它们的笔兼容入口比主栏完整。

## PPT 长按静态结论

- PageControl.cpp:2506 调用 ResolvePptDirectionPressPolicy，按下立即翻一次，满足开关和可重复条件时记录 pressStarted（:2513）。此逻辑不排除 Pen/Touch。
- PageControl.cppm:93 策略仅 PPT 模式有长按；白板模式返回空策略，白板方向在抬起时单次执行（PageControl.cpp:2596），不是笔输入缺失。
- PageControl.cpp:2686 的 PageControlInputTick 在内部 worker 截止时间到达时重复执行 InvokeDirection（:2701）；无需连续 WM_MOUSEMOVE，也不依赖 Raw Mouse 或系统长按右键手势。:4539 注册了 tick 回调。
- Up、移出、取消、工作区/放映会话变化等结束长按。未采样高漫 M6 真实 Down/Up 时序，不能保证该驱动实际长按，但不能从“不走 Raw/仅兼容鼠标入口”推导 PPT 长按不可用。
- 当前核查为静态阅读，未运行编译、测试或 GUI。
