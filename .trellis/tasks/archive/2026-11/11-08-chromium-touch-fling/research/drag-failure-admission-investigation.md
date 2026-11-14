# Research: 拖动全失效的接触准入与生产探针复查

- Query: 增加触控板/笔漫游后所有设备不能拖动、触控板仅偶发成功；上一轮前端修复未解决。复查接触 Down → 导航历史 → 动力学及测试覆盖。
- Scope: internal（只读源码与已有任务记录；本文件为唯一写入）
- Date: 2026-11-12

## Findings

### 1. 当前 consumer 没有发现“惯性关闭导致全部跟手失效”的通路

- `Draw3.ContactInput.cpp:78–81` 的唯一导航设备 predicate 已接受 Touch、MouseLeft、Pen；笔尾沿 Pen 的 inverted 元数据进入。
- `Draw3.ContactInput.cpp:785–825` 在同一导航截面初始化 Down、入命令队列并发布导航 Down；Move/终态沿同一环。`DrainNavigationHistory:930–945` 等待对应 Down 出队，未新增触控板专用开关来全局阻断接触历史。
- `Draw3.DrawingController.cpp:13910–13911` 的真实 Run 已构造 CanvasPanInput 并 BindTouchpad；不是只在 probe 里绑定。
- `Draw3.DrawingController.cpp:15979–16000` 先核对 ContactAdmitted 和加载状态，再在 Selection 调用 HandleSelectionDown。获准路径 `:3171–3177` 执行 CancelNative(atDownQpc) → Register，之后 `Step:2936–2963` 消费真实历史。
- `Draw3.CanvasNavigation.cpp:1088` 仅在终态用 allowInertia 门控释放；`StopFling:1159` 只停止自由曲线/清速度，不清 detector 或 held。它不能单独导致真实 Move 完全没有几何输出。
- `SetTouchpadNavigationEnabled:Draw3.WindowControl.cpp:842–851` 只有 enabled/inertia 变化时才取消/发布 owner 更新，未观察到每帧无条件 Cancel 的循环。

以上为局部源码排除，不能宣称真实设备输入已正常送达。

### 2. NativeRunning 是新增的共同拒收点，但其是否命中需要现场证据

`HandleSelectionDown:Draw3.DrawingController.cpp:3146–3152` 对任何 Selection 接触查询 NativeRunning(真实 Down QPC)，若命中则直接 DiscardUntilTerminal，连 Register 都不会出现。该路径覆盖鼠标、触摸、笔、笔尾，是新增逻辑中可以同时解释四类接触均不准入的单一 consumer 门禁。

当前实现已经由确认 Status Running 建立区间，而非 SetContact S_OK 直接伪 Running。`Draw3.TouchpadNavigation.h:150–161` 的 RunningAt 查询 matching admissionRevision 的真实区间；被淘汰历史的 unknownThrough 仅对过去 QPC 保守拒收，不会无条件拒绝未来所有输入。未从当前源码证明区间会永久残留并持续覆盖未来 QPC。

因此：若新现场出现 `pan-reject ... reason=native-running`，应继续审查真实 DM 状态为何长时间 Running；若没有该记录，不能把“不出现 pan-register”自动归因于此门禁。原附件只属于修复前，不能替本次结果。

### 3. 生产 Run 与数值 probe 的关键差异

`CanvasPanProbeRig::Down/Move/Up:Draw3.DrawingController.cpp:6652–6663` 直接调用 Coordinator.Publish*，绕过 RTS、Win32 owner、系统命中及 producer 准入。

`CanvasPanProbeRig::Frame:6678–6695` 调用真实 HandleSelectionDown 和 Step，足以检验被注入的接触在 consumer 的数值行为；它并未运行完整 Run 的工作区/加载生命周期、实际 DM owner 或 Windows 输入派发。Frame 还在 `:6688` 主动 SetCanvasPanWorkspace，真实产品则在 `publishWorkspaceReadyAfterPresent:13428–13439` 经准备/呈现后才发布工作区。

这解释了为何 downstream probe PASS 只能排除有限范围的数值/状态问题。

### 4. 上一轮“输入隔离回归”的前半是自建策略模型

`InkeysHeadlessTests/draw3_contact_tests.cpp:2097–2105` 的 deliverThroughPolicy 由测试自行定义：仅当 options 包含 EXPLICITHITTEST 且 INPUT 时调用 ordinary()。ordinary 在 `:2088–2092` 直接 PublishDown/Move/Up。

负对照旧 flags 不会调用 ordinary、当前 flags 会调用 ordinary，是该测试代码自身预设的结果。它没有调用 Windows input dispatcher、RTS 或 COM viewport，所以“真实 Coordinator 收到三事件”不能证明 Windows/RTS 在新 flags 下实际可达。

`Draw3.WindowControl.cpp:733–755` 的 production frontend probe 也以 options bit 判定 AutomaticallyAssociates/InputVisible。共享 setup routine 可以验证真实代码传什么参数；API 行为模型是否适合 RTS/DM 共存仍需独立证据。

### 5. 已确定的另一个验证漏洞：隐藏 HWND 测试消息撞号

- 新增生产 owner pump：`Draw3.WindowControl.cpp:44` 的 kTouchpadNavigationMessage = WM_APP + 0x3D5。
- 既有隐藏测试：`Draw3.HiddenWindowTest.cpp:84` 的 kHiddenCaptureProbeMessage 同为 WM_APP + 0x3D5。
- `HiddenDrawpadWindowProc:88–93` 遇该消息直接处理 capture 并 return，先于 DrawpadMsgCallback，因此生产 owner 的 pump/stop 消息无法透传到 WindowController。

这是确定的测试接线冲突，不能拿它解释产品真实拖动全失效。用户未授权改代码，未修复此冲突。

## Files Found / Code Patterns

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp`：接触准入、Run/Probe 接线与原生仲裁。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.cpp`：唯一接触 producer、导航环、水位、准入代次。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.CanvasNavigation.cpp`：真实跟手/释放内核，惯性门控仅影响终态。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.TouchpadNavigation.h`：Running 历史与 admissionRevision 仲裁。
- `InkeysHeadlessTests/draw3_contact_tests.cpp`：前端“输入可达性”模型与直接 Coordinator 注入。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.HiddenWindowTest.cpp`：隐藏 HWND 的消息撞号。

## External References

本研究未引入外部 API 事实。Direct Manipulation/RTS 所有权合同由并行 frontend 研究重新核对，不直接继承上一轮“flags 已保证输入可见”的结论。

## Related Specs

- `.trellis/spec/native-desktop/canvas-pan.md`
- `.trellis/spec/native-desktop/input-and-ink.md`
- `.trellis/tasks/11-08-chromium-touch-fling/research/multi-input-regression-review.md`（旧验证与明确的硬件证据边界）

## Caveats / Not Found

- 本轮未运行 GUI、设备输入注入、编译或测试；未修改产品、测试、spec、既有任务文档。
- 未对源码做任何 Git 操作；961ffdf8 基线由主会话独立对照，本文件聚焦当前生产链和 probe。
- 当前没有修复后的新现场日志，不能断言所有设备故障均由 NativeRunning、RTS 被截断或隐藏测试撞号造成。
- 暂未在接触 consumer/动力学中发现一个无条件、足以解释全部跟手拖动失效的确定缺陷。最强后续分界是：新现场 Down 是否到 producer、是否被 ContactAdmitted 拒绝，以及有无 pan-reject/native-running。

## 最新用户纠正与时序（取代此前口述）

用户进一步纠正：拖动尝试后切回画笔也不能画；Pen hover 跟随正常，落笔后光标固定在 Down 点、不出墨，Up 后恢复。先前“同一运行切回画笔仍正常”的口述已被纠正，不能继续据此排除全局接触链故障。

用户新日志经主会话逐序核对：

| 序列 | QPC | 事件 |
| --- | --- | --- |
| seq2–680 | 240479279868–240494105709 | 多 context 的 rts-batch，published=1；全部发生在首次 DM 初始化记录之前 |
| seq681 | 240498149214 | UI 线程 touchpad-init result=0x80070005 unavailable |
| seq682 | 240587869978 | drawing 线程 leave-move-mode |
| seq683 | 240685325006 | 后续 UI 线程 DM 初始化成功 |
| seq684 | 240693019452 | 同一 drawing 线程再次 leave-move-mode |

该粘贴在 seq681 后没有 rts-batch；没有 pan-register/pan-reject/pan-step。未取得独立原始文件，本表引用主会话对用户回贴的核对结果；不自行把无频率 QPC 差表述为精确硬件秒数。

### published=1 的准确语义

`ContactInputCoordinator::PublishMove:Draw3.ContactInput.cpp:834–883`：

1. 没有匹配 Producing/Quarantined record 时返回 false（:838–853）。
2. 若找到的 route 为 Quarantined 或已变更，也因不是 Producing 返回 false（:855–861）。
3. 只有发布 snapshot（:865）、对导航设备 AppendNavigation（:866–870）、再次核对 Producing（:871–877）后，才在 :883 返回 true。

所以普通产品 `rts-batch published=1` 是样本真实写入 Coordinator 的证据，不是“无 active contact 也返回成功”。RTS 的编译期 InterruptedStrokeSimulation 另有吞点成功路径，但 `Draw3.InkPrediction.cppm:82` 明确为 false，当前产品路径不使用该包装。

由于这些成功批次全在 DM 初始化记录之前，它们只证明此前 producer 正常，不能证明触控板初始化后 RTS 接触样本仍到达。不得据此把根因直接移动到 consumer，也不得把早期 published=1 误表述为失败发生时输入仍持续写入。

### consumer 未整体死锁，但接触样本丢失边界仍需收紧

两条 drawing 线程 leave-mode 表明该线程后来仍可运行 refreshCanvasPanMode，排除“自首次失败起永久锁死在某个调用”作为全部现象的解释；不排除临时阻塞或某条输入门控。

RTS 入口本身未包含 Selection/CanvasPan 判断。`StylusDown:Draw3.RealtimeStylus.cpp:1180–1289` 在 callback/publication gate、decoder 和 TouchPad source 过滤后直接 Coordinator.PublishDown。ContactPublicationGate 只在初始化候选切换时 Enable，未发现随 Selection 变化关闭该 gate 的调用。

在从未发生触控板手势的初始状态，bridge Running=false、unknownThrough=0；没有实际 DM Status Running/区间，首次 Down 不应在 NativeRunning 被拒。现有 consumer 未找到一个能同时解释“进入拖动后四类全失效、随后画笔也无接触样本、hover/Up可恢复”的新增无条件门控。

现象与 DM 初始化/激活后接触阶段输入路径改变相容，尚需 frontend 的官方 API 和实际 Owner 调用顺序核对。当前 :681 的 HRESULT 是整个初始化链聚合值，缺少失败阶段，不能仅凭 E_ACCESSDENIED 指认 Activate/Enable/SetViewportOptions 中哪一步。

### idle/drain 的源码边界

`WaitDequeue:Draw3.ContactInput.cpp:1149–1164` 先 TryDequeue、复查 wakeGeneration，再等 auto-reset event；PublishDown 入队后 SignalWake，不是仅 Move 的无唤醒语义。

`DrainIngressBatch:Draw3.DrawingController.cpp:172–186` 在 commandBoundaryPending 时会暂停 Down 出队，直到实际 canvas command 在安全边界处理后清该标记（:19403–19404、:20481–20482）。该限制也存在于非触控板路径，当前没有证据显示该标记因 DM 初始化永久保留；单凭缺少 pan-register 不能确定 Down 未入队、消费者早退还是没有后续 producer 样本。

最新调查应保留两个证据层：早期 RTS 样本确实发布成功；DM 初始化之后的 producer 活性尚无正证据。不能继续把上一轮“只修 flags 即恢复真实递送”或某个下游纯数值测试 PASS 当作现场根因闭合。
