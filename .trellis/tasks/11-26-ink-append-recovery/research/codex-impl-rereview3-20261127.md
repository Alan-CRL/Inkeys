## FACTS（已读源码）

**仍有 blocker。** 合并旧会话、桌面 Clear 的提交顺序、恢复落盘判定和退出取消仍有缺口。`H:873–897,983–1024,3894–3901`、`RS:469–499`、`DC:23203–23236,24369–24395`

引用简称：`H`、`DC`、`PS`、`RS` 分别为 `Inkeys/Inkeys/Drawing/Draw3/` 下的 `Draw3.Host.cpp`、`Draw3.DrawingController.cpp`、`Draw3.PagedInkStore.cpp`、`Draw3.RecoverySession.cpp`。`CH`、`F`、`SS` 分别为 `Inkeys/Inkeys/Helper/` 下的 `Helper.CrashHandler.cpp`、`Helper.Fatal.cpp`、`ShutdownSupervisor.cpp`。`Main`、`State` 为 `Inkeys/IdtMain.cpp`、`Inkeys/IdtState.cpp`。

| §15 项目 | 第 4 轮核对 |
|---|---|
| R1 | **主体已修。** FileId 查询失败拒绝登记，登记成功即置 `recoveryPendingStale`，后续单块也重新登记。`ClearPending` 写失败回滚内存。PPT 接管的空闲重试仍有遗漏。`H:1104–1117`、`PS:1072–1092,1216–1236,1399–1403,1452–1485`、`RS:562–580` |
| R2 | **已修原阻断。** `rotatePending` / `rotateInFlight` 拦住新区间写入，按 epoch 消费回执。失败后可用被清页 GUID 在其他场景空闲重试，最多三次。`DC:23139–23213,23257–23266,23319–23335,24154–24173` |
| R3 | **部分修。** 桌面同 epoch 检查确认修订及 `RecoveryPendingSettled()`，跨 epoch 等 Rotate 成功。白板另查脏页、在途写入和降级。桌面漏查 `completion.degraded`。`DC:23203–23249` |
| R4 | **部分修。** 合并写成功才进入旧记录删除分支，最新记录的 Clear 标记可阻止借用。中间会话的仅清空记录及尚未发布的候选路径仍会影响来源选择。`H:873–897`、`RS:715–728`、`PS:812–822` |
| R5 | **已修原阻断。** 关闭时跳过写前登记，文件登记失败不拒绝普通保存，无 pending 且身份未变时跳过 JSON 写盘。Clear 仍提交 Rotate。`H:1088–1117`、`RS:562–580`、`DC:24154–24173` |

- **T1** `ClaimCrashHandling` 只允许一条处理链，`RaiseNow` 遇既有 Close/Restart 意图会让位。抑制期 UEF 仍尝试 Critical 日志、报告和 dump。提示档只有 `Yes` 能争抢重启槽，建窗超时传 `nullptr`，直接结束。`KnownFailureCleanup` 的 publisher 在抑制期走 Close，符合强退链条款。`CH:121–132,536–607,672–714,723–790`、`F:130–163,211–217`、`SS:3074–3079`、`Main:410–434`、`.trellis/spec/native-desktop/errors-logging-and-resources.md:391,557,566`。YesNo 的关闭限制仍与 §15.1 记录一致。`Inkeys/Inkeys/UI/MessageBox/MessageBox.cpp:210–220`、`.trellis/tasks/11-26-ink-append-recovery/design.md:378`
- **T2** `RecoveryNeedsPrompt` 对 ≤5 分钟直接恢复，时间缺失或在未来则询问。桌面和白板仍受崩溃场景与静默档限制，仅桌面撤回点静默装入。`H:822–838,933–939,1003–1009`、`Main:4143–4148`
- **T3** 桌面装入有笔画才发通知，PPT 等 lane 初始化并完成加载后调用 `TakeAdoptedNotice()`。`Host::RecoveryInkVisible` 转发到原子标志，主循环调用 `ChangeStateModeToPenIfIdle()`，模式判断和提交同锁，`SyncDraw3State()` 在锁外。`DC:24532–24560`、`PS:1430–1437,1785–1788`、`H:746–754`、`Main:542–547,4420–4422`、`State:2561–2570`
- **T4 / 退出** `ObserveDesktopAutoSave` 记 Clear 请求 ID，匹配完整保存回执后通过 `PublishDesktopPreviousInterval` 交给会话 worker。Rotate 经 `node.baseRevision` 传代次，JSON 双向保存 `desktopClear`。`TakeDesktopRecovery` 一并取当前区间与上一段，`installPrevious` 建 CPU 撤回点并刷新可用性，撤回到底走 `restoreSnapshot` 并补写活动区间。原 `recoveryExitDurable` 兜底已由各场景实际回执替代，正常退出排空桌面队列后结算，`WaitRecoveryDurable` 最多等两分钟，已进入 Stop 时跳过桌面扫描分支。`H:726–740,768–779,983–992,2231–2246,3035–3078`、`RS:238–244,324–331,517–527,642–659`、`PS:1522–1532,1729–1738`、`DC:23543–23559,24203–24229,24369–24410,25200–25218`

## INFERENCES（静态推断）

下表的复活、丢失和退出强退是由源码分支推导的结果。新 Clear 快照回执在绘制线程只拿短锁发布请求，JSON I/O 在会话 worker；恢复耐久等待释放 `recoveryMutex`。目前最明确的退出等待问题是同步弹窗缺取消。`H:768–779,3035–3078,3894–3901`、`RS:517–527,642–659`、`Main:587–592`

## UNKNOWN / 需实机验证

本轮只读，未执行构建、测试或 GUI。沿用 Claude 报告的 ARM64、两组存储测试、9 项 C1.4 自检及 fatal-entry 8/8。fatal-entry 走自动场景，隐藏自检跳过 `Session::Start`。优先实测建窗超时与退出时取消、连续两次崩溃、Clear 两个 worker 反序、慢盘下落笔及开关切换。`.trellis/tasks/11-26-ink-append-recovery/implement.md:28–32`、`SS:3195–3207`、`H:3238–3253`、`RS:642–659`、`PS:1522–1532`

## Findings

| 严重度 | 文件:行 | 问题 | 最小修复 |
|---|---|---|---|
| blocker | `H:873–897,1076–1079`、`RS:715–728` | 中间会话只有 `desktopCleared` 时被跳过，最旧墨迹可被借回。合并写失败后，末尾仍可能删掉唯一的清空标记。 | 借用和删除统一识别清空标记与有效上一段。确认旧来源已安全淘汰后再删标记。 |
| blocker | `PS:812–822`、`H:794–802,873–897,988–999` | 新会话只登记候选路径就崩溃，会挡住借用并删除旧记录。候选文件尚未发布，随后读失败，旧墨迹失去恢复入口。 | 合并时验证候选是否已发布。候选缺失或暂不可读时保留旧来源，同时尊重有效 Clear。 |
| blocker | `RS:469–499,642–659`、`H:983–1017` | 快照 worker 先写时会推进 Clear 代次，却保留清前的 `desktop`。两次写盘之间崩溃会把已清墨迹装成当前区间。 | 同一记录提交中推进代次并撤下旧活动条目，或让扫描识别尚未完成 Rotate 的代次差。 |
| blocker | `DC:2916–2927,19101–19105,24154–24173,24369–24395`、`H:1022–1051` | 本次启动先写笔再 Clear，迟到恢复仍会通过空页检查，装入旧墨迹或覆盖刚建立的撤回点。Host 的代次只约束后续登记。 | `installDesktopRecovery` 拒绝启动后已经执行桌面 Clear 的恢复包。 |
| blocker | `DC:23203–23236`、`PS:966–984,1048–1052`、`H:1044–1052` | 桌面 `Succeeded + degraded` 仍能推进耐久确认，随后结算旧来源。 | 在桌面回执记住确认版本是否降级，仅用完整版本确认交接。 |
| blocker | `H:921–939,986–1009,3824–3828,3894–3901`、`Main:587–592,4479–4496` | 慢读后仍可能在 Stop 期间弹窗。已开的同步框也没有取消令牌，恢复线程 join 可耗尽退出监督期限。 | 两处弹窗前复查 closing，由 Stop 入口取消传给 `request.cancellation` 的令牌。 |
| should-fix | `PS:1399–1403,1442–1485,1602` | PPT 接管清登记失败后，若只有读请求，空闲重试可能一直进不了分支。 | 将 `recoveryClearRetryPending` 纳入 worker 唤醒谓词。 |
| should-fix | `H:992–999,1028–1052` | 当前可读时忽略 `previousFailed`，交接可成功，但本次会话缺少撤回点。 | 保留上一段的独立重试状态，确认装入后再结算该段。 |
| should-fix | `PS:1433,1785–1788`、`DC:24554–24560`、`Main:542–547,4420–4422` | PPT 通知按存储消费，首个空页即可吃掉通知。主循环收到的布尔值也无法核对画面是否已切换。 | 按成功装入的接管页触发，交给主循环时携带场景或页身份并复查。 |