# 设置页直接退出/重启意图起点独立设计复审

审查时点：2026-09-28 23:21:26 UTC。未冻结工作区 Git blob：`Setting.cpp=3044ea60263d879ab0b66a404bf8ed38d2ba5707`、`Helper.CrashHandler.cppm=30ff2119b9e5ae067f35c54c18a1551e87572a77`、`IdtMain.cpp=fb5b1bf222d44e1a63668a73e099a8803fcdb4e6`、`ShutdownSupervisor.cpp=8301a55fbcd3bbeddfeb83f51303364bbe83512b`、`RenderPipeline.cpp=d753be32ffb59786ede456388c7efcd657b1e712`、`Window.cpp=f0107639bd39740eae521a094759e4f99b50358e`。本轮只读 `setting-exit-intent-deadline-design.md` 和实际调用链；另一 worker 占用构建槽，未修改产品/共享任务文档、未运行构建/GUI/采样。用户截图没有线程栈，本链是独立源码风险，**不是已证实的现场唯一根因**。

## 静态确认的入口顺序

- 直接设置页按钮 `Inkeys/Inkeys/UI/Setting/Setting.cpp:1570-1589` 在 ImGui/Setting 渲染协程内先 `Setting::Hide()`，再调用被 `:687-688` 宏改写的 `QueueRestart/QueueClose`。两个 wrapper 目前在 `:458-466` 仅将 Restart/Close 节点放入单一 `SettingBusinessQueue`。worker `:163-190` 一次只执行一项，旧命令从 FIFO 取出后释放队列锁再执行。`Execute:197-351` 可等待磁盘 JSON 写、ShellExecute、用户信息/确认框、更新目录、DDB 启停等；旧任务若不返回，直接按钮虽已有 UI 反馈，正式 `CloseProgram/RestartProgram`（`:251-256`）仍未调用。因而 `IdtMain.cpp:256-278` 的 `SetOffSignal` CAS、Window `BeginShutdown` 与独立 15 秒监督尚未 Arm。该时序缺口是源码确认，未对特定旧 I/O 做故障注入。
- `Setting::Hide()`（`Setting.cpp:8537-8547`）自身只在短状态锁下改可见意图、排一个 `HideWindow` 节点并请求 RenderPipeline；worker 的旧长 I/O **不持有**业务队列 mutex（`:167-177` 出队后已解锁），所以旧 FIFO 阻塞不会挡住这一步。Hide 仍先于正式请求，不能严格把 ImGui 点击时间等同 watchdog 起点；状态锁/queue 锁或调度器若本身卡住仍是独立条件风险。全局正式入口建立后另走 Window Service 的 `RequestHideAllUserWindows`，无需依赖 Setting worker 执行旧 Hide 节点。

## 对实施候选的判断

**赞成只把 `QueueClose/QueueRestart` 两个无确认 wrapper 改为显式 `::CloseProgram()/::RestartProgram()`**，并改 `RunSettingSession` 附近“所有潜在阻塞业务都入 FIFO”的过时注释。宏定义位于 wrapper 之后的源码行，`::` 明确调用已 import 的全局函数，避免递归/误路由。这样不改按钮、业务队列、COM/Window/Storage owner 或确认框；旧 worker 即使停在文件 I/O，点击线程也能争取 `offSignalInterop` 并建立 supervisor。高优先级再排同一 worker 或仅调整 FIFO 顺序都无法抢占已经执行且卡住的旧任务。

- `Helper.CrashHandler.cppm:61-71` 的正式入口调用 `SetOffSignal(1/2)`，然后请求 owner 异步隐藏；`IdtMain.cpp:260-277` 的第一次 CAS 决定 Close/Restart、调用 Window `BeginShutdown`、`ArmShutdownSupervisor`、发布 offSignal 并唤醒渲染。`ShutdownSupervisor.cpp:265-383` 的 ArmCore 在 CreateProcess/最多 2.5 秒握手前启动自身截止线程，外部 helper 等精确旧 HANDLE 死亡后才允许**唯一**重启。重复点击或 worker 后续调用旧 Restart/Close 因 CAS 败不创建第二 helper。设置页直接按钮没有确认，故不需把「用户取消」保留到 Arm 前；用户选择语言后的 `ConfirmRestart` 必须继续等实际 OK。
- **渲染线程代价**：直接 wrapper 在共享 UI3/Setting render callback 内同步执行完整 `SetOffSignal`。其中 CreateProcess 没有单独超时，随后握手等待上限 2.5 秒；`CrashHandler::Shutdown` 和 `StopMagnifierCoordinator` 也可等待。本次主张以已建立的自身 15 秒兜底约束这些等待，不把点击后 Bar/Setting 仍持续流畅作为保证。`RenderPipeline::WakeForStop` (`RenderPipeline.cpp:742-745`) 只 `SetEvent`，没有回调锁；没有从此路径找到必然的同线程重入死锁，但真实 UI3/Window owner 并发仍需隔离 GUI 验收。若产品要求点击回调完全不阻塞，需另设计「快速 CAS+自身 deadline」再异步 helper 的窄接口；直接另起无监督线程投递结束请求会重现未 Arm 窗口，超出两 wrapper 最小 diff。
- **保存顺序**：改前旧 Setting 写命令一定先于队列里的 Restart/Close；改后正式意图可能先于这些写入。但主退出仍在 `IdtMain.cpp:2752-2755` 调 `Setting::Shutdown()`，后者 `Setting.cpp:8510-8522` 等 session drain、`SettingBusinessQueue::Stop()` 的 jthread join；worker `:170-176` 在 stopping 后继续排空既有命令。正常收尾仍尝试 FIFO 写完，超过 15 秒才按用户既定硬退策略放弃未 durable 请求。不能把“已入队配置”称为“已持久化”，也不能为了保持旧写盘先后再把监督器放回 worker 后面。

## 仍可被 worker 阻塞的入口与非入口

| 入口 | 当前/建议时序 | 结论 |
| --- | --- | --- |
| 设置页“关闭软件”“重启软件” | 现为 Hide→QueueClose/Restart→worker→正式入口；建议 Hide→wrapper 直接正式入口 | 已确认 FIFO 前置堵塞；两 wrapper 最小改能消除这一层等待。若需从点击瞬间严格计时，还要检查/调整前置 Hide 与点击回调本身。 |
| 语言变更 `QueueConfirmRestart` (`Setting.cpp:1905-1916`) | 旧 WriteSetting→同 worker MessageBox→用户 OK→worker 立即 `RestartProgram` (`:228-249`) | 仍可被旧 worker 工作挡在**确认之前**；未确认前不应 Arm。确认框返回 OK 后同一个 Execute 直接调正式入口，中间不再排 Restart 节点；取消/失败不拉起。 |
| `SettingBusinessKind::Restart/Close` 旧分支 | `Execute:251-256` 仍在 worker | wrapper 改为直呼后本仓搜索无其它生产者；保留枚举/分支不会影响两个按钮，但未来调用者若再入队仍有同一风险，应在最终 diff/静态搜索中注明或限制。 |
| `RestartDdb`/`StartAutomaticUpdate` | DDB 只重启辅助插件；自动更新先由 worker 启动 detached 更新线程，`Net.Update.cpp:733` 在后续更新条件下调正式 `RestartProgram` | 不是无确认的设置页直接 App Restart 按钮；旧 worker 可推迟更新线程启动，但尚未完成更新和确定正式重启意图，不能把它冒充本次按钮路径已修或未修。 |
| Setting 窗口 WM_CLOSE/SC_CLOSE | `Setting.cpp:632-643` 只 Hide 设置窗 | 不是关闭整个 Inkeys，不应 Arm 15 秒进程退出监督。Bar 主栏的确认式关闭和未编译 `IdtFloating` 属另一调用链。 |

## 验收与未测范围

1. 代码冻结后静态搜索两按钮宏、wrapper 与全部 `SettingBusinessKind::Restart/Close` 生产者；确认点击线程在旧业务命令仍执行时能先进入 `SetOffSignal`，并只由首次 CAS 决定 Close/Restart。`Hide()` 的锁/队列请求不能被误认成 Window owner 已隐藏或监督器已建立。
2. 无 GUI 的现有 `--shutdown-supervisor-tests` 可证明正式入口的重复请求/精确旧 HANDLE/15 秒后死亡，但**不能证明设置按钮本身**及时调用。可在独立任务根提供可控 SettingBusinessQueue 旧 I/O gate，并让生产 wrapper 从隔离线程提交，记录 worker 未放行前 `offSignalInterop` 已变、supervisor 已 Arm、然后放行 worker；另测前置 Hide 不阻塞。测试必须调同一生产 wrapper，不能复制一个布尔决策。
3. 在用户允许 GUI 后仅点击自建 PID/HWND 的设置页按钮，旧 worker 用私有配置/I/O hook 卡住；记录按钮输入时间、第一次 SetOffSignal/Arm、双窗隐藏、旧 PID 15 秒边界、Restart 至多一个新 PID、已提交与未提交配置的分界。目标 HWND 未命中即停，不操作用户已有程序。真实 Win7/用户截图原因、RTS/COM/worker drain 卡住及强退时新 UInk durable 状态仍需单列，不能由按钮反馈或静态代码宣布全链 PASS。

## Verification

本 reviewer 只读上述 Trellis 设计、Setting/正式出口/RenderPipeline/Window/监督器源码与调用点；未改产品/测试，也未运行构建、lint/type-check、GUI 或性能采样。当前 blob 仅为设计审查时点，实施和最终验证须用新指纹重做相关 diff 审查。

## 2026-09-28 23:30 UTC 增量复核：F-059 冻结源码

- `Inkeys/Inkeys/UI/Setting/Setting.cpp` 新 Git blob 为 `aba73fd320d244291b026fad8191c16e27af7f50`。`:458-468` 的两个 wrapper 现在分别显式调用 `::RestartProgram()` / `::CloseProgram()`，没有入 `QueueBusiness`；`:682-690` 只修作用域注释并保留 `#define RestartProgram QueueRestart` / `#define CloseProgram QueueClose`。两个 wrapper 定义在宏之前，且显式 `::` 指向已 import 的全局 `Inkeys.Helper.CrashHandler` 导出；按钮 `:1572-1590` 位于宏作用域，调用确实展开为这两个 wrapper。因此按钮顺序为 ImGui 接受→`Setting::Hide()`→全局正式入口，不经过 SettingBusinessQueue worker 执行旧 I/O 后才 Arm。
- `Helper.CrashHandler.cppm:61-71` 的全局入口仍先 `SetOffSignal`；`IdtMain.cpp:256-278` 首次 CAS→Window BeginShutdown→ArmSupervisor→发布 offSignal，之后才进行可能卡住的收尾。旧队列 `SettingBusinessKind::Restart/Close` 的 Execute 分支 `Setting.cpp:251-256` 仍存在，但当前源搜索无生产 `QueueBusiness({Restart/Close})`，不会重新拦截这两个按钮。语言变更 `QueueConfirmRestart:453-455,1917` 仍由 worker 弹确认，`Execute:228-249` 仅在 OK 后直呼全局 Restart；取消/失败不 Arm。`RestartDdb` 是辅助插件而非本程序重启。
- 未见新增产品阻断：直接 wrapper 不持 SettingBusinessQueue mutex，也不在 worker 的 I/O/prompt 后排队；重复按钮由 SetOffSignal 首次 CAS 去重。`Setting::Shutdown` 仍等渲染会话并让 worker drain 既有命令，15 秒硬退可失去未 durable 设置，这与设计/用户决定一致。仍需如实保留两个条件边界：按钮先执行的 `Setting::Hide()` 是短锁/入队但不是零耗时；全局 `SetOffSignal` 在共享 UI3 渲染回调内同步建 helper，CreateProcess 无单独时限，握手上限 2.5 秒，之后其它清理可阻塞但自身 deadline 已建立。源码未显示必然的同线程重入死锁，不等于真 GUI 响应已通过。
- 静态检查：`git diff --check H0 -- Inkeys/Inkeys/UI/Setting/Setting.cpp` exit0；当前文件无 BOM、只有 CRLF 行尾（无 bare LF）。本 reviewer 未运行构建、Headless、15 秒 suite 或设置页 GUI；旧 supervisor 无 GUI 绿证只能证明全局入口，不证明此次按钮宏展开后的点击时间和 worker 堵塞交错。建议按上节隔离测试方案补精确自建进程/HWND 的按钮证据，并在最终源码指纹后复验。
