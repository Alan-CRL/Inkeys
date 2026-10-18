# F-057 普通 contact Closing 延后回收实施记录

- 日期：2026-09-29
- 范围：`Draw3.ContactInput.cpp/.cppm` 的 route 状态与回收，`InkeysHeadlessTests/draw3_contact_tests.cpp` 的无窗口生产状态机回归。
- 依据：[实施前设计](normal-contact-closing-liveness-design.md)、[独立设计复审](normal-contact-closing-design-review.md)。本单元未修改 Controller、Host、RTS、Idt、工程配置或模型/视觉。

## 旧行为与红证

普通消费者的 `DiscardUntilTerminal` 在真实 Up/Cancel producer 已把同 generation route CAS 为 `Closing` 后无限 `YieldProcessor`。只让消费者直接 return 会在 producer 随后转 `ConsumerOwned` 时失去唯一回收 handle。先用既有 `PauseNextCloseAfterRouteClosedForTesting` 在 CAS 成功后暂停真实 Up，Headless 测试让普通 `DiscardUntilTerminal` 在另一线程执行，100 ms 内断言返回；之后先放行 producer，再 join，两线程与测试进程都自然结束。旧版完整 `InkeysRepo.sln Debug|ARM64` 构建 exit 0，`InkeysHeadlessTests.exe --no-window` 自有 PID 7336 自然 **exit 1**，仅新增断言 `ordinary Discard returns while Close remains in Closing` 失败，后续 occupiedSlots/recycled 清理仍通过。原始日志：`TestResults/release-hardening/f057-closing-red-debug-arm64-build.log`、`f057-closing-red-debug-arm64-headless.{stdout,stderr}.log`。

## 状态与所有权

低三位 route 加 `ClosingDiscarded=6`，不改变既有编号。正常 `Producing→Quarantined` 不变；消费者遇精确 generation 的 `Closing` 时 CAS 到 `ClosingDiscarded` 即返回，同一 handle 再次 Discard/Recycle 不重复计数。若 producer 先转 `ConsumerOwned`，仍由消费者 `Recycle→Free`。`Recycle` 遇 Closing 也交出 handle，覆盖普通 Controller 在 Cancel/Up 重叠时的回收调用。

唯一 `Close` producer 在取得 writer latch 后，用一次 route 读验证同 generation 的 `Closing` 或 `ClosingDiscarded` 及 contact 身份；终态快照发布并释放 latch 后分别尝试 `Closing→ConsumerOwned/Free` 与 `ClosingDiscarded→Free`。只有成功把 route CAS 到 Free 的 owner 执行 `ReleaseSlot`、`recycled` 计数和控制唤醒；若原 route 是 Quarantined，其计数也仅由该成功分支减一次。`ClosingDiscarded` 保持槽占用直到 producer 完成，不会提前让新 generation 重用正在写的 record。终态参数坏包先读最后稳定样本；若它也无效，则回退已验有限的不可变 Down 位置完成 Up/Cancel。Down 自身受内存破坏时不伪释放槽。

`TryReadSnapshot` 在读快照前后各读取一次完整 route，单次同时核 generation 与允许状态，只允许 Producing/Closing/ConsumerOwned；`ClosingDiscarded` 在两个观察面均拒绝。终态快照仍只允许 ConsumerOwned 返回。旧 handle 在 slot 再用后由 generation 检查拒读。

## 测试与当前结果

绿版 Headless 用真实 Up/Cancel 的 CAS 后 pause hook 覆盖普通 Discard、预先 Quarantined、`Recycle`、重复 Discard、无效终态位置、producer 先到 ConsumerOwned 逆序；断言 producer 暂停时消费者有界返回、slot 仍占用且不可读，放行后 `occupiedSlots=0`、`recycled=1`、Quarantined 计数归零，再在同一 record 上复用新 generation，旧 handle 不可读/不被承认。完整 `InkeysHeadlessTests.exe --no-window` 自有 PID 59316 自然 exit 0；产品早期 `--draw3-parked-desktop-exit-test` 自有 PID 32980 自然 exit 0，Desktop/FatalClosing/FatalActiveInk 三项 PASS。日志 `f057-closing-green-debug-arm64-headless-1.*`、`f057-closing-green-debug-arm64-cli.*`。

首次完整 `InkeysRepo.sln Debug|ARM64` 绿版 Build 在源码编译后由 Visual Studio ARM64 `link.exe` 自身 C0000005 崩溃，`LNK1000 Internal error during IncrBuildImage`，exit 1；Headless 目标仍成功生成并通过。没有改源码/工程/签名或工具链来消除环境错误；**原命令仅重试一次**，完整 Solution exit 0、0 error。两份日志分别为 `f057-closing-green-debug-arm64-build-1.log` 与 `f057-closing-green-debug-arm64-build-2.log`。三个变更文件经 `git diff --check`，ContactInput `.cpp/.cppm` 保持 UTF-8 BOM+CRLF，既有 Headless 测试文件保持无 BOM+CRLF。当前冻结源码 blob：ContactInput.cpp `e9b1792417d7cb40c14ddca969ae84ab09a83777`、.cppm `72f565da7c8fe80f8a66e403f045ca803bd12854`、Headless tests `21ad3e729245bd749228a203b10d50a0c7fcfca3`。独立真实 diff 复审待回传。

## 剩余与停止条件

- `AbortUnqueuedDown` 只处理尚未入队的 Down，没有可用消费者交接，仍在与另一 Up/Cancel 的 Closing 竞态中等待；本修补不宣称该 producer 入口无等待，也不把 `ClosingDiscarded` 机械套到它。
- 独立 reviewer 另发现既存的普通 caller 泄漏：Controller 的“Laser 禁多指”后续 Touch Down 分支在 record 仍 Producing 时直接 `Recycle(handle)`；现 API 与旧版 API 都只处理 ConsumerOwned/Closing，后续 Up 可能转 ConsumerOwned 而无人再回收。本单元未改 Controller，须另列修复/测试，不把 F-057 的 slot 回归外推到所有拒收分支。
- `ResetForNextRun` 仍依赖 Host 停止所有输入 producer/RTS 回调且 join 绘制线程后调用；当前没有显式 in-flight Close 计数，也没有证明 COM disable/remove 必然等待所有旧回调。新状态不应在旧 producer 仍写时被 Reset 清空。该跨层 quiescence 条件由最终集成复审决定，不能用 Headless pause 测试冒充证明。
- 内存损坏导致不可变 Down 也无效时，Close 不会把可能仍在写的 record 释放，槽可持续占用；单个永不完成的 producer 也可占一槽，极端 4096 上限全占时新 Down 会拒绝。没有真实 RTS/COM、Win7 或用户现场卡顿栈证据；本单元只证明普通消费线程不因已有 Closing 自己无限等待，不声称现场截图根因已修。
- 后续仍需在冻结源码和独立 reviewer 结论下完成 Release|x64/Win32 适用完整 Solution、相关 CLI/Headless 与多轮 x64 自然退出；不能把当前 Debug|ARM64 绿灯扩写成三架构已验。
