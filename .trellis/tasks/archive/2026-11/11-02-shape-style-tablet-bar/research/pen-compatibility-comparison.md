# 笔兼容鼠标消息：主栏与翻页准入比较

## 当前结论

用户认为该差异是高漫M6“主栏点不动、翻页可以”的原因。静态证据支持将其列为首要根因候选：如果设备发送带笔签名的WM_MOUSE按下/移动/抬起，而主栏没有完整有效WM_TOUCH替代流，那么主栏会丢失按钮输入，PageControl仍可操作。设备实际消息尚未采样，不能把此条件直接当作现场已证实事实。本轮仅调查，不改输入代码。

## 源码差异

- Message.cpp:61/68/74 根据GetMessageExtraInfo高位0xFF515700签名及低位0x80区分笔/触摸来源；并非所有WM_MOUSE都是物理鼠标。
- 主栏 Bar.Interaction.cpp:9728 的QueueWindowMessageInLayoutSpace返回Discard，带笔标记消息不进共享交互worker。:3208的WndProc再过滤；原注释认为Pen/Touch均由WM_TOUCH转译。
- 主栏WM_TOUCH :3069转译带Source::Touch的样本，:3101入队按钮业务；:2914的WM_POINTER旁观只用于触觉，没有替代业务Down/Up。单独收到Pointer不能补回被过滤的按钮事件。
- 翻页PageControl.cpp:2844只对经签名确认的笔兼容Down/Move/Up另设路径：标Source::Pen；Down创建generation与非鼠标租约；后续只接受同一活动Pen会话，Up终结并释放租约。
- PageControl.cpp:3093捕获并二次核对已呈现身份，:3103 Session::Begin建立归属；:3133 SubmitSurfaceInput进入共享worker。Pen不要求物理Raw Mouse，鼠标/触摸的终态不能完成其笔会话。
- PageControl只过滤触摸提升副本以去重，保留WM_TOUCH入口；支持笔兼容消息不等于忽略触摸。

## 行为对照

| 设备实际消息 | 主栏 | PPT/白板翻页 |
| --- | --- | --- |
| 带Pen签名WM_MOUSE，缺有效WM_TOUCH | 鼠标事件被丢弃，Pointer旁观不执行业务 | 建Pen会话并点击/长按（按工作区策略） |
| 完整WM_TOUCH | 沿触摸转译处理 | 沿触摸转译处理 |
| 无Pen/Touch签名的普通WM_MOUSE | 普通鼠标入口 | 普通鼠标入口 |

放开主栏过滤还不足以形成完整修复：它当前消费层主要按Mouse/Touch分流，需保留真实Pen来源、generation、capture/取消与重复流仲裁，不能把Pen强送Raw或伪装为物理Mouse。

## 日志最小判据

同一次M6点击主栏和翻页，记录原始message、extraInfo及来源判断、WM_TOUCH是否到达/触点flags和阶段、会话generation与Down/Up接纳/取消原因。若主栏Pen签名Down/Up被过滤且没有有效Touch，而翻页Pen会话被接受，即可确认这条因果链；无需先推测驱动具体设置。

## 官方资料

- https://learn.microsoft.com/en-us/windows/win32/tablet/system-events-and-mouse-messages ：系统笔可产生带来源签名鼠标消息；官方建议UI指向/选择可使用鼠标消息。
- https://learn.microsoft.com/en-us/windows/win32/wintouch/wm-touchdown ：WM_TOUCH可描述手指或笔触点；未给出每种笔驱动/模式一定向目标窗口提供该流的保证。

未编译、未运行GUI或真实设备测试。此记录不变更等待日志再修R2的边界。
