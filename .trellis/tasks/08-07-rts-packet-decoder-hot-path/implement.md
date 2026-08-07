# 实施清单

## Phase 1: RtsContextDecoder extraction

- [ ] 在 `realtime_stylus.cpp` 中把 `TabletMetadata` 提炼为 immutable `RtsContextDecoder`，保留现有 property、metrics、device type 和数值转换语义。
- [ ] 抽取可测试的 `BuildContextDecoder` candidate builder，集中完成 packet description、GUID/index、metrics、device kind 和 per-context scale 构建；所有 `PACKET_PROPERTY*` 在 helper 内释放。
- [ ] 将 `DecodeSnapshot` 改为 pure decoder + packet transformation，不访问 cache、binding、lifecycle state、COM、mutex、coordinator 或 cursor sink。
- [ ] 为 decoder slot 加入固定 index、valid 和不会误复用的 generation；只发布完整 candidate，不原地修改已发布 payload。

回滚点：Phase 1 只做结构提炼和等价性测试；若数值结果变化，先回退 extraction，不进入 lifecycle/binding。

## Phase 2: decoder lifecycle

- [ ] 实现共享的 `ClearActiveBindings`、`InvalidateAllDecoders`、`ResetSharedPositionScale`、`ResetDecoderLifecycleState` 及 staged full-rebuild helper，固定清理顺序并消除四份重复逻辑。
- [ ] 在现有精确 `kProductionRtsDataInterest` mask 上仅 OR `RTSDI_UpdateMapping`；保留全部原 flags，不改为 `RTSDI_AllData`，不改变其它 production RTS configuration。
- [ ] 改造 `RealTimeStylusEnabled`：先终止 stale lifecycle/contacts，建立新 generation；decoder 预热使用 callback context IDs，shared scale 默认保留一次 `GetAllTabletContextIds()[0]` 旧语义。
- [ ] 改造 `RealTimeStylusDisabled`：invalidate bindings/decoder generations，reset shared scale/context caches，终止 enabled lifecycle，并沿用 producer contacts、simulation 和 cursor 的安全清理。
- [ ] 实现 `UpdateMapping`：取消活动 contacts、invalidate generation，枚举 current contexts，stage 新 shared scale/decoders并发布；callback 内不得重新设置 desired properties、tablet flags、DataInterest 或 tablets mode。
- [ ] 改造 `TabletRemoved`：全量取消/invalidate 后调用 `GetAllTabletContextIds()`，立即重建 remaining contexts 和 shared scale，不猜 removed tcid。
- [ ] 改造 `TabletAdded`：正常使用 `GetTabletContextIdFromTablet()` 局部构建并原子发布一个 decoder；失败时转入取消 contacts 的安全全量 staged rebuild，不留下部分/混合 generation。
- [ ] 明确 full rebuild 失败策略：旧 mapping/generation 保持 invalid，缺失 decoder 的输入安全拒绝，绝不恢复陈旧 scale。

回滚点：在接入 active binding 前，单独验证 Enabled/Disabled、mapping 和 tablet hotplug 状态机。

## Phase 3: active contact binding

- [ ] 增加最大 4096 槽固定 backing array；runtime logical capacity 复刻 Coordinator 的 `round_up_32(2 * (SM_MAXIMUMTOUCHES + 2))`、最小 32、最大 4096 策略，不修改 Coordinator。
- [ ] 实现 `(tcid, cid)` fixed hash + bounded probing，只保留 `EMPTY`/`OCCUPIED`；bucket 使用 `hash % logicalCapacity` 和显式 wrap，不依赖 power-of-two bitmask。
- [ ] 实现 delete + reinsert following cluster：删除后修复连续 probe cluster并恢复真正 `EMPTY`；repair 只允许在 Up、duplicate/stale cleanup 和 lifecycle 低频路径执行，禁止从 `Packets()` 触发。
- [ ] binding 只保存 key、decoder slot index 和 decoder generation；命中后先校验 index/valid/generation，mismatch 永不 dereference stale decoder。
- [ ] 实现 duplicate Down 策略：安全 cancel/close 同 key 的旧 producer contact，释放旧 binding，再建立唯一的新 binding。
- [ ] 维护精确 `activeBindingCount`；外部 insert/erase 更新 count，cluster 内部 reinsert 不更新。count 为 0 时 repair invariant 保证表已全 EMPTY，lifecycle reset 仍显式 clear；容量耗尽拒绝新 Down且不修改现有 bindings。

## Phase 4: Down / Packets / Up / InAir migration

- [ ] `StylusDown` 使用 bounded tcid resolver，先 binding 后 decode/publish；decode 或 `PublishDown` 失败时释放 binding。
- [ ] `Packets` 改为 binding -> validated decoder slot/generation -> fixed-index decode -> `PublishMove` -> bounded diagnostics；移除正常路径 `FindMetadata`、decoder scan、COM recovery 和 pixel-scale publication 查询。
- [ ] `StylusUp` 只在 generation 匹配时 decode，随后尝试 `PublishUp`；缺失/mismatch/坏包使用现有安全 terminal fallback，并在所有返回路径释放 binding。
- [ ] `InAirPackets` 仅执行 tcid -> bounded decoder-cache lookup -> decode；不得创建 active contact binding。range callbacks 保持低频 resolver 语义。
- [ ] 保持 Pen inversion、MouseLeft/Right routing、cursor sink、interrupted-stroke simulation、QPC、phase 和 bounded `--rts-trace` 行为等价。

回滚点：若 callback migration 出现回归，可回退到已验证的 immutable decoder cache，但不得把 COM/metadata scan 放回正常 `Packets`。

## Phase 5: synchronization cleanup

- [ ] 展开所有 RTS callback、Shutdown 和 diagnostics 访问链，证明 decoder/binding ownership 及 callback serialization。
- [ ] 只有证据充分时才删除旧 metadata/shared-scale mutex/atomics；证据不足则保留 lifecycle/build 慢路径的安全 publication，`Packets` 仍不得获取 mutex或看到原地修改的 decoder。
- [ ] 静态审查 `Packets`：无 `GetAllTabletContextIds`、COM、allocation、GUID scan、metadata/context linear scan、formatted output、file I/O、HUD 或系统查询。
- [ ] 只删除被旧 RTS metadata/shared-scale publication 独占且确认无调用的 helper；不清理 renderer 或任何无关公共功能。

## Phase 6: pure decoder / binding / lifecycle tests

- [ ] decoder fixtures：X/Y 与 optional property index、metrics、property count mismatch、shared position scale、per-context contact scale、pressure、tilt/orientation、contact size 和 device type 等价。
- [ ] `RtsProductionDataInterestIncludesUpdateMapping`（或等价测试）：验证 `RTSDI_UpdateMapping` 已设置，并逐项验证 Enabled/Disabled、InRange/OutOfRange、InAir、Down/Packets/Up、TabletAdded/Removed、Error 原 flags 未丢失；同时确认不是 `RTSDI_AllData`。
- [ ] binding 基础 invariants：hash collision、EMPTY stop、cluster-repair release、slot reuse、duplicate key 唯一性、active count 和 lifecycle clear。
- [ ] non-power-of-two capacity fixtures：至少覆盖 logical capacity 96、160、224，验证 bucket/probe 使用 modulo/wrap 后 lookup、insert、erase 均正确。
- [ ] repeated single-contact lifecycle：至少 10000 次 insert -> lookup -> remove，最终 active count 为 0、表恢复 EMPTY、仍可继续插入且 lookup state/cost 不随历史次数退化。
- [ ] collision cluster deletion：构造 A/B/C/D 同 cluster，删除 B 后 C/D 仍成功、B 失败、E 可插入，覆盖尾部 wrap-around。
- [ ] repeated collision churn：数千次插入 colliding keys、删除 subset、插入新 keys，验证无错误 lookup、无 duplicate、无容量假耗尽、无永久 tombstone。
- [ ] Enabled A -> build decoder/bind contact -> Disabled：binding、decoder、shared scale 全失效；Enabled B 后 old binding 永远不能 resolve。
- [ ] UpdateMapping generation A + active binding A：contact 被安全取消、A decoder 失效、shared scale 被替换、current contexts 以 generation B 重建。
- [ ] TabletRemoved contexts A/B/C -> remove B：contacts 关闭、旧表失效、枚举 A/C并重建、B absent。
- [ ] TabletAdded existing A/C -> add B：只增量发布 B，A/C decoder slot/generation 保持有效。
- [ ] TabletAdded context-ID lookup failure：触发安全全量 rebuild，不出现部分发布或混合 generation。
- [ ] active binding logical capacity exhaustion：next Down 安全拒绝，无 allocation，既有 bindings 未损坏。
- [ ] duplicate `(tcid, cid)` Down：旧 producer contact cancel/close、旧 binding release、当前 Down 成为唯一 binding。
- [ ] binding/decoder generation mismatch：Packets 丢弃，Up 走安全 terminal close，二者都不读取 stale decoder且最终释放 binding。
- [ ] shared scale compatibility：即使 callback `pTcids` 顺序与 `GetAllTabletContextIds()` 顺序不同，Enabled 结果仍等价于当前枚举首 context 语义。

## Phase 7: build and validation

- [ ] 确认改动主要限于 `draw3/realtime_stylus.cpp/.cppm` 和必要纯 RTS tests；除新增 `RTSDI_UpdateMapping` 外，HUD、DrawingController、ContactInputCoordinator、Renderer、StrokeModeler、prediction、ContactSnapshot 及其它 production RTS configuration 未改。
- [ ] 使用 ARM64 MSBuild 构建 `Debug|ARM64` 与 `Release|ARM64` 全解决方案。
- [ ] 运行 ARM64 Debug/Release 全部测试；可行时补 Debug/Release x64。
- [ ] 执行 `git diff --check`、BOM/CRLF 检查、禁止 API/hot-path 人工展开和最终代码 review。
- [ ] 向用户交付 Precision Touchpad、Pen、Touchscreen、Physical Mouse、DPI/orientation mapping change 与 tablet hotplug 的硬件验证矩阵；不自动启动 GUI。

## Validation commands

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe' .\inkStrokeModelerTest.sln /m /p:Configuration=Debug /p:Platform=ARM64 /t:Build
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe' .\inkStrokeModelerTest.sln /m /p:Configuration=Release /p:Platform=ARM64 /t:Build
.\ARM64\Debug\inkStrokeModelerTestTests.exe
.\ARM64\Release\inkStrokeModelerTestTests.exe
git diff --check
```

实现前必须由用户确认本轮最终规划摘要；本文件不是 `task.py start` 或产品代码实现授权。
