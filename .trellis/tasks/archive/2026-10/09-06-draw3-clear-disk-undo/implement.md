# Draw3 Clear 区间恢复实施清单

## Preconditions

- [ ] 用户已审阅本次更新后的最终规划摘要并再次批准实施。
- [ ] `task.py start` 已把任务状态切为 `in_progress`。
- [ ] 已加载 Phase 2.1、`trellis-before-dev` 与 manifests 中的 spec/research。
- [ ] 保持当前 merge state，不执行 `merge --abort`、不创建 commit。

## 1. Resolve Semantic Merge

- [ ] 合并 `.trellis/workspace/AlanCRL/index.md`：保留 Session 28 状态并插入 Session 27。
- [ ] 以当前 `draw` 的 autosave/PPT/SlideID/laser/三态代码为 `Draw3.DrawingController.cpp` 骨架，移除冲突标记。
- [ ] 回退产品与 standalone `InkHistory*` 中只服务内存 Clear barrier 的 RenderItemKind/AppendClear/GPU operator/tests，保留现有 Stroke Undo/Redo。
- [ ] 保留并适配 Bar Clear 点击 helper、double-click Accepted gate、interaction 接线和 headless tests；确认近期 PPT 按钮尺寸及三态交互未回退。
- [ ] 保留来源任务归档/journal；活动 spec 留待步骤 8 改写。
- [ ] 执行 conflict marker 搜索和 `git diff --check`。

## 2. Implement UInk Type 6 Clear

- [ ] `uink_model.cppm`：增加常量 `kClearType=6`、`UInkClear`、`UInkContent/UInkAppendObject` variant。
- [ ] `uink_codec.cpp`：解析 Clear Map、内容序列 normalization、独占 undo group 校验、invalid provenance 与 resource limits。
- [ ] `uink_codec_encode.cpp`：精确 uint16/uint32 编码、Clear validation/packing、Type 0-6 writer 支持。
- [ ] `uink_file*`：允许仅在末尾最后 Canvas 追加完整 Clear；旧 Canvas/Undo/压缩仍走完整保存。
- [ ] 更新 UInk tests：round-trip、A/Clear/B/Clear/C、空/连续 Clear、共享 undoId 拒绝、未知/损坏块、append/truncation、v10 既有 fixtures 不退化。
- [ ] Draw3 export/import snapshot 支持有序 Stroke/Clear operations、interval selection 和 `renderOnlyWhenLatest` 在 Clear 处截断。

Rollback point：Type 0-5 编码与现有 fixtures 必须保持不变；若 Clear 分支失败，单独撤下 Type 6，不改旧类型解释。

## 3. Add Pure Interval State

- [ ] 新增最小 `Draw3.ClearRecovery.cppm/.cpp`，定义 interval identity、undoFloor、Desktop recovery states、Presentation cursor 和 completion matching。
- [ ] `CanvasPageRuntimeState` 增加 undoFloor/intervalId；`DrawingDocumentSlot` 增加按 pageGuid 保存的 interval/cursor 状态。
- [ ] 纯逻辑测试覆盖 Desktop 单点轮换、PPT cursor 逐级后退、duplicate load、stale generation、branch truncation decision。
- [ ] 对新增 module/source 更新 `Inkeys.vcxproj` 与 test project，保持配置对称。

## 4. Extend Desktop Recovery

- [ ] 让 Desktop Clear request 使用 controller 可关联的 request identity。
- [ ] `DesktopAutoSaveService` 增加 wake/completion queue，返回 file/index durable 终态、fileGuid 和最终 locator。
- [ ] 增加严格 load request/completion，复用 UInk reader/importer；不改变日期目录/index schema和 Exit 语义。
- [ ] 设置开启：commit 后释放 fallback；关闭/拒绝/失败：只保留最近一个 memory fallback。
- [ ] 测试 autosave on/off、collision final path、index failure、delayed writer、Undo pending、load corruption 和第二次 Clear 轮换。

## 5. Convert Presentation Persistence To Operation Log

- [ ] 移除 Presentation 对 `AutoSaveEnabled()` 的门控；保留未来独立开关的单一策略接点，不新增设置。
- [ ] 扩展 save request 为 per-page active tail + interval identity + optional SealClearBoundary。
- [ ] worker reducer 保留 canonical 文件中的 sealed Clear history，只替换 active tail；同 interval Tail latest-wins。
- [ ] Clear boundary 请求按 PresentationKey/page/interval FIFO，不允许 replaced-pending；前一 boundary 失败时 later boundary 不越过提交。
- [ ] full save 合并 active/retained Canvas 的历史，保持 fileGuid/path、expected source revision 和现有 strict index schema。
- [ ] 增加 interval load 请求：按 PresentationKey/pageGuid/SlideID/fallback + Clear ordinal 只返回指定区间与更早 cursor。
- [ ] 成功跨 interval Undo 后提交 branch truncation rewrite，使 canonical current state 与重新进入放映一致；不保存 forward Redo。
- [ ] 测试 A/Clear/B/Clear/C 的保存、退出/重入、逐级 Undo、boundary/tail 乱序、failure retry、CloseAndDrain。

Rollback point：operation-log reducer 与 canonical index schema隔离；失败保留 controller fallback和旧 canonical file，不写部分历史。

## 6. Integrate Controller Clear/Undo

- [ ] Clear 移动旧 page/runtime 到短期 fallback，创建同 pageGuid/viewport 空 runtime，建立 interval identity。
- [ ] 抽取/复用 document slot switch 的 GPU invalidation/full-present helper，保持 `laserTipVisuals`、particles、cursor、viewport recovery 和 presenter 完整清理。
- [ ] Desktop 按开关提交；Presentation 始终提交 ordered SealClearBoundary；durable completion 后释放 fallback。
- [ ] Undo 先限制在 undoFloor 之上；到 floor 后 Desktop 恢复最近点，PPT 请求前一 Clear interval。
- [ ] materialize 只替换目标 page/runtime并设置 undoFloor；PPT 成功时推进 mutation revision并提交 canonical truncation。
- [ ] active/parked/retained slot swap 与 stable topology remap 携带 interval cursor；所有 completion 复核 scene/page/interval/Host generation。
- [ ] full replay/Present 成功后才发布 content/clean/ready。

## 7. Integrate Clear Button Fixes

- [ ] 合入 `ResolveBarClearClickAction`、`ExecuteClearClick` 与必要状态。
- [ ] Clear 绕过 300ms toggle coalescer；accepted/rejected double-click 与异步 content 回报测试通过。
- [ ] 不修改或启用 Redo 按钮入口。

## 8. Specs And Whiteboard Plan

- [ ] 更新 `.trellis/spec/native-desktop/draw3-integration.md`：Desktop 单边界、PPT always-save、多 Clear canonical log、三态 ready。
- [ ] 更新 `.trellis/spec/native/runtime-and-rendering.md`：runtime history 只负责当前 interval Stroke，记录 undoFloor/GPU reset；移除 RenderItem Clear barrier 合同。
- [ ] 更新相关 UInk persistence spec，使 Type 6 codec/import/export 与 `D:\Project\Inkeys\website\docs` 的 version 10 合同一致。
- [ ] 保留 Whiteboard 后续 workspace/canonical history/ready/failure/test 计划，但不接 I/O。
- [ ] 核对最终 diff 与 `research/chore-draw3-merge-analysis.md` 的保留/改写/舍弃清单。

## 9. Validation

- [ ] UInk codec/export/import/file tests：Type 6、旧 fixtures、完整保存、append 和损坏恢复。
- [ ] History tests：普通 Stroke Undo/Redo、branch discard、per-page isolation 不退化。
- [ ] Desktop tests：开关、disk/memory fallback、最近边界、pending/failure/stale completion。
- [ ] Presentation tests：A/B、不同页、parked、SlideID reorder/insert/delete/retained/reappear、A/Clear/B/Clear/C 退出重入和逐级 Undo。
- [ ] 三态/hidden tests：Clear clean frame、load pending、恢复后 content/ready 顺序；只运行无窗口路径。
- [ ] 使用 ARM64 `MSBuild.exe` 构建完整 `InkeysRepo.sln` `Debug|ARM64`，超时至少 5 分钟，不单独遗漏 PptCOM。
- [ ] 若 standalone UInk/history 文件变化，构建并运行相应 `inkStrokeModelerTestTests`。
- [ ] `git diff --check`、冲突标记、UTF-8 BOM/CRLF、vcxproj config symmetry 和未提交状态检查。
- [ ] 不启动可见产品、Office/WPS，不创建 commit。

## Expected Commands

```powershell
<ARM64-MSBuild> InkeysRepo.sln /m /p:Configuration=Debug /p:Platform=ARM64
.\Build\ARM64\Debug\InkeysHeadlessTests.exe --no-window
git diff --check
git status --short
```

真实 Pen/Touch、D3D 或 Office 验证若未获可见窗口授权，必须如实列为未执行。
