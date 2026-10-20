# Research: 大曲率转弯立即抬笔的本地历史与当前生产路径

- Query: 复查此前末钩/拐动的撤回方案，追踪主产品 Draw3 的 raw 输入、运动/停止/Up 模型输出、prediction、endpoint admission、L0/L1、完成尾段和 Stored，区分已修复的欠阻尼过冲与未解决的运动阶段外偏。
- Scope: internal
- Date: 2026-10-12

## Findings

### 1. 结论与证据等级

1. **历史用户结论：问题仍未解决。** `09-19` 最新 PRD 明确记录用户复测：高速大曲率转弯时物理轨迹偏内、建模墨迹偏外，立即 Up 回接真实终点形成拐动；之前自动化通过不能代表解决。用户当时要求暂缓，撤回局部 cubic 补偿。证据：`.trellis/tasks/09-19-draw3-low-speed-stop-convergence/prd.md:5-9,29-30,53-56`；`design.md:7-8`；`implement.md:5-14`。
2. **当前源码确认：终点门禁解决的是新增尾段的越界/回摆，不是整个运动中心线的曲率拟合。** 普通 Tracking 将所有新的 model Result 增量转入 `realPoints`；仅 terminal 或已有 settling/recovery 状态进入受限 scratch。终态门禁只检查到 endpoint 的距离与沿 approachDirection 的投影，没有角度、曲率或真实轨迹走廊约束。证据：`Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:9218-9244`；`Draw3.StrokeGeometry.cpp:1535-1587,1653-1682`。
3. **推断：** 运动期已形成的外偏，加上强制精确 raw Up、保护 pre-Up 确认尾段，允许几何上出现短横向回接。这解释当前策略为何没有排除用户现象，但没有设备全轨迹或实际模型曲率复现，不能把“模型外偏”精确归因到 spring、wobble、Kalman、采样任一单项。
4. **最新基线：** `10-11` 的弧长/笔身直径笔锋修正只调整派生半径与既有线段内的插值连接点，保留模型中心线路径和 L1 游标；不能计为高速曲率中心线修复。证据：`.trellis/tasks/10-11-pen-taper-shape-and-stop-smoothing/design.md:4,7,11-15`；当前 `Draw3.StrokeGeometry.cpp:172-231,490-531`。

这里的 `realPoints` 表示被产品接纳的模型中心点，**不是原始设备点数组**。不能因名字是 real 就把它与物理手轨迹等同。

### 2. Files found

| 文件 | 作用 |
| --- | --- |
| `.trellis/tasks/09-19-draw3-low-speed-stop-convergence/prd.md` | 用户确认有效部分、末端拐动未解决、撤回 cubic 的最新要求 |
| `.trellis/tasks/09-19-draw3-low-speed-stop-convergence/design.md` | 当前批准设计优先级，以及历史 endpoint monotonicity 方案 |
| `.trellis/tasks/09-19-draw3-low-speed-stop-convergence/implement.md` | 已撤回 `CorrectPenTerminalHook` 及其专属断言；保留哪些验证 |
| `.trellis/tasks/09-19-draw3-low-speed-stop-convergence/research/release-tail-thin-coverage.md` | 单调门禁仍会有切线突变的反例及撤回原因 |
| `.trellis/tasks/09-19-draw3-low-speed-stop-convergence/research/modeler-stop-up-semantics.md` | 历史 spring/Move/Up/Predict 语义调查；代码行号是当时基线 |
| `.trellis/tasks/09-19-draw3-low-speed-stop-convergence/research/git-regression-analysis.md` | 既有 Git 历史调查：早期冻结可见 L0 与后来的 model Up 尾段路线 |
| `.trellis/tasks/09-19-draw3-low-speed-stop-convergence/research/dwell-appearance-and-resume.md` | 中心线位置与停笔半径老化分离 |
| `.trellis/tasks/10-11-pen-taper-shape-and-stop-smoothing/design.md` | 最新生产笔锋弧长形状和 Up/Stored 合同 |
| `.trellis/tasks/10-11-pen-taper-shape-and-stop-smoothing/research/{implementation,verification}.md` | 生产 CPU probe 的实际覆盖、结果和边界 |
| `.trellis/workspace/AlanCRL/journal-1.md` | 08-02 实时笔锋恢复/抬笔定住记录 |
| `.trellis/workspace/{AlanCRL,codex}/index.md` | Journal 覆盖日期和会话索引 |
| `.trellis/spec/native/runtime-and-rendering.md` | 现有收敛/Up 合同，明确高速大曲率问题未解决 |
| `.trellis/spec/native-desktop/input-and-ink.md` | 最新笔锋弧长及中心线不改写合同 |
| `.trellis/spec/native-desktop/draw3-integration.md` | 真实生产路径、固定模型库/头文件 ABI 来源 |
| `.trellis/spec/guides/cross-layer-thinking-guide.md` | 输入、模型、稳定前缀、文档先成为真值的跨层边界 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp` | RTS 包最后一点解码、QPC 和 Publish Move/Up |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.{cppm,cpp}` | contact 最新快照与一致读取 mailbox |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.InkPrediction.cppm` | ActiveStroke/EndpointAdmission/terminal 边界状态 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.StrokeGeometry.cpp` | gate/pin、派生 L0、完成尾段及 Stored 求解 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` | Tracking/settling/terminal 调用组装、L1 和文档提交 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.PenDiagnostics.h` | 隐藏诊断字段和最多 8 个尾段位置 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp` | 隐藏诊断回调安装、最新快照保存 |

### 3. 历史回查：不可重新混为一类的问题

#### 3.1 已修复/保留的路线

- 早期 RTS 多 contact 迁移后，无新 snapshot 的帧仅 Predict，未推进模型。09-19 恢复 stationary 同点 Move，并同时检查模型位置与剩余速度，不再把“连续三帧同样 prediction”当作收敛。历史说明：`prd.md:15-20`；当前代码：`Draw3.DrawingController.cpp:12909-12966`、`Draw3.StrokeGeometry.cpp:1318-1344`。
- 恢复模型推进会暴露 spring 欠阻尼的过冲/回摆。历史研究从当时参数/方程推导阻尼比约 0.663，明确这是数学推导；普通 Move 和 Up 的前置线性更新均可能产生过冲。见 `research/modeler-stop-up-semantics.md:26-36,40-48`。本研究未重新审计静态库实现，不能将该历史 source 行号当作当前产品 `.cc`。
- 随后引入 `modelScratch`、最新内部 Result 和 endpoint admission：内部仍推进，但新增可见中心线仅接纳严格接近的前缀，首次触边/越界/不再接近时 pin 精确 endpoint。已 pin 的后续内部回摆不增长几何。当前代码：`Draw3.InkPrediction.cppm:413-485`、`Draw3.StrokeGeometry.cpp:1535-1587`。
- 模型时间与显示时间分离，长静止不造成巨大积分增量；停筆半径继续老化，恢复固定旧停点后过滤惯性前缀。当前代码：`Draw3.StrokeGeometry.cpp:1596-1650,1364-1382`；规范：`native/runtime-and-rendering.md:421-427`。
- 物理 Up 锁住显示年龄；等待续接/延迟完成/Stored 不继续养粗；成功续接解除。当前代码：`Draw3.StrokeGeometry.cpp:419-446`、`Draw3.DrawingController.cpp:9187-9198,9022-9027`。

这些安全和时钟修复应作为后续方案回归边界保留，但其通过记录不是曲率外偏问题的通过记录。

#### 3.2 放弃/撤回的路线

- **`CorrectPenTerminalHook` / 仅 Up 新增后缀 cubic 重建已删除。** 既有实施记录明确两侧移除此 helper、cubic 及专属断言，保留最多 8 点的只读终态诊断（`09-19/implement.md:11`）。撤回原因是 Up 前外偏未修，却重分配确认尾段曲率/弧长半径；没有实际收益证据（`design.md:8`；`research/release-tail-thin-coverage.md:5-7,19`）。当前生产 geometry 没有该 helper。
- **无条件 completed taper floor 已被后续批准设计取代。** `09-19/design.md:16,29-33` 明确当前批准设计优先，完成态和活动态共享显示时间规则，停稳同位 Up 不重新收尖。不能复用旧实施记录 `implement.md:118,141` 的 fully-developed floor 作为当前合同。
- 历史拒绝默认全局调 drag/spring、关 Kalman/切 StrokeEnd、shader 只隐藏错误、只改末点和中途 Reset；理由分别涉及全局手感、无法限制真实 Up 中间输出、Stored 不一致、残留 loop、预测/压感历史断裂。见 `09-19/design.md:130-136`。此次研究可重新比较这些策略，但不能将历史未授权的算法变更当作已经批准实施。
- Git 历史的“freeze last visible L0”是另一策略，不是当前合同：既有研究记录 `a40801a5` 避免 kUp 重连，`3d52b33d` 后又默认改回 model Up + 清 prediction，`1cc746e7` 固定为仅确认点进入 Stored（`research/git-regression-analysis.md:29-49,90-97`）。此次未运行 Git 命令独立复核这些提交，按既有历史研究证据引用。

#### 3.3 Workspace 日志覆盖边界

在两个 workspace journal/index 中搜索停笔、cubic、曲率、外偏、末钩、末端等，仅找到早期 08-02 笔锋政策摘要：`.trellis/workspace/AlanCRL/journal-1.md:411-426` 记录 Mouse/Touch 实时笔锋恢复和真实尾 + tip、去 prediction。AlanCRL 索引最后活跃 09-04（`index.md:10-12`）；codex 索引最后活跃 10-10（`index.md:10-12`），索引无 09-19 此任务会话。09-19 的具体撤回与用户复测结论保存在任务 PRD/design/research/implement 中，不应因 journal 缺项而说历史问题不存在。

### 4. 当前生产数据流与 code patterns

```text
RTS 最后 packet / Mouse 原始事件
  -> ContactSnapshot 最新 mailbox
  -> consumeLatestSnapshot 的 model Input
     Tracking: modeledResults -> AppendNewModeledPoints -> realPoints
     settling/terminal: modelScratch -> endpoint admission -> realPoints
  -> 未结束: realPoints 可变后缀 + prediction -> 派生 L0
             超过保护窗口的 realPoints -> L1（提交游标单调）
  -> 结束: 清 prediction -> BuildCompletedPenTail（只取 realPoints，派生 taper）
  -> FinalizeStoredStroke（稳定前缀 + 完成尾段）
  -> Canvas/History 先追加 -> 从同一 Stored Stroke 首次画 L2 -> 保存/重放
```

#### 4.1 真实输入和采样不能忽略

- `ContactSnapshot` 保存位置、stylus 状态、QPC、phase、sequence（`Draw3.ContactInput.cppm:56-71`）；mailbox 发布写同一组位置/时间字段，使用奇偶序号一致读，没有普通笔历史点数组（`Draw3.ContactInput.cpp:265-318`）。
- RTS `Packets` **只取每个回调包最后一个 packet**，以回调 `QueryQpc()` 填 timestamp，再发布 Move（`Draw3.RealtimeStylus.cpp:1357-1375,1388-1393`；`:1002`）。因此它不是对 packetCount 中每个设备样本逐点建模，也没有在此路径读取每个 packet 的采样时间。
- Move 最新覆盖，不唤醒活动帧（`Draw3.ContactInput.cpp:789-801`）。绘制线程 `consumeLatestSnapshot` 只读取新的 sequence（`Draw3.DrawingController.cpp:9096-9104`）；多次 producer Move 可在消费前被后来的 Move/Up 覆盖。
- 消费位置变化超过 `0.25px` 或 stylus 状态变化才进入普通 Move 模型；真实速度由消费到的相邻 raw 距离/QPC 一次滤波（`Draw3.DrawingController.cpp:9146-9181`）。
- RTS Up 独立解码实际 Up 包并调用 `PublishUp`（`Draw3.RealtimeStylus.cpp:1267-1296`），可能给模型一个与已消费运动位置不一致的最新 endpoint；这是正确的终点权威，但可能放大最后回接的差异。
- **受限推断：** 丢弃包内中间采样和帧间合并会改变有效转弯曲率/时间，可能放大稀疏拐弯误差。未进行完整采样与 coalesced 对照，不能据此宣称它就是外偏根因；也不能认定全部设备用同样 packet batching。后续需比较“完整样本输入、包末点输入、实际帧消费输入”三条模型轨迹。

#### 4.2 Tracking / settling / recovery / Up

- `updateContactModel` 对普通 Pen 直接调用模型 Update；特殊长 gap reanchor 仅 Touch SpeedEraser，此题不能误套（`Draw3.DrawingController.cpp:8163-8191`）。
- `boundedEndpointUpdate = endpointTool && (terminal || endpointAdmission.active)`；常态运动 Update 的输出未经过 endpoint gate，`AppendNewModeledPoints` 把新 Result 逐个转换进确认点（`:9218-9244`；`Draw3.StrokeGeometry.cpp:1653-1682`）。
- 超过一个目标帧间隔的空帧才开始 endpoint settling；stationary 同点 Move 使用最近接受的 `lastModelSnapshot`，不刷新真实运动年龄（`Draw3.DrawingController.cpp:12909-12966`）。
- `BeginEndpointAdmission` 的 approachDirection 通常由**可见末点指向 raw endpoint 的弦**产生，距离极小时才从末段找方向；它不是末端 raw 输入切线（`Draw3.StrokeGeometry.cpp:1499-1525`）。
- 当前接纳条件：距离必须比 previousDistance 至少改善 `1e-6px`，离 endpoint 大于 `0.05px`，前向平面投影不超过 `0.05px`；第一个不满足的候选结束 prefix，pin raw endpoint（`:30-31,1544-1587`）。旧历史设计中的“距离可允许 +0.05px”描述不能替代当前代码的更严格改善检查。
- `PinEndpointGeometry` 仅能替换未提交、且物理 Up 后新追加的近点；在 `terminalFirstPoint` 之前的最后真实点不改，必要时另加精确 endpoint，半径沿用最后有效值（`:1438-1462`）。**即使最后段为横向回接，这里也没有 tangent 连续性检查。**
- Up 首次设置 `terminalFirstPoint = realPoints.size()` 并锁住 physical display time、清 prediction（`:419-429`）。直接 Up 与 deferred completion 的 `kUp` 批次均 scratch + 同一门禁（`Draw3.DrawingController.cpp:9187-9241,9004-9063`）。deferred Up 起初可送 `kMove`，之后真正完成时再送 `kUp`；这两步不能混为某个 predictor 自动完成。
- 失败回退也只补 raw 终点，不替换 pre-Up 保护点（`Draw3.StrokeGeometry.cpp:449-463`）。

#### 4.3 Prediction、L0、L1

- 预测半径继承最后确认宽度；只预测位置，`RebuildPredictedPoints` 不把 predicted pressure/velocity 用作笔宽（`Draw3.StrokeGeometry.cpp:1685-1703`）。
- 活动 L0 从 `committedIndex` 开始复制确认尾段，再插 prediction，应用 taper 和半径公切线投影；旧和新 bounds 需参与 dirty（`:1712-1738`）。taper 影响 `r`，最新插值点仍在原线段上（`:218-228`），不修复中心线弯折。
- L1 保护 `liveTipDuration + predictionDuration`，提交游标只前移，L0/L1 共享连接点（`:1741-1753,1784-1785`）。endpoint admission 活跃时停止新增稳定前缀，停笔安全点不被提前提交（`Draw3.DrawingController.cpp:13294-13302`）。
- 普通 Pen、硬件压感 Pen、HardPen 的保护/模拟收锋行为不同：HardPen 的 live-tip protection/taper 为 0；普通 Pen 保护窗口仍按配置，与是否硬件压感收锋分离（`:13264-13270`）。若选择可变尾重拟合，不能假定所有工具拥有同样可用窗口。
- Up 后 clear predictedResults/predictedPoints/L0，再生成确认完成态（`:13317-13323`）。故最终中心线的折弯不能直接等同于“预测点被保存”；但抬笔前 prediction 消失也可能贡献可见跳变，须把上一帧 prediction 与最后 Stored 对照。

#### 4.4 完成、Stored 和位置/半径边界

- `BuildCompletedPenTail` 从 `realPoints[committedIndex..]` 复制尾段，只做当前派生 taper 与 capsule 半径约束，没有 curve reconstruction（`Draw3.StrokeGeometry.cpp:490-504`）。
- `FinalizeStoredStroke` 保留稳定前缀，使用完成尾段替换共享连接点并拼接；转换 `r -> width=2r`（`:514-531`）。因此门禁后的横向回接会进入 Stored，持久重放不自动校正。
- 产品先把这同一 InkStroke 追加 Canvas/history，再从刚追加的对象执行首次 L2 raster（`Draw3.DrawingController.cpp:1833-1849,13449-13471`）。后续方案必须把修复送进这一共同真值，而不能仅在 shader/L0 遮盖。
- 10-11 长度约束改变锋尖宽度、停筆老化和有效长度封顶，可能改变折弯视觉显著程度；`r` 变化与中心坐标误差应分开测量，不能用“细尾没有凸包”替代“切线没有拐动”。

### 5. 现有诊断能证明什么

- `CapturePenTerminalTrace` 最多均匀采样 8 个 modelScratch 和 accepted 尾段位置，accepted 包含 pre-Up 连接点；不存这些点的 time/velocity/radius，不存整段运动 raw 历史（`Draw3.StrokeGeometry.cpp:466-486`）。
- `PenRuntimeDiagnostics` 仅有 rawMove/rawUp、最多 8 点 modelTail/acceptedTail、终点误差、半径和计数（`Draw3.PenDiagnostics.h:9-32`）。`rawMove` 锁自最近用于真实测速的消费 snapshot（`Draw3.DrawingController.cpp:9188-9190`），不能认定是设备最后全部 Move 中的末点。
- 诊断由 controller 按 consumed runtime 发布（`:7663-7699`）；Host 仅隐藏注入测试安装回调并保存最新快照（`Draw3.Host.cpp:924-929,1590,1907`）。它可以观察消费/接纳的终态，不是设备所有 packet 的原始录制。
- 即使打开 `DRAW3_RTS_DIAGNOSTICS`，`RecordCallback` 从收到的一个 packet 指针提取一个 rawX/rawY（`Draw3.RealtimeStylus.cpp:1493-1525`），Packets 调用处传 `lastPacket`（`:1395-1397`）。它知道 packetCount，但当前此段日志仍不包含包内所有轨迹。
- 10-11 probe 记录 72 model/36 resume、331063 断言、0 failures，但明确只执行 CPU 模型/geometry/helper/完成/Stored，不执行完整 DrawingController owner loop、GPU 或实体设备（`10-11/research/verification.md:12-18,33-35`；`implementation.md:14-16`）。曲线数学用例、最终点 pinning 与 Stored 一致不是运动曲率问题的真实复现。

### 6. 后续候选策略与本地限制

| 候选 | 应解决的部分 | 本地限制/风险 |
| --- | --- | --- |
| **保留稳定前缀，在有界未提交尾段内用 raw 辅助约束重拟合，终点和两侧切线共同约束** | 可在 Up 前外偏已形成但尚未提交时重新分配回接，而非只修 Up 新增几个点 | 需要近期 raw 历史；不能改已提交 L1 前缀；当前 terminalFirstPoint 保护 pre-Up 点，需明确设计允许改哪个未提交子区间；短划/HardPen 可用窗口可能不足；修复必须共用 L0/完成/Stored 和 dirty。更合理的研究方向，但尚未证明收益 |
| **运动期改变位置模型/自适应参数或 raw-aware 约束** | 从运动阶段减少偏差，而不是只补终点 | 改动面和跟手/稳定性风险大；临界阻尼仍有相位滞后；数据必须先证明 spring/wobble/采样哪一层贡献。库变更需三架构头文件/固定 ABI 同步，不是只改一行参数就可称修复 |
| **输入保真：有界保留/消费包内与帧间样本，再比较同一模型** | 排除/降低 sparse curve 和末点合并的输入损失 | 未证明是根因；真实 packet 时间未知时不能伪造精确 timestamp；需保持 producer 非阻塞和预算、终態准入顺序。优先作为对照研究，不直接承诺产品改队列 |
| **终态增加切线/走廊检查** | 检出距离和平面安全但方向突变的末钩 | 单纯更严过滤可能只剩更长的 pin 直连，反而加大角跳；没有可重分配尾段时几何约束可能互不兼容，不能只丢点就称修复 |
| **只重建 Up 新增 cubic / 改半径遮盖** | 某些纯终态人工反例可能改善 | 历史已撤回；不修 pre-Up 外偏；弧长改变会连带收锋比例与压力半径分布；需要新的明确收益证据方可重新考虑 |
| **冻结抬笔前最后可见 L0** | 消除抬笔瞬间的形变/跳动 | 历史存在此方案，但精确 raw Up 可能失配，可能保存 prediction。直接采用违反当前 raw endpoint 与 prediction 不持久合同；只能用作诊断对照或用户重新选择的产品取舍 |
| **升级有明确针对性的上游修复** | 若上游修正运动/Up 机制，可减少自有补偿 | 需核对实际 fix 范围、源版本与本地固定 binary，而不能把改 README/头文件视为库升级；本主题不作上游状态结论 |

当前最有价值的前置比较是：同一时间基准下完整 raw、包末点 raw、消费 raw 的 position-model output，以及预测、gate 后 real、L0、Stored 的轨迹。先测运动中有符号内/外偏、Up 前后切线角、raw Up 接回向量、稳定锚点可用距离，再决定采用输入保真、运动阶段补偿或可变尾重拟合。只测 back() 精确到 endpoint 会漏掉用户现象。

### 7. Related specs

- `.trellis/spec/native/runtime-and-rendering.md:400-429`：模型与视觉收敛分离、门禁/恢复、物理 Up 时间和未解决的大曲率问题；`:424,448,462` 明确不能以 cubic/末点到位宣布修复。
- `.trellis/spec/native-desktop/input-and-ink.md:645-652`：最新收锋规则只作用派生半径、L1 首点和真实中心线不改、完成与 Stored 同解法。
- `.trellis/spec/native-desktop/draw3-integration.md:5-8,23-34`：固定库 provenance 和升级需要成套 ABI；`:64-65` 绘制线程拥有模型、几何、文档。
- `.trellis/spec/guides/cross-layer-thinking-guide.md` 的 Model to render points/Layer composition：提交游标、latest snapshot 节奏、Stored 真值、共享 L0 dirty 和不同生命周期。

### 8. External references / versions

本主题只做本地历史和当前产品源码调查，未进行互联网查询。上游是否发布修复由同任务的上游研究独立核查，不能从本地 spec 推定。已读取的产品集成规范固定来源为 `Alan-CRL/Inkeys3-Draw3@8d045298eaaac76f752b4f8b5f3303b3520e50b7`（`draw3-integration.md:5,24`），该值是来源快照，不等同 Google upstream 精确 commit，也不证明当前 binary 的实际内容；既有 09-20 报告的 SHA-256 见 `09-19/research/git-regression-analysis.md:14-17`，此次未重算。

## Caveats / Not Found

- 未修改产品、测试、第三方、规范、既有任务或 workspace；只写本任务 `research/local-history-and-current-path.md`。未运行 Git 操作、编译、测试、GUI 或 Computer Use。
- 当前产品源码可确认数据流和缺少切线合同，但没有本次高速转弯的完整物理 packet trace 或当前 model 的定量复现。历史用户观察不是每个设备/轨迹的普遍数值结论。
- 对现有 JSON/轨迹反例仅交叉引用主会话生成的 `research/gate-counterexample.json`，未重复计算。它证明门禁数学上不保证切线，而不是当前硬件/模型实际复现。
- 历史研究使用当时代码行号，有些“当前无门禁”“全部 kUp 被追加”“completed floor”描述已被后来代码取代。本文按当前生产文件重新追踪，不把这些历史表述原样套到今天。
- 没有在 workspace journal 中找到 09-19 末钩撤回专门会话；最强本地来源是该任务的最新 PRD/design/research/implement。没有未经验证的未来任务选择或实现授权。
