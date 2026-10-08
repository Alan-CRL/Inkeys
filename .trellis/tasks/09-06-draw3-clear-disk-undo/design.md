# Draw3 Clear 区间恢复设计

## 1. Design Goals

- Clear 立即显示透明空画布，但不因异步保存失败丢失刚清空内容。
- Desktop 只跨最近一个 Clear；PPT 每个 Slide 的全部 Clear 区间保存在同一个 canonical UInk，允许逐级 Undo 到最初。
- PPT 历史不常驻 DrawingController 内存，重新进入放映后仍能恢复当前内容和区间导航。
- 保留当前 `draw` 的文档 slot、PresentationKey/SlideID、retained canvas、三态 ready、Laser 和 transparent presenter 合同。
- 使用 UInk version 10 Type ID 6 Clear 的正式语义，不使用 `chore/draw3` 的内存 RenderItem barrier。

## 2. Final Scenario Scope

| Scene | Persistence gate | Cross-Clear depth | Storage |
| --- | --- | --- | --- |
| Desktop | `saveSetting.enable` | 最近一个 | 开启时复用日期 UInk；关闭/失败时一个 memory fallback |
| Presentation | 当前始终开启 | 当前 Slide 全部有效 Clear 区间 | 同一 PresentationKey canonical `.uink` |
| Whiteboard | 当前禁用 | 本期不实现 | 只保留后续设计 |

Presentation 不再读取 `window_.AutoSaveEnabled()` 作为保存/加载门控。未来 PPT 独立开关必须另建产品设置和迁移策略，不能重新复用 Desktop 开关。

## 3. UInk Version 10 Clear

model 新增：

```text
UInkClear
  type: uint16 = 6
  contentId: uint32
  undoId: uint32
  extra?: Map
```

- Clear 与 Ink/Shape/Media 共享 Canvas 内容序列，且独占自己的 undo group。
- forward composition 遇到 Clear 时把同一 Canvas 此前全部可见结果重置为透明；后续内容从透明结果继续。
- `A, Clear1, B, Clear2, C` 当前只显示 C；跨区间 Undo 顺序为 C、B、A。
- Clear 只影响所属 Canvas，不改变 page/device/layer/workspace identity 或 viewport。
- 当前 full Clear 包括 Media；矩形范围与内容类型筛选延期，以未来可选字段扩展。
- `renderOnlyWhenLatest` 的反向扫描在 Clear 处终止。
- 仍可 Undo 的旧区间是有效内容，完整保存必须保留；已撤回的尾部和 Redo 不要求跨关闭保留。

对应公开规范已经写入 `D:\Project\Inkeys\website\docs`；实现合同详见 `research/uink-v10-clear-contract.md`。

## 4. Runtime Interval Model

新增最小纯值状态模块（暂名 `Draw3.ClearRecovery.cppm/.cpp`），不依赖 UI/D3D：

```text
PageIntervalState
  intervalId
  parentIntervalId?
  undoFloor
  pageIdentity
  desktopRecovery?
  presentationCursor?
  save/load generation
```

- `undoFloor` 是当前 runtime history 不可跨越的根。UInk 区间导入后，导入 Stroke 仍参与 replay，但其 item count 成为 floor；新 Stroke 在 floor 之上正常 Undo。
- Desktop recovery point 状态为 None / SavingWithMemory / MemoryReady / DiskReady / Loading。
- Presentation cursor 至少包含 PresentationKey、pageGuid、StableSlideId 或 fallback binding、当前 Clear ordinal/undoId、canonical source revision 和 interval generation。
- `DrawingDocumentSlot` 按 pageGuid 保存 interval state。StableSlideId 重排只改变 active projection，不改变 pageGuid/SlideID 历史归属；deleted page 与 cursor 一起进入 retained/parked 状态。

## 5. UInk Codec And Draw3 Projection

- `UInkContent`、append variant、reader、encoder、normalizer、limit/accounting 与 diagnostics 增加 UInkClear。
- reader 接受当前 version 10 Type 0-6。无效 Clear 会改变视觉真值，因此 strict Draw3 importer 必须拒绝对应 Canvas，而不是跳过后继续显示。
- Draw3 snapshot 从纯 `strokes[]` 扩展为有序 operation/tail 表达，至少能区分 Stroke 与 Clear 并保持 contentId/undoId。
- Desktop snapshot 不需要 Clear 时继续生成既有 version 10 Stroke-only 文件和相同 index schema。
- Presentation importer 提供两种投影：
  - active projection：只物化每页最后一个有效 Clear 后的当前区间；
  - interval projection：按 page identity + Clear ordinal 物化指定旧区间，并返回更早 cursor。
- importer 解析期间可以暂时持有完整 UInkDocument，但 completion 只携带目标区间纯值；controller materialize 后释放解析对象，不把全部历史驻留内存。

## 6. Presentation Canonical Operation Log

不再创建每次 Clear 的独立 checkpoint 文件。现有 `PresentationAutoSaveService` 继续让同一 PresentationKey 绑定一个 fileGuid/path，但每页 Canvas 内容升级为操作日志。

保存请求按 page/interval 表达：

```text
PresentationTailSnapshot
  target + mutationRevision + sourceRevision
  per-page pageIdentity + intervalId + current visible tail
  optional SealClearBoundary
```

worker 在串行所有权内读取/缓存 canonical 文档，并进行以下 reducer：

- 普通 save：保留每页已 sealed 的 Clear 历史，只替换当前 interval tail；同一 interval 的 pending tail 可以 latest-wins。
- Clear save：先用请求快照封存 Clear 前 tail，再追加独占 undoId 的 UInkClear，创建空的新 interval。每个 Clear boundary 必须按接受顺序 durable，不能被 later request 合并。
- Undo boundary save：把当前有效位置截断到目标旧 interval，移除其后的内容/Clear，并完整保存；本任务不保留跨关闭 Redo。
- topology save：保留 active 与 retained Canvas 的全部 sealed intervals，按当前 SlideID 投影重新编号 pageIndex，不按页序丢历史。

每次完整保存继续使用 expected source revision、临时文件、自校验、原子覆盖和 index 更新。per-key 队列需要区分可合并 Tail 与不可合并 Boundary；CloseAndDrain 必须让每个已接受 Boundary 进入 Committed/Failed，并让最后 Tail 收敛。

## 7. Clear Transaction

只在全部 active/reconnect contact 收尾后的 Canvas command 安全点执行：

1. 验证当前页有内容；空 Clear no-op，不创建 boundary。
2. 生成 clearRequestId/new intervalId，捕获当前可见 tail。
3. 暂时把旧 page/runtime 移入 memory fallback，创建保留 pageGuid/viewport 的空 page/runtime。
4. 清 GPU history/cache、composition maintenance、viewport recovery、trusted L2、Laser/粒子/光标/临时 operator，推进 raster generation。
5. 完整透明 Present 后发布 `currentPageHasContent=false`；Presentation mutation revision 推进。
6. Desktop 按开关提交不可变历史请求；Presentation 始终提交不可合并的 SealClearBoundary。
7. durable completion 匹配时释放 fallback。提交拒绝或失败时保留一个 fallback，允许本次误清空恢复；Presentation 后续 boundary 在前一 boundary 未收敛前按序排队。

GPU 和 active page 旧资源立即退出当前文档；为保证失败安全，fallback/worker snapshot 最多保留到 boundary durable。成功后不再占 controller 或 queue 内存。

## 8. Undo Flow

### 8.1 Current interval

- 若 `LastVisibleItem` 位于 undoFloor 之上，执行现有 Stroke Undo。
- 到 floor 之前不发磁盘 load；Clear 后写 A/B 时连续 Undo 顺序先 B、A。

### 8.2 Desktop boundary

- MemoryReady/SavingWithMemory 直接恢复 memory page；DiskReady 提交一次 strict load。
- 成功恢复后设置 undoFloor，消费唯一 recovery point；再次 Undo no-op。
- load 失败保持当前权威画面和 locator，允许后续显式重试。

### 8.3 Presentation boundary

- 到 floor 后用当前 cursor 请求前一个 UInk Clear 区间；重复 Undo 在 Loading 时 no-op。
- worker 校验 canonical source/file/PresentationKey/page identity，返回目标 interval snapshot 和更早 cursor。
- drawing thread 只替换目标 page/runtime，设置 undoFloor，推进 mutation revision；其他页和 retained Canvas 不动。
- 成功 full replay/Present 后才发布 content/clean/ready，并提交 Undo-boundary canonical rewrite，使退出/重入保持当前旧区间。
- 若 cursor 仍有更早 Clear，下一次 boundary Undo继续加载；到最初区间后停止。
- 当前旧区间上新增 Stroke 会建立新分支；后续 save 截断较新 intervals。Redo UI 与前向 interval 保存延期。

## 9. Async Identity And Stale Results

所有 save/load completion 携带：Host generation、workspace、PresentationKey（如有）、pageGuid、SlideID/fallback binding、intervalId、clearRequestId、mutation/source revision。

- stale completion 可以完成 worker 清理，但不能释放新 fallback、推进错误 revision 或激活到其他 scene/page。
- 页面/场景切换时 completion 附着 matching active/parked slot；激活前再次核对 target revision。
- 新 contact、Clear 或破坏性 page command 使正在加载的旧 generation 失效，避免迟到 interval 覆盖新绘制。
- CPU candidate 构造完成前不修改活动 slot；GPU full replay 或 Present 失败沿现有 authoritative recovery 路径收敛。

## 10. Selection And Three-State Presentation

保留来源分支 `ResolveBarClearClickAction`：有内容发布 Clear；空且非 Selection 进入 Selection；空 Selection no-op；首击只有 Accepted 才让 double-click continuation 进入 Selection，失败则重试 Clear。Clear 不进入 300ms toggle coalescer。

内容真值只能由当前 active interval runtime 发布。Clear clean frame 到达前不提前切换 surface；旧 interval 恢复时先 materialize/full replay，再发布有内容 revision。Primary/Presentation/Hidden sibling ULW 继续只根据 scene identity、content revision 与 clean frame 互斥，不直接读取 history cursor。

## 11. Redo Compatibility

- 不修改 Bar Redo 入口，也不实现跨 Clear interval Redo。
- 当前 runtime 内 undoFloor 以上的 Stroke Redo 保持原实现与测试。
- 成功跨 interval Undo 后，canonical rewrite 按 UInk 现有合同丢弃 forward history；未来 Redo 任务需要改变此策略并增加前向 cursor/branch persistence。

## 12. Whiteboard Follow-Up

Whiteboard 重新启用时另建任务：定义稳定 workspace key 与 canonical UInk；复用 Type 6 Clear、interval cursor、undoFloor、completion identity 和 full-present 顺序；补齐多页/viewport、Desktop/PPT/Whiteboard 隔离、保存失败和 stale completion 测试。当前不创建 Whiteboard 文件、不接 worker、不启用入口。

## 13. Merge Decisions

- 保留：Clear 单击/双击/Selection helper、Accepted gate、Clear 绕过 click coalescer、来源任务归档与 Session 27。
- 改写：Controller Clear/Undo、GPU full reset、hidden-window 测试与活动 spec。
- 舍弃：RenderItemKind::Clear、AppendClear、Clear composition operator 及 standalone 镜像；UInk Type 6 承担持久区间边界，runtime history 只负责当前区间 Stroke。
- journal index 保留当前 Session 28 元数据并插入 Session 27。

## 14. Failure Matrix

| Condition | Result |
| --- | --- |
| Desktop autosave off | 不写文件，保留最近一个 MemoryReady point |
| Desktop save/index failed | 保留 memory fallback，不建立 DiskReady |
| Presentation boundary submit failed | Clear 视觉仍完成，保留 fallback并重试 boundary；不得让 later boundary 越过它提交 |
| Undo while boundary saving | 直接使用 fallback；迟到 completion 只按 generation 收敛 |
| interval load/strict import failed | 当前页不变，cursor 保留供重试 |
| duplicate Undo while Loading | 不重复 I/O |
| canonical SourceChanged/foreign claim | 不覆盖，不释放 fallback，不串用 foreign history |
| SlideID topology changed | 按 stable ID 重映射；非法/重复/不兼容则拒绝 |
| graphics Present failed | 不发布 content/ready，进入 authoritative recovery |
| app exits with pending boundaries | drain 所有 boundary 和最后 tail 到明确终态 |

## 15. Compatibility And Rollback

- Header.version 仍为 10；公开 Beta 文档已声明当前 Type 0-6 基线取代较早草案。
- 不改变 Header array、既有 Type 0-5 字段、Desktop index、Presentation index schema、Bridge enum 或 COM ABI。
- 旧 Desktop UInk 保持可读；PPT canonical 文件按当前 Beta 新 Clear 语义写入。
- codec/Presentation operation-log 接入失败时可以回滚到 Desktop/Presentation memory fallback，但不得重新采用无限常驻的 runtime Clear barrier并宣称磁盘历史完成。
- 不创建 commit；website 与 Inkeys 两个仓库的提交均由用户后续决定。
