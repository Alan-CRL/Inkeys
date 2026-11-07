# Research: Host 映射刷新、线程争用与报点退化

- Query: 36b418b0之后绘制明显卡段，是否新增Host刷新/锁导致RTS报点变慢或堆积；如何恢复到原来或更优。
- Scope: internal；只读当前源码及主会话提供的42f4b9a8基线/差异，不编译、不运行GUI、不修改产品。
- Date: 2026-11-06

## Findings

### 1. 关键排除：已提交代码没有“每帧无条件取得RTS写门”

当前RealTimeStylusInput::ApplyPendingWindowMapping在Draw3.RealtimeStylus.cpp:3277–3290，首先执行requested的acquire-load，并在ownerMappingRevision相等时立即返回（3279–3280）。只有不相等才获取diagnosticsMutex、复制plugin强引用并调用私有刷新。

因此，不能仅看到StylusSyncPlugin::ApplyPendingWindowMapping（1190–1195）内部无条件创建RtsStateWriterGuard，就宣称所有绘制帧都取得写锁。外层去重确实存在，ownerMappingRevision的持有者是绘制owner（2855–2856），成功取得plugin并调用后更新（3289）；初始化把requested递增（2975–2978），并非每帧递增。

正常已同步状态每次Host调用新增成本为：一次外层函数调用、一次64位acquire-load、一次普通owner字段比较/分支。无diagnosticsMutex、plugin引用计数、callback lease、stateWriterMutex、GDI或COM查询。代码本身不能支持“每帧取得写门造成大幅稳态退化”的结论。

### 2. PumpBridgeState的实际触发条件也需准确

- Draw3.Host.cpp:1778：本次增加stylus.ApplyPendingWindowMapping调用，放在PumpDisplayScale之前。
- Host:2044–2052：ConsumeBridge处理General/Command control wake后调用PumpBridgeState。
- DrawingController.cpp:13467–13479：真正取出空record控制标记后，先AcknowledgeControlWake，再调用observer.controlWake；并非每个普通Move/每个Draw都调用。
- Host:2214、2294：启动/首帧前也显式调用PumpBridgeState。
- 主会话提供的Build/task-research-palm/rts-host.diff.txt证明Host仅增加上述调用及ForwardMessage的Notify调用；原来的displayScaleDirty.store与PublishControlWake已经存在。

所以也不能将原有窗口消息→displayScaleDirty→ControlWake链整体算成本次新增回归。

### 3. 真正新增的窗口侧工作与条件

WindowMappingRevision::Notify（RTS.cpp:141–152）：

| 消息 | 新增工作 | 是否改变requested |
| --- | --- | --- |
| WM_WINDOWPOSCHANGED，同一显示器 | MonitorFromWindow + monitor.exchange | 否 |
| WM_WINDOWPOSCHANGED，显示器改变 | 同上 + fetch_add | 是 |
| WM_DPICHANGED，相同WPARAM | dpiMessage.exchange | 否 |
| WM_DPICHANGED，WPARAM改变 | exchange + fetch_add | 是 |
| WM_DISPLAYCHANGE | 无条件fetch_add | 是 |
| HWND不匹配 | 一个window.load后返回 | 否 |

Host::ForwardMessage（3045–3050）仍对这三种消息标记displayScaleDirty并发布ControlWake（旧行为）。ControlWake内部先CAS controlWakePending，重复时只SignalWake；不会为每条重复消息无限增加新的队列项（ContactInput.cpp:369–397）。但消费者每次Acknowledge会清pending，持续窗口消息仍能不断触发控制处理——这是频率问题，需要实测计数，不能从合并位直接宣称零开销。

一个可减少但尚不能认定为严重卡顿根因的点：即使HMONITOR/WPARAM未变，Notify仍做原子exchange；这会产生写入/缓存一致性流量，而requested与这些字段同处一个很小的结构体（RTS.cpp:134–139）。可改成load比较、仅改变时写入，并保留单一窗口owner或并发调用的正确性。是否需要cache-line隔离应由基准证明，不能先加padding当修复。

### 4. 条件成立时，新增跨线程刷新确实会拒收报点

真正改变requested时，会形成以下链：

```text
窗口Notify递增requested
  → Packets看到mappingRevision不匹配而返回S_OK，不发布Move
  → control wake使绘制owner刷新
  → 持有plugin强引用 + callback lease
  → stateWriterMutex + stateGate writer bit
  → 取消旧contact、清decoder、BeginLifecycle、更新mappingRevision
  → 下一个RTS慢路径重建decoder
```

代码锚点：
- WindowMappingCurrent：RTS.cpp:1814–1816。
- InvalidateWindowMapping：1819–1827，只在revision不同才清理。
- 外层保活：3277–3290；释放diagnosticsMutex后才调用plugin，锁顺序没有把diagnostics锁带入writer等待。
- 私有刷新：1190–1195。
- Packets：1516–1525，状态门失败或revision不匹配统一记成state-busy并返回S_OK。
- InAir：1482–1492，状态门/旧revision失败则不发布光标样本。
- RtsPacketStateGuard：82–98，单次CAS，遇writer或CAS竞争立即失败，不等待。
- RtsStateWriterGuard：109–123，先锁writerMutex，置writer位，再自旋等已有reader退出。

这里必须区分：
1. reader遇writer是应用丢弃当前通知/本批最后样本，不是把该包塞入应用队列等待重放。
2. writer会等现存reader完成；RTS慢回调之间会争writerMutex。若某慢回调持写门时发生慢系统调用，会扩大其他callback延迟/拒收窗口。
3. 真正高频的映射变化通知可造成大量取消/缺decoder，表现为断段，但当前没有现场WM_DISPLAYCHANGE/monitor/DPI/requested频率证据，不能断言用户设备正在发生此情况。
4. 正常固定窗口、固定monitor/DPI、无display-change时，这条取消链不会不断执行。

### 5. 日志目前不能把三种现象分开

Packets把state-gate未取得与mappingRevision失配合并为state-busy（1517–1520）。现有产品Packets诊断又是每plugin最多每秒一次（spec/input-and-ink.md:21；ShouldRecordInputPacket）。因此单看稀疏日志的“报点慢”，不能区分：RTS回调实际到达率下降、应用state gate/revision拒收、渲染消费者只读到最新mailbox造成合并。

补充诊断应是固定大小计数和短内存采样，不逐包格式化/输出：
- callback_count与packetCount总和（硬件报点数/通知数分开）；
- accepted_move、state_gate_busy、stale_mapping、no_binding_decoder分开计数；
- requested变化原因及次数、owner实际writer次数、GDI倍率查询次数/耗时；
- Draw消费的snapshot序号和QPC；
- Down/Up/UpdateMapping/metadata重建的耗时分布。

### 6. 修复方向：缓存与发布优先，不把“无条件写门”当根因

以下为初轮研究的候选优化，不是最终建议；用户随后要求收紧到倍率获取的最小边界，最终以第8节为准，尤其不建议本轮实施其中的两阶段快照/发布设计：

1. 保留现有外层revision fast path。不要将同样的逻辑再加一遍当作已经定位的修复。
2. 窗口只在实际映射相关变化时发出新revision；同monitor位置消息和相同DPI值先比较再写。WM_DISPLAYCHANGE仍不能仅凭分辨率相同就丢掉，因为可能有方向/拓扑变化；可按完整有效映射快照复核后决定是否发布新generation。
3. HDC查询结果作为窗口映射快照缓存，查询放在初始化/真正映射变更的慢路径，并尽量在取得RTS state writer gate之前完成；不把GDI查询塞入Packets或每Down。
4. 若进一步将HDC获取移出同步RTS回调，窗口owner先在锁外构造S及结果，再用短发布锁/固定snapshot发布；RTS慢路径只复制完整带revision的快照。不要分别atomic写Sx/Sy导致混代，不用每packet shared_ptr引用计数来换取表面“无锁”。
5. decoder与active binding继续在建立时缓存S、C、面积因子；稳定Down命中缓存，Move/Up读绑定结果。真正变化时沿用取消旧接触和generation检查，不能为提速直接撤掉生命周期安全。
6. 当前owner可能在RTS慢路径已处理同一revision后再取得一次writer；可考虑在发布的applied/cancelled revision上再做一层无争用判断，但这是低频优化，不是稳态卡顿的充分解释。只在测得有重复写门时采用。
7. 运行期间真实变化后的取消应仍可在“没有后续RTS包”时完成；不能退化为只等下一次Down才清理旧橡皮。

上述3–4须由主会话结合RTS回调研究给出最终最小边界；不能只在Host加fast path便宣称与旧版一样快。

### 7. 验证：证明缓存和去重，而非只测数学

确定性无窗口测试：
- 真实public ApplyPendingWindowMapping在无变化情况下执行一万次；断言GDI/COM查询数、writer进入数、取消数、plugin借用次数均为0，不能只直接调用私有Invalidate helper。
- 让RTS reader持有state gate，另一线程反复调用无变化的public Apply；必须在reader释放前完成，证明没有偷偷走writer；测试退出先放行再join，禁止靠挂起线程。
- 同monitor WM_WINDOWPOSCHANGED、相同DPI通知burst后requested不增加；按注入接口分别断言Monitor查询与状态写次数，不需要创建HWND。
- 真实变化后owner只取消一次；在查询期间再变化只发布最新完整快照，旧binding不复活。
- 无后续包、Up与owner刷新并发、Shutdown与刷新并发、plugin尚未发布/已清空均覆盖；plugin强引用、callback关闭和drain顺序保持。

性能对照：
- 同一release或同一debug工具链下，对旧基线42f4b9a8、已提交36b418b0及候选版跑同一生产CPU回放；记录Mouse/Pen/Touch、1/多contact、单包与批包。
- 稳态阶段不注入mapping变化，要求HDC/COM查询数=0、因mapping而新增的writer/cancel/拒收=0；记录callback/packet吞吐及P50/P95/P99/max耗时、accepted率。
- 独立压力轮注入同monitor窗口消息与真实revision变化，不把稳态吞吐与预期取消轮混合统计。
- 可控慢HDC查询fixture用于证明查询不会锁住Packets读取；不要只断言函数最终返回正确倍率。
- “同样或更好”应先由同条件交替多轮基准排除明显CPU退化，再以用户现场真实输入验证；没有现场计数/时间线前，不给出报点率已恢复的承诺。

## Files Found

- Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp：窗口通知、ControlWake消费者及启动owner。
- Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp：mapping revision、owner fast path、callback门及writer/reader gate。
- Inkeys/Inkeys/Drawing/Draw3/Draw3.RtsStartup.h：callback lease、Close/Drain生命周期。
- Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp：控制标记消费与Host observer调用。
- Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.cpp：ControlWake合并与确认。
- Build/task-research-palm/rts-host.diff.txt、rts-before.cpp.txt：主会话提供的只读基线/差异。

## Related Specs

- .trellis/spec/native-desktop/input-and-ink.md：RTS坐标映射、热路径、停产与诊断合同。
- .trellis/spec/native-desktop/canvas-freeze-boundaries.md：不阻塞诊断和Move唤醒握手。
- .trellis/spec/native-desktop/cpp-conventions.md：线程/锁边界与最小修改。

## External References

本研究结论来自当前仓库和主会话提供的基线，没有依赖未核验的外部文档推断消息频率或API延迟。Windows API单位定义沿用本任务已有research/mapping-design-evidence.md；实际系统消息频率与RTS回调时序仍需运行证据。

## Caveats / Not Found

- 未运行GUI、真实RTS设备或性能基准；没有现场事件计数、调用栈或调度时间线。
- 未发现稳态每次Host pump必然取得RTS写门的代码；外层去重明确排除了这一说法。
- 未证明WM_DISPLAYCHANGE或跨monitor变化正在用户现场高频发生。
- 只从Host/跨线程代码不能精确宣称严重稳态退化的唯一根因；RTS同步慢回调内新增系统查询及数据流需与另一研究报告合并评估。

### 8. 范围收紧后的最终建议：撤回新增Host刷新链，复用原RTS生命周期

主会话转达用户进一步质疑：最初只是修正倍率获取，不应为此加入数百行新的RTS生命周期行为。按这一明确范围，前面的广义性能优化候选不作为本轮实施方案。推荐恢复原先的缓存/调度时序，只替换量纲错误的数值来源。

#### 必要改动与额外扩展的区别

必要：
- 取得同一HWND的HDC比例S，正确释放HDC、兼容Win7；成功初始化时缓存。
- 在旧共享位置倍率的读取位置使用S，避免首context假设。
- 宽高预计算组合S/C，输入包继续乘缓存因子；Palm的sqrt(W×H)保持。
- 为上述单位关系、元数据失败和资源配平增加有判别力的回归。

并非完成上述公式替换所必需：
- 新增WindowMappingRevision、独立requested/ownerMappingRevision协议。
- 新增Host→RTS的NotifyWindowMappingChanged/ApplyPendingWindowMapping接口、control wake后主动取消机制。
- Packets/InAir额外读取新的全局mapping revision。
- 为这条新机制而增加的Shutdown/迟到Up/重复通知状态和大规模生产probe注入。

这些扩展是为了比旧版更主动地覆盖HWND/DPI变化，而不是数学修正的前提。即使没有证据证明它们必然造成用户的严重卡段，也没有必要在本次范围里保留这一新增调度面。

#### 具体最小流程

1. InitializeAttempt在现有初始化边界查询一次HDC的S并缓存；如果获取失败则按既有初始化失败与Win32Mouse回退处理。Enabled复用这次成功结果，不做第二遍HDC查询，也不需要为第二遍查询失败单独增加结果锁存协议。
2. 保持旧RealTimeStylusEnabled、UpdateMapping、TabletRemoved/必要全量恢复、TabletAdded增量及InRange/Down补建时序。原来全量重建共享倍率时查询首context的地方，换成一次HDC查询；正常增量上下文继续用本代已缓存S。
3. Down命中context cache时直接绑定decoder及其generation，GDI/COM倍率查询为0。仅原来允许的冷cache/错误恢复边界执行一次查询与补建；不改成每Down都重新查询，更不逐Packets查询。
4. Packets/InAir恢复旧读门、binding查找、DecodeSnapshot、发布的结构；移除新增的WindowMappingCurrent检查。位置仍乘缓存S，宽高仍乘预计算S/C系数。
5. 撤回Host.cpp本次两处新增调用；删除仅服务于Host主动失效的WindowMappingRevision、ownerMappingRevision、私有/公开Apply/Notify及相关状态。保留原有displayScaleDirty/ControlWake链，不借机重写窗口调度。
6. 恢复由原RTS lifecycle事件负责取消旧contact、清decoder与递增generation。保留callback lease、旧writer/reader gate和原Shutdown停产/排空顺序。
7. 测试集中验证真实生产换算与缓存：warm Down和N次Packets查询计数=0；Enable/每次真实RTS全量重建只查询一次S；改原生计数精度不影响位置/物理接触尺寸；无新增Host writer或revision拒收。无需两阶段staging框架或新工作线程。

#### 明确功能取舍

删除Host主动失效链后，如果窗口收到WM_DPICHANGED/跨monitor变化，而RTS没有发出UpdateMapping或其它旧的重建事件，输入映射不再由这条新增链主动刷新/取消；这恢复的是42f4b9a8的时序保证，不能继续宣称上一轮新加入的“所有HWND变化都主动更新输入”能力仍然存在。

这一更广的多屏/DPI主动刷新问题应单列为后续增强，只有用户确实需要超出旧版保证、且能说明具体场景时，再讨论最小追加机制。本轮不提出新的两阶段发布状态机。

#### 对“原来一样或更优”的可承诺范围

- 结构上恢复旧稳态Packets路径，倍率/面积因子仍在缓存建立时预计算，没有新增GDI/COM、互斥锁、全局revision检查或逐包除法。
- 初始S复用可减少Enabled内一次重复系统查询。
- 还需同条件基准与现场确认最终吞吐/延迟；不能仅凭删除扩展代码保证任意设备测量结果。
- 如果最小版仍有严重卡顿，继续沿已分离的callback到达、应用拒收、消费者快照合并调查，不回到“必然每包查询”或“必然每帧写门”的错误归因。
