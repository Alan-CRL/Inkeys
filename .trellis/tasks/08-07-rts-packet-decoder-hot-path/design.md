# RTS packet decoder 热路径设计

## Boundary

```text
RTS lifecycle callback
    -> reset/invalidate old generation when required
    -> BuildContextDecoder (slow-path COM/property parse)
    -> publish fixed immutable decoder slots

StylusDown
    -> bounded tcid -> decoder lookup
    -> bind (tcid, cid) -> decoder slot + generation
    -> DecodeSnapshot
    -> PublishDown

Packets
    -> active binding lookup
    -> validate decoder slot + generation
    -> fixed-index DecodeSnapshot
    -> PublishMove
    -> bounded scalar diagnostics

StylusUp
    -> lookup binding
    -> decode only when generation is valid
    -> PublishUp or existing safe terminal close
    -> always release binding

InAirPackets
    -> bounded tcid -> decoder lookup
    -> DecodeSnapshot
```

本任务主要只改 `draw3/realtime_stylus.cpp`、`draw3/realtime_stylus.cppm` 和必要的纯 RTS 测试。decoder/binding 完全属于 RTS producer 内部，不进入 `ContactInputCoordinator` 或 `ContactSnapshot`。

## Production DataInterest

`UpdateMapping` 必须先被精确订阅才能成为正常 lifecycle notification。production mask 只做这一项增量：

```cpp
kProductionRtsDataInterest =
    existingFlags |
    RTSDI_UpdateMapping;
```

`existingFlags` 继续精确保留 `RTSDI_RealTimeStylusEnabled`、`RTSDI_RealTimeStylusDisabled`、`RTSDI_StylusInRange`、`RTSDI_StylusOutOfRange`、`RTSDI_InAirPackets`、`RTSDI_StylusDown`、`RTSDI_Packets`、`RTSDI_StylusUp`、`RTSDI_TabletAdded`、`RTSDI_TabletRemoved` 和 `RTSDI_Error`。不使用 `RTSDI_AllData`。

这是本任务唯一允许的 production RTS configuration 行为变化。desired packet properties、tablet flags、`SetAllTabletsMode`、MultiTouch 和既有 Mouse/Pen/Touch routing 全部保持不变。

## Immutable Context Decoder

将当前 `TabletMetadata` 重构为固定槽中的 `RtsContextDecoder`。字段只包含：

- `tabletContextId`、`propertyCount`、X/Y 及 optional properties 的固定 index；
- pressure、tilt、azimuth、altitude、width、height 所需 metrics/scalars；
- `positionScaleX/Y`、当前 context 的 `contactScaleX/Y`；
- `InputDeviceType`、slot generation 和 valid state。

`BuildContextDecoder` 先在局部 candidate 中完成 `GetPacketDescriptionData`、GUID/index 解析、metrics 复制与 device-kind 查询，离开 helper 前始终 `CoTaskMemFree`。只有完整 candidate 才能发布到 slot；已经发布的 payload 不原地修改。

decoder cache 继续采用简单固定容量（现有 32 context slots）。Down、InAir 和 lifecycle resolver 可按 tcid 做最多几十槽的 bounded linear lookup；这不是高频 `Packets` 的 metadata scan，无需为了低频 resolver 引入第二套复杂索引。

## Shared Position Scale Compatibility

decoder 预热与 shared position scale 使用不同来源，避免把未证明的 context 顺序当成契约：

```text
decoder prewarm contexts
    = RealTimeStylusEnabled supplied context IDs

shared position scale
    = GetAllTabletContextIds()[0]
      -> GetPacketDescriptionData(firstContext)
      -> existing scale calculation
```

因此 Enabled 慢路径默认保留一次 `GetAllTabletContextIds()`。所有 decoder 都复制同一 shared position scale，contact size 仍复制各自 context packet scale。只有 API 契约或项目验证严格证明 `pTcids[0]` 与旧枚举首项顺序等价后，才允许删除这次枚举。

`UpdateMapping`、`TabletRemoved` 和全量恢复也从最新 `GetAllTabletContextIds()` 首项重建 shared scale。正常增量 `TabletAdded` 沿用当前 enabled lifecycle 的 shared scale并只发布新增 decoder，保持既有 contexts 不变。

## Lifecycle State And Helpers

使用一个共享 reset 入口及小型组成 helper，避免 Enabled/Disabled/TabletRemoved/UpdateMapping 漂移：

```text
ClearActiveBindings()
InvalidateAllDecoders()
ResetSharedPositionScale()
ResetDecoderLifecycleState(qpc, closeProducerContacts)
RebuildCurrentContextDecoders(source)
```

`ResetDecoderLifecycleState` 的安全顺序是：停止解析旧 binding，使所有 active binding 无效；通过现有 coordinator 终态策略取消/关闭 producer contacts；invalidate decoder slots 并推进/终止其 generation；清空 context-ID-derived cache 和 shared scale。lifecycle reset 同时把 binding 表全部恢复为 `EMPTY`。现有 interruption simulation 和 cursor reset 语义保持不变。

generation 可以由 lifecycle epoch 加 slot generation 表达，也可以只用不会误复用的 slot generation；硬约束是 slot reuse 必须改变 generation，Disabled 后 generation A 的 binding 永远不能在下一次 Enabled 中匹配。

全量 rebuild 使用固定 staging state：先取得完整 context 列表和 shared scale，再构建 candidate decoders，最后发布单一新 lifecycle generation。失败时不回退到旧 mapping，也不发布新旧混合状态；缺失 decoder 的输入安全拒绝并记录 bounded diagnostics。

## Lifecycle Flows

### RealTimeStylusEnabled

```text
discard stale lifecycle state / close stale producer contacts
    -> establish enabled generation B
    -> GetAllTabletContextIds once for current first-context scale
    -> build candidates for callback-supplied context IDs
    -> publish generation B decoders
```

Enabled 每次都从空状态开始，不依赖上一次 Disabled 是否完整执行。

### RealTimeStylusDisabled

```text
invalidate bindings
    -> cancel/close producer contacts
    -> invalidate decoder slots/generations
    -> reset shared scale and context-derived cache
    -> terminate enabled lifecycle
```

Disabled 不保留可供下一次 Enabled 复用的 tcid、binding 或 decoder。

### UpdateMapping

```text
invalidate bindings and cancel/close active contacts
    -> invalidate decoder generation and shared scale
    -> GetAllTabletContextIds
    -> stage new shared scale and current context decoders
    -> publish one new generation
```

一个 contact 不会跨 mapping generation 继续；显示 mapping 改变时取消当前 stroke 是明确的安全策略。`RTSDI_UpdateMapping` 在静态 production mask 中订阅；该 callback 本身不调用 `SetDesiredPacketDescription`、`SetAllTabletsMode`，也不改变 DataInterest 或 tablet flags。

### TabletRemoved

回调只有 tablet index，因此不猜 removed tcid。它执行与 mapping rebuild 相同的全量 invalidation/cancel，然后枚举当前 contexts，按最新 first-context scale 立即重建剩余设备 decoder。移除 B 后，A/C 进入新 generation，B 不存在。

### TabletAdded

正常路径调用 `GetTabletContextIdFromTablet()`，在局部构建 B candidate，成功后只占用/替换 B 的 decoder slot；A/C 的 slots 和 generations 保持不变。

若 context-ID lookup 或增量构建无法形成完整 candidate，则不发布 B 的部分状态，转为全量 reset + `GetAllTabletContextIds()` staging rebuild。fallback 会安全取消活动 contacts，保证最终只有一个一致 generation。

## Active Binding Capacity

decoder slots 表示 tablet contexts；active binding slots 表示同时处于 Down -> Packets -> Up 生命周期的 RTS contacts，两者不能共用容量。

active binding 使用最大 4096 槽固定 backing array，运行时 logical capacity 在初始化慢路径按 Coordinator 现有策略计算：

```text
maximumTouches = max(0, GetSystemMetrics(SM_MAXIMUMTOUCHES))
requested = 2 * (maximumTouches + 2)
logicalCapacity = clamp(round_up_to_32(requested), 32, 4096)
```

这保留 Coordinator 的并发余量和异常系统值上界，同时确保 binding path 不分配。容量耗尽时 Down 在 `PublishDown` 前安全失败，既有 bindings 不变，只记录 bounded failure。

logical capacity 只保证是 32 的倍数，不保证是 power of two。bucket 必须使用 `hash % logicalCapacity`，probe wrap 使用等价的显式回绕；禁止 `hash & (logicalCapacity - 1)`。测试必须覆盖 96、160、224 等非 power-of-two fixtures，容量语义不得为 hash 实现偷偷改动。

## Active Binding Invariants

采用固定 hash + bounded linear probing，每槽只有 `EMPTY`、`OCCUPIED` 两态，并保存 key、decoder slot index、decoder generation。设计不使用永久 tombstone：

- lookup 从 `hash % logicalCapacity` 起点探测；遇 `OCCUPIED` 比较完整 key，遇 `EMPTY` 即可判定不存在；最多探测 logical capacity。
- insert 从同一 bucket 探测到首个 `EMPTY`，在到达它之前检查 duplicate key；无 `EMPTY` 才是真实容量耗尽。
- erase 使用 delete + reinsert following cluster：目标先变为 `EMPTY`，随后按 probe 顺序取出连续 `OCCUPIED` entries、清空原槽，并以各自 hash 重新插入，直到遇到原 cluster 的 `EMPTY`。repair 支持数组尾部回绕，最多处理 logical capacity。
- cluster repair 只发生在 StylusUp、duplicate Down cleanup、stale binding cleanup 等低频删除路径，允许 bounded O(capacity) work；`Packets()` 永不触发 repair、rebuild 或 compaction。
- duplicate `(tcid, cid)` Down 使用已有 binding 对 coordinator 走现有安全 cancel/terminal fallback，cluster-repair 删除旧 binding，再把当前 Down 绑定到唯一 slot；不允许两个 OCCUPIED slot 持有同 key。
- Packets 命中后先校验 decoder index、valid 和 generation；mismatch 视为 stale binding，绝不 dereference decoder，只丢弃该包并记录 bounded failure。stale entry 留给后续 Up、duplicate Down 或 lifecycle reset 的低频 erase，`Packets()` 不执行 cluster repair。
- Up 仅在校验通过时解码；缺失或 mismatch 时直接调用现有安全终态关闭/取消路径。无论 decode/publish 成功与否，callback 返回前都通过 cluster-repair erase 释放 binding。
- `activeBindingCount` 随外部 insert/erase 精确增减；内部 cluster reinsert 不改变 count。count 为 0 时，repair invariant 已保证所有槽为 `EMPTY`，无需在每个单笔 Up 后再扫描 4096 槽；lifecycle reset 仍显式调用 `ClearActiveBindings()`。
- Down 建立 binding 后才 `PublishDown`；若发布失败立即执行同一 erase。

该删除方式不会留下 tombstone，因此 lookup cost 只由当前 active bindings 的 cluster 决定，不随历史 Down/Up 次数永久增长。

## Pure Decode Contract

`DecodeSnapshot` 输入为 `const RtsContextDecoder&`、property count、packet、phase/QPC，输出 `ContactSnapshot`。它只执行 property count/index 校验、位置/压力/角度/接触面积数学变换和 snapshot 写入；不访问 cache、binding、lifecycle state、COM、mutex、coordinator 或 window sink。

Pen inversion、MouseLeft/Right routing、cursor sink 和 interrupted-stroke simulation 仍留在 callback wrapper，保持现有行为。

## Thread Ownership And Hot Path

decoder 构建、binding 修改和 lifecycle reset 只属于同步 RTS plugin callback；窗口/绘制线程和 diagnostics flush 不读取这些表。实现阶段必须展开 callback ownership：若可证明 callback 串行，删除只服务旧 metadata/pixel-scale publication 的 mutex/atomics；若证据不足，保留 lifecycle/build 慢路径的安全 publication，同样不得让 `Packets` 获取 mutex 或看到原地变更的 decoder。

`Packets` 唯一允许的工作是 bounded binding probe、slot/generation 校验、pure decode、既有 publish 和 bounded scalar diagnostics；没有 COM、fallback rebuild、tcid decoder scan、格式化或分配。

## Compatibility And Rollback

除精确新增 `RTSDI_UpdateMapping` 订阅外，不改变其它 production DataInterest flags、desired packet properties、tablet flags、Mouse routing、normalization、inverted pen、contact size、Coordinator、HUD、DrawingController、Renderer、StrokeModeler 或 prediction。若 active binding 迁移验证失败，可回退该阶段并保留 immutable decoder cache；不能回退到 `Packets` COM/metadata scan，也不能牺牲 lifecycle generation safety。
