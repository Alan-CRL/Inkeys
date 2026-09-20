# Draw3 低速停笔预测收敛修复

## Goal

修复普通笔在鼠标低像素速度移动后快速停下时，停笔更新被过早冻结、可见笔锋未追到最后原始位置，以及再次移动时旧预测突然甩出的回归；冻结必须发生在模型位置、预测笔锋和笔宽视觉都已收敛之后，同时保持停笔阶段的模型输出和 GPU 点数有界。

## Background

- 当前 RTS 多 contact 主循环只在 contact snapshot sequence 变化时调用 `StrokeModeler::Update`；无新 snapshot 的活动帧只调用不改变模型内部状态的 `Predict`。测试宿主与产品 `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` 保留了同一缺口。
- `UpdateIdleFreezeState` 只要求停笔时长达到 live-tip 时长且 L0 连续三帧近似不变。由于 modeler 状态没有推进，同一状态的 prediction 天然会重复，因此“视觉稳定”可能只是“输入状态没变”，不能证明 modeled tip 已追上最后 raw 位置。见 `inkStrokeModelerTest/draw3/stroke_geometry.cpp:1255`。
- `StrokeModeler::Predict` 的接口契约明确为“不改变 internal model state”，而 `Result` 暴露 position/velocity，可作为收敛证据。见 `inkStrokeModelerTest/additional/ink_stroke_modeler/stroke_modeler.h:82`、`types.h:193`。
- 旧鼠标循环会在未冻结时按墙钟持续送入同坐标 `kMove`；`f9c9ee70` 迁移到 RTS 多 contact 后漏掉了这一行为。该迁移的设计仍明确写着“动画帧仍可更新模型输出，但不伪造速度样本”。见 `.trellis/tasks/archive/2026-08/draw3-source/active/07-18-rts-multicontact-rendering/design.md:65`。
- 项目规范要求活动 contact 按帧更新停笔预测，并仅在视觉连续三帧稳定后冻结。见 `.trellis/spec/native/runtime-and-rendering.md:28`、`:390`。
- `23dcb728` 恢复 stationary `kMove` 后，模型状态能够追上 raw endpoint，但普通 Move 路径没有 `ModelEndOfStroke` 的过锚点回退；把全部 synthetic Result 直接追加为 `realPoints` 会显露欠阻尼弹簧的“前冲 -> 回摆”，形成终点折返。
- Git 历史中的 `a40801a5` 曾明确禁止用 `kUp` smoothing 重连完成尾段，以修复 foldback/double-tail；`3d52b33d` 后来把 model `kUp` tail 恢复为默认。模型 `ProcessUpEvent` 的前半段仍是无 crossing guard 的 `UpdateAlongLinearPath`，快速 Up 的中间 Result 可能先越过 raw Up，再由后半段回到终点。

## Requirements

1. 对普通笔 contact，在没有新 raw snapshot 且尚未收敛时，绘制线程必须以最后真实输入的位置和笔状态推进 modeler 的 logical time，使 mass/spring 内部状态真实追上 raw endpoint；内部推进结果不得再无条件等同于可见或持久几何。
2. 合成的停笔推进不得更新 `lastSpeedSnapshot`、滤波输入速度或制造新的速度/压力样本；模拟压感宽度保持最后真实输入建立的状态。
3. `idleFrozen` 不得只由“连续三帧 L0 相同”触发；还必须验证 modeled endpoint 已接近最后 raw endpoint，且模型剩余运动已低于同一视觉容差，避免在穿越 endpoint 的瞬间误冻结。
4. 可见 endpoint 到达 raw endpoint 后，stationary Result 只允许继续在有界 scratch 中推进内部状态，不得继续增长 shader、L0/L1 或持久点列；modeled endpoint 和 velocity 收敛后立即停止同坐标输入，再用既有三帧视觉稳定门槛确认 taper/radius 不再变化。
5. 新 raw snapshot、stylus 状态变化、Up/Cancelled、reconnect、resize 与现有多 contact 生命周期必须保持现有顺序和语义；真实输入到来时仍解除冻结。
6. 修复必须同时落在确定性测试宿主和实际产品 Draw3 的共享 RTS 路径，覆盖 MouseLeft/MouseRight 复现；产品 Pen/HardPen 共用本合同，Pen/Touch 保持同一普通笔状态机一致；不改变 Eraser、Highlighter、Laser、Shape 的既有产品行为。
7. 不修改第三方 `ink_stroke_modeler`、HLSL、CPU/GPU 数据布局、预测参数或公开配置；修改保持 minimal diff。
8. 停笔收敛期必须把最后 raw endpoint 作为硬几何边界：允许可见尾端单调逼近，但不得越过 endpoint plane 后再折返，也不得在到达最近点后重新远离 endpoint；一旦到位，只保留一个可替换/最终落定的 endpoint 点。
9. 物理 Up 必须把 raw Up 坐标作为完成态 centerline 的权威终点。`kUp` 新增的整段 Result 都必须经过同一单调/越界门禁，不能只校验 `back()`；prediction 仍不得进入 Stored Stroke。
10. 正常运动期继续保留现有 Kalman prediction 和现有 spring/drag 手感；不得用全局关闭 prediction、切换 StrokeEnd predictor、调成临界阻尼或重编三架构模型静态库来替代 Draw3 终点策略。
11. SoftPen/普通笔的快速移动 Up 必须形成明确的完成笔锋：有有效运动时，最终 endpoint radius 至少收敛到既有 fully-developed taper 下限，而不是因 `kUp` 批次时间过短留下粗圆头；纯点击/极短划和 HardPen 保留各自现有语义。
12. 若在内部尚未完全衰减时恢复真实 Move，第一批可见输出仍需从已钉住的 endpoint 朝新 raw endpoint 单调前进；不得重置整支 modeler 导致预测、压感或笔宽历史断裂，也不得重新显露旧回摆动量。

## Acceptance Criteria

- [ ] 鼠标以低像素速度绘制后快速停住，笔锋继续刷新并在有界时间内到达最后 raw 坐标；停住一段时间后再原地 Up，可见端点和粗细不发生可观察变化。
- [x] 停笔期间只有 modeler 尚未收敛时才追加 modeled output；收敛后点数停止增长，L0 连续三帧稳定后进入 `idleFrozen`，不再继续追加 modeled/L0/shader 输入重复点；为消费只更新 mailbox 的 Move，活动 contact 帧轮询可继续。
- [x] 停稳后再次移动，首帧不会把先前未消化的预测一次性甩出；新移动按真实 raw QPC/距离计算速度。
- [x] 收敛判定同时覆盖端点误差和 modeled velocity/单帧剩余位移，不能仅以 prediction 数组逐帧相等作为依据。
- [x] 30/60/120/240 FPS 对应的停笔推进保持单调时间、行为一致，且每次 `Update` 不超过既有 `max_outputs_per_call` 预算。
- [x] 多 contact 中一支普通笔停住、另一支继续移动时互不冻结或污染速度；任一真实移动都只解除对应 runtime 的冻结。
- [x] prediction disabled、无 prediction 结果、modeler Update/Predict 失败时保持有界降级，不死循环、不无限增长点列。
- [x] Eraser、Highlighter、Laser、Shape 的既有运行路径与视觉语义不变。
- [x] ARM64 `Debug|ARM64` 的 `inkStrokeModelerTest.sln` 与 `InkeysRepo.sln` 完整构建通过；新增模型级回归测试、现有测试和 `InkeysHeadlessTests.exe --no-window` 通过。GUI 人工复现若本会话未获授权，则明确列为用户侧验收。
- [ ] 低速、中速和高速瞬停时，所有接纳为可见几何的新增中心点对 stop axis 单调逼近 raw endpoint，前向投影不超过 `0.05px`，不存在“前冲后折返”；prediction enabled/disabled 均满足。
- [ ] 可见 endpoint 钉住后继续按住 10 秒，real/L0/L1/shader/Stored 候选点数不再增长；内部 model scratch 在 `<=200ms` 诊断预算内收敛并停止 Update，超预算只报错/失败测试，不靠继续产点掩盖。
- [ ] 高速 Move 后立即 Up、Up 再前进少量和 Up 同位三类轨迹中，整段 terminal accepted points 都不越过 raw Up plane、不在最近点后远离，Stored Stroke 最后 center 精确落在 raw Up 容差内。
- [ ] SoftPen 快速 Up 没有肉眼可见的大圆头或轴向突出；纯点击仍是正常圆点，HardPen 只保留正常半笔宽 round cap 而无 centerline 外延。
- [ ] visual-pinned 但 internal-unsettled 时恢复 Move，首批输出不向旧方向回摆，不释放旧 prediction；随后恢复正常 Kalman 运动期行为。

> 状态说明（2026-09-20）：确定性模型回归已直接断言 30/60/120/240 FPS、三种速度及三种 prediction 模式的 `internal settled <=200ms`；此前仅验证 visual-pinned 时间的断言已移除。上述复合验收项仍保持未勾选，因为 real/L0/L1/shader/Stored 的完整产品帧级点数计数与真实设备 GUI 矩阵尚未完成端到端验证。

## Out of Scope

- 调整 Kalman、wobble、spring/drag、live-tip 时长或预测时域参数。
- 修改或重新产出第三方 `ink_stroke_modeler_merge.lib`，或在一笔中途 Reset modeler 来伪造零速度终点。
- 修改 HLSL、shader buffer、笔迹格式或渲染层架构。
- 借本任务重构多 contact、reconnect、速度模拟压感或其他工具。
- 以固定额外延迟替代模型收敛判定。

## Technical Notes

- 建议复用当前 `0.05px` L0 位置视觉容差，并按当前目标帧间隔把 `Result::velocity` 换算为“下一帧剩余位移”；端点误差与该位移均低于容差后才视为模型追上。现有三帧 `AreL0VisualsClose` 继续负责 prediction、taper 与 radius 的最终视觉稳定确认。
- 停笔推进时间以本帧 QPC 转换后的 logical time 为上限，并保持 `lastModelInputTime` 单调；不触碰真实 raw snapshot 的 QPC 和速度基准。
- `StrokeModeler` 没有公开 settled/confidence 标志；继续用 modeled position/velocity 作为内部收敛证据。`predictedResults.empty()` 只能作为 prediction confidence 的间接现象，不能替代终点门禁。
- endpoint 门禁建议复用 `0.05px`：候选到 endpoint 的距离不得增加超过该值，沿最后非退化真实方向不得越过 endpoint plane 超过该值；缺少有效方向时退化为距离单调约束。
- 详细证据见 `research/root-cause.md`、`research/git-regression-analysis.md`、`research/modeler-stop-up-semantics.md`，修复状态机见 `design.md`。
