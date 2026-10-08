# F-045 Draw3 接触与控制边界顺序

日期：2026-09-28。阶段：ingress 和独立 Command marker/Controller 批次门候选已有无窗口红→绿；真实 Host/RTS/呈现/保存及用户现场未验证，须独立 diff 审查。不能把用户图片归因于本问题。

## 实际链路与已确认反例

`ContactInputCoordinator` 使用 32 个 Down producer token 和另一个 control token。`moodycamel::ConcurrentQueue::try_dequeue` 会按各 producer 的待办量择取，不提供跨 producer FIFO。原 F-042 失败入队退路按 `slotCapacity` 消费预算合成 control，预算也不标识“control 发布前已接受的 Down”。`DrawingController::processCommand(nullptr)` 只唤醒 `Host::ConsumeBridge`，后者一次排空 Bridge 命令并把它们放入 WindowController canvas queue；Clear/最终自动保存只在 Controller `active.empty()` 时执行。

新增 headless 生产链测试 `TestControlWakeSeparatesAcceptedContacts` 在不同线程按“旧 Down+Up 已接受 → Bridge Clear + 实体/失败 control → 新 Down+Cancelled 已接受”发布，直接用生产 `TryDequeue`/`Bridge::TryConsume` 检查顺序 `old,control,new`。旧实现完整 Debug|ARM64 Solution Build exit 0；`InkeysHeadlessTests --no-window` exit 1，实体 marker 与失败退路两项都失败。日志 `TestResults/release-hardening/f045-ingress-red-*`。这证明当前 ContactInput 不保持该测试轨迹的控制边界；尚未证明 Controller 可见墨迹或用户现场经过此路径。

## Stage 2 ingress 合同与文件边界

仅 `Draw3.ContactInput.cpp` 和 `InkeysHeadlessTests/draw3_contact_tests.cpp`：Down 与实体 control 使用一个 explicit producer token，短原子旗标临界区只覆盖 `try_enqueue` 和成功 Down 计数（第三方 API 的 `try_enqueue` 为 CannotAlloc），不覆盖槽分配、Move/Up/Cancelled、业务锁、模型、GPU 或磁盘。`PublishDown` 只有在 enqueue 成功、accepted 水位已发布、临界区已释放后才返回 true；失败路径仍撤销未排队槽位。由此一个已经返回 true 的 Down 必先于随后发起的实体 control 处于同 token FIFO。

control 入队失败时在相同临界区记录此前**成功入队**的 Down 总数。唯一绘制消费者每取出一个非空接触递增 consumed；`consumed >= captured` 后先合成一次 `nullptr`，再取后续新 Down。消费者停滞时此条件不会被预算越过，整进程退出的 15 秒兜底另由 F-043 管理。成功/失败 marker 的当前合并行为暂未改变；F-042 一次失败仍会唤醒 `WaitDequeue`。输入到队列的 Down 次数上界受 slot 容量限制，但总计数贯穿 Host 生命周期，不以 `size_approx` 推断顺序。

Stage 2 串行复验：完整 Debug|ARM64 `InkeysRepo.sln` Build exit 0、Headless `--no-window` exit 0；旧/控制/新两条断言及 F-042 连续 64 Down/Up/Cancel 同过。日志 `TestResults/release-hardening/f045-ingress-stage2-{build-debug-arm64.log,headless.stdout.log,headless.stderr.log}`。Release/Win32/x64 与真实 RTS 并发压力由总集成矩阵记录。源码保持 BOM/CRLF；因为旧文件已含本任务其它改动，不能把整个文件 diff 都归因于 F-045。

## Stage 3 跨层尚未闭合

即便 `TryDequeue` 给出 `old,control,new`，`DrawingController.cpp:7906,7939` 的同帧 `while(TryDequeue)` 会继续接纳新 Down，然后 `processCanvasCommands` 直到 `active.empty()` 才执行；新 Down 若在前一个 Clear 控制 marker 后进 active，Clear 可能清掉它。控制 marker 到达时如有旧活动接触，本来就必须等待可靠 Up/Cancelled 与最终墨迹提交；暂停**新** Down 不应中断旧终态，不能靠丢样缩短等待。

另一个边界是合并：`PublishControlWake` 已 pending 时后续调用不再留下独立物理 marker，而 `Host::PumpBridgeCommands` 在首次 callback 排空所有 Bridge 命令。若 `Clear A → Down B → Clear C` 在第一次消费前发布，C 可能随 A 提前到 B 前；把第一个 marker 延至最新 Down 之后又会令 A 错误清掉 B。当前仓库全局检索 `EnqueueCanvasCommand` 的生产调用都在 Host，`WindowController::QueueCanvasCommand` 管理实际 deque 并在每次入队后再请求相同 control wake；还须区分这个二次唤醒与原 Bridge marker。因此只修改 ContactInput、只让 Host callback 返回 bool，或在 Controller 每次 null 后无条件等待旧 active 清空，都不能证明所有状态/命令的精确插序；无条件等待也会延迟普通设置/光标通知后的新接触。

多命令 headless 红测 `TestDistinctCommandsKeepTheirIngressBoundaries` 冻结了 `Clear → Down+Up → Undo` 的交错：Bridge 接受两条，旧 `PublishControlWake` 只保留第一条 marker；单 ingress token 修补仍不能给第二条命令一个单独边界。完整 Debug|ARM64 Solution Build exit 0，Headless exit 1，唯一新增失败是 `separate commands retain two physical ingress boundaries`。日志 `TestResults/release-hardening/f045-multicommand-red-*`。后续测试改用实际新 Command 专用入口，另覆盖第二 marker 失败时的序号/水位与 258 预约容量/代际 reset，最终 Headless exit 0。

### 待实施的最小 per-command 协议

1. Bridge `Publish` 的当前唯一产品写入口是 `Host::PublishCommand`（`Draw3.Product.cpp::PublishProductCommand` 转发；Bar/IdtState/Facade 最终走该函数）；最终 `PrepareExitAutoSave` 的唯一入口是 `Host::Impl::Stop` 的 `bridge.StopWithFinalCommand`。全仓其余 `ProductBridge()` 调用只是状态 Snapshot 或 Workspace/Target 发布。Bridge 普通队列容量固定 256，`StopWithFinalCommand` 使用独立 final 槽，故同一时刻待消费命令上界 257。Bridge 自身保持原 FIFO、scene-stamped 身份和 Unsupported gate，不修改 Bridge 文件。
2. ContactInput 增加 Command 专用控制事件，与 Down 共用已建立的单 producer FIFO；普通窗口、光标、显示、自动保存 completion、状态唤醒仍使用当前可合并 General marker。`TryDequeue` 对外仍返回 `record=nullptr`，但单消费者可读取本次 marker 的 `General/Command` 类别；只有 Command marker 才允许 Host 从 Bridge 取**恰好一条**命令。General marker 只处理 completion/state，不偷取 Bridge 命令。成功实体 Command marker 在 Down 间自然 FIFO；其 `try_enqueue` 失败时在预分配 258 项 ring 留下逻辑事件与当时已入队 Down 水位，绘制线程只在此前 Down 全被取出后、之后 Down 之前合成该 Command marker。普通 F-042 General 失败退路保持独立。不能为让命令早执行而跳过旧 Down，也不靠 `size_approx`。
3. Host 的短命令发布 mutex 只覆盖 `bridge.Publish` 与对应 Command marker 提交（均无 GPU/COM/磁盘/窗口调用），使并发调用的 Bridge FIFO 与事件 FIFO 同序。`Stop` 在相同 mutex 下以独立 final 槽关闭 Bridge 生产，再**解锁**执行 RTS `Shutdown`，关闭回调后发布 final Command marker，使最终水位包含所有已接受 Down；绝不把 RTS Shutdown 放在该 mutex 内。
4. 为保证“Bridge 已接受但 marker 失败不能被丢弃”，Host 在调用 Bridge 前先在 ContactInput 原子预约 Command 事件容量；普通命令预约失败即返回 QueueFull，**不会**调用 Bridge。Bridge 拒绝时取消预约；接受后实体 marker 成功或失败 ring 插入都不能失败。最终命令使用第 258 个预约容量。每个 Command marker 被消费者领取时退还一个预约；失败 ring 的占用不可能超过尚未领取预约数，因此 258 项固定 ring 在该合同内不会满。若 invariant 被代码破坏，必须记录并中止该请求或进入受控故障，不得静默丢已接受命令；测试覆盖容量边界与注入失败。普通 General 不占该 ring。
5. Controller 的 `observer.controlWake` 把 marker 类别传给 Host。Host 对 Command marker只 `TryConsume` 一条并按原 `EnqueueCommandScene→CanvasCommand` 进入 WindowController；对 General 处理 completion/state。每个 callback 后可应用 latest state，但 scene-stamped 命令必须优先于 latest 恢复。Controller 只在真实 `WindowController::HasPendingCanvasCommand()` 时建立 barrier：此帧停止继续取 marker 后新 Down；旧 active 继续接收原位快照/Up/Cancel 并正常落定；actual `processCanvasCommands` 排净后再开放输入。普通视觉唤醒没有命令时不延迟新 Down。`WindowController` 只增低频只读队列查询，不更改 HWND/呈现 owner。
6. Host 重启复用同一 `ContactInput` 对象，而当前 `Start` 仅重置 Bridge 与 canvas deque。`Stop` 尾部仍可能留下 General wake。新协议须在无 producer/consumer 的 Start 前显式丢弃旧 ingress 事件并清理旧 generation 的 slot/pending/reservation，之后才 `bridge.Reset`；旧事件不能误消费新 Bridge 命令。需要无窗口代际测试；真实 Stop/Start/画布和自动保存仍列 GUI 人工项。

### 唤醒来源迁移表

| 来源 | 当前入口 | 新事件 | 处理 |
| --- | --- | --- | --- |
| Bar/快捷键/IdtState/Facade 的 Clear/Undo/Redo/翻页 | `PublishProductCommand → Host::PublishCommand → bridge.Publish → PublishControlWake` | 独立 Command | 每 marker 仅一条 Bridge 命令，保留 scene-stamp。 |
| `Host::Impl::Stop` 最终保存 | `StopWithFinalCommand → stylus.Shutdown → PublishControlWake` | 独立 final Command | RTS 停止后捕获水位，最后一条。 |
| `Host::PublishState` 与产品 Workspace/Target | `bridge.PublishState → PublishControlWake` | 可合并 General | 保留最新状态收敛，不提前取命令。 |
| WindowController 内部设置/光标/尺寸/退出 | `RequestControlWake → PublishControlWake` | 可合并 General | 唤醒并读原子请求；有实际 canvas 队列时由 Controller gate。 |
| Desktop/PPT worker completion、显示快照、设置项变化、隐藏测试 contact | `PublishControlWake` | 可合并 General | 保留原 completion/state 处理与 ready 门。 |
| Host 将命令/完成/scene 排入 WindowController canvas deque 后的二次唤醒 | `EnqueueCanvasCommand → RequestControlWake` | 可合并 General | 实际 deque 由 Controller 读取；不得再次消耗 Bridge 命令。 |

上述合同已在 ContactInput `.cpp/.cppm`、Host `.cpp`、DrawingController `.cpp/.cppm`、WindowControl `.cpp/.cppm` 与无窗口测试实现；没有修改 Bridge、渲染器、保存 worker 或 GUI。`Host::PublishCommand` 在 Bridge 接受前预约，在相同短 mutex 下发布独立 Command marker；Stop 的最终 marker 位于 RTS Shutdown 后。Command 失败入队走固定 ring、consumer 按旧 Down 水位及 Command ordinal 恢复；General marker 保持 F-042 低资源合并。`Host::ConsumeBridge` 的 General 仅泵 completion/state，Command 恰好泵一个 Bridge 命令。Controller 的 `DrainIngressBatch` 及单条等待路径在真实 canvas deque 有待办时 gate 后续 Down，旧 active 仍推进，排空命令后释放；没有命令的普通状态/光标 wake 不 gate。

Controller 无窗口生产探针 `RunDraw3ControlFenceProductionProbe` 使用真实 ContactInput + WindowController deque，且调用 `Run` 三处批次循环共用的 `DrainIngressBatch`。旧 helper 同帧吞入新 Down 时，以 `Start-Process -WindowStyle Hidden -PassThru` 精确等待 GUI 子系统 EXE：pid 55948 exit 1，stderr 为 `Run ingress batch stops before new Down while Clear is queued`；修补后 pid 44012 exit 0、stderr 空。PowerShell 直接 `&` 对此 GUI subsystem EXE 曾立即返回陈旧 `$LASTEXITCODE=0`，**不是**有效红/绿证据；有效记录为 `f045-control-fence-{red,green}-explicit.*` 和父任务验证 ledger 的 PID/exit。最后一次完整 Debug|ARM64 Solution Build exit 0（`f044gc-f045fence-build-debug-arm64.log`），Headless `--no-window` exit 0（`f045-control-fence-green-headless.*`）。所有构建/测试由主 agent 串行执行，本 subagent 没有并行启动 MSBuild。

## 自审和剩余验证

- 顺序证明覆盖“`PublishDown` 返回 true → Command wake 发布 → 新 Down 返回 true”：Down 成功入队并发布计数后才返回；同 token FIFO 确保实体 marker 位置。失败 marker 的固定 ring 只在已成功入队 Down 计数达到水位且此前 Command ordinal 已消费时合成；不在资源失败时按 `slotCapacity` 越序。普通 General fallback 同样改为成功入队水位。
- `Host::PublishCommand`、`StopWithFinalCommand` 是唯一 Bridge 命令写源，短 mutex 只包 Bridge 接受、Command 预约/发布，不包 RTS Shutdown、COM/GPU、窗口调用或磁盘；Bridge 容量 256+final 与固定预约 258 使 fallback ring 满在当前合同下不可达。预约满的普通命令在 Bridge 接受前返回 QueueFull，最终预约失败则关闭 Bridge 并报告没有最终屏障，不伪造成功。若未来 Bridge 容量改变，须同步复核 final 保留容量。极端内部 ring invariant 破坏调用 `std::terminate`，不能把它当普通失败恢复路径。
- `Host::Start` 在旧 producer/consumer 停止前提下先 `ContactInput::ResetForNextRun` 再 `bridge.Reset`；它排空旧队列、清旧槽位与 pending/reservation，避免旧 `window.RequestExit` 尾 marker 命中新一代 Bridge。Headless 直接验证旧代实体 General+Command 被清、新 Command 仍可领取；实际 Host Stop/Start 需要隔离 GUI/真机复验。
- Controller gate 仅在 `HasPendingCanvasCommand()` 真时保持；因 QueueCanvasCommand 的来源目前都在 Host，包含 Bridge 命令、恢复 completion 与目标场景收敛。旧 active 的 Up/Cancelled 已由 contact 快照更新，gate 只暂停**新 Down 出队**；旧活动笔正常模型提交与 `active.empty()` 后命令执行未在此无窗口探针内实际运行。长时间未终态的旧 contact 仍可能延迟 Clear，且被 gate 的新 Down 可能占用槽位；不得用丢样或提前 Clear 掩盖，真实设备手感/多接触压力未验证。
- Headless 测了 ContactInput/Bridge 逻辑和 Controller 同用批次 helper；没有实例化完整 Host、RTS、Renderer、Presenter、AutoSave worker 或 HWND，也没有验证 GUI 下 Clear 像素、Exit 最终 snapshot durable、PPT/多屏页面身份。F-043 的 15 秒强制退出只保证可终止旧进程，不是这些工作完成的证明。
- 新 `std::atomic_flag` 只串行低频 Down/control 的 `try_enqueue`（第三方 CannotAlloc），Move/Up/Cancelled 快照路径不持该锁；具体 Down 尾延迟、并发高压下自旋/优先级反转与是否达到 Inkeys2/Canary 手感仍未测，不得报性能 PASS。Release、Win32/x64、Win7 SP1+KB2670838 真实运行尚待最终矩阵。

因此 Stage 2 绿灯只说明 ContactInput 边界，不足以把 F-045 整体记为已修复。最终仍需复查同帧 drain、多个 Clear/Undo/Exit、窗口直接命令、活动终态、scene-stamped PPT 与 15 秒强制退出之间的时序。
