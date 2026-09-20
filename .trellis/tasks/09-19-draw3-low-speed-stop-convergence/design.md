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

## Follow-up Design Amendment (2026-09-20)

上一轮的 `stationary Update -> AppendRuntimeModeledPoints(all results)` 只解决了内部模型不推进，但把欠阻尼回摆直接变成了中心线几何。本节覆盖前文中“stationary 输出直接追加真实点”的部分；position/velocity settled gate、真实速度基准隔离和三帧视觉稳定门槛继续保留。

### Revised State Machine

普通笔 contact 使用以下阶段：

1. `Tracking`：有真实 model input，沿用现有 Kalman prediction、真实点转换和速度/压力状态。
2. `EndpointSettling`：首帧没有真实 model input 时锁定 `stopAnchor`、最后非退化真实方向和进入阶段前的可见尾点。继续给 modeler 同点 `kMove`，但输出进入复用 scratch，不直接追加 `realPoints`。
3. `VisualPinned`：scratch 中首个到达 endpoint 容差、越过 endpoint plane 或不再接近的候选触发钉住；可见几何只追加/替换一个精确 raw endpoint，清空运动期 prediction。之后内部同点 Update 可以继续，但几何和 shader 点数不变。
4. `Frozen`：内部 position+velocity settled，且现有 L0 position/radius 连续三帧稳定后冻结。
5. `TerminalSanitize`：物理 Up 的模型输出单独进入 terminal scratch，经 raw Up 门禁后才写入完成几何。

该状态机不在中途 Reset modeler。Reset 会清空 Kalman、wobble、stylus projection 和 position velocity 历史，恢复 Move 时更容易出现接缝；内部继续衰减、可见几何单独钉住的风险更小。

### Endpoint Admission Contract

- `stopAnchor/rawUp` 是硬边界。以阶段起点到 endpoint 的向量为 approach axis；若该向量退化，则使用最后非退化真实输入方向；两者都不可用时只执行距离单调检查。
- 候选只有在 `distance(candidate, endpoint) <= previousAcceptedDistance + 0.05px` 且前向投影不超过 endpoint plane `0.05px` 时才可接纳。
- 第一次越界、距离反增或进入 `0.05px` 后，不再接纳后续 model Result；视觉尾端改为精确 endpoint。内部 Result 仍可更新 convergence snapshot，但不进入 real/L0/L1/Stored。
- stationary scratch 每次调用前清空并复用容量；只保存最后内部 Result、门禁所需少量状态和一个可见 endpoint，不让 `modeledResults` 随静止时间增长。
- 进入 `EndpointSettling` 后暂停 Kalman future extension。正常 Tracking 的 prediction 不变；新真实 Move 到来后，首批输出仍受从 pinned endpoint 到新 raw endpoint 的恢复门禁，确认单调向前后再恢复常规可见转换。

### Terminal Up Contract

- 仍调用 `StrokeModeler::Update(kUp)` 完成库生命周期，但将输出写入独立 terminal scratch，而不是先全量追加 `realPoints` 再补最后一点。
- 从 pre-Up accepted tip 到 raw Up 对整批 terminal Result 应用 endpoint admission；拒绝首个越界/反向候选以及其后的返回段，最后至多追加一个精确 raw Up 点。
- 完成态继续排除 prediction。Stored Stroke、首次 L2 raster 和后续重放必须消费同一份 sanitized centerline，不能只在即时 L0 隐藏坏点。
- 对已有有效运动的 SoftPen，完成 taper 应向前取得足够上下文，并让 raw Up endpoint 达到既有 fully-developed taper floor；不能因 terminal scratch 时间跨度短而留下大圆 cap。纯点击/极短划保持圆点，HardPen 不强加 SoftPen taper。

### Timing And Budget

- 不增加固定长延迟。第一帧缺少真实 model input 即可进入 settling；如后续分析表明正常 RTS 帧间空洞会误触发，只允许加入最多一个目标帧的 sample-age 门槛，并以轨迹测试证明必要性。
- internal settled 继续使用 `error <= 0.05px && |velocity| * targetFrameInterval <= 0.05px`；`<=200ms` 只作为默认 120 FPS 的测试/诊断上限，不是通过继续追加几何实现的等待时间。
- `VisualPinned` 后 real/L0/L1/shader/Stored 候选点数严格恒定；后台 model scratch 有界，内部 settled 后停止帧推进。

### Rejected Alternatives

- 全局调高 drag、改变 spring、缩短 prediction horizon：回归面覆盖整条笔迹和全部设备，且不能建立严格 raw endpoint 边界。
- 全局关闭 Kalman 或改用 StrokeEnd predictor：损失运动期低延迟，仍不能约束真实 `kUp` 前半段输出。
- 只在 renderer/shader 裁剪：坏中心线仍会进入 Stored Stroke，重放后复现。
- 只把最后一点改成 raw Up：不能删除“先出去、再回来”的中间 loop。
- 中途 Reset + synthetic Down：虽然能伪造零速度，但会切断 predictor/stylus/width 历史，不作为首选。
