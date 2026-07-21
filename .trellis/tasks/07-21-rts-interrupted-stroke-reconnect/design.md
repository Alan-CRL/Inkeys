# RTS 断触墨迹暂留与续接设计

## 1. 边界与状态

改动限定在 `draw3.ink_prediction` 的纯运动学判定、`DrawingController` 的 runtime 生命周期，以及现有控制台测试。RTS 回调仍只发布快照，D3D 和 modeler 仍只由绘制线程访问。

`RuntimeStroke` 增加暂留状态、保存的 Up 快照、截止 QPC、匹配专用 prediction 和 Up 进入暂留当帧的可视刷新标记。`ended` 继续表示可以进入既有完成批处理；暂留状态不算真实活动 contact，但仍参与 L1/L0 重建。

## 2. 纯匹配契约

`draw3.ink_prediction` 导出纯预测运动解析、数据输入和判定结果，使生产路径与测试共享同一算法。物理 Up 作为 `kMove` 后同步生成 prediction；按 Down 间隔在预测时域内插值速度，超出时域使用最后预测速度。预测速度无效时尝试预测轨迹弦方向，仍无效则回退真实尾方向与滤波原始末速。

预测有效时判定使用：

```text
gap = newDownQpc - oldUpQpc
bridgeSpeed = distance(oldEnd, newDown) / gap
expectedDistance = predictedSpeed * gap
minimumDistance = max(0, expectedDistance*0.35 - 4*dpiScale)
maximumDistance = min(64*dpiScale,
    expectedDistance*2.75 + 4*dpiScale)
```

回退路径继续使用 12px/4px 真实尾方向、滤波末速和 32px 绝对上限。方向、时间和距离包络全部通过才匹配；兼容身份由控制器在调用纯函数前检查。

多个合法候选按 `(abs(log(actual/expected)), angle, distance, -upQpc)` 字典序选择，避免遍历顺序改变结果。

## 3. 绘制循环数据流

每轮先读取已有 runtime 的快照，使旧 contact 的 Up 先进入暂留，再排空 Down 队列。这样 Up 与新 Down 即使都在两个帧之间到达，新 Down 也能看到候选。

进入暂留时把 Up 作为 `kMove` 输入同一模型，并在 Down 队列出队前同步生成匹配专用 prediction；渲染复用该结果，不重复调用 predictor。原 handle 保持占用。匹配成功时回收旧 handle、绑定新 handle，并用原 `qpcOrigin` 把新 Down 作为 `kMove` 输入。旧 prediction 在同帧被新的真实桥接与 prediction 替换，dirty rect 同时覆盖旧、新 L0。

超时时以保存的 Up 位置和单调时间发送 `kUp`。模型结果进入现有 `DrawCompletedStroke`、批量 L2 resolve、`RebuildActiveLayers` 与回收路径；`retainPredictionOnUp` 仍只在此最终完成阶段选择尾段。

## 4. 容量、等待与故障处理

候选上限为 8。新 Up 将超过上限时先终结最旧候选，同帧统一提交，确保最多额外固定 8 个 contact slot 和 runtime。

存在真实活动 contact 时保持目标帧率等待；只有候选时结束 timer period，以 `WaitForWake` 等待到最近 deadline。Down、控制请求或 deadline 任一发生都会重新进入循环。完全无 runtime 时继续使用原 blocking dequeue。

暂留转换或续接模型 Update 失败时记录错误并立即退回终结路径；不得留下无 handle 的活跃 runtime。Cancelled 永远走立即完成/丢弃路径。

## 5. 日志与回滚

启动日志只输出一次配置；成功日志每次续接一行。正常模式拒绝和逐帧路径不输出；人工测试开关开启时，未命中的新 Down 只输出最近候选的一行拒绝原因与预测量化值。

回滚只需关闭配置 bool 即恢复旧 Up 行为；关闭分支不得改变现有完成、批处理、等待和回收顺序。
