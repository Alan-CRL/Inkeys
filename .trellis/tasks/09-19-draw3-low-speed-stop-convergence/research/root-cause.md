# 低速停笔预测冻结回归调查

## 结论

根因不是 shader 自身停止，也不是 Kalman 参数突然失效，而是 RTS 多 contact 迁移后丢失了停笔期间对 `StrokeModeler::Update` 的同坐标推进。当前代码在无新 raw snapshot 时只反复执行 `Predict()`；该调用不改变 modeler 内部状态，导致 L0 很快逐帧完全相同，现有三帧稳定门槛把“未推进”误认为“已收敛”。下一份真实 Move 到来时才让 modeled tip 继续追 raw endpoint，于是旧欠账与新 prediction 一起甩出。

## 证据链

1. 测试宿主 `drawing_controller.cpp` 与产品 `Draw3.DrawingController.cpp` 的 `consumeLatestSnapshot` 都只有 snapshot sequence 变化才调用 `modeler.Update`；没有新 snapshot 时直接返回。
2. 两侧每帧都会更新 wall-clock `logicalInputTime`，但原实现只调用 `Predict` 和重建 L0，没有把该时间送进 modeler。
3. `stroke_modeler.h:82-93` 明确说明 `Predict` 不改变 internal model state。
4. `stroke_geometry.cpp:1255-1269` 的冻结条件只有：停笔达到 live-tip 时长、L0 连续三帧位置/半径变化小于 `0.05px/0.02px`。它不检查 modeled tip 到 raw endpoint 的误差或 velocity。
5. 因此固定 modeler state 会产生固定 prediction，三帧后必然冻结；这与用户所见“停住后笔锋没追上，再移动突然甩出”完全吻合。
6. `f9c9ee70` 前的鼠标循环在 `!idleFrozen` 时每帧用当前鼠标位置和递增 logical time 调用 `modeler.Update(kMove)`；该提交迁移到 RTS 多 contact 时删除了旧循环，却未在新 runtime 中加入等价的 stationary advance。
7. 迁移任务设计 `.trellis/tasks/archive/2026-08/draw3-source/active/07-18-rts-multicontact-rendering/design.md:65` 已规定：“只有 sequence 变化时才消费真实快照、计算速度；动画帧仍可更新模型输出，但不伪造速度样本。”当前实现与该设计不一致。
8. 当前规范 `.trellis/spec/native/runtime-and-rendering.md:28` 也规定“活动 contact 仍按帧更新停笔预测”。
9. `native-desktop/input-and-ink.md` 确认产品输入主链位于 `Draw3.RealtimeStylus.*`、`Draw3.ContactInput.*` 和 `Draw3.DrawingController.*`；因此只修测试宿主不能修复用户实际使用路径。

## 为什么不是延后固定毫秒数

单纯把 live-tip timeout 或稳定帧数调大只能降低复现概率，不能证明 modeler 已追到 raw endpoint；不同 FPS、速度、DPI 和 spring 状态下仍可能提前或过晚。过晚又会持续生成同坐标 modeled points，增加 L0/L1、Stored Stroke 和 shader 输入点数，正好触发用户担心的重复点问题。

## 可用收敛信号

- `Result::position`：最后 modeled endpoint 到最后 raw endpoint 的误差。
- `Result::velocity`：按目标帧间隔换算后的预期单帧剩余位移，可防止 tip 在高速穿越 raw endpoint 时仅因某一帧位置接近而误判。
- `AreL0VisualsClose`：现有 position/radius 三帧视觉稳定门槛，适合做最终 prediction/taper/radius 确认，但不能单独证明 modeler 收敛。
- `SamplingParams::end_of_stroke_stopping_distance`：当前配置为 `0.001px`，是 Up 收尾的库内停止尺度；实现可用作硬下限/测试参考，但冻结的可见判定应与现有 `0.05px` 视觉容差一致，避免为了不可见精度生成大量点。

## 建议状态机

`RawMoved -> SettlingModel -> StabilizingVisuals -> Frozen`

- `RawMoved`：消费真实 snapshot，更新真实速度/压感，清零稳定计数。
- `SettlingModel`：本帧没有 model input 时，以最近接受进入模型路径的 snapshot position/stylus state 和单调 logical time 送入合成 `kMove`；传 `inputSpeed=-1`，不更新真实速度基准。
- `StabilizingVisuals`：modeled endpoint 误差和 velocity 位移都进入视觉容差后，不再送合成 `kMove`，只重建 prediction/L0 并沿用三帧视觉稳定计数。
- `Frozen`：停止 stationary model advance；真实 Move 或 stylus 变化立即回到 `RawMoved`。

## 风险

- 合成 input time 若超前于并发到达但尚未消费的 raw QPC，下一 raw input 会被现有单调夹紧压缩到 `lastModelInputTime + 1us`。实现应只推进到已捕获 frame QPC，并加入专项测试验证恢复首帧没有突跳。
- 直接复用普通 `AppendNewModeledPoints` 会把所有 stationary output 留进 `realPoints`。必须在模型收敛后立刻停止 Update，并用点数上限测试防止长按线性增长；如实际测量仍产生过多近重复点，再在 idle-only 转换入口按现有视觉 position/radius 容差合并，而不改真实 raw 路径。
- 多 contact 的 synthetic advance 必须逐 runtime 独立执行，不能共享时间、raw endpoint 或稳定计数。

## 历史检索

`trellis mem search` 未找到额外历史对话命中；权威证据来自当前源码、Git 提交 `f9c9ee70` 及其已归档 Trellis 设计。
