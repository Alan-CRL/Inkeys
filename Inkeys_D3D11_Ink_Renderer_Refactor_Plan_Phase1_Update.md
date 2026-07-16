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

### 3.1 笔宽变化约束

速度压感使用相邻有效原始鼠标点的距离/时间，不读取弹簧建模或预测结果的速度；严格恒速输入因此得到恒定目标笔宽。首个有效速度会回填尚未提交的起笔点，停笔及预测段继承最后真实笔宽，避免启动坡度、模型过冲和预测收敛造成“先细后粗”。速度压感和原有指数平滑之后，每个新点的半径变化还需同时满足：

```txt
maxRadiusDeltaByTime = 6 * baseDiameter * deltaTime
maxRadiusDeltaByDistance = 0.8 * distance(previousPoint, currentPoint)
maxRadiusDelta = min(maxRadiusDeltaByTime, maxRadiusDeltaByDistance)
```

两个方向使用相同限制；零时间或零位移不改变半径。L0 完成实时笔锋收细后再检查一次同样的约束，确保 `|r1-r2| < segmentLength`，不露出胶囊端帽。当前测试配置中普通笔和橡皮的 `baseDiameter` 都为 50px，对应最大半径时间变化速率 300px/s。

像素着色器仍保留退化保护：当一个端点圆完全包含另一个时，直接使用实际较大端点的圆心和半径；近零长度段同样返回较大端点圆。

## 4. 工具切换、平头荧光笔与圆角橡皮

- `1/Num1` 选择普通笔，`2/Num2` 选择荧光笔，`3/Num3` 选择橡皮；默认是普通笔，仅鼠标左键会开始笔画。
- 每笔按下时固定当前工具，笔画中切换只影响下一笔。
- 荧光笔固定为 50px、`RGB(255, 0, 0)`、整笔 `alpha = 0.35`；保留普通笔的建模、平滑与 Kalman 预测，不使用模拟压感或实时笔锋。
- 橡皮使用 50px 基准直径和圆角胶囊（`shapeType = 0`），继续使用画笔的建模、平滑、模拟压感、实时笔锋和分段绘制，但不调用 `Predict()`。
- 橡皮以 `(0, 0, 0, 1)` 绘制覆盖率遮罩，继续使用 `fwidth + smoothstep` 产生抗锯齿边缘。

### 4.1 标准平头荧光笔

荧光笔的每个建模点保存单位走势。新走势按路径距离指数平均，响应距离固定为一个基准直径：

```txt
averageWeight = 1 - exp(-pointDistance / baseDiameter)
maxAngleChange = 0.8 * pointDistance / radius
```

短线段上的角度变化受第二式限制，防止末端 1–2px 抖动旋转平头或使四边形翻面。首个有效走势只回填尚未提交的起笔点；走势尚未建立时不提交 L1，单击或全程零位移最终按默认水平方向生成居中的 50×50 方形印记。预测使用方向估算器副本，不污染真实点状态。

每段由两个端点的平滑法线和 25px 半宽组成凸四边形，GPU AABB 从四个实际角点计算；CPU 脏矩形使用相同角点公式并增加抗锯齿余量。若极端折返导致四边形退化，CPU 与 GPU 都回退为沿当前线段方向的矩形。

荧光笔在 L1/L0 中以白色、不透明覆盖率和 `D3D11_BLEND_OP_MAX` 逐段累积。实时预览及抬笔提交都只执行一次：

```txt
coverage = max(L1.a, L0.a)
premultipliedSource = (0.35, 0, 0, 0.35) * coverage
dst = premultipliedSource + dst * (1 - premultipliedSource.a)
```

因此同一笔内的分段重叠、自交及 L1/L0 交界不会加深；不同荧光笔笔画仍按标准 source-over 累积，并可与普通笔历史内容共同保存在 L2 中。

### 4.2 圆角橡皮

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
- 若普通画笔以后也支持半透明，再复用荧光笔的 per-stroke coverage 语义，避免同一 stroke 内部 alpha 自叠加。
- 后续如果要把最后一帧 prediction 存入正式墨迹数据，需要在持久化结构中标记该段来源，方便以后调参或回放时识别。
