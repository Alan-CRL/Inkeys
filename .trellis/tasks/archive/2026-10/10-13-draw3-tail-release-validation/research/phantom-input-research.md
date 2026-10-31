# Research: 抬笔时突然出现长斜线的输入来源与终态路径

- Query: 阶段四正常颜色程序中，用户确认一条从左上横跨至右下、未实际绘制的长斜线在物理 Up 时突然出现；调查输入归属、延后 Up、接触池复用，以及最小消费端诊断。
- Scope: internal（源码、已有测试、用户运行日志；不修改生产代码）
- Date: 2026-10-13

## Findings

### 已确认事实与早期建议

1. 当前现场证据不能归因。`Build/modeler-baseline/stage3-main-native/ARM64-Debug/Inkeys/bin/log/idt1791876669945.log` 共 26 行，`rts-input` 为 0，尾段/接续/运行笔划诊断为 0。日志覆盖 15:31:09.950 至 15:31:44.276，只有启动、Draw3 门禁、UI3 统计、退出；UI3 的耗时不等同绘制线程耗时。不能根据该日志宣称错误来自鼠标、RTS Up、拟合或者 GPU。
2. 先增加完成态同一记录中的输入归属及消费证据，把输入异常、消费合并、CPU 几何变化与 GPU 展示区分开。仅有最终曲线和 raw 尾窗不足以定位；尾窗可能只含很晚的几个已消费样本。
3. 不建议未经证据直接过滤所有远距离 Up。合法快速运动、消费者漏掉 Move 与驱动坐标错误会呈现相似端点跳跃。会改变真实端点合同的修复需要先有判定依据。

### 输入链及归属

| 文件 | 作用 |
| --- | --- |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp` | RTS Down/Packets/Up、上下文解码、绑定、有限诊断队列 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.cpp` / `.cppm` | 接触池、不可变 Down、最新快照、代次/所有权检查 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` | 消费快照、实际模型调用、Touch 接续、完成 Stored Stroke |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.InkPrediction.cpp` / `.cppm` | 接续身份、ActiveStroke 和显示/模型状态 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.StrokeGeometry.cpp` | raw 尾窗、终点锁定、Up 接纳、最终拟合与重采样 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp` | WM_MOUSE 来源过滤及光标反馈；不发布绘制 Down/Up |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.PenDiagnostics.h` | 隐藏集成观察结构，尚不足以携带输入归属链 |
| `inkStrokeModelerTestTests/contact_input_tests.cpp` | 接触池、RTS 生命周期、接续和几何测试 |

- RTS `StylusDown` 在重复 `(tcid,cid)` 时先取消旧 producer 并擦除绑定，然后插入当前绑定；源码 `Draw3.RealtimeStylus.cpp:1170` 附近。
- `RtsActiveContactBinding` 保存 `tcid/cid/decoderSlotIndex/decoderGeneration/deviceType`。解码缓存 `Resolve` 验证 slot、generation、tcid（`Draw3.RealtimeStylus.cpp:769`）。`StylusUp` 只查精确 `(tcid,cid)`，没有找不到时改用另一接触/默认解码器的分支（`:1241`）。缺包/不匹配解码器/解码失败均取消 contact，并最终沿用最后有效位置。
- `DecodeSnapshot` 严格检查包属性数量和 x/y 索引，并检查输出位置有限（`:950`）。但有限且很远的 Up 坐标没有与上一真实位置的连续性比较。它会原样进入 coordinator。
- `ContactInput.cpp:473` 的 `FindProducing` 只返回相同 `tcid/cid`、Producing/Quarantined 状态。`:564` 的 `Close` 在写入前后检查期望 generation、身份、Closing 状态并独占 writer。`:821` 的 `TryReadSnapshot` 在前后检查 handle generation 和允许状态；Up 只在 ConsumerOwned 后可读取。通常的旧 handle 读到新接触已被显式阻止。
- `ContactInput.cpp:285` 的来源在不可变 Down 锁存；Move/Up 的来源不混入其他设备的 source。`snapshot.position/qpc/sequence/phase` 使用一致多字段快照发布。
- `WindowControl.cpp:1562` 的 WM_MOUSE 分支只处理光标；接受后 `:1737` 发布 Mouse cursor sample。该文件没有 `PublishDown/PublishMove/PublishUp` contact 调用。不能把 Windows 兼容鼠标 Up 的位置直接当作这条产品墨迹的入口。

### 两种尚未确认、但源码允许的长连接机制

**A. 有效但异常的终点位置。** `consumeLatestSnapshot` 使用终态 snapshot.position 构建 kUp（`DrawingController.cpp:9217`）并记录 raw 尾窗（`:9224`）。`LockPenTerminalState` 保存 lastSpeedSnapshot 为 rawMove、当前 Up 为 rawUp（`:9196`；几何 helper `StrokeGeometry.cpp:500`）。Up 的 endpoint admission/fallback 均保证最终原始终点：`PinEndpointGeometry` 在相距较大时追加 endpoint；`AppendTerminalFallbackPoint` 亦追加不同位置。这是“真实 Up 精确保留”合同，不是输入坐标校验。因此若驱动或早期输入端提供错误但有限的终点，完成态能产生长连线。此时 raw endpoint 亦错误，尾段算法不能恢复丢失的正确终点。

**B. 消费延迟使整段 Move 被合并。** 不可变 Down 外，coordinator 只保留最新 snapshot。`PublishMove` 覆盖旧 Move（`ContactInput.cpp:265,775`），终态随后覆盖它（`:615`）。绘制线程如未及时消费，可只看见 Down+Up。`DrawingController.cpp:8849` 初始化模型 Down，`:9217` 把最新终态直接送入 kUp；没有已消费中间点时会沿两点直接连接。测试明确允许 Up 在 Down 出队前到达（`contact_input_tests.cpp:317`）。这不需要坐标串到别的 contact，也可能表现为 Up 时突然出现一条未走过的直线。它仍只是候选解释；现有日志没有绘制线程停顿和消费序列证据。

这两类可用 `produced seq` 与 `consumed seq/count`、Down/最后非终态/Up 的坐标同时区分。序列跳跃表示发布被合并，不能自动解释成坏输入。

### 延后 Up 与接续

- 一般 Pen 书写不进入接续匹配。`IsInterruptedStrokeReconnectIdentitySupported`（`InkPrediction.cpp:861`）只允许 Touch，以及倒转 Pen 的 Eraser；普通 Pen 不支持。
- Touch defer 条件位于 `DrawingController.cpp:9147`，物理 Up 首先作为 kMove，`deferredUpSnapshot` 是按 runtime 保存的值副本（`:9339`）。超时/候选淘汰时 `completeModelUp(runtime,runtime.deferredUpSnapshot)`（`:12880,12897`），不是读取全局最后 Up。
- 接续比较设备/工具/宽度身份，并比较旧 Up、当前 Down 的位置时间与运动方向；`DrawingController.cpp:8451` 调用 Evaluate。成功时先回收旧 ConsumerOwned handle，改为新 handle，重设快照、sequence，清除 terminal 状态（`:8633` 至 `:8653`）。该路径没有直接采纳其他未匹配接触的 Up。
- Runtime 池重新取得后调用 `ResetNominalStroke`，继而 `ActiveStroke::Reset` 清 real/预测/尾窗/终态状态（函数位于 `StrokeGeometry.cpp`，本轮其他 worker 添加测试期间行号从 1702 移到 1774）。没有发现普通池复用残留 `terminalRawUp` 或 rawCount 的事实证据。

### 最小消费端诊断建议（不改 producer，不做每 Move I/O）

既有 RuntimeStroke 字段可以提供 Down、handle、当前 snapshot、最后模型位置和 Up，但**不能提供真实消费次数、最大合并跨度，亦不能在终态处理后可靠恢复 Up 前的 Move**：`consumeLatestSnapshot` 一进入就覆盖 `lastConsumedSequence/lastInputSnapshot`，位置移动时也覆盖 `lastSpeedSnapshot`（`DrawingController.cpp:9111,9272`）。`diagnosticModelUpdates` 仅在 observer 启用时更新，且混有 idle 模型推进，不等于消费 Move 数量。

建议增加一个平凡、有界且仅启用时更新的诊断状态，不保存无界历史：

- consumedSnapshotCount、consumedMoveCount、maxSkippedPublications、lastSequenceGap。
- lastNonterminal(position,qpc,sequence)、terminalPrior(position,qpc,sequence)、firstTerminal(position,qpc,sequence)。
- terminal 模型输入 type、modelTime、status code、产生模型点数、接纳点数、fallback/endpoint-pin、deferred/timeout/reconnect 分支。
- reconnectCount；每次 handle 接管时明确新的 tcid/cid/generation/Down sequence，不能把新 Down=2 与旧 contact sequence 做差。

最小插入点：

1. 新 runtime Down 初始化后将 count=1、lastNonterminal=Down，记录 generation；每次普通新 Down 重置。成功接续记录换代并重置每 contact 的 sequence 计数，可另保留笔划总计。
2. `consumeLatestSnapshot` 在覆盖旧 `lastConsumedSequence` **之前**：对唯一 snapshot 增 count；若 current > old 且差为偶数，`skipped=(current-old)/2-1`。统计包括随后因微抖动被跳过的 Move，但不把它们计为模型 Update。记录新的非终态，遇首个终态冻结之前的 lastNonterminal。
3. `updateContactModel` 周围只统计实际模型调用的 event type/time/status，不把 `completeModelUp` 再次使用同一 deferred snapshot 算作另一次输入消费。普通即时 Up 和 deferred Up 的两种调用都需要记录 terminal 模型结果。
4. `CommitRuntime` 的 `FinalizeStoredStroke` 调用前（`DrawingController.cpp:1836`），复制纯数据 trace context 至 ActiveStroke：tcid/cid/handle.generation、metricDeviceType、Down(position,qpc,sequence,source)、qpcOrigin、last/deferred/model snapshot、上述统计。字段只用于 trace，不进入 Stored Ink 格式；ActiveStroke::Reset 清零。
5. 同一完成记录输出未修正 realPoints 与最终 Stored points 的首尾/count/hash、max segment length+索引、tail decision、修正区间和采样预算。输出同一 identity 足以关联 producer Down/Up、controller 消费和最终 Stored。需要 post-Finalize Stored 概要，不能只观察调用前的 ActiveStroke。

原则：trace 正常默认关闭；启用时在完成态输出一次有上限的数据，输入回调/每 Move 只内存计数。不能让诊断开关控制算法或反色状态。诊断 id 需独立于 `observer.penDiagnostics`，否则产品正常 Host 中会恒为零。

### 已有测试与缺口

- `contact_input_tests.cpp:232`：池容量与跨 generation 复用；拒绝旧 handle 读取和回收。
- `:266`：非法 Down/Move/Up；坏 Up 仍闭合，位置沿用最后有效样本。
- `:317`：Up 先于消费者出队、Move/Up 并发以及关闭后的 Move 拒绝。
- `:633` 至 `:648`：绑定重复/碰撞删除、代次不一致、映射生命周期变化、tablet add/remove、Error 保留解码器、RTS reader/writer gate。
- 缺少现场同样的输入身份/消费序列 fixture；现有纯算法回放不覆盖真实 RTS 到 Controller 的消费间隔。
- 可新增的有意义回归：两 contact 同时活动且一个 Up 终态，不得改另一 runtime；reuse 时旧终态不污染新 contact；Down+多 Move+Up 全部在消费前产生时明确报告合并次数；Touch defer 后同帧新 Down 的拒绝/成功与 timeout 均正确保留终点。最后一类不能仅依据图形“看起来是长线”判错，因为 mailbox 合并是既有合同。

## Related Specs

- `.trellis/spec/native-desktop/input-and-ink.md`：RTS 唯一绘制 producer、WM_MOUSE 仅光标、来源与有限队列诊断、硬件验证边界。
- `.trellis/spec/native-desktop/draw3-integration.md`：唯一 Host、文档/历史归属与完成路径。
- `.trellis/spec/native/terminal-tail-reconstruction.md`：原始终点精确保留、raw64/模型128/160ms、完成副本与保护前缀、诊断不改变几何。
- 当前任务 `prd.md/design.md`：正常默认无诊断 I/O，不修改阶段三算法；用户新严重现场问题需调查，尚未人工验收通过。

## External References

本题判断依据均为当前仓库代码及用户日志，未使用外部资料；不以 Windows 驱动行为的记忆补全缺失现场输入。

## Caveats / Not Found

- 未找到本次异常的逐笔日志；未确认实际 deviceType（用户说没有用鼠标绘制，不能据此推定必须是普通 Pen 或 Touch）。
- 尚无证据证明 RTS 绑定/池复用错误、异常 Up 坐标、消费延迟或者渲染跨度复用中的任一种为根因。
- 其他 worker 正在调查 CPU/GPU 完成态；本文件只讨论输入路径，不重复该职责，不修改生产代码、工程或 git。
- 行号来自研究时版本；并行 worker 已在同一 StrokeGeometry 文件添加诊断，行号会移动，以函数名为准。
- 未运行 GUI、真实 RTS 设备或额外编译测试；本研究是静态定位，不作修复完成声明。
