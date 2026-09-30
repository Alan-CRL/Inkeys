# Laser 禁止多指时的 Contact slot 寿命

日期：2026-09-29。F-057 独立复审发现起点前即存在的另一条确定缺口；不归咎 F-057 新态，也没有用户现场线程栈证明这就是截图根因。

`Draw3.DrawingController.cpp::initializeStroke` 在 Laser 已有活动 Touch 且 `laserMultiTouchDrawingEnabled_ == false` 时，对第二个已出队 Down 直接 `input_.Recycle(handle)` 并返回。此时 record 仍是 `Producing`，而 `Recycle` 的终态合同只处理 `ConsumerOwned`，遇非 Closing 即返回。物理 Up 后 `Close` 把它转 `ConsumerOwned`，Controller 已丢失 handle，无人再回收；重复被忽略的手指可耗尽固定 contact slot 池，使后续 Down 被拒。用户图称画布卡住但本轮尚无其设备/日志，不能据此确定同一触发。

最小修正是在该生产分支用现有 `DiscardUntilTerminal(handle)`：在仍 Producing 时精确 generation 转 Quarantined，保留物理路由到 Up/Cancel，届时唯一 producer 自动 Free/ReleaseSlot。若 Up 正并发 Closing，则 F-057 的延后回收态保护绘制线程不忙等。不得把第二根手指加入 Laser 笔迹、丢弃第一根必要样本、改变多指开关或全局放宽 `Recycle` 终态合同。其它 `input_.Recycle` 调用点需核是否都在已终态/显式 Cancel 后。

红绿：至少使用真实 `ContactInputCoordinator` 先 PublishDown→Dequeue，模拟被拒而调用旧 Recycle，再 PublishUp/Cancelled 与诊断 occupiedSlots；红版证明仍占位，新版采用**产品当前分支入口**更理想；若只能用直接状态机测试，必须与实际 Controller 调用行的静态证据合并、明确没有跑完整 Host Laser 多触点。绿版核多轮 Down/Up/Cancel、槽位复用、generation、第一根活动 contact 不受影响。完整 Solution Debug|ARM64 与 Release 可得架构、Headless/隐藏 Host适用用例；Win7/真 Touch/激光动画仍是人工。此修补只涉及 Controller 一个分支和必要测试，独立 reviewer 核实际 diff。
