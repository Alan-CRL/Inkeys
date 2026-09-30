# SR-05 / SR-06：手动崩溃确认与迟到进程终止

日期：2026-09-28。独立 reviewer 在 F-047 后确认两处剩余风险。本单元只负责 `Helper.CrashHandler.cpp`、`ShutdownSupervisor.cpp/.h`，不改 `IdtMain`、Window Service、Draw3 或工程文件。

## SR-05：确认前不能占用正式退出意图

`crashMode=0` 的 UEF 原来在 dump、报告和用户确认框之前 CAS `offSignalInterop:0→3`，此时还没有获准重启，也未建立 helper。若确认框/报告卡住，另一线程正式 Close/Restart 的 `SetOffSignal` CAS 会失败，连 15 秒保护都不能启动；用户点取消也已占槽。修正合同：自动模式 1 在报告前争 CAS 并立即 Arm；手动模式 0 在用户点击 OK 之后才争 CAS，只有获胜才 Arm；手动拒绝/超时、模式 2/3 都不占槽。UEF 原有 `g_isGeneratingDump` 一次性门闩继续阻止重入双拉起；受控退出先获胜则 UEF 不再启动另一实例。

可执行无 GUI 红测使用 UEF 实际调用的窄策略函数 `TryClaimCrashRestartIntent(slot, mode, consent)` 与生产 `ArmCore`：隔离 child 模拟 manual pending，另一测试线程按正式 `SetOffSignal` 的 CAS0→1 请求 Close，迟到的 OK 再争 CAS；期望 Close 获胜、建立 deadline、旧 child 到期强退、新实例数0。红版策略仍按旧行为在 pending 阶段 CAS，故 Close 无法建监督；测试宿主仅以自己持有的 child HANDLE 做失败清理。此测试不弹 MessageBox、也不执行真实 UEF；真实用户确认竞态仍属人工/专用进程验证。

## SR-06：强退后仍须等待旧进程真正死亡

对其他进程 `TerminateProcess` 发请求可先于进程句柄 signaled 返回；pending I/O 可能让旧进程在 5 秒后才真的结束。现 helper 的固定 5 秒死亡等待到期便退出，Restart/CrashRestart 会漏拉起。修正合同：到期只对精确继承的旧 HANDLE 发 Terminate，请求后继续等待**同一 HANDLE**直到 WAIT_OBJECT_0，才唯一启动新进程；若长期不 signaled，helper 可以一直安全等，不能与旧进程并发启动。15 秒是发出强退的截止，不承诺 Restart ready 也在 15 秒内。

无 GUI 红测以显式测试标志使同一 helper 跳过实际 Terminate，但仍进入生产的旧 HANDLE 死亡等待；对应测试 child 在 3 秒 deadline 后再活超过 6 秒才自然结束，且仅该场景禁用自身 fallback，防止它先杀死 child。固定 5 秒等待的红版应丢失后置 restart；绿版必须等旧 child 真正退出后 marker=1。它检验等待/唯一启动逻辑，**不声称真实磁盘 I/O 导致的迟到 signaled 已在本机复现**。整个测试只创建/等待本任务子进程，不触碰用户既有 Inkeys/Office。

## 红→绿状态

- 红版完整 `InkeysRepo.sln Debug|ARM64` build exit 0；显式隐藏 no-GUI suite pid 27564 exit 62。新增 `manual-pending-close-wins` FAIL：旧自建 child 无监督，到测试宿主约 11 秒上限才以其精确 HANDLE 清理，exit 995；新增 `late-death-restart` FAIL：旧 child 在约 9.5 秒自然 exit 77，但 restartCount=0。其它先前用例 PASS。原始 `TestResults/release-hardening/sr05-sr06-red-build-debug-arm64.log` 与 `sr05-sr06-red-debug-arm64.{stdout,stderr}.log` 保留。
- 绿版仅改同一策略的触发条件与 helper 超过 5 秒后的同 HANDLE 等待；UEF 异常早退保留一次性门闩。完整 `InkeysRepo.sln Debug|ARM64` build exit 0；显式隐藏 `Start-Process`/`WaitForExit` suite pid 58152 exit 0。`manual-pending-close-wins` old pid 30440 由自身兜底以 `0xE1430016` 在 marker 后约 5938 ms 结束，restartCount=0；`late-death-restart` old pid 36144 自然 exit 77、marker 后约 9531 ms 才结束，此后 restartCount=1。其它原例含真实 15 秒 Close、CrashRestart、helper 创建/握手失败均 PASS。原始日志 `TestResults/release-hardening/sr05-sr06-f041-policy-green-build-debug-arm64.log` 与 `sr05-sr06-green-debug-arm64.{stdout,stderr}.log`。这是**本机无 GUI 隔离进程范围通过**，红版失败记录不删除。

自审：生产 `TryClaimCrashRestartIntent` 只在 mode1 自动或 mode0 已接受时 CAS0→3；UEF mode0 在报告后再检查共享意图，确认 OK 后仍以 CAS 判定唯一 owner。对话框取消/超时以及 mode2/3 不占槽；控制退出先获胜时不出现第二 launcher。helper 超时后只对精确继承旧 HANDLE 强退，再等待同一 HANDLE signaled；超过 5 秒发一条低频诊断后继续等待，永不 signaled 则绝不并发启动新实例。测试晚死分支显式跳过真实 Terminate 与本进程兜底，只验证生产等待/后置 launcher 合同，不能冒充 pending I/O、真实 UEF 或 GUI 证据。真实手动对话框、未处理异常、真正 pending I/O/断电、新产品实例 ready 与保存恢复仍未验证。

Win7 SP1+仅 KB2670838、Release/x86/x64、GUI、实际 UEF 与保存恢复仍另列未验证；本改动不触及 FLIP_SEQUENTIAL、仅 DComp/ULW 或 HARDWARE/WARP 图形合同。
