# F-057 普通 contact Closing 活性最终独立只读复审

审查时点：2026-09-28 23:27:26 UTC；冻结文件 Git blob：`Draw3.ContactInput.cpp=e9b1792417d7cb40c14ddca969ae84ab09a83777`、`.cppm=72f565da7c8fe80f8a66e403f045ca803bd12854`、`InkeysHeadlessTests/draw3_contact_tests.cpp=21ad3e729245bd749228a203b10d50a0c7fcfca3`。参考调用链 `DrawingController.cpp=e1c40b657d2fcb5b332ef40b69bc110547288fac`；本轮只读源码、设计/既有复审与实施日志，不改产品/测试/共享任务文件，不运行构建/GUI/性能采样。截图中的画布卡死没有线程栈，不能仅因本状态机绿灯认定现场根因已找出。

## Findings (fixed in candidate)

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.cpp:40-55,837-882`：新增 `ClosingDiscarded=6`，与 generation 同编码在 route 中。普通 `DiscardUntilTerminal` 保留 Producing→Quarantined，看到精确 generation 的 Closing 才 CAS→ClosingDiscarded 并立即返回；CAS 输给 producer 终态则走 ConsumerOwned→Free 的原回收。`Recycle` 在 Closing 也用同一延后交权，故不会在绘制线程无限自旋。重复 Discard 在 Quarantined/ClosingDiscarded/Free 返回，旧 generation 的 CAS 不会更改新槽。
- `Draw3.ContactInput.cpp:564-640`：Close 在获得 writer latch 后一次 route 读取同时核 generation、Closing 或 ClosingDiscarded、原 tablet/contact 身份；这关闭了设计复审指出的「只认 Closing 而提前返回」缺口。非法终态位置先读有限 latest，失败再用本 generation 不可变且经 Down 有限校验的 `DownSnapshot`；只有该源也损坏才保持占用并返回，不将可能仍在写的槽伪装 Free。发布终态并解 writer 后，精确 `Closing→ConsumerOwned/Free` 与 `ClosingDiscarded→Free` 两条 CAS 竞争，只有成功者按 `quarantined` 原所有权恰减计数、ReleaseSlot 和计 recycled。`ReleaseSlot` 在 route 已 Free 后才开放 freeMask；后续日志/wake 不再访问该 record。未见这两条正常分支的双释放或跨代写入。
- `Draw3.ContactInput.cpp:815-835`：`TryReadSnapshot` 在读取多字段快照**前后**核同 generation 且 route 为 Producing/Closing/ConsumerOwned，ClosingDiscarded、Quarantined 和 Free 均拒绝；Up/Cancel 终态只在 ConsumerOwned 交付。被消费者放弃的旧 handle 不能在 producer 完成后读到伪有效终态。

## Findings (not fixed / residual)

### P2：既存的 Laser 多指忽略分支仍可能泄漏 Producing 槽位

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:5172-5177` 在禁用 Laser 多指时，对后续 Touch 的已入队 Down 直接调用 `input_.Recycle(handle)` 然后丢弃 handle。该 record 若仍 Producing，`ContactInput.cpp:837-855` 的 Recycle 既不设 Quarantined 也不释放；稍后真实 Up 经 `Close:564-640` 进入 ConsumerOwned，已无 Controller owner 再回收，直到 Host Reset 前保持占用。该调用在 H0 已存在，不是 F-057 新引入；重复的额外 Touch 可能耗尽有界 slot 池，须单独用该真实 Controller 分支或生产 ContactInput 轨迹红测并修，不能把 F-057 的 Closing 绿灯外推为所有输入槽零泄漏。

### P2：`AbortUnqueuedDown` 与 Host 停机交错未被本补丁证明

- `Draw3.ContactInput.cpp:536-562` 的未入队 Down 回滚在并发 Up/Cancel 已赢 Closing 时仍可 `YieldProcessor` 等其终态；由于 Down 未入 ingress，普通消费者没有 handle 可提交 ClosingDiscarded，新方案不应机械复用 Discard。若 producer 永久停在 Closing，此 producer 回滚调用仍可能卡住，需单列活性范围和故障注入。当前 Headless 的新 Closing 测试不覆盖此分支。
- `Draw3.RealtimeStylus.cpp:2615-2650` 的 Shutdown 先 disable/remove RTS plugin、再 `CloseAllProducerContacts`；后者（`ContactInput.cpp:921-949`）只扫描 Producing/Quarantined，已在 ClosingDiscarded 的旧 Close producer 应自行完成 Free。`Host.cpp:1325-1363` 随后 drain/join，下一次 Start `:1042` 的 `ResetForNextRun` 会无条件清 writerLatch/route/freeMask（`ContactInput.cpp:1053+`）。源码没有独立 in-flight callback 计数；安全复用依赖 COM disable/remove 真正使既有同步回调静止。若该保证在异常 RTS 中不成立，旧 writer 可在 Reset 后继续写新 generation；本轮没有受控 Stop/Reset 与暂停 Close 的测试或真设备证据，故这是条件性所有权门禁，不将其宣称已发生。

### P2：测试覆盖生产状态机，但不覆盖完整 Controller/Host 使用者

- 新 `TestClosingDiscardLiveness`（`InkeysHeadlessTests/draw3_contact_tests.cpp:507-642`）直接调用生产 `ContactInputCoordinator`，不是复制一套状态算法。测试 hook 在**真实 Close CAS→Closing 之后**暂停 Up/Cancel，检查 Discard/Recycle 在放行前有界返回、仍占一槽且不可读；放行后核 `occupiedSlots=0`、`recycled=1`、`terminalPublished=1`、Quarantined 归零、重复 Discard、同一 record 新 generation 与旧 handle 失效。用例含 Up/Cancel、原 Quarantined、Recycle、非法终态和 producer 先完成，且登记于 `RunDraw3ContactInputTests:990-1008`。这足以支撑局部 CAS/位图合同的红→绿。
- 但它没有运行 Controller `sealPresentationContacts:6144`、拒收 Down `:6190-6205` 或正常完成回收 `:10175` 中任一真实调用，也没有测试 `AbortUnqueuedDown`、Host Stop/RTS Shutdown、全池填满或非法终态 fallback **所选坐标**；这些是产品整链/容量验收缺口。坏终态用例只断回收计数，建议在 Recycle 前读生产 snapshot 验证取自本代 Down 而非坏 NaN。Win7、真实笔/触摸与旧 `ProductRunning()==true` 的用户卡死现场均未测。

## 构建与成本证据口径

- 实施日志 `f057-closing-red-debug-arm64-headless.stderr.log` 保留单一红断言 `ordinary Discard returns while Close remains in Closing`，进程 exit1；绿 `f057-closing-green-debug-arm64-headless-1.*` 记录 Headless exit0，生产 fatal/desktop CLI 的 `[Draw3FatalClosing]` 与 `[Draw3FatalActiveInk]` 仍 PASS。首次完整 Debug|ARM64 Solution 候选 Build 在 `link.exe` 的 `IncrBuildImage` 报 `LNK1000`/C0000005（`f057-closing-green-debug-arm64-build-1.log`），按实施者记录同命令重试退出0、0 error（`build-2.log`）；前者属 linker internal 环境故障，不能用后者抹掉，也没有据此改源码/工程。重试为增量 Build，不能冒称全量 clean Rebuild。本 reviewer 没有重跑任一命令。
- 生产常规 Move/帧路径未新增锁、I/O 或队列扫描；Close 每次保留既有测试 hook 的一次 atomic load，新增 route 快照与第二 CAS 仅在终态/Closing 竞争发生。`TryReadSnapshot` 把先后 generation/state 读取合成两次 route 读取，保持有界；这只是静态成本判断，未经 CPU/尾延迟采样。一个永久卡住的 producer 仍会占一个 slot；所有槽均卡住仍可拒绝新 Down，F-057 不解决 GPU/COM/Window owner 其它挂起。

## Verification

- 只读检查冻结源码与上述现有红绿日志；`git diff --check H0 --` 限三份已跟踪目标退出0。未独立运行 lint、type-check、构建、测试、GUI 或性能采样，符合本次分工。F-057 的局部 Closing CAS/回收问题静态关闭；Laser 多指既存泄漏、Abort 回滚及 Host/RTS 停机静止、真实 Controller 交错仍需按各自证据处理。最终 HF 若再修改这三文件或依赖层，需重审/复验。
