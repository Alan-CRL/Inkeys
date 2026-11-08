# Research: 输入管线、终态覆盖与跨帧拓扑审计

- Query: 为什么白板惯性中新滑动可能卡顿、反向或突然加速；当前输入管线是否满足 Chromium 手势内核的前提。
- Scope: internal；只读当前生产源码、既有任务研究与测试。本文没有修改代码、编译、运行产品或打开 GUI。
- Date: 2026-11-08
- 基准: 主会话确认当前 HEAD 为 `7b213d90aa766ddec57f2bbddd92321bce58f79d`。
- 路径缩写: `Nav` = `Inkeys/Inkeys/Drawing/Draw3/Draw3.CanvasNavigation.cpp`；`Ctl` = 同目录 `Draw3.DrawingController.cpp`；`CI` = `Draw3.ContactInput.cpp`；`RTS` = `Draw3.RealtimeStylus.cpp`。行号来自当前工作树。

## Findings

### 1. 核心结论与证据等级

| 结论 | 等级 | 与用户症状的联系 |
|---|---|---|
| RTS 批末取样、回调到达 QPC、每指单槽覆盖、内核再按整段合并 Move | 源码直接确认 | 估速输入取决于帧率、回调调度和终态覆盖，不能把 Chromium 数学复刻等同于 Chromium 输入行为 |
| Up 可覆盖尚未消费的反向 Move，新的 Up 路径既不消费末段位移，也不将其纳入估速 | 源码可构造确定性反例；本轮未运行 C++ 复现 | 明确可产生尾段丢失、向旧方向启动 fling；比笼统“LSQ 不准”更具体 |
| Down 出队与 latest mailbox 读取没有共同消费截面；较新 Move/Up 可能先于较旧 Down 进入内核，全局时间门随后永久拒绝该 Down | 源码可构造合法并发调度；频率须真实日志确认 | 可导致加指失效或跨手势交接时新手按住而惯性继续，不是当前帧 stable_sort 能修复的问题 |
| 活动帧等待收到唤醒仍等到 deadline | 源码直接确认 | 所谓“接触立即停”实际是下一消费帧；增加停止和响应延迟，但单独不能解释方向错误 |
| 现有手感探针对末帧反向、卡顿后立即释放、跨读取阶段拓扑交错缺少覆盖 | 源码直接确认 | 解释为什么模型与探针通过却仍可能手感失真；不否认此前实测 PASS |

这些是可验证的机制，不是已经证明用户每次异常都由同一个机制触发。需要生产路径重放和真机输入日志把实际症状归因。

### 2. Files found / 相关规范

- `RTS:56-68, 859-883`：期望属性与解码表没有时间戳字段。
- `RTS:1243-1245, 1323-1325, 1416-1435`：Down/Up/Move 使用 `QueryQpc()`；Packets 只解码批末包。
- `CI:266-284, 775-807, 611-616`：Move 和 Up 写入相同 seqlock 最新快照槽。
- `CI:741-768, 827-846`：Down 独立入队；最新快照按 generation/route 防止槽复用和撕裂，但不提供多触点的原子截面。
- `Ctl:2748-2766, 2822-2882`：Down 被注册成 pending；每帧独立读取每指最新快照，构造样本后排序。
- `Nav:734-820`：全局单调 QPC 门、连续 Move 段合并和拓扑事件处理。
- `Nav:208-280, 393-428`：共享 LSQ2 tracker 与 Up 处理。
- `CI:1223-1251, Ctl:19972-20006`：活动帧等待。
- `Ctl:6406, 6447-6471, 6668-6725, 6992-7021`：生产探针 fixture、queued-hitch 旁路和验证边界。
- 相关规范：`.trellis/spec/native/runtime-and-rendering.md` 的 Free Canvas Navigation、已知输入阶段 1 限制；`.trellis/spec/native-desktop/input-and-ink.md` 的唯一 RTS producer、绘制输入与光标 mailbox 分工。
- 历史研究：`.trellis/tasks/11-06-whiteboard-canvas-pan-inertia/research/inertia-root-cause.md` 已把批末取样、单槽、到达时间列为结构性限制；此限制并非本轮才发现。

### 3. 三层样本损失，而非只有 RTS 的一层

当前路径：

```text
RTS Packets[N]
  -> 仅 lastPacket，qpc=回调执行时 QueryQpc
  -> PublishMove 覆盖每指 latest mailbox
  -> Up 再覆盖同一 mailbox
  -> 绘制帧读取每指一个最新 snapshot
  -> CanvasPanDynamics 将同一拓扑边界之间的所有 Move 合并成一次 detector.Move
  -> tracker 收到一个共享 movement
```

证据：

1. `RTS:1416-1417` 计算 `lastPacket`，`1433-1435` 仅解码该包；`1452` 调 `PublishMove`。
2. `CI:795` 发布单个快照；`266-284` 没有历史队列。Up 通过 `Close` 于 `616` 覆盖同一组位置/qpc/phase 字段。
3. `Ctl:2840` 每指读一次 latest，`2865` 只排序本帧收集到的数据。
4. `Nav:767-773` 每次 Move 只更新持有触点的位置与 `moveQpc`；`flushMove` 于拓扑边界或循环末才调用 `detector.Move`（`754-759, 775-776, 820`）。即使以后把所有原始 Move 放进 `contacts`，这里仍会再丢历史。

因此修复“全样本”必须同时覆盖 producer、跨线程传递、adapter 三处，不能只把 RTS 的 lastPacket 改成 for 循环。

`Nav:219-220` 对有重叠 id 的相邻已消费 movement 间隔 >=40ms 清空 tracker。由于目前 movement 间隔是绘制消费的结果，30 FPS 下一次约 33.3ms、稍有抖动即可跨过 40ms；此时即使设备持续 120/240Hz 上报，内核也会把“渲染没消费”误当作“输入停止”。这会导致刚卡顿后松手没有惯性、下一次恢复又突然有速度。

### 4. 终态覆盖反向 Move 的具体反例

单指沿 X，时间单位 ms，QPC 使用任意正常正起点：

| 时间 | producer 事件 | 消费情况 |
|---:|---|---|
| 1 | Down x=0 | 已消费 |
| 17 | Move x=48 | 已消费，越过 slop |
| 33 | Move x=96 | 已消费，前三点为 +3000 DIP/s |
| 41 | Move x=72 | 未到下一绘制帧 |
| 49 | Up x=48 | 覆盖 x=72 的 Move |
| 50 | 绘制帧读取 | 只看到 Up x=48 |

当前结果由源码可推导：

- `Ctl:2858-2861` 只向内核提交 Up，不补被覆盖的 Move。
- `Nav:803` 更新触点终态坐标，但 `detector.Up`（`393-428`）不派发末段 ScrollUpdate，不调用 AddMovement。
- tracker 仍仅看到 1/17/33ms 的正向位置；Up 距末样本 16ms，小于 `80ms`，不会被 `PointerUp` 清空（`252-255`）。
- `Nav:414-423` 使用旧的正向估速启动 fling，即便最后16ms手已经以 -3000 DIP/s 向反方向移动。

本例不需要推测浮点误差或 booster；旧方向释放是丢掉反向输入的必然结果。真实用户若先正向惯性、重新触摸后很短地加速/折返并释放，此缺陷可叠加 booster 或短窗口估速，使表现更复杂。

修复应优先保存并消费真实末段 Move；不能为了“修反向”简单将 Up 当成高速 Move 估速。Up 坐标可能有特殊噪声，且 Chromium 不纳入 UP 速度样本的选择本身不是错误；错误在于上游连它此前的真实 Move 一起丢掉。

与 `708cb6bc` 的关系（主会话独立 Git 审查反馈，研究代理未运行 Git）：

- `708cb6bc -> 7b213d90` 的生产改动仅在 CanvasNavigation 与 DrawingController；RTS/ContactInput 未改。
- 旧 `Ingest` 明确保留 Up 终点内容位移，但不追加速度样本，并通过 `CanvasPanReleaseGate(age)` 衰减释放速度。
- 新实现既取消旧的短样本保护/年龄软门限，也不计 Up 末段位移，因此相同上游损失的影响发生变化。不能把历史输入缺陷说成本提交新增，但可说新内核更依赖上游保真度。

### 5. Down 与 latest snapshot 跨帧乱序竞态

`Ctl:18207` 在导航前调用 `drainIngressBatch`，随后 `18213` 调导航，而 `CanvasPanInput::Step` 在 `2840` 独立读取每指 latest。Down 队列与 Move/Up 槽之间没有共同截止序号或快照冻结。Up 不需要单独消费队列项：producer 的 `Close()` 在 `CI:618-619` 已把 `Closing` 转成 `ConsumerOwned`，所以发布完成的 Up 满足 `TryReadSnapshot:843-844` 的终态可读条件，即使它发生于本帧 Down drain 之后。

合法调度示例（先前 A 已经是持有的平移触点）：

1. 绘制线程完成本帧 Down 队列 drain。
2. 输入线程发布 B Down，qpc=100；此 Down 留在队列里，尚未注册 B。
3. 输入线程发布 A Up，qpc=110。
4. 同一帧 `CanvasPanInput::Step` 读到 A Up110，内核处理 Up，把 `lastInputQpc_` 推到110；如果之前有速度便启动 fling。
5. 下一帧或帧后段才出队登记 B；下次内核 Step 的 B Down100 命中 `Nav:763` 的 `sample.qpc < lastInputQpc_`，被直接忽略。
6. B 后续 Move 找不到 held key，在 `Nav:770` 被忽略；B 的 Up 也找不到 key。控制器仍知道 B 物理按住，但内核不知道它存在。

A 与 B 在时间100–110有真实重叠，所以它本应是 POINTER_DOWN -> POINTER_UP，而不是释放后缺失的新手势。当前帧内排序无法修复跨帧已经推进的时间线；简单删除全局 QPC 门也不正确，因为 tracker 时间和上一帧已产生的滚动无法任意倒回。

同类竞态在“Down100未出队，A Move110已被读取”的场景会表现为新增手指永远不参与质心；必须用生产协调器的受控调度验证，而不是只给内核一次性提供正确排序向量。

最小修复原则：对导航消费建立统一有序事件流或统一发布水位线；Down/Move/Up 属于同一事件序列，消费者不能越过尚未交付的拓扑事件。若短期仍保留 mailbox，应定义稳定截面/重试机制，不能只加一次额外 drain（仍有竞态窗口）。

### 6. 时间戳与短窗速度风险

- 当前期望属性表没有请求时间字段（`RTS:56-68`），decoder 也无时间字段（`859-883`）。不能直接声称现有包具有可信硬件时间戳。
- 到达 `QueryQpc()` 位于解码调用处；此前包含 callback gate、绑定查找/状态锁等工作。回调延迟或批量到达会使位置间隔与时间间隔不对应。
- 新 tracker 两点即可给速度（`Nav:273-276`），没有最小样本跨度；按轴限幅为17000 DIP/s。若物理位移几十DIP在回调中只间隔亚毫秒/几毫秒，即使两点位置有效，估速也可能达到上限。
- 这是确定的放大机制，但“当前机器发生了多大时间压缩”仍缺真机日志，不能编造发生概率或硬件报告率。

建议在实现前先探测实际 RTS packet description 与可用时间来源；可信硬件时间存在时统一映射到 QPC，记录 wrap/epoch。若只能得到到达时间，保留其可信度标记，采用明确的退化估速策略，不把批内点强行赋同一时间，也不凭假设均分成完美240Hz时间线后宣称保真。

### 7. 活动等待没有提前返回

`CI:767` Down 会 SignalWake；`CI:639` Up 也 SignalWake，且注释声称“终态必须打断活动帧等待”。但 `WaitForFrameDeadline` 的循环只按 deadline 决定返回（`1234-1251`）；`WaitForSingleObject` 返回事件后，代码继续下一轮等待。它与检查 wakeGeneration 的 `WaitForWake`（`1189-1220`）不同。

导航活动时生产调用点为 `Ctl:20006`，所以 Down/Up 并不会像注释说的那样提前结束等待。最坏还要等剩余帧预算，另加当帧渲染工作。这个缺陷解释触摸停止和新滑动响应迟缓，不能单独解释错误方向。

修复宜限定拓扑/控制唤醒可提前返回，普通高频 Move 继续维持呈现节拍，避免为了“早醒”让所有绘制和导航无上限空转。需要证明无丢唤醒、控制栅栏顺序不变、idle 时仍可阻塞。

### 8. 当前测试为何没有拦住

- 生产探针 `Frame()`（`Ctl:6447-6471`）同步执行“全部 Down 出队 -> Step”，测试主体没有在两步之间插入 producer 回调，所以无法捕获 §5 的竞态。
- `QueueLatestMovesForProbe`（`2807-2818`）是探针专用“保存每次 latest”的旁路，产品仍是单槽。`Advance` 在 queuedHitch 模式于每次 publish 后额外抓快照（`6683`）。
- 但内核仍将这些连续 Move 合并，所以 queued 输入数量不能直接当作 tracker 样本数量。
- `Drag` 的速度稳定断言仅在120ms之后、非hitch、前后known时成立（`6709-6715`）；hitch 放在拖动中段（`6700`），最终释放前已有恢复时间（`7000`）。因此它不覆盖“最后一帧卡顿后立即释放”。
- `coast-reverse` 重新按下后反向拖400ms（`6919-6930`），有充分时间冲掉估速窗口；不覆盖16–40ms末段反向后立即Up。
- `topology` 测试特意额外给120ms稳态输入后才验证释放（`6952-6954`），不覆盖刚变拓扑就释放。

建议把模型黄金值与产品行为测试分开：模型测试可以严格匹配Chromium；产品测试必须走真正的事件发布/消费管线，并针对真实用户不变量断言，不以更换实现常量后同步调整预期作为手感通过的依据。

### 9. 推荐最小修复分段与无GUI验收

1. **先补定向失败测试**：旧正向点+末段反向Move被Up覆盖；Down drain后插入新Down再插入旧Up；帧停顿40/100/250ms后直接Up；两点回调时间压缩；同一输入轨迹以30/60/120/240fps消费。
2. **统一导航事件时间线与末段保留**：producer保留有界历史、拓扑事件不被覆盖，使用接触generation和事件序号；帧一次取确定截止的有序批；溢出显式标记速度不可信并安全重建基线。不要破坏现有绘制消费者的单槽合同或控制栅栏。
3. **修adapter**：按可信时间/采样帧组建多指movement，不再“整个渲染帧连续Move只产生一个tracker样本”；允许合并呈现结果，不合并丢失估速历史。异步触点时不可假称旧位置是新时刻测量值，需单独评估逐指估速还是同步frame模型。
4. **终态前处理真实Move**：Up仅闭合；尾段位移和释放速度来自已保留真实Move。无可靠尾段时退化应可诊断，不能继续拿旧方向高速释放。
5. **拓扑/控制早醒**：在统一时间线保证后接入活动等待提前返回，保留帧率和idle策略。
6. **再评估估速与booster参数**：输入未修好前不宜继续用加大门限、加快摩擦掩盖问题；物理尺度工作可并行设计，但速度必须先有可信输入。

必须保留：落指取消惯性、笔hover/落笔停止、活动Touch对Pen的既有抑制、Touch/Mouse设备互斥、Stop后的旧接触不复活、命令栅栏和handle generation保护。

验收至少观察四层日志：producer原始样本时间/到达时间/批数 -> 消费事件序号/丢弃原因 -> tracker样本数/原始速度/boost后速度 -> 已应用viewport增量。只有“帧输出速度”不足以定位丢样本或时间压缩。

### 10. 以导航专用旁路隔离改动，不重写全局绘制输入

建议优先采用共享 `ContactInputCoordinator` 生命周期下的 pan 专用有界事件旁路：现有 latest mailbox 继续服务绘制、橡皮等消费者，其语义不变；导航只读专用流。复用已有 handle/generation、准入 revision、关闭与控制栅栏语义，避免另外造一套不相关的触点身份。

隔离的必要约束：

- 启用旁路不能等到绘制线程 `CanvasPanInput::Register` 后才开始收集。producer Down 到 consumer Register 之间可能已经有多个 Move 甚至 Up；必须在producer端Down即保留首段，或在record中保留足够的预登记历史，再由已准入的导航消费者领取。
- 模式变化仍需控制栅栏：只允许被移动模式准入的contact驱动导航；仅采集历史不等于批准导航。离开模式/翻页/笔停止必须废弃旧generation的数据，避免队列让本已正确的Stop退化。
- 多producer是否存在、队列publication与序号分配如何配合，须依照实际RTS/Mouse/控制线程所有权选择；不能未经核实就称为SPSC。
- 有界流要支持固定消费截止（已连续发布水位，而非仅“已分配的最大序号”）；以免读到后写入事件而越过尚未publish的Down。
- 溢出时显式取消受影响的导航手势并抑制该手势fling，记录丢样本；不能静默丢Down/Up，也不能把缺口后的短时间差拟合成高速。
- 保留实际采样时间和到达时间两条信息；没有硬件时间时不合成虚假的高频时间戳。可先只保留每次回调末样本、修单槽/终态/跨帧顺序，再以独立阶段引入RTS批内可信时间；后一步不应成为第一步交付的阻碍，但不将前一步标称完整保真。

输入完整性修复也不是充分条件：二次拟合在短折返后仍可能输出旧方向速度。主会话独立oracle已观察短反向释放场景，应在保真管线之上单独设计释放方向一致性策略与测试，不能承诺“排队后所有反向问题自然消失”。这类策略是产品对Chromium数学模型的有意差异，需要写入设计并评审。

## External references

- 本研究结论来自本地生产源码，不依赖未核实的外部 API 或 Edge 出货配置。
- Chromium参考副本由主会话提供：`C:/Users/alan-/AppData/Local/Temp/inkeys-codex/chromium-src`；本研究没有将本地适配行为默认视为Chromium上游保证。
- 主会话已查Windows官方 [PacketProperty GUIDs](https://learn.microsoft.com/en-us/windows/win32/tablet/packetproperty-guids)：`GUID_TIMER_TICK` 表示packet生成时间。可作为后续能力探测入口，但其单位、设备支持、property metrics、回卷与重置仍需验证；本文未承诺某设备一定可用。无timer时每回调仅给一个标记为低可信度的到达时间速度点，批内几何可保留，不挤入多个伪QPC。Pointer history仅为后续可选项，不建议本轮更换输入后端。

## Caveats / Not Found

- 本轮没有运行编译、C++测试、探针或GUI；§4/§5是源码推导的确定性复现场景，不冒称已执行失败测试。
- 未获得用户实际异常的原始输入日志，故不能判断各机制的发生占比。
- 本研究角色禁止Git操作；提交间差异来自主会话独立只读Git核对的反馈，已明确标记。
- 未对绘制性能/tile恢复做完整审计；渲染卡顿仍可能参与，但不能据此忽略已经确认的输入损失。
- 最终队列拓扑、容量、时间戳退化和多指同步策略应先进入设计评审；这会触及共享输入与绘制线程，不宜在本轮只调查的授权下直接实现。
