# 鼠标 Hook / PPT 实现记录

用户批准后实现；本子代理没有构建、启动 Hook、GUI、commit 或 push。完整 solution / headless 由主会话统一执行。

## 实现与所有权

- MouseHook 保留受管 jthread + Win32 消息泵，安装/卸载同线程；Start/Stop 由 operation mutex 串行，停止事件独立 wake mutex，专用线程不再运行收起/翻页/渲染/timer 业务。线程或安装失败回退 false；退出/WM_QUIT 不再反复零时限轮询。
- HookProc 负 code / 停止 / MOVE 立即链传。鼠标 L/M/R 缓存改固定原子位，键盘缓存 map 和键盘 Hook 不改。其余入口只复制 MSLLHOOKSTRUCT 值，经生产 Bridge 的一次原子快照读和固定 ring 尝试接纳。
- Bridge 内 256 槽 SPSC 有效容量 255；owner 暂停超过 250ms、路由禁用、快照正在写、零/提升 wheel、满队列均 fail-open，失败没有可消费副本。仅已提交 wheel return 1；OutsideCandidate 始终链传，兼容提升点击只作为候选，不产生输入。
- Route 每轮由 Bar owner 发布，普通上下文捕获完整 PPT token 与 PageControl 可用状态。内容变化才递增 route generation，heartbeat 单独原子续租。发布载荷全部原子，读侧一次尝试不旋等；uint64/uintptr/size 原子有 lock-free static_assert。
- 生命周期代次在 Hook 回调入口读取，停止先关闭 running 再推进代次；旧回调不会在停止后借用新代次。队列不在活跃 producer/consumer 间重置，TryPop 有限退休旧事件。
- Bar 通过 PublishRoute / TryPop / DispatchWheel / PollPptWheel 消费，每次最多有限批量，嵌套 TryRead 也轮询；栏外 timer、命中、fold 归主会话的 Bar 实现。

## PPT

- 全局 wheel 在 Bar owner 调 ConsumePptWheel。PageControl 本地 WndProc 保留原 targetVisible/inputLocked/workspace wheel 门禁和提升来源过滤，再进独立有界 64 槽 mailbox；不会成为 Hook SPSC 第二 producer。
- 两路累计器只有 Bar owner 写。signed delta 按 120 累计，保留符号余数；路由/模式切换、失效/会话变化退休余量。完整意图立即尝试既有业务门禁，节流拒绝不留下欠账。每次本地最多 drain 16 项。
- 本地 mailbox 捕获 token 和生命周期 epoch；PageControl Release 关入口、推进 epoch、清旧 mailbox，迟到原生请求不能在重新 Acquire 后复活。
- 新轻量 PptSessionToken.h 从原 PptSession.h 原样抽出值 token / Matches；新增 native typed input 声明。Consume 的捕获 token 原样进入 IdtPlug-in 最终 request，不重新 CaptureCurrent，不调用会回落白板的 InvokeDirection。最终业务线程沿原 IsCurrentPptSession 检查。
- Previous/Next 保留现有 outstanding；EndShowGate 在业务队列 mutex 下限制每 token 一个 pending/in-flight，完成/错误/过期/停止释放；旧完成不释放新 token。COM ABI 与确认策略不变。
- 移除已经没有调用者的同步 QueueGlobalWheel / QueuePptWheel 旁路；旧 KeyboardInteraction 不复活。没有向 HiMsg 注入 Raw 或另造系统按键。

## 测试接线

新增 InkeysHeadlessTests/mouse_hook_transport_tests.cpp，入口 RunMouseHookTransportTests。测试真实 HookProc 使用的 BypassHook / Bridge::TryCommit、SPSC / RoutePublisher、WheelAccumulator、PptEndShowGate：固定容量/停住消费者/并发消费者、准入提交与无幽灵事件、来源、时间回绕、并发快照一致性、细分/反转/多刻度/清余量、EndShow 有界及跨 token 完成。输出停住/并发消费者桥入口 P50/P95/P99/max；不计 CallNext 下游 Hook，不将合成数值视为系统鼠标体验结论。

新头文件登记：Inkeys/Business/PptSessionToken.h、Inkeys/Input/MouseHook.Transport.h。测试 cpp 登记和总入口由主会话完成。

本子代理仅执行 git diff --check（通过）和静态核对。没有启动任何系统 Hook 或窗口。实际 Win7 安装/卸载、硬件/提升输入、PPT/WPS、UIPI、系统光标跟手感依旧需要用户设备人工验收，不能用上述无窗口测试代替。
