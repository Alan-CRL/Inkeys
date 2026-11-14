# Research: 白板"移动(Selection)"模式下 Draw3 的输入路由与平移接入点

- **Query**: 梳理白板 Selection(移动)模式下鼠标/触摸/笔如何进入 Draw3，平移手势应挂在何处；Pan 接入需要改动/作废的现有机制；模式切换时的停止钩子；光标/触觉副作用与无窗口测试接入方式。
- **Scope**: internal（只读，仅 `Inkeys/Inkeys/**`、`InkeysHeadlessTests/**`、`.trellis/spec/**`）
- **Date**: 2026-11-06
- 行号均基于工作树 `feature/whiteboard3`；`Draw3.DrawingController.cpp` 简称 **Ctl**，`Draw3.WindowControl.cpp` 简称 **Win**，`Draw3.RealtimeStylus.cpp` 简称 **RTS**，`Draw3.CanvasNavigation.cpp/.cppm` 简称 **Nav**。

---

## 0. 一屏结论

1. **产品里只有两条"接触生产者"**：RTS（主路径，鼠标/触摸/笔全部走它）和 Win32 鼠标兜底（仅 RTS 初始化失败时启用）。`WM_POINTER*`、`WM_MOUSE*`（RTS 模式下）、`WM_TOUCH`（Draw3 根本没有）都**不**产生 contact，只维护光标/触觉/屏障。
2. **白板 + Selection 时 Draw3 画布 HWND 仍是主画布且输出仍走 PrimaryDrawpad**（`Bridge::SelectionUsesAuxiliaryOutput` 对白板为 false），所以 RTS 的触摸/鼠标/笔 Down 确实到达绘制线程；它们在 `processCommand` 里被 `DiscardSelectionDown` 一刀切丢弃（Ctl:13479），丢弃后物理 route 进 Quarantined，后续 Move 不可读（ContactInput.cpp:870-894）。Win32 兜底路径则在 `MouseInputState::Down/Move` 里就按 `selectionMode` 隔离（MouseInput.h:38-42、64-69），根本不发布 contact。
3. **现有平移内核对设备不敏感**：质心/速度/惯性代码只看 `gestureContacts` + `touchGesture.Disposition(key)==Pan`（Ctl:13620-13717），key = `tabletContextId<<32|contactId`（Ctl:2557-2562）。需要"改"的是入口（谁能成为 Pan、何时成为 Pan）与一批只服务"两指 180ms / 先画后取消"模型的状态（见 §2）。
4. **模式切换没有任何"退出 Move → 停止平移"的钩子**：`SetSelectionMode` 只翻原子位+刷新光标（Win:672-680）；Ctl 里 `interruptNavigationForPenOrMouse` 只有 3 个调用点（Ctl:12140 Pen/Mouse Down、13543 笔 mailbox 落笔、16122 画布命令）；工作区/加载/最小化靠 admission 封口 → `sealPresentationContacts`（Ctl:13364-13419）一并清零。
5. **Selection 下副作用现状**：不画任何应用光标（Ctl:13978-13989）；系统光标鼠标显示箭头、触摸/笔仍隐藏（Win:1412-1425、PenCursor.cpp:459-485）；连续触觉整体禁止（HapticRequest.h:44-48）；高精度 1ms 计时器被释放（Ctl:16534、TimerPeriod.h:30-43），Move 模式下逐帧平移将在**无** `timeBeginPeriod(1)` 的前提下做帧间隔等待。
6. **无窗口测试**：`InkeysHeadlessTests.vcxproj` **没有**编译 `Draw3.CanvasNavigation.cpp/.cppm`（Inkeys.vcxproj:1047-1050 才有）；现存平移单测在不属于 `InkeysRepo.sln` 的旧沙盒 `inkStrokeModelerTestTests/canvas_navigation_tests.cpp`（模块名 `draw3.canvas_navigation`）。接入方式见 §4.3。

---

## 1. 各设备进入 ContactInputCoordinator / 控制器的精确路径

### 1.1 拓扑

```
[RTS 主路径]  Win32 HWND(Drawpad) ──put_HWND──> RealTimeStylus ──StylusDown/Packets/StylusUp──> StylusSyncPlugin
                                                   (SetAllTabletsMode(TRUE)，MultiTouchEnabled(TRUE))
              ──> ContactInputCoordinator::PublishDown / PublishMove(覆盖最新) / PublishUp / PublishCancelled
              ──> 绘制线程 DrainIngressBatch → processCommand → [DiscardSelectionDown | initializeStroke]

[Win32 兜底]  仅 stylus.Initialize 失败：WindowController::SetMouseCompatibilityEnabled(true)
              WM_LBUTTON*/WM_RBUTTON*/WM_MOUSEMOVE ──> HandleCompatibilityMouse ──> MouseInputState ──> 同一个 Coordinator
```

- 选路：`Draw3.Host.cpp:2444-2470`。`stylus.Initialize(hwnd,input,&window,...)` 成功 → `HostInputMode::RtsExtended/RtsBasicXY`；失败 → `window.SetMouseCompatibilityEnabled(true)` → `HostInputMode::Win32Mouse`（日志"RTS unavailable; Win32 mouse compatibility input enabled"，Host.cpp:2466）。
- RTS 配置：`put_HWND`（RTS:2702）、`SetAllTabletsMode(TRUE)` "同时接收鼠标、笔和触摸"（RTS:2714）、`put_MultiTouchEnabled(TRUE)`（RTS:2777）、关闭 Flicks（RTS:2760）。HWND 属性 `kTabletInputFlags = TABLET_ENABLE_MULTITOUCHDATA | DISABLE_PRESSANDHOLD | DISABLE_PENTAPFEEDBACK | DISABLE_PENBARRELFEEDBACK | DISABLE_TOUCHUIFORCEOFF | DISABLE_FLICKS`（Win:43-50），在 `AttachExternal` 首触前写入（Win:366-376）并在 `WM_TABLET_QUERYSYSTEMGESTURESTATUS` 返回同值（Win:1645-1647）。**RTS 级多点恒开**，与用户设置的"多指书写"无关。
- 数据兴趣：`kProductionRtsDataInterest` 含 InRange/OutOfRange/InAirPackets/StylusDown/Packets/StylusUp（RTS:70-75），**不含**按钮事件。

### 1.2 RTS 各设备

| 设备 | 判定 | 证据 |
|---|---|---|
| Touch | `IInkTablet2::get_DeviceKind == TDK_Touch` → `InputDeviceType::Touch` | RTS:944-958 |
| Mouse | `TDK_Mouse` → 解码器先给 `MouseLeft`；**Down 时**再按 `GetAsyncKeyState`：`右键按下且左键未按` → `MouseRight`，否则 `MouseLeft` | RTS:955、1261-1268 |
| Pen | 默认 | RTS:944 |
| 触控板板面 | `SourceKind::TouchPad` 直接返回，不进接触链 | RTS:1208-1213 |

- 其余路径：Down→`PublishDown`（RTS:1269-1277）；Packets→`PublishMove`（RTS:1447-1452，**覆盖式**，写门闩争用允许丢，见 ContactInput.cpp:775-809）；Up→`PublishUp`（RTS:1345-1350）；无包/坏包→`CloseProducerContact`→`PublishCancelled`（RTS:1310-1319、1683-1694）。
- Touch 额外：`NotifyRtsStylusDownCursor` 在 Down 回调里立即调 `NotifyTouchContactBegin()`（RTS:1034-1040、1217）；`NotifyRtsTouchContactEnd` 在 Up/失败时（RTS:1042-1047）。仅 Touch 解码接触面积（RTS:1018-1027）。
- 笔 hover/落笔光标样本**与选择模式无关**，`PublishPenCursor`（RTS:1659-1676）在 InAirPackets/Packets 里发布，`StylusOutOfRange`/Up 清除（RTS:1168-1178、1344）。这是现有"笔靠近刹车"的数据源（见 §3）。

### 1.3 Win32 兜底（仅 Win32Mouse 模式）

- 开关：`WindowController::SetMouseCompatibilityEnabled`（Win:445-474），在 HWND owner 线程收 `kMouseInputOwnerMessage` 后 `mouseCompatibilitySelected_=true`（Win:1554-1590）。
- 兼容模式下 `WM_NCPOINTERUPDATE..WM_POINTERROUTEDRELEASED` **整段交 `DefWindowProc`**，"系统提供的单点鼠标提升只走下面一条 producer"（Win:1594-1596）；只处理 `WM_LBUTTONDOWN/UP`、`WM_RBUTTONDOWN/UP`、`WM_MOUSEMOVE`（Win:1597-1608）。**结论：该模式下触摸/笔都被系统提升成单点鼠标，只会出现一个 `MouseLeft/MouseRight` contact，无多指**。
- `HandleCompatibilityMouse`（Win:494-541）：丢弃旧代次消息（`GetMessageTime()` 对比 `mouseStartTick_`，Win:500）；坐标 `GET_X_LPARAM/GET_Y_LPARAM(lParam)` 的客户区像素（Win:503-504）；QPC 取处理时刻 `QueryPerformanceCounter`（Win:501-502）；Down 先 `NotifyCanvasContactDown()`（Win:512），`mouseInput_.Down(...,SelectionMode())` 成功才 `SetCapture(window)`（Win:513-517）；Move 用 `MK_LBUTTON/MK_RBUTTON` 判断仍按住（Win:520-524）；末尾发布鼠标光标样本并把 owner 设为 Mouse（Win:526-535）。
- `MouseInputState`（MouseInput.h）：tablet context 固定 `0xFFFFFFFE`（:94），`contactId_` 递增跳 0（:44），`source.kind=Mouse`、压力/面积保持未知（:101-103）；左右键**只保留一条流**，新键 Down 先对旧键 `End(Up)`（:36-37）；`CaptureLost/Stop/Cancel` 发布 `Cancelled`（:76-91）；**`selectionMode || AdmissionBlocked` 时 Down 把按键放入 `quarantinedButtons_` 并返回 false，不发布任何 contact**（:38-42）；活动流遇到 `selectionMode` 则取消并隔离（:64-69）。该行为被 `InkeysHeadlessTests/draw3_mouse_input_tests.cpp:105`（"selection refuses Down"）和 :154-157（"selection key quarantine"）固化。
- `WindowController` 没有 workspace 字段（`activeWorkspace` 是 Ctl:10584 的控制器局部变量，在 `SetWorkspace` 命令里改，Ctl:16024-16080）。因此 Win32 兜底路径目前**无法**区分"白板 Selection"与"桌面/PPT Selection"（`SelectionMode()` 是唯一输入）。

### 1.4 不产生 contact、但容易误认的消息

- **RTS 模式下的 `WM_POINTER*`**（Win:1727-1918）：只做 `QueryPointerDetails`→更新 cursor owner、触摸屏障 `NotifyTouchContactBegin(false,tick)`（Win:1765-1770）、笔 hover 样本（Win:1798-1822）、触觉 Observe（Win:1833-1844）；最后 `break` 落到 `DefWindowProcW`（Win:2304）。无 `WM_TOUCH`/`RegisterTouchWindow`（`Draw3/**` 内 grep 无结果）。
- **RTS 模式下的 `WM_MOUSE*`**（Win:2035-2223）：经 `FilterMouseCursorMessage`（PenCursor.cpp:556-589）过滤 promoted/touch/pen 兼容消息后只 `PublishMouseCursorSample`（Win:2205-2215），`inContact = buttonDown`，`buttonDown` 含 `MK_MBUTTON`（Win:2045-2047）。它既不创建 contact，也**不是导航真值**（spec runtime-and-rendering.md:881）。
- `WM_MOUSEWHEEL` 只更新 cursor owner（Win:2252-2301）。

### 1.5 Coordinator → 控制器

- `PublishDown`（ContactInput.cpp:741-773）：锁存 `admissionRevision`（:760），`HasFinitePosition` 失败直接拒绝（:744）。`PublishMove` 只覆盖 `Producing` route 的最新快照（:775-809）。`TryReadSnapshot` 对 Up/Cancelled 只在 `ConsumerOwned` 状态可读（:843-844）。
- 绘制线程消费：`DrainIngressBatch`（Ctl:168-183）由 `drainIngressBatch`/`tryConsumeOneIngress`（Ctl:13505-13521）调用；`processCommand`（Ctl:13458-13495）顺序：

```
13473  if (!input_.ContactAdmitted(handle)) { DiscardUntilTerminal; return; }
13479  if (DiscardSelectionDown(input_, handle, window_.SelectionMode())) return;      // <-- 白板 Selection 的丢弃点
13480  if (whiteboard && whiteboardLoading || PresentationInputSuppressed(...)) { DiscardUntilTerminal; return; }
13488  if (canvasDebugOptions_ == 3u) { registerCanvasDebugProbe(handle); return; }
13494  initializeStroke(handle);
```

- `DiscardSelectionDown`（Ctl:3777-3784）：`if (!selectionMode) return false; input.DiscardUntilTerminal(handle); return true;`，**不看设备类型、不看工作区**（注释"三工作区的选择都是电脑操作；RTS 鼠标也不得绕过穿透而初始化笔迹"）。生产探针 `RunDraw3ControlFenceProductionProbe`（Ctl:5910、6021-6093；CLI `--draw3-control-fence-test`，IdtMain.cpp:2021-2022）对 3 工作区 × {MouseLeft, MouseRight, Touch, Pen, 倒转 Pen} 固化了这一行为（含"切到 Pen 后被拒 contact 的 Move 不复活、新一轮 Down 正常通过"）。
- `DiscardUntilTerminal`（ContactInput.cpp:870-894）：Producing→Quarantined（`quarantinedContacts` 计数），此后 `FindProducing` 找不到该 route，Move 发布失败；Up/Cancelled 到来由 producer 自回收。`HasQuarantinedContacts()` 为真期间 UI 点击触觉被挡（Ctl:17030），连续触觉也被 `ContinuousAllowed` 的 `quarantined` 参数关闭（Ctl:16941-16944）。

### 1.6 白板窗口层是否把输入送到 Draw3

- 白板 + Selection 时 **Drawpad 保持 Primary 可见**：`desired = ... : (!policy.whiteboard && policy.selectionMode ? Presentation : Primary)`（Window.cpp:3261-3262）；Drawpad 的 click-through 为 `hideDrawpad || minimized || (!whiteboard && selectionMode)`，**白板 Selection 不是 click-through**（Window.cpp:3158-3159）。
- 输出目标：`SelectionUsesAuxiliaryOutput(selection, workspace) = selection && workspace != Whiteboard`（Draw3.Bridge.h:187-191）；Ctl:16519-16532 / Host.cpp:2294 / PresentationState.h:20-22 据此在白板 Selection 保持 `PrimaryDrawpad`，桌面/PPT Selection 才切 `SelectionUlw`（穿透的 ULW 窗口）。
- 白板上叠加的 UI 窗（PageControl 等）`WM_NCHITTEST` 命中控件/弹层才 `HTCLIENT`，其余 `HTTRANSPARENT`（PageControl.cpp:3651-3658），即画布区域的指针落到 Drawpad；`UI/Freeze`、`UI/Whiteboard` 内无命中测试代码（grep 无结果），Freeze 窗在 Drawpad 之下。
- 未找到：生产代码里谁在白板 Selection 时设置/清除 Drawpad 的 `WS_EX_TRANSPARENT`（`Service::SetClickThrough` 在 `Window.cpp` 之外没有调用点；`RequestPrepare/CompleteWorkspaceWindowPolicy` 仅出现在 `PageControl.cpp:5774-5832` 的测试里）。上面 Window.cpp:3158-3159 是该策略函数的定义行为，运行时是否经此路径应在联调时实测确认。

### 1.7 位置/时间/单位

- **位置单位**：RTS `snapshot.position = packet[x]*positionScaleX`，scale 来自 `GetPacketDescriptionData` 的 ink→device 比例（RTS:983-984、916-933、1744-1760；日志含义写作 `canvasPx=packetXY*sharedPositionScale`，RTS:2972）；Win32 兜底是客户区坐标。进程是 Per-Monitor DPI aware（IdtMain.cpp:142-161），因此两路都是**窗口客户区物理像素**，**不是 DIP**。
- **平移代码从不乘 DPI**：Nav 里常量命名为 `...DipPerSecond`（Nav.cppm:14-24），但 `updateCanvasNavigation` 直接把质心差（物理像素）喂给 `UpdateCanvasPan`/`ApplyCanvasContentTranslationChecked`（Ctl:13649-13664、13814-13816），整个平移区域没有 `dpiScale` 引用（`dpiScale` 只用于笔宽/光标/激光等，Ctl:415-454、4788 等）。若需要 DIP 语义，现成的像素→DIP 比例在 `SpeedEraser::DisplayScale.dipPerPixelX/Y`（Host.cpp:1635-1636，由 `PumpDisplayScale` 发布到 `window.SetSpeedEraserDisplayScale`，Host.cpp:1685-1691）。
- **QPC**：RTS 用**回调到达时刻**的 `QueryPerformanceCounter`（RTS:207-212；Down 在 RTS:1244），不是硬件时间戳；Win32 兜底用 `WM_*` 处理时刻（Win:501-502）。`ContactSnapshot.sequence` 是 seqlock 序号，Down 为 2、每次发布 +2（ContactInput.cpp:219、268-284）。Pan 采样的 `snapshot.qpc` 就是这个到达时刻。
- **Mouse 快照形态**：`pressure/tilt/orientation=-1`、`contactSize` 未知（RTS:985-1013 只对 Pen 解码压力倾斜、只对 Touch 解码面积；MouseInput.h:103）；`isInvertedCursor` 只对 Pen 为真（RTS:1258）；`source.kind` 对 Mouse 由 `ClassifySource` 给 Mouse，兼容路径固定 Mouse（MouseInput.h:101）。
- **捕获**：RTS 路径不 `SetCapture`；仅 Win32 兜底 Down 成功后 `SetCapture`（Win:515）并在 `WM_CAPTURECHANGED(非本窗)/WM_CANCELMODE/WM_KILLFOCUS/隐藏/失活` 取消（Win:1609-1616）。白板 Drawpad 覆盖主屏全屏，拖出窗口的场景很少，但 RTS 模式对拖出窗口的行为代码里没有处理（未验证）。
- **右键/中键**：RTS 仅用 `GetAsyncKeyState(VK_LBUTTON/VK_RBUTTON)` 区分 L/R，没有中键判别，也没有交换按键（SwapMouseButtons）补偿（RTS:1264-1267；`GetAsyncKeyState` 反映物理键）。右键 contact 在 Selection 下同样被 `DiscardSelectionDown` 丢弃。

---

## 2. 白板 + Selection：Mouse-left 与全部 Touch 立即成为 Pan —— 受影响的代码点

> 以下只列"现状如何编码了旧模型"和"哪些点会因新决策失去意义/需要不同输入"，不含实现建议。

### 2.1 入口：两个拦截点之间要有 Pan 分流

- 现在所有 Selection Down 在 Ctl:13479 被丢弃，永远到不了 `initializeStroke`，而 Touch→FSM 的分流（Ctl:12142-12215）只存在于 `initializeStroke`。
- `initializeStroke` 里 Touch 分支之前已有三段会影响 Move 模式 Touch 的前置逻辑：
  - 12077-12093：Laser 多指拒收（`IgnoreAdditionalLaserTouch`，Ctl:3813-3822）；依据 `window_.ActiveTool()`（Selection 不改 `activeTool_`，所以 Move 模式下 ActiveTool 仍可能是 Laser）和**仍活动的** Laser touch runtime。
  - 12120-12135：Pen 在 Touch Pan 期间/有存活被抑制 Pen 时 `suppressPenUntilRelease`。
  - 12136-12141：**Pen/Mouse Down 一律 `interruptNavigationForPenOrMouse("drawing-contact")`** —— 当前语义里 Mouse Down 是"刹停者"而不是"抓取者"。
- Touch 分支之后的 `RejectIndependentTouch(..., generalOptions.multiTouchEnabled)`（Ctl:12602，`multiTouchEnabled` 默认 true，Ctl:79；来源 Host.cpp:1761-1767 的 `general & 1u`；实现 Ctl:3786-3811）只在走到绘制路径时才生效；**走 Pan 分支提前 `return true`（Ctl:12213）就不会碰到它**，即"用户关闭多指书写"不会限制 Move 模式的多指 Pan，除非 Pan 分流被放到它之后。
- 白板加载/演示输入抑制（Ctl:13480-13487）和调试探测（Ctl:13488-13493）在 `DiscardSelectionDown` 之后；Pan 分流若放在 13479 与 13480 之间会绕过这两道门，放在 13494 的 `initializeStroke` 内则与它们同序。`ContactAdmitted`（13473）无论如何先行。
- 设备过滤点：`handle.record->DeviceType()` 在出队处即可读（Touch / MouseLeft / MouseRight / Pen）。

### 2.2 `CanvasTouchGestureState`（Nav.cppm:71-106、Nav.cpp:179-333）里"两指 180ms"专用的部分

| 成员/逻辑 | 位置 | 对应旧模型 |
|---|---|---|
| `kCanvasPanGestureWindowSeconds=0.180` | Nav.cppm:14 | 第二指时间窗 |
| `OnTouchDown` 首触点分支：`Draw` 或 `PanCandidate`（惯性中） | Nav.cpp:192-203 | 先画/先候选 |
| `OnTouchDown` 第二触点：`<=180ms` 且 `batchStartedEligible_` → `beginPan`+`cancelExistingTouchDrawing`，否则 `batchAllowsPan_=false` 并 Draw | Nav.cpp:213-238 | 两指窗口、晚到只能画 |
| `Update()`（超时后 `batchAllowsPan_=false`，候选触点转 `Suppressed`） | Nav.cpp:251-263 | 窗口过期 |
| `InertiaCandidateActive()` / `InertiaBrakeRequested()` | Nav.cpp:288-297 | 候选期/候选超时制动 |
| `batchStartedEligible_`、`batchAllowsPan_`、`firstDownQpc_`、`inertiaCandidate_` | Nav.cppm:99-105 | 批次资格 |
| `CanvasTouchDecision::cancelExistingTouchDrawing`、`CanvasTouchDisposition::PanCandidate` | Nav.cppm:54-68 | 先画后取消、候选 |
| `OnTouchDown(..., inertiaActive, blockingContactActive)` 形参 | Nav.cppm:74-75 | 惯性候选/其它设备阻塞 |
| `InterruptForPenOrMouse()`：把 Pan/Candidate 触点转 `Suppressed` 并置 `batchAllowsPan_=false` 直到批次清空 | Nav.cpp:265-275 | 打断后本批不可再 Pan |

仍然通用的部分：`OnTouchUp/Reset/PanActive/ContactCount/GestureContactCount/PanContactCount/HasContact/Disposition`；`CanvasPanMotionState` 全套（`BeginCanvasPan/UpdateCanvasPan/ResetCanvasPanVelocitySamples/EndCanvasPan/StepCanvasPanInertia/StopCanvasPan`，Nav.cpp:335-503）；`ResolveCanvasPanContactAnchor`、`ShouldConsumeCanvasPanContactSnapshot`、`IsCanvasPanLifecycleOwnershipConsistent`（Nav.cpp:153-177）。

### 2.3 控制器里与旧模型绑定的点

| 点 | 位置 | 说明 |
|---|---|---|
| 门禁常量 | Win.cppm:30；Ctl:12152-12153 | `canvasNavigationBlocked = blockingContactActive || !kCanvasNavigationProductIntegrationEnabled`；为 true 时 `OnTouchDown` 首点就 `batchStartedEligible_=false` → 永远 Draw（Nav.cpp:196-197、219） |
| `blockingContactActive` | Ctl:12146-12151 | 存在非 Touch 的活动 RuntimeStroke（Pen/Mouse 正在画）则本批不可 Pan |
| `cancelTouchDrawingForPan` | Ctl:11810-11863；调用 Ctl:12202-12203 | 第二指到达时把已开始绘制的 Touch RuntimeStroke 取消并按"当前快照"转成 Pan contact（`addGestureContact(..., Current)`），同时清 L0/L1/激光/粒子；只在 `cancelExistingTouchDrawing` 为真时执行 |
| `retireEndedTouchBeforeDown` | Ctl:11925-11956；调用 Ctl:12145 | 新 Down 出队时先退休已物理结束但未被导航消费的旧单指批次（`PanActive()==false && ContactCount()==1`） |
| `touchGesture.Update(...)` + 到期日志 | Ctl:13545-13559 | 每帧驱动 180ms 窗口与候选超时 |
| `candidateBrake = InertiaBrakeRequested()` | Ctl:13777-13788 | 候选超时 → 与笔 hover 同样的 12000 加速制动 |
| `inheritedInertia = panMotion.inertiaActive` 传入 `BeginCanvasPan(motion, inherit, qpc)` | Ctl:12155、12186 | 旧速度仅作一次性 residual 候选 |
| `navigationInProgress` / `navigationActive` 含 `InertiaCandidateActive()` | Ctl:16788-16790、17157-17160 | 主循环保持活动帧与 idle 判定 |
| Touch 登记到 FSM 的簿记（RuntimeStroke.touchGestureKey 与 `OnTouchUp` 配对） | Ctl:2557-2571、12608-12623、13094、13140、17339、17362 | 绘制模式的 Touch 也进 FSM；`RejectIndependentTouch` 用它退休被拒触点（Ctl:3801-3811） |
| 诊断串 `within-180`、`touch-window-expired`、`inertia-brake active=` | Ctl:12165、13552、13781 | 仅旧沙盒 `inkStrokeModelerTestTests/laser_incremental_coverage_tests.cpp:545-554` 做源码文本断言，且读的是沙盒副本 `inkStrokeModelerTest/draw3/drawing_controller.cpp`，不是产品树 |

另：产品树中 `CanvasTouchGestureState::OnTouchDown(key,qpc,freq,inertiaActive,blockingContactActive)` 的调用方除 Ctl:12162 外，还有生产探针 Ctl:5254、5279、5330、5427、5489、5518（均传 `(…,1000,false,true)`），改签名会波及这些探针。

### 2.4 Mouse 如何建模进同一套质心代码

- 质心/速度循环只依赖 `gestureContacts[i].snapshot.position`、`velocityPosition` 与 `touchGesture.Disposition(key)`（Ctl:13620-13634）；`gestureContacts` 的登记函数 `addGestureContact`（Ctl:11310-11341）对设备无过滤（以 `CanvasTouchKey(handle)` 去重）。因此一个 Mouse contact 在结构上等价于"只有 1 个元素的触点集合"，Up 时走同一条 `OnTouchUp`→`topologyChanged`→`EndCanvasPan`（Ctl:13719-13735、13845-13907）。
- `CanvasTouchKey` 对 Mouse：RTS 为鼠标 tablet context id<<32|cid（cid 取值规律未验证，`OnTouchDown` 拒绝 key==0，Nav.cpp:184）；兼容路径为 `0xFFFFFFFE<<32|递增 id`（MouseInput.h:44、94），每次按下新 key。
- 质心代码**没有设备排除**：Touch 与 Mouse 同时为 Pan 时会被平均进同一质心（`count` 累加，Ctl:13625-13634）。
- 带"Touch"语义的周边：`cancelTouchDrawingForPan`/`penDownBelongsToTouchPan`/`retireEndedTouchBeforeDown` 均按 `DeviceType()==Touch` 过滤（Ctl:11816、11683-11686、11946-11947）；`window_.SetTouchPanActive(touchGesture.PanActive())` 每帧按**FSM 状态**调用（Ctl:13766），所以只要 FSM 里有 Pan，不论设备是谁，窗口层都会进入"Touch Pan"态（光标隐藏/笔反馈抑制/触觉禁止，见 §4.1）。
- `ShouldPrioritizeDrawingContact(navigationInProgress, penInContact, mouseInContact)`（Nav.cpp:533-537；Ctl:16785-16795）：导航中只要笔或鼠标 mailbox 为 contact 就先 `drainIngressBatch()`；`priorityMouseInContact` 读的是 `WM_MOUSE*` 光标样本的 `inContact`（Win:2209）。

---

## 3. 模式切换/页面/加载/最小化/笔：现有停止与复位钩子

### 3.1 钩子清单

| 钩子 | 位置 | 做什么 |
|---|---|---|
| `interruptNavigationForPenOrMouse(reason)` | 定义 Ctl:11958-11974；调用 12140、13543、16122 | `InterruptCanvasPanForDrawing`（`StopCanvasPan` 即 `motion={}` + `touchGesture.InterruptForPenOrMouse()`，Nav.cpp:503-510）→ `SetTouchPanActive(false)` → 所有 `gestureContacts[].disposition=Suppressed` → `panCentroidValid=false`、`previousPanContactCount=0`；**触点仍留在 gestureContacts/FSM 里直到各自 Up**（Ctl:13719-13742），且该批 `batchAllowsPan_=false`，剩余手指本批无法再 Pan |
| 笔 mailbox 落笔 | Ctl:13525-13544 | `penInContact && !suppressPenUntilRelease` → 立即 interrupt（reason `pen-mailbox-contact`）。**与是否 Selection 无关**，Selection 下笔 Down 虽被丢弃，但此 mailbox 路径仍工作 |
| 活动 Touch Pan 中的笔落笔 | Ctl:13531-13536、16776-16784 | `ShouldBeginSuppressingPenContactDuringTouchPan(PanActive,penInContact)` → `suppressPenUntilRelease=true` + `window_.SuppressPenContactForTouchPan()`，**不刹停**（spec:874） |
| 笔 hover 制动 | Ctl:13774-13801；Nav.cpp:472-501 | 惯性中 `penBrake = penInRange && !suppressPenUntilRelease` → `StepCanvasPanInertia(...,penInRange)` 使用 12000 而非 6000 减速（`kCanvasPanPenBrakeDecelerationDipPerSecondSquared`，Nav.cppm:22）；不是瞬停 |
| 画布命令 | Ctl:16114-16122 | Clear/Undo/Redo/NextPage/PreviousPage/SetPage/`TranslateViewport` 在处理前统一 interrupt。`ShortcutFence`、`SetWorkspace`、`SetWhiteboardTheme`、`SetPresentationTarget` 等更早 `continue` 的分支**不经过**该调用（Ctl:16016-16080 先返回） |
| 准入封口 `sealPresentationContacts` | Ctl:13364-13419；触发条件 `AdmissionRevision` 变化 | 对仍活动的 RuntimeStroke 补 Up；`gestureContacts` 全部 `DiscardUntilTerminal` 并清空；`touchGesture.Reset(); panMotion={}; panCentroidValid=false; previousPanContactCount=0; window_.SetTouchPanActive(false)`；重置触觉/橡皮 hover/激光层并 `RequestFullPresent`。它**不**重置 `lastNavigationQpc`、`inertiaBrakeStateValid/previousInertiaBrake`、`suppressPenUntilRelease`、`lastPanInputQpc`、`lastTouchPanEndQpc` |
| 准入来源 | Host.cpp:893-942 `RefreshPresentationInputGateLocked` | reasons：输入停止(1)、场景命令挂起(2)、白板冷页加载(4)、PPT 目标/就绪(8-128)、致命(256)、恢复(512)、工作区切换挂起(1024)、白板样式挂起(2048)；快捷键租约另算（`SetShortcutAdmissionBlocked`，Host.cpp:3476-3496） |
| 页面切换 | Ctl:16345-16481 | NextPage/PreviousPage/SetPage 之前已 interrupt；白板还会 `saveWhiteboard()`、冷页触发 `whiteboardLoading`（16426-16438），然后 `resetGpuForPageSwitch`（定义 Ctl:14609-14650：清 L0/L1/L2/激光/粒子、`++acceptedStrokeSceneGeneration`）与 `restorePageContent`；`EvictWhiteboardPage`（Ctl:571-581）把已保存的非当前页换成**新空 `InkPage`**（视口也随之回默认） |
| 工作区切换 | Ctl:16024-16080 | 交换文档槽、`whiteboardLoading.reset()`、`++whiteboardGeneration`、`restoreAfterDocumentSlotSwitch`（Ctl:14652-14688，重置 viewport 恢复状态，**不碰 panMotion**）；导航停止依赖其前置的 admission 封口（`SetWorkspaceTransitionPending`，Host.cpp:3111-3128） |
| 白板加载中 | Ctl:13480、16099-16104 | 新 Down 被丢弃；除 `PrepareExitAutoSave` 外的画布命令被 Blocked |
| 最小化 | Host.cpp:3308-3325（测试描述）；Window.cpp:3158-3159 | 走"现有工作区准入原因"，即同样触发封口；Drawpad 同时 click-through。WindowControl 对 `WM_SHOWWINDOW(0)/SWP_HIDEWINDOW` 只做触觉离开+唤醒（Win:1981-2009），RTS 模式下不主动取消 contact |
| Resize / 设备恢复 | Ctl:16704-16756、16596-16671 | 只设 `viewportRefreshPending` 等，**没有** interrupt 调用（spec runtime-and-rendering.md:882 写"页面切换、Undo、Resize 和键盘平移先终止手势/惯性"，代码里 Resize 一支未见该调用） |
| Settings 打开 | 未在 `Draw3/**` 找到任何钩子 | Setting 是独立 owned 窗口（spec rendering-and-ui.md:1871：绘制态 owner=Drawpad，Selection/穿透态 owner=null）；Draw3 不感知，已在画布上的 contact 保持到 Up |

### 3.2 Move → 绘制工具（含惯性中）

- `SetSelectionMode`（Win:672-680）仅翻 `selectionMode_`、`RequestDrawingCursorRender`、`QueueSystemCursorRefresh`、`RequestControlWake`；由 `PumpBridgeState`（Host.cpp:1787）在绘制线程应用，同一处 `window.SetActiveTool(tool)`（Host.cpp:1814）。
- Ctl 主循环每帧读 `window_.SelectionMode()`（Ctl:16519）只用于：输出目标（16522-16532）、1ms 计时器（16534）、光标可视（13978）、触觉（11155、16941）、橡皮 hover（13945）。**没有任何代码在 Selection 翻转时停 `panMotion`/`touchGesture`/`gestureContacts`**。
- 因此：正在 Pan 的手指在切到绘制工具后会继续平移到抬起；惯性会继续滑行，直到被 §3.1 的 Pen/Mouse Down、画布命令、笔 contact mailbox、准入封口之一打断，或自然衰减。
- 绘制工具下**新的 Touch Down** 会走现有 Touch 分支（Ctl:12142-12213）：若 `panMotion.inertiaActive` 则首指成为 `PanCandidate`（不绘制）；第二指 180ms 内会接续 Pan（旧逻辑）。这正是产品决策要移除的"绘制模式双指触发"，即该路径现在对"Move→绘制切换后的惯性残留"也会生效。
- 反方向（绘制→Move）：活动的绘制 RuntimeStroke 不受影响地走到 Up（Ctl 无 Selection 中断逻辑，仅橡皮会话在 `finishMouseSpeedEraser` 里因 `SelectionMode()` 记为 cancelled，Ctl:11799-11801）；兼容路径的鼠标则被 `MouseInputState::Move` 取消并隔离（MouseInput.h:64-69）。

### 3.3 `TranslateViewport` 与视口持久

- 白板下 `TranslateViewport` 命令直接丢弃（Ctl:16316-16323），全工程无生产者（grep 仅命中 Ctl:16316 与 Win.cppm:96）。
- 实际平移写视口的唯一位置是 `updateCanvasNavigation` 末段（Ctl:13807-13843）：`ApplyCanvasContentTranslationChecked` → `canvas->SetViewport({x,y,1})` → `InvalidateRaster`、`markPresentationMutation()`、`viewportRefreshPending=true`、`historyGpuCache.DiscardHotPreimages()`；触及 `±1048576` 清零该轴速度。每个 Page/Canvas 存自己的 `InkViewport`（`runtime->viewport = canvas->Viewport()`，Ctl:12646；spec:882）。

---

## 4. 光标 / 触觉 / 视觉副作用与测试接入

### 4.1 Selection（含将来的 Move 拖动）下的现状

- **应用光标**：`buildDrawingCursorVisuals` 在 `SelectionMode()` 时直接 `return`（Ctl:13978-13989，"选择态只呈现画布和瞬态层，不保留绘制光标"）。
- **系统光标**：`ApplyWindowCursor`（Win:1378-1456）：`mouseUsesSystemCursor = selectionMode || ...`（Win:1413-1414，注释"选择态只交还鼠标；白板仍显示主画布时，Pen/Touch 继续按原策略隐藏"，Win:1412）。`ShouldHideSystemDrawingCursor`（PenCursor.cpp:459-485）：Eraser/Laser 恒隐藏；`touchPanActive && !realMouseTakeover` → 隐藏；owner=Pen/Touch → 隐藏；owner=Mouse → `!mouseUsesSystemCursor && mouseSampleValid`（Selection 下为 false → 显示箭头）。光标只有一种：`defaultCursor_ = LoadCursorW(nullptr, IDC_ARROW)`（Win:361、1438），**没有抓手/拖动光标**。
- **Touch 光标抑制**：RTS 的 Touch Down 回调同步调用 `NotifyTouchContactBegin()`→`SetTouchCursorSuppressed(true)` 并 `ClearMouseCursorSample()`（Win:978-1008），除非"Touch Pan 且真实鼠标已接管"（Win:987-993）。`SetTouchCursorSuppressed(false)` 在笔/鼠标样本到达时（Win:1075、2198）。真实鼠标消息在 Touch Pan 中会置 `realMouseTakeoverDuringTouchPan_`（Win:2196-2197），`SetTouchPanActive(true)` 时清零（Win:919-920），结束时按 `ResolveDrawingCursorOwnerAfterTouchPan` 恢复 owner（Win:939-947；PenCursor.cpp:512-521）。
- **笔相关副作用（Touch Pan 期间）**：`ShouldSuppressPenFeedbackForTouchPan`（PenCursor.cpp:609-613）、`ShouldTreatMouseContactAsPenCompatibilityMessage`（PenCursor.cpp:591-600：`touchPanActive` 且有新鲜 pen presence、鼠标 contact、距离 ≤8px、样本龄 ≤100ms 的 `WM_MOUSE*` 被当作笔兼容消息吞掉，Win:2180-2195）、`SetPenContactSuppressedForTouchPan`（Win:1032-1050）。**以上都挂在 `touchPanActive_` 上，而 `SetTouchPanActive` 是按 FSM 的 `PanActive()` 调的（Ctl:13766）**。
- **触觉**：`Haptics::ContinuousAllowed(selection, visible, laser, blocked, quarantined, pan, suppressed)` = `!selection && visible && !laser && !blocked && !quarantined && !pan && !suppressed`（HapticRequest.h:44-48）；调用点 Ctl:11155-11157、16941-16944。Selection 下连续触觉被禁；UI 点击触觉在 `!active.empty() || touchGesture.PanActive() || input_.HasQuarantinedContacts()` 时被丢弃（Ctl:17030）——现状下 Move 模式的被丢弃 contact 处于 Quarantined，会同样挡住 UI 点击触觉。Pen 的 WM_POINTER 触觉 Observe 不看 Selection（Win:1833-1844），由 `canvasHapticsAvailable` 在使用处门控（Ctl:11152-11158）。
- **计时器/帧率**：`timerPeriod.SetSelectionMode(selectionMode)`（Ctl:16534）→ Selection 时 `timeEndPeriod(1)`，非 Selection 时才 `timeBeginPeriod(1)`（TimerPeriod.h:30-43，注释"选择模式整段释放高精度计时器"，Ctl:16533）。导航帧靠 `WaitForFrameDeadline` 等待（Ctl:18572-18581；ContactInput.cpp:1223-1252：`WaitForSingleObject(wakeEvent, floor(剩余-1.25)ms)` 之后自旋），目前**没有任何 Selection 下持续活动帧的场景**，因此 Move 平移会首次在计时器分辨率被释放的状态下逐帧运行。
- **主循环 idle**：只要 `gestureContacts` 非空、`panMotion.inertiaActive`、`viewportRefreshPending/RecoveryPending` 任一成立就是 `navigationActive`，不进入阻塞 idle（Ctl:17157-17199）；Selection 下当前不会出现这些状态。
- **呈现**：白板 Selection 输出目标 `PrimaryDrawpad`，无需 ULW 全帧零 alpha 校验（`auxiliaryCleanVerificationPending` 只在 `selectionUsesAuxiliary` 时置位，Ctl:16531、17115-17124、18475-18485）。

### 4.2 现有 Headless / 生产探针覆盖

| 位置 | 覆盖 |
|---|---|
| `InkeysHeadlessTests/draw3_mouse_input_tests.cpp`（`RunDraw3MouseInputTests`，:223-232） | `MouseInputState` 生命周期/左右键接管（:51-90）、启动按住隔离/Selection 拒 Down/页边界/取消（:92-132，**:105 固化"selection refuses Down"**）、捕获丢失重同步（:134-169）、Stop/owner 交错（:171-220）。不触及 Controller |
| `InkeysHeadlessTests/draw3_contact_tests.cpp`（`RunDraw3ContactInputTests`，:1967） | Coordinator 生命周期/唤醒/命令屏障/页边界 admission 与 Quarantine（:933-984）、Closing/Discard 活性（:986-1121）、光标样式与归属（:1123-1832，含 `ShouldHideSystemDrawingCursor` 调用 :1450-1451、1729、1755、1790 与 Touch 光标归属 :1778-1840）、fatal/快捷键 admission（:1906-1990）。**没有**平移/Selection-Pan 用例 |
| `InkeysHeadlessTests/draw3_haptic_tests.cpp:18-21` | `ContinuousAllowed` 各条件边界 |
| `Inkeys.exe --draw3-control-fence-test`（IdtMain.cpp:2021-2022 → Ctl:5910） | Selection 对 3 工作区×5 设备类型的丢弃（Ctl:6021-6093）；Touch 丢弃身份（Ctl:5234-5288，内部用 `CanvasTouchGestureState`）、单触续接准入（Ctl:5290+）、重连身份（Ctl:5393+）。这些探针不进入 `InkeysHeadlessTests` |
| 旧沙盒 `inkStrokeModelerTestTests/canvas_navigation_tests.cpp`（429 行，`RunCanvasNavigationTests`，由 `contact_input_tests.cpp:3638` 调用） | 视口平移/限位、FSM 180ms 边界/额外触点/候选、anchor 补齐、拓扑 reset、释放速度拟合等；使用 `import draw3.canvas_navigation; namespace draw3`。**该工程不在 `InkeysRepo.sln`**（`InkeysRepo.sln` 中无 `inkStrokeModelerTest*`，它们只在独立的 `inkStrokeModelerTest.sln`；产品 `Inkeys.vcxproj:1051-1055` 仍会从 `..\inkStrokeModelerTest\draw3\` 引入 `uink_model/uink_codec` 等共享源，但不含 `canvas_navigation`）。沙盒 `draw3/canvas_navigation.cpp/.cppm` 与产品 `Draw3.CanvasNavigation.cpp/.cppm` 逐字相同，仅模块名/命名空间不同（已 diff 验证） |

### 4.3 新增纯 CPU 测试如何导入 `Inkeys.Drawing.Draw3.canvas_navigation`

- 产品工程已编译：`Inkeys/Inkeys.vcxproj:1047-1050`（`.cpp` 普通，`.cppm` 带 `<CompileAs>CompileAsCppModule</CompileAs>`）。
- `InkeysHeadlessTests/InkeysHeadlessTests.vcxproj` **未包含**（全文 grep `CanvasNavigation` 无命中）。它按需列出 Draw3 单元：`Draw3.ContactInput.cpp/.cppm`（:133-134）、`Draw3.PenCursor.cpp/.cppm`（:138-139）、`Draw3.CanvasDebug.cpp/.cppm`（:136-137）、`Draw3.SpeedEraser.cpp`（:233）、`Draw3.PalmEraser.cpp`（:234）、`Draw3.Bridge.cpp`/`Draw3.Presentation.cpp`（:129-130）。这里 `.cppm` 都写成不带 `CompileAs` 的 `<ClCompile Include=…>`（:134、139）。工程开启 `ScanSourceForModuleDependencies`（:78）、C++20、`DRAW3_CONTACT_TESTING` 宏（:75），无 `.filters` 文件。
- `Draw3.CanvasNavigation.*` 仅依赖标准库（Nav.cppm:3-8，Nav.cpp:3-6），不 import 其他 Inkeys 模块，可直接加入而无顺序依赖。
- 测试文件模式：参考 `draw3_mouse_input_tests.cpp`（头部 `import Inkeys.Drawing.Draw3.contact_input;`，匿名命名空间 `Expect(bool,name,failures)`，导出 `int RunXxxTests()`）。接入需三处：`.vcxproj` 增加 ClCompile、`animation_tests.cpp` 顶部前置声明区（:71-79 附近）加 `int RunXxxTests();`、`main` 里 `failureCount += RunXxxTests();`（:1877-1879 附近）。
- 说明：旧沙盒测试 `canvas_navigation_tests.cpp` 的断言是在**当前模型**（180ms、候选、`cancelExistingTouchDrawing`）上写的，直接复制到 Headless 会随 FSM 行为变更失效。

---

## 5. 相关 Spec / 既有调研

- `.trellis/spec/native/runtime-and-rendering.md:848-921` "Free Canvas Navigation And Predictive Recovery"：:869（门禁常量）、:870-872（180ms/候选/超时）、:873（不叠加旧速度）、:874（Pen 与 Touch Pan 的 RTS 行为）、:875（cursor owner 与 Touch Pan 分离）、:876-877（mailbox 时序、惯性减速 6000/12000）、:878-879（旧批次退休/anchor/lifecycle 一致性）、:881（Mouse mailbox 不是导航真值）、:882（页面/Undo/Resize 先终止手势；每页独立 viewport）、:895-908（验证矩阵）。
- `.trellis/spec/native/runtime-and-rendering.md:1143-1147`：调试探测接触"不复用 DiscardSelectionDown：被隔离的接触读不到后续 Move/压力/面积"——即 Quarantined 不可作为 Pan 数据源的既有结论。
- `.trellis/spec/native-desktop/rendering-and-ui.md:1871`：Setting 窗口 owner 规则（Settings 打开路径）。
- `.trellis/tasks/10-27-whiteboard-first-open-visibility/research/exit-desktop-mouse-followup.md:69`（"不存在本轮应保留的已启用 Selection 平移手势……应显式区分导航与墨迹，不能继续用 Selection 漏洞承载"）与 `…/planning-before-final-state-machine/design.md:65`（Selection 丢弃共用操作的由来）。

---

## Caveats / Not Found

- **未实测**：未运行任何程序；Window 层白板 Selection 的 `WS_EX_TRANSPARENT` 实际生产写入方未找到（见 §1.6），需联调确认 Drawpad 在白板 Selection 时确实接收 RTS 输入（`DiscardSelectionDown` 的存在与 `SelectionUsesAuxiliaryOutput` 的白板例外都表明设计上是接收的）。
- **RTS 鼠标细节未验证**：RTS 鼠标 `contactId` 取值规律；鼠标拖出窗口后 RTS 是否继续交付 Move；中键是否产生 StylusDown；交换鼠标键时 `GetAsyncKeyState` 的 L/R 判定。
- **Win32Mouse 兜底模式**：触摸被系统提升成单点鼠标，不可能多指质心；`MouseInputState` 的 `selectionMode` 参数与 `WindowController` 无 workspace 信息，两点均由现有代码事实决定（§1.3）。
- **平移单位**：常量与 spec 称 DIP，代码实际在物理像素上运算，无 DPI 换算（§1.7）。
- **Resize 与 spec 不一致**：spec:882 称 Resize 先终止手势/惯性，Ctl 的 Resize 分支（16704-16756）未调用 interrupt。
- `inkStrokeModelerTest(Tests)` 沙盒不在 `InkeysRepo.sln` 内，其源码文本断言（含 `kCanvasNavigationProductIntegrationEnabled = false` 与 `within-180` 等串）针对沙盒副本，不影响产品树构建；是否仍在 CI 里运行未核实。
