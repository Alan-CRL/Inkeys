# Research: RTS 热路径与连续绘制卡顿调查

- Query: 36b418b0 后持续同一笔绘制卡段，是否每次 Packets 查询倍率；如何恢复修改前或更优的输入处理成本。
- Scope: mixed；生产 RTS 主路径及其同步消费者，比较父会话提供的42f4b9a8原文与36b418b0 diff。
- Date: 2026-11-06
- Restrictions: 仅研究；未修改产品/规范、未编译、未启动GUI、未执行Git操作。父会话将只读比较资料放在忽略目录Build/task-research-palm/。

## Findings

### 1. 明确排除：当前提交没有逐Packets查询倍率，也没有每帧无条件抢输入写门

下表全部是静态调用链结论，不是现场计时结果。短路径均指Inkeys/Inkeys/Drawing/Draw3/。

| 路径 | 当前行为 | 相对42f4b9a8新增的稳态工作 |
| --- | --- | --- |
| Draw3.RealtimeStylus.cpp:1500 Packets | callback lease → 原有non-waiting reader gate → binding/decoder缓存 → DecodeSnapshot → PublishMove/cursor | 成功进入reader后一次WindowMappingCurrent，即一次requested.acquire load及比较 |
| 同文件:1471 InAirPackets | cache-only hover解码；没有Ensure或Build | 正常有效hover两次revision load/比较 |
| 同文件:1277 StylusDown | 原有writer边界内Resolve/Ensure；已有有效decoder直接命中 | Ensure里的Invalidate读取revision一次，发布Down前再读一次；稳态命中不查GDI/COM |
| 同文件:1389 StylusUp | 原有writer终态路径 | 增加Invalidate的revision检查；没有倍率查询 |
| 同文件:3277 public ApplyPendingWindowMapping | ownerMappingRevision等于requested立即返回 | 正常绘制帧一次acquire load/比较，未进入diagnosticsMutex或plugin writer |

关键证据：
- Packets:1516–1557没有QueryWindowPositionScale、GetPacketDescriptionData、PrepareAreaTransforms调用；snapshot坐标在DecodeSnapshot:1066–1067只乘缓存的inkToCanvasScaleX/Y。
- SourceForCursor:973只搜索已缓存pointerSources；不重新查询设备。该函数在diff中未改变。
- PrepareAreaTransforms:608预计算S/C，在PublishStaged:812、PublishIncremental:831中调用；长度的除法不在每包执行。
- 私有plugin::ApplyPendingWindowMapping:1190确实总拿writer，但必须与public wrapper:3277的clean fast-return一起看，不能据私有方法声称每帧无条件拿锁。

因此，“每次Packets取倍率”和“每帧无条件占用RTS写门”都不符合当前36b418b0源码。一个新增原子读取不能被静态分析直接宣判为用户描述的严重持续卡顿根因。

### 2. 实际查询点与可能阻塞区域

QueryWindowPositionScale:185在支持新DPI API时执行：GetWindowDpiAwarenessContext一次、SetThreadDpiAwarenessContext设置/恢复各一次、GetDC一次、GetDeviceCaps两次、ReleaseDC一次。Win7省略前三个DPI-context调用，其余GDI查询相同。

生产调用点：
- InitializeAttempt:2980：预检，结果initialScaleX/Y随后未复用；这是启动方查询。
- StageAndPublishDecoders:1887：Enabled内再次查询。
- RebuildCurrentContextDecoders:1916：UpdateMapping/TabletRemoved/增量失败后的全量重建。
- EnsureContextDecoder:1943：仅当前shared scale无效时查询；已有slot在1935–1937直接返回。

后三种查询处于RtsStateWriterGuard作用域。writer:109–124先拿mutex、置writer bit，再等待旧reader退出。Packets碰到writer bit或CAS失败直接返回S_OK，丢弃当前包，并不排入自有重试队列。Down/Up等writer则会等待mutex。

这构成真实的新风险：如果实际映射通知频繁或HDC查询在系统调用内变慢，RTS慢回调会占用同步输入线程/写门更久，既可能让系统待处理包聚集，又可能让并发reader拒收。但是源码未证明稳态笔划期间这些查询频繁发生，必须以查询次数和通知计数判别。既有设备元数据COM/Pointer枚举也在BuildContextDecoder:997–1056内；它原来已存在，不应把全部冷路径成本算成此次新增。

### 3. 新映射失效有比“慢一点”更强的表现：取消整条输入身份

- WindowMappingRevision::Notify:140–151只在monitor变化、DPI值变化、或WM_DISPLAYCHANGE时增加requested；同monitor WINDOWPOS不会增加代次，但仍进行MonitorFromWindow和monitor.exchange。
- Notify后、owner取消前，Packets:1517以“state-busy”原因拒收stale revision；该原因与真正writer竞争混用。
- InvalidateWindowMapping:1819–1827在代次变化时调用ResetDecoderLifecycleState(true)，清除全部decoder和active bindings；不仅更新两个浮点系数。
- ResetActiveContactState:1792–1805对已绑定接触发布Cancelled；所有旧绑定被移除。
- 即使owner已把mappingRevision更新为新requested，现有物理笔的后续Move也没有binding，直到新的合法Down才重新取得身份。因此频繁失效更像明确断笔/取消，不一定像长期积压逐点缓慢追赶。

用户后来明确为“同一持续笔划、位置更新间隔变长”。在没有对应revision/Cancel证据时，不能只用上述取消机制解释全部现象。

### 4. 同步间接调用未发现因新坐标值增加的重循环

- Draw3.ContactInput.cpp:775 PublishMove：有限位置检查、查找现有record、try writer latch、固定字段发布；竞争直接false，不进行等待。S或面积数值改变没有改变这里的循环次数。
- 同文件:475 FindProducing扫描已占用slots；这是既有算法，改动未新增其扫描或队列。
- Draw3.WindowControl.cpp:1053 PublishPenCursorSample：既有source/owner/mailbox处理；1106注释和分支明确inContact不每包RequestDrawingCursorRender，只有hover请求。
- 同文件:1108仅valid/inContact/inverted变化才QueueSystemCursorRefresh；新坐标数值本身不进入该条件。
- 现有PenSource/cursor诊断会受开关影响，但本次没有新增逐包输出或改变其开关，不能直接指认为此次代码回归。

没有发现“窗口倍率改变→Packets内部新增大范围循环”的静态分支。不过数值变化可能影响下游建模/擦除工作量，不能只凭shader轻载排除全部CPU消费开销；此项由主会话检查HUD口径和Host消费链。

### 5. 既有批处理与诊断口径可能影响观感，不能误当新根因

- Packets:1514–1515只选择batch的最后一个packet；InAir:1480同样如此。这是基线已有行为。
- 若同步线程被某个回调阻塞，之后一个callback可能携带更多packetCount；本程序仍只发布最后一点。可呈现“有效位置更新变疏”，但不能因此推断硬件采样率下降。
- 当前desired packet properties:51–61只有X/Y、压力、倾斜、方位、宽高，没有设备采样时间属性。
- snapshot.qpc由DecodeSnapshot调用处QueryQpc获得，不能测出进入当前同步回调之前的RTS/驱动等待时间。
- Draw3.CanvasDebug.cppm:55–57和.cpp:634–645的Lat是快照QPC到Present返回；不是RTS callback处理时间。100ms行值刷新限制在CanvasDebug.cppm:23，也不能用肉眼数HUD行变化作为包回报率。
- RecordInputDiagnostic:1692和ShouldRecordInputPacket:1707每秒只记录一次Packets；无计数分母，不能据此计算真实回报/丢弃率。

### 6. 建议修改范围：把新系统查询彻底移出RTS同步路径，保留原缓存架构

这是一套有明确性能合同的设计建议，不是声称已经证明现场根因。

1. 建立窗口映射快照：在现有窗口/生命周期owner侧查询HDC并缓存S；启动预检得到的S直接复用，不在Enabled重复查询同一代。
2. RTS的Enabled/InRange/Down使用已经准备好的窗口快照标量，当前context C及metrics仍沿原来decoder建立/失效机制获得。不得退回“每个Down都查GDI”的实现；原代码其实是context缓存命中时连Down都不再查。
3. PrepareAreaTransforms仍只在decoder发布时烘焙S/C；Packets/InAir/Up不查询GDI/COM、不分配、不阻塞、不做新倍率除法。Down已有decoder时也应零查询。
4. 将“需要检查窗口映射”与“已确认当前映射无效”区分；减少无意义通知引起的writer/清空，但真实RTS UpdateMapping或确认的映射代次变化仍按原安全合同取消一次。不能仅因S相同就跳过可能变化的C/设备映射，也不能去掉所有失效检查强行继续旧坐标。
5. 真失效时慢查询在writer之外准备，只有提交/取消持有短writer；不能把COM指针跨apartment裸搬到绘制线程，不新增无界等待/无限seqlock重试来替代当前non-waiting reader。
6. 不先改batch仅取最后点、contact队列或渲染策略；这些基线行为一并重构会失去清晰对照。也不回退S/C修复或Palm等面积规则来掩盖性能问题。

### 7. 由实施者负责的性能证据与验收

这些是我们修复时自行加入并运行的验收，不以要求用户提供更多日志作为开始修改的前提。调查结论和方案基于静态调用路径；现场体感反馈作为退化事实保留。轻量可关的固定计数器/时段聚合，不能逐包写控制台：
- callback arrivalCount与累计packetCount（区分回调频率/原始样本数）。
- acceptedMove、stateGate拒绝、mapping-stale、missing-binding、coordinator contention分别计数。
- HDC查询次数/总耗时/最大耗时及触发原因；context metadata查询次数；revision增量原因和cancel数量。
- callback entry/exit耗时分位数；必要时拆解gate、decode、PublishMove、cursor sink四阶段。
- consumer实际读取的新sequence数，以及消费/Present间隔；这些和callback入口分别记录，不能混为一个“RTS耗时”。

无窗口回归必须验证：
- 预热后连续大量同一触点Packets，HDC/GDI/COM查询计数增量为0，PrepareAreaTransforms计数增量为0，所有应接受的Move都成功发布。
- 预热Down与后续Down缓存命中也为零查询；真实代次变化只更新一次且不漏Up/Cancelled。
- 没有实际变化的重复窗口通知不产生cancel或writer进入；真变化仍拒收过期代次。
- A/B比较42f4b9a8、36b418b0、修复候选，同工具链/同配置/同CPU输入回放；记录P50/P95/P99、长尾最大值和拒收率，不能只测试一次平均耗时。
- 目标为稳态回调不劣于原版的可重复测量范围、没有新增毫秒级长尾、无意外丢弃/取消；系统映射查询只出现在确认的低频更新边界。
- 用有界阻塞的假查询验证“owner查询慢时不占RTS writer”，而不是只断言函数最终成功。旧59项正确性probe没有这种性能/锁停留保证。

## Files Found

- Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp：本次新增映射、实际packet路径、cache和gate。
- Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cppm：public通知/owner消费接口。
- Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.cpp：现有非等待Move发布和record快照。
- Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp：现有PenCursor sink与接触期间唤醒抑制。
- Inkeys/Inkeys/Drawing/Draw3/Draw3.CanvasDebug.cpp/.cppm：HUD延迟/刷新口径。
- Build/task-research-palm/rts-before.cpp.txt、rts-host.diff.txt：父会话提供的只读基线比较资料，不作为交付工件。

## External References

- [Microsoft: Performance Considerations for StylusInput](https://learn.microsoft.com/en-us/windows/win32/tablet/performance-considerations-for-the-stylusinput-apis)：同步插件复杂性会增加墨迹收集延迟，应最小化；本次按此限制新增系统查询的位置。
- [Microsoft: Plug-ins and RealTimeStylus](https://learn.microsoft.com/en-us/windows/win32/tablet/plug-ins-and-the-realtimestylus-class)：同步插件通常运行于RTS高优先级线程，适合计算轻量的实时工作。
- [Microsoft: Threading Considerations](https://learn.microsoft.com/en-us/windows/win32/tablet/threading-considerations-for-the-stylusinput-apis)：不同回调线程分类不能代替本项目state gate的同步合同。

## Related Specs

- .trellis/spec/native-desktop/input-and-ink.md：新映射与面积合同；必须补性能/计数门槛，不能仅写“没有逐包查询”。
- .trellis/tasks/archive/2026-08/draw3-source/archive/2026-08/08-07-rts-packet-decoder-hot-path/design.md:202–232：原有cache-only Packets/InAir和non-waiting gate合同；本次方案应保护此边界。

## Caveats / Not Found

- 当前源码未提供足以确认持续卡顿唯一根因的现场计时；不能把罕见失效机制当作已证实持续发生，也不能宣称一处atomic load解释严重退化。
- 必须确认用户实际运行二进制含public ownerMappingRevision fast-return，不能拿源码排除旧构建存在的每帧writer回归；此项由父会话核查，尚不是发现。
- 未执行真实RTS、Surface、Win7或GUI测试，也未运行基准；所有调用计数为静态路径数量。
- 不建议要求用户继续凭感觉试倍率；先提供明确缓存/线程合同及可自动验证的性能门槛，再以真实设备验证系统回报率。
