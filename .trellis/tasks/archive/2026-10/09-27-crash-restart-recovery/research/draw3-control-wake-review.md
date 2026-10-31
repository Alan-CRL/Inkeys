# F-042 Draw3 control marker 入队失败修补独立复审

日期：2026-09-28。本人未实施此补丁；仅只读 `Draw3.ContactInput.cpp/.cppm`、Host/Controller 消费链、`InkeysHeadlessTests/draw3_contact_tests.cpp` 实际 H0→工作树 diff、任务/规范和已有日志。没有构建、运行 headless/GUI、采样性能或复现用户图片。结论只针对**一次 control-token enqueue 失败后的活性**；原报告中的 Window Service 同步隐藏等待是另一个独立候选。

## 结论

F-042 的**原始失唤醒链在单消费者生产路径上静态闭合**，未发现本次修改新引入的确定 P1/P2：`PublishControlWake` 成功时先入队后发 event，失败时先发布 fallback pending 再发 event；`TryDequeue` 消费真实记录之后可以在绘制线程恰一次合成 `nullptr`，`WaitDequeue` 改从 wake generation/event 再检查真实队列与 fallback。无窗 red→green 是主 agent 的运行证据，证明所测 ContactInput/StateBridge 子链；**没有证明 Host::Stop/Clear、GPU/RTS、HWND 或用户现场已恢复**。

仍有一个须维持为发布边界的**既存条件性 P1 顺序风险**：多 producer 的 ingress queue 没有可证全局 FIFO，旧成功物理 control marker 与新 synthetic fallback 都可能在某个发布较早但尚未取出的 Down 前被消费。F-042 的 `slotCapacity` 预算保证合成控制不被连续新输入无限饿死，不证明 Clear/退出屏障与所有先前物理接触的顺序。此风险不是本补丁回归，当前无窗测试也没有强制该对抗调度。若发布合同要求“Clear/Exit 不越过任何已接受的旧 Down”，需独立序列水位/单 ingress 顺序或等效可证明门禁；不能仅以预算、`size_approx` 或本轮绿灯推断无墨迹丢失。

## 逐边界核对

| 边界 | 当前源码和具体判断 |
| --- | --- |
| 发布顺序、空闲唤醒 | `ContactInput.cpp:347-377::PublishControlWake` CAS 把 `controlWakePending` 置 true 后，成功 `queue.try_enqueue(controlProducerToken,nullptr)` 才 `SignalWake`；失败则先存 `controlWakeDrainBudget=slotCapacity`、`controlWakeFallbackPending=true`（release），再增失败计数、`SignalWake`。`SignalWake` 先 `wakeGeneration.fetch_add(release)` 后 `SetEvent`。消费者 acquire 读 generation/fallback，不会把尚未入队/尚未发布的控制项误作已可见；即使消费线程在 CAS 后、fallback store 前醒来，成功/失败出口还会再发一次 wake。已有 pending 的合并调用继续 `SignalWake`，同一业务待办无需第二个物理 marker。失败后 `controlWakePending` 不再被旧逻辑清 false。 |
| `TryDequeue` 的恰一次回调和真接触 | `ContactInput.cpp:833-850` 先检查 fallback 且预算已零，满足时 `TryTakeUnqueuedControlWake` 用 `exchange(false,acq_rel)` 只给一个消费者返回 `record=nullptr`；否则先从真实 queue 出队。每取一个非空 contact 在 fallback 期间递减预算，队列为空时无需用尽预算也合成一次 null。预算初值 `slotCapacity`，所以在持续真实出队的条件下最多再取该数目后必须给控制机会；合成不从 queue 删除 Down/Up/Cancelled，不补投稍后会重复的物理 marker。Controller 的唯一 null 路径仍 `AcknowledgeControlWake` **先清 pending** 再 `Host::ConsumeBridge`（`DrawingController.cpp:4717-4725`、`Host.cpp:1015-1024`），重复合并请求在这一回调看到最新 bridge/window 请求；若新发布发生在 Ack 之后会形成下一次 marker/fallback。这个恰一次证明依赖产品的单消费绘制线程合同，不涵盖两个线程同时调用 `TryDequeue`。无壁钟上界：GPU/模型/RTS 或 Controller 本身卡住时，再好的队列分支也不会执行。 |
| `WaitDequeue`、活动帧及 `HasPendingWork` | `ContactInput.cpp:852-874` 在每轮先捕获 generation、`TryDequeue`，若 generation 变化立即重试，否则等同一个 auto-reset event，event 创建/等待失败才 `Sleep(10)` 有限轮询。失败 fallback store→SignalWake 的顺序使 `WaitForSingleObject(INFINITE)` 不再只依赖 queue semaphore；若 SetEvent 在检查与 wait 之间发生，event 保持 signaled，若更早发生则 generation 比对重试。Controller idle/异常 Hold 路径实际调用 `WaitDequeue`（`DrawingController.cpp:7835-7885`）；其它帧用 `WaitForWake`/`WaitForFrameDeadline`，wake generation 同样能退出等待进入下一轮。`HasPendingWork` 已包含 fallback bool，旧物理 queue 空也不误报 idle。活动帧不是由 `WaitDequeue` 单独驱动，因此无窗阻塞消费者测试只覆盖一个必要分支。 |
| stop final 与其它等待 | `Host::Stop` 在桥接 `StopWithFinalCommand(PrepareExitAutoSave)` 后关闭 RTS producer，再调用 `PublishControlWake` 并等待 `exitAutoSavePrepared || !running`（`Host.cpp:1320-1337`）。本补丁修复了**唯一 control marker 失败且绘制线程仍消费输入**时的到达性；bridge final 仍需 `Host::PumpBridgeCommands` 变成 CanvasCommand，再等 Controller `active.empty()` 才由 `processCanvasCommands` 出队（`DrawingController.cpp:6550-6562,7793-7799,8743-8745`）。RTS Shutdown/`CloseAllProducerContacts`、活动 contact 终态、Presenter/GPU 调用、AutoSave worker drain 与 Window Service owner 都有独立等待；F-042 不能保证任意卡死均退出。 |
| 默认测试入口/诊断 | 一次性 `FailNextControlWakeEnqueueForTesting` 只设置对象内原子 bool，构造默认 false；生产检索没有 CLI、配置或普通 UI 调用它。注入只令下一次 `controlProducerToken` 入队短路，不更改 Down/Move/Up/Cancelled 发布。方法仍存在于产品模块公开接口，属于可调用测试 seam，**用户入口不可达是当前调用图事实**，而非编译期删除。失败和 inline recovery 两个计数在对应稀疏边沿无条件加原子；生产 `HostRuntimeSnapshot` 当前没有透传它们（`Host.cpp:1404-1409` 只拷贝 Down/Move/Terminal/Recycled），也无失败边沿日志，现场仍不能直接用 Host 快照证明 fallback 曾触发。这是诊断覆盖缺口，不影响本轮必要活性逻辑。 |

## 多 producer 顺序的实际限制

`StateBridge::StopWithFinalCommand` 保证 **bridge 命令队列内部** 的已接受命令先于最终保存屏障（`Bridge.cpp:145-157,177-196`）；`Host::PumpBridgeCommands` 将每条 captured scene 与命令按顺序放入 WindowController 的 canvas queue（`Host.cpp:964-985`）。F-042 没有改变这两条 FIFO。

它们并不能为物理 ingress 的各 producer token 赋全局顺序：`ContactInput.cpp:303-307` 以多个 Down producer token + 一个 control token 建队列，Controller 到达 synthetic null 后立刻调用 Host pump。若更早接受的某个 Down 尚在别的 producer 队列，而较新 Down 被连续取出满 `slotCapacity` 预算，`TryDequeue` 在再次查看 queue 前先返回 synthetic null。随后 Controller 若 `active.empty()` 会处理 Clear 或 `PrepareExitAutoSave`，旧 Down 之后才出现。旧成功入队的物理 control marker 同样来自独立 producer，所以此前也没有严格证明“控制必在所有先前 Down 后”。这不是“此补丁丢弃接触”的结论，而是**接触与命令相对顺序未获保障**：Clear 后可能处理原本应归属 Clear 前的笔迹，Exit snapshot/worker drain 可能先于该接触。最低限度应以双 producer 可控调度注入重现；若合同强制旧 Down 先于控制，最小正确修正须在接受 Down 与发布控制之间建立可证明水位，consumer 仅在此前已接受 contact 全部归类/终态后执行破坏性命令，并保持新输入有界隔离。不能靠无限等 Up 或直接清 active 代替。

## 红绿证据与未覆盖项

- `InkeysHeadlessTests/draw3_contact_tests.cpp:100-270` 直接调用生产 `ContactInputCoordinator::PublishDown/Up/Cancelled/TryDequeue/WaitDequeue/FailNextControlWakeEnqueueForTesting/AcknowledgeControlWake` 和真实 `StateBridge::Publish/StopWithFinalCommand/TryConsume`。一组先排 Down+Up 再失败唤醒，核终态不丢、Clear 与 selection 一次；一组空队列 final 屏障；一组阻塞消费者只靠失败 event 唤醒；一组 64 条连续 contact/交替 Up/Cancelled，核全部消费与控制次数/预算。它**手动**消费 bridge，不调用完整私有 `Host::ConsumeBridge`、`DrawingController::Run`、真实 AutoSave worker，因此不能称已验证最终保存回执或页面可见性。连续 contact 测试没有强制对抗性跨 producer dequeue 顺序。
- 主 agent 已运行的受限记录：red 完整 Debug ARM64 Solution Build exit 0、Headless exit 1 且 stderr 三项 FAIL（Clear/selection、final、阻塞消费者）；首候选 Build exit 0、Headless exit 1，stderr 为持续输入合并断言 FAIL（具体子条件旧日志未展开）；最终 Debug ARM64 Solution Build exit 0、Headless `--no-window` exit 0，stdout 末行 `PASS animation correctness`。来源为 `TestResults/release-hardening/draw3-control-wake-{red,green,final}-headless.*` 与 `draw3-control-wake-final-build-debug-arm64.log`；这些是主 agent 运行证据，我未重跑。未运行真实 Host/Windows owner、Win7 SP1+KB2670838、Hardware/WARP、PPT/Office、真实强制队列压力或用户截图现场。
- `git diff --check` 对 `Draw3.ContactInput.cpp/.cppm` 与 `InkeysHeadlessTests/draw3_contact_tests.cpp` 退出 0；字节检查两份 ContactInput 为 UTF-8 BOM+CRLF，测试为无 BOM UTF-8+CRLF。测试文件还含本轮其它工作新增的 `BenchmarkContactMove`，未执行也不归因于 F-042 活性证据；未采样性能。

复查命令：`git diff -- Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.cpp Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.cppm InkeysHeadlessTests/draw3_contact_tests.cpp`；`rg -n 'PublishControlWake|TryTakeUnqueuedControlWake|TryDequeue|WaitDequeue|AcknowledgeControlWake|StopWithFinalCommand|processCanvasCommands' Inkeys/Inkeys/Drawing/Draw3 InkeysHeadlessTests/draw3_contact_tests.cpp`。
