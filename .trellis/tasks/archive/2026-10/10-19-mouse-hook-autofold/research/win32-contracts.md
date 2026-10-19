# 官方输入合同与规划约束

2026-10-19 核对 Microsoft Learn，以下用于设计约束，不代表已经做过系统钩子性能或 Office 实机测试。

1. [LowLevelMouseProc](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelmouseproc)：回调在安装线程执行，需要消息循环；超时会影响输入，Win7 起还可能被系统静默移除。官方建议专用线程接收后立即把工作交给其他线程。因此不仅 HookProc，安装线程的延迟业务、回调和锁等待也要排除在输入接收路径之外；不靠提高 LowLevelHooksTimeout 修复。
2. 同文档要求 nCode<0 直接链传；未处理消息链传；只有确实接管的消息才可返回非零抑制后续投递。规划上只有本进程有界通道成功接纳且路由快照允许的滚轮才抑制；不吞普通移动/按键。
3. [Hooks Overview](https://learn.microsoft.com/en-us/windows/win32/winmsg/about-hooks)：钩子增加输入链处理成本。规划要求 WM_MOUSEMOVE 立即通过、无阻塞锁/堆分配/格式化日志/COM/UI呈现/配置I/O，不安装重复鼠标钩子；保留受管线程同线程卸载和可唤醒停止。
4. [MSLLHOOKSTRUCT](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-msllhookstruct)：数据需在回调返回前复制，不能跨线程保留 lParam 指针；保留时间、屏幕点、mouseData、flags 和来源信息，明确注入/提升事件策略。
5. [WM_MOUSEWHEEL](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-mousewheel)：delta 带符号，标准阈值 WHEEL_DELTA=120；高分辨率设备可能分段提供 delta。PPT 翻页应定义累计策略并隔离会话，不能将每个非零小包都当作完整刻度；屏幕点有负坐标，不用无符号 LOWORD/HIWORD 解释位置。

性能验收分层：生产快路径无故意等待/分配和固定工作量；无窗口压力基准报告本地路径延迟分位、队列拥塞/关闭/消费者停顿；未来获得 GUI 授权后的真实鼠标与 Office 测试才可评价系统光标跟手感。不能把 OS 的超时上限当成可接受延迟预算，也不能用合成基准宣称系统绝无卡顿或跨 UIPI 保证。
