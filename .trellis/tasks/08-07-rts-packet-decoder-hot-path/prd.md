# RTS packet decoder 热路径优化

## Goal

把 Draw3 的 RTS 输入生产者重构为低开销、稳定的 packet decoder 路径：context 级 packet metadata 在生命周期慢路径一次编译为 immutable decoder，活动 contact 在 `StylusDown` 时绑定 decoder，后续 `Packets` 只做 binding 查找、固定索引解码和既有发布。

唯一目标是消除高频 in-contact `Packets()` 中的 metadata 扫描和同步依赖，同时保持当前输入数值、路由与 lifecycle 安全语义。

## Confirmed Current Facts

- `inkStrokeModelerTest/draw3/realtime_stylus.cpp` 当前使用固定 32 槽 `TabletMetadata`；`EnsureMetadata` 慢路径执行 `GetPacketDescriptionData`、GUID 解析和 tablet device-kind 查询，并释放 `PACKET_PROPERTY*`。
- `Packets` 每次仍通过 `FindMetadata(tcid)` 扫描 metadata 数组；`StylusDown` 尚未保存 contact 到 decoder 的绑定。
- `UpdateMapping` 当前不刷新任何 metadata 或 scale；`RealTimeStylusDisabled`、`TabletRemoved` 当前只关闭 producer contacts，没有清理或重建 context-ID-derived decoder 状态。
- 当前精确 `kProductionRtsDataInterest` mask 未包含 `RTSDI_UpdateMapping`，因此仅实现 callback 还不足以让 mapping 变化进入正常 notification path。
- 当前 position scale 来自 `GetAllTabletContextIds()[0]` 对应 packet description；contact width/height 使用各 context 自己的 packet scale。
- `ContactInputCoordinator` 使用 `RoundUpSlotCapacity(2 * (SM_MAXIMUMTOUCHES + 2))`，最小 32、最大 4096；其 contact pool、generation 和安全终态关闭语义是既有契约。
- `TABLET_CONTEXT_ID` 只在一次 RealTimeStylus enabled 生命周期内有效，不能跨 Disabled/Enabled 边界复用。

## Requirements

1. 引入 per-context immutable `RtsContextDecoder`（或等价名称），只保存 context ID、property count、固定索引、`PROPERTY_METRICS`、设备类型和解码所需 scalar scale；构造完成后 callback 只读。
2. `BuildContextDecoder` 只在 Enabled、tablet/mapping 生命周期和罕见恢复慢路径执行 COM/GUID 解析，并在返回前释放 RTS 分配的 `PACKET_PROPERTY*`。
3. `RealTimeStylusEnabled` 必须先丢弃全部旧 lifecycle state，建立新的 enabled generation，再以回调提供的 context IDs 预热 decoder。为严格保持当前 position-scale 数值语义，允许在 Enabled 慢路径调用一次 `GetAllTabletContextIds()`，仍以其首 context 建立 shared position scale。
4. `RealTimeStylusDisabled` 必须成为显式边界：清除 active bindings，invalidate decoder slots/generations，reset shared position scale 和全部 context-ID-derived cache，并沿用现有 producer contact cancel/close。
5. `kProductionRtsDataInterest` 必须在现有精确 flags 上唯一新增 `RTSDI_UpdateMapping`，使 DPI/orientation/display mapping 变化实际进入 decoder lifecycle；不得改为 `RTSDI_AllData`，现有必要 flags 必须全部保留。
6. `UpdateMapping` 必须取消/关闭活动 producer contacts、使 bindings 和旧 decoder generation 失效，调用 `GetAllTabletContextIds()` 取得当前 contexts，重新取得 shared position scale 并重建 decoder；callback 内不得重配 desired packet properties、tablet flags、`SetAllTabletsMode` 或其它 production configuration。
7. `TabletRemoved` 不猜测 removed tcid；它必须全量清理活动 bindings/contacts 和 decoder table，然后枚举当前仍存在的 contexts，重建 shared position scale 与剩余 decoder。
8. `TabletAdded` 正常路径使用 `GetTabletContextIdFromTablet()` 增量构建一个 decoder并保持既有 decoder 有效；该查询失败时执行 `GetAllTabletContextIds()` 驱动的安全全量重建，且不得发布混合 generation 或部分构建状态。
9. 增加固定 backing、运行时有界的 active contact binding，按 `(tabletContextId, contactId)` 保存 decoder slot index + generation。logical capacity 必须与 Coordinator 的现有公式一致：`round_up_32(2 * (SM_MAXIMUMTOUCHES + 2))`，最小 32、最大 4096；decoder context 容量和 active contact 容量是两个独立维度。
10. active binding 删除必须修复后续 probe cluster并恢复真正的 `EMPTY` slot，不得永久积累 tombstone；重复 Down、StylusUp 和 stale binding cleanup 可以承担 bounded O(capacity) 删除成本，但 `Packets()` 不得触发 rebuild/compaction。
11. binding hash bucket 必须使用适用于任意 logical capacity 的计算；容量只是 32 的倍数、不是 power-of-two 契约，禁止以 `hash & (capacity - 1)` 代替 `hash % logicalCapacity`。
12. binding 路径不得 heap allocation、扩容、`unordered_map` 或 mutex。容量耗尽时安全拒绝新的 Down，不破坏既有 bindings；重复 Down 必须先安全取消/关闭同 key 的旧 producer contact并释放旧 binding，再绑定当前 Down，绝不能保留两个相同 key。
13. `StylusDown` 在发布前 resolve decoder 并建立 binding；发布失败时回收 binding。`StylusUp` 只在 generation/validity 匹配时解码，并且无论解码或发布是否成功都释放 binding；generation mismatch 不得 dereference stale decoder，直接走现有安全终态关闭/取消路径。
14. 正常 `Packets()` 路径必须为 active binding -> stable decoder -> fixed-index `DecodeSnapshot` -> `PublishMove` -> bounded diagnostics，不得调用 `FindMetadata`、`GetAllTabletContextIds`、任何 COM、mutex、GUID scan、metadata linear scan、格式化、文件 I/O 或分配。
15. `InAirPackets` 不创建或使用 active contact binding；它按 tcid 对固定 decoder cache 做简单有界 lookup 后解码。Down/InAir/lifecycle resolver 可保留低频、有界 decoder lookup 和罕见 lifecycle recovery。
16. `DecodeSnapshot` 变成只依赖 const decoder、packet 和输出参数的纯解码函数；position scale 继续复制自 shared first-context scale，contact size 继续使用当前 context scale。
17. 保留现有 bounded `--rts-trace` 诊断语义；若线程 ownership 证据不足，可保留 lifecycle 慢路径同步，但 `Packets` 必须无锁。

## Acceptance Criteria

- 正常 `Packets()` 路径没有 `GetAllTabletContextIds`、COM、mutex、allocation、GUID scan、metadata linear scan、格式化输出或文件 I/O。
- production DataInterest 明确包含 `RTSDI_UpdateMapping`，同时保留原有精确 flags；DPI/orientation/display mapping 变化能够实际进入 `UpdateMapping` decoder lifecycle。
- 同一 raw packet 在 Pen、Touch、MouseLeft/Right 下产生与当前实现等价的 position、pressure、tilt、orientation、contact width/height、inverted/device routing；shared position scale 仍等价于当前 `GetAllTabletContextIds()[0]` 语义。
- 没有任何 `TABLET_CONTEXT_ID`、decoder 或 active binding 能跨越 RealTimeStylus Disabled/Enabled 边界；generation A 的 binding 在 Enabled generation B 中永远不能 resolve。
- `UpdateMapping` 完成后没有 decoder 继续使用旧 display mapping/scale，mapping A 中的活动 contact 已安全取消，current contexts 已按新 scale 重建。
- `TabletRemoved` 后剩余 attached digitizers 立即拥有 prebuilt decoder；removed context 不再可解析。
- `TabletAdded` 成功时只增量加入新 decoder并保留既有 decoder；context-ID lookup 失败时进行一致的全量重建，不留下部分发布状态。
- active binding 容量遵循 Coordinator 的现有系统容量公式；容量耗尽安全拒绝 Down，duplicate Down 遵循 cancel/rebind 策略，slot generation mismatch 永不访问 stale decoder。
- repeated Down/Up 后 active binding lookup 不会持续退化；删除不会永久积累无界 tombstone，collision cluster 修复后仍可正确 lookup/insert。
- hash indexing 对 96、160、224 等非 power-of-two、但为 32 倍数的 logical capacity 正确，不依赖 bitmask。
- 单元测试覆盖 decoder parser/scale/normalization、binding 状态/碰撞/复用/耗尽/重复 Down/generation mismatch，以及 Enabled/Disabled、UpdateMapping、TabletAdded、TabletRemoved 和 shared-scale 等价 lifecycle。
- 除精确新增 `RTSDI_UpdateMapping` 订阅外，不修改 `runtime_metrics.*`、HUD、`DrawingController`、`ContactInputCoordinator`、Renderer、StrokeModeler、prediction、`ContactSnapshot` metadata shape 或其它 RTS production configuration。
- `Debug|ARM64`、`Release|ARM64` 全解决方案构建及现有/新增测试通过，`git diff --check` 和 BOM/CRLF 检查通过；本轮只完成 planning，不启动实现或提交。

## Out Of Scope

- 不重新调查 Precision Touchpad 根因，不新增 HUD 信息/线程，不重构 WindowController 或 DirectComposition。
- 除新增 `RTSDI_UpdateMapping` 外，不改变原有 DataInterest flags、Precision Touchpad/Pen/Touch/Mouse 路由、desired packet property 集合、tablet flags 或 coordinator semantics。
- 不把 decoder/binding 放入 `ContactInputCoordinator`、`ContactSnapshot`、Renderer、StrokeModeler 或 prediction。
- 不在 `Packets` 内加入 COM recovery、系统查询、格式化、GDI、文件 I/O 或临时 probe 分支。

## Risks And Deferred Items

- Microsoft API 契约不足以证明 Enabled 的 `pTcids[0]` 与 `GetAllTabletContextIds()[0]` 始终顺序等价，因此本任务默认保留一次 Enabled 慢路径枚举；只有实现阶段取得严格证据后才允许删除。
- RTS callback 串行性必须按实际 ownership 复核；若不能证明，保留 lifecycle 慢路径同步，不以潜在 data race 换取优化。
- 全量 rebuild 必须先在固定 staging state 中构建，再发布单一新 generation；构建失败时保持旧 generation 已失效并安全拒绝缺失 decoder 的输入，不能混用新旧 mapping。
