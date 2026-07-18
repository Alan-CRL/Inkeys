# Runtime and Rendering

## Ownership And Flow

`main.cpp` 的初始化顺序是：

1. `WindowController::Initialize`
2. `InitializeGraphicsDevice`
3. `TransparentPresentationController::Initialize`，内部初始化 `InkRenderer`
4. `DrawingController` 创建并进入消息循环

不要交换窗口、设备、交换链和 renderer 的依赖顺序。`TransparentPresentationController` 保存对 `GraphicsDeviceResources` 和 `InkRenderer` 的非拥有指针，调用者必须让这些对象覆盖 presenter 生命周期。

## Input And Thread Boundary

`WindowController::HandleWindowMessage` 将 resize、清屏、全量呈现、DWM 变化和退出写入原子状态。主循环通过 `Consume*` 方法消费请求。

- 窗口回调不重建 D3D 资源。
- `pendingResizeWidth_/Height_` 先写入，`resizeRequested_` 最后 release 发布。
- `DrawingController::ProcessPendingResize` 在绘制线程依次 resize renderer、resize presenter，两个步骤都成功后才 `CommitSize`。
- 单笔循环直接读取当前光标并清空积压鼠标消息，以降低延迟。

## Tool State

工具在 `WM_LBUTTONDOWN` 时复制到本笔局部变量，中途按键只影响下一笔。

当前行为：

| Tool | Width | Prediction | Width mode | Live layer |
|---|---:|---|---|---|
| Pen | 100px | active configured mode | simulated pressure | real tail + prediction + taper |
| Highlighter | 50px | enabled after real path reaches 12px | fixed | flat primitives, no taper |
| Eraser | 50px | disabled | fixed | real points directly committed to L1 |

这是当前实验实现。预测时长、目标帧率、笔宽、live-tip 和几何阈值默认都是实验参数；只有公开接口、持久化格式或明确兼容要求已经依赖某值时，该值才升级为兼容契约。

当前源码描述现有行为，阶段说明描述历史设计或计划。两者出现数值或工具语义差异时，应并列记录，不得自动用源码覆盖历史目标，也不得自动让实现恢复为阶段说明。

## Stroke Modeling Invariants

- 输入时间使用单调递增的 logical time，来源是 wall-clock delta。
- 小于 `0.25px` 的原始移动视为抖动。
- 普通笔宽使用相邻有效原始鼠标点的速度，不使用 modeled/predicted velocity。
- 预测点通过 `StrokeWidthEstimator` 副本继承最后真实宽度。
- 半径变化同时受时间和距离限制；L0 taper 后再次调用 `LimitRadiusTransitions`。
- 视觉连续三帧稳定后可冻结停笔更新，移动时解除冻结。

依据：`StrokeWidthEstimator::Append`、`UpdateRawPositionAndDetectMovement`、`UpdateIdleFreezeState`、`RebuildPredictedPoints`。

## Three-Layer Contract

- `L2`：已完成笔画的最终 premultiplied RGBA 画布，背景真透明。
- `L1`：当前笔已稳定的前缀操作。
- `L0`：当前帧仍会变化的真实尾部、预测与笔锋，每帧先恢复单位操作再重画。

临时操作层表示：

```text
Result = Add + Retain * Below
```

`Add` 使用 BGRA8，`Retain` 使用 R16F；新层的单位操作是 `Add=(0,0,0,0)`、`Retain=1`。

绘制过程中，保护窗口之外的真实前缀推进到 L1。普通笔保护时间为：

```text
liveTipDuration + predictionDuration
```

抬笔时最后可见 L0 原样追加到 L1，再把合并操作一次性应用到 L2，随后清空 L1/L0。不要在抬笔时重新用“仅真实点”重建最终几何，否则会造成回缩或跳变。

### Visual Prediction Is Not Persistent Ink

上面的抬笔规则只描述当前测试程序的瞬时视觉画布：

- prediction 是瞬时视觉结果。
- L2 是当前已落定的像素画布，不是正式墨迹持久化记录。
- 正式持久化原则上从已确认输入生成。
- 最终 prediction 必须被后续真实采样替换，或经过明确定义的提交动作转为已确认数据。
- 未确认预测点不得无条件写入永久笔迹、回放记录或跨版本格式。

> **待验证**：仓库尚无正式 `InkStrokeRecord`、预测来源标记或显式提交协议。未来持久化任务必须单独定义数据结构、替换/提交条件和兼容行为，不能直接复制当前 L2 的视觉提交语义。

## Scenario: Persisting Stroke Samples

### 1. Scope / Trigger

当任务新增 `InkStrokeRecord`、保存、导出、同步、回放或从 L2/`ActiveMouseStroke` 提取永久笔迹时，必须应用本契约。

### 2. Signatures

当前仓库没有正式持久化 API 或记录结构。未来任务必须先定义明确的写入签名和数据结构；禁止把 `ActiveMouseStroke::predictedPoints` 或 L2 像素读取隐式当成持久化接口。

### 3. Contracts

永久样本只能来自以下状态之一：

- 已确认真实输入；
- 由后续真实输入替换后的 prediction；
- 经过明确定义的提交动作转为已确认的数据。

仅为当前帧展示而生成的 prediction 保持 transient，不进入永久记录。

### 4. Validation & Error Matrix

| Input state | Persistence behavior |
|---|---|
| Confirmed real sample | 可以写入 |
| Prediction later replaced by real sample | 只写替换后的确认结果 |
| Prediction explicitly committed by future protocol | 按该协议写入并保留可解释的确认语义 |
| Unconfirmed prediction | 拒绝或省略，不得静默写入 |
| L2 pixel result | 只能作为视觉结果，不能自动还原为永久 stroke record |

### 5. Good / Base / Bad Cases

- Good：持久化层只接收已确认样本，并测试 replacement/explicit commit。
- Base：在尚无持久化结构时保持 prediction 仅用于 L0/L2 视觉流程。
- Bad：抬笔时直接把 `predictedPoints` 全量序列化，因为它们当前可见。

### 6. Tests Required

- 未确认 prediction 不出现在保存记录中。
- 真实采样到达后替换对应 prediction，回放使用确认结果。
- 显式提交协议存在时，提交条件和记录解释可重复。
- 保存后回放不依赖当前 predictor 参数才能解释旧数据。

### 7. Wrong vs Correct

Wrong：`L2 已包含最终 prediction，所以永久笔迹直接保存 predictedPoints。`

Correct：`L2 只表示当前视觉结果；永久笔迹只接受真实确认或显式提交的数据。`

## Highlighter Geometry

- 连续点距离不超过 `0.01px` 时去重。
- body 是固定半径 `25px` 的解析矩形。
- 内部端点沿切线重叠 `2px`，全局 butt cap 不延伸。
- 转角超过 `0.5°` 生成外侧 round sector，达到 `177°` 改为完整圆。
- CPU bounds 与 GPU AABB 使用同一几何，并额外保留 `3px`。
- 起点方向在真实路径达到 `12px` 后锁定；尾端保留至少 `12px` 上下文。
- 最终真实路径不足 `12px` 时生成从按下点出发的确定性 `12×50px` short mark；预测不参与短划分类。

上述值在当前实现中共同构成一套几何约束，但仍属于实验参数。修改一个值前必须搜索全部消费者并验证相关场景；这不表示当前数值已经成为永久产品标准。

## Dirty Rect Contract

- 每个 rect 在资源操作前裁剪到当前画布。
- L0 更新的 dirty rect 是上一帧 L0 与当前 L0 的并集，确保旧预测被清除。
- 第一帧、窗口重新暴露或呈现失败后的恢复使用整画布。
- stroke dirty 同时包含稳定提交和最终 live 几何。
- resize 只保留新旧画布左上角交集，不缩放历史墨迹。
