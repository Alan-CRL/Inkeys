# Research: 外部窗口介入后的 Draw3 呈现与 HWND 交接深审

- Query: 除已验证 F1/F2/F3 外，Settings、Explorer、窗口切换/显隐是否能通过 Draw3 output target、窗口事务、共享 RenderPipeline 或同步回调形成普通入口可达的持续画布冻结。
- Scope: internal；生产源码只读，定向复核已有审计没有闭合的等待边；不构建、不运行 GUI、不修复。
- Date: 2026-10-15（Asia/Shanghai）
- Task: `.trellis/tasks/10-14-canvas-freeze-investigation`

## Findings

### 1. 结论与证据等级

在本次负责的 **Draw3 呈现、目标交接、窗口显隐与 RenderPipeline 边界** 内，没有新增一条已经闭合、可由普通 Settings / Explorer 顺序必然触发的永久死锁。不能把真实存在的 `Present1` / `Map` / `WaitForCommitCompletion` 外部调用边界直接升级成新根因，也没有编写“让这些 API 永远不返回”的桩来冒充自然复现。

本次收窄了三个容易误判的方向：

1. **选择/绘制往返不等于 DComp 重建。** 普通切换保持同一套 presenter、device、swap chain，只切输出目标与全帧请求；因此重建清理中的 DComp wait 不是每次 Settings / Explorer 往返的必经步骤。
2. **双窗隐藏失败不是没有重试。** HWND 成对事务失败有双隐藏保护、返回 false 和状态线程重试。要证明长期留在空白，需要继续证明持续失败、状态线程被阻塞，或目标全帧永远不 ready。
3. **普通 ControlWake 并未因在帧中更新 selection 而被直接吞掉。** 消费前先 acknowledge，`SetSelectionMode` / `SetActiveTool` 再发送唤醒；下一轮可读取新目标。已知 F3 的无唤醒 Move 不应复制成这个工具切换路径的新缺陷。

另找到一条需要保留的具体同步依赖：**绘制线程在特定显示上下文回调内可以进入 `SyncDraw3State()`，进而等待 Setting/overlay 窗口事务。** 该入口比泛称“GPU 可能卡住”具体，但目前缺少使窗口 owner 永久等待绘制线程的反向边，不能记作新确认缺陷。

### 2. 普通工具切换的实际呈现交接

| 阶段 | 生产代码 | 对冻结判断的意义 |
| --- | --- | --- |
| 发布业务状态 | `Inkeys/IdtState.cpp:723–746`：Setting owner 应用后才 `PublishDraw3State()`，随后 reconcile | 按钮意图和绘制侧接收到状态必须分开；前面的同步 owner 事务可成为等待点，但仅有等待点不是死锁 |
| 消费状态 | `Draw3.Host.cpp:1243–1323`：`PumpBridgeState()` 应用 selection/tool，再发布 runtime revision | 确认实际消费 revision 才能声称工具切换到了画布 |
| 消费唤醒 | `Draw3.DrawingController.cpp:9823–9833`：先 `AcknowledgeControlWake()`，再调用 `observer.controlWake`；Host `:1416–1425` 调用 PumpBridgeState | pending 清除先于状态应用；回调中的新请求可以生成下一次 General marker |
| 后续唤醒 | `Draw3.WindowControl.cpp:431–449`：SetActiveTool 请求 cursor render + ControlWake；SetSelectionMode 改变时请求 ControlWake | 排除在帧中才更新 selection 后直接无后续唤醒的简易推断 |
| 目标切换 | `Draw3.DrawingController.cpp:12608–12621`；`Draw3.TransparentPresentation.cpp:1145–1162` | 仅改 requested target/revision，不释放 presenter，不重建 HWND |
| 完整帧 | `Draw3.DrawingController.cpp:12661`：outputTargetChanged 强制 full present；`:14244` 扩至整画布 | 新 target 不能只靠一个旧局部脏区被宣称 ready |
| 回执 | `Draw3.Host.cpp:692–732`：成功 full present 才更新 ready output/revision，随后发布 runtime revision | present return 与可见性应用的证据需分别记录 |
| ready gate | `Draw3.PresentationState.h:17–31` | 同时核对工作区、selection、目标、output revision、content revision、command pending；不存在一个只看 UI 工具的简化 gate |
| 成对显示 | `IdtState.cpp:458–637`；`Window.cpp:1974–2080` | ready 前保留旧表面；ready 后 primary / presentation / hidden 三选一 |

ControlWake 自身的入队失败也不能简单判为永久漏唤醒：`Draw3.ContactInput.cpp:368–397` 有 fallback marker 和 SignalWake。这里未重做输入代理负责的所有 ingress/terminal 证明，只核对本条窗口工具切换链没有明显缺失通知。

### 3. DComp 清理不是普通窗口切换必经链

`TransparentPresentationController::SetOutputTarget()` 不调用 `ReleaseAttempt()`、`TryInitialize()` 或 `ConfigureWindow()`。实际 Present 路由见 `Draw3.TransparentPresentation.cpp:1256–1280`：

- SelectionUlw：对长期存在的 selection presenter 提交最终 backbuffer。
- Primary + ULW 后端：对 primary presenter 做 CPU readback 与 ULW。
- Primary + DComp：调用原 swap chain 的 `Present1`。

`ReleaseAttempt()` 位于 `:806–811`，调用 `ResetPresenters()`、renderer 资源释放和 swap chain reset；DComp reset 的 `WaitForCommitCompletion()` 位于 `:633`。它被初始化尝试、Shutdown 与实际 runtime recovery 触发，普通 selection toggle 不经过这里。故新 GUI 实验必须先证明发生了 runtime failure/recovery，才能用此 wait 解释此次停顿。

Draw3 创建自己的 hardware-first/WARP-fallback 设备。设置与 Bar 共享另一套 UI RenderPipeline WARP epoch，规范及 `Draw3.Host.cpp` 初始化、`RenderPipeline.cpp` epoch 代码相符。不能因为两者均使用 D3D11，就假设两边持有同一个 immediate context 锁。

保留的外部边界是实际 `Present1`、ULW staging `Map(READ)`、`UpdateLayeredWindowIndirect`、设备恢复时的 DComp cleanup。它们没有给应用提供普通错误返回之外的全局活性证明。**需要连续线程栈落在同一调用、最后进入/退出序号及 HWND 事务阶段，才可升级故障位置。** 此结论不是排除驱动或 Windows 内部等待。

### 4. HWND 显隐失败、系统隐藏与状态线程的恢复边界

`Window.cpp:1974–2080` 的 `ApplyDrawpadSurfaceVisibility()`：

- 检查两 HWND 有效；撤下主画布前仅释放其捕获。
- 用 `BeginDeferWindowPos/DeferWindowPos/EndDeferWindowPos` 成对应用，无激活、无移动/缩放/改 Z 序。
- 即使 EndDefer 返回成功，也检查两 HWND 实际可见性。
- 批量失败后尝试顺序隐藏两窗、显示唯一目标。
- 仍失败则双隐藏并返回 false；退出期禁止重新显示。

`IdtState.cpp:572–594` 接到失败会设置 `draw3PresentationRetryPending=true`；`StateMonitoring():1754–1772` 在 runtime revision 改变或 retry pending 时再次 reconcile，正常 cadence 最多 250ms。目标不 ready 同样保持 retry pending（`:535–538`）。因此不是一次失败就永久放弃的代码。

更窄的待证边界：**系统在应用成功交接之后额外改变 HWND 可见性，并不直接修改上述 retry 标志。** `Draw3.WindowControl.cpp:1526–1528` 的 WM_SHOWWINDOW/WM_ACTIVATE 仅对 gpuTransparent 请求全帧；StateMonitoring 不是每 250ms 无条件重读所有 HWND。全帧如果没有改变 output/content readiness，Host 的 `runtimeChanged` 也可能为 false。

这意味着“被系统额外隐藏后、原工具不变、没有其它 revision 变化”值得实际测试；但本轮没有证明正常 Explorer/Settings 会独立隐藏主 HWND，也没有证明工具切换后仍无法恢复。主栏在同一 owner 链上，系统整体隐藏往往也会影响 Bar。**不能用外部强行 `ShowWindow(drawpad, SW_HIDE)` 作为自然 bug 的证据。** 普通 Pen/Selection 往返会显式调用 reconcile，这也是对此假说的重要反证。

### 5. 新收窄的绘制线程 → 窗口同步入口

这条依赖在纯“显示线程/绘制线程互不影响”的概括中容易被漏掉：

1. `Draw3.Host.cpp:2153–2163` 的 ForwardMessage 在 Drawpad 收到 WM_WINDOWPOSCHANGED、WM_DPICHANGED、WM_DISPLAYCHANGE 时设置 `displayScaleDirty` 并唤醒。
2. `:1126–1240` 的 PumpDisplayScale 在绘制线程消费 dirty，读显示快照、监视器和 client bounds。displayMutex 在读取快照后就释放。
3. 只有 `!penWidthContextPrepared`、monitor 身份改变或 `EquivalentContext` 变化时（`:1230–1239`）才调用产品 `penWidthContextChanged`。
4. `IdtMain.cpp:3242–3255` 的 callback 先调用 `ApplyPenWidthContext`；当返回 false 时直接调用 `SyncDraw3State()`。
5. `IdtState.cpp:931–942`：ApplyPenWidthContext false 表示业务 context 已等价，true 分支只在释放 stateMode 锁后发布 Draw3 state。
6. SyncDraw3State 回退路径则有 Setting owner 同步与 presentation reconcile；`settingOwnerApplyMutex` 跨 Window Service 提交，`Window.cpp:1376–1397` 普通 Submit 等 future 没有 deadline。

**精确触发例子（待 GUI 验证）**：同规格双屏间目标 monitor 身份变化，但业务 width/height/DPI context 等价，Host 触发 callback、业务 Apply 返回 false，绘制线程走完整 Sync。启动首轮也可走此回退，但用户已经画过后的普通窗口覆盖不必触发它。

目前没有找到窗口 owner 等待该绘制线程所持锁的反向边：

- displayMutex、stateModeTransitionMutex 均不跨此 callback 的窗口等待。
- `PublishDraw3State` 的 productCallMutex 不跨后续 Reconcile；StopProduct 也已经在 Host.Stop 前释放 productCallMutex。
- `PrepareSettingZOrder()`（Window.cpp:477–506）明确禁止 Setting owner 同步等待 overlay 根抬升。
- `ObserveDrawingActivity`（Host.cpp:1092）产品回调是 `Bar.Interaction.cpp:7460–7478` 的计数与 PostMessage，没有同步等待 Bar HWND。

所以应保存为具体待取证入口，不能据此命名“第四个已确认死锁”。

### 6. UI RenderPipeline 与 Settings 的可证伪约束

UI scheduler `RenderPipeline.cpp:813–856` 在 callbackMutex 内复制 callback/记 active 计数，然后释放锁才执行 callback。它不是持 callbackMutex 执行整帧；回调中正常 Request 不因此形成自锁。idle 的 ResetEvent 后也再读取请求（`:707–733`），不存在仅凭手动复位事件就能推出的漏请求。

Settings `Setting.cpp:8765–8769` 持 settingImguiMutex 恢复 coroutine；其 `:8644` Present 的确发生在这个互斥期间。窗口过程 `:610–617` 在可见时也获取该 mutex。**这是一组真实风险边界，但要闭合为死锁，仍须证明该次 DXGI/Win32 调用同步等到了同一 HWND 线程。** 本轮没有新的系统调用证据，因此不重复把通用 DXGI 警告升级为已确认新问题。

若完整新点击能够让 Settings 导航/内容持续更新，这支持共享 UI render callback 仍前进，对“当前整个共享 RenderPipeline 停在 Settings Present 内”构成直接反证。只看窗口还在屏幕上、按钮旧高亮或可拖动框架不够。若只有 Bar 输入状态似乎能变而 Settings 内容完全空白，则不能用该现象排除 shared UI wait。

### 7. 提交给主会话的普通 GUI 动作矩阵

这些建议不包含暂停任意线程、故意阻塞 API、外部隐藏内部 HWND 或造永久占锁。主会话决定并执行；本研究者未运行。

| 动作 | 主要区分项 | 必须保留的证据 |
| --- | --- | --- |
| 有墨迹的 Pen → Selection → Pen，设置隐藏/可见分别执行 | 主 DComp/ULW 与辅助 ULW 交接 | 两 HWND visible、owner、exstyle，requested/ready output revision，full-present 回执 |
| 空画布同样切换，并在 Settings 中点击不同导航页 | Hidden 分支与真实 Settings 活性 | fresh frame/导航变化、回到 Pen 后新真实笔迹 |
| Explorer 普通打开/切换/关闭，配合 Alt+Tab | 系统焦点转移，非本应用模态暂停 | foreground/focus/capture、RTS Down/Up、绘制帧是否继续 |
| Win+D 后返回、Settings 最小化/还原 | 系统隐藏 owned popup 与应用显隐一致性 | Bar 和两 Drawpad 的实际 visible/owner，不用外部 ShowWindow 干预 |
| 若现有环境有同规格双屏：监视器目标迁移后切工具 | 上述 PumpDisplayScale → Sync 回退 | callback 入口、mode/bridge revision、Window submit/execute 起止与线程栈 |

故障后的恢复试验应按“新落笔 → 工具往返 → Settings 新导航 → 窗口切回”分别留证。只有移动系统光标、旧笔迹出现或按钮高亮不能证明 RTS、消费者、输出目标全部恢复。诊断必须避开 F1 的同步 stdout。

## Files Found

| 文件 | 本次用途 |
| --- | --- |
| `Inkeys/IdtState.cpp` | ready gate、双窗 reconcile、状态线程 cadence、显示上下文业务回退 |
| `Inkeys/IdtMain.cpp` | 产品 HostRuntimeCallbacks 与实际 penWidth 同步入口 |
| `Inkeys/Inkeys/Window/Window.cpp` | owner 线程命令、双窗显隐事务、Setting Z 序防重入 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp` | 状态消费、输出回执、显示快照回调与消息转发 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Product.cpp` | productCallMutex 所有权及 Stop 排空边界 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.PresentationState.h` | HWND 交接的完整 readiness 谓词 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cpp` | output target 只切路由，DComp reset 与外部 Present 边界 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` | ControlWake acknowledge、目标变更与 full-frame、idle 分支 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp` | 显隐/激活消息、控制唤醒与 selection/tool setter |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.cpp` | 本条控制唤醒路径的入队 fallback |
| `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cpp` | callback 锁外执行与 idle 请求检查 |
| `Inkeys/Inkeys/UI/Setting/Setting.cpp` | ImGui 锁、Present、窗口消息处理 |
| `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp`、`Bar.Main.cpp`、`Bar.Button.cpp` | 绘制活动回调、内容/历史通知、锁外轻量更新反证 |

## Related Specs and External References

- `.trellis/workflow.md` 与 `.agents/skills/trellis-brainstorm/SKILL.md`：当前仅规划研究，没有实施修复。
- `.trellis/spec/native-desktop/index.md`、`rendering-and-ui.md`、`draw3-integration.md`：唯一 Host、双表面所有权、独立 D3D 设备、Settings 的共享 UI epoch、证据等级。
- 既有 `window-permanent-stall-audit.md`、`render-permanent-stall-audit.md`、`no-console-settings-followup.md`：本次只补充交接内部路径与反证，不把旧候选重新计为新发现。
- 本轮无新增外部 API 行为断言、文献或依赖版本；所有结论基于上述当前生产源码。没有将一般 DXGI 同步消息风险当成特定用户步骤的必现事实。

## Caveats / Not Found

- 没有本研究者执行的 GUI、编译、真实死锁复现或新线程栈；主会话的后续实验须独立记录结果，不由本报告代填。
- 未确认新的普通窗口入口可触发的闭合锁环。具体候选与缺失反向边已说明，不能将“没有发现”写成全产品无此类 bug。
- 系统隐藏后未无条件复核 HWND 是一个条件化恢复边界，未证明 Explorer/Settings 自然触发且工具切换不能恢复。
- 未生成专属强制阻塞探针：没有待证的具体自然失败条件时，桩化成永远失败只能证明人为模型，不能满足用户要求的强证据。
- 产品源码、spec、任务状态及其它工作者研究文件均未修改；本轮只写本文件。
