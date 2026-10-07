# 正常 Draw3 contact Closing 等待活性：实施前设计

日期：2026-09-29。关联 F-026 独立复审中的条件性 P1，用户报告的「画布卡住、UI 正常」仍未取得 dump/线程栈，**不能把本链认定为现场根因**。本设计只覆盖生产 `ContactInputCoordinator::DiscardUntilTerminal` 等待 Closing 的确定代码风险；另有 GPU/RTS/Window Service 失败路径需独立证据。

## 事实与最小行为差距

`Draw3.ContactInput.cpp::Close` 在 Up/Cancel 先将 `Producing/Quarantined` CAS 为 `Closing`，再等 writer latch、发布终态、转为 `ConsumerOwned/Free`。绘制线程在普通页边界、拒收 Down、完成笔画时调用 `DiscardUntilTerminal`；如果恰好看到 `Closing`，它无限 `YieldProcessor`。生产 Up 若永久停在 writer latch、COM 回调被阻、异常线程终止，绘制线程将不再消费新帧/命令；单纯全局提前 return 则会在 producer 后置 `ConsumerOwned` 时泄漏槽位。现有 fatal 专用路径以停止 admission 且不回收 handle 避开该等待，只对正在退出的 Controller 合法，不能套用正常运行。

目标是普通消费者在已有 `Closing` 时**立即且安全地交出路由**：同一 generation 的唯一 Close producer 之后完成终态并回收槽位，或 producer 长期不完成时最多暂占一个固定槽，而绘制线程继续运行。已接受的 Down/Move/Up/Cancel 顺序与样本内容不变；不通过丢输入、降低模型频率、改变视觉或释放仍在写的 record 来换活性。

## 状态合同候选

现 route 用低 3 位存 `ProducerState`，有未用取值 6/7。可加 `ClosingDiscarded=6`（名称待实现者按项目风格确认）。`DiscardUntilTerminal(handle)` 的现有 Producing→Quarantined 保留；遇精确 generation 的 Closing 时 CAS→ClosingDiscarded，成功即返回。CAS 若输给 producer 的 Closing→ConsumerOwned，则走现 `Recycle`，若 generation 已变/Free 则返回。二次 Discard 看到 ClosingDiscarded 也直接返回。

`Close` 在 Closing CAS 成功后持 writer latch 处理 snapshot，终态成功发布时分别处理精确 `Closing→ConsumerOwned` 与 `ClosingDiscarded→Free+ReleaseSlot+Count(recycled)`。对后者不可先开放 freeMask 再改 route，也不能在仍持 latch 时提前归还位图。`TryReadSnapshot` 不应把 ClosingDiscarded 当可交付终态。若 snapshot 错误分支要退出，需审计它是否会永久留下 Closing/ClosingDiscarded；已有有效 Down 且 writer 已锁时理论上应有有效 latest，但不以理论替代错误边界处理。`Recycle`/`AbortUnqueuedDown` 的 Closing 分支需逐调用者审查：若它们也可在正常运行碰到同一等待，应复用同一延后回收合同，避免另留忙循环。

不新增锁、阻塞队列或后台清扫线程；变更限 `Draw3.ContactInput.cpp/.cppm` 和生产模块的无窗口状态机测试。默认性能成本应只有发生 Closing 竞态时的额外 CAS，常见完整 Up 的 ConsumerOwned→Free 路径保持。状态/计数/位图的 acquire/release 与 generation 精确匹配必须由独立 reviewer 核对。

## 红绿与交错验收

1. 使用已设计的显式测试 hook 在 `Close` 成功 CAS→Closing 后、`LockWriter` 前暂停**自建** Up/Cancel producer；不得再用任意点 `SuspendThread`。消费者在 producer 暂停时调用真实 `DiscardUntilTerminal`，有界等待先证明旧实现无法在阈值内返回；红灯也必须先 resume/join 所有测试线程、自然退出，避免测试本身卡死。
2. 修补后同一调度下 Discard 有界返回；放行 Up/Cancel 后 `occupiedSlots` 回到原值，`recycled` 恰一次，原 handle 不可读；再填满/复用固定槽池以证明无泄漏、generation 不交叉。交错反向（producer 先到 ConsumerOwned）也应正常 Recycle；重复 Discard 不双重 Release。
3. 覆盖普通页切换/完成笔画至少一个**真实 Controller 调用**及生产 ContactInput 状态机；独立审查 Close 所有早退分支、Host Stop/RTS Shutdown 与 fatal 专用路径。连续多轮 x64、ARM64、Win32 相关 CLI 和 Headless、完整 Solution 构建需在代码冻结后复验。编译成功或测试夹具成功不等于现场截图复现/修复。

## 边界和停止条件

当 producer 本身永久不完成，单个槽会被占用到 Host Stop/Reset；该条件下允许绘制线程继续处理其他 contact/命令，但若所有 4096 上限槽均被异常 producer 占住仍可能拒绝新 Down，须记录为条件性残余。GPU `Present/Map` 永久阻塞、RTS COM Shutdown、保存 worker I/O 卡住、Window Service owner 死锁也不由本状态机修补。用户选择的 15 秒正式退出/重启无条件强退是进程退场兜底，不应冒充绘图期持续可用性的证明。若无法证明状态回收无 ABA/重复释放，保持现代码并把其列为发布阻塞，不做仅加 timeout 的泄漏型修补。
