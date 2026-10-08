# `chore/draw3` 合并分析

## 分支关系

- 合并目标：当前 `draw`（合并前 `d4421cdb`）。
- 来源：`origin/chore/draw3`（`5a5d044c`）。
- 来源独有提交：
  - `b44dec66 fix(draw3): make clear undoable and refine selection flow`
  - `d8a20457 chore(task): archive 08-24-draw3-clear-undo-selection`
  - `5a5d044c chore: record journal`
- Git 已自动合并大部分文件，但在 `.trellis/workspace/AlanCRL/index.md` 与 `Draw3.DrawingController.cpp` 留下冲突；当前没有合并提交。

## `b44dec66` 的功能拆分

### 1. 内存 Clear history barrier

来源分支新增 `RenderItemKind::Clear`、`CanvasRuntimeHistory::AppendClear()` 与 `HasVisibleContent()`：Clear 不删除 Stroke，而是在 runtime history 中追加 `Add=0/Retain=0` 的透明 operator。Undo 隐藏 barrier 后恢复旧 Stroke，Redo 重新显示 barrier 后再次清空。

相关修改同时镜像到产品与 standalone：

- `Draw3.InkHistory.cpp/.cppm`
- `Draw3.InkHistoryGpu.cpp`
- `inkStrokeModelerTest/draw3/ink_history.cpp/.cppm`
- `inkStrokeModelerTest/draw3/ink_history_gpu.cpp`
- `inkStrokeModelerTestTests/ink_history_tests.cpp`

结论：**不直接保留**。它以长期保留旧 Stroke/history 为前提，与本任务“Clear 后释放旧区间，只保留磁盘或单个内存恢复点”的目标冲突。对应 API、GPU operator 和 standalone 镜像应恢复为当前 `draw` 的 Stroke-only history，再由区间级恢复模型替代。

### 2. `DrawingController` Clear/Undo/Redo 接入

来源分支增加 `resetGpuForClear`，Clear 时追加 barrier、分配 raster token 并保留 CPU history；Undo/Redo 针对 Clear 类型分别执行 composition restore 或全量透明重置。

结论：**按新设计重写**。保留 GPU/瞬态必须完整失效的思想，但继续使用当前 `draw` 的文档 slot、Desktop/PPT 自动保存、mutation revision、SlideID 重映射、`laserTipVisuals` 和 scene-stamped completion。Clear 改为新区间轮换，Undo 在当前 history 为空时进入区间恢复，不把 Clear 放进 Stroke history。

### 3. 清空按钮与选择模式

来源分支新增纯逻辑 `ResolveBarClearClickAction` 和 `ExecuteClearClick`：

- 有内容单击发布 Clear；
- 空内容且非选择模式时进入选择；
- 选择模式空内容时 no-op；
- 双击第一击 Clear 只有在命令真正 `Accepted` 后，第二击才直接进入选择；
- 第一击失败时第二击重试 Clear，不依赖异步内容状态是否已经回报；
- Clear 不进入 300ms toggle 合并。

结论：**保留并适配当前 `draw`**。这是独立于内存 barrier 的交互修复，且能避免异步三态内容回报造成双击竞态。实现时保留现有 Bar/PPT/Whiteboard 布局及近期按钮尺寸修复，只引入 Clear 决策 helper、必要状态和测试。

### 4. 测试与规范

- 来源隐藏窗口测试把 Clear 后 Undo/Redo 改成内存 barrier 的对称恢复。
- CPU history 测试新增 `A/B -> Clear -> C` barrier 顺序、空 Clear 和分支丢弃。
- 两份 Draw3 spec 把 Clear 合同从永久截断改为 barrier。

结论：**历史任务文档保留，活动合同与测试改写**。隐藏窗口测试改为区间恢复；barrier 专属 CPU/GPU 测试不保留；新增纯状态机、Desktop 磁盘、Presentation canonical Clear history、内存 fallback、stale completion、SlideID 和三态测试。活动 spec 记录最终区间模型，不能保留互相矛盾的 barrier/永久截断表述。

### 5. Trellis journal

来源分支带入 Session 27，当前 `draw` 已有更新的 Session 28。

结论：`.trellis/workspace/AlanCRL/index.md` 手工合并：保留 Session 28 的总数、日期与行数，并把 Session 27 插入历史表；`journal-1.md` 的非冲突 Session 27 内容保留。

## 风险结论

- 不能整文件选择 `ours`：会丢失清空按钮双击/选择修复及来源任务历史。
- 不能整文件选择 `theirs`：会回退 Desktop/PPT 自动保存、稳定 SlideID、parked slot、激光视觉和三态恢复。
- Presentation canonical `.uink` 会覆盖同一路径；worker 必须在每次完整保存时保留已封存的 Type 6 Clear 区间，只替换当前 active tail，不能用最新可见 snapshot 覆盖掉历史。
- Desktop Clear 文件是不可变历史文件，可在补齐 durable completion/path 与读取入口后直接作为磁盘恢复点。
- 导入整份 PPT UInk 后不得整文档覆盖当前 slot；Undo 只替换被 Clear 的目标页/SlideID，否则会回退其他页面在 Clear 之后的修改。
