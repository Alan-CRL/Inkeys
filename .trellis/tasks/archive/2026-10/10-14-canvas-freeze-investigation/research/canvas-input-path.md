# Research: Draw3 输入与绘制线程的持续阻塞路径

- Query: 调查打开/关闭设置、打开资源管理器并点选择后，UI 可继续操作而画布不响应的 Draw3 输入与渲染路径；寻找可永久保持的准入、命令边界、锁、RTS 生命周期和捕获状态，并给出精确排除方法。
- Scope: internal；仅只读检查产品源码和项目规范。视频日志事实由主会话提供，本研究未独立逐帧读视频。
- Date: 2026-10-14
- Updated: 2026-10-15；补充视频约74秒清除旧墨迹的反证及现有诊断能力边界。
- Active task: `.trellis/tasks/archive/2026-10/10-14-canvas-freeze-investigation`
- Modification boundary: 仅新增本研究文件；未修改产品代码、未运行 GUI、未运行构建、未执行 Git 操作。

## Findings

### 1. 当前结论和证据等级

**直接确认：画布实际落笔只依靠 RTS contact，Win32 的 `accepted` 鼠标日志属于独立的光标反馈。** 不能用视频中的 `accepted=1`、`mouse=1/1` 证明 RTS Down 已到达或已被控制器接受。证据：`Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:1731` 仅构造 `DrawingCursorSample` 并调用 `PublishMouseCursorSample`；`Draw3.RealtimeStylus.cpp:1225` 才调用 `coordinator_.PublishDown`；控制器仍在 `Draw3.DrawingController.cpp:9837` 检查 `ContactAdmitted`。

**直接确认：视频提供的 `tool=0 / mouseLifecycle=0/0 / present success=0 dirty=(0,0,0,0)` 不是三个故障指示。** `tool=0` 正是 Pen，`mouseLifecycle` 是速度橡皮的视觉位置/动画状态；空脏区的循环没有执行 Present 时，同样打印 `success=0`。这些日志自身不能定位根因。

**可验证假说：** 最先应确认 RTS Down 是否继续进入，再确认 Down 是否被发布/出队。当前代码有一个能保持阻塞的 controller 命令边界：旧 `active` 不为空时 canvas command 等终态，同时后续 Down 不再出队。它需要“旧 contact 终态没有收敛”等前提；尚无现场证据证明此问题就是该路径。

**后续视频反证（主会话提供）：** 约74秒 `Build/CanvasFreezeInvestigation/video/end-01.jpg` 的旧红色墨迹已被清除，随后软笔尝试仍只有Win32 accepted而没有新墨迹；约50秒起曾出现橡皮光标却擦不掉旧内容。这明显削弱“50秒之后所有canvas command始终被同一个旧active阻塞”的全程解释。若清除对应真实Clear命令，则该时刻controller命令/呈现链已经推进；它不能同时被描述为始终未消费任何命令。仅凭画面消失仍不能独立区分Clear、隐藏表面或换场景，因此保留现场命令序号验证，但将这一候选降级。

**排查排序是工作假说：** 当前优先检查“RTS交付中断或先前callback占用”，其次是“Down到达但decoder持续失败”，再检查“已published但controller admission拒绝”。箭头只表示取证顺序，不表示前三者已经发生，也不表示它们构成已证实的因果链；完整原始日志和故障快照可以改变排序。

**尚未确认：** 设置/资源管理器的具体系统事件是否使 RTS 停止回调、卡在生命周期 COM 查询、或导致画布窗口命中/层级变化。仅靠当前源码和提供的几行视频日志不能把这些候选升级为根因。

### 2. Files found

| 文件 | 作用与证据 |
| --- | --- |
| `Inkeys/Inkeys.vcxproj:925` | 编译 `IdtState.cpp`、`IdtDrawpadFacade.cpp`；`1038`、`1046`、`1083`、`1114`、`1117` 注册本研究所追踪的 Draw3 ContactInput、Controller、RTS、Host 和 Product。 |
| `Inkeys/IdtDrawpadFacade.cpp:8` | 旧 Draw2 符号兼容存根；旧 `DrawpadInstallHook` 不安装、旧 `CreateRealTimeStylus` 返回空，不能用此处全局锁解释当前生产输入卡死。 |
| `Inkeys/IdtState.cpp:215` | 从统一状态发布 bridge；`723` 同步 owner/工具/画布显隐；`1104` 选择按钮可先发布 Clear。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Product.cpp:75` | WndProc 与产品发布的调用闸门；`85` 合并工具属性、`135` 只有 Presentation 身份才能暂停输入。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp:549` | 聚合 Presentation/command scene 准入；`1243` 消费 bridge；`1924` 运行快照；`2197` 身份化暂停。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Bridge.cpp:30` | 短锁发布不可变产品状态；`119` 有界命令 FIFO；不等待渲染/窗口 API。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.cpp:737` | Down 取得 slot 并入队；`889` 奇偶 admission revision；`956` dequeue；`1009` idle 等待。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp:1137` | 实际 Down producer；`1346` Packets；`1086` / `1102` lifecycle；`1473` mapping；`2463` 初始化。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:9822` | ingress/命令边界；`11824` canvas command 安全点；`12598` 主循环；`14419` 诊断的 present 字段。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:1562` | 鼠标/Pointer 光标反馈；`1526` 激活/显隐只请求完整重呈现，没有重新注册 RTS。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.PenCursor.cpp:269` | primary cursor 的可见性；系统鼠标模式明确不生成应用 primary visual。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cpp:1257` | 同一绘制线程提交主/辅助输出；`625` DComp 释放有同步提交等待。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Diagnostics.cpp:574` | 绘制线程排空输入/光标诊断；RTS/窗口事件先进入有界内存队列。 |
| `Inkeys/Inkeys/Input/MouseHook.cpp:98` | 产品低级 hook：Down/Up 更新键状态后继续 hook chain；仅 PPT 全局滚轮分支吞消息。 |

### 3. 实际输入和线程所有权

```text
Window Service owner thread
  DrawpadMsgCallback
    -> Product::ForwardProductMessage（recursive productCallMutex）
      -> Host::ForwardMessage -> WindowController::HandleExternalMessage
        -> WM_MOUSE / WM_POINTER -> cursor sample + control wake

RTS 同步 callback thread
  StylusDown -> stateWriterMutex/stateGate -> decoder/binding
    -> ContactInputCoordinator::PublishDown -> 固定 slot + 唯一 FIFO + event
  Packets -> 非等待 reader gate -> 最新 contact snapshot
  StylusUp/Disabled/Error -> 终态/取消 + wake

唯一 Draw3 drawingThread
  ingress -> Bridge state/command -> controller admission -> runtime/model
    -> document/history/L0/L1/L2 -> transparent presenter
  同线程消费 cursor sample，故即使没有 RTS contact，鼠标移动仍可能推动 frame 日志

UI3 / Setting 的共享 RenderPipeline
  独立于 Draw3 device/context/thread；主会话另行调查其 Owner/ImGui 死锁
```

- `Draw3.Product.cpp:12` 明确 `SetWindowPos` 可同步重入 WndProc，因此 `productCallMutex` 使用 recursive mutex；同一闸门也覆盖工具发布（`88`）。若**同一时段仍有新的 Drawpad accepted 鼠标事件**，它们已经通过这把锁，不能同时声称此锁处于永久占有且阻断整个 Drawpad WndProc。
- `Draw3.Host.cpp:1574` 在图形就绪后才允许 RTS 初始化；`1684` 才进入 `drawing->Run()`。这些 startup handshake 的无限 condition wait 不属于已正常绘制后突然卡死的常规路径。
- `Draw3.Host.cpp:1851` 正常 Stop 先停 RTS，再提交最终保存屏障。打开设置/资源管理器没有调用此流程的证据；不能把退出屏障等待误认为运行期暂停。

### 4. 视频日志逐字段解释

主会话提供：约 50 秒 UI 软笔高亮，绘制线程日志 `tool=0 owner=2 primary=0 touchVisuals=0 mouse=1/1 mouseLifecycle=0/0`；随后 `present success=0 ... dirty=(0,0,0,0)`；Window owner thread 继续输出 accepted 鼠标事件。

| 字段 | 当前产品源码的精确定义 | 能/不能证明什么 |
| --- | --- | --- |
| `tool=0` | `DrawingTool::Pen`，`Draw3.WindowControl.cppm:29`；frame 使用 effective cursor tool，存在 primary runtime 时还会用 runtime 工具覆盖，`Draw3.DrawingController.cpp:10383`、`10402`。 | 与软笔一致，不是“空工具”；也不能单独代表 bridge revision 已完整应用。 |
| `owner=2` | `DrawingCursorPointerAuthority::Mouse`，`Draw3.PenCursor.cppm:42`。 | 当前视觉归属为鼠标，不代表实际 RTS MouseLeft Down 已建立。 |
| `mouse=1/1` | `mouseSample.valid / mouseSample.inContact`，`Draw3.DrawingController.cpp:10552`。 | Win32 sample 有效且按键位表示接触；不代表 controller active contact。 |
| `mouseLifecycle=0/0` | `mouseSpeedEraser().HasPosition() / NeedsAnimation()`，`Draw3.DrawingController.cpp:10553`。 | 只是速度橡皮视觉生命周期；普通 Pen 时两个 0 正常。不是 RTS contact 计数。 |
| `primary=0` | primary 应用光标 visual 数量，`Draw3.DrawingController.cpp:10477`。 | 系统鼠标光标启用且非橡皮时，`Draw3.PenCursor.cpp:295` 提前返回 invisible，因此 0 可正常。 |
| `present success=0 dirty=0` | 本轮 `presentSucceeded` 在 `Draw3.DrawingController.cpp:14286` 初始化 false；只有非空 dirty 且 raster 未失败才调用 `PresentFrame`（`14287`、`14379`）；诊断仍在 `14419` 输出。 | 可能表示根本没有尝试 Present，不能认定 DXGI/ULW 调用失败。实际失败应看真实 Present observer/错误码/attempt count。 |
| 持续新的 frame 日志 | `Draw3.DrawingController.cpp:10546` 在绘制循环中生成，`14454` 排空。 | 持续产生的新 tick/seq 支持该时段绘制循环在推进，反对当时永久停在 mutex、GPU Map、Present 或 console write 内。只看到旧屏幕残留行不构成此证明。 |

### 5. 能持续阻断的状态及作用范围

#### A. 全局 admission revision（有明确的桌面排除式）

- `Draw3.ContactInput.cpp:889` 用 revision 奇数表示 blocked；`913` 的 `ContactAdmitted` 同时要求当前 revision 偶数、generation 匹配、Down 锁存 revision 恰好等于当前值。
- Down 在 `Draw3.ContactInput.cpp:756` 锁存 revision，但 `PublishDown` **不会因为 blocked 而拒绝入队**。因此 `rts-input ... published=1` 仍不证明 controller 已接受；被拒 Down 在 `Draw3.DrawingController.cpp:9837` 交给 `DiscardUntilTerminal`。
- Host 常规写入集中在 `Draw3.Host.cpp:550`：

  ```text
  blocked = commandScenePending ||
            (desired.workspace == Presentation
             ? target不存在 / ready身份不匹配 / suspended / UI未ack
             : applied.workspace == Presentation)
  ```

- 因而 `desired.workspace=Desktop && applied.workspace=Desktop && commandScenePending=false` 时，常规刷新应得到 `blocked=false`。此组合若仍长期 blocked，必须调查 fatal/线程停滞/未刷新或实际数据不一致，而不是归因为设置某个未找到的 pause 布尔值。
- `Host::SetPresentationInputSuspended` 在 `Draw3.Host.cpp:2203` 拒绝非 Presentation、空目标或身份不匹配的请求；源码调用方是 `Inkeys/IdtPlug-in.cpp:806`、`857` 的 PPT 桥接。没有“打开设置就直接写 suspended=true”的入口。
- `Draw3.Host.cpp:838` 的 workspace-ready observer 会清 `commandScenePending` 并刷新 gate。正常产品 reset 在 `603` 至 `612` 初始化 Desktop/selection/gate。
- fatal 路径 `Draw3.DrawingController.cpp:3181` 显式封闭 admission，`11115` 在图形 fatal 取快照时调用；Host 异常会打印 `drawing loop stopped` 并 `RequestExit`（`Draw3.Host.cpp:1686`）。若后续真实 frame 持续推进，此路径优先级降低。

#### B. controller 命令边界（持续阻塞的候选，非已确认根因）

1. `Draw3.DrawingController.cpp:9827` 消费控制 token，若 Host 创建 canvas command，在 `9831` 设置 `commandBoundaryPending=true`。
2. `62` 至 `75` 的 `DrainIngressBatch` 和 `9877` 至 `9883` 的 `tryConsumeOneIngress` 在该位为 true 时停止出队，包括后续 Down 和 General 控制唤醒。
3. canvas command 只在 `active.empty()` 时执行，证据 `11835`、`13163`、`14188`；全部 pending command 处理后才在 `13169` / `14192` 复位边界。
4. 点选择可能先发 Clear：`IdtState.cpp:1107` 要求模式配置为 1、非白板、当前有内容；`1110` 要求 Clear 已 Accepted 后才切换 Selection。模式配置不满足则这一 Clear 触发前提不成立。
5. 普通 Desktop Clear 不改变 admission revision：`Draw3.Host.cpp:1330` 只在命令或当前工作区涉及 Presentation 且场景不匹配时关闭 gate。`sealPresentationContacts` 在 `Draw3.DrawingController.cpp:9776` 只在 admission revision 变化时主动封口，因此不会无条件解除 Desktop Clear 对旧 active 的等待。

**可造成持续等待的前提：** 存在始终不结束的 active runtime，或其终态收尾永远未完成，随后遇到 canvas command。此时 frame/cursor 可以继续推进，但新的 Draw3 state/Down 会在命令后排队，表现为 UI 切换而画布工具不跟随。

**防止误判的反证：** RTS `StylusDown` 在 `Draw3.RealtimeStylus.cpp:1170` 找到同 `tcid/cid` 旧 binding 时先 `PublishCancelled`。因此普通鼠标复用同一 contact ID 的新 Down 通常能取消旧 producer；不能只靠一个缺 Up 就断言用户必然永久卡死。`active.empty()==true` 或命令已处理完也直接排除此等待条件。已 ended runtime 在 `Draw3.DrawingController.cpp:14082` 被移出 active，即使 Stored raster 失败也不会仅因此永久留在 active。

**视频约74秒的新证据降低本候选优先级：** 旧墨迹随后被清除而新笔迹仍无响应，更符合“canvas command与绘制contact分开”的待查形态。应分别采样Clear被接受/处理的序号和RTS Down链，不能继续使用“所有命令从此永久卡住”概括这段视频。

#### C. RTS lifecycle / writer gate / decoder

- `Draw3.RealtimeStylus.cpp:109` 的 `RtsStateWriterGuard` 获取非递归 `stateWriterMutex`，置 writer bit 后等待 reader 退出；Down/Up、Enabled/Disabled、TabletAdded/Removed、UpdateMapping 都走 writer。
- Packets 使用 `RtsPacketStateGuard`，遇 writer 只返回 state-busy（`1359`），不等待；reader 析构 `97` 归还计数。writer 正常析构 `123` 清 gate，没有普通 UI pause 状态。
- writer 内仍可能调用 RTS COM：Down 的 `ResolveContextDecoder` 可进入 `EnsureContextDecoder`（`1723`），后者必要时 `GetAllTabletContextIds` / `GetPacketDescriptionData` / `BuildContextDecoder`。Lifecycle mapping 重建同样会调用这些 API。
- 若 callback 卡在 COM 查询、writer mutex 或 reader 等待中，Draw3 与 Win32 光标仍可以活；这与 UI RenderPipeline 死锁是不同的候选。**这是可能阻塞的调用位置，不是已证明 API/重入死锁。** 需要同一现场线程栈与 entry/exit 记录。
- Disabled 在 `1109` 调 `ResetDecoderLifecycleState`，cache `Reset` 会置 `lifecycleEnabled=false`（`687` 至 `694`），后续 Down 的 `EnsureContextDecoder` 在 `1729` 直接拒绝 disabled 生命周期。Enabled 会 `BeginLifecycle`（`1096`）；TabletRemoved/UpdateMapping 的 rebuild 也先 BeginLifecycle（`1706`），所以不能误报这些重建“忘记重新启用”。
- `RealTimeStylusInput::Initialize` 仅绑定主 HWND（`2531`）、AllTablets TRUE（`2542`）、加唯一 plugin（`2670`）、put_Enabled TRUE（`2687`）。运行期 Settings show/hide 的 WndProc 只 RequestFullPresent（`Draw3.WindowControl.cpp:1526`），没有重新验证当前 RTS Enabled 或绑定状态。

#### D. slot 耗尽与 quarantine

- Down 无空 slot 会拒绝：`Draw3.ContactInput.cpp:745` 至 `749`。拒收 Producing route 在 `863` 至 `885` 隔离到真实 Up/Cancel 才归还，不能为恢复画布直接 Reset/复用活 route。
- 连续丢终态并产生不同 ID 可耗尽 slots；仅单次打开设置不足以证明已耗尽。需要观察 `occupiedSlots/slotCapacity/downRejected/recycled` 和所有旧 route 的 tcid/cid/generation。
- `Draw3.ContactInput.cppm:173` 明确热路径计数须启用。Host 在 `Draw3.Host.cpp:1481` 仅对 hidden test 或 runtime metrics 开启计数；**普通 Cursor console 开关不会自动开启**。`HostRuntimeSnapshot.inputDownPublished` 等全 0 不得用于排除 RTS Down。已有快照又只转发四项计数（`Draw3.Host.cpp:1951`），未把 downRejected/occupiedSlots 透出。

#### E. 捕获、工具和视觉抑制的排除边界

- 所检查 Draw3 生产路径没有 `SetCapture/ReleaseCapture/SetPointerCapture` 调用；Pointer/Mouse WndProc 只发布反馈，没找到 CaptureChanged/CANCELMODE 触发的持久绘制 pause。若有遗留捕获，来源需在 Setting/Bar/Window 侧核查，不应把 Draw3 cursor owner 当作 Win32 capture。
- `suppressPenUntilRelease`、TouchPan、pen compatibility、touch cursor suppression 影响 Pen/视觉/导航，不能单独解释真实 Mouse RTS Down 全部失效。新 Pen Down 也在 `Draw3.DrawingController.cpp:8576` 解除局部 Pen suppression；产品 CanvasNavigation 编译门 `Draw3.WindowControl.cppm:26` 为 false。
- 旧 facade 的 `drawWaiting`、`TouchList`、旧 g_pRealTimeStylus 等是兼容符号（`IdtDrawpadFacade.cpp:8`），不能因名字像锁而把它们列成当前 Draw3 暂停源。
- 低级 `MouseHook::HookProc` 对 Down/Up 没有 return 1 截断，证据 `Inkeys/Inkeys/Input/MouseHook.cpp:104` 至 `140`；吞消息分支仅为 PPT 鼠标滚轮（`121` 至 `129`）。

### 6. 同步等待与“UI 活”的区分

- 输入的 spin latch 只串行队列 try_enqueue，`Draw3.ContactInput.cpp:461` 至 `470` 没有持有它处理业务/窗口/COM；正常代码中没找到设置调用与此 latch 的等待环。
- idle 的 `WaitDequeue`（`Draw3.ContactInput.cpp:1009`）用自动复位 event 和 wake generation 双重检查；Down/terminal/control 都会 signal。持续 mouse frame 表明至少 control/cursor wake 尚可到达绘制线程；它不排除 RTS 没发布 Down，也不排除 controller 的命令边界阻止处理 Down。
- `WaitForWake` 和 frame deadline 除显式 idle外有时间上限（`1130`、`1164`）；未发现普通 Setting 打开后锁存的无限 frame pause。
- DComp `WaitForCommitCompletion` 存在于 presenter Reset（`Draw3.TransparentPresentation.cpp:625` 至 `638`），可能在重建/释放时阻塞；不应把它作为每帧正常等待。是否卡在 GPU/USER32/COM 必须取现场栈。持续新增的 controller frame 排除此类永久阻塞发生在同一时段。
- hidden-successful-Present 无限 event 等待（`Draw3.Host.cpp:736` 至 `744`）同时要求 hidden injection 和两只非空测试 event，正常产品 options 不满足。Contact close 的测试 pause 默认空（`Draw3.ContactInput.cpp:577`）；不应误列成正常设置暂停机制。
- 真实 Present 失败会经 `Draw3.DrawingController.cpp:7585` 请求完整重呈现/恢复；是否失败要看 `Host` 的真实 observer/attempt/result，而不是空 dirty 的 cursor trace。

### 7. 最小诊断与精确排除方法（建议给后续 implement 调查代理）

以下是调查方案，当前未改代码，也不是修复实现。

| 假说 | 最少需要的现场证据 | 精确排除条件 |
| --- | --- | --- |
| RTS callback 停止、disabled 后未恢复、collector/绑定异常 | 主 HWND、RTS owner/callback TID、Down arrival 和完成计数、Enabled/Disabled/Error/Mapping/Tablet 事件序号与 HRESULT、一次受控 `get_Enabled/get_HWND` 结果、GetGUIThreadInfo 捕获事实。 | 故障期间每次实际新 Down 都有到达及 published=1，且随后 controller accepted；不能只看 WM_MOUSE accepted。 |
| RTS callback 卡在 writer/decoder COM | Down arrival 后无阶段完成；记录 `writer-enter/writer-acquired/decoder-enter/decoder-exit/published`；同一现场 RTS callback stack 和 stateGate writer/readers 值。 | 每次 Down 全阶段完成，或现场所有相关线程不在相应锁/COM 中；没有 arrival 也不能直接判 writer 卡住。 |
| commandBoundaryPending + 旧 active | controller 线程记录 commandBoundaryPending、pending canvas command 类型/序号、active/physical/ended/reconnect 数和每个 runtime 的 record+generation、down/latest sequence/phase/qpc；同时比较 bridge revision 与 appliedBridgeRevision。 | active 数为 0 且 pending command 已排空，或新 Down 已被 controller accepted；同 tcid/cid 新 Down 取消旧 route 后能继续则属于短暂等待。 |
| admission 长期 blocked/PPT 身份未收敛 | 同快照记录 desired/applied workspace、commandScenePending、ready/uiReady/suspended 身份、admissionRevision、controller Drop 原因。 | Desktop/Desktop/commandScenePending=false 且 admission 偶数、Down revision 相等，且无 fatal 后的新一代混淆。 |
| slot 永久耗尽/旧 route 隔离 | 开启真实计数，输出 occupiedSlots/capacity、downRejected、terminalPublished/recycled 基线差值；异常时列旧 route。 | 有空闲 slot，Down 持续 published，且 controller 已 dequeue；未启用计数的零值不能作为证据。 |
| 画布表面隐藏、命中/owner 层级变化 | RTS/controller 已 accepted，document/history/contentRevision 增长；真实 present attempt+success 和 source alpha；再查主/辅助 HWND 实际 visible/style/rect/owner/topmost/capture。 | 输入未到达时不能把窗口呈现当唯一根因；源像素与输出正常但桌面不可见时才向 Window/DWM 定位。 |
| GPU/USER32/日志调用阻塞 | 同一 PID/TID 的重复线程栈与等待点，最后进入/离开 stage 的时间，真实 Present/Map/ULW 耗时。 | 故障时持续新增的 controller frame 和 stage 完成记录直接反对此时永久卡在该调用；旧控制台末屏不是活性证明。 |

建议在同一条一次性 `freeze-probe` 快照内输出上述便宜状态，并在状态改变或每秒最多一次时写入有界队列。RTS callback 不直接调用窗口服务、写盘/控制台、做恢复或 Reset。将视频现有日志与“Down arrival -> published -> dequeued -> admitted -> model/stored -> actual present attempt”关联，能把一次复现分成明确层次。

现有 Cursor switch 已有 `rts-input event=Down/Up reason=arrival/published/no-decoder/...`（`Draw3.RealtimeStylus.cpp:1529`），Packets 每秒限一次（`1544`）。主会话的Git核对确认它在10月3日已加入，RTS源码此后未变；视频基于此后正常构建时应具备，但仍须核对EXE身份、Cursor开关和完整原始流，不能以滚屏片段缺行证明没有Down。现有日志只能部分分层，不能完整区分实际COM Enabled、本地decoder/lifecycle和collector停投递；最小无恢复动作的补诊断方案见 `rts-lifecycle-followup.md` 第8节。

### 8. Related specs

- `.trellis/workflow.md`：研究持久化、规划与实现分离。
- `.trellis/spec/native-desktop/index.md`、`draw3-integration.md`：唯一 Host/RTS、独立 Draw3 device、Window Service 独占 HWND/样式/显隐；输入 owner 和辅助 selection-only 表面不能混用。
- `.trellis/spec/native-desktop/input-and-ink.md:19`：鼠标 accepted 仅视觉；`21` 产品运行期 RTS 诊断；`427` 起 contact 代次/Quarantine/Closing 合同；`488` 起输入/GPU/USER32 的证据分层。
- `.trellis/spec/native/runtime-and-rendering.md`：RTS producer 与 drawing consumer 边界、control wake、cursor mailbox 和 Present 范围；独立 demo 的 bootstrap 不能代替产品。
- `.trellis/spec/native-desktop/rendering-and-ui.md`：UI3/Setting 共享 RenderPipeline 与 Draw3 的独立所有权。
- `.trellis/spec/native-desktop/cpp-conventions.md`、`errors-logging-and-resources.md`：最小变更、原编码/EOL、线程/COM 生命周期、低频非阻塞输入诊断。

### 9. External references / versions

- 本轮没有使用外部网页或远端仓库，也没有以 Windows/RTS 未公开内部行为作为结论。Win32/RTS/COM API 名称均来自当前产品源码。
- 源码使用 C++20 module 与 D3D11/RTS；实际编译路径由本地 vcxproj 的 ClCompile 条目确认。未用独立 `inkStrokeModelerTest` 的行为或历史 `IdtRts.cpp` 为产品背书。

## Caveats / Not Found

- 没有修改代码或执行动态复现，以上是带验证条件的静态研究，不是已经修复或已经证实的根因。
- 没找到 Settings/Explorer 直接设置 Draw3 pause/admission=true 的常规入口；没有据此排除系统 collector、窗口层级、Owner/捕获或 RTS COM 事件链。
- 视频的几行 cursor trace 不能证明当前产品二进制、RTS contact 或 GPU 失败。`mouseLifecycle` 和空 dirty 的 `success=0` 尤其容易造成误诊。
- 设置 ImGui 与 Window owner 线程的等待环由其它研究负责；本文件没有重复作完整审查。必须用同一故障时段的实际线程活性区分各报告是否同根因。
- 未运行 GUI、构建、测试或 Git 检查；本文件只增加研究资料，产品源码保持只读。
