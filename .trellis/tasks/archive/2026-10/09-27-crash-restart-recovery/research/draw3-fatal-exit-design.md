# F-026 / F-027：Draw3 持久图形恢复失败后的退出、输入与恢复设计

日期：2026-09-28。范围为当前集成产品代码的只读调用链；未运行构建、无窗口或 GUI 故障注入，未修改产品源码。证据中的行号对应本次持续变动工作区，恢复实施时应以符号重新定位。

## 触发和当前调用顺序

1. `DrawingController::PresentFrame`/光栅失败可设置 `graphicsRecoveryPending_`。下一轮 `DrawingController::Run` 在 `Draw3.DrawingController.cpp:6525-6556` 释放 GPU history 后调用 `TransparentPresentationController::RecoverFromRuntimeFailure()`；若 presenter 恢复失败，或随后 `historyGpuCache.Initialize` 失败，调用 `WindowController::RequestExit()` 并直接 `break`。此时并未执行 `CanvasCommandType::PrepareExitAutoSave`。
2. `RecoverFromRuntimeFailure` (`Draw3.TransparentPresentation.cpp:841-899`) 先 `ReleaseAttempt`；设备丢失时重新创建 Hardware-first/WARP-fallback 设备，再在既有主 HWND 上重试 DComp/ULW。已绑定 DComp 的创建期 `WS_EX_NOREDIRECTIONBITMAP` 若不能被 Window Service 清除，ULW 样式写后读回会拒绝；该 Windows 条件尚未实测。两种 DWM 透明模式保持禁用，交换链保持用户已在 Win7 SP1+仅 KB2670838 实测可用的 `FLIP_SEQUENTIAL`，不能用 DWM/bitblt 充当补丁。Win7 的 Hardware FL11.0、无 FL11.0→WARP、ULW 真正 Present 仍须分别实测。
3. `RequestExit` (`Draw3.WindowControl.cpp:559-563`) 只设置 Draw3 自己的 `exitRequested_` 和 input wake，不写进程 `offSignal`。Host 绘制 `jthread` 在 `DrawingController::Run` 返回后 (`Draw3.Host.cpp:1193-1226`) 补发 drawingActivity=false，销毁 `drawing`、presenter、renderer、graphics，最后 `running=false` 并通知条件变量。它没有在异常/非正常 Run 返回时关闭 bridge、RTS 或执行最终持久化屏障；`firstFrameReady` 在该分支也没有同步撤销。
4. `RealTimeStylusInput::Initialize` 在 `Host::Start` 调用线程执行，`Shutdown` (`Draw3.RealtimeStylus.cpp:2615+`) 会禁用/解绑 COM 插件、关闭 producer contact 并 `CoUninitialize`。现有唯一可靠调用在 `Host::Stop` (`Draw3.Host.cpp:1316-1358`)，而 `IdtMain.cpp:2190+` 的主循环只等独立全局 `offSignal`/启动故障，没有观察 Host 意外停止。故恢复失败后可能出现 Host 绘制消费者已消失、RTS/WindowController 仍附着、主进程仍运行。输入队列有容量上限，并非已证实无限内存增长；新画笔请求无法得到正常绘制，是条件性严重输入失效。
5. `IdtState.cpp::ReconcileDraw3Presentation` (`:380-580`) 依据 bridge/latest runtime 的旧 `firstFrameReady`、target/content revision 对 Window Service 提交 Primary/Presentation；没有检查 `runtime.running` 或致命代次。主 Drawpad 即使带 `WS_EX_TRANSPARENT` 也不能作为操作系统输入穿透保证。Window Service 已有成对 `SetDrawpadSurfaceVisibility(Hidden)`、可见性读回与主画布 capture 释放 (`Window.cpp:1653-1733`)，但此异常链不调用它；另一状态线程还可能按旧 ready 重新显示。

## 数据所有权和可失去的范围

| 数据 | 当前所有者/提交点 | 恢复失败后准确结论 |
| --- | --- | --- |
| 已 durable 的 Desktop/PPT UInk 与索引 | 各自单 worker，UInk 先提交再原子索引 | 当前失败路径没有主动删除旧文件；既有最后有效文件/索引应保留，但需独立故障注入和严格回读证明。不能把文件有效称为新进程画面已恢复。 |
| 已提交到内存 `InkCanvas`/`CanvasRuntimeHistory`，本次尚未排入保存 worker | `DrawingController::Run` 局部 active/parked slot，退出快照由 `PrepareExitAutoSave` 触发 | 直接 `break` 后 Host `drawing.reset()` 释放这些槽；之后 `Host::Stop` 即使调用 `bridge.StopWithFinalCommand`，等待谓词见 `!running` 后会记录 `controller_stopped`，无法追补新的快照。Desktop 自上次 Clear 以来的 Stroke、PPT 当前页 dirty 内容均可能没有新的恢复点。 |
| 仍活动的 contact | `ContactInputCoordinator`、`RuntimeStroke::realPoints`、modeler，Up 后才进入权威 document/history (`DrawingController.cpp:7648-7870`) | Fatal `break` 早于 RTS Shutdown 的终态，尚未 Up 的真实点没有安全提交；不能承诺当前笔迹零丢失。正常 Stop 的 barrier 被 `processCanvasCommands` 在 active 为空后处理，而此 fatal 路径绕过整个收尾循环。 |
| 双画布窗口/输入捕获 | Window Service owner thread，RTS 另由 Host Start 调用线程拥有 | Host 绘图结束不等于 HWND 隐藏/销毁。旧 Primary 可继续可见/截取；Presentation 窗口仍在；状态线程可能重显。是否在目标系统真实截取由 GUI 故障注入验证。 |

### 另一个已确认的正常退出保存缺口：parked Desktop

最小可达轨迹：启用 Desktop 自动保存，在 Desktop 画至少一笔且不 Clear；通过 `SetPresentationTarget` 进入 PPT（或 `SetWorkspace` 进入 Whiteboard）；在该 workspace 正常关闭。切换代码 (`DrawingController.cpp:6035-6091,6138-6174`) 只 `capturePresentationAutoSave()`，随后把 Desktop 文档/history 交换进 `desktopSlot`，没有 Desktop 保存。`PrepareExitAutoSave` (`:6243-6258`) 只调当前 `captureDesktopAutoSave(Exit)`，而它 (`:1890-1895`) 以 `activeWorkspace` 调 `DesktopAutoSavePolicy::ShouldCapture`；该策略 (`Draw3.AutoSave.cpp:703-707`) 仅接受 Desktop+开关开启+有可见内容。因此 PPT/Whiteboard 活动时直接 false；后续循环只提交 parked Presentation slots，不提交 `desktopSlot`。全文件 Desktop 捕获调用点只有 Clear/Exit。若此前有 Clear 提交，旧 UInk/索引保持但不含当前区间；若此前没有，当前区间无 Desktop Exit 文件。跨进程 Desktop 自动可见恢复目前也未开放，不能把这个写盘缺口与新进程可见性混同。该结论是静态路径确认，尚无隔离 Host 运行红灯。

最小安全修补切口：让既有 Desktop 捕获函数以**显式 Desktop slot 的** `document/pageRuntimeStates/currentPageIndex` 作为值源，保留相同可见 history 过滤、workspaceGuid/pageGuid、UInk schema、policy 和 Host worker；Clear 继续传 active Desktop，Exit 在 active Desktop 与 parked `desktopSlot` 中恰选一份，绝不从当前 PPT/Whiteboard 文档代取。不要在切换时新增保存触发（现合同只允许 Clear/Exit），不要让 Whiteboard/PPT 借用 Desktop 开关。提交拒绝或失败要保留旧索引并记状态，不能静默称成功。

## 最小安全 containment 与完整恢复的边界

1. **先阻断静默失效**：Host 把运行期不可恢复的 presenter/device/history-cache 结果发布为只增一次的 fatal reason/generation；绘制线程不在回调中同步等待窗口或主线程。主协调器必须在启动成功后观察这个事件或 Host unexpected stop，而不能仅等 `offSignal`。产品命令和新 contact 需进入关闭/拒绝状态；RTS 的 `Shutdown`/COM 清理由其 Start/Stop 所在线程经 `StopProduct` 完成，不能从绘制线程临时调用。原有 `ProductRunning()` 在 controller 已毁后拒绝命令，但该时刻太晚，且状态发布入口仍可写 bridge。
2. **立即移除输入拦截面**：在主协调器观察 fatal 后，通过 Window Service owner thread 提交双表面 `Hidden`，并取消 Drawpad capture；优先使用现有 `SetDrawpadSurfaceVisibility(Hidden)` 的写后读回，失败则执行 owner-thread `HideAllUserWindows()` 作为受控退出兜底并记录结果。`IdtState::ReconcileDraw3Presentation` 和其提交的 `stillDesired` guard 必须共享 fatal generation 门禁，否则旧 revision 的异步可见性命令可在隐藏后重新显示 Primary。隐藏不应等待磁盘 worker，也不能以 `AdmissionBlocked` 或 `WS_EX_TRANSPARENT` 代替。
3. **受控停止而非假自动恢复**：确保 Stop 路径停止 bridge/RTS，排空已接受的 worker 请求，再有序释放 Host/Window Service；控制信号走 `SetOffSignal(1)`，不把 renderer fatal 伪装为 UEF 崩溃或无条件 `-CrashTry`。记录 fatal reason、模式、设备 HRESULT、窗口代次、双窗隐藏结果、退出 barrier 和 worker 终态。若 controller 已返回，`controller_stopped` 必须继续记 FAIL，不能以 `CloseAndDrain` 覆盖。这个 containment 能处理“程序活着但绘图已死/窗口仍拦截”，**本身不能保住当前活动 Stroke 或未排队的文档**。
4. **要达成数据收口，还需在 controller 存活时完成 CPU 屏障**：可把 `PrepareExitAutoSave` 的现有 active/parked slot 捕获提为单一事务，fatal 分支在释放 controller 前复用；先阻断新输入，按最后已接受真实样本把活动 contact 合法封口，或明确放弃并报告，再把完成 Stroke 登记到同一 Canvas/history，随后提交 Desktop/PPT（含 parked Desktop）快照，等待 worker 终态。不能在 GPU 设备失效后调用依赖 L2/renderer 的普通 Up 绘制路径并假称保存成功；需要单独的 CPU-only seal 合同，保持 Cancel 不转为可持久笔、PPT page identity 不串用、同一 stroke 不重复追加。若做不到，应把当前活动 Stroke 和尚未排队的完成内容列为发布风险，不能升级 F-026 为完全修复。
5. **跨 HWND 代次无损 DComp→ULW**：还需冻结命令/RTS、取得权威 CPU document/history/scene、排空存储、停 Host、等待 Bar/PPT/Setting 客户端、顺序重建整条 Window Service owner 链，在 legacy-compatible 主 Drawpad 上启动 ULW-only Host，并恢复 scene 后得到首次成功 Present 才显示。这不是 `RecoverFromRuntimeFailure` 内同 HWND 再试一次能实现的；`Host::Start` 会 reset bridge 并新建 controller。保留两个 DWM 禁用和 FLIP，不用改画质/输入采样来绕过。

## 可执行验证切口与状态

- **无 HWND，最先做**：把 fatal publication/one-shot generation、产品命令 admission 和“fatal→Hidden/Stop”决策放进生产共用的窄状态对象；headless 测重复失败只触发一次、Stop/正常退出不误报、启动新代次清旧 reason、迟到 UI ready/visibility 请求被 fatal guard 拒绝。测试必须调用实际生产状态对象，不能复制一套决策。此测试不能证明 USER32 的双窗隐藏。
- **无 HWND，Desktop 缺口 red→green**：从相同生产 Desktop snapshot 捕获 helper 构造 active Desktop 与 parked Desktop/PPT、Whiteboard 两组文档，断言 Exit 仅导出 Desktop 的 workspaceGuid/pageGuid/可见 Stroke、空页与开关关闭零请求、恰好一次提交。以隔离目录的现有 `DesktopAutoSaveService` 测 UInk/index 严格读回及失败时旧有效文件不变。只测 policy 布尔值不足以证明实际 parked slot 被捕获。
- **无 HWND，F-026 数据屏障**：若实现 CPU-only seal，生产 helper 需覆盖 Down→Move→fatal、Up 已消费但 GPU Present 失败、Cancelled、多 contact、PPT 同页/跨页、Desktop parked/active；验证真实 `InkCanvas` 与 `CanvasRuntimeHistory` 同一 Stroke ID/可见集合，保存 worker 对同一场景的文件内容与状态。若没有该 helper 或隔离进程故障入口，保持未验证/发布门禁。
- **需 GUI 明确授权**：隔离配置/文档与专用进程，在 DComp 成功后注入运行期 style 拒绝、device-lost 及 history-cache 失败；记录 HWND generation、真实 `GWL_EXSTYLE`、Primary/Presentation 可见性、capture、RTS 是否仍发布、主进程是否自动进入受控退出、最后已提交文件回读。强杀不能替代 presenter 故障。Win7 SP1+仅 KB2670838 的 Hardware FL11.0 与无 FL11.0→WARP、ULW FLIP 首帧/resize/失效另列矩阵；本机 Win11 ARM64 构建不能填 PASS。

## 结论状态

- F-027 静态设计缺口与进程存活但 Host 终止的调用链：**confirmed（源码）**；Windows 样式拒绝、透明输入拦截的实际设备触发：**未验证**。
- F-026 Fatal Run 早于 exit barrier 释放局部 CPU 文档/活动 contact：**confirmed（源码）**；每类具体丢失字节和已提交文件故障恢复：**未验证**。现有已提交文件并未被此路径主动覆盖/删除，不应称数据损坏已实测。
- Parked Desktop 正常退出无 Exit 捕获：**confirmed（源码可达）**；隔离进程 red→green 与实际业务入口：**待验证**。应独立建 finding 并优先修复，因为它不依赖 DComp 故障即可发生。
- 本报告只提供设计；没有修改产品代码、运行测试或宣称自动重启/数据恢复成功。
