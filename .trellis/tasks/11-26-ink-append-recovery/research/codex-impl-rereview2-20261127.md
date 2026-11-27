## FACTS（已核实）

**仍有 blocker。** 重复崩溃提示无法关闭，写前登记重试可能误报成功，桌面恢复和退出结算仍有丢失恢复来源的路径。见 `MessageBox.cpp:210–219`、`Draw3.RecoverySession.cpp:523–535`、`Draw3.Host.cpp:978–1039,3876–3885`。

| 上轮问题 | 第 3 轮核对 |
|---|---|
| R1 | **部分修复。** FileId 查询失败会拒绝登记，登记成功即置 `recoveryPendingStale`，后续单块也会覆盖旧截点。worker 已接入空闲重试，但 `Session::ClearPending` 的短路可让重试误报。`Draw3.Host.cpp:1091–1104`、`Draw3.PagedInkStore.cpp:1072–1094,1216–1236,1470–1479`、`Draw3.RecoverySession.cpp:523–535` |
| R2 | **主路径修复。** Full 和 Append 等当前 epoch 的 Rotate 成功回执才放行，失败置 `rotatePending`，桌面空闲最多显式重试三次。换场景后这条重试路径停用。`Draw3.DrawingController.cpp:23138–23212,23256–23282,23319–23336` |
| R3 | **判据已接入，仍可误结算。** 桌面检查同 epoch 修订和 `RecoveryPendingSettled()`，Clear 等 Rotate 完成。白板检查脏页、在途写入、降级和登记状态。退出兜底绕过部分判据。`Draw3.DrawingController.cpp:23220–23249,2139–2161`、`Draw3.Host.cpp:765–775,3876–3885` |
| R4 | **部分修复。** 合并写成功才删旧记录。合并检查已清标记和 Clear 代次，但扫描可能先删除只有已清标记的记录。`Draw3.Host.cpp:863–892`、`Draw3.RecoverySession.cpp:664–675` |
| R5 | **原两处已修。** 关开关时文件登记失败仍准入，`ClearPending` 无登记且身份相同则免写 JSON。T4 又在关开关时引入了同步写 JSON。`Draw3.Host.cpp:1074–1098,2218–2232,3022–3035`、`Draw3.RecoverySession.cpp:523–535` |

- **T1。** `RaiseNow` 和 UEF 共用接管状态，已有普通 Close/Restart 时 `RaiseNow` 让出。五分钟内 UEF 仍尝试写 Critical、dump 和报告，提示档只有 `Yes` 才尝试抢重启意图。`nullptr` 截止仍会终止进程，也仍会先尝试占重启意图槽。`IdtMain.cpp:417` 的强退 publisher 在抑制期走 Close，符合现行强退分流规范。`Helper.Fatal.cpp:130–164,211–217`、`Helper.CrashHandler.cpp:121–131,535–573,669–689,759–782`、`errors-logging-and-resources.md:391,557,566`
- **T2。** `RecoveryNeedsPrompt` 在恰好五分钟时静默恢复，时间取不到或在未来时询问。桌面和白板保留原场景门控，静默档仅在 `-CrashTry` 时跳过提示。`Draw3.Host.cpp:820–836,928–934,995–1001`、`IdtMain.cpp:4142–4148`
- **T3。** 桌面装入非空墨迹后发通知。PPT 等当前 lane 加载完成，以 `TakeAdoptedNotice()` 一次性取走存储级通知，再检查当前页是否有内容。`Host::RecoveryInkVisible` 转发到原子标志，主循环消费。`Draw3.DrawingController.cpp:24518–24540`、`Draw3.PagedInkStore.cpp:1430–1438,1779–1782`、`Draw3.Host.cpp:743–751`、`IdtMain.cpp:542–546,4421–4424`
- **T4。** `desktopClear` JSON 往返四个字段。快照成功且未降级时按请求 ID 登记，Rotate 经 `node.baseRevision` 传 Clear 代次。扫描按代次读快照，`TakeDesktopRecovery` 一并移交当前和上一区间。`installPrevious` 绑定当前页并保留 CPU 副本，撤回到底走 `restoreSnapshot(*desktopClearRecovery->canvas)`。`Draw3.RecoverySession.cpp:238–244,324–331,469–502`、`Draw3.PagedInkStore.cpp:1513–1526,1723–1732`、`Draw3.Host.cpp:724–738,804–815,978–1022,3022–3035`、`Draw3.DrawingController.cpp:24350–24389,25174–25198`

## INFERENCES（静态推演）

- 空闲清除重试即使真实成功也没有发绘制唤醒。若绘制线程已消费保存回执并进入 `WaitDequeue`，`settleRecoveryDurable()` 可能等到下一次输入才重查，恢复线程可能先超时。`Draw3.PagedInkStore.cpp:1473–1479,1600–1612`、`Draw3.DrawingController.cpp:24534,26565–26577`、`Draw3.Host.cpp:765–775`
- PPT 通知只携带布尔值。接管通知发出后若切页或切场景，主循环可能为已经不可见的墨迹切画笔。`Draw3.DrawingController.cpp:24535–24540`、`IdtMain.cpp:542–546,4421–4424`

## UNKNOWN / 需运行验证

本轮仅静态复审。既有构建、存储测试和 C1.4 自检见 `.trellis/tasks/11-26-ink-append-recovery/implement.md:28–32`。优先实测真实 JSON 替换失败后的重启、Clear 后换场景及连续两次崩溃、退出快照写失败、重复提示的关闭与建窗超时。现有登记失败测试使用模拟钩子。`paged_ink_store_tests.cpp:663–699`、`Draw3.RecoverySession.cpp:251–264`、`errors-logging-and-resources.md:569–573`

## 问题表

| 严重度 | 文件:行 | 问题 | 最小修复点 |
|---|---|---|---|
| blocker | `Draw3.RecoverySession.cpp:523–535`、`Draw3.PagedInkStore.cpp:1084–1092,1473–1479` | **R1/R3。** `ClearPending` 先清内存再写盘。写失败后重试命中内存短路，磁盘仍有旧截点，耐久判定却可能放行并删除源记录。 | 写失败时恢复原身份和登记，或保留待落盘状态直到写成功。空闲重试成功后唤醒绘制线程。 |
| blocker | `Draw3.RecoverySession.cpp:664–675`、`Draw3.Host.cpp:870–892` | **R4。** 无快照的已清记录会先被扫描清掉，随后可恢复更旧的桌面墨迹。反过来，仅有上一区间的旧记录不参与借用，却可以被合并删除。 | 已清标记保留到合并完成。用同一 `desktopRecoverable` 判定扫描、借用与删除。 |
| blocker | `Draw3.Host.cpp:983–991,1005–1013,1031–1039`、`Draw3.DrawingController.cpp:24524–24528` | **T4。** 当前文件读取失败但上一段可读时，会合成空当前页并立即结算，丢掉重试当前墨迹的机会。仅上一段存在却读失败时也立即结算。 | 区分无文件条目、读到空页、读失败。读失败沿用重试预算，保留来源。 |
| blocker | `Draw3.Host.cpp:1034–1039`、`Draw3.RecoverySession.cpp:469–502` | **T4。** 旧撤回点交接忽略登记失败，还可在用户新 Clear 后、其快照尚未登记时，被绑定到新 Clear 代次。 | 交接附带装入时的预期代次，代次变更时放弃旧撤回点。登记失败时保留来源记录。 |
| blocker | `Draw3.Host.cpp:765–775,3865–3885`、`Draw3.DrawingController.cpp:2139–2161,23520–23527` | **退出。** 全局兜底用白板结果替桌面确认。桌面快照只检查入队，白板退出也未检查降级或残留登记。仍在等待的旧记录可能被提前删除。 | 按场景使用实际成功回执结算，白板复用正常恢复的登记与降级判据。 |
| blocker | `Helper.CrashHandler.cpp:121–131,743–765`、`MessageBox.cpp:210–219`、`MessageBox.Window.cpp:697–701,1380–1382` | **T1。** YesNo 默认隐藏关闭按钮并忽略 `WM_CLOSE`。可交互后建窗计时停止，无法按用户要求关闭后退出。超时分支还会在空回调时抢重启槽。 | 仅重复提示启用关闭，保留 `Dismissed` 和 `Yes` 判定。先检查回调，再抢重启槽。 |
| should-fix | `Draw3.DrawingController.cpp:23256–23282,24139–24150` | **R2。** Rotate 失败后切到白板或 PPT，空闲重试不再服务停放的桌面。此时再崩溃可读到清前区间。 | 用停放桌面页身份服务已有的有界 Rotate 重试。 |
| should-fix | `Draw3.DrawingController.cpp:19123–19149,24358–24390`、`Bar.Button.cpp:1788–1799` | **T4。** 仅装撤回点时未发布 `canUndo`。主栏据旧值可拒绝发出 Undo。 | 在 `installPrevious` 成功后刷新历史可用性，覆盖空当前页。 |
| should-fix | `Draw3.DrawingController.cpp:21964–21971`、`Draw3.Host.cpp:3022–3062,3146–3155`、`Draw3.RecoverySession.cpp:491–502,251–264` | **T4/R5。** Clear 回执在绘制线程持会话锁写 JSON 并 Flush，关闭恢复开关时仍执行。慢盘可阻塞绘制。 | 把登记移到现有持久化 worker，绘制线程只提交请求身份和代次。 |
| should-fix | `IdtMain.cpp:4421–4424`、`IdtState.h:97`、`IdtState.cpp:643–651,2551–2558,2950–2956`、`Bar.Button.cpp:401,449` | **T3。** 主循环无锁读普通枚举，可与 UI 写入竞争。检查与切换分开，也可能覆盖刚选的工具。 | 在 `stateModeTransitionMutex` 内一起判断和提交，锁外调用 `SyncDraw3State()`。 |
| should-fix | `Draw3.Host.cpp:981–1001,3827–3857,3876–3882`、`Draw3.DrawingController.cpp:24512–24531` | **退出。** `recoveryStop` 到绘制线程 join 后才置位。退出屏障期间仍可发桌面提示或发布装入。 | Stop 入口关闭恢复提示和装入准入，屏障后再发布耐久结果并唤醒等待者。 |