# RTS 断触墨迹暂留与续接设计

## 1. 边界与状态

改动限定在 `draw3.ink_prediction` 的纯运动学判定、`DrawingController` 的 runtime 生命周期，以及现有控制台测试。RTS 回调仍只发布快照，D3D 和 modeler 仍只由绘制线程访问。

`RuntimeStroke` 增加暂留状态、保存的 Up 快照、截止 QPC、匹配专用 prediction 和 Up 进入暂留当帧的可视刷新标记。`ended` 继续表示可以进入既有完成批处理；暂留状态不算真实活动 contact，但仍参与 L1/L0 重建。

## 2. 纯匹配契约

`draw3.ink_prediction` 导出纯预测运动解析、数据输入和判定结果，使生产路径与测试共享同一算法。物理 Up 作为 `kMove` 后同步生成 prediction；按 Down 间隔在预测时域内插值位置，超出时域使用最后预测位置并显式记录额外时长。模型坐标中的预测位移平移到物理 Up 坐标，避免平滑模型位置相对 RTS 原始位置的滞后。

预测有效时判定使用：

```text
gap = newDownQpc - oldUpQpc
forecastDisplacement = selectedPredictionPosition - modeledPositionAtUp
forecastPosition = physicalUpPosition + forecastDisplacement
endpointError = distance(forecastPosition, newDown)
endpointTolerance = 4*dpiScale + 0.35*length(forecastDisplacement)
    + 0.75*forecastAverageSpeed*beyondPredictionHorizon
referenceSpeed = max(forecastAverageSpeed, recentFilteredInputSpeed)
adaptiveBridgeLimit = clamp(referenceSpeed*gap*1.75 + 4*dpiScale,
    64*dpiScale, 256*dpiScale)
```

预测位移方向和终点瞬时速度方向都保留为诊断量，但预测路径不再用终点切线对整段曲线弦做硬角度拒绝。落点误差通过且桥接未超过速度自适应上限即进入候选；比较额外允许至多 1 个当前渲染像素的数值容差。回退路径继续使用 12px/4px 真实尾方向、滤波末速、35° 与 32px 绝对上限；兼容身份由控制器在调用纯函数前检查。

若上述冻结端点失败，仅对预测弦方向可靠、`forecastChordAngle <= 35°`、相对 `referenceSpeed` 的速度比位于 `[0.35, 2.75]` 且目标时间确实超过预测时域的候选尝试加速自适应走廊：

```text
adaptedDistance = max(forecastDistance, referenceSpeed*gap)
longitudinalError = abs(project(bridge, forecastDirection) - adaptedDistance)
lateralError = abs(cross(bridge, forecastDirection))
axisTolerance = 4*dpiScale + 0.50*adaptedDistance
```

纵向和横向误差必须分别通过 `axisTolerance`，同时仍受 35°、速度比和动态桥接上限约束。该分支不再叠加 `0.75*speed*beyondHorizon`，防止移动预测中心后重复计算同一份时间不确定度。原冻结端点仍是第一判定，自适应走廊只修复预测时域覆盖不足且抬笔前正在加速的高置信误拒。

圆弧运动中，`physicalUp→predictionEndpoint` 的短弦会落后于预测末端速度方向。预测弦走廊失败后，若模型提供有效末端速度，则使用相同的 `adaptedDistance`、35°、速度比、纵向/横向容差和动态距离上限，再以末端速度方向评估第二条走廊。该路径不替换冻结端点、不提高全局阈值，也不要求短预测弦达到 4px 方向可靠阈值；末端速度向量本身无效时不可启用。候选排序使用实际命中走廊的方向夹角，日志同时保留预测弦角、末端速度角和所选角度。

预测仍覆盖新 Down 时，预测位置在急弯处可能严重低估位移。冻结端点失败后可使用同一纵横误差公式评估末端速度方向，但该时域内补救单独收紧为：完整间隔不超过 35ms、末端方向夹角不超过 15°、速度比位于 `[0.5, 2.0]`。命中时设置 `selectedTerminalDirectionCorridor=true` 和 `selectedInHorizonTerminalDirectionCorridor=true`，但保持 `predictionExtrapolated=false`；预测弦不进入该分支。

多个合法候选按 `(normalizedEndpointError, selectedCorridorAngle, distance, -upQpc)` 字典序选择；回退候选的首排序值仍为 `abs(log(actual/expected))`，避免遍历顺序改变结果。

## 3. 绘制循环数据流

每轮先读取已有 runtime 的快照，使旧 contact 的 Up 先进入暂留，再排空 Down 队列。这样 Up 与新 Down 即使都在两个帧之间到达，新 Down 也能看到候选。

进入暂留时把 Up 作为 `kMove` 输入同一模型，并在 Down 队列出队前同步生成匹配专用 prediction；渲染复用该结果，不重复调用 predictor。原 handle 保持占用。匹配成功时回收旧 handle、绑定新 handle，并用原 `qpcOrigin` 把新 Down 作为 `kMove` 输入。旧 prediction 在同帧被新的真实桥接与 prediction 替换，dirty rect 同时覆盖旧、新 L0。

超时时以保存的 Up 位置和单调时间发送 `kUp`。模型结果进入现有 `DrawCompletedStroke`、批量 L2 resolve、`RebuildActiveLayers` 与回收路径；`retainPredictionOnUp` 仍只在此最终完成阶段选择尾段。

启动时通过 HiEasyX 已有的 `PreSetWindowShowState(SW_HIDE)` 创建隐藏窗口，保持现有窗口→D3D→presenter→RTS 依赖顺序。`DrawingController::ClearCanvas()` 首次提交透明画布后再由 `WindowController::Show()` 显示，避免初始化期间暴露第三方窗口类的白色背景；不修改 HiEasyX 第三方源码。

## 4. 容量、等待与故障处理

候选上限为 8。新 Up 将超过上限时先终结最旧候选，同帧统一提交，确保最多额外固定 8 个 contact slot 和 runtime。

存在真实活动 contact 时保持目标帧率等待；只有候选时结束 timer period，以 `WaitForWake` 等待到最近 deadline。Down、控制请求或 deadline 任一发生都会重新进入循环。完全无 runtime 时继续使用原 blocking dequeue。

暂留转换或续接模型 Update 失败时记录错误并立即退回终结路径；不得留下无 handle 的活跃 runtime。Cancelled 永远走立即完成/丢弃路径。

## 5. 日志与回滚

启动日志只输出一次配置；成功日志每次续接一行。正常模式拒绝和逐帧路径不输出；人工测试开关开启时，未命中的新 Down 只输出最近候选的一行拒绝原因与预测量化值。

回滚只需关闭配置 bool 即恢复旧 Up 行为；关闭分支不得改变现有完成、批处理、等待和回收顺序。

## 6. 断触注入测试路径

独立编译期测试开关位于共享配置常量。开启时，RTS 同步插件为最多 32 个物理 contact 保存固定状态：正常转发阶段按 180–520ms 随机计划断点；触发 Move 被改写为合成 Up，随后丢弃 20–70ms Move；首个到期后的真实 Move 以高位合成 contact id 发布为新 Down，后续 Move/Up 路由到该 id。

物理 contact id 只负责查找测试状态，绘制线程仍看到两个真实的 coordinator handle，因此可覆盖候选暂留、身份匹配、旧 handle 回收和新 handle 接管。物理 Up 落在丢弃窗口内时不生成新 Down，旧候选自然超时。

关闭时 `if constexpr` 选择原 `PublishDown/PublishMove/PublishUp` 直达分支，模拟器类型退化为空对象；不查询 QPC frequency、不生成随机数、不加锁、不写模拟日志。
