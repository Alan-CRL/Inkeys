# Research: Draw3 输入、RTS、队列与准入的持久停滞审计

- Query: 扩大到全部输入/RTS/队列/controller admission 路径，查找触发后画布长期不工作、UI 与设置仍可使用的确定缺陷；逐条证明入口、持续条件、恢复出口，优先提供可脱离 GUI 复核的原码实验。
- Scope: mixed；HEAD 工作树源码静态审计、真实源码提取的独立 headless probe、微软一手 API 资料。没有使用视频表现排除其他代码路径。
- Date: 2026-10-15
- Active task: `.trellis/tasks/10-14-canvas-freeze-investigation`
- Boundary: 本研究代理未修改产品、未执行 Git、未编译或启动 GUI。只写本任务 research。本轮独立 probe 由研究代理生成，编译/执行交主会话；下文区分源码证明、条件缺陷与尚未得到的运行证据。

## 主会话运行更新

2026-10-15：独立ARM64编译与实际运行均退出0，`Build/CanvasFreezeInvestigation/input-wake-probe.log`记录所有断言PASS、probe_failures=0。覆盖Touch组件与原码等待片段；真实Pen/RTS/完整Run未动态覆盖。生成器保存在research，较大的重复摘录C++输出在忽略的Build/CanvasFreezeInvestigation目录。

## Findings

### 1. 审计结论及证据等级

1. **找到新的、普通绘制操作可达的无限重复等待：速率橡皮接触静止收敛后，Touch/Pen Move 没有 wake，而 controller 在同一旧 generation 上重复等待。** 它能保持到真实 Up/Cancel、其他有效 wake 或退出到达；不能据此宣称“一次触发后所有新笔画永久失效”。它是确定的等待协议缺口，且隐藏测试额外 wake 会掩盖。
2. **真正能持续阻断所有后续 Down 的输入逻辑状态仍需要额外前提：至少一个旧 contact 的终态永久缺失，且命令边界已关闭 ingress。** 新设备的完整 Down/Up 不能终止另一个旧 identity。仅说“Up 丢失”不够，必须取得旧 runtime/route 的证据，并排除 duplicate Down、RTS lifecycle reset、admission 换代这些恢复出口。
3. **同步诊断输出本身具有确定的无限等待契约。** controller 直接写 stdout；管道缓冲满且读端存活但不读时不会返回。它保留 Host.running=true，也不要求 UI 线程停止。主会话已完成完整程序复现/取栈与恢复对照，见 expanded-investigation.md F1；本研究不据代码代替该实验证据。
4. **没有从当前应用内锁图证明 RTS writer 的普通必现自死锁。** writer 包住 COM/USER32 查询是实际无界边界，但必须再证明系统调用重入或不返回，不能把“锁中调用 COM”直接当成锁环。
5. **单纯 Run 返回、WM_CLOSE、恢复失败后 break 受到主线程监督。** 在清理能完成时主进程受控退出，不能解释持续可用的 UI；清理先卡住则是另一个需现场栈确认的路径。

| ID | 状态 | 可达触发 | 无限持续所需条件 | 已有恢复出口 |
| --- | --- | --- | --- | --- |
| I-WAKE | 原码组件实验通过；真实 Pen 未覆盖 | 选择 Speed 橡皮，Touch/Pen 接触保持不动至收敛，再按住移动 | Move 没有额外 cursor/control wake，原码 while 持续使用同一 generation | 同 identity 真 Up/Cancel、任意新 generation、ExitRequested+wake |
| I-ORPHAN | 条件状态可证明；真实丢终态触发未证 | 已被 controller 接受的 A 没终态；随后接收一条 canvas command | A 不再产生可识别终态/duplicate Down，RTS 不做 lifecycle reset，admission 不换代 | A 的真实 Up/Cancel/重复 Down；RTS Error/Mapping/Disabled；admission revision 改变；Host 停止 |
| I-RTS-WRITER | 条件候选 | Mapping/Tablet/Enabled/Down 等取得 writer 后进入慢/重入外部调用 | COM/USER32 永不返回，或同线程同步回调再请求非递归 writer | 外部调用返回；有锁环时正常 UI 设置重开不解除 |
| I-DECODER | 条件可证明 | cache reset 后共享 scale 或请求 context 补建持续失败 | 第一 context 无法查询/总 context 超限/目标 decoder 持续不合法 | 后续查询成功或生命周期重建；一次 transient 失败通常可恢复 |
| I-GATE | 条件候选/部分预期行为 | Presentation target/readiness、UI ack、load unresolved、suspend 或 command scene 保持未决 | 所需 ack/completion 不到达且不切回已确认的非 PPT scene | 正确 identity ack/completion、成功呈现并 ObserveWorkspace、切场景 |
| I-IO | API 契约确定；现场匹配由主会话验证 | Cursor/Draw3 日志启用且 stdout 为未排空的阻塞管道/受阻控制台 | OS 同步写持续不完成 | 排空/关闭管道或解除控制台选择等外部条件；改工具不能取消已进入的写 |
| I-EXIT | 清理能完成时不匹配长期活 UI | WM_CLOSE、Run 异常/返回、GPU fatal break | 仅在后置清理先永久等待时 running 仍未发布 false | 清理返回后主线程 100ms 检查受控退出 |

### 2. Files found / 本轮证据入口

| 文件 | 职责/本轮重点 |
| --- | --- |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.cpp`、`.cppm` | fixed slot/route 所有权、终态、队列控制 marker、wake generation、自动复位 event、finite/idle wait。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp` | reader/writer gate、decoder lifecycle、Down/Up/Packets、COM/USER32 查询、重复 Down 与异常收尾。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` | ingress 屏障、active/gesture 生命周期、Speed eraser idle loop、scene/load admission、Run 退出与异常。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp`、`.cppm` | Touch/Pen/Mouse cursor wake 差异、原子请求、canvas command 队列、WM_CLOSE。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.SpeedEraser.cpp`、`.h` | 实际解析 Speed input、收敛判据、NextAreaWakeSeconds deadline。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp` | bridge→controller、安全点、PPT 准入、绘制 callback、startup/stop 等待与 running 发布。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Bridge.cpp`、`Draw3.Product.cpp` | command 发布序列与锁图、状态快照、产品入口串行化。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Diagnostics.cpp` | 日志 producer 短锁、drawing consumer 的同步 stdout I/O。 |
| `Inkeys/IdtMain.cpp` | debug console 重定向保留、drawing callback、ProductRunning 监督。 |
| `Inkeys/IdtState.cpp` | SyncDraw3State / Setting owner、PenWidth context callback 的跨层同步边。 |
| `inkStrokeModelerTestTests/contact_input_tests.cpp` | 实验项目使用另一套 `draw3.*` module；不能默认拿它覆盖产品当前新增等待分支。 |
| `research/make-input-wake-probe.py`、`Build/CanvasFreezeInvestigation/input-wake-probe.generated.cpp` | 直接摘取产品实现的独立单 TU 实验；不调用 hidden injection 或真实 HWND。 |

### 3. I-WAKE：速率橡皮收敛后的 Move 不再唤醒消费者

#### 3.1 普通使用如何进入这段代码

- `Draw3.SpeedEraser.cpp:384-399` 的 `ResolveInput`：全局 automatic 开启时，显式 `EraserToolPolicy::Speed` 可以选出 `EraserKind::Speed`；无需隐藏测试开关。
- `Draw3.DrawingController.cpp:8273-8289`：选择橡皮允许该入口擦除，Touch 分支直接按真实 Down 初始化 `runtime.speedEraserOc`；Pen/Mouse 分支使用对应 session。`8739` 一带按真实 input entry/source 解析该 config。
- `Draw3.DrawingController.cpp:13369-13377` 每帧对未结束 Speed eraser `Advance`，并更新收敛尺寸。
- `Draw3.SpeedEraser.cpp:1459-1461` 公开停留时长；`1513-1522` 的 `NeedsAnimation` 在尺寸/历史窗口收敛后为 false；`765-771` 在没有有效面积下限或已经降到最小值时 `NextAreaWakeSeconds()==0`。无 contact area 元数据是一条可达输入，并非注入异常。
- controller `14471-14477` 此时使 `eraserIdle=true`；没有导航或其他 animation 的接触会走 `14524` 后分支。无 reconnect/area deadline 时，`14548-14551` 执行：

```cpp
while(!window_.ExitRequested() && !input_.WaitForWake(frameWakeGeneration,1000.0)) {}
```

每 1000ms 仅重新执行同一个等待；并不回到外层 frame loop。其循环体为空，也不检查新 mailbox 或 pending queue。

#### 3.2 真实 Touch/Pen Move 为什么不提供所需 wake

- `Draw3.ContactInput.cpp:771-801` `PublishMove` 成功仅覆盖 latest snapshot。`801` 明确返回时不触发活动帧唤醒；`wakeGeneration` 没有增加。
- RTS `Draw3.RealtimeStylus.cpp:1393-1394` 发布 Move 后调用 `PublishPenCursor`。`1559-1560` 排除非 Pen，所以 Touch 不借此 wake。
- Pen 也不能保证被 cursor mailbox 解救。`Draw3.WindowControl.cpp:840-843` 明确认为接触期间由活动绘制帧读取，只有 `!sample.inContact` 才 `RequestDrawingCursorRender`。稳定 owner、suppression 不变的 Pen contact Move 不 wake。
- `Draw3.WindowControl.cpp:1339-1432` 的 Pointer 分支对 Touch 只记录位置/模态，对 Pen 发布同一个 Pen mailbox；`SetDrawingCursorOwner` 在 owner 没变化时 `946-947` 直接返回；Touch suppression 没变化时 `965-967` 同样直接返回。因此稳定 `WM_POINTERUPDATE` 也没有必达 wake。
- 兼容 mouse 的 promoted 消息在 `Draw3.PenCursor.cpp:448-454` 明确被过滤。不能依赖系统提升消息修复等待协议。
- 真实 Mouse `PublishMouseCursorSample` 在 `Draw3.WindowControl.cpp:870-881` 位置变化时会 `RequestDrawingCursorRender`；该请求在 `929-932` 发 ControlWake。因此普通鼠标移动与 Touch/Pen 的确定条件不同。
- **现有 hidden injection 会掩盖：** `Draw3.Host.cpp:2111` 对所有成功发布的阶段额外调用 `input.PublishControlWake()`，包括 Touch Move。通过 `PublishHiddenTestContact` 的完整绘图测试不能复核这条真实 producer 缺口。

#### 3.3 不是“1000ms 后自然恢复”

`Draw3.ContactInput.cpp:1130-1161` 的真实 `WaitForWake` 每次只比较当前 generation 与传入值；timeout 后返回 false。controller 的 `while` 立即以**原来的** generation 再等待 1000ms。只要后续只有 Move，所有这些读取仍相等，循环没有主动退出条件。

这也不是单纯自动复位 event 丢失。若新的 wake generation 真发生，`1134`/`1144` 即使 event 已被别的 finite wait 消费，仍会返回 true。

#### 3.4 Up/Cancel 能否被 command boundary 堵在队列后面

**正常已建立的 contact 不能据此被堵住终态：终态不是新的 ingress token。**

1. `Draw3.ContactInput.cpp:804-817` 用现有 producing route 找到 record 并调用 `Close`。
2. `564-639` 先关闭 producer route，等已进入的 Move writer 离开，发布 terminal snapshot，再将 route 置 `ConsumerOwned`（或归还已隔离槽），最后 `638-639` `SignalWake`。状态与快照都在 generation 的 release 发布之前。
3. terminal 到达 idle loop 之后会使 generation 改变，原码 while 返回；不需要先 `TryDequeue`，所以 `commandBoundaryPending` 不能挡住该 wake。
4. 返回外层 frame 后 `Draw3.DrawingController.cpp:13300` 先捕获该帧 generation，`13323-13326` 再读取活动 record；`9496-9519` 按新 sequence 读取 terminal。若 Up 先于帧 generation 捕获，其快照已稳定发布，读取会看到它；若 Up 后到，其 signal 对该帧 generation 可见。
5. 正常完成在 `9764-9769` 标记 ended，`14082-14094` 从 active 归还/移除。随后 `14188-14193` 或 `13163-13170` 可执行命令并重新打开 ingress。
6. Touch/受支持 identity 的 reconnect 会在 `9737-9757` 延迟 Up，但有明确 deadline；`13355-13363` 到期调用 complete。它不是永不消费终态。

因此，**没有独立证据表明 I-WAKE 自身会在有效 Up 已发布之后永久阻塞所有画布输入**。若实际 Up 到了 RTS 却未发布终态，应归入 producer writer/decoder/route 异常，不能把它算作这里已经证明的行为。

#### 3.5 可反驳方法与独立原码实验

普通 GUI 复核顺序：用真实 Touch/笔选择 Speed 橡皮，保持接触不动直到尺寸完全收敛，保持按下后移动；记录 Move published、wakeGeneration、controller 所在阶段。若无别的 wake 但 controller 仍持续处理 Move，I-WAKE 的真实产品可达性被该运行反驳；若有隐含 mouse/control wake，需要找到提供它的具体入口。随后真实抬起，确认 terminal published 后恢复。

已生成以下两件仅属 research 的工件：

- `make-input-wake-probe.py`：Python 读取产品原文件，移除 ContactInput 的 module/export 包装，保持实现语句；直接 `#include "Draw3.SpeedEraser.cpp"`，再从 controller 原码摘取 `DrainIngressBatch` 模板与上面的 wait while。打印所有原文件 SHA256。
- `Build/CanvasFreezeInvestigation/input-wake-probe.generated.cpp`：单 translation unit，无真实 HWND，无 renderer、modeler、IFC 或完整产品依赖。`WindowController` 替身只提供 exit/pending 标记，测试环境外设 RTS callback 不在覆盖范围。

可复核内容：

1. 实际 `ResolveInput`/SpeedEraser controller 能达到无动画、无 area deadline 的 idle gate。
2. 实际 `PublishMove` 成功、latest Move 可读、generation 不变；摘取的 controller while 超过 1200ms 仍未返回。
3. 实际 `PublishUp` 使原码 while 返回，终态不依赖 ingress 消费。
4. 用原 `DrainIngressBatch` 制造 pending command boundary，再验证同一 Up 仍解除 idle wait。
5. 用实际 `WaitForFrameDeadline` 消费 event；旧 generation 仍能检测该 wake。反过来以已经更新的 generation 等待，即使队列已有 token，也会超时——此 API 并不承担 `HasPendingWork` 检查。

生成命令：

```powershell
python .trellis/tasks/10-14-canvas-freeze-investigation/research/make-input-wake-probe.py
```

独立编译参数意图：现有 ARM64 原生 cl，`/std:c++20 /EHsc /utf-8 /DNOMINMAX`，include 产品 Draw3 目录和现有 `VcpkgInstalled/Arm64/arm64-windows-static-v143/include`，链接 `user32.lib kernel32.lib`。只构建这一个 probe，不构建或改动完整工程。生成器 prefix 自己也先定义 NOMINMAX，避免 Win32 min/max 宏污染原码。研究代理未执行 cl；主会话的构建/运行结果应另行落盘并回链，不把预期 PASS 写成已通过。

### 4. I-ORPHAN：一个失去终态的旧 contact 怎样形成全局命令屏障

这是对旧候选补全的可证明条件链，不将其当作已找到现实触发器。

1. A 已 `initializeStroke` 成为 active，随后没有可消费 Up/Cancelled。`consumeLatestSnapshot` 在无新 sequence 时直接返回（`Draw3.DrawingController.cpp:9499-9502`），没有 idle age 看门狗。
2. 任意 scene/canvas command 被桥接消费。`Draw3.DrawingController.cpp:9825-9833` 观察 command queue 后置 `commandBoundaryPending=true`。
3. `DrainIngressBatch` 在 `65` 与 `tryConsumeOneIngress` 在 `9882` 都拒绝继续出队；`processCanvasCommands` 在 `11835` 又必须 `active.empty()`。A 未 ended，因此这两个条件可一直互相保持。
4. B 的 Down/Up（不同 `(tcid,cid)`）即使全部 published，B 的 record 也不会被取出。B 的终态只更新 B，不会改变 A 的最后 snapshot。此时“新输入正常到达”不足以证明 controller 能恢复。
5. UI 工具按钮可以先更新产品 state，但 controller 的 General marker 也留在边界后；不是所有 UI 视觉变化都证明 `PumpBridgeState` 已运行。

必须逐项记录的恢复出口：

- A 后续真实 Up/Cancel：直接更新 A 的 mailbox，并 wake；不受新 Down 队列阻挡。
- 同一 `(tcid,cid)` 再次 Down：RTS `1170-1177` 先 Cancel 旧 route 再 erase/rebind。对固定 mouse/pen identity，这使“只漏过一次 Up 就永久坏”推导不成立。
- `Error`、Disabled、Tablet removal、UpdateMapping：RTS `1590-1609` 统一 Clear bindings 并 CloseAllProducerContacts；不是单纯清空表后遗留 active。
- admission revision 改变：controller `9774-9794`、`12892`、`13321`、`13352` 主动收尾已接收 active 并隔离物理 route。此调用不仅存在于 ingress，所以 command boundary 不能阻挡 revision 换代本身被看到。
- Host Stop 先关闭 RTS 并收拢终态（`Draw3.Host.cpp:1851-1854`）。

最强 headless 复核应使用产品 coordinator + 原 `DrainIngressBatch` + controller active 收尾测试 seam：A 不发终态，Command 后发布 B Down+Up，证实 B 长期不出队；再分别注入 A Cancel、重复 A Down、admission 换代，验证恢复。**人为漏发终态只验证状态机后果；要归因用户问题，仍必须证明真实 RTS 在何种正常操作下真的漏掉了 A 的所有终态/恢复。**

`StylusOutOfRange` 当前在 `Draw3.RealtimeStylus.cpp:1127-1134` 仅清 Pen cursor，不 Cancel 生产 route。它是值得记录的终止边界；微软 API 把它定义为离开检测范围，但已查资料没有给出可直接依赖的“任意 Up 缺失必定由 OutOfRange 补偿”保证，也没有证明普通 Settings 关闭会制造该序列。

### 5. 所有应用内无界等待与锁顺序核对

| 等待/锁 | 入口与持有区间 | 已查退出/边界 | 结论与必须取得的反证 |
| --- | --- | --- | --- |
| RTS `stateWriterMutex_` | `Draw3.RealtimeStylus.cpp:109-132`；Enabled、Down、Up、Tablet、Mapping、Error | RAII 清 writer bit 并释放 mutex；writer 等已有 readers 清零 | 同线程重入是自锁的充分条件，但尚未找到已证重入调用。必须取 mutex owner 与嵌套 callback 栈。 |
| RTS reader drain spin | `RealtimeStylus.cpp:82-107` reader、`117` writer spin | readers try-enter，无等待 writer；析构原子减计数；所有正常退出走 RAII | 可能被外部/日志/线程异常永久悬停的 reader 拖住；没有普通应用内部 reader→writer 反向边。 |
| Contact writer latch | `ContactInput.cpp:249-263`，Close `586`，Move `776` | Move try-lock、不等待；真实 snapshot 写区只有原子存储，无 callback/IO；RTS writer 会先排空 Packets reader | 正常生产调用中未找到丢 unlock 或跨线程逆序；强行 SuspendThread/异常终止不是普通使用触发。 |
| ingress enqueue latch | `ContactInput.cpp:461-470`，EnqueueDown/Control/Command | 只包 moodycamel try_enqueue 与序号，没有 observer/业务/COM 调用 | 没有设置调用形成的可达锁环；队列入队失败走显式 fallback/拒收。 |
| cursor mailbox latch | `Draw3.PenCursor.cpp:197-219` | 原子字段复制，最后 clear；没有调用另一个业务锁 | 无应用内自重入或锁序逆转证据。 |
| diagnostics queue mutex | `Diagnostics.cpp:561-571`、`583-592` | 写入有界 ring；flush 复制后先 unlock，再作 IO | stdout 卡住会停消费者，不会由于同一个 queue mutex 顺带阻塞所有 RTS producer。 |
| product mutex / recursive call mutex | `Draw3.Product.cpp:11-13`，产品入口持 call lock | controller observer 通常不持它；bridge 快照/Publish 自锁，返回前不回调 Host | 必须结合 WindowService 跨线程等待审计，不能只看 recursive 名字排除其他线程等待。 |
| `presentationTargetMutex→bridge.mutex` | Host `550-552`、`847-855`、`2173-2176` | Bridge 各方法只操作自己的状态，返回前不回调 Host | 本轮未找到反向 `bridge.mutex→presentationTargetMutex` 仍持有的边。 |
| `commandPublishMutex→bridge/ingress` | Host `2214-2219`；Stop `1842-1850` | publish 先 reserve→bridge accept→marker；各 publisher 在此串行 | 防止命令与 wake 交叉发布。不能独立断言 ordinal inversion。 |
| idle `WaitDequeue` | ContactInput `1009-1025` | 先取 generation、再试队列、再复查 generation；Down/terminal/control signal；event 失败有限 Sleep 后再查 | 正常单消费者没有已证 lost wake。 |
| hidden event/pause | Host `736-744`；ContactInput `578-584` | 需 hidden injection + 非空测试 event 或显式测试 pause | 正常产品 options 不启用，不作为设置窗口暂停候选。 |
| startup waits | Host `1581`、`1739`、`1783` | startup commit/stop 通知；常规运行后不再进入 | 不能解释已画几笔后首次失效，除非产品被外部重新 Start/Stop。 |
| stop barrier / join | Host `1858-1862`、`1870-1877` | Stop 先关 producer、发布退出屏障，再排空 worker/join | 确实可无超时等待，但前提是正式关闭流程；需区分关 Setting 与关整个 Product。 |

新发现的跨层同步入口：`Host::PumpDisplayScale` 在 `1238-1239` 调用产品 PenWidth callback；`IdtMain.cpp:3249-3255` 在 `ApplyPenWidthContext` 返回 false 时同步 `SyncDraw3State`。后者 `IdtState.cpp:744` 设置 Setting owner，`746` ReconcileDraw3Presentation。因此**绘制线程也能进入 WindowService/Setting owner 等待**。它不只发生于 UI 线程。`ApplyPenWidthContext` 本身 `931-942` 释放 state mutex 后才 Publish；不能额外虚构该 mutex 被带进 Setting queue。本轮已把此边通知主会话窗口审计者，尚无完整锁环证明。

Drawing activity callback 不是同一类同步 UI 等待：`IdtMain.cpp:3244-3247` 转 Bar Notify；`Bar.Interaction.cpp:7460-7477` 只原子计数与 `PostMessage`，没有等待 Bar 命令完成。

### 6. 队列容量、fallback 与“已被消费的 wake”

#### 6.1 FIFO 与 fallback 没有发现必然悬空序号

- `ContactInput.cpp:327-353` 建立唯一显式 producer token，ingress latch 串行全部 Down/control/command enqueue；Down 最小容量 256，slot 上限 4096。
- command reservation 容量 258（`398-406`），fallback ring 也按该容量；`409-428` 的 marker ordinal 与 `downEnqueuedCount` 一起发布。
- `431-440` 只有下一个 ordinal 且之前 Down 都出队才恢复 command fallback；`977-999` 二次检查命中 fallback 时把刚取出的实体保留为 deferred，下一次再处理，没有直接丢掉这个 Down。
- `Host.cpp:2214` 的 commandPublishMutex 防止 Bridge 命令次序和 marker 次序被两个正常 publisher 交叉。单独模拟不带这个锁的多 publisher，不能算产品反例。
- General control wake 允许合并；`AcknowledgeControlWake` 在处理请求前先清 pending（`1085-1089`、controller `9827`），新的并发状态发布会再 signal。pending=true 的重复 publish 也会 signal（`370-375`），不是静默吞掉后续请求。

#### 6.2 Event 被 finite wait 消费不等于持久 wake 丢失

`WaitForFrameDeadline`（`ContactInput.cpp:1164-1193`）可以消费自动复位 event，但不会修改 monotonic wakeGeneration，也不会取出 queue。下一轮 ingress 检查和旧 generation 比较仍有效。

更精确的危险边界是：**调用方已经捕获到更新后的 generation，队列却因为 commandBoundary 不被取出，再进入不检查队列的 idle wait。** 此时旧 token 的 event 可能也被 finite wait 消费，`WaitForWake(currentGeneration,...)` 不会因为 queue 非空就返回 true。I-WAKE/孤儿 active 组合可保持这一状态；但正常已建立 contact 的有效 Up 仍会产生更新一代的 signal，不能因这个竞争被永久吞掉。

该区分已纳入独立 probe，避免只对 `SetEvent/WaitForSingleObject` 作简单模型、忽略产品真正的 generation 协议。

### 7. decoder、admission、pool 的持久状态核对

#### 7.1 decoder 不是“一次 Mapping 失败后永不补建”

- Mapping/Tablet removal reset 后，`RebuildCurrentContextDecoders` 在 RTS `1706` 先 `BeginLifecycle`。
- `EnsureContextDecoder` 在 `1723-1741` 给后续 InRange/Down 补建机会；Error `1466-1469` 只关闭 contacts，不重置 decoder lifecycle。
- 可持续拒收的具体条件是：shared scale 不合法且 `QuerySharedPositionScale` 总从第一 context 查询失败（`1656-1662`，`1733-1738`），或 `QueryCurrentContextIds` 的数量/返回值不合约（`1612` 起），或请求 decoder 的实际属性查询持续失败。
- 32 context capacity（RTS `40`）是真正边界；超限不是“读写锁卡住”。需记录 contextCount、firstContext、query phase/result 才能与 source 停投递区分。多设备/虚拟 tablet 配置可以构造条件，但本轮没有证明 Settings 操作改变到该配置。
- BuildContext 查询在 writer 内（`889-947`），因此长期阻塞与反复 no-decoder 是两个互斥现场表现，需要各自的阶段/栈证据。

#### 7.2 admission 所有持续关闭条件都有明确归属

- `Host.cpp:558-562`：`commandScenePending`、目标不存在、ready identity 不匹配、suspended、require UI ack、不满足从 Presentation 退场的 applied workspace。
- caller 只需持续发布同一奇数 revision 就会继续拒 Down（ContactInput `889-919`）；正常 UI 工具变化不能自动替代 scene ack。
- `Host.cpp:838-855` 的 ObserveWorkspace 只在成功场景呈现后更新 applied workspace/ready，并清 commandScenePending。controller `7795-7812` 暂存 pending ready，`14425` 一带成功呈现后才发布。
- controller `9850-9855` 还对 `activePresentationLoadPending || presentationLoadUnresolved()` 拒收；已得到 terminal/manual-required load 失败时也可能按设计持续关闭，见 `12059-12096`。它要记录 storage completion/identity，不应归成 RTS 收不到 Down。
- `SetPresentationInputSuspended`（Host `2197-2209`）要求当前 Presentation exact identity，不是普通 Settings pause 标志。
- 恢复条件是对应 identity 成功 ready/ack/completion 或完成非 Presentation scene 交接。对 Desktop→Desktop，且无 commandScenePending 时，该公式不应继续关闭；若观察到关闭，足以定位到状态损坏/不同 scene，而非“设置很容易卡”的泛化解释。

#### 7.3 pool 泄漏需要先证明终态失联，不能用容量失败反推

- 默认 slot 数是 `2*(SM_MAXIMUMTOUCHES+2)` 后向上取块（ContactInput `109-113`），按系统容量预分配；没有按每帧增长的 ingress record。
- `PublishDown` 无 free slot 时在 `745-749` 拒收；RTS `1227-1231` 也移除未发布成功的 binding。不能把所有 publish-failed 一概当作队列断开。
- `DiscardUntilTerminal`（`863-887`）故意保留 Quarantined 物理 route；真正 terminal 负责归还。新 Down 重用同 identity 先 Cancel 旧 route，包含 Quarantined，因此固定 mouse identity 仍可能回收旧槽。
- `AbortUnqueuedDown`（`536-562`）看似无限循环，但未入队的 Down 不应被 consumer 得到并 Quarantine；正常 producer 外部由 RTS writer 串行，不能直接把一个凭空的 Quarantined unqueued record 当成可达状态。
- terminal 坐标无效会回退 latest snapshot，再回退 immutable Down（`597-608`）；PublishDown 已拒绝非有限 Down，正常输入不应走到“坏 Down 导致 Close 留在 Closing”的最终失败支路。它需要破坏内存或违反公开输入契约，不能作为普通 Settings 触发。
- 微软把 `IStylusPlugin::Error` 说明为 RTS 捕获 plugin exception 后通知；已查资料没有保证“任意返回 E_OUTOFMEMORY 必定自动调用本 plugin.Error”。所以既不能假设 pool 满后必然自愈，也不能假设必然禁用 collector。该 OS 行为需单独实验。

### 8. I-EXIT：线程退出与 running 发布的准确边界

- `WindowControl.cpp:1259-1263` WM_CLOSE 只 RequestExit+wake；WM_DESTROY `1491-1492` 同样通知绘制线程。
- `DrawingController.cpp:13064-13067` 退出循环；graphics recovery 无法恢复时 `12675-12682`/`12697-12703` RequestExit+break；catch 在 `14558` 后抛到 Host。
- `Host.cpp:1684-1699` 捕获绘制异常，随后 `1702-1708` 结束 activity、采集 metrics、析构 drawing、Shutdown presenter、释放 renderer，然后才 `1709` `running=false`。
- `IdtMain.cpp:3396-3425` 每约100ms检查 ProductRunning；变 false 后发 offSignal、成对隐藏画布、HideAllUserWindows、设退出码6并离开主循环。因此“WM_CLOSE 让 Draw3 永远停止但正常应用一直留着”在清理完成时被当前 HEAD 反驳。
- **剩余盲区是上述后置清理本身：** 若 `drawing.reset/presentation.Shutdown/renderer.ReleaseResources/CaptureRuntimeMetricsAfterRun` 卡住，running 还保持 true。主线程监督只看该标记，无法判断 Run 已结束。需要 dump 显示 Run 栈已退、线程在这些析构/IO/commit 等待里，而不是笼统宣布消费者退出。
- 当前 scope 不构造 DComp/GPU driver 永不返回作为必现触发；这些外部等待应由渲染侧现场栈审计。

### 9. I-IO：诊断能使画布活线程停在无限同步写

`Draw3.Diagnostics.cpp:48-55` 缓存 stdout 后同步 `WriteConsoleA`，失败后同步 `WriteFile`，没有 timeout、取消机制或 IO worker。`FlushCursorDiagnostics` 在 `617` 调用该函数；controller `12607` 与 `14454` 都在 drawing 线程调用 flush。

`IdtMain.cpp:300-310` 明确保留 FILE_TYPE_PIPE 的 stdout/stderr，双路重定向时不新建 console。因此以下触发是代码支持的环境：启动已启用 Cursor/Draw3 日志的进程，stdout 连到存活但未排空的管道，输出写满缓冲。微软 WriteFile 文档明确同步管道写在读方释放空间前可以不完成；API 不承诺超时。[WriteFile 官方文档](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefile)

此时：drawing 不再消费 ingress，Host.running 尚为 true；RTS/Win32 producer 仍可把短记录写入有界 ring（flush 作 IO 前已释放 queue mutex）；UI/Setting 的控制权不必跟随 drawing 停止。关闭/重开 Setting 不能取消另一个线程已进入的同步写。解除管道背压后恢复，是强区分实验。

传统 console 的 Mark/QuickEdit 属同类外部等待线索，但不能猜测用户系统默认设置；需要 `GetConsoleSelectionInfo`、真实 WriteConsole 栈或退出选择即恢复的证据。独立的 console selection 文档只证明可查询 selection，不能单凭它证明本视频已处于该状态。[Console Selection 官方文档](https://learn.microsoft.com/en-us/windows/console/console-selection)

主会话已接手真实运行的 stdout 停读实验和栈匹配；本文件仅保留代码/API 证明，不复制或伪造主会话实验结果。

### 10. 后续取证必须能区分的现场

| 实际观测 | 指向 | 必须再核对 |
| --- | --- | --- |
| Move published 持续、generation 不变、drawing 在 I-WAKE while；Up 后恢复 | Speed eraser 缺 wake | 是否为真实 Touch/Pen producer，而非 hidden helper；owner/suppression 是否稳定 |
| A 长期非 terminal，commandBoundary=1，B Down/Up 已 published 但 Down 未出队 | orphan+command barrier | A `(tcid,cid,generation)`、所有 terminal/lifecycle 事件、gate revision 是否改变 |
| Down arrival 有、writer acquired 无，多个 writer 等同一 mutex | writer 阻塞 | owner 线程栈，是否 COM 自重入/reader 未退 |
| Down arrival 有且 no-decoder 连续 | decoder/state | observed lifecycle、contextCount/firstContext、补建每步 HRESULT；不能说 collector 不回调 |
| Down+dequeue 有，ContactAdmitted false | Host 准入 | desired/applied workspace、commandScenePending、ready/uiReady/suspended identities、revision |
| Down admitted 但 PresentationInputSuppressed | load unresolved | currentLoadRetry 状态、worker request/completion、实际文件 IO |
| drawing 固定在 WriteFile/WriteConsole，读管道/解除选择立刻恢复 | 同步日志 IO | 实际调用栈必须经过 Draw3 flush 或其它 drawing 诊断出口 |
| drawing 不在 Run，却 running=true | 后置清理未完成 | drawing析构/presenter Shutdown/metrics/renderer释放具体阻塞栈 |

### 11. External references / 一手资料及适用限度

- [WriteFile](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefile)：同步 handle、管道背压与返回条件，支持 I-IO 的严格持续条件。
- [Console Selection](https://learn.microsoft.com/en-us/windows/console/console-selection)：提供查询 selection 的 API，不作默认 QuickEdit 状态推断。
- [IStylusPlugin::StylusUp](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-istylusplugin-stylusup)、[IStylusPlugin](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nn-rtscom-istylusplugin)：事件职责；不额外推断关闭 Settings 必然丢 Up。
- [IStylusPlugin::Error](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-istylusplugin-error)：RTS 捕获 plugin exception 后的通知；没有从该页得到任意失败 HRESULT 自动复位本插件的保证。
- [RealTimeStylus Classes and Interfaces](https://learn.microsoft.com/en-us/windows/win32/tablet/realtimestylus-classes-and-interfaces)：HRESULT 的一般含义，没有给出所有错误的 collector 恢复状态机。
- [RTS threading considerations](https://learn.microsoft.com/en-us/windows/win32/tablet/threading-considerations-for-the-stylusinput-apis)：sync 回调与 Enabled/Disabled 调用线程边界；外部同步调用可以拖住 ink，不能单独证明本项目锁环。
- [2024年 RTS Touch 停投递一手报告](https://learn.microsoft.com/en-us/answers/questions/1641576/c-realtimestylus-is-dead-and-doesnt-handle-any-tou)：作者报告 Win7sp1 上 layered/noactivate/topmost 窗口在全屏/MessageBox 抢焦回焦后 Touch collector 停投递，点任务栏可恢复；回复未确认根因、未给最小复现。本轮此前已调查过，仅作外部焦点实验线索，不能升格为 Windows11、多设备、当前视频的强根因。

### 12. Related specs

- `.trellis/workflow.md`：planning 调查与实现分离；本轮不修复产品。
- `.trellis/spec/native-desktop/input-and-ink.md`、`draw3-integration.md`：唯一 producer/consumer 与稳定 contact 所有权、主/辅助画布输入边界。
- `.trellis/spec/native-desktop/rendering-and-ui.md`：Draw3 独立绘图线程与 UI3/Setting 的线程/渲染所有权，不能把 UI 可用等同绘制线程进度。
- `.trellis/spec/native-desktop/errors-logging-and-resources.md`：日志与故障边界，需要对同步输出卡住取证。
- `.trellis/spec/native/runtime-and-rendering.md`：运行期 HWND/renderer 与消息线程职责。

## Caveats / Not Found

- 本轮还没有“普通 Settings→Explorer 操作一次后，全设备在有效新 Down/Up 下永远无法恢复”的完整自包含根因证明。真实日志阻塞由主会话验证；输入路径新增确定缺口 I-WAKE 的恢复边界已单独证明。
- 未因视频中的持续 frame、某次 Clear 或 cursor trace 排除本文件任何候选。过去视频证据与本轮全量代码审计的证明范围不同。
- 本轮源码提取 probe 保留真实 coordinator/SpeedEraser/等待实现，但替身 window 不覆盖真实 RTS/USER32/device driver 行为，也不覆盖完整 GPU controller；必须把独立实验结论限制在被摘录的等待协议。
- 研究代理仅运行 Python 生成器成功，未运行 cl 或 probe；编译及结果由主会话独立落盘。脚本生成时已输出原文件 hash，避免将其他独立实验目录的旧模块当作本产品验证。
- 没有得到 RTS 自重入、生命周期永久禁用、持久 first-context 查询失败或实际孤儿 contact 的现场栈/日志；这些仍是条件候选，而非已确认根因。
