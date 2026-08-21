# Draw3 水平矩形识别设计

## Architecture And Boundaries

```text
Draw3 contact lifecycle
  -> final Pen Stroke append
  -> Draw3 shape-recognition adapter
  -> Inkeys.CV.ShapeRecognition (OpenCV implementation only)
  -> correction plan
  -> runtime history + local GPU composition transaction
```

- `Inkeys.CV.ShapeRecognition` 是纯 CPU 识别边界。接口只交换 trivially-owned 的 Inkeys 点、Stroke view 与结果结构；实现单元捕获所有 OpenCV 异常并以 Unknown 失败关闭。
- `Inkeys.Drawing.Draw3.shape_recognition` 了解 Draw3 的 `InkStroke`、样式和 runtime history，但不了解 OpenCV。它收集最大连续候选并将 Canvas-local 物理像素转换为识别输入。
- `DrawingController` 仍是唯一编排者：绘制线程在完成 Stroke 提交、没有任何活动接触或导航后调用适配器，并以事务方式应用修正。
- Renderer/HLSL 不增加新 shape。修正结果作为既有 `StoredInkType::OutlineRectangle` RenderItem 绘制。

## Recognition Contract

- 每个 Stroke 先按弧长均匀降采样到最多 1024 点，所有 Stroke 总计不超过 4096 点；原始顺序和 Stroke 边界保留。
- 用全部点的 `convexHull` 得到候选外包络与轴对齐 bounding rectangle；`approxPolyDP(0.02 × hull perimeter)` 必须得到四角。
- 用 hull 面积与 bounding rectangle 面积计算矩形度，并对有效长段调用 `fitLine` 评估水平/竖直偏轴。
- 以和笔宽、DPI 成比例的边带把采样段分配到上/右/下/左边；沿边区间合并后计算单边及总覆盖，另外累计离边长度、每笔边带占比、总路径长度与角点连接质量。
- 只返回规范化的 `{left, top, right, bottom}`。综合分数通过全部硬门槛后才有意义；任一硬门槛失败立即返回 Unknown。
- Draw3 从最多 6 条的最大后缀开始尝试，命中即停止，使重复描边与构成矩形的所有相邻笔画一起隐藏；更小后缀只用于排除更早无关 Pen Stroke。

## Conditional Rendering Model

- `InkStroke::renderOnlyWhenLatest` 是持久化友好的内容元数据，不表示当前是否被 Undo。
- Runtime RenderItem 保留 `active`（未撤回）状态；`effectivelyVisible` 由整个 active 序列反向推导。末尾连续的条件项可见；在其后存在任一 active 非条件项时，这些条件项隐藏。
- 修正事务把候选原稿标记为条件项，并追加一个非条件 Shape。Shape active 时原稿 effective hidden；Undo Shape 后，原稿重新成为 active 尾部条件组并全部 effective visible。
- 对原稿继续 Undo 时仍按 append 顺序逐项操作，条件组不成为合并的 undo 单元。
- Undo Shape 后若 Append 新内容，先把当前恢复的尾部条件组固化为普通项，再清除 redo 分支；这一步改变内容元数据但不改变当前有效画面。

## History And GPU Transaction

- 可见性变化统一产出受影响 RenderItem 和 Tile 的 delta。原稿与修正矩形的 footprint union 是修正/Undo/Redo 的权威局部范围。
- 热路径可使用覆盖 union 的 preimage ticket；冷路径在提交 CPU active/effective 状态前，根据预演后的有效可见序列执行 composition cache/rebuild/ordered replay。
- 多个 RenderItem 同时改变有效可见性时，按历史顺序对每个受影响 Tile 重放，不能把条件组当作单个 operator，也不能先提交 CPU history 再尝试 GPU。
- 应用修正的顺序为：生成计划 -> 预演条件状态 -> 捕获/恢复 union 背景 -> 绘制 OutlineRectangle -> resolve -> 提交元数据、RenderItem、raster state 与热前像。任何失败都保持原稿 active/effective visible。
- Undo/Redo 先预演目标状态，再恢复 union Tile；成功后才提交 active/effective 状态与 redo 栈。部分 GPU 写入后的失败以旧状态 ordered replay 恢复，失败则标记 viewport 不清晰并请求权威刷新。

## Trigger And Concurrency

- 识别调用只在 Draw3 绘制线程，避免给 document/history 增加锁。
- 触发门复用现有 active contact、reconnect、gesture/navigation 状态；完成一次识别尝试后记录提交 generation，直到新的最终 Pen Stroke 才允许再次尝试。
- 非 Pen 终态、Cancelled、Laser、页面命令和没有新增 Stroke 的全抬起不触发候选替换。

## Compatibility And Failure Handling

- OpenCV 实现只使用 core/imgproc 的 CPU 算法，不引入摄像头、GUI、文件编解码或 Win10 API。
- 所有输入必须 finite，尺寸/计数计算先检查范围；`noexcept` 边界捕获 `cv::Exception`、`std::bad_alloc` 与其他异常并返回 Unknown。
- 识别失败完全保留既有绘制行为；可通过移除适配器调用和新增模块登记回滚，不改变文件格式或 shader ABI。
