## FACTS

**发现 blocker。** 关键问题在失败清理截止、UEF 并发接管、提示框启动，以及看门狗可能把仍在推进的渲染判为卡死。见下表。

- UI3 的 `wakeEvent` 等待前后有 `Idle` / `Beat`。30/60 FPS 节拍等待使用已设置到期时间的 timer。Draw3 的 `WaitDequeue` 和橡皮静止等待已标记空闲。GPU `Map`、设备重建和交接时的 `DwmFlush` 仍受监督。`RenderPipeline.cpp:624,741–763`、`Bar.FramePacing.cppm:397–406`、`Draw3.DrawingController.cpp:25882–25953,27934–27964`、`Draw3.TransparentPresentation.cpp:978–992,2694–2700`
- Bar 的源矩形延期、Settings 的遮挡探测、PageControl 的租约延期和 Whiteboard 的回执等待均有独立分支。`Bar.RenderLoop.cpp:12273–12289`、`Setting.cpp:5839–5843`、`PageControl.cpp:1627–1637,2751–2754`、`Whiteboard.cpp:449–463`
- `CrashRecoverInk` 默认 `true`，`GeneralDraft` 新字段位于末尾，现有位置初始化对应原字段。开关通过 `QueueConfigWrite` 写入本地配置。配置读取只接受 `TeachingSafetyMode` 整数 0–2，设置页也限制为三档。`Other.Config.cppm:486–489`、`Other.Config.cpp:747–754,885–888`、`Setting.Pages.h:319–361`、`Setting.Pages.cpp:1412–1419`、`Setting.cpp:1129–1131,3351–3361,3473–3482`、`setting_design_tests.cpp:4357`
- CRT 安装与恢复通过 `g_filterRegistrationMutex` 串行，回调不读 `g_crt`，恢复 CRT 早于恢复旧 UEF。`Report` 可在启动前留存，启动主动唤醒。唤醒事件保留到进程结束，避免与 `Report` 并发关句柄。`Helper.CrashHandler.cpp:108–116,138–148`、`Helper.Fatal.cpp:86–91,143–175,194–219`、`Helper.Fatal.State.cpp:74–84`

## INFERENCES

- 看门狗可先读到旧 `first`，随后渲染线程成功清零并发生一次新失败，再读到新 `last`。这个组合会被当成持续失败。即使快照一致，当前触发前也只复查退出和停止标志，期间完成的 `Beat` 或成功提交仍可能被错过。`Helper.Fatal.State.cpp:56–60,96–118`、`Helper.Fatal.h:124–139`、`Helper.Fatal.cpp:53–69`
- 5 秒陈旧窗口仍允许隐藏后触发。若失败持续到第 9.9 秒、随后隐藏且没有清槽，第 10 秒仍满足条件。Settings 隐藏保留 resident，且 epoch 重建能在隐藏时继续返回 `Retry`，因此其失败还可能持续刷新。`Helper.Fatal.h:133–139`、`Setting.SessionState.h:345–361`、`Setting.cpp:5577–5583,5674–5683,5728–5755`
- `SetOffSignal` 和 fatal publisher 的 CAS 只允许一个意图获胜，但 CAS 与监督就绪之间有窗口。普通退出先赢时，publisher 可以提前发布 `offSignal` 并读到零截止。fatal 刚赢时，普通退出会立即返回，无法补上仍未建立的监督。`IdtMain.cpp:342–368,410–436`、`ShutdownSupervisor.cpp:424–428,3049–3052`

## UNKNOWN / 需要运行测试

- 本轮只读源码，未执行构建或测试。用户提供的八入口 Debug 结果对应隔离测试的静默档，且子进程修改了 Debug CRT 报告模式。仍需验证原样 Debug invalid parameter、Release ARM64、并发 `SIGABRT` 的 handler 重置，以及真实提示失败和 `-CrashTry` 五分钟抑制。`ShutdownSupervisor.cpp:1317–1323,1570–1584,1773–1775,1812–1813`、`Helper.Fatal.cpp:194–218`、`Helper.CrashHandler.cpp:474–486,646–676`
- 需实测低端机重页重放、设备恢复、阻塞 `Map` / `DwmFlush` 的最大单元耗时，以及停启竞态。已读的生产调用在主线程启动和停止，`StopWatchdog` 等待 2 秒后不检查线程是否退出，不能据此证明可安全再次启动。`design.md:97–101,185–186`、`IdtMain.cpp:4332–4341,4407`、`Helper.Fatal.cpp:143–175`

## Findings

| 严重度 | 文件:行 | 问题 | 最小修复 |
|---|---|---|---|
| blocker | `IdtMain.cpp:410–423`、`FailedCleanupDeadline.cpp:91–107`、`ShutdownSupervisor.cpp:424–498` | `PublishFatalFailedCleanupNoWait` 同步 Arm，包含 `CreateProcessW` 前置工作和两段等待。在 `publisher()` 返回前，调用线程不会执行 `ForceCurrentProcessAt`。发布 tick 本身没有执行者。 | publisher 只发布意图和唤醒。由独立执行者 Arm，原 fatal owner 继续执行原绝对截止。 |
| blocker | `Helper.CrashHandler.cpp:486–493`、`Helper.Fatal.cpp:114–140,187–191` | `g_isGeneratingDump` CAS 成功后、`g_crashThread` 发布前，第二个 `RaiseNow` 可从 core 返回、读到 owner 为 0 并杀进程。同线程递归也会直接终止。 | 原生 UEF 和 `RaiseNow` 共用一次携带线程 ID 的原子接管。明确区分首入、其他线程和同线程递归，保留首入已建立的截止。 |
| blocker | `Helper.CrashHandler.cpp:636–676`、`MessageBox.cpp:46–55,277–293`、`MessageBox.Window.cpp:1624–1643,1840–1847,1899–1928` | 提示档先撤销报告截止。`CriticalNoWait` 只让 admission 使用 `try_lock`。弹窗 GDI+ preflight、建窗和线程回收仍可无限等待，卡在显示前就永远拿不到 `Failed`。 | 保留独立的建窗截止，收到“可交互”信号后才允许无限等待用户。建窗超时沿提示失败路径重启。 |
| blocker | `Draw3.DrawingController.cpp:185–199,21953–21976,25064–25067,25882–25953` | `DrainIngressBatch` 可持续消费补进来的输入，单批超过 15 秒仍有进展，但只看帧顶 `Beat`。反过来，空闲分支先 `Idle`，再由 `tryConsumeOneIngress` 执行命令，真卡死会漏报。 | 将 `Idle` 移到实际等待前，在共同的 `processCommandAndReconcile` 入口 `Beat`。 |
| blocker | `Helper.Fatal.State.cpp:56–60,96–118`、`Helper.Fatal.cpp:63–69` | 分开采样首末失败时间，加上触发前不复验槽，允许在成功恢复后误重启。 | 一致读取同一失败代次，触发前复验对应槽。 |
| should-fix | `Helper.Fatal.cpp:112–140`、`Helper.CrashHandler.cpp:112–116,138–146,494–530`、`IdtMain.cpp:346–368,397–400` | CRT hook 先于 core 发布。退出 CAS 后到撤销 core 前，`RaiseNow` 仍可进入 core，随后在普通退出监督尚未就绪时杀进程。 | 先发布 core 再装 CRT。已受理退出时让当前退出 owner 接管终止，覆盖 CAS 与 Arm 就绪之间的区间。 |
| should-fix | `IdtMain.cpp:4297–4305`、`Log.h:115–133`、`Log.cpp:235–243` | `Draw3Stopped` 在 `RaiseNow` 前调用可阻塞的 `Log::Critical`。 | 改用 `Log::TryFormat` 或删除此日志。 |
| should-fix | `Setting.cpp:5674–5683,5728–5755,5547–5577`、`Setting.SessionState.h:345–361` | 隐藏后的 resident 重建失败可持续累计。`DXGI_STATUS_OCCLUDED` 也会走成功清槽，虽然同一函数只在 `S_OK` 确认提交。 | 隐藏时清槽，呈现槽只累计可见会话故障，只在 `S_OK` 清成功。 |
| should-fix | `Draw3.DrawingController.cpp:18577–18580,22214–22231,25378–25386`、`Draw3.TransparentPresentation.cpp:1271–1297,2873–2917` | 三个帧外 ULW 失败出口漏记。`PresentAsync` 仅累计脏区也可返回 `true`，清掉真实失败。 | 在三个 `report.failed` 分支计失败，以 `CommittedPresentCount()` 实际推进为成功证据。 |
| should-fix | `Whiteboard.cpp:577–602`、`IdtFreezeFrame.cpp:85–91,146–154,182–191` | 白板把真实 ULW 失败与换代合并为 `!presented`，漏记前者。Freeze 事后重读 owner 可误记交接拒绝，退休透明提交持续失败时又会逐轮清槽。 | 在 `SubmitFreezeSurface` 区分拒绝与实际 API 失败。两侧用同一次结果计数，退休成功后再清槽。 |
| should-fix | `Bar.RenderLoop.cpp:12292–12307,12644–12652`、`Bar.PresentDecision.h:161–165`、`SelectionOverlay.cpp:107–112`、`Bar.ShapeEditor.cpp:2297–2301` | Bar 计数只检查 `endDrawHr`，`GetDC` / `ReleaseDC` 的共享设备丢失可同时进入客户端槽和恢复链。Selection 只识别 `D2DERR_RECREATE_TARGET`，原样返回的 DXGI 设备丢失会误归客户端。 | Bar 复用 `presentAttempt.HasSharedDeviceLoss()`。Selection 先走共享设备丢失判断，再计本地故障。 |
| should-fix | `PageControl.cpp:1451–1460,1488–1493,2751–2754` | 无效几何或 HWND 的默认 `Retry` 也会计失败。四个表面共用一槽，健康表面成功可清掉另一表面的持续故障。 | 在明确 API 失败处计数，按表面维护计时，在该表面隐藏或换代时清除。 |
| should-fix | `Helper.Fatal.State.cpp:35–41,74–83` | 并发 `Report` 可混配首个 `reason` 与另一线程的 `detail`。时钟调用失败时切到 `GetTickCount64`，不同基准可造成阈值跳变。 | 单原子发布整对报告。保持同一计时基准，取时失败的样本跳过。 |