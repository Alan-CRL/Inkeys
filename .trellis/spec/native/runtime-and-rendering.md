# Runtime and Rendering

## Ownership And Flow

`main.cpp` 的初始化顺序是：

1. 创建 `ContactInputCoordinator`
2. `WindowController::Initialize` 创建隐藏窗口，随后把 coordinator 绑定到窗口控制唤醒
3. `InitializeGraphicsDevice`
4. `TransparentPresentationController::Initialize`，内部初始化 `InkRenderer`
5. `RealTimeStylusInput::Initialize` 启用同步 RTS 插件
6. `DrawingController` 创建，先提交透明画布并显示窗口，再进入消息循环

不要交换窗口、设备、交换链和 renderer 的依赖顺序。`TransparentPresentationController` 保存对 `GraphicsDeviceResources` 和 `InkRenderer` 的非拥有指针，调用者必须让这些对象覆盖 presenter 生命周期。

退出时先停止 RTS 并解除 `WindowController` 的 coordinator 指针，再销毁 coordinator。RTS 的 COM 对象只由完成 MTA 初始化的主线程创建、禁用、移除插件并释放。

## Input And Thread Boundary

`WindowController::HandleWindowMessage` 将 resize、FIFO Canvas command、全量呈现、DWM 变化和退出发布给绘制线程。主循环通过 `Consume*` 或 `TryDequeueCanvasCommand` 消费请求。

- 窗口回调不重建 D3D 资源。
- `pendingResizeWidth_/Height_` 先写入，`resizeRequested_` 最后 release 发布。
- `DrawingController::ProcessPendingResize` 在绘制线程依次 resize renderer、resize presenter，两个步骤都成功后才 `CommitSize`。
- RTS 同步回调只完成 packet 解析、contact 状态发布和唤醒，不调用 D3D、presenter 或 stroke modeler。
- 活动 Touch 跟手平移中的 Pen suppression 是跨层生命周期契约：RTS/绘制层不得建笔，窗口层不得发布接触光标或绑定触觉；状态必须锁存到同一 Pen 的 Pointer/RTS 终态，不能只在合成前隐藏像素。
- 控制请求先写 sticky 原子标记，再通过 coordinator 队列唤醒；消费方在阻塞前二次 dequeue，避免清 pending 与入队交错造成丢唤醒。
- 无活动 contact 时使用 blocking dequeue；活动 contact 仍按帧更新停笔预测。
- Down、Up/Cancelled 和控制请求递增 wake generation 并触发 Win7 可用的 event；Move 只更新合并快照，不把 240Hz packet 变成无界帧驱动。RTS/`WM_POINTER` 的 Pen Contact cursor 同样只覆盖最新 mailbox，不逐包发布 render/control wake；Hover、Canvas command 与终态继续按原路径唤醒。
- contact slot 的 `writerLatch_` 只在对象初始化时为 clear，跨 generation 复用不得重置；只有实际取得 latch 的调用可以释放，避免旧 generation Move 与新 writer 形成 ownership ABA。
- 绘制线程使用 `THREAD_PRIORITY_ABOVE_NORMAL`，活动末段按 QPC deadline 核对 wake generation；完全空闲仍阻塞在队列 semaphore，不自旋。

## Tool State

单笔兼容路径在 `WM_LBUTTONDOWN` 时复制工具。多 contact 路径只在空闲批次的首个 Down 读取 1/2/3/4 选择，并把同一工具锁定到该批所有 contact；活动期间的选择只影响下一批。

当前行为：

| Tool | Width | Prediction | Width mode | Live layer |
|---|---:|---|---|---|
| Pen | 5px base; hardware 1–7px | active configured mode | per-device fixed/simulated/hardware | real tail + prediction + taper |
| Highlighter | 6.25×50px fixed vertical nib | enabled from Down | fixed | rectangle sweep primitives, no taper |
| Eraser | 50px | disabled | fixed | real points directly committed to L1 |
| Laser | 5px solid (1.67px core) + 5px diffuse/side | active configured mode | Pen laser pressure; Mouse/Touch fixed | stable premultiplied color + per-stroke coverage scratch, never L2 |

这是当前实验实现。预测时长、目标帧率、笔宽、live-tip 和几何阈值默认都是实验参数；只有公开接口、持久化格式或明确兼容要求已经依赖某值时，该值才升级为兼容契约。

## Scenario: Analytic Line And Rounded Rectangle Tools

### 1. Scope / Trigger

修改 `Q/W/E/R` 工具选择、Shape 活动态、解析几何、Stored Shape、dirty bounds、history footprint 或首次 L2 提交时，必须同步应用本节与 [CPU/GPU Contracts](../shaders/cpu-gpu-contracts.md)。

### 2. Signatures

- `DrawingTool { Pen=0, Highlighter=1, Eraser=2, Laser=3, SolidLine=4, DashedLine=5, OutlineRectangle=6, FilledRectangle=7 }`
- `StoredInkType { Pen=0, Highlighter=1, Eraser=2, SolidLine=3, DashedLine=4, OutlineRectangle=5, FilledRectangle=6 }`
- `ShapePrimitiveKind { SolidLine=16, DashedLine=17, OutlineRectangle=18, FilledRectangle=19 }`
- `ResolveShapeLiveEndpoint(predictedResults, modeledEndpoint, hasModeledEndpoint, rawEndpoint)`
- `FinalizeStoredShape(primitive, style) -> optional<InkStroke>`
- `DrawShapePrimitives(primitives, kind, color, operatorKind)`
- `RectFromShapePrimitive(primitive, kind, width, height)`
- `BuildStrokeTileFootprint(stroke, visibleBounds)`

上述枚举都是 append-only 数值协议；不得重排既有值或把 shader `0..15` 复用于 Shape。

### 3. Contracts

- 空闲批次首个 Down 锁定所选工具；`Q/W/E/R` 只影响下一批。四种 Shape 使用 Pen 的 `5px` 基础直径、颜色、圆形 cursor 和触觉，不读取压力、倾角、笔锋或 taper；倒转 Pen 仍可按既有规则覆盖为 Eraser。
- 原始 Down 坐标永久作为起点。活动终点依次选择最后一个有限 prediction、最后建模点、最后原始输入；非有限候选必须回退。原始 Up 坐标强制覆盖最终终点，不能由 modeler/prediction 再修改。
- 每个活动 Shape 只保存固定 primitive、kind、原始/建模末点和少量标志。`modeledResults`、`predictedResults` 仅作预留 scratch，提取末点后清空；不得生成真实路径、可变宽度点列、笔锋或 L1 稳定前缀。
- 活动 Shape 完整绘制在共享 L0。仅当所有活动 L0 内容都是 Shape 且全部末点稳定时才可保留既有 L0；任一 Shape 变化或 Pen/Highlighter live 内容需要刷新时，先把 L0 清为单位操作，再按 kind 批量重放全部活动 Shape。同类 contact 在容量允许时只提交一次 `Draw(6 * count)`。
- Resize、contact 完成和 Cancel 后通过活动层全量重建恢复仍活动 Shape。Cancel 只脏化旧/新 L0 bounds 并丢弃对象；不得追加文档、RenderItem 或 L2 像素。
- 实线是圆头胶囊；虚线由 pixel shader 解析为中心线 `4 * width` 实线段、`6 * width` 空隙和圆头，使圆头侵占后的可见线段与可见空隙接近 `1:1`，不在 CPU 展开短划。矩形先规范化任意方向对角点，使用 `4 DIP * dpiScale` 圆角并钳制到短边一半；Outline 边界居中，Filled 不附加边框。
- 完成 Shape 恰好保存两个同宽 `StoredInkPoint` 和 Pen 颜色/透明度。零长度 Line 合法并重放为圆点；宽或高为零的 Rectangle 非法。首次落定顺序保持 `Finalize -> AppendStroke -> AppendRenderItem -> raster appended Stroke -> capture preimage -> resolve L2`，页面恢复、冷重建和 history tile 也必须调用同一个 `DrawStoredStroke` 入口。
- Line footprint 使用扩宽线段；OutlineRectangle 只遍历四边；FilledRectangle 覆盖完整规范化面积。pixel bounds 与 dirty bounds 都包含半线宽（Filled 除外）、AA padding、裁剪和反向拖动。

### 4. Validation & Error Matrix

| Condition | Required behavior |
|---|---|
| Predict 失败或最后 prediction 为 NaN/Inf | 清空 prediction，回退最后有限建模点；仍不可用时使用原始输入 |
| Model Update 失败 | 保留原始末点；Up 仍能用原始坐标完成有效 Shape |
| 原始 Up | 无条件成为 Stored 第二端点；旧 prediction 不得写入文档 |
| 零长度 Solid/Dashed Line | Stored 有效，shader 输出一枚线宽圆点 |
| Rectangle 宽或高为零 | `FinalizeStoredShape`/`InkStroke::IsValid` 拒绝，不产生 history 项 |
| Cancel | 清除旧 L0 可见区，不追加 Stroke/RenderItem，不 resolve 到 L2 |
| Shape-only 稳定帧 | 不清理、不上传、不重复 draw L0 |
| 稳定 Shape 与移动 Pen/Highlighter 并存 | 共享 L0 重建并重放普通 live 内容及全部活动 Shape |
| renderer Map/Draw 失败 | Stored 文档对象保持真值，栅格失败沿用 history 诊断/恢复路径 |
| 极端但有限坐标 | 文档保留原值；无法精确量化时 history 使用保守可见 footprint |

### 5. Good / Base / Bad Cases

- Good：多支同类 Shape 共享一次上传/Draw，另一支 Pen 移动时仍完整重放 Shape；Up 后撤回、翻页和 resize 都从两个 Stored 点恢复相同解析几何。
- Base：反向拖动的小矩形把 `4 DIP` 圆角钳制到短边一半；零长度虚线显示圆点。
- Bad：把拖动路径保存成点列、把 Shape 稳定前缀提交到 L1、CPU 生成每一段 dash，或为了保留稳定 Shape 而跳过共享 L0 中普通笔的刷新。

### 6. Tests Required

- 静态断言三组 enum 数值、`sizeof(ShapePrimitive)==32`，并核对 HLSL `16..19` 分支和 `Draw(6 * count)`。
- 单元测试 prediction -> modeled -> raw 优先级、NaN/Inf 回退、原始终点强制覆盖、反向矩形、`4 DIP` 圆角钳制、`4:6` 中心线虚线比例和 Shape dirty bounds。
- 文档测试覆盖四种 Stored 类型、严格双端点/同宽/正宽、三点拒绝、零长度 Line 合法和退化 Rectangle 拒绝。
- history 测试覆盖扩宽 Line、四边 Outline、完整 Filled 内部 tile、反向拖动、AA padding 和可见区裁剪。
- `--drawing-perf` 在 scratch/batch 预留后反复清空 modeled/predicted 长度并重建同类批次，断言零分配、容量稳定和有限终点；GPU 批量调用仍以 renderer 静态契约和人工 D3D 验证为准。
- ARM64 Debug/Release 完整 solution 构建与全部测试；人工覆盖 `Q/W/E/R`、prediction、Cancel、撤回、翻页和 resize。未执行 Pen/Touch 或 D3D Debug Layer 时必须明确标记。

### 7. Wrong vs Correct

Wrong：`Move -> append predicted path points -> commit old prefix to L1 -> Up 时继续使用预测末点。`

Correct：`Down 固定起点 -> Update/Predict 只消费末点 scratch -> 全 Shape 留在 L0 -> raw Up 覆盖终点 -> 保存两个点并从 Stored 对象重放。`

## Scenario: Laser Pointer Glow Trail

### 1. Scope / Trigger

修改 `DrawingTool::Laser`、Laser coverage、激光粒子/笔尖、留存计时或其多 contact 主循环时，必须应用本契约。

### 2. Signatures

- `DrawingTool::Laser`，数字键/小键盘 `4`
- `StrokeModelConfiguration::laserParticlesEnabled`、`laserParticleConfig`、`laserHoldDurationSeconds`
- `StrokeModelConfiguration::laserMultiTouchDrawingEnabled`
- `DrawingController::SetLaserParticlesEnabled/GetLaserParticlesEnabled`
- `DrawingController::SetLaserMultiTouchDrawingEnabled/GetLaserMultiTouchDrawingEnabled`
- `DrawingController::SetLaserHoldDurationSeconds/GetLaserHoldDurationSeconds`
- `LaserParticleDirtyTracker::Snapshot`
- `InkRenderer::StepLaserParticles/SimulateLaserParticles/EmitLaserParticles/ResetLaserParticles/DrawLaserParticles`
- `InkRenderer::DrawLaserCoverage/ClearLaserCoverageRect/ResolveLaserStrokeCoverage/ResolveLaserCompositedColor/DrawLaserDots`
- `InkRenderer::ClearLaserLiveCoverageRect/ResolveLaserIncrementalCoverage/EnsureLaserIncrementalCoverageResources`

### 3. Contracts

- Laser 在 Down 时锁定到当前批次，支持 Pen、Mouse 和 Touch；`laserMultiTouchDrawingEnabled` 默认关闭，此时只接受第一根活动 Touch，后续 Touch Down 直接回收且不打断第一根，运行中切换只影响之后开始的 contact。开启后恢复多 Touch；Laser 继续跳过断触 reconnect、倒转笔尾橡皮覆盖和触觉反馈。
- 已完成 Laser 使用独立 `R8G8B8A8_UNORM` 预乘颜色层，任何 Laser 几何都不得进入 L2。单 contact 快路中，`t7` `laserStrokeCoverage` 保存按时间保护边界推进的稳定真实前缀，`t9` `laserLiveCoverage` 保存当前真实尾部和 prediction；两者在 shape `13` 逐通道取 `max` 后只调用一次 `ResolveLaserMaterial`。L1/L0 交界保留一个重叠点，稳定前缀只接受已确认 `realPoints`，所以 prediction 回缩不会污染稳定 coverage。
- `t9` 只由绘制线程在首次观察 `ActiveTool() == Laser` 时按需创建并预热，创建后直到 renderer 释放都保留；资源创建/Resize 失败只记录一次并把当前会话降级为完整重绘。没有选择 Laser 的会话不分配该缓冲。
- 单个有效 Laser layer 且 coverage 可用时使用增量快路；出现第二个 layer、Cancel/状态异常或资源不可用后，当前批次锁定完整重绘直到最后一次 Bake。完整路径继续逐笔清理 `t7`、按 Down 顺序 source-over，不能把不同 contact 放入同一次 MAX。
- 多 contact 完整路径只把稳定 delta 与旧/新 live bounds 加入最终 `frameDirty`；每层只清理 `layer.bounds ∩ frameDirty`，scissor 资源可用时在同一 scissor 内仍上传和绘制完整 layer 几何，再按原 Down 顺序 resolve。scissor 创建失败只失去像素裁剪优化，由矩形 quad 保持相同输出范围；draw 结束必须恢复普通 rasterizer state，不能影响粒子、其他工具或 presenter。
- 96 DPI 默认实体总直径为 5px，白芯直径约 1.67px，红色实体轮廓外每侧固定扩散 5px，因此完整视觉直径为 15px；漫反射 coverage 在实体边界为 1，使用平方曲线向外单调衰减并在 5px 外缘为 0，红粉外缘高光只混合 RGB 而不额外增加 alpha。`LaserDot`、Touch 笔尖和固定宽度轨迹复用同一尺寸契约。Pen 使用 `0.65 + 0.75 * clamp(p, 0, 1)` 缩放实体、白芯和内侧散射，外部 5px 漫反射只随 DPI 缩放；无效压力保持上一宽度，prediction 继承最后真实实体半径，Mouse/Touch 固定基准宽度。
- 多支 Laser 按 Down 顺序分层，后 Down 的整支轨迹位于上层；较早结束的 contact 保留最终 CPU 几何直到同批最后一支抬起，同一笔自交仍以 coverage 并集避免重复加深。
- 粒子默认关闭，外部可通过设置接口按需开启；启用后只使用 FL11_0+ 的 D3D11 Compute Shader 固定池：`2048` 个 `80 bytes` 的 `LaserGpuParticle` 槽（总计 `163840 bytes`），以及同一缓冲的 `u0` UAV/VS `t8` SRV。创建 buffer 时不分配 CPU 零数组，必须用 GPU `resetAll` 初始化；失败只降级为无粒子。没有路径点/路径头资源、Append/Consume、间接绘制或 GPU 回读；整笔逐帧重绘和 Resize 都不得重建 GPU 粒子。
- 出生位置直接取当前 `l0DrawPoints.back()`；切线从 L0 尾部反向查找最后一个非退化段，重复点沿用上一有效切线。真实首次移动前没有方向时不发射，也不累计会在之后补出的 Down 余量。prediction 只影响新粒子的出生位置/切线，不影响密度。
- 发射 `dt <= 1/30s`，速率为 `min(72, 6 + filteredRealInputSpeedDipPerSecond / 2.5)` 粒/秒：静止每 contact 每秒 6 粒，移动约每 `2.5 DIP` 一粒，全局每帧最多 `96` 粒；预算超额和卡顿的整数部分不积压。运行中关闭会在下一帧 reset；重新开启后仍须已有或重新取得有效 L0 切线。
- 每粒以 50/50 概率选择正/负法线，并在对应法线附近均匀偏转 `±25°`。尺寸层级为 `lerp(0.28, 1.15, pow(random, 2.8)) DIP`；出生速度以 70% 独立随机与 30% 反向尺寸层级混合后映射到 `10–17 DIP/s`，寿命独立均匀随机为 `0.7–1.0s`，标称行程为 `3.5–8.5 DIP`。粒子不继承画笔前向速度。
- 每粒只保存屏幕位置、固定速度、年龄/寿命、实际累计/最大行程、出生/当前半径、Alpha、亮度和呼吸状态。实际推进速度与 Alpha 均乘 `1-smoothstep(0,1,age/lifetime)`，运动 `dt <= 1/30s`，年龄使用实际 wall time。前 10% 最大行程保持出生半径，之后按实际累计行程 smoothstep 缩至 20%。
- 粒子出生后不再读取 L0、真实点、prediction 或 contact 状态。Up/Cancel 只停止新发射；prediction 回缩、急弯和最后 Up 均不得修正存量位置/速度。禁止路径追赶、generation/弧长/segment cursor、端点受阻淡出和 prediction correction。
- 出生点可在白芯内沿所选法线随机偏移。基础亮度以 72% 尺寸层级和 28% 独立随机样本混合后映射到 `0.42–1.0`，因此大粒子通常更亮、小粒子通常更暗但范围仍重叠。`0.8–1.4Hz` 随机相位呼吸以 `0.12` 振幅在 `0.2s` 渐入且只改变 RGB；核心 Alpha 只使用生命周期曲线。
- Compute 顺序固定为 `VS t8 unbind -> 一次绑定 CS u0 -> 可选 update -> 按请求原顺序 emit -> 统一 CS unbind`；多 contact request 共享同一个 UAV 绑定周期，但 seed、spawn cursor 和 dispatch 顺序不变。绘制为 shape `10` 的 `DrawInstanced(6, 2048, 0, 0)`，死亡槽生成退化图元。每个 dirty frame 的顺序固定为 `L2 + 普通 L1/L0` 合成、粒子、已烘干 Laser 颜色层、shape `13` 的稳定/live coverage、Laser tip、普通 cursor、Present；粒子因此仍在激光主体下方。VS 令辉光半径为当前核心半径的 `2.0` 倍加 `2 × dpiScale` 地板，并传递出生基础亮度；PS 粒子核心直接复用激光红色外套 `borderColor=(1.0, 11/255, 30/255)`，只乘生命周期/呼吸亮度，不再向白混合。辉光保持 `(1.0, 0.32, 0.40)`、峰值 Alpha `0.18` 和 `pow(1.6)` 衰减，所有输出继续使用预乘 Alpha。
- CPU 把同一帧实际发射请求合成一个未裁剪保守包络，按最大减速弹道、白芯出生偏移、最大粒子半径、辉光和 AA 扩展；每个帧批次在最大寿命 1 秒后独立到期。Tracker 保存原始包络，每帧只执行一次 prune/snapshot，idle、timer、simulation、draw 和 dirty 决策共同复用；包络按当前画布逐帧裁剪，Resize 后不得复用旧画布裁剪结果。snapshot 无存活批次、无刚到期批次且无新请求时不得 Dispatch 或 DrawInstanced；批次刚到期仍提交最后一次 update 清理 GPU alive 槽，并用上一帧 bounds 清除一次旧像素，但不再 DrawInstanced。
- 最后一根 Laser Up 才记录 `lastAllUpQpc`；默认满亮保持 `1.0s`，固定 `0.8s` smooth fade。当前批次实际安排过粒子时，有效 Hold 为 `max(公开设置值, maximumLifetimeSeconds)`，默认下限即 `1.0s`，但 setter/getter 值不变。新 Down 在 Hold/Fade 中把整组 opacity 恢复为 `1` 并重新计时。
- `SetLaserHoldDurationSeconds` 只接受 finite non-negative 值；运行中调整须由 control wake 唤醒并相对最后一次全部 Up 立即重算。粒子 setter 同样发布 control wake；关闭后下一帧 reset GPU 状态并 union 旧保守 bounds。
- Hold 静态期不持续 Present；Fade、接触、prediction 和粒子动画才驱动帧。resize 只保留稳定颜色层左上角交集，`t7/t9` 新建为空；活动 Laser coverage 从 CPU 几何重建，不能复制旧 live coverage。clear 必须同步清理稳定颜色、scratch 和新旧 bounds；Present failure 保留仍有效的 coverage、请求下一帧 full-present，并重新解析旧/新 dirty bounds。粒子或 cursor dirty 与稳定 Laser 相交时，coverage 必须在最终 `frameDirty` 内再次解析。

### 4. Validation & Error Matrix

| Condition | Required behavior |
|---|---|
| Down/Up 多 contact | 只有最后一根 Up 启动 Hold；中间 Up 不改变 opacity |
| 多指开关关闭 | 第一根 Touch 正常绘制；其仍活动时忽略后续 Touch Down，Pen/Mouse 和既有 contact 不受影响 |
| Hold/Fade 新 Down | 旧稳定颜色层全组恢复满亮并重新计时 |
| 非法 hold seconds | setter 返回 `false`，旧值保持不变且不发布设置 |
| Laser Up | 清除 prediction；同批最后 Up 才有序烘入稳定颜色层，不 reconnect、不 resolve 到 L2 |
| 粒子关闭 | 下一帧 reset GPU 粒子池、清空保守脏区并 union 旧 bounds |
| FL < 11_0 或 CS/SRV/UAV/buffer 创建失败 | 只记录一次诊断并将粒子系统标记不可用；激光主体、瞬态层和 Present 继续 |
| GPU 初始 reset 失败 | 释放粒子资源并降级为无粒子，不得读取未初始化 structured buffer |
| 粒子 update 常量 Map 失败 | 解绑 Compute 状态并释放粒子资源，避免旧 alive 槽在后续发射时重现 |
| 无存活粒子且无新发射 | 无到期事件时不执行 update/emit/draw；若上一批刚到期，执行最后一次 update 并只重建旧 bounds 一帧，不 draw |
| scissor rasterizer state 创建失败 | renderer 继续初始化，Laser 矩形 quad 使用普通 rasterizer state 保持相同输出，仅失去局部像素裁剪优化 |
| `laserLiveCoverage` 创建/Resize 失败 | 只记录一次 `[LaserPerf]`，当前会话禁用增量快路并锁定完整重绘；Pen/Highlighter/Eraser 和粒子池继续 |
| 增量状态异常或 Laser Cancel | 清理 `t7/t9`，脏化旧 live/stable bounds，并将当前批次锁定完整重绘；不把部分 coverage 烘入颜色层 |
| 无首次真实移动/有效 L0 切线 | 不发射、不累计小数余量；不能由 prediction 单独触发 Down 爆发 |
| 卡顿 | 寿命按实际 wall time；屏幕运动最多推进 1/30s，未推进的距离不追赶 |
| Up/Cancel/prediction 回缩 | 停止新请求或改变后续出生源；存量粒子继续原屏幕轨迹 |
| Hold 静止且无存活粒子 | 不产生额外 frame/Present；deadline 或 control/input wake 才恢复 |
| Hold 中仍有存活粒子 | 粒子保守 dirty 驱动帧；最后一粒到期后停止持续 Present |
| resize/clear/Present 失败 | 稳定颜色交集保留或按请求清空，未烘干层重建；粒子未裁剪包络按新画布重裁剪，旧/新 glow bounds 都进入 dirty |

### 5. Good / Base / Bad Cases

- Good：白芯与厚红边在深色、混合背景可见，深红粉粒子辉光在白底可辨；新粒子从笔尖两侧法线喷射，急弯和 prediction 回缩不改变旧粒子。
- Base：默认关闭多指绘图时仅第一根 Touch 生成轨迹和 tip；开启后各活动 Touch 显示独立 tip。GPU 不可用时只有粒子降级，抬笔后已有粒子按自身最长 1 秒寿命继续减速、缩小和淡出。
- Bad：关闭多指后中断已经开始的 Touch、让被忽略的第二根 Touch 在第一根抬起时中途接管，或让粒子绑定/重采样整条路径、用 prediction 位移提高密度、预测回缩后瞬移已有粒子、同时绑定粒子 UAV 与 VS SRV、用 GPU readback 决定 dirty。

### 6. Tests Required

- 断言按键 4/枚举、最后 Up 计时、1.0s Hold、0.8s fade、运行中设置变化和非法输入。
- 断言多指配置默认关闭、setter/getter 往返，以及关闭时第二根 Touch Down 被忽略、开启时进入既有多 contact Laser 路径。
- 断言 Laser 不进入 reconnect/L2，压力 `0/0.5/1` 只缩放实体且 prediction 半径继承正确，coverage bounds 覆盖 15px 基准完整视觉直径、最大压力实体和固定 5px 漫反射；静态核对漫反射 alpha 从实体边界的 1 单调衰减到外缘的 0，resize/clear/Present failure 无残影。
- 增量状态测试必须断言时间保护边界单调推进、L1/L0 共享一个连接点、prediction 回缩不后退稳定游标、自交 dirty union 不丢失、第二 contact 锁定 fallback、稳定 delta 与旧/新 live dirty、Resize/Clear/resource failure 重建，以及 `max(t7,t9)` 与完整 coverage union 的 CPU 等价性；静态核对 t9、shape `13`、矩形 shape 以 `globalColor` 生成 quad、t6-t9 解绑与 scissor 恢复契约。
- 断言默认关闭；开启后覆盖 Down 零爆发、首次真实移动后静止 6/s、移动每 2.5 DIP 密度、72/s 与全局 96 上限且不积压、双侧法线与 `±25°` 偏转、连续偏小尺寸分布、尺寸/亮度正相关、尺寸/射程弱反向相关、`10–17 DIP/s` 速度、`0.7–1.0s` 寿命和不超过 8.5 DIP 的标称行程、速度/Alpha 曲线单调性、按行程缩至 20%、比例辉光和核心色相层级、亮度呼吸不改 Alpha、出生锚点取 L0 前端、Up/prediction 跳变不改变存量运动、1 秒有效 Hold 下限、帧批次 dirty 到期、单次 snapshot、空闲零命令、批量 request 顺序、80-byte CPU/HLSL layout、GPU reset 初始化和 resize 重裁剪。
- Debug/Release ARM64 全解决方案构建、VS/PS/两个 CS 均以 SM5.0 编译并嵌入；人工覆盖慢/快画、急弯、长按、路径追加、多接触、Resize、ULW、开关和 Hold/Fade。

### 7. Wrong vs Correct

Wrong：`每帧 CPU 更新 vector<Particle> -> 上传 InkData -> Draw`，或 CS dispatch 时仍让同一粒子缓冲绑定在 VS `t8`。

Correct：`从当前 L0 笔尖/切线创建发射请求 -> GPU Update/Emit -> 显式解绑 -> DrawInstanced 固定池`；last Up 有序烘干主体，GPU 存量粒子沿各自屏幕速度继续减速、缩小和淡出，只在 Fade、输入或保守粒子 dirty 活动时刷新，L2 保持不变。

Wrong：关闭多指开关后，在第一根 Touch 抬起时把仍按住但 Down 已忽略的第二根 Touch 接成新轨迹。

Correct：关闭时直接回收后续 Touch 的 consumer slot；该手指必须重新 Down 才能在之后开始新轨迹，运行中开关变化不改写既有 contact。

当前源码描述现有行为，阶段说明描述历史设计或计划。两者出现数值或工具语义差异时，应并列记录，不得自动用源码覆盖历史目标，也不得自动让实现恢复为阶段说明。

## Scenario: L0-Frame Transient Drawing Cursor

### 1. Scope / Trigger

修改 RTS range/in-air 坐标、Pointer authority、`WM_SETCURSOR`、工具光标外观、瞬态 backbuffer 绘制或 cursor dirty bounds 时，必须应用本契约。

### 2. Signatures

- `RealTimeStylusInput::Initialize(HWND, ContactInputCoordinator&, DrawingCursorEventSink*)`
- `DrawingCursorEventSink::PublishPenCursorSample/ClearPenCursorSample`
- `WindowController::ConfigureDrawingCursor/ConsumeDrawingCursorRenderRequest`
- `StrokeModelConfiguration::drawingCursorDuringContactEnabled/translucentInkCursorEnabled/mouseUsesSystemCursor`
- `DrawingController::SetDrawingCursorDuringContactEnabled/GetDrawingCursorDuringContactEnabled`
- `DrawingController::SetTranslucentInkCursorEnabled/GetTranslucentInkCursorEnabled`
- `DrawingController::SetMouseUsesSystemCursor/GetMouseUsesSystemCursor`
- `ResolvePrimaryDrawingCursorVisual`、`MakeTouchEraserDrawingCursorVisual`
- `ShouldSuppressMouseButtonUpCursorSample`
- `DrawingCursorVisualBounds`、`InkRenderer::DrawTransientDrawingCursor`

### 3. Contracts

- 自定义 Pen/Highlighter/Eraser 光标属于 L0 帧的最终瞬态视觉：先把 dirty 区域按 `L2 + L1 + L0` 合成到 backbuffer，再逐枚绘制 cursor。禁止把 cursor 写入共享 `layerL0`、L1、L2、ActiveStroke、contact payload、reconnect 或 metrics。
- shader shape type `4/5/6` 分别表示 Cursor Circle、Rectangle、EraserGripCircle；复用两项 `InkPoint`、48 字节全局常量和 resolve dual-source blend，直接输出 premultiplied Add 与 Retain。尺寸变化只能更新常量/primitive，不创建尺寸相关纹理或 `HCURSOR`。
- Pen 直径为 `max(当前基准画笔粗细, 5px * dpiScale)`；Highlighter 为 6.25x50px 固定竖直矩形。二者使用当前 RGB 和 `#B8B8B8` 细内描边，压力不改变 cursor 尺寸。`translucentInkCursorEnabled=false` 时 Pen authority 的整体与填充 Alpha 都为 1；开启后恢复 25% fill Alpha，且 fill Alpha 不得降低 outline Alpha。
- EraserGripCircle 直径直接复用 50px 画布擦除宽度，不乘 DPI、不设最小值；主体纯白，圆环宽度为 4%D，两条圆头竖线宽度为 10%D、中心偏移为 12%D、半高为 24%D，结构颜色为 `#CFCFCF`。Pen Hover 默认整体 Alpha 1，半透明开关开启后为 0.5；Contact 始终为 1。Touch Eraser Contact 始终为 1，不受开关影响。
- RTS InAir/Down/Packets 发布 X/Y/QPC、inverted 和 contact；StylusUp 只清除 Pen 样本，不把终态坐标冒充 Hover。后续真实 InAir 包才允许重新显示 Hover。InAir/Packets 只解码批次最后一个包；`Packets` 成功解码后的顺序固定为 `PublishMove -> PublishPenCursor -> diagnostics`，即使 Move 发布失败也继续更新 cursor mailbox。所有 Pen 样本都继续写入 writer latch + sequence mailbox；`inContact=false` 保持 sticky cursor render wake，`inContact=true` 不逐样本请求 render/control wake，由已有活动帧读取最新坐标。RTS 回调不得等待、分配、调用 D3D 或 `SetCursor`。
- Windows 8+ 动态解析 Pointer API，并区分 `Unknown/Pen/Mouse/Touch`；`WM_POINTERENTER/UPDATE` 使用 `GetPointerInfo/GetPointerPenInfo` 继续发布 Pen 坐标，包括 Contact 样本，不能依赖首个 RTS Down。它与 RTS 共用相同 mailbox/wake 规则；禁止为了去重而停止发布 Contact 坐标。`WM_POINTERUP` 同样只清除 Pen 样本，后续 Update 才恢复 Hover。每个有效 RTS Pen 样本都必须明确取得 Pen authority。旧系统由 RTS Pen 样本和非 promoted Mouse 消息回退。Pointer authority 仍为 Pen 且 Pen 样本有效时，低优先级 `WM_MOUSE*` 不得抢占；Pen/Touch authority 下的孤立 Mouse ButtonUp 必须忽略，避免终态兼容消息重新生成 Hover。Mouse 使用 `TrackMouseEvent/WM_MOUSELEAVE` 清理。
- Pen/Touch 离开后应清除其可见样本，但保留最后设备 authority 作为“当前无光标”状态；`WM_POINTERLEAVE` 无法取得 pointer type 时，只要旧 authority 或有效样本表明是 Pen，仍按 Pen 离开处理。禁止将 authority 立即改为 Unknown 而使旧 Mouse 样本复活。只有新的非 promoted `WM_MOUSE*` 才能明确切换到 Mouse 并恢复鼠标。
- `drawingCursorDuringContactEnabled` 默认关闭；setter/getter 使用 DrawingController 原子状态，值实际变化时只发布一次 control wake，不改变 contact packet 的 mailbox-only 规则。
- `translucentInkCursorEnabled` 默认关闭；setter/getter 使用 DrawingController 原子状态，值实际变化时发布一次 control wake，使当前与上一帧 bounds 按新 Alpha 重建。该开关只改变 Pen authority 的 transient appearance，不改工具颜色、尺寸、contact 或持久墨迹。
- `mouseUsesSystemCursor` 默认开启；WindowController 原子状态是普通绘制工具下系统 cursor 与 transient cursor 的单一真值，值变化时同时请求 cursor render 和 `WM_SETCURSOR` 私有刷新。开启时 Mouse authority 在 Pen/Highlighter/Shape 下保留 `IDC_ARROW`；关闭时这些工具改用对应的不透明应用光标。Eraser/Laser 始终使用专用应用光标，不受该开关影响。
- Pointer API 可用时，promoted Pen `WM_MOUSE*` 由消息签名直接过滤；其余非 promoted `WM_MOUSE*` 必须视为真实鼠标并立即取得 Mouse authority，即使旧 Pen mailbox 尚未收到 OutOfRange/Leave 清理。只有 Windows 7 无 Pointer API 回退才允许用有效 Pen 样本抑制低优先级鼠标消息。
- Pen/Highlighter：Pen Hover 显示应用 cursor；Pen Contact 在 Contact 开关关闭时只隐藏系统 cursor，开启时复用对应 Hover 的同一 appearance 和最新 Pen mailbox 坐标。Touch 不显示笔尖 cursor。
- Contact 开关只控制应用内 transient cursor，不修改 `ShouldHideSystemDrawingCursor`。开启后 Pen authority 仍隐藏系统箭头，不能用诊断 probe 的 `IDC_ARROW` 替代应用 cursor。
- Eraser/倒转笔尾：Pen 使用当前 Ink 透明模式，Contact 强制 Alpha 1.0，并隐藏系统 cursor。Mouse 始终显示应用 EraserGripCircle，Hover Alpha 0.5、Contact Alpha 1.0，不受普通画笔光标开关影响。每个活动 Touch eraser contact 独立显示一枚 Alpha 1.0 cursor，不存在 Touch Hover；多指不得互相覆盖状态。
- 活动主指针使用 Down 锁定的有效工具；没有匹配主指针但仍有活动批次时使用批次 `selectedTool`。Touch cursor 直接读取各自 runtime 的一致 `lastModelSnapshot` 和有效 `tool`。
- 当前和上一帧全部 cursor bounds 的并集必须加入 `frameDirty`；隐藏、离开、Up、工具切换、resize、clear、重新暴露和 Present 恢复都沿用该规则。即使 cursor 未变，只要其他几何会触发 Present，也必须把当前 cursor bounds 合入脏区并从 `L2 + L1 + L0` 重建，禁止在上一帧半透明 cursor 像素上再次叠加。静止且几何/状态不变时不得单独重复 Present。
- 窗口线程只允许 `SetCursor(nullptr)` 或非拥有的系统 `IDC_ARROW`；禁止 `CreateIconIndirect`、`SetSystemCursor`、窗口类全局 cursor 和计数式 `ShowCursor`。私有刷新必须先用 `WindowFromPoint` 确认当前 HWND 所有权。
- sink 是非拥有指针；RTS shutdown 必须先禁用并移除插件，再清理 Pen 样本和 sink。disabled、error、tablet removal、out-of-range 同样清理旧 visual。

### 4. Validation & Error Matrix

| Condition | Required behavior |
|---|---|
| Pen Hover + Pen/Highlighter，默认模式 | 隐藏系统 cursor，显示 Alpha 1 的 Circle/Rectangle |
| Pen Hover + Pen/Highlighter，半透明模式 | 隐藏系统 cursor，显示 25% fill Alpha 的 Circle/Rectangle |
| Pen Contact + Pen/Highlighter，开关关闭 | 隐藏系统 cursor，不绘制应用 cursor |
| Pen Contact + Pen/Highlighter，开关开启 | 隐藏系统 cursor，绘制与对应 Hover 完全相同的 Circle/Rectangle |
| RTS/Pointer Pen Contact sample | 继续覆盖 cursor mailbox，但不逐样本调用 `RequestDrawingCursorRender`；活动帧读取最新坐标 |
| Pen Up | 清除应用 cursor；只有后续真实 InAir/Pointer Update 才恢复 Hover |
| Pen Hover、Canvas command、Up/Leave 或首次 authority 变化 | 保持现有 render/control wake、清理和系统 cursor 刷新行为 |
| Pen Hover + Eraser | 隐藏系统 cursor；默认 Alpha 1，半透明模式 Alpha 0.5 |
| Pen Contact + Eraser 或 inverted Pen | 隐藏系统 cursor，绘制 Alpha 1.0 EraserGripCircle |
| Mouse + Pen/Highlighter/Shape，`mouseUsesSystemCursor=true` | 保留 `IDC_ARROW`，不绘制主应用 cursor |
| Mouse + Pen/Highlighter/Shape，`mouseUsesSystemCursor=false` | 隐藏系统 cursor，绘制对应不透明应用光标 |
| Mouse + Eraser/Laser | 始终隐藏系统 cursor并绘制专用应用光标，不受开关影响 |
| 有效旧 Pen 样本 + Pointer API + 非 promoted Mouse Move | 立即接受 Mouse 并切换 authority；工具切换不得重放旧 Pen 坐标 |
| N 个 Touch Eraser Contact | 同时绘制 N 枚 Alpha 1.0 EraserGripCircle |
| Touch Up + 兼容 Mouse ButtonUp | 清除对应 contact visual，并忽略终态 Mouse Up，不在原地生成 Hover |
| Touch 使用 Pen/Highlighter | 不绘制应用 cursor |
| Mouse 使用 Pen/Highlighter | 按 `mouseUsesSystemCursor` 选择 `IDC_ARROW` 或不透明应用 cursor |
| Cursor 消失或移动 | dirty 包含旧 bounds 与新 bounds，正常图层重建后无残影 |
| Contact 完成/L2 resolve | cursor 像素从未进入 operator layer，L2 只接收墨迹 |
| Pointer API 不存在 | authority 为 Unknown，按有效 Pen 优先、Mouse 次之回退 |
| Mouse 已移动后 Pen Enter | 立即用 Pointer 客户区坐标绘制 Pen Hover，不等待 Down |
| Pen 离开且无新 Mouse 消息 | 清除 Pen visual 与系统 cursor，不显示旧 Mouse 位置 |
| Pen 离开后 Mouse 再移动 | 切换 authority 为 Mouse；Pen/Highlighter 恢复箭头，Eraser 显示 Mouse 橡皮 visual |
| Renderer 尚未配置 | 不提前隐藏系统 cursor；应用初始化继续按顶层失败契约处理 |
| RTS disabled/error/removal/shutdown | 清理 Pen 样本、唤醒绘制线程并清除旧 bounds |

### 5. Good / Base / Bad Cases

- Good：两指同时擦除时显示两枚白色不透明抓手圆；其中一指 Up 只清除对应旧区，另一枚继续移动。
- Good：默认不透明模式下 Pen Hover 为实心工具光标；运行时开启半透明后，同一位置立即重建为旧 Alpha 且无残影。
- Good：默认关闭 Contact cursor 时 MPP Pen Down 后应用笔尖消失；开启后 Down 继续显示同一彩色笔尖且系统 cursor 仍隐藏。Up 后立即清除，后续真实 Hover Update/InAir 到达后才在新样本位置恢复。
- Base：Mouse 默认在 Pen/Highlighter 下保持箭头，在 Eraser/Laser 下始终显示专用应用光标；关闭 `mouseUsesSystemCursor` 后 Pen/Highlighter 改用不透明应用光标。Windows 7 路径标记待真机验证。
- Bad：把多枚 cursor 画进共享 L0 后随 contact Up resolve 到 L2，或为每次宽度变化重建 `HCURSOR`/纹理。

### 6. Tests Required

- 自动断言 Contact 与半透明配置默认关闭、鼠标系统光标默认开启；静态核对 setter/getter 与原子状态/窗口委托，值变化时能够触发即时重建。
- 自动断言默认 Pen visual 的 `opacity/fillAlpha` 为 1，开启半透明后恢复配置 appearance；Pen/Highlighter 开启 Contact visual 后与同模式 Hover 的位置、形状、尺寸、颜色、fill/outline/opacity 完全一致。
- 自动断言 appearance 有效性、sample sequence 一致性、Pen/Mouse/Touch authority、Hover/Contact/Inverted 矩阵和系统 cursor 隐藏决策；Mouse、Touch、Laser、Eraser 和倒转笔尾不受 Contact 开关影响。
- 自动断言 Circle/Rectangle/Eraser 参数、Touch 强制 Alpha 1.0、旧/新 bounds、边界裁剪和多 visual 同时存在。
- 自动断言 Pen authority + 无效 Pen 样本 + 陈旧 Mouse 样本不生成 visual；真实 Mouse authority 切换后才恢复 Mouse visual。
- 自动断言 Mouse 在 Eraser/Laser 下始终生成 visual 并隐藏箭头；普通工具按鼠标系统光标开关选择系统箭头或不透明应用光标。
- 自动断言 promoted 消息始终忽略；Pointer API 可用时非 promoted Mouse Move 即使存在有效旧 Pen 样本也不忽略；无 Pointer API 时仍按有效 Pen 样本抑制。
- 自动断言 Pen/Touch authority 抑制孤立 Mouse ButtonUp，Mouse/Unknown 不抑制；静态核对 RTS StylusUp 与 `WM_POINTERUP` 都调用 clear 而非发布终态 Hover。
- 静态核对 `Packets` 为 Move -> Pen cursor -> diagnostics，Move 失败不跳过 cursor；Contact 样本仍写 mailbox 但不逐包请求 render wake，`WM_POINTER` 分支保持发布坐标。
- 静态断言 RTS DataInterest 含 InRange/OutOfRange/InAir，InAir 选择最后 packet；搜索确认无自建 `HCURSOR` API。
- 完整 `Debug|ARM64` 解决方案构建必须重新编译两个 shader 并完成 `.cso` 资源嵌入；运行控制台测试。
- 真机覆盖 Pen、Highlighter、Eraser、笔尾、Mouse、单/多 Touch、窗口边界、SDR/HDR 和白/红/黑背景。

### 7. Wrong vs Correct

Wrong：`Pen leave -> authority=Unknown -> ResolvePrimaryDrawingCursorVisual 回退到旧 Mouse 坐标。`

Correct：`Pen leave -> clear Pen sample + keep Pen authority；新 WM_MOUSEMOVE 才切换 Mouse authority。`

Wrong：`StylusUp/WM_POINTERUP -> publish(valid=true, inContact=false, lastContactPosition)`，随后没有 Hover/Leave 时光标永久停在抬笔点。

Correct：`StylusUp/WM_POINTERUP -> clear Pen sample`；后续真实 InAir/Pointer Update 才发布 Hover，Pen/Touch authority 下的兼容 Mouse ButtonUp 直接忽略。

Wrong：`每个 RTS/WM_POINTER Contact cursor 样本都 SetEvent`，或为避免唤醒而停止发布 `WM_POINTER` Contact 坐标。

Correct：`Contact sample -> latest mailbox only`；已有 120 Hz 活动帧读取坐标，Hover、Canvas command、终态和 authority 变化仍沿用各自离散唤醒。

Wrong：`绘制时显示光标 -> 为 Pen Contact 保留 IDC_ARROW`，导致产品行为依赖系统 cursor 路径且外观与 Hover 不一致。

Correct：`drawingCursorDuringContactEnabled -> ResolvePrimaryDrawingCursorVisual 复用 Hover appearance`；系统 cursor 隐藏矩阵保持不变。

Wrong：分别修改 transient visual 与 `WM_SETCURSOR`，使 Mouse Eraser 同时出现箭头和应用圆，或两者都消失。

Correct：`mouseUsesSystemCursor -> WindowController 原子单一真值 -> 同时请求 backbuffer cursor 重建与系统 cursor 刷新`。

## Stroke Modeling Invariants

- 输入时间使用单调递增的 logical time，来源是 wall-clock delta。
- 小于 `0.25px` 的原始移动视为抖动。
- 普通笔 `SimulatedPressure` 使用相邻有效原始点的速度，不使用 modeled/predicted velocity。
- RTS 路径先按真实 snapshot 的距离/QPC 计算速度并做一次低通，再交给笔宽估算器；同一批 modeled output 不得被当成多份新的速度采样。
- 第一份有效速度只能从基准宽度渐进追随，禁止回写并瞬间改变已经可见的起笔点。当前直径时间变化上限为每秒 `3 × baseDiameter`，相邻点半径变化上限为 `0.35 × distance`。该稳定限速只作用于 `realPoints` 生成（L0→L1 稳定宽度）。
- 预测点直接继承最后真实宽度，不使用 predicted velocity 或 predicted pressure 改写半径。
- 普通笔 `HardwarePressure` 使用模型插值后的 `[0,1]` pressure 映射基准直径的 `0.2–1.4` 倍；Down 压力缺失时整笔回退 `SimulatedPressure`，后续偶发缺失保持上一真实宽度。
- 普通笔 L0 实时笔锋：`HardwarePressure` 禁用 tip taper；`SimulatedPressure`/`Fixed` 启用。taper 后仅做空间公切线安全投影（斜率 `0.95`、双向），不再套用稳定笔宽时间限速。
- L1 保护窗口仍使用配置 `liveTipDuration + predictionDuration`，与 tip 是否绘制解耦。
- 视觉连续三帧稳定后可冻结停笔更新，移动时解除冻结。

依据：`StrokeWidthEstimator::Append`、`UpdateRawPositionAndDetectMovement`、`UpdateIdleFreezeState`、`RebuildPredictedPoints`。

## Scenario: RTS Interrupted Stroke Reconnect

### 1. Scope / Trigger

修改 RTS 物理 Up、`RuntimeStroke` 生命周期、modeler 终态、活动层重建或 contact handle 回收时，必须应用本契约。

### 2. Signatures

- `StrokeModelConfiguration::interruptedStrokeReconnectEnabled`
- `DrawingController::SetInterruptedStrokeReconnectEnabled/GetInterruptedStrokeReconnectEnabled`
- `TryGetInterruptedStrokeTailDirection`
- `ResolveInterruptedStrokeReconnectMotion`
- `EvaluateInterruptedStrokeReconnect`
- `AreInterruptedStrokeReconnectIdentitiesCompatible`
- `IsInterruptedStrokeReconnectDeviceSupported`
- `IsBetterInterruptedStrokeReconnectMatch`
- `InterruptedStrokeReconnectResult::predictionExtrapolated`
- `InterruptedStrokeReconnectResult::selectedTerminalDirectionCorridor`
- `InterruptedStrokeReconnectResult::selectedInHorizonTerminalDirectionCorridor`
- `kInterruptedStrokeReconnectSimulationEnabled`

### 3. Contracts

- 开关默认开启，外部通过 `DrawingController::SetInterruptedStrokeReconnectEnabled/GetInterruptedStrokeReconnectEnabled` 运行时读写；关闭时发布 control wake，已有候选立即按保存的 Up 完成，并保留原先 Down 出队和立即 `kUp` 顺序。
- 断触修正仅支持 Touch。Touch 物理 Up 在 80ms 窗口内先作为 `kMove` 输入并冻结 modeler、真实点、prediction、L0/L1 CPU 状态、提交游标、宽度估算器和旧 handle；Pen/Mouse Up 始终立即发送 `kUp`，Cancelled 立即终结。
- 候选和新 Down 都必须是 Touch，并具有有效末速和末端方向；批次选择工具、有效工具、宽度模式、倒转状态和压力屏蔽策略必须完全一致。Up 的 `kMove` 成功后必须在新 Down 出队前冻结匹配专用 prediction，保证同帧 Up→Down 使用最新状态。
- prediction 有有效位置和正时域时，按 Down 间隔在预测时域内插值，超出时域使用最后预测位置；以 `selectedPredictionPosition - modeledPositionAtUp` 得到模型预测位移，再平移到物理 Up 坐标生成预测落点，禁止直接把预测终点瞬时速度方向与整段曲线桥接弦做硬比较。
- prediction 路径要求新 Down 到预测落点的误差不超过 `4px*dpiScale + 0.35*predictedDistance + 0.75*forecastAverageSpeed*beyondHorizon`；桥接距离上限按 `referenceSpeed=max(forecastAverageSpeed,recentFilteredInputSpeed)` 计算为 `clamp(referenceSpeed*gap*1.75 + 4px*dpiScale, 64px*dpiScale, 256px*dpiScale)`，且必须在完成方向与落点诊断后才以该上限拒绝。边界比较额外允许 `0.5px*dpiScale` 数值容差；终点速度只用于诊断。
- 冻结预测端点失败后，先在预测弦可靠、桥接夹角不超过 35°、相对 `referenceSpeed` 的速度比位于 `[0.35, 2.75]` 且 Down 超过预测时域时尝试加速自适应走廊：`adaptedDistance=max(predictedDistance,referenceSpeed*gap)`，桥接向预测弦投影后的纵向误差与横向误差必须分别不超过 `4px*dpiScale + 0.50*adaptedDistance`；禁止再次叠加时域外不确定度。
- 若预测弦走廊失败且预测末端速度方向有效，以相同 35°、速度比、动态距离和纵横误差限制尝试第二方向走廊；该走廊不依赖短预测弦达到 4px，但末端速度方向自身必须有效。禁止通过提高全局角度或扩大圆形落点容差代替第二走廊；命中时设置 `predictionExtrapolated=true`，并以 `selectedTerminalDirectionCorridor=true` 标记所选方向轴；拒绝诊断也使用该字段说明当前纵横误差属于哪条走廊。
- 若冻结端点失败但新 Down 仍在预测时域内，只允许末端速度方向使用严格补救走廊：完整间隔不超过 35ms、末端方向夹角不超过 15°、速度比位于 `[0.5,2.0]`，且继续通过现有动态距离和纵横误差限制。预测弦不得进入该分支；命中时设置 `selectedTerminalDirectionCorridor=true`、`selectedInHorizonTerminalDirectionCorridor=true`，并保持 `predictionExtrapolated=false`。
- prediction 为空、位置/时域无效或工具禁用 prediction 时，回退真实尾方向与模型真实点末端时间窗速度：96 DPI 下方向回看 12px、有效方向至少 4px、绝对距离不超过 32px，沿用 35°、自适应距离和速度比规则；模型时间窗不可用时才使用滤波 RTS 末速，并按当前间隔与绝对上限裁掉无意义尖峰。
- 多候选按归一化预测落点误差、实际选择走廊夹角、距离、较新 Up 的顺序选择；回退候选首排序量仍为实际/预测距离比例误差。命中时旧 handle 回收，新 handle 接管原 runtime，新 Down 以连续时间作为 `kMove`；不得 Reset modeler 或宽度状态。
- 最多保留 8 个候选；超限先以保存的 Up 完成最旧候选。仅剩候选时结束 1ms timer period，并等待新 Down、控制 wake 或最近 deadline。
- 超时后才发送真正 `kUp`，随后沿用同帧有序 Stored Stroke 提交、活动层重建、指标提交、handle 回收和 runtime Reset。resize 重建候选，Canvas command 最多额外等待当前候选剩余窗口。
- RTS 断触注入只用于人工测试：开启时使用固定 32 contact 状态和合成 contact id 随机生成 Up→丢弃 20–70ms Move→新 Down；关闭时必须由 `if constexpr` 选择原始 coordinator 直达分支，空模拟器不得查询频率、生成随机数、加锁或输出日志。
- `kInterruptedStrokeReconnectManualTestModeEnabled` 与 `kInterruptedStrokeReconnectSimulationEnabled` 的正式默认值均为 `false`。人工测试开关关闭时恢复笔尾倒转橡皮，并由 `if constexpr` 移除绿色桥接覆盖和拒绝诊断；模拟开关关闭时不进入任何合成 contact 热路径。

### 4. Validation & Error Matrix

| Condition | Required behavior |
|---|---|
| Pen/Mouse Up | 立即发送 `kUp`，不得进入候选或匹配断触修正 |
| 运行中关闭开关 | 发布 control wake；已有 Touch 候选立即按保存 Up 完成，之后的 Down/Up 使用原队列顺序 |
| Up 无有效末速/方向 | 立即发送 `kUp`，不进入候选 |
| Cancelled | 立即终结，不可续接 |
| 设备、工具或笔状态不一致 | 新 Down 创建独立笔画，旧候选继续等待 |
| prediction 路径超过时间、动态距离上限或预测落点误差 | 拒绝续接；正常模式不输出逐候选日志，人工测试模式只输出最近候选的一行诊断，并带新/候选 contact id 与 generation |
| 冻结预测端点失败但方向/速度连续且输入末速更高 | 尝试一次纵向/横向分离的加速走廊；命中设置 `predictionExtrapolated=true` |
| 预测弦角超过 35°但预测末端速度方向在 35°内 | 使用相同距离、速度比和纵横误差约束尝试末端方向走廊；命中同时设置 `selectedTerminalDirectionCorridor=true` |
| 新 Down 仍在预测时域内且预测端点明显偏短 | 仅在 `≤35ms`、末端方向 `≤15°`、速度比 `[0.5,2.0]` 和纵横误差均通过时使用严格终速走廊 |
| 预测弦和末端速度方向都超过 35°，或末端方向无效 | 不使用第二走廊，保留原拒绝结果 |
| 加速走廊角度超过 35°、速度比越界或方向不可靠 | 不尝试加速走廊，保留原拒绝结果 |
| prediction 为空/位置或时域无效/禁用 | 使用真实尾方向、模型尾部时间窗速度和 32px 保守上限；时间窗速度不可用才回退滤波 RTS 末速 |
| 暂留 `kMove` 失败 | 立即回退真正 `kUp`，不得遗留候选 |
| 续接 `kMove` 失败 | 新 Down 正常建笔，旧候选继续等待至超时 |
| 第 9 个候选进入 | 最旧候选同帧完成，候选数恢复为 8 |
| 80ms 到期 | 用保存 Up 发送 `kUp` 并进入既有完成批处理 |
| 断触注入期间物理 Up | 不生成恢复 Down，已合成 Up 的候选自然超时 |

### 5. Good / Base / Bad Cases

- Good：Touch 沿预测曲线 50ms 后恢复 Down；预测终点切线即使已经转向，只要新 Down 落在预测位移走廊内且桥接不超过当前速度自适应上限，原 qpc origin、modeler、真实点和宽度状态连续。
- Good：预测只覆盖 17ms、实际间隔 70ms，但预测弦与桥接夹角 10°、抬笔前输入速度高于预测平均速度且速度比约 1.0；冻结端点失败后按加速走廊命中。
- Good：圆弧桥接与 19ms 短预测弦偏差 68°，但与预测末端速度方向偏差 30°，速度比和纵横误差均通过；第二方向走廊命中且不提高全局 35°。
- Good：波浪线在 30ms 内恢复，预测仍覆盖该时刻但位移严重偏短；桥接与末端速度方向偏差 1°、速度比 1.75 且纵横误差通过，严格时域内走廊命中。
- Base：未命中的 Touch 普通笔、荧光笔和橡皮按独立笔画处理，旧候选按 deadline 正常收尾；Pen/Mouse 从不进入续接候选。
- Bad：物理 Up 立即 `kUp` 后尝试 Reset/拼接新模型，或用预测终点瞬时切线对整段曲线桥接弦做 35° 硬拒绝。
- Bad：把 `matchScore` 全局放宽以接纳加速样本，导致 90° 以上的人工重新落笔一起误连；或模拟开关关闭后仍在每个 Move 上查状态和加锁。

### 6. Tests Required

- 精确覆盖 80ms、prediction 位置时域插值/末状态、预测位移与终点切线反向仍命中、预测落点误差与数值容差、时域外不确定度、64–256px 动态预测上限、35°/32px 回退边界、DPI 缩放和 RTS 速度尖峰抑制。
- 覆盖高速直线略超基础上限、慢到快圆弧/波浪线加速走廊命中、预测弦超过 35°但末端速度方向走廊命中、时域内 35ms/15°/[0.5,2.0] 严格终速走廊、对应间隔/角度/速度比越界拒绝，并断言 `predictionExtrapolated`、两个终速走廊标记与横向/纵向误差。
- 分别以模拟开关 true/false 构建；false 的 Release 编译必须选择原始 Down/Move/Up 直接发布分支。
- 覆盖 Touch 支持、Pen/MouseLeft/MouseRight 拒绝、三类工具以及全部身份字段不一致拒绝。
- 覆盖预测距离比例误差/角度/距离/Up 时间的多候选确定性选择、候选上限和超时策略。
- 同一 modeler 依次接收 `Down → Move → 暂留 Up(kMove) → 新 Down(kMove) → Up`；橡皮使用 Disabled predictor。
- 静态检查功能关闭时保留旧队列顺序，仅候选时 timer period 已结束且 deadline 可自行唤醒。
- 实体 Touch 验证续接、主动分笔不误连、快速断触、resize、new-page 和三类工具；实体 Pen/Mouse 验证 Up 立即收尾且不续接，`SendInput` 不能替代 RTS 硬件验证。

### 7. Wrong vs Correct

Wrong：`收到物理 Up 就发送 kUp；新 Down 命中后 Reset 一个模型并手工画直线桥接。`

Correct：`Up 暂作 kMove 并保存快照；命中后把新 Down 作为原模型的连续 kMove，超时才发送保存的 kUp。`

Wrong：`用目标预测状态的瞬时 velocity 方向比较 physicalUp→newDown 的整段弦；曲线跨过拐点时接近反向就拒绝。`

Correct：`Up 的 kMove 后立即冻结 prediction；把 modeledUp→selectedPredictionPosition 的位移平移到 physicalUp，按 newDown 到预测落点的误差判定，prediction 不可用时再回退真实尾和 32px 保守策略。`

Wrong：`预测端点太短时直接把圆形容差整体乘大、继续用固定 64px 提前拒绝高速直线，或无条件沿预测方向外推。`

Correct：`先执行冻结端点判定并完成诊断；仅在方向可靠、夹角≤35°、速度比有效且目标超过预测时域时，用抬笔前输入速度修正纵向距离，并分别限制纵向和横向误差。`

Wrong：`圆弧短预测弦超过 35°后直接拒绝，或把全局角度提高到 50°。`

Correct：`保持预测弦走廊不变；其失败后只在预测末端速度方向有效且仍满足 35°、速度比、动态距离和纵横误差时，尝试第二方向走廊。`

## Scenario: RTS Stylus State And Device Width Modes

### 1. Scope / Trigger

修改 RTS desired packet、tablet property metadata、`ContactSnapshot` 笔状态、Stroke Modeler 输入或普通笔宽度来源时，必须应用本契约。

### 2. Signatures

- `ContactSnapshot::{pressure, tilt, orientation}`
- `InputWidthMode { Fixed, SimulatedPressure }`
- `PenInputWidthMode { Fixed, SimulatedPressure, HardwarePressure }`
- `InputWidthModeSettingsState::Set/Get`
- `DrawingController::SetInputWidthModeSettings/GetInputWidthModeSettings`
- `ResolveStrokeWidthMode`、`HardwarePressureDiameter`

### 3. Contracts

- RTS 必须要求 X/Y；NormalPressure、X/Y Tilt、Azimuth、Altitude 是可选属性，metadata 在慢路径缓存 index 和 `PROPERTY_METRICS` 后一次发布。
- Pen snapshot 使用模型单位：pressure `[0,1]`、tilt `[0,π/2]`、orientation `[0,2π)`；未知为 `-1`。Touch/Mouse 三项保持未知。
- 压力按 `(raw-logicalMin)/(logicalMax-logicalMin)` 归一化，不写死硬件级数。角度优先 Azimuth/Altitude，缺失时由 X/Y Tilt 推导。
- 三类设备设置编码在单个 32 位原子快照中；设置更新只影响之后 Down 的普通笔，活动笔画不切换。默认 Mouse/Touch 模拟、Pen 硬件。
- 非普通笔工具始终固定宽度。Pen 硬件模式 Down 无压力时锁定回退模拟；真实直径为 `base × (0.2 + 1.2 × pressure)`。
- pressure/tilt/orientation 即使在固定或模拟宽度模式也传入模型；坐标未移动但笔状态有效变化时不得被原始移动阈值丢弃。
- 预测点冻结最后真实半径；`SimulatedPressure`/`Fixed` 再应用既有 L0 taper，`HardwarePressure` 不叠加 tip；不得用预测 pressure 改写尾宽。

### 4. Validation & Error Matrix

| Condition | Required behavior |
|---|---|
| 扩展 desired packet 请求失败 | 记录慢路径诊断并重试仅 X/Y |
| 可选属性缺失、单位/分辨率无效 | 对应字段为 `-1`，输入继续工作 |
| `logicalMax <= logicalMin` | pressure 为 `-1`，禁止除零或猜测级数 |
| Pen Hardware Down pressure 无效 | 整笔使用 `SimulatedPressure` |
| Hardware 笔画后续 pressure 暂时无效 | 沿用上一有效值，不中途切换模式 |
| runtime 设置包含无效 enum | setter 返回 false，原子快照保持不变 |
| 仅 pressure/tilt/orientation 变化 | 进入模型但不更新位置速度基准 |

### 5. Good / Base / Bad Cases

- Good：MPP2.0 的 4095 logical max 与更高级数设备都归一化为同一曲线，倾角沿笔画进入模型，运行时切换只影响下一笔。
- Base：无压感 Pen、Mouse 和 Touch 继续通过固定或模拟模式绘制。
- Bad：把 raw pressure 当 4096 分母、在 Move 回调查询 COM，或用三个独立原子发布成组设置。

### 6. Tests Required

- 4095/8191 范围、越界夹取、无效范围；degrees/radians、Azimuth/Altitude、X/Y Tilt 和环绕。
- contact Down/Move/Up/竞争/复用中的三字段一致快照。
- Mouse/Touch/Pen 模式矩阵、无效 setter、Down 缺失回退、后续缺失保持。
- pressure 0/0.5/1 对应 5px base 的 1/4/7px，短点击使用 Down 半径，预测半径等于最后真实半径。
- 实体 Pen 验证轻重压、倾斜/旋转、抬笔；普通鼠标自动输入不能替代硬件结果。

### 7. Wrong vs Correct

Wrong：`pressure = packetValue / 4096.0f；每帧按当前设置重新选择宽度模式。`

Correct：`按当前 tablet PROPERTY_METRICS 归一化；Down 时从单原子设置快照解析并锁定整笔模式。`

## Scenario: RTS Inverted Pen Eraser

### 1. Scope / Trigger

修改 RTS `StylusInfo`、`ContactSnapshot` 倒转状态、笔尾橡皮开关、contact 工具覆盖或倒转 Pen 模型输入时，必须应用本契约。

### 2. Signatures

- `ContactSnapshot::isInvertedCursor`
- `StrokeModelConfiguration::invertedPenEraserEnabled`
- `DrawingController::SetInvertedPenEraserEnabled/GetInvertedPenEraserEnabled`
- `ShouldUseInvertedPenEraser`
- `ResolveStylusPressureForModel`

### 3. Contracts

- RTS Down、Move、Up 从 `StylusInfo::bIsInvertedCursor` 发布倒转状态；只有 `InputDeviceType::Pen` 可置为 `true`，且该字段属于 contact seqlock 一致快照。
- 开关默认开启并由单原子 Set/Get 更新；绘制线程只在 Down 读取，活动 contact 不切换语义。
- 开关开启时，倒转 Pen 只覆盖当前选择为 Pen/Highlighter 的 contact 为 Eraser；已选 Eraser、Mouse、Touch 不受影响。
- 每个 runtime 必须分别保存批次 `selectedTool` 和 contact 有效 `tool`。同批后续 contact 继承前者，禁止从倒转覆盖后的 Eraser 继承。
- 倒转 Pen 无论开关状态都锁定屏蔽 pressure；Down、Move、Up 模型输入为 `-1`，倾角和方位角继续传输。开关关闭后普通笔 HardwarePressure 因 Down 压力未知回退 SimulatedPressure。
- 倒转橡皮复用现有 50px 固定宽度、禁用 prediction、真实点直接提交 L1 的路径；不得提前引入压感橡皮语义。

```cpp
const bool supportsOverride = selectedTool == DrawingTool::Pen ||
    selectedTool == DrawingTool::Highlighter;
effectiveTool = ShouldUseInvertedPenEraser(deviceType, inverted, enabled, supportsOverride)
    ? DrawingTool::Eraser : selectedTool;
modelPressure = ResolveStylusPressureForModel(deviceType, inverted, rawPressure);
```

### 4. Validation & Error Matrix

| Condition | Required behavior |
|---|---|
| Pen + inverted + enabled + Pen/Highlighter | 当前 contact 锁定 Eraser，pressure 为 `-1` |
| Pen + inverted + disabled | 保持选择工具，pressure 仍为 `-1` |
| Pen + inverted + selected Eraser | 保持 Eraser，不产生第二种工具语义 |
| Pen + non-inverted | 保持选择工具和原压力链 |
| Touch/Mouse 即使标志异常为 true | 不触发倒转橡皮或压力屏蔽 |
| Move/Up 标志相对 Down 变化 | 保持 Down 锁定的工具与压力策略 |
| 倒转 contact 后同批正常 contact Down | 继承原 `selectedTool`，不得继承有效 Eraser |

### 5. Good / Base / Bad Cases

- Good：MPP2.0 翻转后直接使用现有橡皮路径，关闭开关后笔尾可用模拟压感书写，同批正常 contact 仍按原工具绘制。
- Base：不支持倒转标志的 Pen、Mouse 和 Touch 保持既有行为。
- Bad：把倒转后的 `tool` 当作批次选择继续传播，或仅在 Down 屏蔽压力而在 Move/Up 恢复硬件压力。

### 6. Tests Required

- 倒转字段在 Down、Move、Up、竞争读取和 slot 复用中的一致性。
- device/inverted/enabled/tool-eligibility 策略矩阵；倒转 pressure 为 `-1`，HardwarePressure 回退 SimulatedPressure。
- ARM64 `Debug|ARM64` 完整解决方案构建与控制台测试。
- MPP2.0 真机覆盖 Pen/Highlighter 翻转擦除、开关关闭后的笔尾书写、抬笔和恢复正常笔尖。

### 7. Wrong vs Correct

Wrong：`runtime.tool = Eraser; nextBatchTool = runtime.tool; model.pressure = snapshot.pressure;`

Correct：`runtime.selectedTool` 保留批次选择，`runtime.tool` 只保存当前 contact 覆盖；倒转 Pen 整笔向模型传 `pressure=-1`。

## Three-Layer Contract

- `L2`：已完成笔画的最终 premultiplied RGBA 画布，背景真透明。
- `L1`：共享临时操作层，合并全部活动 contact 已稳定的前缀操作。
- `L0`：共享临时操作层，合并全部活动 contact 当前帧仍会变化的真实尾部、预测与笔锋；每帧只恢复一次单位操作再重画。

应用绘制 cursor 虽属于 L0 帧的瞬态视觉层级，但只在 backbuffer 最终合成阶段绘制；它不是 `layerL0` operator 的内容，也不得参与任何 L0→L2 resolve。

临时操作层表示：

```text
Result = Add + Retain * Below
```

`Add` 使用 BGRA8，`Retain` 使用 R16F；新层的单位操作是 `Add=(0,0,0,0)`、`Retain=1`。

绘制过程中，保护窗口之外的真实前缀推进到 L1。普通笔保护时间为：

```text
liveTipDuration + predictionDuration
```

contact 结束时，Pen 用已确认真实点合并稳定前缀和完成态 taper 尾段，连接点只保留一次；Highlighter/Eraser 保存完整真实中心线。Down 后立即 Up、尚未生成建模点时使用初始点生成单点。prediction 和 `time` 不进入 `InkStroke`。每条非取消、非 Laser Stroke 必须先追加到当前 `InkCanvas`，再从刚追加的同一对象重建 operator 几何并独立 resolve 到 L2；同帧只共享最终 backbuffer composite/present，不共享跨 Stroke coverage。全部完成项处理后再从 CPU runtime 重建仍活动 contact 的 L1/L0。

## Scenario: RTS Multi-Contact Input And Rendering

### 1. Scope / Trigger

修改 RTS packet、contact 路由、跨线程队列、活动笔画、resize/Canvas command、临时层合成或呈现时，必须应用本契约。

### 2. Signatures

- `ContactInputCoordinator::PublishDown/PublishMove/PublishUp/PublishCancelled`
- `ContactInputCoordinator::TryDequeue(ContactRecord*&)/WaitDequeue(ContactRecord*&)`
- `ContactInputCoordinator::TryReadSnapshot/Recycle/PublishControlWake`
- `ContactInputCoordinator::CaptureWakeGeneration/WaitForWake`
- `ContactInputCoordinator::DiagnosticsSnapshot`
- `RealTimeStylusInput::Initialize/Shutdown`
- `IStylusSyncPlugin::RealTimeStylusEnabled/RealTimeStylusDisabled/Error`
- `IStylusSyncPlugin::StylusDown/Packets/InAirPackets/StylusUp`
- `DrawingController::Run`

### 3. Contracts

- contact identity 是 `(tabletContextId, contactId, generation)`；route 必须通过 generation 和状态的原子 CAS 防止 ABA，记录内容用单写者 seqlock 发布。
- pool 容量是 `round_up_32(max(32, 2 × (SM_MAXIMUMTOUCHES + 2)))`，RTS 启用前一次性建立；每块 32 个 slot 和一个始终无锁的 `uint32_t freeMask`。CAS 清位取得 slot，record 保存不可变 owner/bit，回收顺序严格为 `ConsumerOwned → Free → release 置位`。
- ingress payload 是原生 `ContactRecord*`：非空只表示 Down，空指针只表示合并的 `ControlWake`。consumer 取到指针后立即读取 generation 并构造线程私有 handle；禁止恢复带 kind 的 command 包装。
- queue 容量是 `max(256, slotCapacity + 1)`。初始化阶段创建 32 个可 CAS 独占的 Down producer token 和 1 个 control token，并以三参数构造器禁止 implicit producer；热路径只能调用 token 版 `try_enqueue`，不得回退到会分配的 `enqueue`。
- contact 查找只遍历 `~freeMask` 的 occupied bits，但 generation/state/tcid/cid 仍是最终路由判据；位图只负责跳过空 slot，不能替代 route 校验。
- Down 成功入队后 consumer 才拥有 handle；Down 入队失败必须先关闭并排空可能已进入的 Move，再回收。Up/Cancelled 是 sticky terminal，成功关闭后后续 Move 不得覆盖。
- packet X/Y 按当前 `tcid` 的 `GetPacketDescriptionData` 返回顺序查找，但所有输入统一使用 `GetAllTabletContextIds` 首 context 的 ink-to-device scale 转为像素，保持与已广泛验证的 `IdtRts.cpp` 一致。禁止改用当前 Pen/Touch context 自己的硬件比例，否则坐标会落到画布外；同步回调不得分配、建模、绘制或记录无界逐包日志。
- RTS 多点启用是三段式契约：第一根手指按下前给 HWND 设置 `MICROSOFT_TABLETPENSERVICE_PROPERTY`，窗口过程对 `WM_TABLET_QUERYSYSTEMGESTURESTATUS` 返回 `TABLET_ENABLE_MULTITOUCHDATA`，并令 `IRealTimeStylus3::MultiTouchEnabled=TRUE`。只完成 COM 属性不能视为多点已启用。
- 同一组窗口标志禁用 press-and-hold、pen feedback 和 flick；可用时同时调用 `IRealTimeStylus2::put_FlicksEnabled(FALSE)`，避免笔事件被系统手势延迟或接管。
- `Disabled`、RTS `Error`、tablet 移除和 shutdown 都把生产中的 contact 发布为 Cancelled；COM 初始化、FTM 聚合、禁用、移除插件与释放全部在完成 MTA 初始化的主线程完成。
- 每个 tablet context 的 packet property、metrics、device type 和 scale 必须在 lifecycle/contact-entry 慢路径编译为固定 immutable decoder。`Packets` 只通过 `(tcid,cid)` binding 解析 decoder，并在成功解码后严格执行 Move publication、Pen cursor mailbox publication、diagnostics；Move 失败仍更新 cursor。`InAirPackets` 只查固定 decoder cache，miss 直接丢弃，二者均不得执行 COM、context 枚举、decoder recovery、分配或 mutex wait。
- `Error` 不是 decoder lifecycle boundary：它只清 cursor、active bindings、断触模拟和 producing contacts，必须保留当前 decoder slots、shared position scale、enabled state 和 lifecycle generation。只有 Enabled pre-reset、Disabled、UpdateMapping、TabletRemoved 和 TabletAdded full-rebuild fallback 才执行完整 decoder lifecycle reset。
- packet/contact callbacks 通常来自 RTS high-priority execution/tablet-data thread；`RealTimeStylusEnabled/Disabled` 可能在修改 `Enabled` 或 plugin collection 的 caller thread 执行，不能假设所有 `IStylusSyncPlugin` callbacks 天然同线程串行。普通 decoder/binding 数组必须由项目自己的 rare-writer/lock-free-reader gate 发布：lifecycle、Down、Up 和 Error 使用 writer mutex + writer bit + reader drain，`Packets/InAirPackets` 只做一次 lock-free CAS，失败立即丢包且不等待。
- state gate 的 happens-before 必须由 C++ atomic memory order 建立：reader acquire-CAS/release decrement，writer acq_rel 发布 writer bit、acquire 等待 reader count 清零、release 清 writer bit；writer bit 发布后禁止新 reader 进入。`put_Enabled(FALSE)` 的 Disabled writer 必须先 drain 已进入的 packet readers，返回后才 Remove plugin 和释放对象。
- 每个活动 contact 拥有独立 CPU runtime，其 GPU 几何共同重建到共享 L1/L0；不得提前进入 L2。完成 contact 按 `active` 稳定顺序逐条 Finalize、Append、从 Stored Stroke 重画并独立 resolve；随后重建仍活动 contact，同帧仍只 composite/present 一次。
- resize 成功后重建活动临时层；`0/5/8` Canvas command 在有活动或续接候选 contact 时保持 FIFO，最后一个 contact 完成后才执行撤回或页面恢复。无活动 contact 时阻塞等待；1ms timer period 只在活动区间启用，且每次成功 begin 必须配对 end。
- waitable swapchain resize 必须先 `GetDesc1`，并原样传回 `BufferCount/Format/Flags`；部分驱动在传入零值时第一次 resize 成功、恢复尺寸时失败。
- 运行指标关闭时不创建会话、不启用输入计数、不写文件；开启后原始样本写入忽略的 `TestResults/`，仓库只保存环境、阈值和分位数摘要。
- 开发诊断 HUD 默认关闭；启用时使用独立 owned layered popup，必须点击穿透且不激活，也不进入 backbuffer、L0/L1/L2、Laser coverage、dirty rect、shader 或 presenter。
- HUD 只在帧首尾都有物理 contact 时采样绘制线程已有的 `frameStartMs/workMs/lastPresentDurationMs_/lastPresentSucceeded_`。每帧只做固定次数的计数、求和与平方和累加；禁止排序、字符串格式化、contact 明细构建、Win32 进程查询、DXGI 显存查询、PostMessage、GDI 或 layered-window 更新。
- HUD 快照与窗口呈现最多 10 Hz；只有快照到期时才格式化短文本并通知窗口线程。实际 FPS 来自帧起始间隔，`Estimated Uncapped FPS` 由 `1000 / averageWorkMs` 推算，不得把目标帧等待计入性能余量。
- HUD 关闭时绘制热路径只允许一次轻量 enabled 分支，不持续统计；物理 contact 序列结束时清空当前累加状态并保留最后快照，下一笔不得把空闲时间算入 frame interval 或 FPS。

### 4. Validation & Error Matrix

| Event / failure | Required behavior |
|---|---|
| Down queue enqueue failure | 关闭 route、排空已进入 writer、回收；不得直接写 Free |
| pool exhausted / 32 tokens occupied | 拒绝 Down，禁止扩容、隐式 producer 或分配回退 |
| `ContactRecord* == nullptr` dequeued | 只解释为 `ControlWake` 并清 pending；不得解引用 |
| stale generation / duplicate recycle | route 校验失败且不重复置位，不得回收新一代 contact |
| Concurrent Move and Up/Cancelled | terminal 状态胜出，后续 Move 不覆盖终态 |
| Snapshot read during write | seqlock 重试，只接受前后相同的偶数 sequence |
| `MultiTouchEnabled=TRUE`，但 HWND 未 opt-in | 视为初始化契约不完整；Touch/多指可能完全没有 callback |
| `WM_TABLET_QUERYSYSTEMGESTURESTATUS` | 返回多点 opt-in 与禁用 press-and-hold/flick 的固定标志，不调用外部 COM |
| 第一份或突变的速度样本 | 从当前直径平滑追随，不回写已可见点，不允许单帧直接跳到目标直径 |
| Disabled / Error / tablet removal | 所有 producing contact 以 Cancelled 结束 |
| Error 后继续收到同 context Down | 旧 binding 已清除且旧 contact 为 Cancelled；原 decoder、scale 和 generation 保留，新 Down 无需等待 Enabled/mapping/tablet callback 即可重新 bind 和解码 |
| InAir decoder cache miss | 记录 bounded/non-blocking diagnostics 后丢弃 hover packet；禁止 Resolve/Ensure/Build、`GetAllTabletContextIds` 或其它 COM recovery |
| lifecycle writer 与 packet reader overlap | writer bit 阻止新 packet reader；writer 等既有 reader 退出后修改固定数组，packet callback 不 spin、不 retry、不取得 writer mutex |
| Resize succeeds | 保留 L2 交集并从 CPU runtime 重建全部活动 L1/L0 |
| waitable swapchain second resize | 保留原 swapchain 描述字段；不得用 `ResizeBuffers(..., UNKNOWN, 0)` 丢弃 flags |
| Canvas command with active contact | FIFO 保留并延后到活动集合为空；完成 Stroke 仍写入命令执行前的当前页 |
| Multiple Up in one frame | 按 Canvas 追加顺序逐 Stroke resolve；一次 composite、一次 present |
| Up arrives after a visible prediction | 只保存确认真实点；Pen 烘入 taper 并去重连接点，prediction 不进入 Stored Stroke |
| Present failure | 保持整画布重呈现请求，下一帧恢复 |
| HUD layered/GDI presentation failure | 隐藏 HUD；不得改变主窗口、绘制资源、dirty rect、Present 或输入生命周期 |

### 5. Good / Base / Bad Cases

- Good：32 个同步生产者使用显式 token 并发 Down，slot/pointer 唯一；两支笔交错移动并同帧抬起，终态完整且批量提交。
- Good：Enabled/Disabled caller thread 与 packet thread 交错时，rare writer gate 安全发布 decoder/binding generation；`Packets/InAirPackets` 仍只有一次 non-waiting atomic reader attempt。
- Base：单 contact 的 Down/Move/Up 使用相同 generation、seqlock、pointer ingress 和批量渲染路径；慢速到快速过渡时宽度连续。
- Bad：容量耗尽后调用隐式 `enqueue` 扩容，只设置 `IRealTimeStylus3::MultiTouchEnabled` 却不处理 HWND opt-in，或因 callback 都属于 sync plugin 就假设 decoder/binding 普通数组天然 thread-confined。

### 6. Tests Required

- `Debug|ARM64`、`Release|ARM64` 全解决方案 Rebuild，且两个 shader、C++ Modules、资源嵌入与最终链接成功。
- `Release|x64`、解决方案 `Release|x86`（项目映射 Win32）Rebuild 并运行测试，验证 8/4 字节指针 payload 和 lock-free 静态断言。
- 自动并发覆盖 32 个生产者、32/64/多 block 容量、耗尽/复用、无分配 Down、Move/Up 竞争、stale generation、重复回收、Cancelled/shutdown 和 ControlWake/Down/终态唤醒。
- 静态检查窗口属性、`WM_TABLET_QUERYSYSTEMGESTURESTATUS` 与 `IRealTimeStylus3` 三处多点 opt-in 同时存在。
- 静态验证 generation/state CAS、sticky terminal、seqlock、零自旋阻塞等待、timer begin/end 配对和 Release 无逐帧日志。
- 自动验证 Error active-only reset 后 decoder/scale/generation 仍有效、旧 contact 为 Cancelled，且同 context 可立即重新 bind/decode；静态核对 `Error` 不调用完整 decoder lifecycle reset。
- 自动验证 InAir decoder hit/miss 和 state gate overlap；静态截取 `Packets/InAirPackets` callback body，断言没有 decoder rebuild/COM/writer mutex/allocation 路径。
- 真机验证鼠标宽度连续、Pen/Touch 单 contact、双 Touch 交错、同时抬起、活动时 resize、活动时 new-page、设备禁用/拔出、长时间 idle CPU 和最终点位置。普通 `SendInput` 不能替代 RTS 硬件验证。
- Release 自动基准至少连续三轮：即时工具 Down→Present p99 ≤ 8.33ms，活动帧间隔 p99 ≤ 9.5ms，>16.67ms 比例 <1%，连续空闲至少 4.9 秒且 frame/Present 零增长。
- 快速曲线末端抬笔时，Stored Stroke 无 prediction 残留、回头或重复连接，且首次 L2 绘制与 Stored Stroke 重放一致。

### 7. Wrong vs Correct

Wrong：`收到 Up 就立即 Present；或把同帧多个 Stroke 合到同一 MAX/MIN coverage 后一次 resolve。`

Correct：`route 用 generation+state 精确交接；同帧 terminal contact 按稳定顺序逐条 Append + resolve，最后统一 composite/present 并重建剩余活动层。`

Wrong：`Up 后把上一帧 predictedPoints 复制到 InkStroke，或首次显示仍从 ActiveStroke 的另一套缓存绘制。`

Correct：`只从确认真实点生成最终 InkStroke；先追加，再从刚追加的对象完成首次绘制。`

Wrong：`put_MultiTouchEnabled(TRUE) 成功，所以窗口已经能收到多指。`

Correct：`HWND 属性、WM_TABLET_QUERYSYSTEMGESTURESTATUS 返回值和 IRealTimeStylus3 三处同时 opt-in，再用实体 Touch/Pen 验证 callback。`

Wrong：`IngressCommand{ kind, record } 入队；token try_enqueue 失败后调用普通 enqueue。`

Correct：`非空 ContactRecord* 只表示 Down，nullptr 只表示 ControlWake；全部 producer 在初始化期建 token，热路径只调用 token try_enqueue。`

Wrong：`所有 IStylusSyncPlugin callback 都在同一线程，所以 Error 可以清 decoder，Packets 可以直接读取 lifecycle writer 正在修改的普通数组。`

Correct：`Error 只清 active contact state；decoder/binding 通过 rare-writer/lock-free-reader gate 发布，Packets/InAir cache miss 直接丢包且永不进入 COM recovery。`

### Visual Prediction Is Not Persistent Ink

上面的抬笔规则只描述当前测试程序的瞬时视觉画布：

- prediction 是瞬时视觉结果。
- L2 是当前页已落定的像素缓存，不是文档真值。
- `InkCanvasCollection` 是当前会话的 CPU 文档真值，只从已确认输入生成。
- 未确认预测点不得无条件写入永久笔迹、回放记录或跨版本格式。

当前只保存会话内对象；UInk 编解码、文件生命周期和旧页重放仍需独立任务。

## Scenario: Free Canvas Navigation And Predictive Recovery

### 1. Scope / Trigger

修改 Canvas 坐标变换、`CanvasCommand::TranslateViewport`、Touch 批次归属、平移/惯性、Pen 接管、分页视口、composition tile 恢复或可信 L2 快照时，必须同步应用本节与 [CPU/GPU Contracts](../shaders/cpu-gpu-contracts.md)。当前 `scale` 固定为 `1`；本契约不包含缩放、旋转、EDID 物理距离、边缘拉伸或回弹。

### 2. Signatures

- `InkViewport { float x, y, scale }`，`InkCanvas::SetViewport(InkViewport) -> bool`
- `CanvasCommand { CanvasCommandType type; float deltaX; float deltaY; }`，其中 `TranslateViewport` 的 delta 是可见内容屏幕位移 DIP
- `ScreenToCanvas(screen, viewport)` / `CanvasToScreen(canvas, viewport)` / `ApplyCanvasContentTranslation(viewport, contentDelta)`
- `CanvasTouchGestureState::OnTouchDown/OnTouchUp/Update/InterruptForPenOrMouse`
- `BeginCanvasPan/UpdateCanvasPan/CanvasPanReleaseAgeSeconds(releaseQpc, lastInputQpc, qpcFrequency, cancelled)/EndCanvasPan(motion, secondsSinceLastInput)/StepCanvasPanInertia/StopCanvasPan/ShouldPrioritizeDrawingContact`
- `PlanCanvasRenderTiles` / `ComputeCanvasRenderBudget` / `ComputeCanvasSnapshotScreenIntersection`
- `CompositionRestoreRequest::{tiles, viewportX, viewportY, canvasWidth, canvasHeight}`
- `ContactInputCoordinator::HasPendingWork()` 与窗口线程 Pen cursor mailbox

### 3. Contracts

- `InkViewport.x/y` 是屏幕左上角对应的 Canvas 世界坐标，固定映射为 `screen = canvas - viewportOrigin`。Pen、Highlighter、Eraser、Shape 和 Laser 在进入模型/文档前都反变换为 Canvas-local；瞬态 L0/L1/Laser/粒子/cursor 在当前视口下重建。Viewport 必须 finite、`scale == 1` 且 `x/y` 在 `[-1048576, 1048576] DIP`；触限轴速度立即归零。
- 方向键只在非自动重复 Down 时发布一次 `TranslateViewport`，内容移动 `64 DIP`，不启动惯性。Viewport 不进入 Undo，也不进入 `InkHistoryRasterKey`；视口变化丢弃依赖屏幕坐标的热前像，但保留 Canvas-local composition cache。
- 只有零 Touch 开始的批次可识别平移。首指静止时立即按工具绘制；第二指的输入时间戳与首指相差 `<= 180ms` 时，即使绘制线程稍晚执行 `Update`，仍取消该批全部 Touch 临时内容、清 Laser/笔尖/粒子并从剩余 Pen/Mouse contact 重建 L1/L0 后进入平移。超时后该批直到全部 Up 都不可再识别平移。
- 平移中新增 Touch 只加入手势、不绘制；拓扑变化重设中心，剩一指仍可拖动。QPC 是唯一时间源；固定容量 `24` 的 Move 样本在约 `100ms` 窗口内对累计中心位移做线性拟合。零位移包、Up 终态和渲染空帧不得进入估速；最后 Up 只补齐最终中心位移，并以 `releaseQpc - lastVelocitySampleQpc` 判断时效。Cancelled、反向时间或无有效 Move 样本禁止惯性。
- 惯性中首个 Touch 进入特殊 180ms 候选期：惯性继续且该指不绘制。及时第二指接续旧速度；候选超时后首指整段生命周期都不补画，并请求加速制动，迟到 Touch 可绘制但不能与首指组成平移。
- 接续惯性时应用层旧速度在约 `120ms` 内与新手势位移混合：同向叠加，反向先制动再反向，合速度钳制到 `24000 DIP/s`。新手势立即停止旧惯性步进并保存残余速度；抓取、混合和反向均由同一个应用层状态机拥有。
- Windows Tablet/RTS 在活动多 Touch 批次中不保证交付后来加入的 Pen contact；本机 Windows 11 ARM64 实测双 Touch 均持续收到 RTS Packets，但随后 Pen 没有任何 RTS `StylusDown/Packets/StylusUp`，只有独立 `WM_POINTER` 触觉/光标事件。活动 Touch 跟手平移期间的 Pen contact 因此锁存为 suppressed-until-up：不刹停、不绘制、不发布接触光标、不预启动触觉，也不能在 Touch 抬起后的惯性阶段补画。Pen 抬起后，惯性阶段重新产生的新 Pen Down 才可抢占；Pen hover 仍只在惯性中提高减速度。Mouse contact 仍可立即抢占。
- 窗口 Pen/Mouse mailbox 必须在导航推进和 viewport tile 恢复前读取。活动 Touch 跟手时 Pen mailbox 只开始/维持 suppression；惯性阶段确认可抢占 contact 后，先排空已发布 Down、创建 runtime 并固定 viewport，再执行本帧导航/恢复。若 RTS Pen Down 曾到达，轻量 contact 消费 Up/Cancelled 后记录终态 QPC，并忽略 `sample.qpc <= terminal.qpc` 的陈旧 Pointer contact；更晚的新 Down 可正常抢占。完全没有 RTS Pen contact 的 Pointer-only 情况只能等待 mailbox 变为非 contact 或离屏后解除，防止旧样本在惯性中误刹停。
- `CanvasPanMotionState` 是手势、估速和惯性的唯一权威；生产路径不得再引入 `IManipulationProcessor`、`IInertiaProcessor` 或与应用状态机并行的系统速度真值。惯性按真实绘制帧 QPC 间隔、`DIP/s^2` 线性减速度和梯形积分推进：普通滑行为 `6000`，Pen hover 或惯性候选超时为 `12000`；单步时间限制为 `50ms`，避免调度长停顿产生位移尖峰。
- 新 Touch Down 出队时，旧首指可能已在合并 mailbox 中发布 `Up/Cancelled`，但导航循环尚未消费。若当前不是活动 Pan，且旧终态 QPC 不晚于新 Down，必须先只调用 `CanvasTouchGestureState::OnTouchUp` 退休旧批次资格；不得提前回收 contact、跳过 Stored 收尾或让新批次取消旧笔画。
- Pan 开始、额外触点加入以及触点移除后，都必须从同一组 snapshot 重建 `previousPanCentroid` 与每个剩余触点的 `velocityPosition`；`ResetCanvasPanVelocitySamples` 的 QPC 不得早于 `CanvasPanMotionState::lastUpdateQpc`。只重建几何中心会把拓扑跳变误算成速度尖峰。首指 Down 开始的新零 Touch 批次必须显式重置旧批次的中断和超时资格。
- 视口硬限位以 `double` 候选原点直接比较 `+/-1048576 DIP`；只有候选真实越界才报告 clamp 并清零该轴速度。禁止通过 float 应用前后差值反推 clamp，远端 viewport 的量化误差不是撞边。
- Pen mailbox 仍用于活动 Touch 平移中的 suppression 和惯性 hover 制动。Mouse mailbox 不得作为导航抢占真值，因为系统可能把 Touch 提升为 Mouse；只有从 contact coordinator 出队的真实 Mouse Down 才可抢占。
- 页面切换、Undo、Resize 和键盘平移先终止手势/惯性。每个 Page/Device Canvas 保存自己的 viewport；切页恢复目标 viewport，只保存位置不保存速度。Undo 只改变当前页 RenderItem visibility，不能移动当前 viewport，离屏内容仍按 Canvas-local tile 恢复。
- L2 是当前 viewport 的清晰稳定层；每次 viewport 变化从 Canvas-local 有符号 `256x256` tile 恢复。规划优先级固定为可见缺失区、运动前缘、150ms 预测扫掠区、后缘维护，预测距离不超过 `1.5` 个视口对角线并带一圈 tile 余量。
- 每帧恢复预算使用目标帧间隔、上一帧工作/Present 耗时和 tile EWMA，预留 `1ms` 且上限 `4ms`；允许预算为零。每个 tile 前检查 `HasPendingWork()` 和 wake generation，输入到达必须让出。可见 tile 失败保留当前游标并重试，不能把 L2 标记为清晰；仅全部可见 tile 清晰后刷新可信快照。
- `CanvasRuntimeHistory` 用可见 composition tile 引用计数维护稀疏索引；Append、Undo 和可见项 geometry update 必须同步加减引用。平移规划先计算当前/预测/后缘带一圈 tile 余量的查询范围，再从索引枚举内容，禁止每帧遍历所有 RenderItem。footprint 是 Canvas-local 真值，窗口 Resize 只更新 raster generation 并恢复当前可见 tile，不重算全页 footprint。
- Stored Pen/Highlighter/Eraser 的 tile restore 只上传与目标 Canvas 区域相交的连续点段；每段保留相邻连接点，胶囊、擦除和荧光 sweep 在 tile 边缘必须连续。Shape 继续使用解析外框筛选并只提交两个端点。
- 可信快照只含最后一次完整清晰 L2。平移时按两个世界视口的真实交集重投影到 backbuffer；低于 `300 DIP/s` 不模糊，之后沿运动方向增加并钳制到 `12 DIP`。清晰 tile 后画并覆盖兜底；超出快照覆盖的区域保持真实透明。兜底像素不得写回 L2、文档、history、热前像或 composition cache。

### 4. Validation & Error Matrix

| Condition | Required behavior |
|---|---|
| viewport 非有限、scale 不为 1 或越界 | `SetViewport` 返回 false，原 viewport 不变 |
| 内容位移触及单轴保护值 | 应用可表示部分并把该轴速度清零，另一轴继续 |
| 第二 Touch 时间戳恰好 180ms | 允许平移；判定不能依赖绘制线程实际处理延迟 |
| 首 Touch 已在 L0/L1 产生内容后及时第二指 | 丢弃该批 Touch 内容并从仍有效 Pen/Mouse 重建全部瞬态层 |
| 惯性首指超时 | 不补画该指；加强制动；迟到 Touch 不得重新组成手势 |
| Pen/Mouse 已 contact | 阻止新双指手势；活动 Touch 跟手中的新 Pen 抑制到 Up，Mouse 仍可抢占；惯性中的新 Pen/Mouse Down 先于导航和 tile 恢复消费并立即刹停 |
| 无新 Touch Move 的渲染帧 | 保持最后有效速度；不得用零中心位移覆盖 |
| 最后 Touch Up | 先应用最终 centroid 位移但不把 Up 写入估速；按 `Up QPC - last Move sample QPC` 判断，最近样本不超过 `100ms` 时用拟合速度启动惯性，处理线程晚到不影响 |
| 触点数量变化 | 重建 centroid 和 Move 样本零点，保留锁存速度；首个拓扑帧不得制造速度尖峰 |
| 远端有限 viewport 发生小位移 | 按 double 候选值判断未越界，不得因 float 量化差异报告 `viewport-clamp` |
| Mouse mailbox 显示 contact 但没有真实 Mouse contact 出队 | 不打断平移或惯性；避免 Touch-to-Mouse 提升污染导航状态 |
| 可见 tile 恢复失败或有新输入 | 不推进失败 tile；保持恢复 pending，下一帧重试 |
| 快照/当前视口无交集 | 不采样、不拉伸边缘，区域保持透明背景 |
| Undo/page/Resize | 快照签名失效；Resize 不重算 Canvas-local footprint；失败的权威恢复继续排队，不能把兜底提交为文档内容 |

### 5. Good / Base / Bad Cases

- Good：普通 `3200 DIP/s` 甩动按 `6000 DIP/s^2` 约滑行 `0.53s / 853 DIP`；快速同向反复滑动即使残余速度较小也能继承并增速；反向滑动先消耗旧速度；活动跟手中的 Pen 不打断且不会留下接触起点光标，抬笔后惯性中的新 Pen 靠近时按 `12000 DIP/s^2` 快速减弱并在落笔前固定 viewport。
- Base：方向键一次移动内容 64 DIP；切到另一页恢复该页上次 viewport；负坐标 Stroke 由有符号 tile 正确显示和撤回。
- Bad：用处理时刻而非 contact QPC 判定 180ms 或释放速度时效、用单个 `delta/dt` 或 Up 终态估速、跨触点拓扑拟合、每个渲染帧用零位移刷新速度、用 float 应用差值反推 clamp、只用 mailbox 刹停却把真实 Down 留到 tile 恢复之后、把 viewport 写入 raster key，或用模糊快照覆盖未知区域。

### 6. Tests Required

- 单元测试覆盖 180ms 内/等于/超时和绘制线程迟到、静止首指撤销、惯性首指抑制、迟到 Touch、额外 Touch、低残余同向/反向接续、活动跟手 Pen suppression、惯性 Pen hover/Down、Mouse contact、导航 contact 优先判定、速度上限、Move-only 多样本拟合、零位移保持、Up 尖峰排除、`100ms` 旧样本淘汰、拓扑重建、新批次资格复位、Cancelled、远端 float viewport 和真实单轴范围保护；静态检查生产源码不包含 Windows manipulation/inertia 接口。
- 坐标/文档测试覆盖 Pen、Highlighter、Eraser、四种 Shape 的 Canvas-local 完成态，Laser 瞬态变换，负/远端坐标、有符号 tile、每页独立 viewport、离屏 Undo 和 viewport 不进入 history raster key。
- 渲染规划测试覆盖双方向预测、`1.5` 对角线上限、优先级、0/4ms 预算、pending input 让出、失败 tile 不推进、可见完成、快照交集、300 DIP/s 清晰阈值和 12 DIP 模糊上限。
- ARM64 Debug/Release 完整 solution 构建并运行两套测试。实体 Touch/Pen、快速反复滑动手感、视觉重投影/模糊、窗口 Resize、翻页、D3D Debug Layer 和 Windows 7 未执行时必须明确标记。

### 7. Wrong vs Correct

Wrong：`screen point 直接写文档 -> viewport 进入 history key -> 平移时整页缓存失效 -> 模糊图写回 L2。`

Correct：`真实 Touch Move + QPC 进入固定窗口线性拟合 -> Up 只补最终位移并以 Up QPC 检查 100ms -> 应用层线性惯性；拓扑变化重建拟合零点但保留锁存速度。screen -> Canvas-local 文档真值；viewport 仅决定目标矩形；Canvas tile 清晰恢复写 L2，可信快照只在 backbuffer 下方作视觉兜底。`

Wrong：`活动 Touch 跟手时只凭 Pointer 触觉假设 RTS Pen contact 存在并清零速度；或惯性中 Pen mailbox 清零速度 -> 本帧继续 tile 恢复 -> 帧后段才出队 Pen Down。`

Correct：`活动 Touch 跟手 + Pen mailbox -> suppression 锁存到 Up；惯性 + 新 Pen/Mouse contact -> 导航/恢复前优先出队真实 Down -> 固定 viewport -> 同帧创建绘制 runtime。`

## Scenario: Ink Document Persistence

### 1. Scope / Trigger

修改完成态 Stroke、Canvas/Page/Collection、首次 L2 提交、新建页、未来保存/导出/同步/回放时，必须应用本契约。

### 2. Signatures

- `StoredInkPoint { float x, y, width; }`
- `StoredInkStyle { StoredInkType inkType; uint32_t fallbackRgb; float opacity; uint16_t texture; }`
- `InkStroke -> InkCanvas -> InkPage -> InkCanvasCollection`
- `InkCanvas::AppendStroke(InkStroke) -> optional<size_t>`
- `InkPage::FindCanvas/GetOrCreateCanvas(DeviceKey, InkViewport)`
- `InkCanvasCollection::AppendPage(InkGuid|InkPage) -> optional<size_t>`
- `FinalizeStoredStroke(ActiveStroke, StoredInkStyle, taperSeconds, scratch)`
- `CanvasCommand { Undo | NextPage | PreviousPage }`
- `WindowController::TryDequeueCanvasCommand(CanvasCommand&) -> bool`

### 3. Contracts

- `x/y` 是 Canvas-local 物理像素，可为负数或有限的屏外值；`width` 是完整直径。模型不做 DPI 换算、viewport 变换或画布裁剪。
- Stroke 只保存有序点和 style，不保存 time、压力、速度、倾角、prediction 来源、blend、AA、shader、UUID 或历史状态。首版固定单层；Laser 不持久化。
- Canvas 没有固定宽高，viewport 默认 `{0,0,1}`；Page 用 16-byte canonical GUID，并按 opaque `DeviceKey` 拥有 Canvas；Collection 的 Page vector 位置就是 pageIndex。
- `DrawingController` 绘制线程独占 Collection，不加锁。运行开始创建 Workspace、第一页和默认 Device Canvas；current page index 是运行时状态。
- Pen 复制稳定前缀并用完成态真实 taper 尾段替换连接点；Highlighter/Eraser 保存完整真实中心线；无 modeled point 时保存 `inputStartPoint`。Cancelled 和 Laser 都不 Append。
- 首次提交顺序固定为 `FinalizeStoredStroke -> AppendStroke -> AppendRenderItem -> DrawStoredStroke(appended stroke) -> capture preimage -> per-Stroke L2 resolve`。任何可见 L2 像素必须先有对应 RenderItem；同帧多个完成项按 Canvas 追加顺序逐条执行，不能共享 coverage union。
- 数字键 `0/5/8` 发布 FIFO Canvas command 并忽略自动重复；请求等待全部 active/reconnect contact 完成。`0` 优先切换已有下一页，仅在末页追加空白页；`8` 返回上一页；`5` 撤回当前页最后可见项。只有 GUID、Page 和默认 Canvas 均成功后才切换到新页；启动 `DrawingController::ClearCanvas()` 只初始化透明表面。

### 4. Validation & Error Matrix

| Input/state | Required behavior |
|---|---|
| Confirmed real sample | 原值写入 `x/y`，半径转换为 `width=2r` |
| Unconfirmed prediction / previous L0 snapshot | 省略，不得静默写入 |
| Down 后立即 Up | 以 `inputStartPoint` 保存单点 Pen/Highlighter/Eraser |
| Cancelled / Laser | 不追加 Stroke |
| 非有限坐标、宽度或 opacity / 负 width | `InkStroke::IsValid == false`，Append 不改变 Canvas |
| 零值/重复 Page GUID | AppendPage 失败，Collection 顺序不变 |
| New-page GUID/Page/default Canvas 失败 | current page 和 GPU 画面保持不变 |
| `0` 且已有下一页 | 切换已有页，不追加 Page |
| `0` 且当前为末页 | 追加一个有序空白页并切换过去 |
| `8` 且当前为第一页 | no-op，Page 顺序和画面不变 |
| L2 draw/map failure after Append | CPU Stroke 保留为真值；不得回写 prediction 作为替代 |

### 5. Good / Base / Bad Cases

- Good：两支半透明 Highlighter 同帧抬起，按 Canvas 顺序分别 resolve，交叠效果与之后逐 Stroke 重放一致。
- Base：Pen 单点、屏外有限坐标和 Eraser 都保留原始 float32 数据；viewport 仍不参与显示。
- Bad：先从 ActiveStroke 画完，再另存一个不同的 Stroke；或把同帧多笔 MAX/MIN 合并后只 resolve 一次。

### 6. Tests Required

- 纯 CPU 覆盖 GUID/Page 顺序、Device Canvas 隔离、默认 viewport、非法值拒绝和 Stroke 顺序。
- 覆盖负/远端坐标与 float32 `x/y/width` 原值不被裁剪或换算。
- 覆盖 Pen taper、稳定/尾段连接点去重、prediction/time 排除；Highlighter/Eraser/单点正确生成。
- 静态或集成验证 Cancelled/Laser exclusion、Append 先于 Draw、首次 Draw 使用刚追加对象、同帧逐 Stroke resolve。
- ARM64 Debug/Release 完整解决方案构建并运行测试；允许运行可见窗口时再人工验证基础绘制、prediction、抬笔、活动时页面命令和 resize。

### 7. Wrong vs Correct

Wrong：`L2 已经可见，所以从 realPoints/predictedPoints 另拼一次保存记录，并把同帧多笔一起 resolve。`

Correct：`只用确认真实点生成最终 Stroke；先 Append，再从该对象逐笔 resolve，L2 只是当前页缓存。`

## Scenario: Runtime Undo Cache And Page Restore

### 1. Scope / Trigger

修改 RenderItem visibility、热前像、合成范围树、history shader pass、`0/5/8` 命令、页面切换或 resize 后 L2 恢复时，必须应用本契约。

### 2. Signatures

- `CanvasRuntimeHistory::AppendStroke / LastVisibleItem / UndoLastVisible`
- `UndoCachePolicy { byteBudget=64 MiB, maxEntries=20 }`
- `CompositionCachePolicy { byteBudget=192 MiB }`
- `InkHistoryGpuCache::CapturePreimage / RestorePreimage / RestoreComposition`
- `CompositionRestoreRequest { canvas, rasterKey, documentCanvas, history, tiles, rangeEnd, canvasWidth, canvasHeight, clearTargetTiles, excludedItem }`
- `CanvasCommandType { Undo, NextPage, PreviousPage }`

### 3. Contracts

- Stored Stroke 不保存 visibility 或缓存；每个 Page/Device Canvas 使用绘制线程独占的 `CanvasRuntimeHistory` sidecar。撤回只隐藏最后可见 RenderItem，不删除 Stroke、不提供 redo；撤回后新笔继续追加，previous-visible 链必须 O(1) 找到尾部。
- 热前像使用 `128x128 BGRA8` screen-local block。Canvas `128x128` undo tile 只确定受影响屏幕范围；小数 viewport 下一个 Canvas tile 可覆盖 129 个屏幕像素，必须拆成相邻 screen block，不能直接写入单个 slice。默认 `64 MiB / 20 entries` 对应 1024 槽；顺序固定为 `Raster L1 -> Capture unchanged L2 -> Resolve L2 -> Commit ticket`。Capture/restore 要求 page、item、raster state、viewport float 值和窗口尺寸完全一致；viewport 只需有限，不要求整数。Copy 只在绘制线程提交，不 Map/readback/wait。
- 冷路径使用 32 RenderItem 的叶 Block 和 `256x256` operator tile；每槽为 `BGRA8 Add + R16F Retain = 384 KiB`，默认 `192 MiB = 512 slots`。组合固定为 `Later(Earlier(Below))`；CPU topology/generation 永久保留，GPU 节点只作 LRU 可淘汰缓存。
- 撤回路径依次为 `hot_preimage -> composition_cache/composition_rebuild -> ordered_tile_replay`。冷撤回只处理被撤项的 composition tiles，候选画面成功后才提交 visibility；失败时恢复原可见范围。缓存预算为 0 或资源失败只能降低性能，不能删除 CPU history。
- GPU history pass 的 array SRV/RTV 必须各自限制为单 slice；所有公开 composition 操作的全部出口解绑 `t0..t13`、`b2` 和 RTV，并恢复全画布 viewport/raster state。没有 `ID3D11DeviceContext1` 时仍可用 transparent scratch copy 清理 L2 tile。
- `0/5/8` 在窗口线程只入 FIFO 并发布 control wake；绘制线程仅在 active/reconnect contact 全部结束后消费。页面不保留独立全尺寸 L2；切换和 resize 都从当前可见 RenderItem 恢复。
- Undo 控制台输出 page、item、实际 path、`hot_remaining` 和可选 `history_end=true`；页面输出 key、action、current/count 和 restore path。

### 4. Validation & Error Matrix

| Condition | Required behavior |
|---|---|
| 热前像 key/state/尺寸匹配 | tile 原样复制回 L2，随后隐藏 RenderItem |
| 前像超预算、淘汰或 generation 不匹配 | 不改变 visibility，转入 composition/ordered replay |
| composition 节点缺失 | 按需重建；资源不足时逐 tile ordered replay |
| 候选冷恢复失败 | visibility 保持原值，并尝试恢复撤回前 tile |
| Cache policy 降低 | 先淘汰最旧热项/LRU 节点；提高预算不恢复已淘汰内容 |
| Resize | 丢弃不兼容热前像、更新 raster generation；保留 Canvas-local footprint 和 tile 索引，并从 CPU history 恢复当前页 |
| 首页按 `8` / 空页按 `5` | 明确 no-op，不改变 Page、history 或 L2 |
| 极端但有限 Stored 坐标 | 文档原值不变；sidecar 无法精确量化时使用保守可见 footprint，不得使有效 Stroke 无 RenderItem |

### 5. Good / Base / Bad Cases

- Good：一条小笔只捕获邻近 128 tile；连续 `5` 先命中热前像，超出热深度后只重建受影响的 256 tile。
- Base：composition budget 为 0 时仍可按当前可见顺序逐 tile 重放；空白页切换只清空并呈现透明 L2。
- Bad：撤回任意一笔都全画布重放，或先隐藏 CPU item 再尝试可能失败的 GPU 恢复。

### 6. Tests Required

- CPU 测试断言 4K 为 510 个 128 tile、默认 1024/20 热预算、512 composition 槽、FIFO/LRU/pin 和 0 禁用。
- 覆盖 Pen/Highlighter/Eraser、单点、负坐标、屏外/极端有限坐标、AA padding 和跨 4K 稀疏对角线 footprint；小数正负 viewport 的 screen block 每边不得超过 128，且边缘 partial block 必须落在窗口内。
- 覆盖稳定 RenderItem 顺序、连续 O(1) 尾撤回、隐藏分支后 append、32 项 Block、范围分解、visibility identity、旧 tile membership 清理和局部 generation 失效。
- 静态核对首次提交顺序、逐笔 capture/resolve、无 readback、单 slice SRV、所有 pass 解绑、事务式 cold undo、FIFO Canvas command 和控制台字段。
- Debug/Release ARM64 完整解决方案 Rebuild并运行两套控制台测试；可见窗口和 D3D Debug Layer 未执行时必须明确标记未验证，不能用静态检查替代。

### 7. Wrong vs Correct

Wrong：`Undo -> visible=false -> 尝试全局重绘；失败后留下空白或半恢复画面。`

Correct：`选择最后可见项 -> 热前像命中则复制；否则只在受影响 tile 构建排除该项的候选画面 -> 成功后提交 visibility -> 失败则恢复原范围。`

## Highlighter Geometry

- 画刷固定为画布 Y 轴方向的 `6.25×50px` 矩形，指针位于矩形中心；路径、笔倾角和设备方向都不旋转画刷。
- 连续点距离不超过 `0.25px` 时去重；累计位移超过阈值后再生成新 sweep。
- 单点生成一个居中矩形；每对相邻点生成该矩形沿中心线平移的凸扫掠区域。
- shader 以 X/Y 轴向边界和线段法线边界的半平面交集计算 sweep；零长度退化为矩形 SDF。
- 相邻 sweep 通过 Add/MAX、Retain/MIN coverage union 连接，不生成 round join、cap 或 short mark primitive。
- CPU bounds 与 GPU AABB 都使用 `halfSize=(1.25,25)`，并分别保留现有 `3px` dirty padding 与 `2px` shader quad padding。
- Down 起允许 L0 点击矩形和 prediction；`12px` 不再是可见性、完成态或几何分支。
- L1 只增量缓存活动态稳定 sweep；Up 从最终 Stored Stroke 的真实中心线重建完整 sweep，单点生成一次点击矩形。

上述尺寸和去重值仍属于实验参数。修改任一值时必须同时验证 CPU bounds、HLSL coverage、L0/L1 切片、缓存完成态和单点行为。

## Dirty Rect Contract

- 每个 rect 在资源操作前裁剪到当前画布。
- L0 更新的 dirty rect 是上一帧 L0 与当前 L0 的并集，确保旧预测被清除。
- 第一帧、窗口重新暴露或呈现失败后的恢复使用整画布。
- stroke dirty 同时包含稳定提交和最终 live 几何。
- resize 只保留新旧画布左上角交集，不缩放历史墨迹。

## Transparent Presentation Contract

- 原生绘图窗口不带 `WS_VISIBLE` 创建；完成 presenter 初始化和首个透明画布提交后才显示。窗口类不得配置会在初始化期间暴露实色背景的 GDI 画刷。
- DirectComposition swapchain、visual tree 和 `Commit` 全部成功，不代表驱动一定按 premultiplied alpha 合成；透明正确性必须通过真实桌面背景验证。
- 所有适配器统一按 DirectComposition、DWM extended frame、ULW 的顺序尝试；厂商、架构或 OS 标签本身不能作为提前降级依据。
- 若要增加设备或驱动专用兼容路径，必须记录 OS、VendorId、DeviceId、SubSysId、Revision、UMD driver version 和真实桌面背景视觉结果；首选模式失败时仍按既有清理和回退协议继续。
- ULW 的 `1/255` alpha 只存在于 CPU 输出副本以维持窗口命中，不能写回 L0/L1/L2 或改变墨迹 alpha。
