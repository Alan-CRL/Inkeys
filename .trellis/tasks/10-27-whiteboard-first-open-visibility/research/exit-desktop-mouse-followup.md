# Research: 返回 Desktop 后物理鼠标的输入路径与 Selection 拒绝边界

- Query: 用户确认返回 Desktop，第一笔来自物理鼠标。核对两种真实鼠标路径、工具发布/应用、退出 ready 与原生交接，并判断把白板专用 Selection Down 拒绝扩为所有工作区是否为安全的最小修改。
- Scope: internal；源码和已有生产无窗口测试只读调查。
- Date: 2026-10-27
- Active task: `.trellis/tasks/10-27-whiteboard-first-open-visibility`。

## Findings

### 1. 结论与范围

1. **物理鼠标不能排除 RTS 路径。** 产品先选 RTS，失败才启用 Win32Mouse；RTS 显式识别 `TDK_Mouse` 并发布 MouseLeft/MouseRight contact。上一报告中“鼠标 fallback 在实际 Selection 下拒绝”正确，但不能推广成所有物理鼠标都经过那个拒绝点。
2. **Controller 当前只对白板 Selection 拒绝新 Down，Desktop/PPT 的相同模式没有该保护。** 结合当前 Selection=电脑操作/穿透、没有画布选区工具、产品平移未启用等明确语义，将现有拒绝扩到所有工作区是有根据的最小一致性修复，不需要新 phase/Host bool/协议。
3. 此修改能堵住“Selection 已应用，错误原生表面仍把 RTS Mouse contact 送来”的误落笔；**不能独自修好原生窗口仍拦截点击、退出排序失败或 Bar 晚提交**。这些继续由窗口退出收敛的修复处理。
4. state/Bridge Selection 先发布、实际 WindowController 后应用的短暂异步区间确实存在；当前没有证据证明本次在这个区间持续等待用户点击。不要为了尚未证实的持续失配另加交接协议。

### 2. 文件与可复查位置

| 文件 | 当前一基行号与作用 |
| --- | --- |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp` | `2203–2236` 后端互斥选择；`1635–1667` 实际工具应用；`807–836` 基础准入；`2759–2768` 工具发布及唤醒。 |
| `Draw3.RealtimeStylus.cppm`、`Draw3.RealtimeStylus.cpp` | module `:29` 明确支持 Touch/Pen/Mouse；实现 `:926–930` TDK_Mouse，`:1237–1251` 左右键及 contact 发布。 |
| `Draw3.WindowControl.cpp` | `1559–1576` Win32 compatibility 消息路由；`494–524` 生产 MouseInput 调用；`2157–2186` RTS 模式 WM_MOUSE 只更新光标；`667–680` 实际 Selection setter/getter。 |
| `Draw3.MouseInput.h` | `31–48` 拒绝 Selection/AdmissionBlocked 的 Win32 producer。 |
| `Draw3.DrawingController.cpp` | `11634–11656` 新 Down 的页准入及白板专用 Selection 拒绝；`10319` 后是实际 initializeStroke；`10503–10514` 临时橡皮；`12107` 选择态取消擦除 hover。 |
| `Draw3.WindowControl.cppm` | `30` 产品画布导航关闭；`33–44` DrawingTool 仅墨迹/擦除/图形，没有选区工具。 |
| `Inkeys/IdtState.cpp` | `1106–1187` 早恢复 Desktop 工具/Bridge；`313–330` 较弱的退出 ready；`2811–2879` native 步骤与最后 Bar/phase 提交。 |
| `Draw3.PresentationState.h` | `17–33` 已有 `DrawpadPresentationReady(desired,runtime)`，会比较期望/实际 Selection 及 pending。 |
| `InkeysHeadlessTests/draw3_mouse_input_tests.cpp` | `94–136` 真实 MouseInput+Coordinator 的 Selection/准入/隔离回归，`:223` 现有入口。 |
| `InkeysHeadlessTests/draw3_bridge_tests.cpp` | `614–656` 实际 ready helper 对 stale mode/output 的测试。 |
| `Inkeys/IdtMain.cpp` | `1447–1455` 状态生产 CLI 在 GUI 初始化前退出。 |

### 3. 实际物理鼠标不只有 Win32Mouse 一条路

产品启动 `Host.cpp:2203–2236`：

```text
RTS Initialize 成功 → RtsExtended / RtsBasicXY → EnableContactPublication
RTS Initialize 失败并确认停产 → Start Win32Mouse compatibility producer
```

两者互斥，不应把它们描述为同时生产两笔。Win32Mouse 模式的 `WM_LBUTTONDOWN` 到 `HandleCompatibilityMouse:494–524`，传入**实际** `WindowController::SelectionMode()`；`MouseInputState::Down:31–48` 在 true 时拒绝并隔离按键直到 Up。这条路径在实际 Selection=true 时不能生成正常新笔。

但 RTS decoder 的 `BuildContextDecoder:926–930` 明确将 `TDK_Mouse` 映射为 MouseLeft；`StylusDown:1237–1251` 根据 `GetAsyncKeyState` 区分左右键并调用真实 Coordinator.PublishDown。这里没有普通工具 Selection 判断。RTS 模式下的 WM_MOUSE 路径只维护 cursor sample（`WindowControl.cpp:2157–2186`），不会重新调用 compatibility MouseInput.Down。

所以用户确认“硬件鼠标”只排除了把事件完全解释为 pen/touch 的猜测，**没有确认这台进程当时选的是 Win32Mouse**。现有代码足以保留“RTS Mouse contact 穿过了 Desktop Selection 缺口”的条件候选，无须再次读取运行中进程来勉强确定后端。

### 4. Selection 的实际语义支持统一拒绝新绘制 Down

生产 `IdtState.cpp:301–302` 将 `IsComputerInteractionMode(StateModeSelect)` 发布为 selectionMode；规范 `draw3-integration.md:356` 明确包括 `IdtSelection` 与 `IdtClickThrough`，是电脑操作/穿透模式，不能根据实际 WS_EX_TRANSPARENT 反推模式。

`DrawingController.cpp:11634–11656` 当前流程是：

```text
ContactAdmitted → 仅 Whiteboard && Selection 时 DiscardUntilTerminal
→ 冷加载/Presentation 加载抑制 → initializeStroke
```

Desktop/PPT 没有一个合法 Selection contact 专用消费者被此分支略过：放行后就是 `initializeStroke:10319`，最终创建笔划并通知 accepted stroke，而不是系统桌面选区或某个 Inkeys 选区手势。DrawingTool 枚举 `WindowControl.cppm:33–44` 没有 Selection/Lasso；白板“拖动”注释也明确尚不启用平移。Desktop/PPT 选择模式下的墨迹只是既有文档的辅助 ULW 呈现（`PresentationState.h:17–33,91–99` 及规范），不构成接受新笔划的理由。

最小边界因此是保留原有位置和 `DiscardUntilTerminal(handle)` 行为，仅去掉 `activeWorkspace == Whiteboard` 限制。无需加入 Exiting、desired、deadline 或新 bool。它是把已经存在的选择语义补齐到三工作区，窗口穿透继续负责把实际点击交给下层应用，而 Controller 负责不把误路由输入变成墨迹。

对具体风险逐项核对：

| 可能受影响行为 | 源码结论及边界 |
| --- | --- |
| 系统桌面选区/点击 | 不属于 Controller.initializeStroke；丢弃送错的 contact 不负责重新向系统转发，仍须修正 HWND 显隐/穿透。不能宣称只加拒绝就恢复所有系统交互。 |
| Bar/PageControl/菜单选择手势 | 各自 HWND 与 UI 会话独立；Controller 只绑定主 Drawpad。WindowController 的 contact/menu 通知在 Controller 接纳前发生，扩大拒绝不删除 UI 消息入口。 |
| 画布平移 | 当前 `kCanvasNavigationProductIntegrationEnabled=false`（WindowControl.cppm:30）；Touch initializeStroke 中也用该门（Controller:10383）。不存在本轮应保留的已启用 Selection 平移手势。未来若产品启用导航应显式区分导航与墨迹，不能继续用 Selection 漏洞承载。 |
| 快捷键恢复绘制 | `ChangeStateModeForShortcutIfRevision` 修改真实工具并经 SyncDraw3State 发布；实际 Selection=false 后的新 Down 仍可进入。Selection 时已经按下的旧 contact 继续隔离到 Up，不能切工具后补画旧 Down。 |
| Clear/Undo/Redo/翻页/ShortcutFence | 它们走独立 CanvasCommand，而该判断只在非空 contact record 的新 Down 路径；不应移动到控制唤醒/命令处理之前。 |
| 鼠标右键临时橡皮 | `Controller:10514` 已要求 `!window_.SelectionMode()`，Selection 下本就不应擦除；非 Selection 行为不变。 |
| RTS 笔尾橡皮 | contact 初始化的 inverted override 当前没有独立 Selection 条件，但擦除 hover `:12107` 和已有擦除更新 `:10090` 已明确 Selection 取消。没有发现“电脑操作 Selection 仍允许笔尾修改文档”的产品合同/测试。统一拒绝会堵住这种误投，而非更改非 Selection 的笔尾擦除；应补其回归。 |
| 已接纳笔划的 Move/Up、批次工具锁存 | 新 Down 分支不重写 active runtime，原有结束/烘干和边界隔离继续处理。不要为此新增一次性清空全部 active 或改已接纳笔划工具。 |

### 5. state/Bridge 已 Selection 与实际鼠标判据的区别

`SwitchWorkspaceStateMode:1173–1176` 先发布新工具/场景；`Host::PublishState:2759–2768` 只更新 Bridge、刷新基础闸、请求 ControlWake。Host 后续 `PumpBridgeState:1635` 才调用 `window.SetSelectionMode(state.selectionMode)`，`:1667` 才应用 ActiveTool，并在 `1724` 发布变化。

因此“state/Bridge=Selection、actual Selection=false”存在一个异步区间，Win32Mouse.Down 只看 actual，期间若其他准入全开放，理论上会接受按下。实际 ControlWake 的 `ConsumeBridge:1826–1836` 会主动应用，`SetSelectionMode:672–680` 也主动请求渲染/光标刷新；**没有证据证明此区间会卡住直至用户点击**。本轮不应因该可能性新增 Host 状态或协议。

另一个容易误判的点：`Bridge.tool`/`WindowController::ActiveTool` 在 Selection 下仍可能保留 Pen；Pen 并不等于正在绘制。真正拒绝鼠标的字段是 selectionMode/AdmissionBlocked，测试和诊断必须区分，不能用 tool 枚举值代替实际 Selection。

如果 Desktop 记忆本来是 Pen，则早恢复后实际 Pen 在 Exiting 落笔是当前模型允许的结果；退出链本身不会强制 Selection。仅凭稍后 Bar 呈现选择态不能证明其中发生新的 Selection 命令，也不应为了堵这一假设一律按 phase 关闭全部输入。

### 6. 为什么 ready 不保证 native 交接完成

`IdtState.cpp:313–330` 的 Draw3WorkspaceReady 只核对 runtime.firstFrame/workspace、以 runtime.selectionMode 计算的输出及 content/output revision，外加空 Selection clean。它不接收当前 desired Selection、不读原生 mode/可见性、也不等 Bar false。

退出调用 `:2823` 明确在 Fullscreen/Leave/Topmost/穿透（`:2859–2869`）之前；native 任一步失败，`:2872` continue，Bar false/Inactive 在 `:2874–2879` 尚未发布。因此 Draw3 可就绪、工具可恢复，native 仍未完成。这种先准备画面再切显隐有必要，但不应该反复通过全局层级条件重建整个模式。

现有更强的 `DrawpadPresentationReady(desired,runtime)`（PresentationState.h:17–33）已经比较期望/实际 Selection、commandScenePending、输出/内容等，且有真实纯测试。不必再发明新状态判断。若修复需要加强退出帧核对，可复用这个现有 predicate 并保留空页 clean 要求；它仍只表示画面可接管，不能把 native 后置条件伪装为已经完成。

### 7. 最小离屏验证与修复建议

本阶段未运行任何测试，下面只列可执行边界，避免用测试复制 Host/Controller 整套算法：

- 复用 `RunDraw3MouseInputTests` 的真实 MouseInput+ContactInputCoordinator：实际 Selection=true 拒绝、false 接受；Selection 拒绝后的同次 held-key 不可在模式 false 后补画，Up 后新的 Down 可接纳。现有测试 `94–136` 已有这些核心行为。
- 在现有 `--state-mode-production-test` 调用真实工作区工具入口，分别预存 Desktop Selection/Pen，退出发布后检查 state/Bridge 恢复值，避免把“退出强制选择”写进错误预期。该 CLI 在 IdtMain:1447–1455 GUI 初始化前返回；它不能证明运行中的 Host actual 已应用。
- 统一 Selection 拒绝的新增回归应调用真实生产拒绝步骤/Controller 输入入口。若 Controller 全回路离不开 HWND，可把现有这一步“检查 Selection、DiscardUntilTerminal、拒绝初始化”提成最小私有生产函数，用真实 Coordinator 注入 MouseLeft/MouseRight/PenTail/Touch，在三工作区确认不初始化 stroke、Move/Up 正确退休、模式恢复后新 Down 才可接纳；不新增阶段状态、不做一份测试专用 initializeStroke 算法。
- 用已有 `DrawpadPresentationReady` 测试对比 runtime Desktop Pen / desired Desktop Selection：强 predicate 必须拒绝，现有 `draw3_bridge_tests:637–654` 已覆盖这种模式失配。若测试退出的较弱 private helper，在既有状态 CLI 直接调用；不要仅复制相同公式。
- 没有现成的无 HWND 完整 Host.PumpBridgeState 运行入口。仅人工把 MouseInput 的 selection 参数写成 false，不能宣称证明了产品“已 Selection 却仍 Pen”的持续故障。无需为本次调查另外建立复杂 Host 模型。

按用户希望简化切换的偏好，建议修复边界收敛为：**原生退出采用能够完成显隐交接的内部合同，去掉无关的全局排序阻塞；Controller 按现有 Selection 真值统一拒绝所有工作区的新绘制 Down。** 保留已有 ready、owner/代际、安全回滚和工作区工具记忆，不引入 phase 输入 bool、背景 token 或新的公共协议。是否还需处理工具应用短暂异步，只在精确回归证明后另定。

### 8. 已定位的同文件生产无窗口接缝（补充）

最适合只改 `Draw3.DrawingController.cpp` 的既有入口是 `RunDraw3ControlFenceProductionProbe:4855–4923`，不是新增测试 EXE 或启动 hidden Host：

- 已在同文件创建真实 `ContactInputCoordinator` 和未 Attach 的真实 `WindowController`（`:4866–4867`）。
- 用真实 `PublishDown/PublishUp`、`TryReserveCommandWake/PublishReservedCommandWake`，经生产共用 `DrainIngressBatch:100–116` 消费真正的 Control marker 与 Down（`:4909,4918`），验证 CanvasCommand 之前/之后的 FIFO。
- 产品 CLI 为 `Inkeys.exe --draw3-control-fence-test`，在 `IdtMain.cpp:1571–1573` 直接调用并返回，位于配置、单实例和产品 HWND 初始化前；无 `INKEYS_ENABLE_UI3_PRESENTATION_HARNESS` 或 `DRAW3_TESTING` 宏包裹。既有导出见 `DrawingController.cppm:64` 附近，不必修改 module 接口或 IdtMain。
- 附带的 `CheckTailInputDiagnosticContract:2166`、`RunEraserIdleWakeProductionProbe:4655–4853` 只使用 Coordinator、CPU RuntimeStroke、同步事件/线程。整个 Controller 文件没有 CreateWindow/AttachExternal/StartProduct 调用；未 Attach 的 WindowController 析构在 `WindowControl.cpp:385–403` 的 wasAttached=false 路径结束，不创建窗口。

但现成 Probe **没有**调用 `Run` 内部 `processCommand:11620` 的整段 lambda；没有现成“白板 Selection 完整 Run 无窗口 harness”。最小可信接线是把当前的 Selection→DiscardUntilTerminal 操作提到同文件私有函数，生产 Run 的真实新 Down 与上述 Probe 均调用它。Probe 继续经真实 DrainIngressBatch 取得 ContactHandle，再调用该共享操作，以成功初始化回调/计数为对照，覆盖 MouseLeft/MouseRight/Touch/Pen（含倒转标记）、三场景的 Selection 与非 Selection、丢弃后 Move/Up、切回绘制后新 Down，以及原 ControlFence 顺序。

这与只测试一个返回 bool 的 predicate 不同：真实队列、实际路由隔离和终态释放都会执行；也没有在测试中重写生产拒绝算法。测试说明仍应准确称“共用新 Down 拒绝操作与 ingress 批次”，不能称已执行完整渲染 Run、真实 RTS COM 或 Win32 消息分发。本阶段只定位接缝，未执行该 CLI 或修改 Controller。

## Related Specs

- `.trellis/spec/native-desktop/draw3-integration.md:356–368,398–406`：Selection 的电脑操作语义、actual/runtime 输出回执与显隐交接。
- `.trellis/spec/native-desktop/input-and-ink.md:133,404–414`：RTS/Win32Mouse 能力边界、临时擦除入口；不能把物理鼠标等同 fallback 后端。
- `.trellis/spec/native-desktop/keyboard-shortcuts.md:44,51–55`：Selection/ClickThrough 快捷键归属、独立 CanvasCommand 与已有短期租约；本轮不复用租约新增状态机。
- `.trellis/spec/native-desktop/whiteboard.md`、本任务 PRD/design/implement 后续调查段：先调查，禁止 GUI/运行时采样，不扩大无证据协议。

## External References

本轮未检索外部资料；鼠标 RTS 支持、互斥及 Selection 判断均直接引用仓库生产源码，没有推断用户进程当时实际后端。

## Caveats / Not Found

- 只新增本报告；没有改产品/已有报告、运行 GUI/采样/编译/测试或 Git 操作。
- 用户确认 Desktop+物理鼠标，尚未确认当时 RTS/Win32Mouse 后端、Desktop 记忆工具、实际 Selection、首失败 native stage。两条鼠标路径只能按其明确条件分析。
- 统一 Selection Down 拒绝有独立明确语义依据；不等于已证明此次唯一根因，也不能修复拦截下层点击的 HWND 本身。
- 现有代码/测试未发现必须在 Selection 下接纳的选区、菜单、快捷键或橡皮绘制手势；未来导航功能另需显式入口，不能据此删除未接纳输入的正常终态清理。
