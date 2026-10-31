# Research: 白板退出状态、工具恢复与绘制准入

- Query: 白板背景/分页已经退出但 Bar 尚未归位，点击屏幕后出现墨迹并显示选择态：逐一核对 Exiting→Inactive 的门、返回场景、工具恢复和 Host 输入/Present 的相对顺序，判断 ab79dcb9 是否改变退出链。
- Scope: internal；当前源码及主会话导出的历史源码/差异，只读调查。
- Date: 2026-10-27
- Active task: `.trellis/tasks/10-27-whiteboard-first-open-visibility`。

## Findings

### 文件与研究边界

| 文件 | 作用 |
| --- | --- |
| `Inkeys/IdtState.cpp` | 白板意图/phase、工具工作区记忆、Draw3 ready 判断、Exiting 所有门禁。 |
| `Inkeys/Inkeys/UI/Whiteboard/Whiteboard.cpp` | 背景 active 与 committed ACK；不深入本次未实施的 token 扩展。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Product.cpp`、`Draw3.Bridge.cpp` | 发布工具与场景及控制唤醒。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp`、`Draw3.Host.h`、`Draw3.PresentationState.h` | 实际工具应用、输入准入、成功 Present 后 runtime/scene ready。 |
| `Draw3.WindowControl.cpp`、`Draw3.MouseInput.h`、`Draw3.DrawingController.cpp` | activation 与输入的区别、鼠标选择态拒绝、Contact 与成功帧的实际处理。 |
| `Inkeys/IdtMain.cpp`、`Inkeys/IdtPlug-in.cpp` | 产品真实绘制通知接线和 PPT 真退出的选择收尾。 |
| `Build/WhiteboardExitInvestigation20261027/history/before-ab79/IdtState.cpp`、`release-20261021a/IdtState.cpp`、两个导出 `.diff` | 主会话只读 Git 导出的历史副本；本研究角色没有执行 Git 操作。 |
| 本任务 `prd.md/design.md/implement.md` 末尾新增退出调查段 | 本轮先调查，不修改产品，不继承上轮 commit 授权。 |

以下代码行号均为本轮读取时当前工作树的一基行号。窗口原生排序和 Bar 实际布局由其他研究者负责，这里只核对它们在状态链中的调用及先后关系。本次没有新的现场采样；前次首开/闪烁数据不得当作这次退出异常的运行事实。

### 1. 关闭请求与返回目标的确定事实

`RequestWhiteboardActive(false)` 在 `IdtState.cpp:994–1015` 捕获原 PPT focus handoff、推进偶数 `whiteboardInputEpoch`、发布 desired=false 并清翻页请求；它本身不把 phase 设为 Inactive，也不还原 Bar。

返回目标没有固定保存为“进入白板前是什么”：`CurrentPrimaryWorkspace:123–128` 每次按 `PptSessionActive()` 选择 Presentation 或 Desktop。因此退出途中 PPT 结束/新开会重新收敛目标。

`WhiteboardTransactionActive:1028–1034` 在 desired=false 但 phase=Exiting 时仍为 true。`PptInputWorkspaceEpoch:1036–1040` 返回 0，PPT 页控/条件工具尾部的业务权限仍关闭；`PptSceneWorkspaceEpoch:1042–1047` 则允许当前偶数 epoch 在 Exiting 发布返回场景。两者是有意分离的权限，不能把前者误称为所有 Draw3 原始输入的全局闸门。

### 2. Active close 到 Inactive 的完整次序

| 当前行号 | 动作/必要条件 | 未完成时留下的状态 |
| --- | --- | --- |
| `2723–2727` | 清翻页等待、取消 capture、收辅助面板、`PublishExpandedLayoutTarget(false)` | 分页已经开始退场，尚不意味着窗口模式/Bar 已恢复。 |
| `2728` | `HideWhiteboardForReturn()` 同步请求 Hide Freeze | 背景先消失；该函数内部 Hide 返回值只用于 focus rebase，不作为本次 close 的早退条件。 |
| `2729–2734` | phase=Exiting；请求主/辅穿透；分页 loading=true；activationAllowed=false | 白板绘制焦点策略关闭，但 Bar 的独立 whiteboardActive 标记还没清。 |
| `2735` | `PublishWhiteboardPrimaryWorkspace(epoch)` | 立即开始返回工作区和工具恢复，早于后续 native 恢复。 |
| `2811–2821` | 持续收 expanded=false；重新算 primary；再次发布返回工具/场景；核对 bridge.workspace 与当前 primary（fatal recovery 可例外） | 若场景目标未匹配，继续 Exiting。 |
| `2822–2823` | 正常路径必须 `Draw3WorkspaceReady(snapshot, primaryWorkspace)`；受保护终态走 release 分支 | ready 不满足就不进入本轮 native 恢复。 |
| `2824–2838` | ProtectedConflict/ManualRetryRequired/Failed 的当前恢复状态先隐藏两画布；fatal 使用专门清理 | 真实隐藏失败继续 Exiting，不能假 ready。 |
| `2842–2843` | 再 Hide 背景，等待 `BackgroundMatchesActive(false)` | 背景看不见并不必然等于 inactive ACK 已消费。 |
| `2845–2850` | 发分页非 loading，释放 Freeze surface ownership，单独恢复辅助 DrawpadPresentation 穿透 | 已经做完部分离场；后续失败不会撤销这些 UI 发布。 |
| `2859` | `SetOverlayFullscreen(false, guard)` | 后续 `&&` 短路；不能直接据现象声称 Shell TRUE/FALSE 振荡。 |
| `2860` | `LeaveWhiteboardWindowMode(guard)` | 失败直接使整个 presentationWindowStateReady=false。原生层的完整模式恢复由窗口报告解释。 |
| `2861` | 仅 Leave 成功后 `SetOverlayTopmost(true, guard)` | Leave 未成功时，不能说此处已经恢复桌面 topmost 政策。 |
| `2862–2869` | 除 fatal 外，将主/辅穿透收敛到 recoveryBlocked 或捕获的 runtime.selectionMode；同时验证当前 mode revision 与 Bridge selection | 工具在同步窗口调用期间改变会使该 guard 拒绝，必须重试最新状态。 |
| `2870–2872` | 只做 focus rebase，然后任一窗口步骤失败就 continue | 不发布 Bar false、不发布 phase Inactive。 |
| `2873–2879` | Freeze whiteboard=false、**Bar whiteboard=false**、重发 PPT COM 可见事实、activationAllowed=false、phase=Inactive | 到这里才表示状态事务收尾。 |
| `2881–2883` | Reconcile 双画布；仅完整离场后消费一次 return focus queue | 这是收尾之后的业务聚焦，不是上述 ready 的条件。 |

`HideWhiteboardForReturn` 定义在 `IdtState.cpp:193–200`：先 PublishActive(false)，再同步 Hide Freeze，再修正 focus handoff。实际背景渲染的 inactive 分支见 `Whiteboard.cpp:147–151,244–248`；它设置 committed=false，但当前实现忽略 Hide 的 false 返回。这是既有 ACK 弱点，**不能仅凭背景已消失断言此次卡在该门，也不应因此扩大本轮调查成 token 重构**。

状态循环在 `IdtState.cpp:2609–2617` 等待 runtime revision 最多 250ms；提前 revision 可以立即推进。所有 desired=false 的 Exiting `continue` 都没有独立重试上限或新 entry 的退避。新的 `WhiteboardEntryAttempt` 仅在 desired=true 的进入/重入分支读取 Ready/Failed（`2631–2647`、`2789–2801`），不会节流实际退出。

因此“背景/分页消失但 Bar 尚未归位”与一次未完成的 Exiting 是一致的源码路径。不过 Bar 自身绘制/折叠通知也可能改变位置，**用户观察到点击后 Bar 移动不等于已经证明 phase 在那时转成 Inactive**。

### 3. 工具实际上恢复得早，Bar 白板标记恢复得晚

`PublishWhiteboardPrimaryWorkspace:1184–1187` 调用带当前 Exiting guard 的 `SwitchWorkspaceStateMode:1106–1182`。该函数在同一短锁事务中：

1. 保存离开的工作区工具，恢复目标工作区独立记忆（`1129–1166`）；目标尚无记忆时默认 Selection（`1130–1139`）。
2. 修改 `stateModeWorkspace` 和 mode revision，发布新的工具/selection（`1160–1173`）。
3. Desktop 同时发布 Workspace；Presentation 则等待完整 target 入口，不制造无身份 Presentation（`1174–1176`）。
4. 锁外 `SyncDraw3State()` 与 `Bar::RefreshToolState()`（`1179–1181`）。

常规白板退出调用没有传 `exitRevision`，所以**不是每次关闭白板都强制切 Selection**。`ApplyExitSelectionLocked:1093–1104` 的选择收尾仅用于显式选择/条件 PPT 真退出等调用。

对比层次应分清：

| 状态 | 写入时点 |
| --- | --- |
| `stateMode`、`stateModeWorkspace` | 第一次 `PublishWhiteboardPrimaryWorkspace` 内已恢复 Desktop/PPT 记忆。 |
| Bridge 的 tool/selection | `PublishDraw3StateLocked:287–304` 同步发布；`Product.cpp:141–157` 保留场景，只覆盖绘图属性。 |
| Draw3 的实际 `WindowController::SelectionMode/ActiveTool` | Host owner 后续 `PumpBridgeState:1619–1724` 消费 revision，`1635` 设置 selection，`1662` 附近设置实际 tool。 |
| runtime.selectionMode | 同一次 Pump 在 `1645–1646` 更新，`1724` 发布 runtime revision。 |
| Bar 独立 `whiteboardActive` | 必须等所有窗口尾部步骤成功后的 `IdtState.cpp:2874`。 |

因此静态上确实存在“state/Bridge 已是 Selection，但绘制 owner 尚未应用、实际仍是 Pen”的异步区间。`Host::PublishState:2759–2768` 更新 Bridge/基础闸并发 ControlWake；它不等待实际应用完成。普通工具发布没有快捷键清空专用的 `shortcutSelectionHandoffPending` 闸（该特殊处理见 `Host.cpp:2873–2876`）。但这些事实**不能证明一次正常的短暂异步区间会持续到用户点击**；ControlWake 的生产消费 `Host.cpp:1826–1836` 本来会主动 Pump，`WindowControl.cpp:672–680` 改 Selection 时也主动请求续帧。

### 4. Exiting 的输入与激活不是同一个门

`SetProductActivationAllowed(false)` 只到 `Host.cpp:2732–2735` / `WindowControl.cpp:692–695` 更新 activationAllowed；`WindowControl.cpp:1931–1934` 用于 WM_MOUSEACTIVATE 返回 MA_NOACTIVATE。**它不是禁止落笔的开关**，不能把 activation=false 解释为 Exiting 绝不会接纳输入。

实际 Host 基础准入 `Host.cpp:807–836` 检查 stopping、commandScenePending、consumerFatal、恢复状态、Whiteboard 冷加载、PPT target/UI ready/suspended，以及从 Presentation 返回的 presented workspace。它没有读取 `WhiteboardPhase`，也不等待 Bar false 或 Leave 窗口模式成功。

`DrawingController.cpp:9393–9402,16493–16505` 在成功 Present 且 viewport 恢复完成后发布 workspace ready；`Host.cpp:1170–1195` 消费为 runtime workspace、清 pending 并重新开放基础输入。因此若返回 Desktop 的记忆工具是 Pen，Draw3 接管帧已成功、恢复状态健康而原生 Leave 仍失败，源码允许 **phase 仍 Exiting 时实际 Desktop 接纳绘制**，只要旧原生样式/可见表面仍把输入送来。`PptInputWorkspaceEpoch()==0` 并不会否定这条原始输入路径。

选择态还须区分输入后端：

- 鼠标兼容后端 `Draw3.MouseInput.h:31–48` 直接拒绝 Selection 或 AdmissionBlocked；实际 Selection 已应用时，不能用它解释“普通鼠标仍画一笔”。
- Controller 的 Down 处理 `DrawingController.cpp:11634–11656` 在 ContactAdmitted 后，只对白板 Selection 明确 Discard；Desktop Selection 依赖窗口穿透/正确输出路由。RTS 发布见 `RealtimeStylus.cpp:1244–1251`，没有普通工具 Selection 条件。若原生完整恢复保留旧不穿透主窗，存在 RTS contact 在 Desktop Selection 仍到达的**条件候选**；本次输入设备与实际样式未知，不能当作此次已证实原因。
- 该候选不要求产品工具真的从 Pen 又变成 Selection：选择态已经恢复但错误窗口仍接触输入，后续穿透/输出修正后用户才看到稳定选择态，也符合描述的一部分。

### 5. 点击为何可能推进：两类解释必须分开

**候选 A：ready 已完成，阻塞在原生窗口尾部。** 工具/CPU 场景提前恢复，背景和分页提前退出，Bar false 等 Leave/Topmost/ClickThrough。窗口研究已经确认 Leave 的 Desktop 分支仍含全局排序/后置条件，失败可走完整模式恢复；点击可能改变原生前台/窗口排列条件，使下一轮原生事务成功。这个候选对持续的任务栏/样式变化解释力较强，但尚无此次 native false stage 或 foreground 转变证据。尤其 activationAllowed 已 false 时普通主画布 WM_MOUSEACTIVATE 返回 MA_NOACTIVATE，不能直接断言点击必然取得前台许可。

**候选 B：绘制/输出交接还欠一次新成功帧。** `Draw3WorkspaceReady:313–330` 要求 firstFrame、目标 workspace、根据 runtime.selectionMode 选择的输出、ready/requested revision 和 presented/content revision；空 Selection 页还要求 auxiliaryFullFrameClean。`Host.cpp:990–1019` 在真实成功 Present 后更新这些值并发布 runtime revision。输入、WM_PAINT 或 WM_ACTIVATE 可产生绘制/全帧唤醒（`WindowControl.cpp:1941–1962`），点击的相关活动可能使待交接帧最终完成，再让 Exiting 接近 native 尾部。仅“点击后恢复”不能证明此前在等待前台许可。

已经真实落笔会收窄判断：稳定的 commandScenePending/Recovery 闸仍关闭时，新的 Down 不应被接纳；但接纳可发生在另一阶段、旧场景或非原生完成之后。此时 Host 的 Present 是否已完成、期望/实际 Selection 是否一致、墨迹属于 Whiteboard 还是 Desktop 均未知，不能由视觉反推唯一时序。

点击通知不负责选择收尾：`IdtMain.cpp:3521–3542` 只将活动/accepted-stroke 通知 Bar；Bar 研究已核对该链只折叠/收面板/刷新。PPT 真退出可在 `IdtPlug-in.cpp:1184–1192` 条件调用 `CompletePresentationExitStateMode`，但其 `primaryOnly` 与 `PptInputWorkspaceEpoch` 在 Exiting 期间会拒绝（`IdtState.cpp:1116–1124,1250–1257`）；不能把旧 PPT 回调随意认作这次 Selection 的来源。

### 6. ab79dcb9 与两个历史基线

使用主会话导出的 `before-ab79-to-ab79.diff`：IdtState 生产改动位于 entry helper、Inactive desired=true 入口、Active 发布后的异步激活 guard，以及 Exiting desired=true 重入。`diff:206–222` 仅更改 Exiting 的重入部分；**desired=false 的实际退出链、工具早恢复、Bar 晚清标记没有被 ab79 的 State 补丁改动**。

因此不能将新反馈直接归为 ab79 新改了 State 的退出顺序；更符合代码事实的是前两项问题解决后，既有退出原生路径和分步提交的剩余问题暴露出来。原生策略改动对退出恢复是否有间接影响由窗口报告说明。

20261021a 导出 `IdtState.cpp:2066–2087,2155–2205` 也先撤分页/Hide 背景、等待 Draw3/背景 ACK，再按 Fullscreen(false)→Leave→Topmost(true)→主穿透，最后 Bar false/Inactive。旧版没有当前 `SwitchWorkspaceStateMode` 工作区工具隔离入口，Active close 直接 PublishProductWorkspace；当前早恢复工具的逻辑已存在于 ab79 父版，不能说它是 ab79 本次新增。基线也没有本次严格通用恢复屏障的全部失败放大机制，单看 State 顺序相似不能推导运行效果相同。

### 7. 可区分候选的离屏验证点（本阶段未执行）

1. 在现有 `--state-mode-production-test` 内调用真实工作区工具入口：保存 Desktop Pen 和 Desktop Selection 两组记忆，进入 Whiteboard，再置 Exiting 并 PublishPrimary；断言工具/Bridge 立即恢复，PPT business input epoch 仍为 0。现有 `IdtState.cpp:2533–2568` 已覆盖 scene/input authority 分离，可在真实生产入口上补充而非测试复制算法。
2. 对真实 `Draw3WorkspaceReady` 分别只改变 requested/ready output、content revision、aux clean；记录哪项 false。在 ready 已全满足时持续注入 Leave=false，证明“等待新 Present”不是该夹具的必要原因，Bar false 必须仍不提交。
3. 反向保持原生步骤成功、只延后一个输出 full-frame 回执；用真实 ready/Present observer 入口推进，证明无需改变 foreground 就可使 ready 从 false→true。没有 HWND 的环境只测试生产纯策略/observer 接口，不能运行 hidden HWND 测试。
4. 对实际 Host/工具应用之间使用受控 seam 延后 Pump：分别断言 stateMode、Bridge、runtime.selectionMode、WindowController selection；普通鼠标 Selection 必须拒绝，不能以最终 UI 图标代替实际 Down 时工具状态。若需要修复新增 seam，应限于现有私有 helper，不扩大公共 Host 架构。
5. 原生失败后 phase 仍 Exiting、Host Desktop ready、记忆 Pen 的组合应明确决定输入政策；若后续方案要禁止过渡落笔，必须将窗口交接身份纳入真实 Host 准入，而不是只改 activationAllowed 或 PPT business epoch。
6. 不先选择上述方案中的一个作为产品补丁。本次缺少退出现场的具体阶段、输入后端和返回 Desktop/PPT 的确认，调查结论只能对照明确条件给出最小修复边界。

## Related Specs

- `.trellis/workflow.md`：研究证据和结论落盘，先调查再确定实施范围。
- `.trellis/spec/native-desktop/whiteboard.md`：工作区工具隔离、真实首帧/背景交接、单次激活、Bar 返回基线及禁止 GUI 时验证边界。
- `.trellis/spec/native-desktop/draw3-integration.md:57–59,100–106`：Window Service 独占原生显隐/样式，Host 独占单输入/场景，辅助表面固定穿透。
- `.trellis/spec/native-desktop/rendering-and-ui.md`：窗口 owner 线程、受管原生事务与恢复、无窗口测试不能代替系统实机。

## External References

本轮无外部检索；以上事实均来自实际产品源码和主会话导出的历史源码，不根据记忆补全 USER32 前台权限行为。

## Caveats / Not Found

- 没有启动 GUI、运行时进程/窗口/PDB 采样、编译、测试、Git 操作或产品修改；只新增本报告。
- 本次具体卡在 ready、背景 ACK、Leave、Topmost、ClickThrough 哪一门未捕获；“阶段符合”不等于“此次已证实”。
- 点击后出现 Selection 可以是既存工具状态终于被呈现，也可以是异步工具/输出/原生窗口交接结束；没有证据证明存在点击触发的主动 Selection 业务命令。
- RTS/错误穿透路径是条件候选，鼠标 fallback 在实际 Selection 下明确拒绝；不能把两者混为同一个已确认问题。
- 历史副本来自主会话，当前行号与导出文件行号分别引用；没有使用上次首开现场作为这次退出现场。
