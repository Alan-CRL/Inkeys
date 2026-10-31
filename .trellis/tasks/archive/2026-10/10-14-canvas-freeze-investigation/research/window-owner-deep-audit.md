# Research: 跨线程 owner、焦点与窗口等待的深入核查

- Query: 查找 F1/F2/F3 之外，Settings、Explorer、选择态、显隐/最小化触发的具体同步等待闭环；用真实 Win32 语义检验隐藏 owned Setting 的捕获和输入队列行为。
- Scope: mixed；只读生产源码、Microsoft 一手文档、主会话当前 HEAD 实验；本研究者只写研究文件和独立探针，不改产品、不构建、不操作 GUI。
- Date: 2026-10-15（Asia/Shanghai）

## Findings

### 1. 新证据与结论

**新增确定的状态残留 F4：Settings 在鼠标按下与抬起之间隐藏后，原生 mouse capture 可以继续指向隐藏的 Settings，主栏工具点击随之失效。** 主会话已在当前 HEAD、不开诊断的完整程序验证，并通过只解除 capture 的同进程对照恢复工具点击。该次实验真实新笔迹仍成功，因此它是有故障/恢复证据的窗口输入缺陷，**不是已经证明的 Draw3 消费线程或 RTS 绘制停顿**。

本轮未找到新的、由正常 Settings / Explorer 操作闭合的永久死锁。比“两个窗口分线程、同步 API 所以必然死锁”更精确的结论是：owner 会把相关 GUI 线程输入状态关联起来，但普通跨线程 SendMessage 的等待具有消息重入；还需要一条无法处理消息的 mutex/future/其他阻塞边才能证明闭环。当前源码已有 guard 拆除了两条历史直接闭环。

### 2. 完整程序捕获残留实验（主会话执行，本研究只读核验）

证据：`Build/CanvasFreezeInvestigation/window-interaction/capture-hide.json`、`capture-hide.png`。

- 主会话记录 PID=29472，Settings HWND=`0x50e06` / TID=14644，主画布 HWND=`0x20e18` / TID=11640。
- 使用实际鼠标在 Settings 内容区按下，保持按下时通过 Alt+F4 关闭，再正常抬起。
- Settings 仍以主画布为 owner；隐藏后 focus 转到主画布，但 capture 没有转移/清空。

| 原始采样 tag | Settings visible | Settings capture | focus | Settings owner |
| --- | --- | --- | --- | --- |
| mouse-down-in-settings | true | 0x50e06 | 0x50e06 | 0x20e18 |
| hidden-before-up | false | 0x50e06 | 0x20e18 | 0x20e18 |
| after-up | false | 0x50e06 | 0x20e18 | 0x20e18 |
| after-new-stroke | false | 0x50e06 | 0x20e18 | 0x20e18 |

JSON 也显示 overlay 与 Setting 两个 TID 查询到相同 capture/focus，符合关联输入队列语义。新笔迹成功由主会话实际输入/屏幕观察确认；本研究没有把 HWND JSON 本身当作“画笔成功”的证明。

主会话进一步完成了故障/恢复对照：

- capture 持续数分钟未自解；真实鼠标点击主栏橡皮坐标没有切换工具。
- 仅向 Settings 自身 HWND 线程 Post `WM_CANCELMODE`，capture 从 `0x50e06` 变为 `0x0`，没有重启程序。
- 再以真实鼠标点击此前相同橡皮坐标，工具正常切换/高亮。
- 新证据是 `capture-recovery.json`（before / after_cancel / after_click）、`capture-toolbar.png`、`capture-cancel-recovery.png`。本研究已只读确认 JSON 中 Settings 与 Bar 的 capture 同时清零；点击效果和持续时间由主会话实际观察确认。

这已闭合“隐藏 Settings capture 残留 → Win32 主栏工具交互停顿 → 取消捕获后恢复”的因果链。鼠标光标反馈、真实 Touch/RTS 是否受影响仍需分别测量。能够通过独立 RTS 继续画，是“capture 残留必使画布冻死”的直接反例，也解释了为什么界面上的某些动画仍可能继续。

### 3. 残留的代码链与真实可达入口

1. `Inkeys/additional/imgui/imgui_impl_win32.cpp:698–705`：收到鼠标 Down，之前无捕获时 SetCapture(Settings)，记录 MouseButtonsDown。
2. `Inkeys/Inkeys/UI/Setting/Setting.cpp:647–658`：SC_CLOSE 与 WM_CLOSE 都直接调用 `Hide()`。
3. `Setting.cpp:8860–8869`：先把逻辑 visible 置 false，再异步排入 HideWindow。
4. `Setting.cpp:610–617`：后续消息只有逻辑 visible=true 才进入 ImGui backend。晚到的 Up 被跳过，而 backend 原本在 `imgui_impl_win32.cpp:719–721` 的末按钮 Up 执行 ReleaseCapture。
5. `Window.cpp:1733–1741`：原生 Hide 命令只 ShowWindow(SW_HIDE)，没有释放 Settings 自身 capture。
6. `Setting.cpp:8657–8663` 的 session cleanup 与 `imgui_impl_win32.cpp:226–244` 的 backend shutdown 也没有显式 ReleaseCapture。

**实际强触发目前是按住鼠标时 Alt+F4；不是标准关闭按钮完整点击。** `Setting.cpp:1433–1436` 的自绘关闭按钮由普通 ImGui Button 触发，通常是 backend 已处理 Up 后再 Hide，不能假定它每次都会留下 capture。任务栏 SC_CLOSE、外部 WM_CLOSE、跨输入源触发隐藏可能进入同类时序，但尚未实测，不升级为已确认触发。

修复方向仅供后续实施：在 Settings HWND owner 线程的 Hide/结束会话边界检查并释放**仍属于这个 Settings HWND** 的 capture，并正确结束本次 backend 按钮状态。不得因为共享输入队列就释放后来已经属于 Drawpad/Bar 的新 capture；也不能在普通 UI/render worker 上直接 GetCapture/ReleaseCapture 并以 null 认为处理成功。本轮未修复。

### 4. owner / 同步焦点的确切语义与错误推断

Microsoft 作者的[跨线程输入队列说明](https://devblogs.microsoft.com/oldnewthing/20130607-00/?p=4143)明确指出：跨线程 parent/child 或 owner/owned 会自动关联输入队列；同队列的 SetForegroundWindow / SetFocus 变化可能需要等待原窗口处理激活/失焦消息。因此“隐藏了 Settings，它就完全不参与输入关系”不成立，隐藏不等于清除 owner。

但 [SendMessage 官方文档](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendmessage)同时规定：跨线程发送方等待时会处理收到的 nonqueued message。故两边普通同步 SendMessage 互相进入，不自动等同于不可恢复死锁。必须额外看到某边卡在不泵消息的 mutex/future，或因重入取回自己已持有的非递归锁，才能闭合。

[SetWindowPos 文档](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos)还限定 SWP_ASYNCWINDOWPOS 对不同输入队列的调用可转为异步；不能假设对已经关联输入队列的窗口加这个 flag 就一定隔离全部同步通知。Topmost 状态会传播到 owned windows，这与产品只调整根窗口的设计相符。

[SetCapture 文档](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setcapture)要求不再需要捕获时由所属线程 ReleaseCapture，同时区分前台/后台和跨线程鼠标位置条件；它不允许据一个非空 capture 值断言所有输入系统、所有进程都失效。现场采样使用具体 TID 的 [GetGUIThreadInfo](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getguithreadinfo)，没有在诊断线程错误地 GetCapture。

### 5. 本轮检查的具体等待链

| 检查链 | 闭环是否成立及原因 |
| --- | --- |
| Setting owner thread → root topmost 命令 → Setting 同步通知 | 当前 `Window.cpp:501–504` 拒绝 Setting owner thread 同步请求 root refresh，`431–454` 有回滚；历史直接闭环不能重用 |
| UI render 持 ImGui mutex → SyncDraw3State → Setting 命令 → ImGui mutex | `Setting.cpp:8766–8768` / `8681–8683` 设置 render scope；`IdtState.cpp:725` / `Setting.cpp:8791–8796` 延后业务同步，直接链被拆开 |
| Overlay root SetWindowPos 与 Setting ShowWindow/SetForegroundWindow 同时执行 | Win32 确实能同步重入，但本轮没有找到新增的生产反向不泵消息等待；不能仅画两条 API 箭头就宣称死锁 |
| Setting 隐藏后继续 owner 关联 → 鼠标 capture 保留 | 完整产品实验支持，Bar 工具点击停止并在 WM_CANCELMODE 后恢复；但新笔仍成功，不能当作 Draw3/RTS 阻塞闭环 |
| 设置标题栏拖动/系统菜单 modal loop → 外层 event 命令排队 | `Window.cpp:913–935` 仅外层 MsgWait/Drain 消费应用 event，native modal loop 不替应用调用 Drain；可造成临时排队，不足以证明退出 modal 后永久阻塞 |
| Draw3 PumpDisplayScale 回调 → SyncDraw3State → owner future | 调用链真实存在，但不是每次 Settings 显隐都触发；需 Host 判定显示上下文需要回调，且 `ApplyPenWidthContext` 返回 false。普通上下文实际改变时 `IdtState.cpp:931–942` 直接 Publish，不走 owner 同步。没有将模态等待推断成普通显隐必现 |
| Settings Present / CreateSwapChain / Release 持 ImGui mutex → DXGI 同步消息 → Setting mutex | 保留既有具体候选；本轮未新增 DXGI 同步通知与锁拥有者证据，不能用 Windows 文档代替现场栈。若成立会牵连共享 UI render，与成功操作设置需核对时间区间 |

Window 命令等待是 `Window.cpp:1376–1397` → `Window.CommandCompletion.h:17–22`；默认 `future.get()` 无 deadline。两组 HWND 各自使用 `MsgWaitForMultipleObjectsEx(...QS_ALLINPUT, MWMO_INPUTAVAILABLE)`，执行命令前释放 queue mutex，Execute 异常也交回 promise。现有等待缺少期限不等于已经证明 promise 会在普通操作中永远无法兑现。

### 6. 独立真实 Win32 / 生产 backend 探针

新增 `research/window_owner_capture_probe.cpp`，主会话已编译、运行；本子代理仅只读核验结果。

复用范围：

- 直接编译仓库的 `imgui.cpp`、`imgui_draw.cpp`、`imgui_tables.cpp`、`imgui_widgets.cpp`、`imgui_impl_win32.cpp`，真实 backend 负责 Down/Up 与 capture。
- 直接 include 生产 `Window.CommandCompletion.h`，使用其默认无 deadline 等待。
- 外层 event + PeekMessage 队列形状与 visible guard 是独立模型；没有编译整个 Window Service、Setting coroutine、Draw3、RTS、D3D 或业务状态机。
- 模型使用 SendMessage 明确构造 backend Down→Hide→Up，不冒充实际物理输入。
- 两个真实 HWND 分属两线程；分别测试 owner=null 与 owner=root。Hide 前后、Up 后、WM_CANCELMODE 后通过真实 GetGUIThreadInfo 取证。
- 可选 `--owner-stress`：Setting 始终隐藏，两个正常请求者自然交错 200 次 owner attach/detach 与 200 次 root TOPMOST/NOTOPMOST。无手动线程暂停、无插入互等锁，无伪造生产死锁条件。
- 20 秒看门狗仅终止本探针，防止独立测试失控；正常流程释放自身捕获并销毁自己创建的窗口。结果退出 0 只表示测试完成、前置 capture 成功，不表示“没有捕获残留”或完整产品通过。

编译入口（利用现有 MSVC 环境，产物放忽略的 Build 目录，主会话执行）：

```text
cl /nologo /std:c++20 /EHsc /utf-8 /DUNICODE /D_UNICODE /I Inkeys/additional/imgui /I Inkeys/Inkeys/Window
  research/window_owner_capture_probe.cpp
  Inkeys/additional/imgui/{imgui,imgui_draw,imgui_tables,imgui_widgets,imgui_impl_win32}.cpp
  user32.lib gdi32.lib dwmapi.lib imm32.lib
```

上式花括号仅列文件说明，不是可直接复制的 PowerShell 展开语法；须使用五个实际源路径，并指定 /Fe、/Fo 到 Build。完整产品 capture 实验已比独立模型更强，模型的作用是有/无 owner 对照和最小回归定位，不能反过来抹去完整产品中“新笔仍成功”的反证。

#### 6.1 已运行的 owner 对照与自然交错结果

主会话使用 `/DUNICODE /D_UNICODE` 对齐 Win32 Unicode 构建后编译退出 0；初次因 `LoadCursorW` 与未定义 Unicode 时的 `IDC_ARROW` 类型不匹配而失败，属于本独立探针的编译参数问题，未改产品。实际运行 `--owner-stress` 退出 0。原始证据为 `Build/CanvasFreezeInvestigation/window-probes/capture-run.log` 与 `initial-run-results.json` 的 `capture-owner-stress` 记录。

| 同一模型中的受控条件 | Hide 时的原生结果 | Up 后结果 | 显式 WM_CANCELMODE 后 |
| --- | --- | --- | --- |
| owner=null | 系统发来取消，`cancel` 由 0 到 1、capture 清零；focus/active 离开该队列 | capture 保持 0 | 保持 0 |
| owner=另一线程的 root | 前台/focus 转到 root；`cancel` 未增加，capture 仍是隐藏 Settings；两线程查询得到相同 capture | capture 仍保留 | 两线程 capture 均清零，`capture_changed` 增加 |

这是一组真实 USER32 + 生产 ImGui backend 的 owner 因果对照：其他步骤相同，建立跨线程 owner 关联后，隐藏时系统不再沿 unowned 对照自动取消捕获的路径，而 visible guard 跳过 Up 的 ReleaseCapture。结论限制在本机该实验条件，不能声称所有 Windows 版本和所有焦点目标均如此。模型仍没有 Draw3/RTS，也没有完整产品 Settings 渲染与生命周期。

同次自然交错压力结果：`owner_completed=200 topmost_completed=200 owner_failures=0 topmost_failures=0`；`RESULT started=1 precondition_failures=0 residual_hidden_capture_cases=1`。它证明该组无需人为暂停的 owner/topmost 操作在本次全部返回，**没有复现同步窗口 API 死锁**；不构成所有时序都无死锁的证明。残留计数 1 是预期被观察的缺陷证据，退出 0 不是“无缺陷”结论。

## Files Found

- `Inkeys/Inkeys/Window/Window.cpp`：两个 HWND 线程、owner、显隐、根 topmost、命令 event 与同步回执。
- `Inkeys/Inkeys/Window/Window.CommandCompletion.h`：实际默认 future 等待，探针直接复用。
- `Inkeys/Inkeys/UI/Setting/Setting.cpp`：visible guard、异步 Hide、关闭入口、ImGui 锁与 defer guard。
- `Inkeys/additional/imgui/imgui_impl_win32.cpp`：真实 backend capture、Up、focus、Shutdown 行为。
- `Inkeys/IdtState.cpp`、`Inkeys/IdtMain.cpp`、`Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp`：同步状态发布与条件显示上下文回调。
- `Build/CanvasFreezeInvestigation/window-interaction/capture-hide.json`：主会话完整程序实际 HWND/线程捕获证据。
- `Build/CanvasFreezeInvestigation/window-interaction/capture-recovery.json`：同进程只取消 capture 的恢复对照。
- `Build/CanvasFreezeInvestigation/window-probes/capture-run.log`、`initial-run-results.json`：真实 USER32 owner 对照及 200+200 次自然交错操作结果。
- 本任务 `research/window_owner_capture_probe.cpp`：独立、限时、仅操作自身窗口的探针。

## Related Specs

- `.trellis/spec/native-desktop/draw3-integration.md`：Window Service HWND 所有权、独立 Draw3 runtime 与输入边界。
- `.trellis/spec/native-desktop/rendering-and-ui.md`：Settings/UI RenderPipeline、线程和资源所有权。
- `.trellis/spec/native-desktop/cpp-conventions.md`：实际锁顺序、最小改动与中文关键注释。
- 本任务 `research/window-permanent-stall-audit.md`、`settings-window-path.md`、`no-console-settings-followup.md`：此前候选与未证边界；本报告对隐藏 capture 从“待实测”提升为“完整程序已观察并阻断 Bar 工具点击，但未使真实新笔失效”。

## Caveats / Not Found

- 没有把普通系统 API 的同步语义或独立 probe 的人为输入顺序，当作“用户原步骤必然永久卡死”的证明。
- 新捕获问题与用户输入设备未知、触摸/鼠标均报告的整体问题尚未建立等价关系。
- 本研究者没有产品修复、构建、GUI 操作；主会话负责实验。所引完整程序证据与独立探针原始日志均已只读核验；不要把相邻其他 RTS probe 记录的退出结果混入本探针结论。
