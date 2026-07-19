# Runtime and Rendering

## Ownership And Flow

`main.cpp` 的初始化顺序是：

1. 创建 `ContactInputCoordinator`
2. `WindowController::Initialize`，随后把 coordinator 绑定到窗口控制唤醒
3. `InitializeGraphicsDevice`
4. `TransparentPresentationController::Initialize`，内部初始化 `InkRenderer`
5. `RealTimeStylusInput::Initialize` 启用同步 RTS 插件
6. `DrawingController` 创建并进入消息循环

不要交换窗口、设备、交换链和 renderer 的依赖顺序。`TransparentPresentationController` 保存对 `GraphicsDeviceResources` 和 `InkRenderer` 的非拥有指针，调用者必须让这些对象覆盖 presenter 生命周期。

退出时先停止 RTS 并解除 `WindowController` 的 coordinator 指针，再销毁 coordinator。RTS 的 COM 对象只由完成 MTA 初始化的主线程创建、禁用、移除插件并释放。

## Input And Thread Boundary

`WindowController::HandleWindowMessage` 将 resize、清屏、全量呈现、DWM 变化和退出写入原子状态。主循环通过 `Consume*` 方法消费请求。

- 窗口回调不重建 D3D 资源。
- `pendingResizeWidth_/Height_` 先写入，`resizeRequested_` 最后 release 发布。
- `DrawingController::ProcessPendingResize` 在绘制线程依次 resize renderer、resize presenter，两个步骤都成功后才 `CommitSize`。
- RTS 同步回调只完成 packet 解析、contact 状态发布和唤醒，不调用 D3D、presenter 或 stroke modeler。
- 控制请求先写 sticky 原子标记，再通过 coordinator 队列唤醒；消费方在阻塞前二次 dequeue，避免清 pending 与入队交错造成丢唤醒。
- 无活动 contact 时使用 blocking dequeue；活动 contact 仍按帧更新停笔预测。
- Down、Up/Cancelled 和控制请求递增 wake generation 并触发 Win7 可用的 event；Move 只更新合并快照，不把 240Hz packet 变成无界帧驱动。
- 绘制线程使用 `THREAD_PRIORITY_ABOVE_NORMAL`，活动末段按 QPC deadline 核对 wake generation；完全空闲仍阻塞在队列 semaphore，不自旋。

## Tool State

单笔兼容路径在 `WM_LBUTTONDOWN` 时复制工具。多 contact 路径只在空闲批次的首个 Down 读取 1/2/3 选择，并把同一工具锁定到该批所有 contact；活动期间的选择只影响下一批。

当前行为：

| Tool | Width | Prediction | Width mode | Live layer |
|---|---:|---|---|---|
| Pen | 5px | active configured mode | simulated pressure | real tail + prediction + taper |
| Highlighter | 50px | enabled after real path reaches 12px | fixed | flat primitives, no taper |
| Eraser | 50px | disabled | fixed | real points directly committed to L1 |

这是当前实验实现。预测时长、目标帧率、笔宽、live-tip 和几何阈值默认都是实验参数；只有公开接口、持久化格式或明确兼容要求已经依赖某值时，该值才升级为兼容契约。

当前源码描述现有行为，阶段说明描述历史设计或计划。两者出现数值或工具语义差异时，应并列记录，不得自动用源码覆盖历史目标，也不得自动让实现恢复为阶段说明。

## Stroke Modeling Invariants

- 输入时间使用单调递增的 logical time，来源是 wall-clock delta。
- 小于 `0.25px` 的原始移动视为抖动。
- 普通笔宽使用相邻有效原始鼠标点的速度，不使用 modeled/predicted velocity。
- RTS 路径先按真实 snapshot 的距离/QPC 计算速度并做一次低通，再交给笔宽估算器；同一批 modeled output 不得被当成多份新的速度采样。
- 第一份有效速度只能从基准宽度渐进追随，禁止回写并瞬间改变已经可见的起笔点。当前直径时间变化上限为每秒 `3 × baseDiameter`，相邻点半径变化上限为 `0.35 × distance`。
- 预测点通过 `StrokeWidthEstimator` 副本继承最后真实宽度。
- 半径变化同时受时间和距离限制；L0 taper 后再次调用 `LimitRadiusTransitions`。
- 视觉连续三帧稳定后可冻结停笔更新，移动时解除冻结。

依据：`StrokeWidthEstimator::Append`、`UpdateRawPositionAndDetectMovement`、`UpdateIdleFreezeState`、`RebuildPredictedPoints`。

## Three-Layer Contract

- `L2`：已完成笔画的最终 premultiplied RGBA 画布，背景真透明。
- `L1`：共享临时操作层，合并全部活动 contact 已稳定的前缀操作。
- `L0`：共享临时操作层，合并全部活动 contact 当前帧仍会变化的真实尾部、预测与笔锋；每帧只恢复一次单位操作再重画。

临时操作层表示：

```text
Result = Add + Retain * Below
```

`Add` 使用 BGRA8，`Retain` 使用 R16F；新层的单位操作是 `Add=(0,0,0,0)`、`Retain=1`。

绘制过程中，保护窗口之外的真实前缀推进到 L1。普通笔保护时间为：

```text
liveTipDuration + predictionDuration
```

contact 结束时保持抬起前最后一帧的视觉结果：重建此前已经进入 L1 的稳定前缀，再把 `previousL0DrawPoints` 中的真实尾部、prediction 和笔锋原样合入 L1。Up 只结束输入和生命周期；其 `kUp` 平滑结果不得再次连接或替换最后可见 L0，否则会形成折返或双束。只有 Down 后立即 Up、尚未生成过可见 L0 时，才使用最终真实点/初始点兜底生成点击或短段。同一帧的全部结束 contact 只执行一次 L2 resolve、一次 backbuffer composite 和一次 present。仍活动 contact 的 L1/L0 必须在清空临时纹理后从 CPU 状态重建。

## Scenario: RTS Multi-Contact Input And Rendering

### 1. Scope / Trigger

修改 RTS packet、contact 路由、跨线程队列、活动笔画、resize/clear、临时层合成或呈现时，必须应用本契约。

### 2. Signatures

- `ContactInputCoordinator::PublishDown/PublishMove/PublishUp/PublishCancelled`
- `ContactInputCoordinator::TryDequeue(ContactRecord*&)/WaitDequeue(ContactRecord*&)`
- `ContactInputCoordinator::TryReadSnapshot/Recycle/PublishControlWake`
- `ContactInputCoordinator::CaptureWakeGeneration/WaitForWake`
- `ContactInputCoordinator::DiagnosticsSnapshot`
- `RealTimeStylusInput::Initialize/Shutdown`
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
- 每个活动 contact 拥有独立 CPU runtime，其 GPU 几何共同重建到共享 L1/L0；不得提前进入 L2。完成 contact 的同帧批次只 resolve/composite/present 一次，随后重建仍活动 contact。
- resize 成功后重建活动临时层；clear 在有活动 contact 时延后。无活动 contact 时阻塞等待；1ms timer period 只在活动区间启用，且每次成功 begin 必须配对 end。
- waitable swapchain resize 必须先 `GetDesc1`，并原样传回 `BufferCount/Format/Flags`；部分驱动在传入零值时第一次 resize 成功、恢复尺寸时失败。
- 运行指标关闭时不创建会话、不启用输入计数、不写文件；开启后原始样本写入忽略的 `TestResults/`，仓库只保存环境、阈值和分位数摘要。

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
| Resize succeeds | 保留 L2 交集并从 CPU runtime 重建全部活动 L1/L0 |
| waitable swapchain second resize | 保留原 swapchain 描述字段；不得用 `ResizeBuffers(..., UNKNOWN, 0)` 丢弃 flags |
| Clear with active contact | 延后到活动集合为空 |
| Multiple Up in one frame | 一次 L2 resolve、一次 composite、一次 present |
| Up arrives after a visible prediction | 稳定前缀加最后可见 L0 原样烘干；不使用 `kUp` 平滑结果重连尾部 |
| Present failure | 保持整画布重呈现请求，下一帧恢复 |

### 5. Good / Base / Bad Cases

- Good：32 个同步生产者使用显式 token 并发 Down，slot/pointer 唯一；两支笔交错移动并同帧抬起，终态完整且批量提交。
- Base：单 contact 的 Down/Move/Up 使用相同 generation、seqlock、pointer ingress 和批量渲染路径；慢速到快速过渡时宽度连续。
- Bad：容量耗尽后调用隐式 `enqueue` 扩容，或只设置 `IRealTimeStylus3::MultiTouchEnabled` 却不处理 HWND opt-in。

### 6. Tests Required

- `Debug|ARM64`、`Release|ARM64` 全解决方案 Rebuild，且两个 shader、C++ Modules、资源嵌入与最终链接成功。
- `Release|x64`、解决方案 `Release|x86`（项目映射 Win32）Rebuild 并运行测试，验证 8/4 字节指针 payload 和 lock-free 静态断言。
- 自动并发覆盖 32 个生产者、32/64/多 block 容量、耗尽/复用、无分配 Down、Move/Up 竞争、stale generation、重复回收、Cancelled/shutdown 和 ControlWake/Down/终态唤醒。
- 静态检查窗口属性、`WM_TABLET_QUERYSYSTEMGESTURESTATUS` 与 `IRealTimeStylus3` 三处多点 opt-in 同时存在。
- 静态验证 generation/state CAS、sticky terminal、seqlock、零自旋阻塞等待、timer begin/end 配对和 Release 无逐帧日志。
- 真机验证鼠标宽度连续、Pen/Touch 单 contact、双 Touch 交错、同时抬起、活动时 resize、活动时 clear、设备禁用/拔出、长时间 idle CPU 和最终点位置。普通 `SendInput` 不能替代 RTS 硬件验证。
- Release 自动基准至少连续三轮：即时工具 Down→Present p99 ≤ 8.33ms，活动帧间隔 p99 ≤ 9.5ms，>16.67ms 比例 <1%，连续空闲至少 4.9 秒且 frame/Present 零增长。
- 快速曲线末端抬笔时，上一帧预测端点仍保留，且最终真实点到预测之间不出现回头、闭环或重复连接。

### 7. Wrong vs Correct

Wrong：`收到 Up 就立即把该 contact resolve 到 L2 并 Present；槽位只靠 contactId 复用。`

Correct：`route 用 generation+state 精确交接；同帧全部 terminal contact 先完成几何，再统一 resolve/composite/present，并重建剩余活动层。`

Wrong：`Up 后用新的真实尾部重建 L0，再把上一帧留下的 predictedPoints 直接追加在最终 Up 点后面。`

Correct：`重建已提交稳定前缀，并把 previousL0DrawPoints 原样烘干；只有从未出现可见 L0 时才用最终真实点兜底。`

Wrong：`put_MultiTouchEnabled(TRUE) 成功，所以窗口已经能收到多指。`

Correct：`HWND 属性、WM_TABLET_QUERYSYSTEMGESTURESTATUS 返回值和 IRealTimeStylus3 三处同时 opt-in，再用实体 Touch/Pen 验证 callback。`

Wrong：`IngressCommand{ kind, record } 入队；token try_enqueue 失败后调用普通 enqueue。`

Correct：`非空 ContactRecord* 只表示 Down，nullptr 只表示 ControlWake；全部 producer 在初始化期建 token，热路径只调用 token try_enqueue。`

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

当任务新增 `InkStrokeRecord`、保存、导出、同步、回放或从 L2/活动 contact runtime 提取永久笔迹时，必须应用本契约。

### 2. Signatures

当前仓库没有正式持久化 API 或记录结构。未来任务必须先定义明确的写入签名和数据结构；禁止把活动 contact 的 prediction 或 L2 像素读取隐式当成持久化接口。

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
- 全局 Start 且方向未锁定时不生成活动几何；首次可见时用首段 12px 真实路径的锁定弦方向，后续 Move、L1 切片和完成态不得改变起始平帽。
- 最终真实路径不足 `12px` 时生成从按下点出发的确定性 `12×50px` short mark；预测不参与短划分类。
- short mark 只在 Up 时生成；Cancelled/shutdown 不生成最终短划或完成几何。

上述值在当前实现中共同构成一套几何约束，但仍属于实验参数。修改一个值前必须搜索全部消费者并验证相关场景；这不表示当前数值已经成为永久产品标准。

> **已解决（2026-07-19）**：起笔方向锁定前的活动几何保持不可见；不足 `12px`、刚达到 `12px`、长曲线、L1 切片、双向 90° 转角和近 180° 回折均有回归。真实窗口检查中平帽四角完整；用户确认旧“缺角”来自橡皮擦除后的缺口，不是当前荧光笔端帽缺失。

## Dirty Rect Contract

- 每个 rect 在资源操作前裁剪到当前画布。
- L0 更新的 dirty rect 是上一帧 L0 与当前 L0 的并集，确保旧预测被清除。
- 第一帧、窗口重新暴露或呈现失败后的恢复使用整画布。
- stroke dirty 同时包含稳定提交和最终 live 几何。
- resize 只保留新旧画布左上角交集，不缩放历史墨迹。

## Transparent Presentation Contract

- DirectComposition swapchain、visual tree 和 `Commit` 全部成功，不代表驱动一定按 premultiplied alpha 合成；透明正确性必须通过真实桌面背景验证。
- 默认优先 DirectComposition；当前 QCOM ARM64 适配器优先使用 `UlwDirtyRect`，因为实体设备已观察到 DComp 透明像素显示为黑色。
- 适配器专用回退只改变尝试顺序，不移除后续模式；首选模式失败时仍按既有清理和回退协议继续。
- ULW 的 `1/255` alpha 只存在于 CPU 输出副本以维持窗口命中，不能写回 L0/L1/L2 或改变墨迹 alpha。
