# F3：速率橡皮静止后移动唤醒：设计

## Change Boundary

行为缺口是活动contact的Move仅更新mailbox，而速度橡皮已进入只等generation的idle循环。修改产品coordinator与Controller睡眠交接，保留移动合并、命令、终态、接触身份及算法。

## Selected Design

coordinator新增默认false的一次idle Move订阅和consumer arm/disarm接口。只有成功发布快照并通过route/identity检查的Move才以acq_rel exchange消费订阅，取到true才调用既有SignalWake，不enqueue命令/控制标记。

Controller确定进入eraser idle时先arm，再读取真实活动physical contact快照，比对lastConsumedSequence。序号变更、终态或暂时无法稳定读取都取消休眠回外层消费。确认无新mailbox数据后仍以帧初、消费前捕获的frameWakeGeneration进入WaitForWake，不能在复查后重取generation。

用作用域清理保证离开等待/异常/退出均disarm；ResetForNextRun在既有producer停止/consumer join后复位。速度橡皮的有deadline等待也使用相同交接以防Move被拖延；保留area/reconnect deadline、图形失败退避、普通帧deadline、Up/Cancel/control唤醒。

此顺序覆盖Move在arm前、arm后复查前、复查后Wait前和已睡眠四种窗口。未arm的普通Move仍不SetEvent，burst只触发一次订阅，静止不增加定时帧。

## Expected Files

- Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.cpp/.cppm：一次订阅、成功Move触发、reset及内部接口。
- Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp：真实idle交接/有限局部共用helper和已有RunDraw3ControlFenceProductionProbe回归。
- InkeysHeadlessTests/draw3_contact_tests.cpp：直接使用产品coordinator的设备、订阅/并发/终态测试。

必要时只把本次arm/复查/wait作用域提取为Run与probe共同调用的内部helper，不移动整个渲染循环，不修改独立demo替代产品，不往Host注入额外ControlWake。

## Tests and Limits

Touch/Pen/Mouse逐项Down→收敛→Move且不Up、无额外wake，真正消费者在合理调度容差内看到新sequence。用barrier或有限测试hook控制四个竞态窗口，不能靠1000ms超时通过。验证多contact、burst合并、stale handle、非有限Move、command boundary、Up/Cancel、退出/reset及有限deadline。

扩充InkeysHeadlessTests --no-window与现有 --draw3-control-fence-test 真实协议子用例。静止帧/唤醒不增长，正常Move未订阅不增加SetEvent。隐藏Host会额外wake，不单独作为本项通过证据；真实物理触摸/笔验收缺口另行报告。

## Dependencies and Rollback

功能无其他子任务依赖。与F2共享Controller时由同一实现者按F2→F3串行编辑；主会话统一构建。完整回退订阅+复查+清理组合，不能只撤其中一个环节；不commit/push。

专项依据：`.trellis/tasks/10-14-canvas-freeze-investigation/research/f2-f3-fix-plan.md`。本方案已获父最终摘要审批，当前进入实施。
