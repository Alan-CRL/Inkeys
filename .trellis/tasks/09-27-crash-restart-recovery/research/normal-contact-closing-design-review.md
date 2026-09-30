# 普通 contact Closing 活性方案独立设计复审

审查时点：2026-09-28 22:57:25 UTC。当前未冻结源码 Git blob：`Draw3.ContactInput.cpp=2346d87e6cc4f94d9782d3187932410c140d4b8a`、`.cppm=87e6377e9d0392fb453a7e26dd469fbdc4cdff67`、`Draw3.DrawingController.cpp=e1c40b657d2fcb5b332ef40b69bc110547288fac`、`Draw3.Host.cpp=63f6518d3aa1f419fb0835e954038df8579ea72f`、`Draw3.RealtimeStylus.cpp=4b193591f70fa55e009bc970c419cedc50696d15`。实施者正在改 ContactInput 测试 hook；本报告只评 `normal-contact-closing-liveness-design.md` 与此时实际状态机，不把快照当最终实现。未改产品/共享任务文档，未运行构建、测试或性能采样。

## 结论

设计的 `Closing→ClosingDiscarded` 单代 CAS 思路可让**普通绘制消费者**不等 Up/Cancel writer，同时让原 `Close` producer 最终回收槽位；它比对 Closing 简单 timeout/return 更符合当前 route/freeMask 所有权。实施前必须解决下述 Close 早退和 Host producer 静止证明，尤其不能只添枚举与 consumer CAS 就报绿。用户画布卡住的现场根因未取得线程栈，不能由本静态路径推断。

## 设计补强项（未修）

### P1：Close 中途的旧 `Closing` 精确检查会把新状态遗留为永久占用

- `Draw3.ContactInput.cpp:558-616` 的 `Close` 先 CAS `Producing/Quarantined→Closing`，然后在 `:580` 获取 writer latch；目前 `:581-585` 只接受仍为 `Closing` 的 route。若普通消费者按新设计在二者之间成功 CAS `Closing→ClosingDiscarded`，`Close` 会提前返回 false、既不发布 Up/Cancel 终态也不归还 freeMask。设计虽要求 `ClosingDiscarded→Free`，但必须**同步修改这里的后置 guard**：接受同 generation 的 Closing 或 ClosingDiscarded、保留原 tablet/contact 身份；最终根据实际 route 和原 `quarantined` 所有权做一次终态转换。只修改结尾 `:602-610` 的 CAS 不足以修复。
- `:587-594` 在终态位置非法且最后快照也无效时同样直接返回，可能遗留 Closing/ClosingDiscarded；有效 Down、writer 已锁的正常路径通常有有限合法 last snapshot，但这不是错误边界证明。需明确该分支如何安全停在占用状态/诊断，或以同 generation 的可信 Down 位置完成 Cancel；不能因无效输入释放仍在写的槽位。红绿测试需覆盖 guard 两侧及这一早退路径。

### P1／条件性：Host 重置依赖 RTS 回调真正静止，源码未有显式 in-flight 计数

- 新延后回收允许 `Close` 在 ClosingDiscarded 等待 writer；`Draw3.RealtimeStylus.cpp:2615-2650` 的 Shutdown 先 disable/remove plugin，再 `CloseAllProducerContacts`，后者在 `Draw3.ContactInput.cpp:883-911` 只处理 Producing/Quarantined，不等待已有 Closing/拟议 ClosingDiscarded。`Host.cpp:1325-1363` 在 Shutdown 后 drain/join；下一次 Start 的 `:1042` 调 `ResetForNextRun`，而 `ContactInput.cpp:1015-1025` 会无条件清 writerLatch、route 和 freeMask。若 COM disable/remove 并不保证所有既有同步回调退出，暂停的旧 Close 可在重置后继续写同一 record，形成跨 generation 的 ABA/槽位误复用；这一步不是当前源码自身证明的。
- 需取得 RTS 停止同步合同或在 Host/ContactInput 层显式等待 in-flight producer 归零后才 Reset；测试用已存在的「Closing CAS 后暂停」hook，在 Stop/Reset 邻接处验证旧 writer 释放前不重用 slot。真正 producer 永久卡住时，进程级 15 秒强退是安全退场边界，不能为了让 Reset 返回而提前清 latch。此为条件性状态机风险，未声称真实 COM 已违反合同。

### P2：其它 Closing 忙等仍在，设计仅提出审计而未给处理边界

- `ContactInput.cpp:807-822` 的 `Recycle` 在 Closing 无限 `YieldProcessor`；`AbortUnqueuedDown:530-555` 在 EnqueueDown 失败并与 Up/Cancel 竞争时也可能等 Closing。普通 Controller 初始化/取消失败分支 `Draw3.DrawingController.cpp:5535,5633,5693` 在 `PublishCancelled` 后直接 `Recycle`，可与原 Up Closing 相遇。若 goal 是「普通页切换/清屏/命令继续运行」，只改 `DiscardUntilTerminal` 还不能宣称所有消费者等待消失；至少对这些调用列可达性证明或用同 generation 延后回收方案处理。`AbortUnqueuedDown` 的 Down 尚未入队，正常没有 consumer 可提交 ClosingDiscarded；因此不应机械复用普通 Discard 的状态跳转。
- 红绿需要分别让 Recycle 与 Abort 在真实 Close 暂停点竞争，记录谁拥有最终 Release。若保留旧等待，应在任务结论明确它们不属于本次已修活性范围。

### P2：快照读取和计数合同需覆盖新状态的全部观察面

- `TryReadSnapshot`（`ContactInput.cpp:791-804`）目前初始只拒 Free/Initializing/Quarantined；拟议 ClosingDiscarded 若只在终态检查拒绝，旧 handle 仍可能读取**非终态** Down/Move 快照。消费者已交出路由时应对该状态在前后两次 route 检查均拒绝，避免测试/诊断把已放弃的路由当可用输入。`FindProducing:467-485` 当前只找 Producing/Quarantined，新增状态保持不可发现是正确的；`ContactAdmitted` 的 generation/admission 与旧记录复用需在测试中核。
- `ReleaseSlot:513-518` 是无条件 `freeMask.fetch_or`，自身不检重。Close 从原 Quarantined 进入 Closing 后，`quarantinedContacts` 已计一次，拟议 ClosingDiscarded 的最终释放仍须恰减一次；从原 Producing 进入则不得减。`DiscardUntilTerminal` 的「先加后 CAS、CAS 失败撤回」不能因新分支重复计数。两个竞争者只可由成功的精确 route CAS 所有者 Release 一次；测试要断 `occupiedSlots`、`recycled`、quarantined 计数及旧 handle 在槽位再用后的 generation 失效，而非只看 Discard 返回快。

## 测试与性能口径

- 现有测试 hook `ContactInput.cppm:157-162,243-244`/`.cpp:571-579,1003-1006` 在真实 Closing CAS 后暂停，默认空指针；生产每个 Up/Cancel 多一次 atomic load/branch，Move/帧路径不经此 hook。若正式方案只在 Closing 竞争时多一次 CAS，不引入锁、扫描或每 Move 原子轮询，预期默认负担有限；但这是静态成本判断，没有性能采样。
- 设计已有「producer 先完成」「消费者先标记」「重复 Discard」「填满/复用槽池」等基本矩阵。再补：Close 身份 guard 看到 ClosingDiscarded、无效终态 fallback、原 Quarantined 的计数、Recycle/Abort 早退、Stop/Reset 在暂停 writer 存在时的安全门、同 contact ID 但新 generation 的 Up、普通 Controller 至少一条实际页边界/完成笔画调用。红灯也须先 resume/join 所有自建线程，不能由夹具死锁制造失败。
- 单个异常 producer 可长期占一槽，池满后拒绝新 Down 是明确残余；它与现有 `ProductRunning()` 只表示 Host 线程活着、15 秒监督仅在正式退出时启动是不同能力边界。无 GUI 的状态机绿灯不能证明用户报告现场、真 RTS/Office、Windows 7 输入或双窗口可见性。

## 静态验证

仅读任务 PRD/design/check 清单、Draw3 输入规范及上述生产状态/调用链；没有改文件或执行构建、lint、type-check、测试、性能采样。Controller/ContactInput 改动未冻结，任何实现后的 PASS 仍需按最终 blob 重审并沿仓库规则做适用完整 Solution 与无窗口回归。
