# 技术设计：激活抬笔与落笔屏蔽

所有位置均为 `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp`（基线 babdecf6），实施前复核行号。

## 1. 现状

- `PalmEraser::ExclusionRadius`（Draw3.PalmEraser.cpp:80）：`r + max(0.15r, 4px)`，画布 px。
- `PalmNeighborBlocked`（:2410）：遍历活动手掌（续接等待中的用冻结 Up 位置，超过续接期限不计），中心距 ≤ R 返回 true。仅用于：新独立 Down 的 `newPalm.Begin(..., recognitionBlocked)`（:12625 附近）与候选观察 `observePalm`（:12036）。
- 激活：`observePalm` 内 `PromotePalmRuntime` 成功（:12052）。
- 并行关闭的收尾：帧内 `hasBlockingPalm() || palmInterruptQpc > 0` 时遍历 active，对非手掌、未结束触点以最后模型快照（续接候选取 `deferredUpSnapshot`）改 Up、清预测、`completeModelUp(runtime, terminal, false)`，并做 Laser / 鼠标尺寸 / 手势 / 触觉收尾（:17352 起）。
- 并行关闭的落笔拒收：`palmObservationOnly && !newPalm.eligible` → `touchGesture.OnTouchUp` + `input_.DiscardUntilTerminal(handle)`（:12631）。

## 2. 方案

### 2.1 形状无关的范围接口（R6）

把 `PalmNeighborBlocked` 内的距离判断抽成 `PalmExclusionContains(const RuntimeStroke& palm, PointF center, PointF point)`（或等价自由函数），本任务只实现圆形；custom 子任务在手掌形状可为矩形时扩展为“矩形每侧外扩 max(半边长×0.15, 4px)”。`PalmNeighborBlocked` 改为调用它，语义不变。

### 2.2 激活瞬间抬笔（R1、R2、R5）

- 在 `observePalm` 中 `PromotePalmRuntime` 成功后，仅当 Touch 并行开启（即当前不会走 `hasBlockingPalm()` 全量中断的情形）时，登记“本帧新激活手掌”（runtime 指针即可，帧内局部 vector/小数组，不跨帧）。
- 在现有 :17352 收尾循环处，把单个触点的收尾逻辑提取为局部 lambda `finishInterrupted(RuntimeStroke&)`，原并行关闭循环改调它（行为不变）。
- 新增：对本帧新激活的每个手掌，遍历 active，条件：`other != palm`、`!ended`、`!palm.active`、`!palmObservationOnly`、`metricDeviceType == Touch`、位置在 `PalmExclusionContains` 内（位置取 `awaitingReconnect ? deferredUpSnapshot : lastModelSnapshot`）→ `finishInterrupted`。
- `completeModelUp` 已置 ended、清续接状态并在正常提交后 `DiscardUntilTerminal`，因此后续 Move 不出墨、真实 Up 回收、续接不会复活（需用例确认）。
- 只在激活帧执行一次；之后不随手掌移动重复截断（R2）。

### 2.3 激活期间落笔屏蔽（R3、R4、R5）

- 独立 Down 准入处（:12625 附近）：当 `deviceType == Touch` 且已计算的 `PalmNeighborBlocked(active, down.position, down.qpc)` 为真时，走与 `palmObservationOnly && !eligible` 相同的拒收：`touchGesture.OnTouchUp(...)` + `input_.DiscardUntilTerminal(handle)` 并返回。`DiscardUntilTerminal` 保证在真实 Up 前隔离（含移出范围）。
- 续接顺序：复核 :12340/:12482 的续接匹配。手掌自身续接匹配须先于屏蔽判断保持优先；匹配到**非手掌**的普通笔划续接前，若 Down 位于任一活动手掌范围内则拒收，防止借续接绕过（R4）。
- 并行关闭时原 `palmObservationOnly` 路径完全不变（R5）。

### 2.4 文案（R7）

`zh-CN.jsonc` 中 `ParallelWritingDescription`（当前“激活手掌橡皮时不会中断其他的输入…”）改为说明：手掌附近的触摸书写会在激活时结束、附近不能落笔，其他位置可继续书写。经 `Scripts/i18n.ps1 sync` 生成并补全全部语言后 check。

## 3. 约束与风险

- 全部在绘制 owner 消费层，不触碰 ContactInput / RTS 回调。
- 激活与 Up 同帧、批内顺序、手掌窗口内面积增长（R 变大）：以激活帧时的 R 为准，只截断一次。
- 保留墨迹意味着激活前已写出的误写仍保留（用户已接受“正常抬笔行为”）。
- 回退：本子任务改动集中在上述三处 + 文案，可整体回退。
