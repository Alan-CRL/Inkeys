# F-024：Stored Stroke footprint 前置校验独立审查

- 日期：2026-09-28。范围仅为 `Draw3.DrawingController.cpp` 完笔后 `finalizedStroke → BuildStrokeTileFootprint → InkCanvas::AppendStroke → CanvasRuntimeHistory::AppendStroke` 的改动，以及相关生产接口、UInk 物化路径。没有运行构建、性能采样或 GUI；主任务报告本改动后的 Debug/Release ARM64 完整 Solution 构建均退出 0。
- 结论：**针对已复现的极端有限值 footprint 失败，静态审查通过；整个文档/history 追加尚非原子事务。** 没有发现此最小改动对通常笔、荧光笔、橡皮和形状成功路径的语义改动。Controller 事务、真实笔迹及 Win7 设备没有动态验证。

## 证据和数据流

1. `InkStroke::IsValid` 接受有限的坐标和非负有限宽度（`Draw3.InkDocument.cpp:32-35,68-81`），而 `BuildStrokeTileFootprint` 在无可见区域回退参数时仍可因扩张后的边界超出 float 表示域而拒绝（`Draw3.InkHistory.cpp:522-573,583-605`）。生产模块 `Draw3.HistoryProbe.cpp:196-207` 的极值检查报告 `extreme_valid=1 footprint_rejected=1`；它只证明两个接口的边界不同，不执行 Controller。
2. 旧 Controller 顺序是先把 `finalizedStroke` 移入 `InkCanvas`，后由已存储的同笔计算 footprint。当前 `Draw3.DrawingController.cpp:7494-7502` 使用同一个 `BuildStrokeTileFootprint(*finalizedStroke)`，不传 `visibleBounds`，且仅在其返回值和 canvas 都存在时才调用 `InkCanvas::AppendStroke`。因此已证实的极值失败在追加前退出，文档笔数、history、redo 均保持不变；不会产生本次缺陷中的“文档多一笔、history 少一项”。
3. 对可表示的正常笔、荧光笔、橡皮与已开放形状，`FinalizeStoredStroke`/`FinalizeStoredShape` 仍产生同一 `InkStroke`（Controller `:7477-7491`），footprint 计算函数与默认参数未变，随后 `InkCanvas::AppendStroke` 仍校验并移动该笔（`Draw3.InkDocument.cpp:109-114`），history 仍接收相同 `strokeIndex` 与 footprint（Controller `:7500-7513`）。这是调用顺序的改变，当前代码未新增宽度、颜色、页 GUID、设备或绘图工具分支。像素等价性需要 GUI/真实输入复验。
4. 成功追加后才执行 `DiscardRedoBranch`（Controller `:7502-7507`）；`CanvasRuntimeHistory::AppendStroke` 自身也在成功时清 redo（`Draw3.InkHistory.cpp:925-933`）。预检失败不再清 redo，与“没有新的权威笔迹”一致。新笔成功时仍清 redo，并按原有单调 `strokeIndex` 留下旧的不可达历史笔；Undo/Redo 使用 history item 的 `strokeIndex` 回读文档（Controller `:5198-5212,5333-5349`），没有要求笔索引等于 render item 索引。当前纯 CPU HistoryProbe 覆盖普通追加、Undo/Redo、新分支和第二页隔离，但没有执行此 Controller 路径，也没有验证形状/擦除最终像素。
5. 外来 UInk 的 Desktop/PPT 物化分别在新建的临时 `InkPage`/slot 中做追加及 footprint/history 校验，失败即返回空值（Controller `:4873-4928,4931-4937,5029-5075`）。本次前置校验没有改变它们；不能把 Controller live 完笔缺陷外推为已损坏的外部文件或跨页串写。它们的前置校验顺序将来可统一，但本次无需为局部修复扩散改动。

## 剩余失败边界

- `CanvasRuntimeHistory::AppendStroke` 在 canvas 已追加之后仍可返回空：history item 数达到 `uint32_t` 上限、`nextItemGeneration_ == 0`，或 `CompositionRangeTree::AppendRenderItem` 因 ID/index/边界校验失败（`Draw3.InkHistory.cpp:902-924,661-664`）。此时 Controller `:7506-7516,7626-7631` 已清 redo，并仅打印失败；文档/history 仍会分叉。前两项需接近 2³² 次 item 追加，第三项需要预先存在的 tree/history 失配或不可预期内部状态；从当前正常构造及成功追加路径未发现现实可达的返回失败。内存分配抛异常属于另一崩溃/资源边界，不能由该预检认定安全。
- `renderItem` 成功但其 `index != beforeStates.size()` 时 Controller `:7516` 也跳过后续状态向量更新；这需要此前已经发生状态不一致，预检不修复它。GPU 首次 raster 或 Present 失败后的 CPU 文档真值与恢复策略属于 F-023 的审查范围，不应通过撤销本次已经追加的有效笔迹来处理。
- **建议归类：F-024 已修复待 Controller/设备复验；剩余非原子边界另列为低可达性正确性风险。** 如以后要给整个 append 事务作强保证，应在生产 owner 内为 history admission 建立可验证的预留/回滚合同，而不是只增加另一个松散的前置判断。当前没有证据要求在首发中改变正常绘图或 GPU 失败语义。
