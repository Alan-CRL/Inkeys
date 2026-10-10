# 多来源联动纯值服务合同（P1）

## 1. Scope / Trigger

新增或修改PresentationLink适配器、目录、仲裁、绑定确认、命令路由时应用本合同。P1实现位于Inkeys/Inkeys/Business/PresentationLinkContracts.h及PresentationLinkService.h/.cpp，命名空间Inkeys::Business::PresentationLink。

P1仅提供标准库纯值状态机、fake adapter与可控时间测试。当前产品COM/Draw3消费者未接入；本合同不能替代旧com-contract.md/native-session-ui3.md。共享内存/pipe序列化、真实COM多候选、权限helper、实际Win7/.NET4及呈现时延均待P2-P5验证。

## 2. Signatures

PresentationLinkService(std::string hostId, Policy policy, Limits limits = {});
RegisterSource(const SourceInfo&, Revision epoch, Tick now) -> ReceiveResult
Receive(const DirectoryMessage&, Tick now) -> ReceiveResult
Disconnect(const std::string& sourceId, Revision epoch, Tick now) -> bool
ObserveForeground(const ForegroundObservation&, Tick now) -> bool
Advance(Tick now) -> bool
Confirm(const BindingConfirmation&, Tick now) -> bool
Selected() -> SelectedSnapshot
TakeBindingRequest() -> optional<BindingRequest>
SubmitCommand(const CommandRequest&, Tick now) -> CommandStatus
TakeCommands() -> vector<Command>
ReceiveCommandResult(const CommandResult&, Tick now) -> bool
CommandOutcome(Revision commandId) -> optional<CommandStatus>
GuardCommand(const Command&, const Route&, const Candidate&, const SourceInfo&,
    Tick now, ExecutionLedger&) -> CommandStatus

Tick/Revision均uint64_t。同一owner串行调用；now采用调用方统一单调时间单位，不能混入远端墙钟或另一个进程的未经校准时基。服务不创建线程、HWND、COM或transport。Policy的9个时限和weakSamples显式传入，测试数值不构成产品实测初值。

## 3. Contracts

| 值 | 必要含义 |
| --- | --- |
| Envelope | major/minor/kind、hostId/sourceId、epoch/sequence；Full/Delta/Heartbeat。epoch只能明确RegisterSource换代，来源消息不能自行复活旧epoch。 |
| SourceInfo | backend/application/transport/observerDomain分别描述，observerSession与被观察进程session不可混同；health/capabilities/controlMode/backendPreference。权限与transport不增加show活动等级。 |
| Candidate | candidateId/localShowId、generation/stateRevision、lifecycle/quality、optional document、ShowIdentity、Page、Activity。完整目录与增量删除一次校验后提交；DirectoryMessage.source可带同epoch的元数据、health与能力更新，Heartbeat不携带该更新。能力变更需与所有保留候选一致，否则整批Invalid；adapter须同步更新受影响值或发full，不能只撤声明却留下被该能力授权的旧值。 |
| ShowIdentity | session/processId/processCreated/window/windowLifetime、processVerified/windowVerified。只有可信adapter证明的跨源连续窗口实例归并；来源自己生成的generation不能充当windowLifetime。 |
| Document/Page | document key/scope/quality与可选path/title；page kind、optional ordinal/total/stableKey、identityQuality/revision/topologyRevision/topology。未知不是0/1/最后页；EndScreen必须明确。 |
| Activity/Foreground | providerActive三态、processHint/wpsHint、observationRevision；统一foreground revision/kind/show/processHint/selfInduced。heartbeat/普通页更新不是独立活动观测。 |
| Route | source/candidate/localShow、epoch/generation/logicalShow、showSessionRevision/routeRevision/targetRevision。命令与绑定确认都按此身份校验。 |
| SelectedSnapshot | Unknown/Pending/Stable、reason、optional route/candidate、capabilities、activityTier/pageSafe。pageSafe不代表Draw3 Present/UI ack ready。 |
| Command | commandId、完整route、deadline、operation、optional gotoOrdinal/expectedPage；执行owner用ExecutionLedger校验同source epoch与ID高水位。 |

Envelope首版只接受major=1/minor=0。初始/断连/sequence gap后必须full；经host/version/source/epoch/sequence核准的gap先撤销route并要求重同步，之后才校验payload；非法payload保持目录及高水位不变且不消耗其sequence，不能保留旧可执行route。合法Full可直接完成该次重同步。来源保留候选generation/state高水位，新的包sequence不能使旧候选代复活。heartbeat只证明通信存活，不能刷新Office页或活动事实。目录/identity/页数/历史/命令记录有界，不能无限保存退役状态。默认最多32来源、每源64候选、总256候选；身份文本32 KiB、页数10000、候选文本1 MiB、目录文本4 MiB。Policy各时限必须大于0、weakSamples至少2；limits的历史容量须足以覆盖对应在用容量，构造参数不合法抛std::invalid_argument。

只将可靠实例证据相同的show合并；同文件/同标题/同PID不同窗口独立。退出、删除、观测缺口、旧epoch与PID/HWND重用不能自动沿用旧场次。相同退役generation的Inactive/Ended可以重复被观察，仍校验state高水位及可比的同revision字段一致性，不分配logicalShow或route、不续可执行寿命；同代Active/Unknown及更旧generation仍拒绝。Full含终态不能阻塞其他健康候选刷新，真正新放映须更新generation/实例生命周期。单一权威来源提供动态快照，禁止跨来源拼接文稿和页码。

先选show活动等级，再在show内择优source。真实已验证foreground最高，provider/PID/WPS是弱提示，同等级保持。弱挑战需持续且有独立新revision，多源重复事实不计多票。反向软切换与来源恢复有各自稳定窗口；真实foreground变更/当前目标失效先撤销旧命令route并快速准备；即使新前台show快照尚不可用，也不得继续向旧后台show发命令。Inkeys焦点维持外部上下文；selfInduced确认不作为独立挑战。

绑定只产出纯值请求，不主动Activate。Confirm匹配request/完整目标并复核最新仲裁意图，B准备时切C则旧B确认拒绝。Foreground请求在精确前台证据失效或降为弱提示后也须取消；即使BestChoice仍为同一个B，旧强请求不能越过weakWindow/独立样本门，须按当前证据重新挑战。软挑战期间有效A页变化仍快速发布；强切换准备中不能把新用户命令发给后台A。确认失败/timeout使该未变化的candidate stateRevision暂不再准入，新的状态才能重新prepare，避免每tick反复调用owner。

同show换来源保持logicalShow/showSessionRevision，routeRevision推进；只有已证明page映射相同才保留targetRevision。不同页、未知映射或新场次推进target；低能力来源不得继承另一来源的稳定页面墨迹身份；同show跨源Ordinal未证明映射时输出pageSafe=false，不能用后续heartbeat解除隔离。绝对页观察不自增页码，不等待活动防抖。P1的pageSafe只授权可证明的普通Slide；EndScreen保留类型/总数/拓扑，不伪造普通页或ShowEnded。P2的真实bridge仍须应用旧结束页独立slot与Present/UI ack合同。

命令仅一个已确认route，无广播。提交、提取与fake执行前均校验目标/能力/截止时间；result与页面/绑定分离。已发送后超时/断连为OutcomeUnknown，不自动改投重试，迟到ACK不污染新绑定。P1并不声称跨崩溃exactly-once。GuardCommand的currentRoute/currentCandidate必须来自执行owner当前可信状态，不能从收到的command.target复制；否则完整目标比较失去意义。ExecutionLedger在明确接受新source epoch后由owner重置，拒绝重放后不再次执行。

## 4. Validation & Error Matrix

| 场景 | 结果 |
| --- | --- |
| 未注册/旧epoch或旧sequence | Invalid/OldEpoch/OldSequence，无旧状态复活 |
| 合法初始delta、gap后的delta/heartbeat | NeedFull/Gap并撤销route；随后合法Full恢复 |
| 已核准gap envelope同时带非法payload | Invalid/Capacity，目录不部分发布，旧route已撤销且NeedsFull=true |
| 候选批次非法/目录超限 | Invalid/Capacity，无部分目录写入 |
| 明确Inactive/Ended或来源失效 | 不准入可执行route，不把通讯故障伪造ShowEnded |
| Full重复携带相同退役generation终态与健康候选 | 终态可观察，健康候选正常刷新；同代Active/Unknown不可复活 |
| B请求后C成为真实前台、旧B确认到达 | 拒绝旧确认，Pending仍针对最新意图 |
| 当前页事件在弱挑战期间到达 | 即时更新target，不延迟到weakWindow结束 |
| 旧route/target、过期deadline、不支持operation | RejectedStale/Unsupported |
| 命令已经提取后结果超时或断连 | OutcomeUnknown，不产生第二份翻页outbox |
| unknown page/不可证明跨源映射 | pageSafe=false，不继承已ready门禁 |

## 5. Good / Base / Bad Cases

Good：类库与helper的可靠show证据一致，聚合一个logical show；helper恢复稳定后换route而不结束场次。随后真实前台变成另一show，绕过弱提示冷却，等待新绑定确认。

Base：第三方只给会话内文稿与页序号；能力/optional表达真实缺失，只有该隔离会话可证明的页语义用于输出，未知状态保持不安全。

Bad：两个来源各自priority=3就累加；用路径或来源local generation合并show；收到页包就续活动TTL；前台改变时仍向后台A翻页；迟到B确认先发布再补救；管道超时后把同一次Next改投备用源。

## 6. Tests Required

实际测试入口为InkeysHeadlessTests/presentation_link_tests.cpp，由animation_tests.cpp现有main登记。fake adapter必须驱动上述公开服务/GuardCommand，测试本身不实现替代仲裁算法，不用sleep。

断言至少覆盖：非法批次原子性、full/delta/gap/epoch、弱身份不合并、同show多源、same-file不同进程/窗口、显式结束/Unknown过渡后Inactive退役/重复终态Full/缺口/重用、heartbeat/重复活动不计样本、短脉冲取消与持续挑战一次提交、同级保持、软反向/source恢复、真实foreground快路径/自有Focus、B→C迟到确认、A正常页快路径、same-show route/target修订、single route/deadline/guard/dedup/OutcomeUnknown/迟到ACK。

编译使用完整InkeysRepo.sln Debug ARM64，原生ARM64 MSBuild、同invocation PATH规范化与至少5分钟时限；运行本次构建InkeysHeadlessTests.exe --no-window。检查生产COM/ABI/随附DLL/TLB基线及工程最小diff。真实Office/损坏COM、Win7、跨权限与Draw3接管另列NOT VERIFIED。

## 7. Wrong vs Correct

Wrong：按候选providerActive或最后消息到达顺序直接换绑定，并在超时后重新发送Next。

Correct：adapter只上报独立观测的Activity.observationRevision与真实ShowIdentity；服务统一仲裁，经BindingRequest/Confirmation提交；用户动作捕获Selected().route修订，单一TakeCommands交给执行owner，并调用GuardCommand。OutcomeUnknown后刷新绝对状态，不重复非幂等操作。

执行端示例（currentRoute/currentCandidate/sourceInfo由owner持有）：

```cpp
const auto decision = GuardCommand(command, currentRoute, currentCandidate,
    sourceInfo, now, executionLedger);
if (decision == CommandStatus::Queued)
{
    // 只在guard准入后执行一次，并回报带原target的结果。
    // 后续adapter在这里沿原owner调用目标，再发送CommandResult。
}
```
