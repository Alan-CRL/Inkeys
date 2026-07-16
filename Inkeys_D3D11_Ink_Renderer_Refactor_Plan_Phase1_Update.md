# Inkeys D3D11 墨迹渲染重构方案 - Phase 1 修订

> 本文记录当前已经落地的第一阶段实现，并修正原方案中与最新决策不一致的部分。

## 1. 当前阶段范围

- 只处理鼠标单指路径，暂不接 RTS、多指、MPSC 队列和正式墨迹持久化结构。
- 渲染图层明确为：
  - `L2_FinalCanvas`：真透明背景的最终画布。
  - `L1_ActiveDry`：稳定前缀烘干层。
  - `L0_LiveComposite`：实时笔锋 + 预测层，每帧清空重绘。
- 调试阶段 L1 稳定段使用红色，L0 实时内容使用蓝色，便于观察实时层和烘干层边界。

## 2. 已修正的关键语义

原方案中写过 `prediction 永远不能进入 L1/L2`。当前第一阶段已改为：

```txt
绘制过程中：
    prediction 只在 L0 中显示，不进入 L1/L2。

抬笔瞬间：
    最后一帧 L0 的可见内容原样提交到 L1，再合成到 L2。
    这包含最后一帧 prediction 和笔锋收束效果。
```

这样做的目的，是保证抬笔瞬间用户看到的画面不回缩、不跳变。

## 3. L0 保护窗口规则

每帧 L0 包含：

```txt
未烘干真实点窗口 + 当前预测点
```

未烘干真实点窗口按时间计算：

```txt
protectedDuration = liveTipDuration + predictionDuration
```

其中：

- `liveTipDuration` 是配置的笔锋时长。
- `predictionDuration` 是当前预测末端相对真实末端的时间长度。
- 旧于该窗口的真实点稳定段进入 L1。
- L0 内只有从最终端点往回 `liveTipDuration` 的部分做笔锋收细。
- 更旧的预测等长保护段仍留在 L0，但保持正常半径，不做收细。

这个规则避免下一帧预测突然缩短或消失时，笔锋直接吃到上一帧已经烘干到 L1 的部分。

## 4. 工具切换与圆角橡皮

- `1/Num1` 选择普通笔，`3/Num3` 选择橡皮；默认是普通笔，仅鼠标左键会开始笔画。
- 每笔按下时固定当前工具，笔画中切换只影响下一笔。
- 橡皮使用 50px 基准直径和圆角胶囊（`shapeType = 0`），继续使用画笔的建模、平滑、模拟压感、实时笔锋和分段绘制，但不调用 `Predict()`。
- 橡皮以 `(0, 0, 0, 1)` 绘制覆盖率遮罩，继续使用 `fwidth + smoothstep` 产生抗锯齿边缘。

橡皮的分层合成语义如下：

```txt
L1/L0 内部：
    每个胶囊段用 D3D11_BLEND_OP_MAX 累积覆盖率。

实时预览：
    先把 L2 复制到 backbuffer。
    maskAlpha = max(L1.a, L0.a)。
    backbuffer.rgba *= (1 - maskAlpha)。

抬笔：
    把最终 L0 段并入 L1，再用合并遮罩对 L2 执行一次 destination-out。
```

该路径仅使用 D3D11 核心混合状态，不依赖 D3D11.1。一整笔在裁除目标前先取最大覆盖率，避免胶囊段重叠或 L1/L0 交界重复衰减抗锯齿边缘。

L0/L1/L2/backbuffer 的内部背景统一为 `(0, 0, 0, 0)`。DirectComposition/DWM 路径保留真零 alpha；仅 UpdateLayeredWindow 回退路径在 CPU 拷贝阶段，把内部 premultiplied 画布 source-over 到黑色 `1/255` alpha 命中测试底层。该底层不回写画布，也不跨帧累积。

本阶段涉及的文本源文件统一为 UTF-8 BOM + CRLF。Windows SDK FXC 10.1 不接受 HLSL 开头的 UTF-8 BOM，因此项目在 `$(IntDir)\fxc_utf8` 内生成仅供编译的无 BOM 临时副本；仓库中的 HLSL 源文件仍保持 UTF-8 BOM。

## 5. 当前实现控制项

当前在 `draw3/ink_prediction.cppm` 中保留了三个编译期 enum 控制：

```cpp
enum class InkPredictionMode { Disabled, StrokeEnd, Kalman };
enum class LiveTipLengthMode { Short, Normal, Long };
enum class DebugLayerColorMode { NormalInkColor, ColorizeLiveLayer };
```

默认值：

```cpp
kActivePredictionMode = InkPredictionMode::Kalman;
kActiveLiveTipLengthMode = LiveTipLengthMode::Normal;
kActiveDebugLayerColorMode = DebugLayerColorMode::NormalInkColor;
```

Kalman 参数继续跟随当前帧率预设，包括 `prediction_interval_seconds`、`kalman_desired_number_of_samples`、`kalman_max_time_samples` 和 `min_output_rate`。

## 6. 后续仍待做

- 接入 RTS 多点触摸和多生产者单消费者输入队列。
- 将单指 `ActiveMouseStroke` 扩展为 `pointerId -> ActiveStroke`。
- 设计正式 `InkStrokeRecord`，保存坐标、半径、颜色、工具类型和合成模型。
- 半透明画笔再引入 per-stroke coverage，避免同一 stroke 内部 alpha 自叠加。
- 后续如果要把最后一帧 prediction 存入正式墨迹数据，需要在持久化结构中标记该段来源，方便以后调参或回放时识别。
