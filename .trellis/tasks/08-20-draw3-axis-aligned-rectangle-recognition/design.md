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
- 用全部点的 `convexHull` 得到候选外包络与轴对齐 bounding rectangle；`approxPolyDP` 分别用周长 `2%/3%/4%/5%` 简化，多尺度中存在与包围框四角距离相符的凸四边形才算四角证据，避免局部手抖产生第五个凸点后直接早退。
- 用 hull 面积与 bounding rectangle 面积计算矩形度，并对有效长段调用 `fitLine` 评估水平/竖直偏轴。
- 先在 DIP 中以固定下限、短边比例和笔宽倍数合成 `6–14 DIP` 自适应边带，再统一转换到画布像素。边带把采样段分配到上/右/下/左边；沿边区间合并后计算单边及总覆盖，另外累计离边长度、每笔边带占比、总路径长度与角点连接质量。
- 跨 Stroke 样式一致性只比较 Pen 类型、RGB、opacity 和 texture；点宽受压力影响，只用于几何容差和最终矩形代表宽度，不作为切断候选的稳定样式属性。
- `fitLine` 检查整边方向，原始长段偏轴同时按全局和每条矩形边分别加权；后者专门阻止整体拟合为水平但局部持续锯齿的边。
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

## Dataset Diagnostics

- `Inkeys.CV.ShapeRecognition` 用自有 `ShapeRecognitionDiagnostics` 返回拒绝原因、阈值、多尺度四角证据、DIP/px 容差和阶段指标；仍不向接口暴露 `cv::` 类型。Draw3 适配器补充候选收集停止原因、样式、footprint 计划结果和逐后缀 attempt。
- `DrawingController` 只在全抬起 latch 被消费后读取一次诊断原子开关；开关关闭时向适配器传空指针，不构造报告或 `ostringstream`。开启时即使没有修正计划也输出拒绝报告，现有修正成功/失败日志使用同一开关。
- 配置归属 `Inkeys.Other.Config`，设置卡片仅存在于 `#ifndef IDT_RELEASE`。`IdtMain` 在 Draw3 Host 启动前同步开关；ShapeRecognition 可独立触发控制台分配，但不启用 Draw3 设备环境输出。仅 PptCOM 开启时继续延迟分配控制台，避免带出 Draw3 启动日志。
- 数据输出继续使用控制台，便于用户通过 PowerShell 重定向为原始数据集。首次启用时输出唯一启动记录，包含格式版本、`session_id`、进程 ID 和启动时间；进程内 `pen_up_id` 单调递增，每个后缀 `candidate_id` 可由会话与抬笔事件稳定定位，Stroke 使用 history/render item 的稳定标识或该次候选内稳定序号。
- 每个候选输出按 Stroke 分组的识别输入点，坐标和宽度统一输出为 DIP，同时保留 `dpi_scale` 以及必要的 px 边界，确保日志可重放且能与 `-rts-trace` 的输入路径交叉核对。文本记录使用单行、带显式 record type 的机器可解析格式，路径点不得拆成无法归属的散行。
- 诊断模式下硬门槛仍决定产品结果，但实现应继续计算所有在当前几何阶段可安全得到的证据，分别记录首个 `primary_reject_reason` 和完整 `failed_conditions`；包括 hull/多尺度近似顶点、选中四角、边分配、覆盖区间、每边最大缺口、距离残差分位数、每 Stroke 对各边贡献和轴向指标。诊断异常继续失败关闭，不得影响绘制线程。
- 产品 Debug 入口对 `lpCmdLine` 做局部、只读的 token 检查，接受 `-rts-trace` 与 `--rts-trace`，并在 Draw3 Host 创建时把覆盖值传给现有 RTS diagnostics；不重写旧的 `*` 分隔命令处理。该参数与图形识别诊断互不隐式启用，但允许同时开启；任一诊断要求控制台时应在 Draw3 启动前完成绑定。二者共用控制台时必须通过共享的原子行写入边界避免内容交错，识别记录携带启动/抬笔 ID，不能改变 RTS 路径、识别触发或修正时序。
- Debug 输出初始化先检查继承的 `STD_OUTPUT_HANDLE`/`STD_ERROR_HANDLE`。若 stdout 是有效磁盘文件或管道，则保留 CRT/iostream 到该目标的绑定，不调用覆盖它的 `freopen_s("CONOUT$")`；仅在没有可用重定向时分配并绑定调试控制台。这样 PowerShell 的 `>`/`2>&1` 与 `Start-Process -RedirectStandardOutput` 都可直接采集完整日志。
