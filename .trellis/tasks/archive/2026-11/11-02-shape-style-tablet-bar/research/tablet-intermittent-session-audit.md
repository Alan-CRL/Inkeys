# Research: 数位板间歇点击的来源租约/会话准入审查

- Query: M6 真实测试中主栏/分页都偶发不可点，跨窗口非鼠标租约、旧 Session 和触觉设备失败是否会阻断点击。
- Scope: internal（当前源码 + 用户提供日志）
- Date: 2026-11-02
- Task: .trellis/tasks/11-02-shape-style-tablet-bar
- Log: C:/Users/alan-/.codex/attachments/7ff6bb2f-bf6c-417a-9ffa-75387b5a0716/已粘贴的文本.txt

## Findings

### 高价值结论

1. **触觉 GetFromPointerId present=0 不会阻断 UI 点击。** 日志已存在业务接受/Click入队后才出现设备绑定失败的完整序列，不能把无震感解释为按钮未执行。
2. **跨窗口租约能拒绝新接触，且没有自动过期。** 如果某窗真实 Up/Cancel 未收尾，另一窗的新 Touch/Pen 会在 BeginTouch 直接拒绝；但日志没有 lease 成败/owner 信息，当前不能确认发生了租约泄漏。
3. **Bar WM_TOUCH 未先做有效控件/背景命中，就申请全局非鼠标租约和 SetCapture。** 错误窗口接到透明/空白位置的 touch 时可能先吃掉接触。日志L103–167完整 Touch 对但缺 Pointer/Click 的段落更需要核对实际投递窗口和几何，而不是统归“漏Up”。完整 Up 路径会释放租约，不能把每个空点击都视作卡租约。
4. L542–595 只有 Bar Pointer Down/Up、没有 Bar Touch 的另一段，属于**不同的入口缺失形态**；单一租约或单一过滤假设不能直接解释全部记录。

### 共享非鼠标租约（事实）

- `Inkeys/Inkeys/Input/UiPointerInput.cpp:13` / `:14` 全局 nonMouseRevision/nonMouseActive 由 Bar 和四个 PageControl 的 MouseChannel 共用；Provider `:278` touchOwner 是一个 channel weak_ptr，不是每窗互相独立的所有权。
- `UiPointerInput.cpp:557` BeginTouch 非零 generation 才有效，`:562` 若另一个 channel 持有 touchOwner 立即 false；成功后 CancelAll Raw 会话、置 owner/generation、设置 nonMouseActive 并推进 revision。这个方法同时服务 WM_TOUCH 和 PageControl 的笔提升鼠标路径，名称不是仅指手指。
- `UiPointerInput.cpp:576` EndTouch 只允许 owner 自己且代次匹配时释放。旧 End 不能释放另一个窗口/新代次。析构 `:526`、DetachRawMouse `:641` 等生命周期路径也会清 owner。
- `UiPointerInput.cpp:87` MouseGesture::Begin 看到 nonMouseActive 就拒绝普通 Mouse Down；`:106` SourceReplaced 还比较 revision，因此 Touch/Pen 在两条鼠标消息间已经开始并结束，旧 Mouse 手势仍失效。这是正常防串来源，不是可选 Raw 不可用造成的点击门。
- `UiPointerInput.cpp:54` Session::Begin 活动 session 只允许 Mouse→Touch 替换；活动 Pen/Touch 的新 Down 默认拒绝。Accept `:71` 必须 source/generation/device/contact 同一身份，不匹配的 Up 不会结束旧会话。
- `UiPointerInput.cpp:344` ExpirePendingMouse 的1000ms仅结束等待匹配 Raw Down 的 armed 会话；**不回收 touchOwner**。`CheckInputDesktop :672` 的 CancelAll 也只取消各 Raw channel 状态，客户端收到桌面失效后仍须走自己的 WM_CANCELMODE→EndTouch。

### Bar 申请与释放、空白命中风险（事实）

- `Bar.Interaction.cpp:3069` WM_TOUCH 读取原始接触。`:3120` 起只按 primary/fallback/source/id 决定是否建立 producer；`:3136` 先 BeginTouch，`:3144` 再做可选 Haptic BindDown。
- `Bar.Interaction.cpp:3099` PublishTouch(Down) 无条件调用 AcquireBarNativeCapture；`:2890` 实际为 SetCapture。这里未先验证可交互按钮/主栏背景命中，捕获票据为0也仍可入队；业务命中之后才在 worker 内处理。因而“没有最终按钮点击”不意味着 producer 没有抢过输入。
- 正常 Up 在 `Bar.Interaction.cpp:3174` 发布归一化 Up、释放票据，`:3175` EndTouch 并清 producer；enqueue失败 `:3107`、替换旧接触 `:3130`、GetTouchInputInfo失败 `:3184` 和 ShutdownWindowInput `:3373` 都有释放。
- `WM_CANCELMODE :2933` 调 ShutdownWindowInput，能释放源租约；`WM_CAPTURECHANGED :3035` 只退休原生capture/属性票据，不把 capture 丢失当真实 Up，也不在此 EndTouch。它本身不是完整触摸取消。worker 的 MakeBarPointerCancel `:617` 只清交互 session/触觉，不直接撤销 owner producer 的租约，后者仍等待真实终态或owner取消。
- 这意味着：若原始 Up 确实丢失且未出现owner取消，provider owner可能保持；但不能用capture丢失伪造成功点击修复，也不能用固定按住超时猜Up。
- Bar完整 WM_TOUCH 分支内未发现透明区域命中准入。此处只证明 **已经投到Bar的Touch会先申请租约/capture**，不证明操作系统本应将该位置投到哪个窗口；仍需核验真实窗口rect、alpha/region、层级、native hittest 和时刻呈现几何。

### PageControl 的前后准入（事实）

- `PageControl.cpp:2844` 笔提升鼠标路径把样本标记为 Source::Pen；`:2853` 当前非Mouse session尚活动就直接return；`:2858` BeginTouch失败也return，此时还没进入后面的SetCapture。因此被另一channel租约拒绝的新窗口，不能靠这次SetCapture自动取消旧窗。
- `PageControl.cpp:3021` WM_TOUCH Down 同样先 BeginTouch，失败continue。正常 Up `:3058` 和笔Up `:2878` EndTouch；后续 `ResetNativePointer :2725` 还会按client.touchGeneration释放（即使Session尚未成功建立），防早退泄漏。
- `PageControl.cpp:3088` 起先检查有效可见/locked/background，再SetCapture；重新校验语义身份、Pen捕获成功、Session::Begin和MouseGesture::Begin，任何失败在`:3101` ResetNativePointer。不应把这些校验失败统称为消息被过滤。
- `PageControl.cpp:2816` PointerTimer 每50ms检查输入桌面和 NativePointerCurrent（可见、semanticRevision、工作区/PPT session/layout/recovery）；无效则取消。只要旧Pen/Touch会话身份仍有效，timer没有“物理已松开”或固定超时回收机制。
- `PageControl.cpp:2959` 仅“真实Raw鼠标拖动”可忽略capturechanged，Pen普通按压仍走取消；隐藏/销毁 `:2836` 起也发WM_CANCELMODE。故正常捕获取消通常能回收租约，但不能从代码假设现场一定收到该取消。
- `PageControl.cpp:2894` Mouse新Down可以取消旧Mouse按压；这条恢复并不适用于`:2853` 已活动的Pen/Touch。跨窗口旧非Mouseowner卡住可导致两个表面都表现为间歇拒绝，是**待证实机制**。

### GetFromPointerId present=0 的实际影响（事实）

- `Draw3.HapticFeedback.cpp:472` WinRT GetFromPointerId 返回 S_OK 但对象为空时，`:474` 仅从触觉 AttachPointerId 返回 false；没有调用 UiPointer Session/BeginTouch/EndTouch，没有修改 UI 输入门。
- `Draw3.DrawingController.cpp:16312` 附近 Click 消费按 attached/current/sent 决定是否发送，失败只 DetachPointer；不回滚先前 UI 业务动作。Host点击发布函数也只是独立mailbox/control wake。
- 日志L42–47可见 pointer1047 的 Pointer/Touch DownUp匹配、product click published、bar_click accepted=1；L51–52之后才 GetFromPointerId present=0、click_consume attached=0/sent=0。这是**业务接受但无触觉设备对象**，不是“点击被绑定失败阻断”。
- 日志L632–635 pointer1058的Touch Move将token从1变0（invalid_source），但其真实Up仍到达Bar。源码中的haptic->valid/token不作为UiPointer会话准入条件；没有bar_click可以是按钮业务未接受、无可用haptic凭据或触点不在控件，日志不能单独区分。

### 日志可确认与缺失

- 日志开头 build id 为 `draw3-Nov  2 2026T20:45:11`、ARM64，不是只凭原始20261028a静态基线解释本轮包。
- 主会话统计L103–167为22组完整Bar Touch DownUp、无对应Bar Pointer/Click，坐标约x2660–2790/y1810–1856。bar_touch Down日志位于BeginTouch成功之后，因此这些样本已取得Bar租约，完整Up走EndTouch；不能用BeginTouch拒绝解释这段。Bar会先占租约/capture的源码与其相容，**这些坐标像右下分页只是推断**；没有用户操作分段及目标HWND/几何，不能认定该段为分页操作或已证明抢窗。
- L542–595 pointer1055有27组Bar Pointer DownUp，无Bar Touch/Click，随后pointer1057恢复Touch与Click。Pointer观察只管理haptic token，不调用BeginTouch。由于bar_touch日志只在BeginTouch成功/producer建立后出现，缺日志不能证明没有收到WM_TOUCH，也可能在BeginTouch拒绝或已有producer分支停下；目前无法区分驱动未投Touch、被上层窗口拦截和本窗准入拒绝。
- 本日志没有 BeginTouch成功/拒绝、touchOwner identity/generation、EndTouch释放理由、Session::Begin/Accept拒绝、PageControl当前hit/locked/semantic或实际业务接受的通用日志。因此不能由“无Click”反推出哪一个准入门拒绝。
- 画布 `[Draw3.Input] admission reasons=...` 是Draw3新笔门，和UiPointer全局lease/主栏分页按钮准入是不同系统；不能以其暂时非0就证明UI按钮被拦。

### 下一步最小证据（仅建议，不实施）

如需补日志，优先在准入的首个失败点记录单条原因：窗口/来源/DownUp、BeginTouch结果及当前owner/generation、Session active/source/身份、GetCapture/captureTicket、可见/locked/hit/semantic以及业务接受。对Bar还需记录同一时刻屏幕点是否命中真实已呈现背景/按钮，避免只看到haptic旁路。按接触记录而非每帧刷屏；不改变来源策略或猜设备。

### Files found / Related specs / External references

- `Input/UiPointerInput.cpp`：共享provider、Mouse/Touch/Pen租约、Session/MouseGesture。
- `UI/Bar/Bar.Interaction.cpp`：Bar WM_TOUCH生产者、捕获、会话与可选触觉凭据。
- `UI/PageControl/PageControl.cpp`：笔兼容消息、触摸、捕获取消、timer及worker准入。
- `Drawing/Draw3/Draw3.HapticFeedback.cpp`、`Draw3.DrawingController.cpp`：WinRT绑定失败及触觉消费。
- `.trellis/spec/native-desktop/ui-pointer-session.md`、`pen-haptics.md`：真实终态/来源隔离、失败不影响UI动作合同。
- 无外部资料；本轮仅用户日志与当前本地源码。

## Caveats / Not Found

- 只写本研究文件，未改产品/spec，未执行Git/build/GUI/设备操作。
- 完整DownUp样本与其他缺入口样本同时存在，尚不能确认唯一根因。透明区输入抢占、原始Touch路径缺失、租约/Session拒绝、命中/语义失效应分证据处理，不强行合并。
- 以上源码行号按本次读取；其他代理可能并行修改相关代码。未用本轮用户日志证明硬件全面支持/不支持触觉。
