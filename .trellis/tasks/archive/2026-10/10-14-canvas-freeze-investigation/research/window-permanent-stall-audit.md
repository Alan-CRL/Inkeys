# Research: Drawpad、Setting 与全局交互的永久停顿审计

- Query: 扩大搜索“画布停止响应而设置仍可操作/重启”的真实可达永久等待；检查 HWND、同步消息、窗口业务队列、锁逆序、COM 和交互状态归位，并给出可区分候选的诊断实验。
- Scope: mixed；生产源码只读审计，另提供任务目录内的独立 ARM64 栈采样器；不修改产品、不构建、不运行 GUI。
- Date: 2026-10-15
- Task: `.trellis/tasks/archive/2026-10/10-14-canvas-freeze-investigation`
- Evidence boundary: 当前源码、历史录屏、旧 Debug 产物与构造故障实验必须分开。所有“可永久等待”结论均注明触发前提；尚无用户现场线程栈，因此不能把静态候选直接定为该视频根因。

## Findings

### 1. 优先级与结论

本轮最高确定性的可达无限等待位于 Draw3 的同步日志 sink：开启光标诊断后，画布主循环把批次直接交给 `WriteConsoleA` 或同步 `WriteFile`，没有隔离线程、期限或取消机制。stdout 指向保持连接但停止读取的同步管道时，缓冲区满即可让这次调用持续等待。环形队列的 1024 条容量与丢弃逻辑不能让已经进入 sink 的线程返回。此时 RTS/窗口生产者不必被同一把日志锁阻塞，Setting 的独立窗口线程和共享 UI 渲染线程也不必停顿，符合“画布不响应、设置仍可用”的线程分离条件。

这是具有明确构造条件的活性缺口；仍未证明用户的控制台发生了相同流控。视频出现 `CURSOR_TRACE` 提高了该前提的相关性，但在确认真正停止后若 Draw3 持续产生新的 frame 记录，则那一时段不可能同时永久停在该 sink 中。视频中的 `tool=0` 是 Pen，`mouseLifecycle=0/0` 是橡皮光标状态；脏区为空时的 `success=0` 也不能当作 GPU 失败。

| 候选 | 可达性与永久性 | 与 Setting 仍可重启的相容性 | 当前置信度 |
| --- | --- | --- | --- |
| Draw3 同步 stdout/console sink | Cursor 开关可由普通设置启用；管道停读可精确构造无限外部等待 | Draw3 不持有 cursor 环形队列锁等待 sink；UI 可独立运行 | 代码缺口高；用户现场根因未确认 |
| Window Service 普通命令 future 无期限 | 普通跨线程命令确实 `result.get()`；还需找到使 owner 永久无法完成命令的原因 | Setting 与 overlay 分线程；但 overlay 阻塞会同时影响 Bar HWND | 等待机制高；真实故障起点未确认 |
| Overlay root 刷新内 Taskbar COM/同步 Win32 | 所属 STA 中存在无应用期限的调用；外部 Shell/窗口阻塞是条件 | Setting 线程独立，但 UI 渲染可能随后等待窗口命令 | 中低；尚无被调用方永久停顿证据 |
| 当前 Setting/ImGui/DXGI 锁闭环 | 有共享串行 UI 线程与 DXGI 同步消息风险，但未找到本次强确定闭环 | 成功操作 Setting 重启按钮削弱“共享 UI 渲染永久死锁” | 低于前述候选 |
| Whiteboard 退出留下 click-through | 能读到可疑状态赋值，但功能入口被 constexpr false 拦截 | 形状上相容，当前正常路径不可达 | 当前根因排除 |

### 2. 最强候选：Draw3 主循环中的同步输出

#### 2.1 完整调用链与锁边界

1. `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:12605` 进入绘制循环；每轮首先在 `:12607` 调用 `FlushCursorDiagnostics()`，早于本轮输出目标/模式处理。帧尾 `:14454` 与退出路径 `:14592` 也调用同一函数。
2. `Draw3.Diagnostics.cpp:544` 的 `RecordCursorDiagnostic()` 是生产者：运行期开关在 `:546` 检查，固定行包含 tick/TID；`:561` 只在追加/覆盖环形队列时持有 `cursorDiagnosticMutex`；队列满时 `:563-568` 丢最旧行并计数。
3. `Draw3.Diagnostics.cpp:574` 的 flush 每批最多 64 条、每轮最多 16 批（`:577-578`）；`:583-592` 只在拷贝/弹出时持有队列锁。格式化与 sink 发生在锁外。
4. `Draw3.Diagnostics.cpp:599` 与 `:617` 调用 `WriteFastConsoleLine()`。
5. `Draw3.Diagnostics.cpp:48-56` 缓存 `STD_OUTPUT_HANDLE`，先 `WriteConsoleA(..., nullptr)`；若不是 console 则回退 `WriteFile(..., nullptr)`。名称中的 Fast 与批量写入只降低调用频度，不给调用增加完成期限。

锁外 sink 解释了为什么 RTS 的入队/accepted 日志生产者可以仍在活动，绘制消费者却停止。日志行里的 TID 是记录生产者的 TID；stdout 停读后看到旧批次，不能单凭行内 TID 推断 flush 仍在运行。需要采样实际 Draw3 线程栈，并用序号、tick 与恢复读端的时间对齐。

**永久性证明的条件**：stdout 是同步写端；读端保持打开且不读取；待写数据超过可用管道缓冲。写端需要读端腾出空间、断开、外部取消或进程终止才会完成。当前 Draw3 线程进入该系统调用后，模式切换、关闭设置、stop 标志和环形队列丢弃均不会在本线程上执行，不能自行解锁这次写入。这属于有条件的无限外部等待，不是已经证明的线程锁闭环。

Microsoft 的[同步和重叠管道 I/O](https://learn.microsoft.com/en-us/windows/win32/ipc/synchronous-and-overlapped-input-and-output)与[同步和异步文件 I/O](https://learn.microsoft.com/en-us/windows/win32/fileio/synchronous-and-asynchronous-i-o)明确区分同步等待与异步操作。[Microsoft 的 pending WriteFile 构造示例](https://devblogs.microsoft.com/oldnewthing/20220425-00/?p=106526%2F)可用于设计读端停读的诊断，不能替代用户现场证据。

#### 2.2 普通用户如何进入该路径

| 入口/条件 | 源码与含义 |
| --- | --- |
| 默认配置 | `Inkeys/Inkeys/Other/Other.Config.cppm:356-360` 的 PptCOM、Draw3、TouchArea、Cursor 均为 false；该路径不能解释未经配置的所有用户 |
| 设置中启用 | `Inkeys/Inkeys/UI/Setting/Setting.cpp:7693` 的实验室 Inkeys3 区域，`:7696-7717` 绘制选项，`:7751-7758` 的“光标调试信息”更新 Cursor 并排队保存配置 |
| 生效时间 | `Inkeys/src/i18n/zh-CN.jsonc:498-499` 明示下次启动输出；设置切换处未直接修改 Draw3 runtime 开关 |
| 启动绑定 | `Inkeys/IdtMain.cpp:2629-2640` 读取 Cursor；三类原生日志任一开启则调用 `InitializeDebugConsole()` 并设置 runtime CursorDiagnosticsEnabled |
| 非发布诊断覆盖 | `Inkeys/IdtMain.cpp:2614-2621` 的 `INKEYS_DRAW3_WIN7_DIAGNOSTICS=1` 可在非 IDT_RELEASE 构建打开 Draw3/Cursor；发布构建该局部覆盖恒 false |
| 控制台与重定向 | `Inkeys/IdtMain.cpp:295-325` 在正式构建也可 AllocConsole；已有 FILE_TYPE_DISK/PIPE 被保留，未重定向的 stdout/stderr 绑定 CONOUT$；两路均重定向时不创建 console |

因此“刚打开 setting 就卡”的时间关系并不证明 setting 本轮开启了诊断：开关在下一次启动生效。历史配置可能早已开启，实际 sink 等待恰好在设置/Explorer 操作后暴露。

#### 2.3 QuickEdit 与 console host 的证据边界

生产源码中未找到 `GetConsoleMode`、`SetConsoleMode`、`ENABLE_QUICK_EDIT_MODE`、`ENABLE_EXTENDED_FLAGS`。应用未自行查询/约束这些模式。

[SetConsoleMode 官方文档](https://learn.microsoft.com/en-us/windows/console/setconsolemode)说明 `ENABLE_QUICK_EDIT_MODE` 为 0x40、和 0x80 的 extended flags 配合控制文本选择。这里应记录实际 `GetConsoleMode(STD_INPUT_HANDLE)`、`GetConsoleSelectionInfo` 与宿主类型；不能只因运行 Windows 11 就断言所有 conhost、Windows Terminal、继承控制台都采用同一模式或同一流控行为。

控制台选择造成输出停顿是值得另测的候选。主实验使用可确定读端行为的管道先验证 sink 缺口，随后才在用户同类 console host 中做选择→画布停顿→取消选择→恢复的对照。若原生 conhost 选择复现，也只证明此用户入口可触发该类问题；视频是否实际选择过控制台仍需证据。

#### 2.4 其他日志出口不能遗漏

- `Draw3.Diagnostics.cpp:655-671` 的 debug frame timing 也会进入该同步 writer，因此 Debug 管道实验不一定只被 runtime Cursor flag 控制。
- `Draw3.WindowControl.cpp:1035-1070`、`:1084-1097`、`:1182-1224` 可见持 trace 锁的 cout/flush，但在 `DRAW3_RTS_DIAGNOSTICS` 条件编译区域；本轮搜索 `Inkeys.vcxproj` 未找到该宏。不能把这些宏内输出当作已启用的发布路径。
- `Draw3.Diagnostics.cpp:146` 的旧 RTS trace cout/flush 也受该宏约束。上面的 Cursor sink 是独立的运行期开关路径。
- 其他绘制线程 cout/stderr 必须一并列出再定修复范围；只替换名为 Cursor 的 sink 不能声称画布从此不受 stdout 阻塞影响。

### 3. 窗口线程、命令队列与跨线程窗口调用

#### 3.1 分清 owner 线程与共享 UI 渲染

`Inkeys/Inkeys/Window/Window.cpp:230-235` 建立 overlay 与 Setting 两个受管线程；`:870-879` 分别登记 TID，overlay 初始化 COM STA。Drawpad、Bar、PageControl、Mag/Freeze 属于 overlay group；Setting 属于独立 group。Bar、PageControl、Setting 的视觉帧则共享 UI RenderPipeline 的串行线程；Draw3 的独立设备/画布线程另属一个 runtime。

这给现场描述两条约束：

- 若 overlay 线程被永久阻塞，Drawpad 与 Bar HWND 都不能正常分派后续输入。Bar 有旧动画/像素变化不能证明其点击已被 HWND 接收；要记录点击后的实际业务状态。
- 若 Setting 按钮在故障之后能接受新的点击并执行重启，Setting HWND 与共享 UI 渲染至少在那时仍能前进。这削弱共享 UI 渲染自身的永久锁死，但不能排除 Draw3 或独立业务线程的等待。

#### 3.2 无期限命令等待确实存在，未发现空 promise/丢唤醒的确定起点

`Window.cpp:1376-1397` 的 Submit 对同 owner 直接 Execute，其他线程把 promise 命令入队并设置 event，再调用 `WaitForCommandResult()`。`Window.CommandCompletion.h:17-22` 仅在显式 deadline 存在时 wait_until，否则 `result.get()` 无期限。`RequestTopmostRefresh` 的默认等待为 INFINITE（`Window.cpp:552-557`）；普通 Show/owner/style 命令也未普遍设置期限。

但这只是等待边，不构成完整死锁证明：

- `Window.cpp:1384-1386` 对 owner-thread 调用直接执行，防止等待自己的队列。
- `Window.cpp:1400-1416` 在队列锁外 Execute；捕获执行异常后仍 set_value(false)，未发现普通异常漏交回执的路径。
- `Window.cpp:919-922` 先 ResetEvent 后 DrainCommands；`:1402-1406` 只在 swap 时持锁。新请求在 drain 期间入队会再次设置 event；目前没有发现简单的 reset-after-push 丢唤醒。
- `Window.cpp:855` 的 FailPending 会结束停止时仍排队的 promise。已经进入卡住的 Execute 仍须另找终止条件，这一点不是 FailPending 可覆盖的。

可执行实验应给每条命令添加研究性序号，记录 submit caller TID、owner TID、execute enter/exit/timeout。永久停顿时，区分“未执行”“Execute 卡在某 Win32/COM 调用”“已完成但等待方未返回”，再展开锁图。仅把全部 `.get()` 换成超时可能让迟到命令继续改变窗口，不能当成完整修复方案。

#### 3.3 Setting owner 的历史闭环已被当前 guard 改写

`Window.cpp:431-441` 的 SetSettingOwnedByDrawpad 把 Setting owner 变更提交到 Setting 队列；PrepareSettingZOrder 在 `Window.cpp:501` 检查当前 Setting thread，遇到必须跨 owner 刷新时返回失败并走回滚，而不是再同步等待 overlay。当前代码不能直接套用旧版本“Setting→overlay→Setting”死锁叙述。

本轮未找到显式 `AttachThreadInput`。但是跨线程 owner/owned 本身可能隐式合并输入队列；[Microsoft 的跨线程父子/owner窗口说明](https://devblogs.microsoft.com/oldnewthing/20130607-00/?p=4143)表明不能据此推断二者焦点处理完全隔离。SetForegroundWindow/SetWindowPos/owner 变更仍值得采样，风险必须有现场相反等待边才能升级为根因。

`Window.cpp:1606-1618` 的 CancelPointerCapture 在 overlay owner 内向其 own HWND SendMessage(WM_CANCELMODE)，并不是无条件的跨线程循环。Bar 取消路径使用 UpdateRendering(false)，也不等待 UpdateRendering(true) 的静态互斥锁（下一节）。

### 4. 当前锁图审计：已检验的反例与尚缺的一条边

| 路径 | 确认的持锁/等待顺序 | 审计结果 |
| --- | --- | --- |
| Product 公共入口 | `Draw3.Product.cpp:75-100` 中 productCallMutex 包住 Host/bridge 调用；ForwardMessage 使用 recursive_mutex | 同线程重入已处理；仍需有跨线程反向等待才能证明闭环 |
| Product 停止 | `Draw3.Product.cpp:32-43` 在等待 Host.Stop 前，`:40-42` 的 productCallMutex 作用域已结束 | 排除“Stop 持该锁 join Draw3”的直接推断 |
| StateMode 发布 | `IdtState.cpp:215-229` mode mutex→eraser preferences→product；`:723-746` Sync 中 mode 编辑/读取先释放，再 ApplyOwner→Publish→Reconcile | owner 应用确实可等窗口，但不能声称整个 Sync 一直持 mode mutex |
| 设置 owner 应用 | `IdtState.cpp:98-105` settingOwnerApplyMutex→Window Setting 队列；StateMonitoring `:1761` 周期调用 | 周期 250ms 不是命令期限；若 owner queue 真停，可连带阻止该调用者 |
| 绘制呈现协调 | `IdtState.cpp:458-473` 短 mode snapshot 后持 draw3PresentationMutex；`:572` SetDrawpadSurfaceVisibility、`:601` refresh | 持 presentation mutex 等窗口可达；尚未发现该 HWND 回调持相反锁等待 presentation mutex 的确定边 |
| Eraser setter | `IdtState.cpp:672-680`、`:693-717` 块内更新首选项，块外 Sync | 未发现 eraserPreferences→mode 的简单持锁逆序 |
| PenWidth setter | `IdtState.cpp:852-881` 明确 unlock 后再持久化/发布 | 未发现 mode 锁跨后续窗口调用的该条路径 |
| Bar UpdateRendering | `Bar.Main.cpp:279-292` 静态状态锁仅包快照与 StateUpdate；`Bar.Button.cpp:901-914` 状态/呈现分开 | PrepareMainPanels→PresetHoming(`:1496`) 当前没有 CloseColorPicker/Thickness 的预想同步 SendMessage，不能据名字拼出闭环 |
| Bar capture 消息 | `Bar.Interaction.cpp:553-557`、`:749-797` 取消/释放捕获使用 UpdateRendering(false)，且先清 flags 后 ReleaseCapture | 排除静态 UpdateRendering 锁在 WM_CANCELMODE/WM_CAPTURECHANGED 的直接重入自锁 |
| HiMsg callback | `HiMsg.hpp:825-834` 在 callbackMutex 内复制回调、释放后调用业务 | 排除“队列始终持 callbackMutex 执行业务”的推断 |
| PageControl 呈现 | `PageControl.cpp:1781-1964` renderTransactionMutex 先结束，再 `:1971-2007` 持 presentationMutex 提交窗口 | 排除 renderTransactionMutex 直接跨 HWND 等待的该路径 |
| PageControl owner 直移 | `PageControl.cpp:1463-1488` try_to_lock presentationMutex，失败排队并返回；`:2021-2022` 失败路径先解 presentation 再取 renderTransaction | 重要逆序已有回避机制，不能用一般锁风险替代具体代码 |
| Bar 直拖 | `Bar.Interaction.cpp:7176-7204` try_lock directWindowDragMutex→SetWindowPos→borderCursorLightMutex；`Bar.RenderLoop.cpp:12956-12957` 的呈现事务也取 direct mutex | 尚未找到持 borderCursorLightMutex 又等待 direct mutex 的相反边。常见 border 读写均短作用域，不把相邻出现直接当锁嵌套 |
| Display callback | `Display.cpp:492-508` subscriber 记账锁外 invoke；`:803-804` refresh 锁释放后 drain；`:825-828` shutdown 跳过当前 callback 自等待 | 排除订阅分发必然在内部锁下执行和直接 self-unsubscribe 死锁 |

正常点击 Bar 的 `clickFunc` 在单一业务交互线程上同步运行（`Bar.Interaction.cpp:4003`，主循环 `:6021`）；一次 Sync 若永久卡住，不能再用之后真实成功的工具切换证明该同一线程仍卡在早先调用中。UI 状态、高亮、Draw3 bridge、实际墨迹必须分别取证。

### 5. Shell、COM 与永久退出的独立候选

- Explorer 入口 `Inkeys/Inkeys/Business/ComponentActions.cpp:53-54` 用 ShellExecuteW；ShowDesktop `:65-71` 在 Bar 业务线程 CoCreateInstance/ToggleDesktop。无应用期限是事实，但“Shell 一定会卡住”没有证据；该业务线程停止本身也不等于独立 RTS/Draw3 停止。
- Window root 刷新会执行 `Window.cpp:1870-1887` 的 SetWindowPos 与 ApplyOverlayFullscreen。后者在 `:2175-2180` 的 overlay STA 中创建 Taskbar COM，HrInit→MarkFullscreenWindow，不由 false whiteboard 开关完全跳过。此处可成为 overlay 对外部 Shell 的无期限等待，但 CLSCTX_INPROC_SERVER 本身不能证明必定存在 RPC 或闭环。需要抓 stack 及被等待 HWND/PID。
- `Draw3.Host.cpp:1684-1712` 的 drawing.Run 异常会标记 runFailed、请求退出、释放绘制并令 running=false；未见自动重启。这可形成“画布永久停止而其他 UI 还活”的另一机制，但还没找到本用户路径的确定生产异常，不能只用 bad_alloc 等泛化推断定根因。诊断需取 running/runFailed、Host thread 是否仍存在。

### 6. 交互状态残留：明确不可达与不应混淆的状态

搜索当前生产源码未找到名为 `interactionDisabled` 的开关，也没有独立的“reconfiguration 开始后忘记归位”字段；reconfigur 命中主要是 ShouldPreconfigureNoRedirectionBitmap 命名。实际准入是 Window/RTS/input admission、selection、呈现目标/暂停与产品停止等多个状态，输入研究者另行负责其全链。

Whiteboard `IdtState.cpp:1969` 的退出恢复会按 snapshot.selectionMode 设置 primary WS_EX_TRANSPARENT，看似可留下 click-through。但 `IdtState.h:201` 的 `IsWhiteboardFeatureEnabled()` constexpr false 与 `IdtState.cpp:785` 的请求 guard 让普通当前产品入口不可达，不能拿它解释打开设置/Explorer 后的现有故障。若未来开放白板，应单独补充跨模式恢复测试。

`PageControl.cpp:175` 默认 inputLocked 与 `:962` 的重算只控制 PageControl 交互，不是 Drawpad 通用禁用。变量名相近不能替代实际消费者。

### 7. “设置仍能重启”能排除什么

`Setting.cpp:1595-1598` 的实际导航重启按钮先 Hide，再调用局部 RestartProgram 宏（`:702`）对应 QueueRestart。当前 `Setting.cpp:471-480` 直接调用 `::RestartProgram()`/`::CloseProgram()`，明确不排在旧 business FIFO 后；15 秒退出监督器可以启动。

因此，故障后新点击该按钮有效，支持共享 Setting 渲染/窗口处理仍工作，同时允许 Draw3、旧业务 worker 或 overlay 某部分等待。它不证明正常退出已逐线程无阻塞完成，也不能把“存在 kind::Restart 的旧 executor 分支”当成当前按钮仍排队的依据。

旧 Debug 产物的空白设置在 WM_CLOSE 后恢复且继续能绘制，Edition 为 `3.0.0-20260811a`；当前源码为 `3.0.0-20261014a`。该现象不升级为当前 HEAD 的新复现。旧 WCT 无 cycle、Bar 仍可操作，也降低“共享 DXGI 永久锁死”的证据权重；DXGI 同步消息的一般风险保留在此前 `settings-window-path.md`，不作为本轮主因。

### 8. 可执行诊断实验与分流标准

| 实验 | 必须采集 | 支持/否定条件 |
| --- | --- | --- |
| stdout 管道保持连接后停读 | 先完成启动且持续排空，开始画笔/橡皮基线；停读后继续产生输入；故障时 Draw3 线程栈、Setting 点击、读端恢复时间 | Draw3 反复处于 WriteFile/WriteConsole→WriteFastConsoleLine→Flush→Run，恢复读端后可继续画，支持 sink 活性缺口；这仍是构造实验 |
| conhost/Terminal 文本选择 | host 类型、实际 console input mode、selection flags、选择开始/取消 tick，开启/关闭 Cursor 的启动对照 | 只有具体 host 可复现并且取消选择即恢复才升级 QuickEdit 入口；不可从 pipe 结果直接推定 |
| 当前 HEAD 的原始操作顺序 | Edition/hash、实际 binary/PDB、设置 Show/Hide、Explorer 前后、每次有效 RTS Down/Up、publish、frame/stroke commit | Down 未产生但 WndProc accepted/frame 活着则优先 RTS 路由；Down published 而帧停止才优先绘制等待 |
| Window 命令序号 | 每条 Submit/Execute 起止、caller/owner TID、role、序号、期限与返回；阻塞时 owner stack | 区分命令未取、Win32/COM 未返回、回执未完成；仅 future.get 栈不是完整根因 |
| 新操作验证 UI 生存 | 故障之后点击设置不同导航项、重启按钮；Bar 工具变更后的 bridge 实际值 | 视觉高亮和旧帧不够；避免把仍显示的窗口当作仍在分派消息 |
| Host 异常/退出 | thread presence、running/runFailed、最后异常记录、controller 收尾点 | 无 Draw3 线程则走终止机制；有线程则先定位实际 wait site |

所有新增诊断输出应避免写到被测试的同一 stdout sink；否则观察本身会阻塞。优先独立采样器读取线程栈与本地文件记录。WCT 对内核 I/O、驱动等待及外部 console 流控未必给 cycle，因此“无 WCT cycle”不能排除此候选。

### 9. 独立 ARM64 线程栈采样器

新增任务产物 `research/thread_stack_probe.cpp`，由主会话负责用现有 ARM64 原生 MSVC 环境编译/运行，不接入 vcxproj、不改构建系统。

用法：`thread_stack_probe.exe <Inkeys PID> [本地 Inkeys.pdb 或符号目录的绝对路径]`。默认搜索目标映像目录；额外符号目录仅一个本地绝对路径。工具先用进程快照与打开后的映像路径双重验证 Inkeys.exe，并检查 native ARM64；线程快照只筛目标 PID，打开后再校验线程仍属于它。未申请 SeDebugPrivilege、不写目标内存、不注入。每线程增加一次 suspend count、GetThreadContext(CONTEXT_FULL)、ARM64 StackWalk64 最多 64 frames，通过 RAII 恰好 Resume 一次；恢复后才做符号/行号输出，避免 stdout 停读时目标仍被暂停。所有 DbgHelp 调用在采样器唯一线程上串行执行。

符号显式本地路径；忽略环境符号路径和 CodeView 内的路径信息；禁止自动识别 symbol server，无 SRV*/网络路径。模块/PDB 预加载位于任何目标线程暂停之前。缺符号时保留原始 PC/SP 和 module offset，不能把未知帧当成安全返回。

限制：逐线程采样不是同一时刻的一致全进程快照；线程可能退出并报告 OpenThread/GetContext/Resume 失败。64 帧只限制栈深度，不是整个 OS/DbgHelp 调用的时限。RAII 覆盖正常及 C++ 异常退出，无法保证采样器被外部强制终止/系统崩溃后的恢复；实际 Resume 返回值明确输出，应检查 resume_error。不要在采样器可能持有暂停线程时随意终止它。进程或线程访问被 OS 拒绝是诊断权限限制，不能据此猜测目标线程状态。

参考：[GetThreadContext](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getthreadcontext)、[StackWalk64](https://learn.microsoft.com/en-us/windows/win32/api/dbghelp/nf-dbghelp-stackwalk64)、[SymSetOptions](https://learn.microsoft.com/en-us/windows/win32/api/dbghelp/nf-dbghelp-symsetoptions)。本机 SDK 10.0.26100.0 的 DbgHelp.h/WinNT.h 为 ARM64 上下文与符号选项的实际编译依据。

#### 9.1 PAC 地址显示候选与正式展开 API

首次实采发现 StackWalk64 在带签名高位的 LR 处停止。采样器现在只在打印阶段增加一次有界解析尝试：保留原始 pc/sp；若原始 module、symbol、line 均无法解析，则计算低 48 位候选；必须通过已加载模块的 BaseOfImage/ImageSize 范围校验，才输出 `normalized-candidate=0x... candidate-only=1`，并对候选查符号/行号。CaptureStack、CONTEXT、StackWalk64 输入和帧数完全不变，未新增栈证据。低 48 位规则仅是此次用户地址布局的解析候选，不是通用 PAC 验证/去签名算法，不能用其结果宣称签名已被验证或更多调用帧已被可靠展开。

若后续确需完整 PAC 感知展开，Microsoft 官方 [StackWalk2](https://learn.microsoft.com/en-us/windows/win32/api/dbghelp/nf-dbghelp-stackwalk2)明确列出 ARM64，并说明可能使用 pointer authentication 的 ARM64 调用者应提供 GetTargetAttributeValue 回调。[PGET_TARGET_ATTRIBUTE_VALUE64](https://learn.microsoft.com/en-us/windows/win32/api/dbghelp/nc-dbghelp-pget_target_attribute_value64)定义 `TARGET_ATTRIBUTE_PACMASK`，也允许返回 LIVE_TARGET 特殊值来表示与采样进程的 PAC mask 相同；需要核实该条件，不能无条件假设所有远程目标相同。两者文档要求 DbgHelp 10.0.22621.4602 或更新版本。本机 SDK 10.0.26100.0 的 DbgHelp.h:900-911、:962-973 已有相应声明；实际 DLL 版本/导出仍应核验。当前未切换此 API，避免在简单符号定位阶段扩大 unwind 行为变更。

#### 9.2 首次管道阻塞实采记录

主会话报告已完成保持连接的 stdout 管道停读复现，并生成 `Build/CanvasFreezeInvestigation/pipe-experiment/blocked-stacks-1.txt`。本子代理只读核验了该原始输出：`:450-453` 的 TID 5192 捕获三帧，依次为 ntdll ZwWriteFile、KERNELBASE WriteFile、原始 pc `0x116a7ff6c306faf0`；`:574` 为 `DONE sampled=111 failed=0 single_pass=1`。每条该线程记录的 suspend_previous=0、resume_previous=1、resume_error=0，表明本次 sampler 归还了自己增加的暂停计数。

主会话将 TID 5192 对齐到 Cursor frame 生产者；低 48 位候选 `0x7ff6c306faf0` 落在 Inkeys 映像范围内，需由新版显示解析和匹配 PDB 确认具体源码位置。当前三帧证明该线程采样时等待同步 WriteFile；尚不把未解析的原始 LR 写成已完整捕获的 WriteFastConsoleLine→Flush→Run 调用栈。Setting 活性、画布停顿/恢复时间和读端条件的完整实验记录由主会话维护，此处不据一份栈输出补造那些观察。

### 10. 后续最小修复边界（仅方案，本轮不实施）

若同步 sink 实验与现场证据成立，修复应落在日志输出所有权：绘制/RTS/窗口/UI 线程只做有容量上限的非阻塞入队，独立 sink worker 消费；溢出显式计数，业务不等待输出。退出不能无限 join 正阻塞的 sink worker，需一并设计可取消/可丢弃的结束方式。单纯加大环形队列、把 WriteFile 改名或批量化、关闭 QuickEdit、给普通同步 handle 传一个伪“超时”均不足以修复各类 sink。

若实际栈落在窗口队列或 COM，按取到的完整等待环修改对应边；保留 owner、激活、捕获和迟到命令语义，避免为消除等待而盲目全面异步化。控制台与纯 stdout 关闭的普通配置还需按原用户顺序回归；不能把一个诊断模式缺口扩大为全部报告的统一解释。

## Files Found

| 文件 | 本轮用途 |
| --- | --- |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Diagnostics.cpp` | Cursor 环形队列、同步 console/file sink、debug timing 与旧宏诊断边界 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` | 实际 flush 消费者与主循环/退出调用点 |
| `Inkeys/IdtMain.cpp` | console 初始化、启动配置及非发布诊断覆盖 |
| `Inkeys/Inkeys/Other/Other.Config.cppm` | 四类 console 开关默认 false |
| `Inkeys/src/i18n/zh-CN.jsonc` | 光标诊断下一次启动才生效的用户描述 |
| `Inkeys/Inkeys/UI/Setting/Setting.cpp` | 普通用户入口、独立重启路径与持久化 |
| `Inkeys/Inkeys/Window/Window.cpp`、`Window.CommandCompletion.h` | 两个窗口 group、owner、队列期限与 Taskbar COM |
| `Inkeys/IdtState.cpp`、`IdtState.h` | mode/owner/presentation 锁边界、whiteboard feature guard |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Product.cpp`、`Draw3.Host.cpp` | productCallMutex、Stop 与 runtime 异常终止 |
| `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp`、`Bar.Button.cpp`、`Bar.Interaction.cpp`、`Bar.RenderLoop.cpp` | Bar 更新、同步业务线程、捕获、直拖锁 |
| `Inkeys/Inkeys/UI/PageControl/PageControl.cpp`、`Inkeys/Inkeys/Display/Display.cpp` | 已有逆序回避/回调锁外分发的反例 |
| `research/thread_stack_probe.cpp` | 本任务独立 native ARM64、单次读取线程栈工具 |

## Related Specs

- `.trellis/spec/native-desktop/errors-logging-and-resources.md`：资源所有权、线程退出唤醒、错误证据边界。
- `.trellis/spec/native-desktop/rendering-and-ui.md`：Window Service 所属线程、Setting deferred lifecycle、共享 UI 渲染与 ImGui 同步约束。
- `.trellis/spec/native-desktop/ui3-render-diagnostics.md`：诊断限频、聚合与非阻塞原则；不可把新观察机制放回热路径的同步 sink。
- `.trellis/spec/native-desktop/draw3-integration.md`：唯一 Draw3 runtime、外部 HWND 与独立设备边界。
- `.trellis/spec/native-desktop/cpp-conventions.md`：先标清实际锁拥有者，最小改动，中文关键注释与编码/换行。

## Caveats / Not Found

- 本子代理未构建/运行 sampler，未执行 GUI，未对产品做任何修复；主会话负责构造 pipe 实验、编译和真实线程采样，结果应独立写实验记录再回链。
- 没有用户现场 full dump/stack、实际 console host/mode 和持久化配置。视频 Edition 与当前源码时间存在差异；任何构造成功仍不足以宣称原始 root 已被定位。
- 当前范围未发现一条完整、由普通用户设置/Explorer 顺序必然触发的永久锁闭环；记录具体缺失的反向边，避免重复把历史修复后的锁图套回当前 HEAD。
- `interactionDisabled`/显式 AttachThreadInput/console mode 设置不存在于当前所搜生产路径；此类“未找到”不等于 Windows 隐式输入队列机制不存在。
- 采样器采用本机 SDK API 的静态检查；本地 PDB 是否与目标匹配、系统 DbgHelp 的 ARM64 unwind 实际深度仍要靠主会话编译运行验证。
