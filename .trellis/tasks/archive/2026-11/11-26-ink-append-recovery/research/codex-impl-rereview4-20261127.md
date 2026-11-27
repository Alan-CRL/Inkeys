## FACTS（源码核验）

**仍有 blocker。** 恢复询问建窗失败会被当成用户拒绝，启动失败路径也可能在取消恢复询问前等待另一只弹窗。旧会话清理仍有让清前墨迹复活的路径。`Main:224–244,593–597,4227–4235`、`H:868–912,951–956,1022–1027,1087–1096`

引用简称：`H/DC/PS/RS` 为 `Inkeys/Inkeys/Drawing/Draw3/Draw3.{Host,DrawingController,PagedInkStore,RecoverySession}.cpp`。`W` 为 `Inkeys/Inkeys/Window/Window.cpp`，`MB/M` 为 `Inkeys/Inkeys/UI/MessageBox/MessageBox.Window.cpp` 与 `MessageBox.cpp`，`Main` 为 `Inkeys/IdtMain.cpp`，`D` 为 `.trellis/tasks/11-26-ink-append-recovery/design.md`。

| 第 4 轮问题 | 第 5 轮核对 |
|---|---|
| 越过中间 Clear、合并写失败仍删记录 | **部分修复。** `desktopResolved` 停在扫描返回的首个桌面状态，`mergedPersisted` 守住两处删除入口。扫描预删与删除失败仍有缺口。`H:875–912,1093–1096`、`RS:722–735` |
| 未发布候选挡住借用 | **部分修复。** 新旧桌面、白板均走 `unpublished`。它把所有属性查询失败都当成文件缺失。`H:868–901`、`PS:812–822` |
| 快照登记先于 Rotate | **已修。** `ApplyDesktopPreviousLocked` 推进代次时撤下活动条目并置 `desktopCleared`，同一份记录写盘。`RS:487–519,649–666` |
| 启动后 Clear 仍装入旧墨迹 | **已修。** 桌面 Clear 递增代次，`installDesktopRecovery` 拒绝非零代次，装入失败保持未结算。`DC:24153–24174,24374–24381`、`H:1058–1081` |
| 降级版本也能交接 | **已修。** 同 epoch 的成功回执记录 `confirmedDegraded`，耐久确认要求它为假且登记已清。`DC:23204–23240,24164–24171` |
| 退出时弹窗无法取消 | **部分修复。** 两个场景在询问前复查 `recoveryClosing`，`Stop` 调用 `recoveryPromptStop.request_stop()`。已显示的框监听取消事件，正常收口返回 `Dismissed`。退出信号到 `Stop` 之间仍有缺口。`H:951–956,1022–1027,3842–3847`、`MB:1739–1759,1917–1948` |
| PPT 接管清登记的空闲重试 | **已修。** 等待谓词包含 `recoveryClearRetryPending`，空队列时在锁外重试一次，成功后唤醒调用方。`PS:1397–1403,1452–1487` |

- 启动报错、窗口创建失败和两处 SuperTop 失败均设置 `ownerlessTopmostAtCreation`。自绘框揭示后立即置顶，再每 500 ms 用 `SWP_NOACTIVATE` 提升自身。首次显示仍调用 `SetForegroundWindow`、`SetFocus`。`TestAutomationTimerId` 与置顶计时器 ID 不同，现有无主框用例在 40 ms 检查创建样式与焦点归还。`Main:169–182`、`Inkeys/Inkeys/Window/Window.Legacy.cpp:136–143`、`Inkeys/SuperTop/IdtSuperTop.cpp:328–349`、`MB:46–50,1562–1579,1717–1733`、`InkeysHeadlessTests/message_box_tests.cpp:1088–1100`
- 恢复询问按 Bar HWND 是否有效选 owner。HWND 在 Draw3 启动前已创建，Bar 初始化线程随后才启动。弹窗对跨线程 owner 先做 250 ms `WM_NULL` 探测，再同步 `EnableWindow`。作用范围是 Bar，Bar 的 `WM_CANCELMODE` 会取消输入。取消时跳过前台归还，仍同步恢复 owner 的 enabled 状态。`Main:591–597,4011–4012,4077–4094,4213–4215,4323–4333`、`MB:52,613–639,1873–1917,1934–1948`、`Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp:3311–3319,3351–3357`
- `SuspendTopmostRefresh()` 在崩溃提示前只写原子量。进入 `RefreshTopmost` / `PromotePpt` 分支后返回成功，并调用 `ReportCommandResult`。已有失败标记时，这会清除标记并记为 `recovered`。同步刷新和异步刷新成功后仍可通知 `StartupPreview` 再置顶。`Inkeys/Inkeys/Window/Window.cppm:331–334`、`Inkeys/Inkeys/Helper/Helper.CrashHandler.cpp:751–768`、`W:328–331,794–804,1118–1135,1154–1169,2346–2355,3920,4474–4487`、`Main:4281–4286`、`Inkeys/Inkeys/UI/StartupPreview/StartupPreview.cpp:137–140,348,976–979`

## INFERENCES（静态推断）

- Bar 健康时，取消事件可以唤醒弹窗消息循环。若 Bar 在探测通过后卡住，同步禁用或恢复 owner 仍可能拖住 `Show`，继而拖住恢复线程的 `join`。`MB:613–639,1884–1910,1921–1948`、`H:3913–3917`
- 500 ms 刷新可能反复盖过其他置顶窗口。崩溃时，已进入的刷新、窗口组事务和 `RaiseOwnedSetting` 仍可改 Z 序，预览也能收到提升通知。弹窗自身的计时器只能在后续 tick 再抬回。`MB:1562–1567`、`W:2583–2640,4093–4096,4138–4148,4365–4395`、`Main:4281–4286`、`Inkeys/Inkeys/UI/StartupPreview/StartupPreview.cpp:137–140`

## UNKNOWN / 需实测

本轮只读。实机重点验证启动预览与恢复框重叠、Bar 卡住后取消、绘制态 Setting 与崩溃框争抢，以及启动／SuperTop 报错跨过 500 ms 后的层级和焦点。现有 40 ms 用例只覆盖首次显示。`Main:591–597,4281–4286`、`MB:1562–1579,1884–1910`、`W:4365–4395`、`Inkeys/SuperTop/IdtSuperTop.cpp:328–349`、`InkeysHeadlessTests/message_box_tests.cpp:1088–1100`

## Findings

| 严重度 | 文件:行 | 问题 | 最小修复 |
|---|---|---|---|
| blocker | `Main:593–597`、`MB:1884–1910`、`M:59–63`、`H:951–956,1022–1027` | Bar 探测超时或建窗失败返回 `Failed`，`PromptInkRecovery` 折成 `false`，Host 据此结算。用户没看到询问，旧记录也可能被删。 | 将 `Yes`、`No` 与失败／取消分开传给 Host。只有明确 `No` 才按拒绝结算。 |
| blocker | `Main:224–244,4227–4235,4468–4501`、`M:46–55,280–296`、`H:3842–3847` | 已开的恢复框持有全局弹窗准入。启动故障同步显示报错，取消却要等后面的 `StopProduct`，可耗尽 15 秒退场期限。 | 正式退出意图获准时就关闭恢复准入并请求取消，放在启动报错和线程等待之前。 |
| blocker | `H:3255–3275,3635–3657,3842–3862,3913–3920`、`Main:1564–1594` | 图形初始化失败时恢复线程已启动。回退调用 `StopProduct` 会命中提前返回，留下 `recoveryThread`；同一 Host 的 ULW 重试因线程仍 `joinable()` 跳过扫描。 | 将恢复扫描启动移到 Host 成功握手后，或在启动失败收口中取消、唤醒并回收线程，再重试。 |
| blocker | `H:875–912,1087–1096`、`RS:722–735` | 只有 Clear 标记时，旧记录删除失败仍可在结尾删除该标记，下次启动可借回清前墨迹。已结算的桌面标记也可能在排序前被预删。 | 保留最近桌面标记直至确认更旧来源已清理。让扫描先保留该标记供有序选择，并检查删除结果。 |
| blocker | `H:868–912`、`PS:812–822,1016–1017` | 候选文件已发布、身份尚未登记时，若属性查询遇到权限或 I/O 错误，仍会被当成缺失并从记录抹掉。 | 仅将 `ERROR_FILE_NOT_FOUND` / `ERROR_PATH_NOT_FOUND` 当成缺失。其他错误保留来源并暂缓该场景的合并清理。 |
| should-fix | `H:910–955,1022–1026,1093–1096`、`D:213` | PPT 消费标记先只写内存。若第二次启动在桌面或白板询问期间被强退，第三次启动仍可能接管原 PPT 条目。 | 用内存列表保留本轮待接管项，认领后先持久化 `settledPresentations`，再读入和放行。 |
| should-fix | `W:794–804,1118–1135,2570–2640,3920,4365–4395,4474–4487` | 暂停仍可误报原生层级已恢复，并触发预览提升。已开始的刷新和独立的 Setting 提升也可越过暂停。 | 保留跳过命令的成功回执，跳过 `ReportCommandResult` 和置顶通知。在共用提升入口与实际抬升前复查暂停状态。 |