# F3 独立检查记录

日期：2026-10-15。检查角色：`trellis-check`（`/root/check_f2_f3`）。本记录是源码/合同检查，构建和运行由主会话串行执行；不得将静态结论转记为测试已通过。

## Context and Scope

已加载本子任务和 F2 的 PRD、design、implement、check.jsonl、父设计/执行计划及专项研究，并按 `spec-context-loading.md` 完整分块读取原始 `input-and-ink.md`（674 行）与 `draw3-integration.md`（440 行）。另核对原生 C++、构建、错误/生命周期及橡皮规范。注入上下文的保存文件已读取。

检查 `Draw3.ContactInput.cpp/.cppm`、`Draw3.DrawingController.cpp`、`InkeysHeadlessTests/draw3_contact_tests.cpp` 的实际生产/测试路径。共享 Controller 在 F1 日志迁移期间仅只读；主会话开始编译后所有编译源冻结。本检查者未修改产品或测试源码。

## Findings (fixed)

无本检查者自行修正的 F3 问题。

## Findings (not fixed)

未发现需要修改的 F3 生产逻辑问题。编译和运行结果待主会话；不以本次静态检查宣称动态回归通过。相邻 F1 独立测试消费者登记缺口见 F2 `research/check-review.md`，已单独报告主会话协调，不重复算作 F3 缺陷。

## Production Protocol Review

- `idleMoveWakeArmed` 默认 false；成功 Move 在真实 snapshot 发布、route/identity 复核和 writer 解锁后执行 `exchange(false, acq_rel)`，仅消费到 true 时复用 `SignalWake`。非法位置、缺 route、竞争失败或隔离 route 不消费订阅；普通 Move 无订阅时仍不 SetEvent，不产生 ingress/control/command marker 或 reservation。
- 唯一消费者 `ArmIdleMoveWake`、`DisarmIdleMoveWake` 都用 acq_rel RMW。Move 在 arm 前完成时，其释放 RMW 与随后 arm 的 acquire 形成可见性交接；arm 后真实 `TryReadSnapshot` 对比 `lastConsumedSequence`，因此这一窗口不会仅依赖事件。Move 在 arm 后发生则消费订阅并推进已有 generation；等待使用消费前的 `frameWakeGeneration`，没有在最后复查后重新捕获通知代次。
- `WaitForEraserIdle` 对所有未 ended、未 reconnect 的真实 contact 做复查；新 sequence、终态或暂时读不稳定均返回外层消费。作用域析构覆盖普通返回、deadline、退出和异常，避免遗留订阅。`ResetForNextRun` 在原 producer 停止/consumer join 合同下复位新增字段，没有扩大允许并发 Reset 的范围。
- Run 与回归共用同一 `EraserContactsIdle` 和 `WaitForEraserIdle`。原 predicate 的 physical contact、navigation、SpeedEraser 动画/idle 条件保持；有/无 area/reconnect deadline 都经过同一握手。面积/reconnect deadline 计算、普通活动帧 deadline、图形失败退避及 Up/Cancel/control/command 协议未改。
- 多 contact 共用一次订阅是预期语义：首个有效 Move 令唯一 consumer 前进，其余快照合并为最新值；下一次等待再次 arm/recheck。这里没有每 packet 唤醒、额外 ControlWake 或将超时后定期渲染当修复。

## Regression Fidelity and Limits

新增 `RunEraserIdleWakeProductionProbe` 实际挂接既有 `--draw3-control-fence-test`，使用产品 coordinator、真实 SpeedEraser 收敛和 Run 共用 helper。Touch、Pen、MouseLeft、MouseRight 各覆盖 arm 前、arm 后/复查前、复查后/Wait 前以及已进入等待四个窗口，并分别有/无 deadline，共 32 组。事件屏障和实际 Wait 调用计数控制交错；250 ms 内未完成先记失败，之后才发退出清理通知，因此失败清理 wake 不能把结果改成成功。另检查有限 deadline、已请求退出、异常 disarm、终态和 stale handle。

headless 直接编译产品 coordinator，新增普通 Move 不 wake、非法/缺 route 不消耗订阅、burst 一次 wake、四 contact 并发合并、Quarantine/Cancel 和 Reset 用例。既有 control-fence/command 顺序测试继续执行；生产代码中的唯一新增信号不写 ingress，保持命令边界。

上述 helper probe 不运行完整 Host/Controller::Run/GPU，也不构成真实物理设备验收。现有隐藏 Host 每个注入阶段的额外 ControlWake 会掩盖原缺口，不能作为本项核心证明。实际触摸/笔、Win7 和多屏结果须单列；不能把本项解释为所有跨工具永久冻结的共同根因。

## Spec Sync

建议主会话实测后把一次性 Move 订阅、arm 后真实 mailbox sequence 复查、帧初 wake generation、作用域 disarm 和静止 Reset 边界一并写入 ContactInput/idle 合同；不能只记录一个布尔标志或将普通 Move 规则改写成每包 wake。

## Verification

- Lint/static format：`git diff --check` 对五个 F2/F3 检查文件返回 0。
- Encoding/EOL：目标文件均为严格 UTF-8；Draw3 `.cpp/.cppm` 保持 BOM，headless 测试无 BOM；工作树 CRLF、无 bare LF。Git 自动换行转换为 `i/lf w/crlf attr/text=auto`，无整文件格式 diff。
- TypeCheck：待主会话完整 Solution Debug|ARM64 编译；本检查者没有运行并发编译或修改构建缓存。
- Tests：本检查者未运行。主会话应运行 `InkeysHeadlessTests --no-window`、`--draw3-control-fence-test` 及匹配的集成回归，按实际退出码/输出记录结果。

## Reviewed Source Identity

本次审阅结束时工作树 SHA-256：

- `Draw3.ContactInput.cpp`：`142ec81c25d0d8c716cf6f69e94e022d0c8afb3328c10e2992742ae68e340c59`
- `Draw3.ContactInput.cppm`：`a99d173d3919f2193eef9b6b577f06879b56f04aabc2a06c861a0b750de3db67`
- `Draw3.DrawingController.cpp`：`5d604bbe4cd8ba7213b37eabe84e2c66190c081fc5a38f4ee141fea7dc0486e3`
- `draw3_contact_tests.cpp`：`83da8fd3195d7991c78d571374d374d93473523486b47e365e49ec7a58ea4b42`

后续源变更须按新 diff 补审并重跑相关验证。未创建 commit/push。
