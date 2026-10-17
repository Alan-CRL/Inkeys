# Research: 设置、资源管理器与画布输入生命周期

- Query: 追踪 Setting Show/Hide/Toggle、工具切换的 Setting owner 提交、资源管理器入口、焦点/鼠标捕获与 RTS 准入，核查能否形成仅画布失效的等待或状态残留；不运行 GUI、不修改产品。
- Scope: mixed（当前工作区源码、项目规范、既有任务研究、Microsoft 官方 API 文档；外部历史问题报告单独标为线索）。
- Date: 2026-10-15（任务于 2026-10-14 发起；源码行号为本次只读调查时工作区）。

## Findings

### 1. 结论与置信度

- **高置信度、源码直接确认：** 设置显隐本身没有替换/销毁 Drawpad HWND，没有调用 RTS disable/rebind，没有给 Drawpad 设置 `WS_DISABLED` 或 `WS_EX_TRANSPARENT`。Explorer 按钮只 ShellExecute 外部 `explorer.exe`，没有应用内文件选择模态流程。
- **高置信度、源码直接确认：** 所有普通工具状态同步先等待 Setting owner 提交，再发布 Draw3 状态；普通 Window Service 命令默认无等待截止。因此 owner 提交停住确实可能挡住某次状态发布，但不能由视频里的 `tool=0` 推导这个情况。
- **高置信度、历史与当前源码对应：** 2026-10-13 的“设置渲染持 ImGui 锁同步等 Setting HWND”闭环已有 thread-local defer guard；设置 owner-thread 等 overlay 根刷新的闭环也有拒绝/回滚。当前调查没有找到绕过 guard、又必然导致本视频故障的调用。
- **中低置信度、待采样候选：** Setting 与 Drawpad 分属两个 HWND 线程，却在绘制态建立 owner/owned 关系，Windows 会自动附加两线程的输入队列；Setting 显示时主动激活/聚焦，隐藏与 ImGui session 销毁没有应用侧显式 capture 清理。必须读取真实 GUI 线程的 capture/focus/active 和 RTS Down 回调计数，才能判断是否残留输入路由态。
- **旧产物观察已校正，DXGI 锁风险下调为独立源码候选：** 发生设置白屏的隔离副本实际是 edition=`3.0.0-20260811a` 的旧 Debug 二进制，当前 HEAD 源码 edition=`3.0.0-20261014a`；不是当前 HEAD 的新复现。旧副本可处理 WM_CLOSE，关闭设置后继续绘制，Explorer -> Select -> 关闭 Explorer -> Pen 也绘制成功，RTS Down/Up 均见 published=1。持续 Bar 操作、恢复绘制与无 cycle 的 WCT 削弱永久共享 UI/DXGI 死锁的解释。当前源码的 DXGI/ImGui 锁顺序仍值得独立验证，不能据旧副本白屏建立因果（见 5.4）。
- **尚未确认本次根因。** 主会话视频审读已更正：`tool=0` 是 Pen，`mouseLifecycle=0/0` 是橡皮光标位置/动画状态，dirty 为空的 `success=0` 不等于 Present 失败。本研究不据这些字段宣称 selection 滞留、Draw3 停线程或 GPU 故障。

### 2. Files found

| 文件 | 用途 |
| --- | --- |
| `Inkeys/Inkeys/UI/Setting/Setting.cpp` | 显隐 API、ImGui WndProc、业务 FIFO、渲染 coroutine 和 drain |
| `Inkeys/Inkeys/UI/Setting/Setting.Base.cppm` | 首帧 CreateSwapChain、RTV、resize 与 cleanup |
| `Inkeys/Inkeys/UI/Setting/Setting.SessionState.h` | 设置可见性、resize、occlusion 与 session 重建决策 |
| `Inkeys/Inkeys/UI/Setting/Setting.Widgets.cpp` | 标题栏关闭按钮的实际 ImGui 点击行为 |
| `Inkeys/Inkeys/Window/Window.cpp` | 两类 HWND 线程、命令等待、owner/Z 序、显隐、click-through |
| `Inkeys/Inkeys/Window/Window.CommandCompletion.h` | future 等待与可选 deadline |
| `Inkeys/IdtState.cpp` | stateMode、owner desired/applied、Draw3 发布与 250ms 重试 |
| `Inkeys/IdtMain.cpp` | 窗口创建样式、RTS Host 接入、绘制线程显示上下文回调 |
| `Inkeys/Inkeys/Business/ComponentActions.cpp` | Explorer 等外部程序启动 |
| `Inkeys/Inkeys/UI/Bar/Bar.Button.cpp` | Settings/Explorer/工具按钮实际回调 |
| `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp` | 单一 Bar 交互线程内同步执行点击回调 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp` | bridge 消费、RTS 初始化、输入准入门 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Product.cpp` | Drawpad WndProc 转发和产品调用锁 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp` | selection/activation 状态、鼠标与 pointer 路由 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp` | 唯一 RTS HWND 绑定、enabled 与 sync plugin |
| `Inkeys/additional/imgui/imgui_impl_win32.cpp` | 实际 capture/focus backend（Dear ImGui 1.92.7） |
| `InkeysHeadlessTests/window_tests.cpp` | Settings owner/Z 序测试，使用 probe WndProc，不是真实 ImGui/RTS |

### 3. 设置显隐、窗口拓扑和资源管理器调用链

#### 3.1 Show / Hide / Toggle

1. 主栏 Setting 按钮直接调用 `Setting::Toggle()`（`Inkeys/Inkeys/UI/Bar/Bar.Button.cpp:695`）。
2. `Toggle()` 查询逻辑 visible、真实 Setting HWND、`GetForegroundWindow()` 与 `Service::LastFocusWindow()`；隐藏时 Show；已显示但无前台/焦点时再次 Show；两者均属于 Setting 才 Hide（`Inkeys/Inkeys/UI/Setting/Setting.cpp:8872`）。这只决定设置显隐，不改变工具身份。
3. `Show()`/`Hide()` 在短 `settingStateMutex` 内设置 visible，随后把 ShowWindow/HideWindow 放入同一业务 FIFO，并 Request(Settings)（`Inkeys/Inkeys/UI/Setting/Setting.cpp:8848`、`:8860`）。逻辑 visible 与原生 HWND 显隐不是同一时刻。
4. FIFO 在独立 worker 执行 Window Service Show/Hide（`Inkeys/Inkeys/UI/Setting/Setting.cpp:165`、`:262`）。`Service::Show(Setting)` 先同步准备 owner 链层级，再投递 Setting 所属线程命令（`Inkeys/Inkeys/Window/Window.cpp:358`）。
5. Setting owner-thread 执行 ShowWindow(restore/show)，若有 Drawpad owner 则 SetWindowPos(HWND_TOP) 并核验在两块画布上方，随后 SetForegroundWindow/SetActiveWindow/SetFocus（`Inkeys/Inkeys/Window/Window.cpp:1668`、`:1680`、`:1690`）。Hide 仅 ShowWindow(SW_HIDE)（`:1733`）。未见恢复“打开设置前前台 HWND”的产品逻辑。
6. 隐藏让 SessionState 的 release 为 true（`Inkeys/Inkeys/UI/Setting/Setting.SessionState.h:114`），RenderSettingFrame drain 并最终 Idle（`Inkeys/Inkeys/UI/Setting/Setting.cpp:8712`、`:8719`）。drain 在共享 UI 渲染线程逆序释放 backend/纹理/session 私有设备资源，不是 Draw3 teardown（`:8657`）。

Setting 创建时被 Window Service 强制归一为 `WS_POPUP | WS_CLIPCHILDREN`、`WS_EX_APPWINDOW`、初始 owner=null（`Inkeys/Inkeys/Window/Window.cpp:1127`）。主 Drawpad 与 presentation-only 画布属于 overlay HWND 线程；Setting 属于另一线程（创建分组 `:971`；测试线程归属断言 `InkeysHeadlessTests/window_tests.cpp:578`）。

#### 3.2 owner 随工具变化，不随设置打开次数直接变化

- `ChangeStateModeToPen()` 先在 mode 锁内修改 stateMode，释放锁后 Sync（`Inkeys/IdtState.cpp:1248`）。橡皮/选择工具同样汇聚 Sync。
- `SyncDraw3State()` 根据非电脑操作模式发布带版本的 Setting owner desired，然后 Apply，再 PublishDraw3State，再 ReconcileDraw3Presentation（`Inkeys/IdtState.cpp:723`、`:740`、`:744`、`:745`）。因此隐藏的 Setting 也可能因工具切换提交 owner。
- `ApplySettingOwnerDesiredState()` 持 `settingOwnerApplyMutex`，丢弃已过期版本，调用 `SetSettingOwnedByDrawpad`；成功才记 applied（`Inkeys/IdtState.cpp:98`）。StateMonitoring 每次最多等 runtime revision 250ms，随后重试 owner（`:1758`、`:1761`）。**250ms 是返回失败后的重试节拍，不能超时打断一个未返回的 Window Service 调用。**
- `SetSettingOwnedByDrawpad()` 先给 Setting 线程提交命令；只改 Setting 的 `GWLP_HWNDPARENT`，选择/穿透态清 owner 并 NOTOPMOST；失败回滚原 owner（`Inkeys/Inkeys/Window/Window.cpp:431`、`:1794`）。此处不改 Drawpad owner/style/RTS。
- 绘制态 attach 后由原业务/协调线程 PrepareSettingZOrder，必要时同步要求 overlay root 刷新；root 上的 HWND_TOPMOST/NOTOPMOST 传播到 owned popup（`Inkeys/Inkeys/Window/Window.cpp:441`、`:477`、`:1870`）。Setting 自己只在继承的带内 HWND_TOP，不独立 TOPMOST。
- 基础链是 `Mag -> Freeze -> DrawpadPresentation -> Drawpad -> Bar/PPT`（`Inkeys/Inkeys/Window/Window.cpp:957`、`:1000`、`:1014`）；设置绘制态以 Drawpad 为 owner。不会把 Drawpad 变成 Setting 的子窗。

#### 3.3 Explorer / 文件选择 / 模态

- 注册的 Explorer 按钮直接绑定 `ExecuteBuiltInComponentAction(Explorer)`（`Inkeys/Inkeys/UI/Bar/Bar.Button.cpp:784`、`:795`），执行 `ShellExecuteW(nullptr, L"open", L"explorer.exe", ...)`（`Inkeys/Inkeys/Business/ComponentActions.cpp:53`）。
- 此链没有 `GetOpenFileName`、`IFileDialog::Show`、`EnableWindow(Drawpad, FALSE)`，也没有等待 Explorer 关闭、自动 selection 或自动恢复工具的代码。用户所称“点击选择关掉”必须与视频里的真实按钮行为对应，不能擅自解释成 Inkeys 内部文件选择框。
- Setting 的 Information/ConfirmRestart 才调用自定义 MessageBox，owner 是 Setting（`Inkeys/Inkeys/UI/Setting/Setting.cpp:215`、`:233`）；该模态实现只按 request.owner 禁用/恢复（`Inkeys/Inkeys/UI/MessageBox/MessageBox.Window.cpp:1725`、`:1753`）。普通打开/关闭设置和 Explorer 按钮不经过它。

### 4. Drawpad 样式、RTS 和捕获：哪些会变，哪些需要现场证据

#### 4.1 没有找到 Setting 对 RTS 准入的直接控制

- 主 Drawpad 创建时清 `WS_EX_LAYERED | WS_EX_TRANSPARENT`，presentation-only 窗固定 `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`（`Inkeys/IdtMain.cpp:3065`、`:3072`）。Draw3 presenter 后续按 DComp/ULW 改重定向/layered 位，但 Setting 显隐不走这条路径。
- `Service::SetClickThrough` 可以改任意角色 TRANSPARENT 位（`Inkeys/Inkeys/Window/Window.cpp:1780`），IdtState 中本次查到的 Drawpad 调用属于 Whiteboard Enter/Exit（`Inkeys/IdtState.cpp:1796`、`:1834`、`:1969`），不属于普通 Setting 或 Explorer 链。若现场涉及白板，需要单独采样 WhiteboardPhase；本次复现描述未包含白板。
- RTS 初始化绑定同一外部 Drawpad HWND，SetAllTabletsMode(TRUE) 同时接收鼠标/笔/触摸，并 put_Enabled(TRUE)（`Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp:2531`、`:2542`、`:2687`）；put_Enabled(FALSE) 位于 Shutdown（`:2717`）。Setting/Explorer 没有对此对象的调用。
- Host 输入门由 commandScenePending、Presentation target ready/UI ack 等控制（`Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp:550`），selection 来自 bridge 消费（`:1254`、`:1257`）。Setting visible/foreground/capture 没有进入这个 gate 条件。
- `WM_MOUSEACTIVATE` 在 Presentation 返回 MA_NOACTIVATE（`Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:1507`）；WM_ACTIVATE/SHOWWINDOW 只 RequestFullPresent（`:1526`），未在这里重新启用 RTS。
- 主画布 Win32 鼠标消息和实际画笔 contact 并非同一条证据链。Drawpad ForwardProductMessage 能继续、鼠标 cursor `accepted=1`，仍不能证明 RTS StylusDown 已到达或通过 contact admission。

#### 4.2 跨线程 owner 会共享输入队列

Microsoft 的窗口输入说明明确：跨线程 owner/owned（与 parent/child 一样）会自动附加输入队列。由此 focus/capture/activation 可能形成跨线程同步依赖，不能把两个独立 HWND 线程当作完全隔离；这也不能单独证明 RTS 必然停流。参见 [Microsoft: Sharing an input queue makes focus changes synchronous](https://devblogs.microsoft.com/oldnewthing/20130607-00/?p=4143)。

本项目只修改 Setting 的 owner，并在 Show 时激活 Setting。对“设置之后容易出现”应采样 attach/detach 与 foreground/focus 的真实顺序，不能仅看最终 owner 值或仅看设置窗口是否已隐藏。

#### 4.3 Capture 的具体候选和反证

- Setting WndProc 仅逻辑 visible 时向 ImGui Win32 backend 转发（`Inkeys/Inkeys/UI/Setting/Setting.cpp:610`）。
- backend mouse Down 查询 GetCapture，必要时 SetCapture(setting)；最后一个 Up 才 ReleaseCapture（`Inkeys/additional/imgui/imgui_impl_win32.cpp:698`、`:720`）。它的 WM_KILLFOCUS 仅 AddFocusEvent（`:776`）。
- ImGui Win32 Shutdown 清 backend data，没有显式 ReleaseCapture（`Inkeys/additional/imgui/imgui_impl_win32.cpp:226`）。Setting Hide/Service Hide 同样无应用侧显式释放 Setting capture。
- **待验证场景：** 若异步 Hide 在 Down/Up 之间把逻辑 visible 先置 false，后到的 Up 会跳过 backend，接着 session 被销毁。需要验证 USER32 的默认取消/隐藏路径是否已经释放 capture；仅凭“没有显式 ReleaseCapture”不能称为泄漏。
- **削弱候选的证据：** 当前自绘关闭按钮是普通 ImGui::Button（`Inkeys/Inkeys/UI/Setting/Setting.Widgets.cpp:156`），标准完整点击通常先由 backend 处理 Up，之后下一帧才触发 Hide（`Inkeys/Inkeys/UI/Setting/Setting.cpp:1433`）。此外，故障时持续收到同一真实 Drawpad HWND 的鼠标 Down/Up，会明显削弱“Setting capture 吞掉全部鼠标”的解释。
- 读 capture 必须用正确 HWND 线程的 `GetGUIThreadInfo(...).hwndCapture`。诊断 worker 上的 GetCapture=null 只能说明诊断线程没有捕获，不能证明 Setting/overlay 没有。参见 [Microsoft GetCapture](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getcapture)。

### 5. 旧死锁、当前等待路径与“仅 Draw3 线程卡住”

#### 5.1 2026-10-13 已处理的 ImGui 闭环

历史证据保存于 `.trellis/tasks/archive/2026-10/10-13-interaction-modes-startup/research/freeze-call-chain.md`：共享 UI 渲染线程持 settingImguiMutex -> Sync -> 等 Setting command -> Setting HWND WndProc 等 settingImguiMutex，整条共享 UI 渲染停止。

当前两处 Resume 都持 ImGui 锁并建立 SettingRenderScope（`Inkeys/Inkeys/UI/Setting/Setting.cpp:8681`、`:8766`）。Sync 入口 guard（`Inkeys/IdtState.cpp:725`）调用 `DeferDraw3SyncIfRendering`，marker 为 true 就排入已有业务 worker 并立即返回（`Inkeys/Inkeys/UI/Setting/Setting.cpp:8791`）。因此该直接闭环已被拆开。Draw3 线程并不持 settingImguiMutex，不能把这条已修链改名后直接套成画布专有死锁。

残余独立审计项：Settings Present(1,0) 仍在 ImGui 临界区内（`Inkeys/Inkeys/UI/Setting/Setting.cpp:8644`、`:8766`），而 WndProc 也取该锁（`:613`）。若 DXGI/USER32 实际存在反向等待，通常首先影响共享 UI render/Setting HWND；要证明现场发生，必须抓这两个线程栈，不能据 API 风险就认定已经死锁。

#### 5.2 2026-10-13 owner/Z 序处理

既有任务 `.trellis/tasks/archive/2026-10/10-13-ppt-exit-setting-zorder/design.md` 与当前源码一致：Setting owner-thread 若需要跨线程 root refresh，PrepareSettingZOrder 直接返回 false（`Inkeys/Inkeys/Window/Window.cpp:501`），wrapper 必要时回滚 owner，而不是在 Setting WndProc 内等 overlay command。根刷新从原业务线程在 Setting 命令完成后执行。

`InkeysHeadlessTests/window_tests.cpp:558` 覆盖此拒绝/回滚，`:564` 覆盖立即根刷新，`:566` 覆盖正确 owner 去重，`:586` 覆盖 owned reshow。测试使用 SettingOwnerProbeProc，未覆盖真实 ImGui capture、真实 RTS 或 Explorer 焦点变化。

#### 5.3 仍存在的无界同步等待

- 普通 Command 的 deadline 默认为空（`Inkeys/Inkeys/Window/Window.cpp:676`、`:692`）。Submit 在异线程排队后 WaitForCommandResult（`:1376`、`:1397`）；helper 没有 deadline 时直接 future.get（`Inkeys/Inkeys/Window/Window.CommandCompletion.h:17`）。
- 设置命令依赖 Setting 队列消息泵，overlay 根刷新依赖另一队列消息泵（`Inkeys/Inkeys/Window/Window.cpp:909`、`:919`）。执行途中卡在 USER32、Setting ImGui mutex 或第三方 hook，就不会走回“返回 false -> 250ms 重试”。
- owner apply mutex 在整个同步请求期间持有（`Inkeys/IdtState.cpp:100`），其他工具 Sync、StateMonitoring 的 owner reconcile 都可能排在同一个未完成请求之后。
- **可验证表现：** mode revision 已变，owner command 只有 enqueue/begin 没有 end，PublishDraw3State 没到达，bridge revision/selection 未更新；UI render 仍可能继续，但 Bar 是单一交互线程，同步 clickFunc 不返回时它不能继续接受后续实际工具动作（`Inkeys/Inkeys/UI/Bar/Bar.Initialization.cpp:217`、`Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp:4003`、`:6021`）。只剩 hover/动画不能称业务完全可操作。
- **确实可从 Draw3 绘制线程进入 Sync 的条件路径：** Host PumpDisplayScale 只在 displayScaleDirty 时运行（`Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp:1128`），若显示上下文需要发布会调用 penWidthContextChanged（`:1238`）。产品 callback 中 ApplyPenWidthContext 未改变时直接 SyncDraw3State（`Inkeys/IdtMain.cpp:3249`、`:3255`）；该线程 marker=false，仍可能同步等 owner。这个路径可以挡住 Draw3 帧，但需要现场显示上下文/调用栈证据，普通 Setting Show/Hide 不能推导它必然触发。
- **本视频约束：** 主会话报告故障时 Draw3 frame 及 Drawpad 鼠标日志持续。若确认是同一个 Host 和 HWND、同一时间段，就反证此时绘制线程永久停在上述 Sync/window future；仍须检查 RTS 独立 sync callback 是否断流。

#### 5.4 旧 Debug 产物的白屏观察与独立源码风险（下调）

**产物身份校正：** 主会话最初使用隔离副本 `Build/CanvasFreezeInvestigation/run/Inkeys.exe` 进行真实鼠标测试。其启动日志后来核实为 edition=`3.0.0-20260811a`，而当前 HEAD 源码 edition=`3.0.0-20261014a`。因此该测试是**旧 Debug 产物的运行观察，不是当前 HEAD 的复现**；不能直接把下述当前源码锁链套在旧二进制上。旧产物内部的具体锁实现没有在本子研究中用符号或反汇编核对。主会话正在完整构建 HEAD，本研究未提前记为构建或运行通过。

**旧副本实际观察与反证：** 主会话先完成绘制基线，随后 More -> Setting，观察到设置空白超过 15 秒、墨迹位于设置之上。该次 PID=39888、Setting HWND=0xb91216/TID=22420、overlay TID=26892、Draw3 frame TID=32880、RTS callback TID=15660，GetGUIThreadInfo capture=0。但后续 WM_CLOSE 能关闭该白屏设置，随后笔迹继续正常；继续执行 Explorer -> Select -> 关闭 Explorer -> Pen 也绘制成功，RTS Down/Up 均记录 published=1，Bar 业务操作仍可响应。此为主会话执行结果，研究子代理未启动/操控 GUI。

这些后续结果削弱了“这次白屏就是永久 DXGI/ImGui 锁闭环、共享渲染线程或 RTS 永久停住”的解释。白屏本身不等于画布卡死，也没有在旧副本重现原视频所述的持久失效；不能将它作为当前 HEAD 根因已确认的依据。

**独立源码候选，优先级下调：** 当前工作区中仍能静态看到一个不由 SyncDraw3State defer guard 覆盖的潜在锁顺序风险，保留供 HEAD 的实际故障证据验证：

1. RenderSettingFrame 在 `settingImguiMutex` 内 Resume（`Inkeys/Inkeys/UI/Setting/Setting.cpp:8766`）。
2. 首次 coroutine Resume 首先执行 `CreateDeviceD3D`（`Inkeys/Inkeys/UI/Setting/Setting.cpp:749`），调用 `IDXGIFactory::CreateSwapChain`（`Inkeys/Inkeys/UI/Setting/Setting.Base.cppm:75`）；此时尚未 `ImGui::CreateContext`（`Inkeys/Inkeys/UI/Setting/Setting.cpp:1117`）。swap-chain 宽高未显式设置，取窗口尺寸。
3. Setting 逻辑 visible 为 true 时，任何非 WM_NCHITTEST 消息都会进入 WndProc 的 `lock_guard(settingImguiMutex)`，**先取锁，后检查 ImGui context**（`Inkeys/Inkeys/UI/Setting/Setting.cpp:610`、`:613`、`:614`）。因此“首帧尚无 context”不能仅从源码排除锁竞争。
4. **若** CreateSwapChain 或后续 Present 同步 SendMessage 给这个 HWND，且该消息正在等同一锁，则可形成 `UI Render -> DXGI/SendMessage -> Setting WndProc -> ImGui mutex -> UI Render`。Microsoft [DXGI Best Practices / Multithreading and DXGI](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/dxgi-best-practices#multithreading-and-dxgi) 说明 DXGI 可同步发窗口消息；这支持风险可行性，**不证明当前 HEAD 或旧产物的本次白屏实际发生了该闭环**。
5. `DrainSettingSessionOnRenderThread` 也在同锁内执行 session cleanup（`Inkeys/Inkeys/UI/Setting/Setting.cpp:8681`），cleanup 的 swap chain Release（`Inkeys/Inkeys/UI/Setting/Setting.Base.cppm:97`）可列入独立依赖审计。SyncDraw3State 延后到 worker 不覆盖这些 DXGI 生命周期操作，但“未覆盖”本身不能替代故障证据。

**WCT 与无栈限制：** 研究子代理只读查看旧副本的 `Build/CanvasFreezeInvestigation/settings-wct.json`；Setting/overlay/Draw3/RTS 都只有单 ThreadObject、cycle=false，未提供用户态栈或锁所有者。WCT 无 cycle 加上 WM_CLOSE、Bar 操作及后续绘制成功，削弱永久共享线程死锁；WCT 本身也不足以排除采样未命中或其不覆盖的用户态等待。当前没有证据把该旧产物白屏归因于上述当前源码锁链。

**重新提升此候选所需证据：** 先核实实际 EXE 的 edition、完整路径与构建身份，再在当前 HEAD 的真实失败时刻取得共享 UI render 栈和 Setting HWND 栈，或加入可关的 CreateDeviceD3D/ImGui init/Present begin-end 与 WndProc 等锁阶段 trace。必须看到具体 DXGI-USER32 等待与锁所有者对应，不能只看到白屏、无新日志或“源码有锁”就诊断死锁；若同一时刻 Bar 真实业务与绘制继续，应降低该解释。

**仅在证据确认后考虑的最小修复方向：** 收窄 ImGui 互斥区，避免可能同步等 HWND 的 DXGI 操作持有 HWND 线程也要取得的锁，保持共享渲染线程及 HWND capture/IME 所有权。仅换 recursive_mutex、检查 context 或覆盖 CreateSwapChain 不能证明 Present/drain 安全。本轮未实施修复，且不以旧产物白屏为理由修改当前 HEAD。

### 6. 建议采集的数据与判定条件

首先核实受测 EXE 的 edition、路径与构建身份，避免再次把旧产物行为映射到 HEAD。随后增加可关的低频状态边/计数采样，避免每 packet 同步日志改变时序；本研究未实现诊断代码。

| 采样位置 | 必需数据 | 区分目的 |
| --- | --- | --- |
| Setting Show/Hide 请求、worker 执行前后 | 单调时间、TID、FIFO serial、逻辑 visible、HWND IsWindowVisible、IsWindowEnabled、owner、style/exstyle、结果 | 区分逻辑已隐藏但真实 HWND 仍在、业务队列堵住、owner 错误 |
| Setting/overlay/foreground GUI 线程 | GetGUIThreadInfo 的 hwndActive/hwndFocus/hwndCapture/flags；GetForegroundWindow、WindowFromPoint、HWND 对应 TID/PID | 识别焦点/捕获/原生 move/menu loop 残留，不能在任意 worker 只 GetCapture |
| owner apply 和 Window command | Sync serial、mode revision、desired/applied、锁前/锁后、enqueue/execute-begin/execute-end、command type/role | 定位未返回的同步边，而不是靠 250ms 重试假设排除等待 |
| Bridge 发布与 Host 消费 | stateMode、完整 bridge revision/tool/selection/workspace、appliedBridgeRevision、selectionMode、输出 target/ready revision | 判断 UI 与 bridge/Host 是否真的不一致；工具数值单项不够 |
| RTS callback 入口与 decoder | Enabled/Disabled/Down/Packets/Up/Error/SystemGesture 的累计计数、最近时间、tcid/stylusId、decoder generation/结果 | WndProc 活着却 RTS 没 Down，与 callback 有 Down 但解码/准入失败分开 |
| contact producer/consumer | Down published/rejected、Move/terminal、active/free slots、AdmissionBlocked 和原因、消费者 frame serial | 区分 RTS 断流、生产拒绝、record 耗尽或消费者未用输入 |
| 单次失败快照 | Drawpad/Presentation/Setting HWND 身份及 enabled/visible/owner/style、RTS get_Enabled/get_HWND 的 HRESULT和值（在适当控制线程、非 callback 热路径执行） | 排除旧 HWND、disabled/lost binding；避免新增 COM 查询反向阻塞 |

建议复现对照：同构建先只画画；再仅 Setting 打开/普通关闭；再 Setting 打开/隐藏后切 Selection/Pen；再仅 Explorer；最后组合原步骤。每次失败先保存证据，再比较“只把前台移到桌面/任务栏，随后回到 Drawpad”是否恢复；不要在采样前自动重启 RTS 或自动改窗口样式。

判定顺序：

1. Window 与 Draw3 heartbeat 都前进、鼠标 Down/Up 确认送到 Drawpad，但 RTS callback 完全没有新 Down -> 优先查 RTS/系统路由与焦点，现有 ImGui 整体死锁不成立。
2. RTS 有 Down，producer 不发布/不接受 -> 查解码、gate 和容量；设置只可能是触发时序，不是已证明根因。
3. producer 已发布，但消费者帧不推进 -> 抓 Draw3 栈，检查 owner future/display callback 或其他等待。
4. frame 推进且 desired/applied selection 不一致 -> 才转查 Sync 发布前等待；需要真实 selection 字段，不使用 tool=0 替代。
5. hwndCapture=隐藏 Setting 或实际 hit window 是 Setting/其他遮挡窗口 -> 验证其生命周期清理；必须同一失败时刻重现，不能因 shutdown 未显式 ReleaseCapture 就直接归因。

### 7. 调查后的修复方案边界（有条件建议，未实施）

- 若确认 HWND/focus/capture 残留：在既有 Setting owner-thread 隐藏事务中收束其自身 capture/输入状态，并保持 UI 渲染/Win32 backend 线程边界；不要跨线程强制 ReleaseCapture 或逐帧抢 foreground。
- 若确认 owner 提交阻塞且挡住绘制：把绘制属性发布与可重试的设置 owner 收敛解耦，或采用已有生命周期 mailbox 安全发布；不能仅把无限等待换超时并留下迟到事务覆盖新模式，也不能禁用 owner 需求来掩盖 Z 序。
- 若确认 RTS 在无焦点 overlay 路由下停流：针对同一 HWND 的 RTS 生命周期设计可观察、owner 串行的恢复/重新准入；先验证原因和不丢活动 contact 的边界，不能直接增加定时无条件重启。
- 若旧版本缺少当前 guard：把 10-13 修复与同场景实际验证对应，不能把当前源码通过的假队列测试当作用户旧二进制已经修复的证据。

### 8. External references / versions

- 本地 Dear ImGui 是 **1.92.7**（`Inkeys/additional/imgui/imgui.h:32`）；capture/shutdown 结论来自仓库内 backend，不能套其他版本的实现。
- [Microsoft GetCapture](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getcapture)：返回当前线程 capture；跨线程检查使用 GetGUIThreadInfo。
- [Microsoft input queue/focus discussion](https://devblogs.microsoft.com/oldnewthing/20130607-00/?p=4143)：owner/owned 关系自动附加输入队列及同步 focus 风险。
- [Microsoft IRealTimeStylus::put_HWND](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-irealtimestylus-put_hwnd)：指定采集 ink 的目标窗口。
- [Microsoft RealTimeStylus Reference](https://learn.microsoft.com/en-us/windows/desktop/tablet/realtimestylus-reference)：RTS sync plugin 在该 RTS 实例的独立执行线程运行，与异步 UI plugin 区分。
- [Microsoft SetAllTabletsMode](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-irealtimestylus-setalltabletsmode)：TRUE 包含鼠标输入，已 enabled 时不能靠再次 SetAllTabletsMode 改模式。
- **仅历史问题线索、非 API 保证/未确认修复：** [2024 Microsoft Q&A 原始报告](https://learn.microsoft.com/en-us/answers/questions/1641576/c-realtimestylus-is-dead-and-doesnt-handle-any-tou) 描述 TOPMOST/NOACTIVATE overlay 在焦点交接后 RTS touch 停流、任务栏激活后恢复。报告偏 Win7/touch 且声称 mouse 不触发，不足以直接解释本次视频。

### 9. Related specs / existing research

- `.trellis/workflow.md`：研究持久化、当前仅调查的任务边界。
- `.trellis/spec/native-desktop/index.md`、`rendering-and-ui.md:1520`、`:1708`、`:1928`：设置渲染/FIFO、Window Service owner、近期 ImGui defer 合同。
- `.trellis/spec/native-desktop/draw3-integration.md:95`、`input-and-ink.md`：独立 Draw3 设备/线程、唯一 RTS、输入与呈现不能混为一谈。
- `.trellis/spec/native-desktop/errors-logging-and-resources.md:61`：可 join 线程与无限等待的退出合同。
- `.trellis/tasks/archive/2026-10/10-13-interaction-modes-startup/research/freeze-call-chain.md`、`validation-replay.md`：已修 ImGui 同步等待及无窗口回放的验证边界。
- `.trellis/tasks/archive/2026-10/10-13-ppt-exit-setting-zorder/design.md`、`research/implementation-verification.md`：已修的 Setting owner/Z 序、任务记录的用户验收；本研究未重复 GUI。

## Caveats / Not Found

- 没有启动程序、操控桌面、运行 GUI/窗口测试或修改代码；没有执行任何 git 操作。仅本研究文件属于写入范围。
- 视频细节来自主会话转述，未在本子研究中自行提帧；视频名不能证明受测二进制的版本/构建时间。需要对照是否已经包含 10-13 的修复。
- 没有找到普通 Explorer 按钮的内嵌文件选择模态链、Setting Show/Hide 直接禁用 Drawpad/RTS 的链，或本视频对应的已确认锁闭环。
- 旧 Debug 副本 edition=`3.0.0-20260811a` 的设置白屏可以关闭且后续绘制成功；它与当前 HEAD 源码 edition=`3.0.0-20261014a` 不匹配。旧副本已有 RTS Down/Up published=1 与 capture=0 的观察，不能再表述成该次测试没有 RTS/capture 证据。当前 HEAD 尚缺实际失败时刻的用户态栈、完整 bridge selection、GUI 线程状态及 RTS/contact 链，暂不能宣布“根因已定位”。
- 文中 capture、input queue、display callback 路径是可验证候选；不建议未经现场证据直接修改 owner 拓扑、强制 foreground、变更输入 API 或加入自动 RTS 重启。
