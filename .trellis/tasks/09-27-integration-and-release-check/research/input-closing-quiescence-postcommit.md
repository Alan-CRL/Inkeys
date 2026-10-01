# Research: 输入 Closing、初始化拒收身份与 Host 换代静止边界

- Query: F057 的普通 Controller 页边界交错、AbortUnqueuedDown、Host ResetForNextRun 是否存在真实生产可达问题；如何用最小生产回归补齐验证？
- Scope: mixed；当前集成后的生产源码及 Microsoft 原始 RTS threading/API 文档。旧 demo 不作为产品生命周期依据。
- Date: 2026-09-30（Asia/Shanghai）。
- Active task: `.trellis/tasks/09-27-integration-and-release-check`。
- 分工：只读源码/spec/任务；仅写本文件，不改产品、既有冻结研究、工程、账本或 Git；不构建、运行、GUI、采样、操控进程或递归派发。

## Findings

### 1. 结论与分类

| 项目 | 结论 | 证据等级/下一步 |
| --- | --- | --- |
| Controller 三处初始化失败按 key PublishCancelled | **条件性身份缺陷：旧 Down 失败可取消同 tablet/contact key 的已接受新 Down。** 调用者已有精确 handle，却按 key 查当前 Producing route。 | 静态确认错误目标可达；真实失败分支触发/动态 red→green 未执行。优先最小修复，见第2节。 |
| 普通 sealPresentationContacts 与真实 Close→Closing | RTS terminal producer 与 Draw3 consumer 本来不同线程，该交错可达；F057 延后回收修补已存在。 | 应运行真 Controller::Run 或提取真实共享逻辑的回归；原 Coordinator/Fatal CPU probe 不是普通页命令全链证据。 |
| RTS Down→Abort 与另一 RTS Up/Disabled 抢 Closing | Down/Up/Disabled/Error/映射变化受同一 stateWriterMutex+reader gate；正常 RTS 不能任意并发进入这些事件。 | “任意Coordinator Up暂停能把真实RTS Down Abort永久卡住”的推断不成立。三处Controller反向按key取消是独立旁路，修后再核。 |
| 正常 Host Stop→Start 的 Reset | Product forwarding gate、RTS disable/remove、绘制 join、HWND解绑的顺序构成基础；初始/DComp失败重建发生在业务线程启动前。 | 没有证据支持正常顺序必有 producer/Reset race；需要 actual callback/owner静止验证。失败COM HRESULT仅日志后继续，缺显式闭门/drain证明，分类为条件性残余，不冒称已复现数据竞争。 |
| 直接 Host hidden injection 与 Stop 并发 | 仅显式测试开启；直接调用 Host::PublishHiddenTestContact 不经过 productCallMutex，必须由fixture保证静止。 | 测试拥有者合同；不能把任意测试直调竞态写成正式用户可达RTS bug。 |
| 所有必要样本完整性 | 当前RTS Packets读取最后packet、Coordinator Move覆盖最新快照是已有合同。 | 本次修补不新增丢点/降频；合成case只能证明它的已接受快照与终态，无权宣称全部物理packet都已逐个建模。 |

### 2. 新发现：initializeStroke 的按 key 取消会越过旧 handle

#### 2.1 实际失败入口

`DrawingController::Run` 的 local `initializeStroke(handle)` 首先核 handle.record 与 generation，复制不可变 Down（Draw3.DrawingController.cpp:5202–5206）。以下三处失败随后放弃该 runtime：

| 原因 | 原调用与后续清理 | 位置 |
| --- | --- | --- |
| acquireStroke 返回空；扩展池 modeler.Reset失败 | 构造cancelled=down；按record的tablet/contact调用PublishCancelled；Recycle(handle)；return false | 5153–5171、5709–5717 |
| runtime modeler.Reset(modelParams) 返回非OK | 同样按key取消/Recycle；runtime.cancelled=true；handBackSpeedEraserController；handle={}；inUse=false；return false | 5805–5819 |
| modeler.Update(kDown) 返回非OK | 同样按key取消/Recycle；同上runtime回收 | 5857–5879 |

`ContactInputCoordinator::PublishCancelled(tabletContextId,contactId,...)` 用 FindProducing 寻找匹配key的 **当前 Producing/Quarantined** record，再 Close（ContactInput.cpp:473–491、807–812），不核调用者持有哪个地址/代次。已结束的 ConsumerOwned旧 record 不在 FindProducing集合。

#### 2.2 真实合法时间线

1. RTS序列 `A.Down(key K) → A.Up(K) → B.Down(K)`；这是串行事件，不要求两个RTS callback重叠。A 已 ConsumerOwned，但绘制consumer还未处理A的队列Down；B已获得另一record且为Producing。
2. consumer从单producer FIFO取A.Down，initializeStroke(Ahandle)走任一上述失败。
3. 旧代码 `PublishCancelled(K)` 找到B；Close将B发布Cancelled。随后 `Recycle(Ahandle)` 归还A，与错误取消B无关。
4. B.Down已经accepted，接下来的Move找不到Producing route，B会作为Cancelled被消费，形成新接触输入丢失。RTS writerMutex不保护Draw3 consumer这条反向PublishCancelled。

两个record初次使用时 generation **可能都等于1**。身份必须比较 `(record地址,generation)`，不能仅在test比较generation数值大小。

**分类：确认的条件性错误目标修改；触发前提是旧初始化失败加同key新接触。** 本轮未运行错误注入，不能说现场卡死一定由它引起，也不能说所有正常输入都发生丢失。三种failure都应修到同一精确handle合同。

#### 2.3 最小修正合同

- 这三处是consumer拒收，不应模拟producer的物理Cancel。改用 `input_.DiscardUntilTerminal(handle)`；保留原runtime cancelled、speed eraser handback、handle清空、inUse=false与return语义。第一分支本来没有已取得runtime，不加多余释放。
- 不保留后续无条件 `Recycle(Producing)`。Discard已经覆盖：Producing→Quarantined，真实Up/Cancel由producer归还；Closing→ClosingDiscarded有界交出；ConsumerOwned→Recycle；stale地址/代次拒绝触碰新route。
- 自己的Down失败必须继续占用物理route直到Up/Cancel；这是防止迟到Move污染其它接触的正确生命周期，不是新的泄槽。界面Touch Begin/End仍由原RTS binding终态通知，不在consumer里伪造物理Up。
- 修补不会更改成功路径、笔迹、pressure、modeler更新频率或正常Down/Up顺序。换成按key加条件的新setter仍有对象选错窗口，不是充分修正。
- 为三处共同失败处理提取很小的命名helper是可选的测试接缝；必须让三处真实caller调用它，再由production probe测试。不能在test复制一份正确Discard算法，让生产继续按keyCancel。

#### 2.4 第一批可直接实施的 red→green

1. 先把三处相同的旧route拒收逻辑收敛到同一很小的生产helper，保持旧行为；在现 `RunParkedDesktopExitAutoSaveTest` 或独立Controller production probe里调用该helper，先保留红灯。
2. 用真实 Coordinator发布 A.Down、A.Up、B.Down（同key），消费A、保存Ahandle；消费B并保存Bhandle只为断言，不修改B。调用旧失败helper(Ahandle)，断言B快照仍Down、B.Move成功、B.Up成功、旧A读失败、最终occupiedSlots=0。旧版将取消B，应自然红灯；改helper到Discard(Ahandle)后绿。
3. 正在Producing的失败A：helper返回后A槽仍占、快照拒读、Move不能进入已交出consumer，Up/Cancel后恰一次释放；重复拒收不重复计数；新Down能取得槽。
4. Close已经CAS Closing并停在现hook：失败helper需在producerresume前返回；该槽不提前free，producer放行后恰一次回收。覆盖Up/Cancel、consumer先/producer先，旧generation读/回收不能动新handle。
5. 后续全Run fixture可在实际初始化model Reset/Update结果边界用显式一次性fault转非OK，进入原三处failure branch。它证明真实分支runtime/speed eraser清理；不必假造任意invalid硬件输入或改变正式modelParams去污染成功路径。

这批helper/Coordinator测试可以严格无HWND；完整Run版本使用允许的隔离隐藏窗口。前者通过不能被命名为整条GPU/PPT/真实RTS输入PASS。

### 3. 普通页边界的真实调用链与测试目标

- `Host::PublishState` 在presentationTargetMutex内更新Bridge并RefreshPresentationInputGateLocked；所期望的target与ready/UI ack不同，就SetAdmissionBlocked并发布ControlWake（Host.cpp:257–277、1629–1637）。`SetPresentationInputSuspended` 也走同一精确expected identity门（1655–1667）。
- 业务命令 `PublishProductCommand → productCallMutex → Host::PublishCommand(commandPublishMutex) → TryReserveCommandWake → Bridge.Publish → PublishReservedCommandWake`（Product.cpp:182–188、Host.cpp:1670–1678）。输入Down和实体Command marker共享ingress producer token与短enqueue latch；失败marker退路有Down水位+ordinal，不让Clear抢在旧acceptedDown之前（ContactInput.cpp:398–444、526–533）。
- consumer取空marker时调用Host controlWake：scene-stamped命令先EnqueueCommandScene，再Canvas命令，PumpBridgeState；随后 `processCommand → sealPresentationContacts`（Host.cpp:929–985、Controller.cpp:6353–6364）。`commandBoundaryPending` 阻止跨已有Canvas命令继续消耗下一批Down（6397–6406）。
- `sealPresentationContacts` 只在AdmissionRevision变化时执行；activeRuntime按最后已消费raw snapshot完成model Up，gestureContacts精确Discard，清导航/激光临时状态并请求完整帧（6307–6350）。activeRuntime后续正常烘干/历史提交后在 `erase_if(active)` 精确Discard(handle)，不会再按key取消（10345–10358）。
- Canvas commands只有active.empty才消费（8263）。SetPresentationTarget/Workspace、Clear/Undo/最终Exit使用各自原事务，不能为了“test立即有响应”越过活动接触或改变durable顺序。

**关键反例：** 同一Desktop场景仅点击Clear/换tool并不必然改变AdmissionRevision。暂停Up在Closing而未写终态时，active可能仍未结束，Clear等待active收尾是既有语义，不能将它自动记死锁。优先用真实PPT target变化/输入suspend保证页边界，再验证Clear FIFO；Selection/工具不擅自加page gate。普通笔与gesture直接Discard分别覆盖；`kCanvasNavigationProductIntegrationEnabled=false`（WindowControl.cppm:26），不能为了生成手势把关闭入口打开。

#### 3.1 推荐完整Controller fixture

复用 `Draw3.HiddenWindowTest.cpp::RunMode/CheckPresentationPersistence` 的真实ProductHost、WindowService双隐藏HWND、独立DComp/ULW设备与真实唯一保存root。已有RunParkedDesktopExitAutoSaveTest执行CPU共有helpers/Fatal route（3338–3728），**没有调用Controller::Run的普通seal lambda**，不能复用它的PASS标签代替本项。

1. 初始化两个StableSlideId目标A/B，A成功Present且匹配UIack；Pen Down和多次Move逐次等待production inputSequence增长，保存最后已消费raw endpoint与旧page身份。
2. 给Host内部Coordinator安装现 `PauseNextCloseAfterRouteClosedForTesting`；独立producer调用真实PublishUp，等pause.entered。这一terminal→consumer交错不被RTS writerMutex排除；没有SuspendThread。
3. 控制线程通过正式PublishProductPresentationTarget或精确SetPresentationInputSuspended改变目标/gate。等待B的实际workspaceChanged/ready、成功Present或至少目标Canvas命令进度；保持producer未resume。断言普通consumer未停在Discard/Recycle。
4. 检查旧笔仅一次进入A的history/UInk（最后已消费实点，非未来Move/预测），B没有旧笔；暂停槽仍占用且其handle拒读。之后release Close、join producer，断言terminal/recycle增加恰1、occupied回基线。
5. B UIack后新Down/Move/Up全部accepted并consumed，完成一笔且旧handle读/Recycle不碰新record。补Clear/Undo/Redo FIFO，验必要accepted命令无丢失；EndScreen与不同文稿可在后续已有PPT矩阵中组合。
6. 额外“Down已排队而Admission被关、该Down Up进入Closing后才consumer拒收”走processCommand:6368–6386；证明拒收路线有界且新target不补画旧Down。失败fixture先resume所有hook再join；永远不detach访问Host的线程。

**最小hook选择：** ContactClosePause type附着在contact_input module，Host.h是普通header；不要在全局module随意forward-declare同名类型制造ODR/import问题。可把上述测试driver作为Host的一项明确仅hidden-test启用的member，实现在Host.cpp（可见Impl/input），从现HiddenWindowTest调用；或把测试控制抽成小的header-owned DTO并同步原Contact hook。只导出必要测试控制，不暴露整个Coordinator给任意线程，也不新增每帧registry/第三套框架。默认hook为空，fixture需own暂停对象寿命。

### 4. AbortUnqueuedDown 的可达性与最小补测

- PublishDown acquire槽→generation递增→Initialize→Producing→try_enqueue；失败后原producer AbortUnqueuedDown，成功才downPublished/SignalWake（ContactInput.cpp:732–763）。Abort先CAS Producing→Closing，等writer退出，Free+ReleaseSlot；若另一个Closer已抢Closing则Yield等终态，或ConsumerOwned→Free（536–562）。
- 真RTS `StylusDown` 从解码/activeBinding到整个Coordinator.PublishDown返回都持RtsStateWriterGuard（RealtimeStylus.cpp:1136–1222）。StylusUp、Disabled、Error、TabletRemoved、UpdateMapping等也持相同stateWriterMutex（1108、1228、1408、1426、1436）。Packets/InAir用reader计数，writer先设置writerBit再等旧reader退出，Down开始后新packet不会取得reader（81–130、1291–1361）。
- 因此正常RTS Up/Disabled不能在该Down未返回时抢同key Closing；同一个失败Down未入queue，普通consumer拿不到其handle。已发现的Controller按key取消可触碰别的未入队新record，是应优先去掉的跨身份旁路；修后该特殊Abort“另RTS Up永久Closing”假设在这条实际ownership中被排除。
- Shutdown中同步disable/remove若都失败且仍有callback，或测试直接调用Coordinator/Host注入，可构造其它Closer；不据此将正常RTS归为已确认deadlock，也不以blanket spin timeout提前free仍writer使用的槽。
- 现failNext只覆盖General/Command marker，没有Down失败hook。若需要确定验证Abort，新增 `FailNextDownEnqueueForTesting()`，只在EnqueueDown持短latch时消费atomic一次性flag，不影响正式default。避免用无限写满队列、注入全局allocator失败或mock另一个正确queue来制造不稳定失败。
- 测试应断言falseDown为未accepted：downRejected+1、downEnqueued水位不前移、slot回基线、无Down指针出队、下一合法Down→Move→Up成功；命令fallback ordinal/先后不变。Abort直接ReleaseSlot不计accepted recycled，不能强迫recycled增加1或把falseDown写为accepted样本丢失。
- 可加publicCoordinator压力交错作为防御契约测试，但标签必须说明不等同真实RTS Down/Up并行。无需修改现Abort自清算法来满足不可达的人工并发假设。

### 5. producer静止、COM与 ResetForNextRun

#### 5.1 正常所有权与顺序

- 生产输入只由主Drawpad绑定的RTS进入；WindowControl的正常Pointer/Mouse消息更新cursor/请求，不直接PublishDown。另一个contact producer仅是明确开启的hidden-test message（Host.cpp:1490–1555、1611–1615），正式HostStartOptions默认false。
- DrawpadWndProc先ProductRunning，ForwardProductMessage再两次检查productStopping并持recursive productCallMutex（Product.cpp:75–83、200–222）。StopProduct将productStopping置true后drain同锁，再Host.Stop；不跨Host.Stop持该锁，避免GPU→Window owner同步回调等待环（32–44）。
- Host.Stop先displaySubscription.Reset，关Bridge生产并排final保存命令，再stylus.Shutdown，排已接受保存，停止/join绘制，再window输入coordinator=null、DetachExternal、清HWND和hiddenTest flag（Host.cpp:1308–1371）。成功Stop返回前旧绘制consumer已join。
- stylus.Shutdown：先撤销diagnosticsPlugin借用（diagnosticsMutex不跨COM），put_Enabled(FALSE)，RemoveStylusSyncPlugin，CloseAllProducerContacts，释放plugin/stylus引用，COMuninit（RealtimeStylus.cpp:2615–2649）。plugin的同步Disabled取得同一writer gate，清bindings并CloseAll（1101–1111、1524–1537）。
- Host.Start检查running/attached为空后，ResetForNextRun在AttachExternal和RTS初始化之前（1033–1073）。ProductStart本身持productMutex；DComp初始回退main先StopProduct与Window owner join再Start，业务jthread在全部初始化后才建立。运行时device-lost由同Controller recovery，不是直接把旧Run的input Reset。
- Reset清队列、writerLatch/freeMask/控制marker字段，只能在producer/consumer静止后调用（ContactInput.cpp:1053–1079）。generation不被清零，旧handle拒绝依靠地址+route状态/新代次。diagnostic累积不归零，test按每次Run baseline比较。

Microsoft的原始文档说明：同步插件主要在RTS数据线程回调，但Enabled/Disabled在改变属性或移除插件的线程调用；这支持当前writer gate的跨线程必要性。不能把“同步插件”解释为所有callback永远同线程。[RTS threading](https://learn.microsoft.com/en-us/windows/win32/tablet/threading-considerations-for-the-stylusinput-apis)、[Disabled通知](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-istylusplugin-realtimestylusdisabled)。

#### 5.2 未关闭的失败边界

- 当前put_Enabled(FALSE)/Remove失败只LogHResult后继续。没有覆盖全部plugin入口的closing flag、真正inFlight=0或禁止下一次Host.Start Reset的失败状态；diagnosticsMutex只是诊断借用，stateGate readerMask只是packets/普通decoder读者，不是全部callback-inflight计数。
- 成功disable/remove会停止/移除插件，是正常生命周期基础；官方API的“不enabled则不收events”不证明失败HRESULT也停止，更未给当前代码所有损坏COM/driver情形的动态保证。[put_Enabled](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-irealtimestylus-put_enabled)。
- **分类：条件性COM失败静止证明缺口；未证明真实 callback 在return后继续，也未复现Reset race。** 不上报确定use-after-free，不把一般静态missing counter列为已发生data race。纯头lessgate测试只证明锁/原子算法，不能替代真实RTS停止。
- 若注入证明失败后仍有callback，应最小补生产plugin的闭门与in-flight排空或让Host失败后禁止Reset/重建并进入fatal退场，保持仍被访问的资源alive。不能仅把impl_->coordinator=null并继续让旧plugin用引用；不能强杀线程、detach或把old callback误路由新epoch。新增callbackgate需全入口/引用计数/错误cleanup独立review及成本检查，不因这一假设立即重写RTS。
- 若只做正常路径补证：用真实plugin callback/source hook hold一个Up/Disabled，Stop请求应先关闭产品forwarding但在producer未放行时不得声称Stop完成/启动新Host；放行后自然Stop，窗口链销毁，然后Start并验新contact和旧generation拒读。正常Close的15秒supervisor另以隔离child保障无法放行的情况。

#### 5.3 hidden producer 与 HWND边界补测

- 经Product WndProc注入的Down/Up持productCallMutex；可在Close hook停住owner，再从其它线程请求StopProduct，应先阻新forwarding，等待旧owner放行。放行后Host.Stop完成，再WindowService.StopAndJoin，才能下一代Start。迟到旧HWNDmessage不应进入新Host。
- 直接 `Host::PublishHiddenTestContact()` 不持上述锁，属于fixtureAPI；不能在one thread继续直调时另threadStop/Reset，并把由fixture造成的race归责正式RTS。若保留该压力fixture，先加明确测试in-flight gate和寿命合同。
- hidden固定tablet/cursor ID与RTS真实ID不同；测试别把其多源并行状态当作同一个RTS串行key的真实事件。一般正常Mouse接触仍由RTS处理，cursor消息不是另一等价绘图producer。

### 6. 实施所有权、依赖和验收

| 改动单元 | 唯一写入范围 | 最小证据与限制 |
| --- | --- | --- |
| 三处failure精确handle拒收 | Controller.cpp/.cppm；若新增probe可复用当前CLI避免并行改IdtMain | 先真实共同helper旧版red，再Discard green，正常/ConsumerOwned/Closing/新同key/stale代次全部验证；完整Solution编译及review三调用点runtime清理。 |
| Abort定向失败测试 | ContactInput.cpp/.cppm、对应headless测试 | 一次性default-off Down enqueue fault、槽/水位/下一接触验证；不改变必要输入或Abort正常算法。 |
| 普通PPT Closing整链 | Host.cpp/.h和HiddenWindowTest.cpp/.h；Controller由同owner按需要小hook | 实际Controller::Run、真实target gate/成功Present/old-page history/command进度；DComp和ULW各三轮；严格no-window结论与hidden结果分开。 |
| RTS失败quiescence | RealtimeStylus.cpp/.cppm，Host公共合同必要时串行 | 先判真实故障/注入边界；正常成功shutdown与COM双失败分开。若改gate，全部callback入口、COM refcount/owner/下一Run需独立check。 |
| CLI/工程/spec/总验证 | E01冻结后root指定唯一IdtMain/工程写入者 | 不与ShutdownSupervisor worker或后续启动scope worker重叠；MSBuild/test输出由root串行占用。 |

- reviewer要检查实际diff和调用链，确认三failure仍正确返回runtime、failedDown未accepted水位正确、Close单次释放、Reset前epoch静止；不只读实施者PASS摘要。
- 首轮自动命令可复用当前存在的 `Inkeys.exe --draw3-parked-desktop-exit-test`（若新probe被实际接入）、`InkeysHeadlessTests.exe --no-window`、`Inkeys.exe --draw3-hidden-test`；新入口只有实际实现后才记为可执行。
- 完整 `InkeysRepo.sln Debug|ARM64` 与Release可得三架构，PptCOM/Headless/相关Draw3回归及15秒supervisor按影响复验；build timeout至少5分钟，避免并发扫描/编译干扰运行。新module/头测试登记需保持现编码/CRLF与ABI。
- 自动合成轨迹须逐次等待已接受Move被consumer观察，验证Down/Up/Cancel顺序/时间戳/尾实点；不新做latest-only或改变已有采样合同以通过测试。
- Win7 SP1仅KB2670838、Hardware FL11.0/无硬件FL11→WARP、仅DComp/ULW、FLIP不变/两DWM禁用、真笔/Touch/RTS驱动压力及用户现场卡死仍是独立真机范围。

## Files Found

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.cpp/.cppm`：route代次、writerLatch、Close pause、Discard/Abort/Reset以及accepted水位。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp/.cppm`：三处failure拒收、sealPresentationContacts、普通Command/烘干/CPUprobe。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp/.cppm`：producer reader/writer gate、绑定、Disabled与COM停止。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Product.cpp/.h`：唯一Host、窗口转发闸门、Stop drain和Bridge入口。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp/.h`：Admission gate、controller observer、隐藏输入、停止/join和换代。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp/.cppm`：非owning HWND/input wake、真实cursor消息、关闭平移gate。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.HiddenWindowTest.cpp`：真实双HWND/Host/Controller/persistence fixture。
- `InkeysHeadlessTests/draw3_contact_tests.cpp`：Coordinator Closing状态机及既有producer-gate相关静态逻辑回归，不含普通Controller全Run。

## Related Specs

- `.trellis/spec/native-desktop/input-and-ink.md`：生产输入/ClosingDiscarded/单释放、Reset静止、普通Controller验证硬要求。
- `.trellis/spec/native-desktop/draw3-integration.md`：单Host、独立device、FIFO命令/保存屏障、两窗、功能gate和presenter合同。
- `.trellis/spec/native-desktop/errors-logging-and-resources.md`：资源逆序、managed线程、15秒监督和停止后不能悬空借用。
- `.trellis/spec/native/quality-and-validation.md` 与native-desktop build spec：生产逻辑验证、架构/Win7/完整Solution证据分开。
- 当前子任务prd/design/implement及父completion/handoff；冻结 `research/post-commit-completion-audit.md` / `research/startup-boundary-postcommit.md`。

## External References

- [RTS threading](https://learn.microsoft.com/en-us/windows/win32/tablet/threading-considerations-for-the-stylusinput-apis)，官方说明2021-01-07，2026-09-30查询：同步Enabled/Disabled可在属性操作者线程调用；异步队列滞后不能外推本产品只用的Sync plugin。
- [IStylusPlugin::RealTimeStylusDisabled](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-istylusplugin-realtimestylusdisabled)，官方2024-02-22，查询同日：disable或remove会通知；未列故障HRESULT下in-flight drain保证。
- [IRealTimeStylus::put_Enabled](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-irealtimestylus-put_enabled)，官方2024-02-22，查询同日：控制是否收tablet数据；不在enabled状态不收事件。
- [IRealTimeStylus接口](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nn-rtscom-irealtimestylus)，官方2022-07-26，查询同日：Sync/Async插件、collection动态变更；这些接口支持声明不等于本轮Win7动态通过。
- RemoveStylusSyncPlugin独立页面本轮open不可访问；未据其缺失制造已确认停止保证。没有使用第三方博客推导COM所有权。

## Caveats / Not Found

- 全部本轮动态case NOT VERIFIED；没有运行/构建/Git操作。上述源码身份错误已即时通知root，但尚未把它写为动态PASS或现场唯一根因。
- acquireStroke/modelerReset/modelerUpdate三条失败需分别按当前模型/资源前提验证；本研究确认错误清理目标，不宣称在正常配置中每种失败都常见。
- 当前没有全面plugin in-flight计数；存在packets reader gate与诊断锁，二者不能冒充所有callback排空。成功API基础与失败残余分别报告。
- consumer普通页boundaries可以与RTS terminal并行；同RTS Down与Up的任意parallel被writer ownership排除。不可把publicCoordinator任意interleaving统一称为真实设备bug。
- 当前Canvas navigation gate关闭，Whiteboard入口亦按用户维持；不给gesture/PPT测试擅自开放功能。未验证新Commit/用户图形驱动行为。
- Research角色隔离未读取implement.jsonl/check.jsonl；已按Active task读取实际prd/design/implement、spec、当前代码与已有研究。输出仅本research文件。
