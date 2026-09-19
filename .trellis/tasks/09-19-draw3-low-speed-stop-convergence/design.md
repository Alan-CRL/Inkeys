# Draw3 低速停笔预测收敛修复设计

## Boundary

修复保持在 Draw3 CPU 侧普通笔 runtime，并同步测试宿主与产品的同构实现：

- `inkStrokeModelerTest/draw3/*` 作为确定性模型回归基线。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.*` 是实际产品路径，按其现有擦除、持久化和工具分支做最小同构移植。
- 两侧 controller 负责判断本帧是否收到 model input，并在普通笔静止时请求一次 modeler advance；prediction 接口与 geometry 实现提供收敛判定和 idle freeze 状态更新。
- 不改 `additional/ink_stroke_modeler`、renderer、HLSL、CPU/GPU 数据结构及文档格式。

Eraser、Highlighter、Laser、Shape 不接入新的 stationary advance；它们的既有专用生命周期和视觉策略保持不变。

## State And Contracts

为 `ActiveStroke` 增加最小的停笔收敛状态，不引入第二套计时器：

- 最后 raw endpoint 与 stationary anchor 继续来自最近接受进入模型路径的 `RuntimeStroke::lastModelSnapshot`；亚阈值抖动只更新 `lastInputSnapshot` 时不得移动模型目标。
- `lastModelInputTime` 仍是送进 modeler 的唯一单调时间。
- `visualStableFrameCount` 继续用于三帧 L0 position/radius 稳定。
- 新增/导出纯判定函数，使用最新 modeled `Result` 的 position、velocity、raw endpoint 和 frame interval 返回 `modelSettled`。

模型收敛条件：

1. 最新 modeled position 与 raw endpoint 距离不超过现有 `kVisualStablePositionEpsilonPx`。
2. 最新 modeled velocity 在一个目标帧间隔内的位移不超过同一位置容差。
3. 数值必须 finite；缺少 modeled result 或异常值时不得报告 settled。

最终冻结条件保持为：

`stoppedLongEnough && modelSettled && AreL0VisualsClose(current, previous)` 连续三帧。

这样位置与 velocity 证明 modeler 已追上，三帧 L0 比较再证明 prediction、taper 和 radius 已稳定。

## Data Flow

1. 帧首照常消费最新 raw snapshot。
2. 对未结束、非 reconnect 的普通笔 runtime（产品 Pen/HardPen，测试宿主 Pen）：
   - 若本帧有真实移动或 stylus 变化，沿用现有真实 `Update`，清除冻结。
   - 若本帧没有 model input、未 frozen 且尚未 `modelSettled`，构造一份 stationary `kMove`：位置取 `lastModelSnapshot.position`，pressure/tilt/orientation 取最后有效模型状态，时间推进到不超过当前 frame QPC 的单调 logical time。
   - stationary `Update` 成功后调用 `AppendRuntimeModeledPoints(runtime, -1.0f, inputTime)`；不改 `lastSpeedSnapshot`、`filteredInputSpeed` 或 `hasFilteredInputSpeed`。
3. 正常调用 `Predict`、提交稳定前缀、重建 L0。
4. 重新计算 `modelSettled`，把它传给扩展后的 `UpdateIdleFreezeState`。
5. 一旦 `modelSettled`，下一帧不再调用 stationary `Update`；最多再等待三帧视觉稳定后 frozen。点列不随长按时间继续增长。

## Time Ordering

- stationary input time 由捕获的 `frameQpc` 相对 `qpcOrigin` 换算，并至少比 `lastModelInputTime` 大 `1us`。
- 不写入真实 snapshot 的 QPC，也不改变真实速度计算基准。
- 下一份真实 raw input仍走现有 `max(rawLogicalTime, lastModelInputTime + 1us)`；新增测试覆盖 stationary advance 后恢复移动的首帧。

## Failure Behavior

- stationary `Update` 失败：记录一次现有错误路径并在该 runtime 锁存停止后续合成重试，不追加点、不把状态标为 settled/frozen；下一份成功真实输入或 reconnect 解除锁存后仍可恢复。
- `Predict` 失败或为空：收敛仍由 modeled position/velocity 判定，L0 稳定比较覆盖无 prediction 情况。
- modeled result 非 finite：禁止冻结，且不把异常值送入新增判定数学。
- contact 结束、reconnect 候选或非 Pen 工具：不执行 stationary advance。

## Duplicate-Point Budget

- stationary Update 只存在于 `SettlingModel`，进入 `StabilizingVisuals` 后立即停止。
- 使用 velocity 单帧位移门槛，避免仅靠长超时等待。
- 回归测试记录停止前新增 modeled point 数，并断言 frozen 后长时间推进不再增长。
- 首次实现不在真实点转换通路做全局去重；若测试显示收敛窗口仍超过合理预算，只在 idle-only append 中按已有 position/radius 视觉容差合并近重复点，避免影响真实轨迹。

## Compatibility And Rollback

- 行为变化只影响普通笔按住静止阶段；真实 Move/Up、Stored Stroke、L0/L1/L2 提交和 renderer contract 不变。产品与测试宿主分别保留自己的演进逻辑，不为消除少量镜像代码做跨工程重构。
- 若 stationary time ordering 或点数预算验证失败，可回滚 controller 调用和两个小判定函数，不涉及格式迁移或 GPU 资源。
