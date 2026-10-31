# F3：速率橡皮静止后移动唤醒

## Goal

速率橡皮接触静止收敛后再次 Move 即可被消费，保持空闲效率及命令/终态合同。

## Confirmed Facts

- 父任务需求：R3 / F3。
- `Draw3.ContactInput.cpp:771–801` Move 只更新 mailbox，`Draw3.DrawingController.cpp:14551` idle 重复等同 wake generation；真实 Up 能解除。
- 原始报告共同根因仍未确认；本项成功不代表所有用户现场已解决。

## Requirements

1. 修复生产 Move 与 idle 等待的通知缺口，优选现有 generation/wake 机制，不靠隐藏注入的额外 ControlWake。
2. 维持 mailbox 合并、多接触 identity、Up/Cancel 终态和 commandBoundaryPending 协议。
3. 静止期间不制造帧忙轮询；有界且明确的错误/停止行为，不通过无限重复超时掩盖漏通知。
4. 覆盖 Touch/Pen/Mouse 数据路径；真实物理设备未验收部分独立报告。

## Acceptance Criteria

- [x] Move在等待前/中/后到达均可被真实生产消费链看到，不需先Up或工具切换。
- [x] 静止不忙循环；并发Move合并、Up/Cancel、多个contact、command boundary及退出回归通过。
- [x] 既有 ContactInput/SpeedEraser headless及生产控制器等待边界回归通过；隐藏测试额外wake不算核心证据。

## Scope and Dependencies

- 功能依赖：无。可独立实施、检查、回退；最终交付必须与父任务其他三项一起通过集成验证。
- 执行依赖：父任务最终规划摘要经后续用户确认后才允许 task.py start；F1合入后再进行开启诊断的跨项GUI验证。
- 共用 IdtMain.cpp / 项目登记 / DrawingController 测试块的修改由主会话按序整合；F2/F3若涉及同文件，由同一实现者先F2后F3处理，不并发覆盖。
- 完整构建由主会话串行执行，不同时写同一MSBuild中间目录。
- 已授权隔离GUI原生验证；不使用Computer Use，不修改用户安装/原配置/原视频。
- 最小diff并保留编码/换行；不commit/push，不做未证实的其他冻结修复。

## Evidence

- 父PRD：`.trellis/tasks/archive/2026-10/10-14-canvas-freeze-investigation/prd.md`。
- 专项实施前研究：`.trellis/tasks/archive/2026-10/10-14-canvas-freeze-investigation/research/f2-f3-fix-plan.md`。
- 历史实验/模型与完整产品作用域以父任务两份调查报告为准，测试须执行修复后的真实生产代码。
