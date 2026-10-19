# 鼠标钩子与 PPT 滚轮研究（仅规划，2026-10-19）

## 授权与最新产品约束

- 本研究只读源码，没有修改产品代码、配置、工程或规格，没有编译、GUI、Git mutation。
- 先读 `planning-boundary.md`，并核对 `native-desktop/ui-pointer-session.md` 与 `ppt-interop/native-session-ui3.md`。后续用户修订优先于 planning-boundary 最初的“仅分离式”：四项自动收起在合并式、分离式均可见且生效，不以样式门控。
- 四项新策略独立放入 Inkeys3 Config，缺字段取默认，不继承或迁移 Inkeys2 布尔字段；默认绘制=非底栏时，橡皮擦/拖动主按钮/栏外点击=关闭。底栏包括普通底栏与居中底栏。
- 键盘钩子、Raw/WM_TOUCH 点击拖动来源合同、PPT COM ABI、焦点/权限策略均不在此次“恢复鼠标钩子”的隐含授权内。

## 直接确认：钩子已经编译并安装

1. `Inkeys/Inkeys.vcxproj:865`、`:866` 编译 `Input/MouseHook.cpp/.cppm`；旧 `IdtFloating.cpp`、`IdtDrawpad.cpp`、`IdtRts.cpp` 分别为工程 `None` 项（`:1206`、`:919`、`:926`）。不能再次启用旧 FloatingHook 或旧 RTS 补发路径。
2. `UI/Bar/Bar.Initialization.cpp:204` 调用 `MouseHook::Start`。`:213` 渲染注册失败时 Stop，`:224` 退出时先 Stop 输入生产者，之后才等待交互线程（`:229`）。`Start` 返回值目前被丢弃。
3. `Input/MouseHook.cpp:143–205` 已有独立 `std::jthread`、消息队列、auto-reset event 和 `MsgWaitForMultipleObjectsEx`；`:159` 安装 `WH_MOUSE_LL`。`:210–235` 为同步 Start/Stop API。
4. 因此“恢复”不是重新加一套全局钩子，而是修正当前回调/线程所有权，接回可用的自动收起业务，并验证现有 PPT 滚轮链。没有实机结果能证明当前安装一定成功；被忽略的 Start 返回值应在后续实现中可观测。

## 直接确认：现有线程、锁和实际调用链

### HookProc 热路径

| 路径 | 证据与实际工作 |
| --- | --- |
| `code < 0`、未 running、`WM_MOUSEMOVE` | `MouseHook.cpp:100–102` 立即 CallNext；当前移动事件没有业务/分配，是应保留的快路径。 |
| 左/中/右键 Down/Up | `:104–119` 同步 `Inputs::SetKeyBoardDown`。该 API 在 `Other/Other.Inputs.cppm:11–22` 使用 `libcuckoo::cuckoohash_map<BYTE,bool>::insert_or_assign/find`，不是固定原子 bit；可能取得 map 内部锁、首次分配，不能当作有界无锁写。 |
| PPT wheel 条件 | `MouseHook.cpp:121–129` 同步调用完整 `GetStateModeSnapshot()` 和读 `PptInfoState.TotalPage`，之后 QueueGlobalWheel，最后无条件 `return 1`。`IdtState.cpp:754–757` 的 snapshot 取得 `stateModeTransitionMutex` 并复制完整 StateModeClass。`IdtPlug-in.h:33–35` 的 CurrentPage/TotalPage 是普通 int，不能把该跨线程读宣称为发布快照。 |
| 栏外点击候选 | `MouseHook.cpp:132–137` 再读旧 `setlist.regularSetting.clickRecover` 和锁式工具快照，ScheduleClickCollapse 将截止点设为 now+100ms（`:45–50`），WakeHookThread 同步取得 lifecycleMutex（`:38–43`）。没有点位判定。 |

### Global wheel 的 Queue 命名并不代表先异步

真实链如下，箭头前半段都仍在安装低级钩子的专用线程上：

```text
HookProc
  → Ppt::QueueGlobalWheel(delta)             Ppt.cpp:384–387
  → PageControl::QueuePptWheel(delta)         PageControl.cpp:3388–3394
  → Snapshot()                              :579–583，snapshotMutex
  → InvokeDirection(0, delta < 0)            :623–651
      再 Snapshot() + callbackMutex + 复制 std::function
  → 初始化注入的 callback                   Ppt.cpp:210–217；IdtPlug-in.cpp:964–970
  → QueuePptUiBusinessCommand                IdtPlug-in.cpp:105–122
      页命令 outstanding.exchange、CapturePptSession/pptSessionMutex、
      pptUiBusinessMutex、deque push（可能分配）、notify_one
  → PPTLinkageMain                          IdtPlug-in.cpp:997–1101，真正消费
```

`MouseHook.cpp:127` 所写“共享渲染线程串行处理”与代码不符。COM 和模态框确实在 PPTLinkageMain 的业务线程（`IdtPlug-in.cpp:1013`），但是之前已有多个业务锁和可分配操作留在 hook 回调中。PageControl 原生点击/本地 wheel 走 Window Service overlay owner thread；渲染回调走 RenderPipeline；Bar Interact 另有线程（`Bar.Initialization.cpp:217`）。不能混写成“所有业务在渲染线程”。

### 自动收起也留在 hook 的消息泵线程上

- `ProcessPendingActions` 在 `MouseHook.cpp:60–78` 复制 collapseCallback（取得 lifecycleMutex），并在该线程直接执行任意 std::function；Run 在 wait/消息 pump 后直接调用（`:178–192`）。它不在 HookProc 调用栈内，但仍会阻塞下一次低级 hook 回调的投递处理。
- Bar 初始化传入的 callback 直接 `barState.fold=true; UpdateRendering(false)`（`Bar.Initialization.cpp:204–208`）。fold 是 IdtAtomic<bool>（`Bar.State.cppm:17`），因此这个单字段并非数据竞争；但 callback 的业务归属、延迟有效性和后续任意扩展依然不应留在 hook thread。
- `Bar.Main.cpp:279–305` 的 UpdateRendering(false) 普通路径跳过大部分 StateUpdate，但仍会 Notify/RenderPipeline::Request；若 finite mutation 上下文生效，还会读工具快照/收口事务。不能把通用 UpdateRendering 等价为纯原子 store。
- 当前延迟动作不检查 100ms 内是否换工具/关选项/切底栏/停止并重启等；连续点击更新同一个截止点，会把执行向后推。所需语义应由 owner 的可取消请求和新策略 revision 表达，而非无归属全局 bool。

## 直接确认的正确性缺口与潜在问题

### 点击收起未保留旧“栏内点击不收起”握手

旧 `IdtFloating.cpp:494–497` 在 Hook 中先把 ConfirmaNoMouMsgSignal=true，旧窗口收到本栏 Down 后清 false（`:826–827`），延迟线程只在仍 true 时收起（`:367–369`）。当前编译中的 `MouseHook.cpp` 不设置也不检查这个信号；Bar 只保留清 false 代码（`Bar.Interaction.cpp:934–937`），`CancelPendingCollapse` 全仓只有声明和定义，没有调用者。故当前 clickRecover 打开时，本栏点击与外部点击无法靠这段代码区分。恢复业务时不应复用这条已断开的 legacy 信号；改用 Hook 的原始屏幕点、已发布的交互范围/owner 接收结果以及可取消的请求身份。

### mouseUpPending 是未完成的遗留状态，非当前已发生的 busy loop

`MouseHook.cpp:52–58` 有 ScheduleMouseUp，`:86–87` 把 mouseUpDeadline 纳入等待；但 ProcessPendingActions 不清该 pending，只留下 Draw3 不补发旧 RTS Up 的注释（`:76–77`）。若以后恢复调用这个函数，500ms 后 NextWaitTimeout 将持续返回 0，Run 可能零等待自旋。**当前 ScheduleMouseUp 没有调用者，mouseUpPending 从不被置 true，因此不能将它描述为现有 CPU 卡顿的已证实原因。** 实现时清理这段孤立状态/API，或把它明确封闭为永不可调度；绝不能恢复旧 RTS 鼠标 Up 注入。

### 启停合同

- Start 先 Stop，再创建线程并等待 startup future；正常 Stop 在外部 owner 请求 stop、唤醒、join，最后清 callback，正常路径具有资源收尾顺序。
- `hookThread` 本身未被 lifecycleMutex 保护；多个线程并发 Start/Stop 会有竞态。当前发现的生产调用集中于同一个 Bar 初始化生命周期，未证明存在实际并发调用；应把“单一生命周期 owner”写成接口合同并测试，或在实现中串行化生命周期，不能在 HookProc 里新增这把锁。
- 任意 collapse callback 若调用 Stop，会 self-join；当前 Bar lambda 没这样做，不是已发生缺陷。取消任意 callback 执行即可消除扩展风险。
- WM_QUIT 在 `:187–188` 只 break 内层 PeekMessage，而非外层循环；Stop 使用 stop_token，当前正常退出不依靠 WM_QUIT。后续应明确 pump 终止状态。
- Start 失败后目前静默降级；规划中可要求一次性 owner 日志/诊断计数，禁止回调内日志。

### wheel 门禁、重复和会话

1. Hook 在非 Selection/ClickThrough 且 legacy TotalPage!=-1 时先调用 QueueGlobalWheel，然后总是吃掉系统 wheel（`:121–129`）。但是 Ppt facade 未 initialized、白板 active/expanded/switching、presentationVisible=false、delta=0 时会在之后的函数返回，无实际翻页（`Ppt.cpp:386–387`，`PageControl.cpp:3388–3394`）。所以目前有“已吞消息但业务拒绝”窗口。
2. Hook 不过滤 MSLLHOOKSTRUCT.dwExtraInfo 的 touch/pen 提升副本，也没有自己的 injected provenance 策略。PageControl 原生路径会过滤触摸提升 wheel（`:2321–2325`）；Message.cpp:61–77 和 UiPointerInput.cpp:702–704 已有签名判定可作为共用证据。不能因为 LLMHF_INJECTED 就把所有输入一概丢弃，精密触摸板/辅助设备与测试工具需明确兼容策略。
3. 当前一个非零 delta 只按正负调用一次，不累计 120，也不按绝对值倍数多翻（`PageControl.cpp:3390–3394`、`:2821–2822`）；0 不翻。这是现状，不等于合理的高分辨率滚轮规范。高分辨率的多次 30 或单次 240 需要产品选择，建议保留本轮可评审边界，另行明确累加规则。
4. PPT 上一/下一页在 `IdtPlug-in.cpp:107–109` 最多一个 outstanding；后续反向或同向请求都直接丢弃，释放发生在业务处理完成的 RAII（`:1019–1029`）。目前不是积累每格数，而是“待处理期间合并为一个”。EndShow 不在该 outstanding 门控中。建议保留页命令反压，明确结束页退出至多一次，并记录 admission/drop 计数，避免在 hook 把“已被现有队列合并”误认为可再走本地路径。
5. 业务请求捕获 PptSessionToken（`:110`），执行前验证 current session（`:1030`），退出在 `:1081–1088` 复查并使用 managed session guard。`Business/PptSession.h` 的 token 全为标量，可作为异步桥事件身份来源；不能在旧 wheel 排队后，换场了才捕获新 session，让旧事件翻新文稿。
6. `PageControl::QueuePptWheel` 先 Snapshot 判断 PPT，再 InvokeDirection 另 Snapshot；其间若白板接管，index=0 的 InvokeDirection 可能选择白板 previous/next callback（`:630–632`）。这是可构造的 TOCTOU 路径，应让“全局 PPT wheel”在同一业务准入中锁定 PPT 身份，不能通用回落到白板。
7. 如果 Hook 确认拦截，原系统 wheel 不会再由原生窗口消费；但异步桥和本地 WndProc 若各发一次会双翻。必须统一逻辑 admission/source，并有清晰的 suppress/pass decision。当前没有两路统一的 event-id 仲裁。

## HiMsg 结论：可选桥的出口，不能直接从 Hook 重压入

- 旧键盘转译链确实存在：`IdtDrawpad.cpp:95–102` 构造按键 ExMessage 并 `Window::Enqueue(drawpad_window, msgKey)`；`:230–250` 的 `KeyboardInteraction()` 从画板 HiMsg 按 Key filter 读取，解释方向/PgUp/PgDn 等按键并调用 Previous/Next。它消费的是画板队列，不是四个 PPT 控件窗口队列。
- 当前 `Inkeys.vcxproj:919` 把旧 `IdtDrawpad.cpp` 列为 None，活动 `IdtDrawpadFacade.cpp:47–50` 的安装函数为空；旧 KeyboardInteraction 不参与当前编译。现行 global wheel 不走 HiMsg，四个 PPT WindowSpec 明确 `bindMessages=false`（`IdtMain.cpp:3081–3089`），PageControl 消费本地 WndProc，翻页最终接入 `IdtPlug-in.cpp:964–970` 注册的业务队列回调。
- 因而 HiMsg 可以承载转译后的翻页输入，但当前须补明确适配消费者，不能把旧消费者当作活动实现；也不能仅由 bindMessages=false 推断整个应用从未有 HiMsg 翻页路径。仅 `Window::Enqueue(pptHwnd, wheel)` 不会再次调用 WndProc，也不会凭空执行翻页。
- Bar HiMsg 被 Interact 读取，但它的普通 wheel 暂无 PPT handler；已有活动指针还会丢弃额外坐标 wheel（`Bar.Interaction.cpp:775–779`）。不能把 hook wheel 填成 (0,0) 的普通本栏 wheel，期待自动翻页。
- `HiMsg.hpp:294–300` 为每条消息 make_unique，`:387` 取得 queueMutex 后才入队；Window::Enqueue 还按 HWND 查 role（`Window.cpp:2385–2401`）。现有 API 不是适合低级 hook 的有界无分配入口。若采用 HiMsg，必须先经轻量异步入口，在 hook 返回之后再调用，且补明确消费者和私有 source/session metadata。
- 推荐最小方案：保留现有 dedicated hook/message pump，仅将 POD 事件经固定容量非阻塞 mailbox + 合并 wake 交给**已有窗口/交互 owner**；consumer 再调用唯一的 PPT 业务队列，不为翻页强行改通用 HiMsg。若用户更希望用 HiMsg，则可把同一个 consumer 的下游定义为“带旁表身份的逻辑全局 wheel”，消费后仍进入现有 business queue；实现前需确定消费者、filter、拖动时处理和容量失败语义。

## 建议实现合同（待主会话/用户批准后实施）

1. HookProc 只进行 code/running/message 的快判定，读取已发布的紧凑 route/policy/generation 快照，复制必要 MSLLHOOKSTRUCT 标量，进行有界无分配投递；不取得配置/工具/会话/HiMsg/生命周期 mutex，不调 UI/COM、任意 std::function、sleep、join、磁盘或逐事件日志。
2. 发布 gate 来源于同一套新 Inkeys3 Config 和当前工具/底栏/工作区/PPT 活动身份；merged/separated 皆有效，普通底栏与居中底栏同一判定。Hook 不读取四个 legacy bool。快照实现必须保证真正有界且无数据竞争：不能用 atomic<shared_ptr> 假定 lock-free，也不能用无限 seqlock 重试/未受保护的双缓冲；不稳定时立即 fail-open。
3. 若保留 Inputs 对三个鼠标按钮的兼容缓存，鼠标专用 fixed atomic bits 可从现有 Set/Is API 分流；键盘路径保持不变。当前 IsKeyBoardDown 的产品鼠标拖动消费者已迁出，老 IdtFloating/IdtRts 不编译；不能为了“恢复”重新把 UI 松手判定接回全局缓存。任何延后写 map 的方案都需明确 staleness 兼容合同。
4. Hook 的 event 取得捕获时 session/routing identity。消费时再次校验身份；当前 PPT business request 必须继续携带 expected token，不能重新捕获新场次而改认旧事件。
5. mailbox 满、停止、目标窗口失效、wake 投递失败、没有可用消费者时，PPT wheel fail-open 调 CallNext；只有路由允许且入口确实接纳后才 return 1。唤醒可以合并，数据不能按唤醒次数计数。使用固定预分配队列/单次 PostMessage 的取舍需在设计说明，保证无 unbounded retry 和迟到跨代投递。
6. 外部点击只产生 owner 收起请求，不在 hook thread 运行收起 callback/计时业务。owner 使用原始屏幕点与已发布/已接收的本栏信息排除栏内操作，设置/模式/底栏/workspace revision 改变时取消过期请求；100ms 是原先行为，可保留为 owner 的可取消延迟，不是 hook 等待。
7. 不改变旧 Raw/WM_TOUCH 入口与会话终态，不启用旧 RTS Up 补发，不让自动 fold 终止正在进行的主按钮拖动（该点由设置/Bar owner 研究配合）。不新增后台常驻线程。

## 可评审的无 GUI 验证计划

- 纯生产策略测试：负 code/MOVE/disabled/gate 不稳/queue full 必须快速 CallNext；admitted wheel 恰好一次且 suppress；delta=0/正负/高分辨率按最终规则；触摸/笔提升、注入策略；无回调分配和下游锁依赖（故意停住 consumer/持业务锁时 producer 仍能完成）。
- 队列生命周期测试：Start失败、重复Stop、请求Stop并join、旧wake/旧包跨restart、饱和/drop计数、合并wake drain全部、投递失败fail-open。timer测试证明截止点消费或取消后回到INFINITE/正时间，不存在已过期永久pending。
- PPT 业务测试：普通/末页/结束黑页、相反方向快速交错、outstanding重置、旧session排队后新session、白板接管/Unknown/隐藏/设置或模态窗口、全局桥与本地WM_MOUSEWHEEL双入口恰好一个逻辑命令。测试必须调用生产admission，不能复制一个同公式假实现。
- 新四项策略测试由设置/Bar研究覆盖；本研究补栏内Down不收起，真正栏外Down可按新策略收起，延迟内切设置/工具/底栏不执行过期动作，drag期间自动收起不假Up/Cancel。
- 性能只计“自有回调体”与队列部分，不能把CallNext下游hook耗时归因本进程。建议记录聚合计数/峰值，后台输出；在模拟consumer阻塞和高事件量下确认成本有界。具体P95/P99数字待选硬件和测量条件后确认，不把Windows hook超时时间当性能目标。无窗口基准不证明真实系统光标无卡顿。
- 批准实现后复用现有 `InkeysRepo.sln Debug|ARM64`、`InkeysHeadlessTests --no-window` 和适用生产探针，遵守PATH规范化与5分钟时限。真实PowerPoint/WPS、管理员前台/UIPI、Win7/触摸板和系统光标手感留人工验收；规划期不运行。

## 主会话还需明确的产品取舍

- wheel是否继续“每个非零包一次且outstanding合并”，还是按WHEEL_DELTA累计；若后者，120倍数/余量重置/方向反转/会话切换一起定义。
- 全局PPT wheel的空间范围是否沿用“绘制模式+有效放映就全局接管”，还是限活动放映窗口/画布；当前Hook缺乏前台/命中区域条件，不应在性能修正中偷偷扩大或缩小语义。
- HiMsg是偏好还是硬要求：现有PageControl根本不绑定它；最小风险是异步桥之后复用现有PPT business queue，而非为了命名把通用消息层改造。
