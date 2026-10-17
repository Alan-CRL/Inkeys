# Design

## Latest approved correction

用户在点击回归调查后确认 HiMsg 为原有入口，Raw 只补充拖动，并要求开始实现。详细边界见 [correction.md](correction.md)；旧的 Raw 独占 Move/Up、Raw Down 绑定后才准入的设计已经撤销。

## Ownership and data flow

- HWND owner 保存原生消息完整 MSG.pt/time、来源和按压代次；Bar 原生/触摸消息沿 HiMsg 进入交互线程。PageControl 原生与本地触摸直接在自己的 owner 上下文处理。
- 共享 MouseGesture 立即接受原生 Down，原生 Up 始终可以结束本次点击；Raw 只由拖动入口准备，Raw Down 是补充身份，Raw Move 可帮助候选越界超过阈值，BeginDrag 后才接受 Raw Up。
- MouseChannel 继续有界私有传输，不向 HiMsg 注入 Raw。现有保守时间+点匹配只决定可选补充是否可用，不参与普通点击准入。光效独立于注册生命周期。
- MouseGesture 归一化到原生业务身份并仲裁重复终态；Raw 活动新鲜时由其独占连续手势移动，原生 Down/Up 保留；事件时间晚于最后有效 Raw 活动至少50ms的本代次 native Move 可接续，并推进 Raw 严格回放下限，保留 Raw Up，不推断终态。迟绑定跳过已消费原生时间段及同毫秒，不能重建此前被合并的路径。较旧终态不回退已接受的拖动位置与时间。Bar 原生 Down 入队只存值型来源准入快照，交互线程拒绝跨来源接管失效的旧 Down。跨窗口 Touch/Pen 接管使用独立租约版本，不依赖可选 Raw claim；客户端活动检查显式取消。
- Raw 未绑定（generation=0）的启动失败/超时不取消原生点击或已经开始的原生拖动；只有非零 Raw 归属的实际拖动明确失去传输才取消，桌面/模式/销毁/来源取消继续独立生效。Cancel 不能伪装 Up。

## Preserved contracts

主按钮/粗细/PPT 原有点击与拖动阈值、颜色即时更新、FineDial 采样时间与惯性、触摸 hSource+dwID、笔兼容捕获、PPT pair try_lock/latest-wins/pending/持久化全部保留。窗口完整 MSG 派发 scope 仍用于积压采样，不再被当作两种消息必然相等的证据。

## Change boundary and validation

点击/一秒修正范围限 UiPointerInput.h/.cpp、Bar.Interaction.cpp、PageControl.cpp 和两个既有 headless 测试文件；边缘与静默后续另包含既有 Bar.BottomDock.h 小型纯抓点策略与 bar_bottom_dock_tests.cpp，详见 edge-and-stream-correction.md。主会话维护任务/spec 并执行完整 Debug|ARM64 solution、--no-window 和早退出生产探针。不改 hook/调用、Draw3、COM、HiMsg 第三方、激活或权限策略，不运行 GUI 或创建隐藏 HWND，不提交/推送。无窗口测试不能证明管理员前台或真实 Win7 触摸行为。
