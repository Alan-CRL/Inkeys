# F-042 Draw3 control wake 入队失败后的活性修补

日期：2026-09-28。状态：生产代码候选已实现，Debug|ARM64 完整 Solution 与现有无窗口套件通过；独立 diff review、真实 Host/窗口路径和用户现场仍待验证。本文件不把截图症状归因为此问题。

## 确认的缺口与边界

`Host::PublishState/PublishCommand` 先更新 `StateBridge`，再调用 `ContactInputCoordinator::PublishControlWake`。旧实现于唯一 control producer token 的 `try_enqueue(nullptr)` 失败时，虽然递增 wake generation 并设置 event，却把 `controlWakePending` 清回 false。绘制线程只有从输入队列取到空记录时才运行 `DrawingController::processCommand(nullptr)` → `Host::ConsumeBridge`；`WaitDequeue` 还只等队列自身的 semaphore。因此无后继成功 marker 时，已接受的 Clear/选择状态可滞留。`Host::Stop` 的 `StopWithFinalCommand(PrepareExitAutoSave)` 使用同一唤醒，随后可在 `exitAutoSaveCondition.wait` 等不到 final command 回执。代码路径成立，但没有用户现场的线程栈或 enqueue 失败计数，不能确认现场已走此分支。

## 修改的生产符号

- `Draw3.ContactInput.cppm` / `.cpp`：`FailNextControlWakeEnqueueForTesting()` 是显式、一次性、仅针对 control token `try_enqueue` 的无窗口故障注入；默认关闭，普通 UI、配置、消息或命令均无入口可触发。未更改 Down/Move/Up/Cancelled 入队与快照语义。
- `ContactInputCoordinatorImpl::PublishControlWake`：control marker 成功入队后才发 event；失败时保留逻辑 pending、记录低开销失败计数，发布 `controlWakeFallbackPending` 后发 event。同一待办期间的后续通知只合并，桥接请求仍由原 owner 消费。
- `ContactInputCoordinator::TryDequeue`：优先取原队列输入记录；队列空时直接返回唯一空记录。持续新输入使队列不空时，最多消费 `slotCapacity` 条后在同一绘制线程返回唯一空记录，以免控制请求无限饥饿。没有重投物理 marker，因此也没有晚到 marker 的重复回调。`AcknowledgeControlWake` 仍在原 `processCommand(nullptr)` 里先清 pending、再复查全部请求。
- `ContactInputCoordinator::WaitDequeue`：用已有 wake event / generation 与 `TryDequeue` 双重复查，消除只等 queue semaphore 而忽略失败 event 的无限等待窗口。Event 创建/等待异常时 `Sleep(10)` 只作为有限退路；正常路径完全阻塞，不忙轮询。`HasPendingWork` 包含该 fallback。
- `ContactInputDiagnosticsSnapshot` 增加 `controlWakeEnqueueFailures` 与 `controlWakeInlineRecoveries`，仅在失败或成功退路边沿写原子计数；没有热路径打印、新线程、磁盘、GPU 或跨线程同步等待。

`InkeysHeadlessTests/draw3_contact_tests.cpp` 调用生产 `ContactInputCoordinator::{TryDequeue,WaitDequeue}` 和生产 `Bridge::StateBridge`。它覆盖一条已排队 Down/Up 后的 Clear/选择、一条无后继 marker 的 Stop final barrier、阻塞消费者只靠失败 event 唤醒，以及连续 64 个 Down 和交替 Up/Cancelled；检查控制消费恰一次、输入最终全消费及送达预算。测试没有复制队列调度算法。Headless 工程没有链接完整 Host/Controller，故它验证必须的输入/bridge 条件，不能当作真实 `Host::Stop` 和 UInk durable drain 通过。

## 红灯、修补与复验

| 冻结代码 | 串行验证 | 结果与证据 |
| --- | --- | --- |
| 只加一次性注入和生产入口红测，未修调度 | Debug|ARM64 `InkeysRepo.sln` 完整 Build；`InkeysHeadlessTests --no-window` | Build exit 0；测试 exit 1、3 项失败：Clear/选择、Stop final、阻塞消费者。`TestResults/release-hardening/draw3-control-wake-red-headless.stderr.log` / `.stdout.log`。 |
| 第一版从消费者补投物理 control marker | 同一配置与套件 | Build exit 0；测试 exit 1，连续新输入的合并断言失败。旧 marker 属另一 producer token，不能依赖跨 producer 全局公平取得；该版本未计入通过。`draw3-control-wake-green-headless.stderr.log` / `.stdout.log`。首次失败未打印各子计数，无法仅凭该合并断言断言是哪一个子条件。 |
| 当前修订版：只在同一消费者执行有界 fallback，不补投物理 marker | 同一完整 Debug|ARM64 Solution Build 和 `InkeysHeadlessTests --no-window` | Build exit 0，Headless exit 0、`PASS animation correctness`。`draw3-control-wake-final-build-debug-arm64.log`、`draw3-control-wake-final-headless.stderr.log` / `.stdout.log`。新增失败诊断只在断言失败时输出详细计数。 |

以上日志位于 `TestResults/release-hardening/`。本单元另执行 `git diff --check`，通过；`Draw3.ContactInput.cpp/.cppm` 仍为 UTF-8 BOM+CRLF，测试文件仍为无 BOM UTF-8+CRLF。已有第三方/旧源码 warning 未为本单元改动。

## 仍需审查和验证

- `moodycamel::BlockingConcurrentQueue` 的多 producer 取出顺序本来不保证严格全局 FIFO；本修补保持真实输入记录的队列取出顺序且不丢弃 Down/Up/Cancel，但在持续资源失败、`slotCapacity` 预算到达时，合成的控制边界不能证明位于每条跨 producer 旧 Down 之后。`size_approx` 没有被用作事务水位。若发布要求严格的跨 producer Clear/状态边界，需要独立的可证明序列水位或单一 ingress 顺序合同，不能把本测试写成已证明该性质。
- 无窗口测试没有启动 Host、RTS、GPU、Window Service 或 UInk worker，真实 Clear 可见时间、活动 contact 收尾、Stop 回执及 worker durable 状态仍需独立整链验证。GUI 现场根因未确认；Window Service 同步隐藏等待是另一条件性风险。
- 仅 Win11 ARM64 上取得构建/无窗口证据；Win7 SP1+仅 KB2670838、FL11.0 Hardware 与无 FL11.0 WARP 的 FLIP/DComp/ULW 组合均未真机运行。本单元没有触碰 presenter 或系统基线。
- 未进行专门的 ContactInput 正常路径性能采样；新增原子读取和 event 等待的延迟/CPU 成本应纳入后续 Draw3 基准，不以这次测试通过推断流畅度改善。
