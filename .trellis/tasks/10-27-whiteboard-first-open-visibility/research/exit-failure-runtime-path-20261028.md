# Research: 白板退出失败提示与进程重启路径

- Query: 用户确认白板背景和分页已经消失，但主栏仍保持白板布局并收到“Exiting the whiteboard did not finish”提示；核对该提示的触发条件、已完成的部分收尾、确认按钮是否启动新进程，以及最小诊断修复边界。
- Scope: internal; 静态源码与现有 headless 测试；未启动 GUI、未构建、未采集用户现场日志。
- Date: 2026-10-28

## Files Found

- `Inkeys/IdtState.cpp` — 三工作区协调器、退出循环、失败预算及通知发布。
- `Inkeys/Inkeys/Window/Window.cpp` — 原生 workspace Prepare/Complete、命令回执和失败回滚。
- `Inkeys/Inkeys/Window/Window.CanvasPolicy.h` — Prepare/Complete 的真实步骤顺序。
- `Inkeys/Inkeys/UI/Setting/Setting.cpp` — 信息通知和工作区重试的设置业务 FIFO。
- `Inkeys/Inkeys/UI/MessageBox/MessageBox.cpp` — OK 对话框结果映射及显示入口。
- `Inkeys/Inkeys/Helper/Helper.CrashHandler.cpp` and `.cppm` — 崩溃重启对话框与显式进程关闭/重启 helper。
- `InkeysHeadlessTests/window_tests.cpp` — workspace policy helper 的逐阶段故障注入。

## Findings

### 1. 提示是工作区 native policy 失败预算耗尽，不是软件退出

`WorkspaceHandoff::Fail` 在同一仍 pending 的请求上累计四次后设置 `failed=true`；前几次分别退避 250、500、1000 ms（`Inkeys/IdtState.cpp:154-212`）。`FailWorkspaceRequestImpl` 仅在首次进入 failed 时排入一条设置业务通知（`:301-316`）。故障版本仍 pending，`Ready()` 随后为 false；显式重试会用新 revision 重启请求预算（`:1238-1258`）。

生产循环只有两处将这种失败计数器推进：`PrepareWorkspaceWindowPolicy` 返回空（`IdtState.cpp:3412-3424`），或 `CompleteWorkspaceWindowPolicy` 返回 false（`:3442-3446`）。两者共同走 `nativeFailed`，它查询最近同步窗口命令回执；除 `Cancelled` 与 `Expired` 外均按一次失败计数（`:3398-3405`）。因此截图证明某一原生事务边界重复失败，但不能区分 Prepare、Complete 或其具体 owner/native 步骤。`LastCommandReceipt`只给出通用阶段，不是失败 API、错误码或失败阶段。

### 2. 白板已隐藏但主栏未归位符合当前分段提交顺序

退出在进入原生 Complete 之前即 `PublishActive(false, revision)` 并等待背景 ACK（`IdtState.cpp:3407-3411`）；PageControl 展开目标也在首次处理该请求时关闭（`:3353-3364`）。主栏的 `SetWhiteboardActive(false)` 则只发生在 `CommitWorkspaceHandoffLocked` 成功之后（`:3449-3465`）。持续原生失败或命令回滚阻塞时，白板背景/分页已收起而主栏仍保持已提交 Whiteboard 是源码允许的中间状态。提前清主栏会伪造 Desktop/PPT 已提交并绕开所需窗口与实际帧凭据；不应作为外观修补。

`CompleteWorkspacePolicy`依序核对准备身份、应用穿透输入、显示目标画布/背景，最后发布 fullscreen，并在每步间重验当前性（`Window.CanvasPolicy.h:141-157`）。其中输入操作会嵌套既有 `SetClickThrough` 命令；原生完成可能在此前已真实隐藏白板背景/调整窗口后才失败。`ExecuteTracked`会对已接受事务建立恢复快照，并在失败后同步尝试回滚；回滚失败会把责任留在 Window owner 恢复屏障内（`Window.cpp:2902-2963,3190-3264`）。现有状态通知没有报告回滚结果或真实写入阶段。

### 3. 现有测试验证局部阶段顺序，不验证真实失败被分类和主栏恢复

`window_tests.cpp:1000-1053` 对无 HWND 的 `PrepareWorkspacePolicy` / `CompleteWorkspacePolicy` helper 注入每个阶段失败，并确认后续步骤停止；它不能覆盖 `Service::ExecuteTracked` 的真实恢复快照、Win32结果、receipt 到 `WorkspaceHandoff::Fail` 的映射。`IdtState.cpp:2384-2393` 的 probe 则直接调用 `FailWorkspaceRequestImpl` 多次，验证四次 edge notice 与重试 token，不模拟 native failure 或验证 `Bar::SetWhiteboardActive(false)` 最终自动发布。

### 4. 提示框的 OK 处理本身不关闭或重启应用

`WorkspaceTransitionFailureNotice`分支只检查故障 revision，然后 fallthrough 到 `Information`；它创建 OK 消息、调用 `MessageBox::Show` 并返回显示是否失败（`Setting.cpp:872-894`）。该 case 不调用 `RestartProgram`、`CloseProgram` 或进程 API。`MakeOkRequest`把按钮和关闭框映射成 `Result::Ok`，而 `Show`只返回对话框结果（`MessageBox.cpp:184-195,290-315`）。同一 switch 的重启和关闭动作位于其他独立命令 case（`Setting.cpp:914-924`）。

崩溃 handler 的独立提示标题是 `Inkeys Error`，文案为崩溃重启，只有其 OK 才 `TryClaimCrashRestartIntent` 并 arm 重启 helper（`Helper.CrashHandler.cpp:640-662`）；用户截图明确是 `Inkeys Tips` 的工作区 notice。正常 Inkeys launch mutex 只拒绝重复启动，不会自行创建第二进程（`IdtMain.cpp:1727-1742`）。因此当前静态代码不能把“点击此 notice 的 OK 后看见新进程”解释为该 notice 的按钮回调。它可能是同一时段其他 watcher/崩溃/外部启动行为；需运行时记录进程路径、PID/父 PID/命令行及前一进程退出状态才能定因。

## Minimal Diagnostic / Fix Boundary

1. 先在实际失败处保存稳定的失败 stage（Prepare 或 Complete）和 `CommandReceipt::Phase`；如果可从 Window owner 安全带回明确失败类别，再附命令类型与回滚是否成功。诊断只在首次失败/预算终止边沿输出，避免轮询日志风暴；不记录 HWND 地址或其他无关用户数据。
2. 不要把 `Cancelled` / `Expired` / stale guard 和真实 API 拒绝统称作相同 native failure。复查同步 nested `SetClickThrough` 的执行失败是否被外层 receipt 准确代表，及失败后 overlay recovery 屏障是否仍未退休；只有取得阶段/回执证据后才按最小失败项改策略。
3. 用生产接缝补离屏回归：native阶段与 receipt 结果驱动失败预算；失败恢复后无需额外鼠标输入，主栏只在真实目标完成后发布一次 Desktop Leave 或 Presentation Switch。实际 USER32/WindowService、Shell、任务栏和多进程生命周期仍需已授权 GUI/实机复验。

## Related Specs

- `.trellis/spec/native-desktop/whiteboard.md` — 白板退出请求、共享 Freeze/owned 窗口链、场景与主栏合同。
- `.trellis/spec/native-desktop/rendering-and-ui.md` — 白板窗口 owner、必要真实窗口交接、GUI 与离场顺序验收。
- `.trellis/spec/native-desktop/canvas-freeze-boundaries.md` — 物理恢复责任、安全隐藏/穿透覆盖和窗口恢复屏障。

## Caveats / Not Found

- 没有用户这次运行的 native stage、API错误、CommandReceipt 相位、Window recovery 快照、日志或新进程 PID/父进程；无法宣称唯一根因已确定。
- 主会话随后按用户授权尝试本机 GUI 复现：Debug `Inkeys.exe` 以高完整性启动，普通权限发往 Freeze HWND 的 `WM_CLOSE` 被 UIPI 以错误5拒绝；提权后 `PostMessage(WM_CLOSE)` 返回成功，但屏幕捕获仍显示 Codex，无法确认目标消息已被交互桌面实际处理，也没有出现可读取的本次失败通知或事务日志。两次 Debug 进程均由本次测试启动，不能拿来解释用户提及的新进程；复现未成立，测试实例应结束。
- 没有产品源码修改、build或测试。当前产品修改建议仍应等待可关联的实际失败 stage 和 native/API结果，不能根据通知本身推断某一个原生步骤。
- 研究仅支持“通知按钮本身不重启”；不能排除应用在同一时刻因独立异常或外部 watchdog 重启。
