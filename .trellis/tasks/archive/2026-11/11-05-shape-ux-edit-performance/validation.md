# 编辑拖动性能 — 验证记录

日期：2026-11-05，分支 feature/shape（基于 e4d33796，工作区未提交）。设备：Windows 11 ARM64，WARP。
R2 已按用户方案改为「待落定图形留在 L1」（替代上一版冻结背景 PreviewReplacement）。

## 构建

`MSBuild InkeysRepo.sln /m /nr:false /p:Configuration=Debug /p:Platform=ARM64 /p:LinkIncremental=false`
- EXIT=0，0 error（`Build/shape-ux-perf/build-final.log`，UTF-16）。改动文件无新增警告（仅既有 STL4029 等）。

## 回归（最终构建后的实际退出码）

| 命令 | 退出码 | 摘要 |
|---|---|---|
| `Inkeys.exe --draw3-shape-core-test` | 0 | Shape.Create/EditHistory/Interaction/Document failures=0（含预览槽、失焦决策表、Z 序后缀用例） |
| `Inkeys.exe --draw3-shape-renderer-test` | 0 | cases=394 failures=0（含焦点层像素用例与 4K 基准） |
| `Inkeys.exe --draw3-whiteboard-storage-test` | 0 | |
| `Inkeys.exe --ui3-shape-editor-test` | 0 | scenes=382 checks=65223 failures=0（含高频合并与本地跟手） |
| `Inkeys.exe --ui3-geometry-attribute-test` | 0 | |
| `Inkeys.exe --shape-tool-state-test` | 0 | |
| `Inkeys.exe --state-mode-production-test` | 0 | |
| `Inkeys.exe --pen-width-production-test` | 0 | |
| `Inkeys.exe --bar-eraser-offscreen-test` | 0 | |
| `Inkeys.exe --draw3-shape-focus-hidden-test`（新增，不可见 HWND） | 0 | 真实 Host 生产接线与失焦触发 SF00–SF26 全部通过 |
| `InkeysHeadlessTests.exe --no-window` 第 1 次 | 0 | |
| `InkeysHeadlessTests.exe --no-window` 第 2 次 | 0 | |
| `InkeysHeadlessTests.exe --no-window` 第 3 次（最终构建） | 0 | |
| `Inkeys.exe --draw3-eraser-hidden-test`（额外） | 0 | DComp + ULW 两种模式 |
| `Inkeys.exe --draw3-hidden-test`（额外，原有隐藏集成，不在基线清单） | 1 | 基线同样失败，见下 |

RenderScheduler 计时检查：本任务期间 headless 共运行 5 次（上一版 2 次 + 本轮 3 次），全部 EXIT=0，日志中没有 RenderScheduler 失败行。此前报告的失败无法复现，判断为计时抖动（flaky），不是真实回归；本任务也未改动 RenderPipeline/Scheduler。

`--draw3-hidden-test` 基线对比：把本任务改动的 24 个源文件临时换回 HEAD（e4d33796）版本，完整构建后运行，基线有 54 个 FAIL；本任务版本是 48 个 FAIL，且全部包含在基线失败集合中（`comm` 对比后没有本任务独有的失败）。第一条失败是 DComp 模式下普通笔的 contact 未被消费（`hidden contact reached Draw3 consumer and presented`），与图形代码无关，属于本机环境下的既有失败。对比完成后已恢复本任务文件，重新触发编译并全量复跑。日志：`tests/draw3-hidden-test{,-baseline}.log`。

日志：`Build/shape-ux-perf/tests/*.log`。

## 验收项对应证据

### R1 每帧最多 1 次预览、同一手势队列中最多 1 个预览
- 无窗口：`draw3_bridge_tests.cpp::TestShapePreviewCoalescing`（500 次 → 1 条最新，终结意图保序、不跨越合并）；`--ui3-shape-editor-test` 中 1000 次 move 经真实 `StateBridge` 只剩 Begin/最新 Preview/Commit；`--draw3-shape-core-test` 中 `ShapePreviewSlot` 的合并、同帧延后、同手势 Commit 取代等规则。
- 真实 Host（`--draw3-shape-focus-hidden-test` SF05–SF07）：40 次 Preview（每 4 次间隔 8 ms 模拟突发）→ 实际执行 10 次，Bridge 合并 30 次，`max_per_frame=1`，`max_pending=1`，入队→成功上屏延迟：最终构建 `avg=1113 µs`、`max=1580 µs`；上一轮 `avg=1846 µs`、`max=3242 µs`（WARP 不可见 ULW Host）。

### R2 编辑期间图形留在 L1，整个会话（含首帧）L2/tile 不重放
- 真实 Host：
  - SF03：抬笔后自动进入编辑，`CompositionRestoreCallsTotal` 不变。
  - SF06：拖动 + Commit 期间 `canvasCopies=0`、`compositionDiscards=0`，tile 重放 0 次。
  - SF07–SF08：预览只走焦点层（`legacyExecuted` 不变，`focusFallbacks=0`）；Commit 后对象仍留在 L1，未落定。
  - 手势日志：`[Draw3.ShapeEdit] action=gesture_end result=committed previews=10 focus=10 legacy=0 tile_replays=0 …`
- 像素（`--draw3-shape-renderer-test`，WARP 128×128）：
  - 首帧 L2+L1 合成与旧路径直接提交逐字节一致；64 次预览后 L2 逐字节等于 before 状态。
  - 整个会话 `CompositionRestoreCallsTotal` 不变；一次性预览修订不进入长期图形缓存（缓存条目数不变）。
  - 预览合成与候选文档的独立全序参照一致（maxDelta ≤ 2）。
  - 未编辑落定、以及 64 次预览后 Commit 再落定，都与旧路径直接提交最终对象逐字节一致（`==`）；落定本身无 tile 重放。
  - 编辑中撤销：丢弃 L1 后 L2 与合成画面都等于 before。

### 落定触发覆盖（真实 Host `--draw3-shape-focus-hidden-test`）

| 触发 | 用例 | 期望 | 结果 |
|---|---|---|---|
| Esc / 关闭浮层（CloseEditor） | SF09–SF10 | 落定 1 次，无 tile 重放，内容保留 | 通过 |
| 下一笔 Down | SF11–SF12 | 先落定旧图形（恰好 1 次），新图形进入焦点层 | 通过 |
| 编辑中 Undo 该对象 | SF13–SF14 | 丢弃 L1（`focusUndoDrops=1`），不落定、不重放；撤销日志 `path=focus_layer` | 通过 |
| 切换工具 | SF15–SF16 | 落定 1 次 | 通过 |
| 进入选择模式 | SF17–SF18 | 落定 1 次 | 通过 |
| 画布外失焦（PPT 放映按键 / 最小化，共用 `RequestShapeEditorFocusLoss`） | SF19–SF20 | 落定 1 次 | 通过 |
| Clear | SF21–SF23 | 先落定，再清空 | 通过 |
| 尺寸变化 | SF24–SF26 | 只丢弃 L1（L2 从文档重建，已含该项），不重复绘制，内容保留 | 通过 |

决策表（`--draw3-shape-core-test`）覆盖全部 `CanvasFocusEndReason`：换页、Resize、设备丢失、视口刷新这四种会从文档整体重建 L2 的情况一律 Drop，其余一律 Materialize。

以下触发已接到同一入口 `EndCanvasFocus`，但隐藏 Host 不能直接注入，只有代码接线与决策表保证：
- 两指触摸平移：在 `SetViewport` 之前落定，并立即刷新可信快照。
- TranslateViewport、PPT/白板换页与工作区切换：走 SceneChange 或 PageSwitch。
- 设备丢失、视口整体刷新：走 Drop。
- 主栏属性面板打开：沿用既有的 CloseEditor。

L2 完整像素读取方的排查：
- 可信 L2 快照：焦点期间不刷新，落定时立即刷新。
- 缩略图与 PPT 可见导出：经检索都来自 CPU 文档，文档在抬笔时已包含该对象，所以不需要落定。
- 撤销热前像：推迟到落定时采集。

### R3–R5（保持上一版实现）
`--ui3-shape-editor-test`：
- 本地跟手：40 次 move 都显示共享核心候选。
- 窗口按块增长，target 重建 ≤4 次。
- 静止不 Present；Down 按更早呈现帧命中。

主栏只在构造提示或会话开闭时刷新。

## 4K / 2000 条普通笔迹基准（WARP 离屏，含 GPU 完成与每帧上屏合成）

最终构建：
```
[ShapeRenderer.Bench] strokes=2000 size=3840x2160 warm_ms=3444.6
  legacy_first_ms=5547.24 legacy_avg_ms=1424.82 legacy_max_ms=5547.24
  focus_first_ms=63.27 focus_avg_ms=25.88 focus_max_ms=29.01 materialize_ms=23.16 speedup=55.1x
```
同一代码的另外两次运行：
- `legacy_first=6901.0 legacy_avg=1497.6 focus_first=87.2 focus_avg=27.3 materialize=26.3 ms`
- `legacy_first=1569.6 legacy_avg=883.0 focus_first=64.5 focus_avg=26.3 materialize=25.2 ms`

- 旧路径每次预览都要复制页面、清空缓存、重放 tile：WARP 上每次 0.9–1.5 s（首次缓存冷启动更高），抖动大。
- 焦点层首帧（抬笔）≈ 63–87 ms，内容是首次构建图形渲染管线/图层，加上一次全画布合成脏区；不构建背景、不重放 tile。
- 稳态每帧 ≈ 26–27 ms（最终 25.9 ms）：清一次 4K L1，画一个对象，再按脏区上屏。WARP 是 CPU 光栅，全画布清除占主要成本，真实 GPU 上远低于此。
- 落定一次 ≈ 23–26 ms。

## 未能离屏验证（需实机/用户验收）

- 笔/触屏拖动的实际手感、松手后无追赶；真实 DComp/ULW、真实 GPU 下的上屏延迟（日志 `[Draw3.ShapeEdit] action=gesture_end …`）。
- 两指平移、PPT 实际放映按键（含只推进动画的按键）、真实窗口最小化、设备丢失下的落定，以及 PPT/白板实际换页。这些已接线，但隐藏 Host 未注入。
- 资源故障注入：L1 绘制或落定失败时的权威重放回退（`[Draw3.ShapeEdit] action=focus_layer|materialize result=fallback`）。
- 多接触同时抬笔、最后一笔不是图形时，不进入焦点层，沿用旧路径（按设计回退，未做实机）。

## 独立检查（2026-11-05，trellis-check）

检查范围是整个工作区（上一轮检查被中断后重新执行）。修复了 3 处问题：

1. 「丢弃 L1、从 CPU 文档重建」这类失焦方式（翻页、尺寸变化、设备丢失、视口刷新）原先没有推进 `rasterState`，导致同一个 raster 令牌对应两种 L2 内容。现在丢弃分支在页面 GUID 一致时，会把 rasterState 推进到 after 令牌（`Draw3.ShapeRuntimeActions.inl` `closeShapeEditor`）。
2. Bridge 中预览合并的反向扫描会越过普通画布命令，导致预览可能先于 Undo、Clear、翻页执行。现在扫描到非图形命令即停止（`Draw3.Bridge.cpp` `PublishShapeAction`）。`TestShapePreviewCoalescing` 新增了「Undo 之后的预览」用例。
3. 回退了冻结背景方案遗留、没有调用方的 `RestoreComposition` `rangeBegin` 叠加模式。HEAD 中原有的 `clearTargetTiles` 与 `excludedItem` 保持不变，套索所需的前置能力不受影响。

最终结果（`Build/shape-ux-perf/check3/`）：构建 EXIT=0。以下 12 项全部退出 0：`--draw3-shape-core-test`、`--draw3-shape-renderer-test`（394 cases / 0 failures；本次基准中焦点层每帧约 39 ms，旧路径约 1643 ms）、`--draw3-shape-focus-hidden-test`、`--draw3-whiteboard-storage-test`、`--ui3-shape-editor-test`、`--ui3-geometry-attribute-test`、`--shape-tool-state-test`、`--state-mode-production-test`、`--pen-width-production-test`、`--bar-eraser-offscreen-test`、`--draw3-eraser-hidden-test`，以及 `InkeysHeadlessTests --no-window`。

遗留问题：
- `CanvasFocusEndReason::PresentationAction/WindowHidden` 已声明但未使用。PPT 操作与最小化走的是 CloseEditor，计数时归入 Escape，按原因统计时无法把这两类区分出来。
- 现有的 `--draw3-hidden-test` 在 HEAD 上就有 54 项失败，改动后为 48 项，且这 48 项都在 HEAD 的失败集合内。首个失败出现在 DComp 模式下的普通笔接触，与图形代码无关，需单独调查。
