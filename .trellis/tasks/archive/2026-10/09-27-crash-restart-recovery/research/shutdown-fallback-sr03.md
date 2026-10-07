# SR-03：监督器创建/握手失败时的自身截止兜底

日期：2026-09-28。F-046 的独立 helper 在正常握手后已有 15 秒绝对截止；但 `CreateProcessW`、握手或 helper 早退可能留下正在 `jthread::join`/Window/保存等待中卡住的旧进程。原 `ArmCore` 在失败时只返回 `Failed`，生产 `SetOffSignal` 记一条日志后继续清理，不能据此保证有限退出。

## 最小低层设计

在首次有效 Close/Restart/CrashRestart 意图通过参数检查后，先取 `GetTickCount64()+deadlineMilliseconds`，用 `CreateThread` 启动本进程内唯一兜底时钟，再尝试外部 helper。线程只读原子 deadline、用 `Sleep` 等剩余时间、到期仅 `TerminateProcess(GetCurrentProcess(), 独立退出码)`；不访问业务对象、CRT、logger、文件、COM 或 GPU，不 join 卡住线程。正常进程先结束时 OS 清理该线程。外部 helper 成功时它仍是唯一 launcher；两个时钟都只能结束**同一个本进程**，兜底绝不启动新实例。外部 helper 创建/握手失败但自身时钟已建立时返回新增 `FallbackArmed` 与原 helper 错误码；此时 Close 最终可强退，Restart/CrashRestart **不能**承诺新实例拉起。两者都失败时仍返回 `Failed` 并保留发布缺口。

这条兜底应在 `CreateProcessW` 前启动，覆盖进程创建调用本身阻塞的情况；从请求接收时的同一单调 deadline 计算，不能在握手失败时重置为另一个 15 秒。`CreateThread` 仅对不调用 CRT 的低层 ThreadProc 适用；Windows 7 SP1+仅 KB2670838 可用是 API 静态核对，真机仍需测试。整个进程被暂停、线程无法调度、loader lock 初始化阻塞或 `CreateThread` 与外部 helper 均失败时不能宣称绝对 15 秒保证。

## 红测定义与状态

显式 `--shutdown-supervisor-tests` 仅创建本任务隔离 child。私有测试参数分别注入：helper 创建被拒绝时 Close child 挂起；相同拒绝下 Restart child 挂起且不得有新实例；正确继承父 HANDLE 但错误 PID 的握手失败并挂起；helper 已 ack 后 500 ms 提前退出、旧 Restart child 仍挂起。期望各 child 在原 deadline 由自身低层时钟以 `0xE1430016` 结束，Restart 失败两例 marker 必须为 0。原坏 PID 自然退出例期待 `FallbackArmed`，证明创建失败状态与旧进程自行结束并不矛盾。每例只由测试宿主持有自己刚创建的 child HANDLE 做失败清理；不按名称或裸 PID 终止用户进程。真 15 秒主样本保留。

## 执行证据与下一门

- 红版完整 `InkeysRepo.sln Debug|ARM64` build exit 0；显式 no-GUI suite pid 64136 exit 62。原有用例 PASS，新增五项 `wrong-parent-pid`、创建失败 Close/Restart、握手失败、helper ack 后早退均 FAIL。失败 child 未被生产兜底结束，由测试宿主持有的自身 child HANDLE 在约 8–11 秒界限后清理。原始日志 `TestResults/release-hardening/f047-supervisor-fallback-red-build-debug-arm64.log` 与 `f047-supervisor-fallback-red-debug-arm64.{stdout,stderr}.log`。红版观察行的 `exitCode=0` 因测试布尔短路未调用 `GetExitCodeProcess`，**不是 Windows 报告的真实退出码**；绿候选已改为无条件读取并输出实际退出码，不能把旧 0 当进程正常退出。
- 首版绿候选完整 Debug|ARM64 Solution build exit 0；显式 suite 其余 fallback 注入转 PASS，但 `crash-restart-forced` old pid 26720 由本进程兜底以 `0xE1430016` 在约 2953 ms 终止，restart marker 仍为 0，因此 suite **FAIL**。原始 `f047-supervisor-fallback-green-build-debug-arm64.log` 与 `f047-supervisor-fallback-green-debug-arm64.{stdout,stderr}.log` 保留。根因是 helper 与本进程兜底同一 deadline 竞争：后者已发出终止而旧 HANDLE 尚未 signaled，前者 `TerminateProcess` 失败后仅 Wait(0) 就退出 27，丢了本可在旧进程死亡后唯一拉起的机会。
- 已作第二轮最小修补：helper 不管自己的 `TerminateProcess(parent)` 返回成功或失败，都对**同一精确继承 HANDLE** 最多等 5 秒；只有得到 WAIT_OBJECT_0 才启动一次 Restart/CrashRestart。旧进程若仍未 signaled，仍禁止新进程。完整 `InkeysRepo.sln Debug|ARM64` build exit 0；显式 no-GUI suite 连续两轮 pid 48148、60724 均 exit 0。`crash-restart-forced` 分别由自身 `0xE1430016` 和 helper `0xE1430015` 先完成旧进程终止，但 restartCount 两轮均 1；CreateProcess 拒绝、错误 PID 握手、helper ack 后早退分别由自身兜底在约 3/6 秒结束，Restart 失败时 restartCount=0；真实 15 秒 Close 两轮观察为 14968/14953 ms（从测试宿主观察到 marker 后计，不能当光学或点击起点）。原始证据 `TestResults/release-hardening/f047-supervisor-race-fixed-build-debug-arm64.log`、`f047-supervisor-race-green-r{1,2}.{stdout,stderr}.log`。此状态为**无 GUI 隔离进程已验证通过**，不能扩大为真实产品 UEF、卡死画布、保存、Win7 或新实例 ready 已通过。

此单元不能替代真实 GUI 卡死、实际 UEF、保存完整或 Win7 测试。SR-04 点击后可见隐藏延迟另审；本兜底不改善 helper 2.5 秒握手造成的 UI 等待。
