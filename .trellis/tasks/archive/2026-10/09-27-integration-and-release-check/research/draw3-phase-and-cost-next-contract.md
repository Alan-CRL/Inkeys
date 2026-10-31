# Draw3 下一单元：有限 phase、真实成本与事件接口合同

日期：2026-10-01。Active task：.trellis/tasks/09-27-integration-and-release-check。分工 READ_ONLY_U3_NEXT，仅写本新文件。完整合同 §8.1/8.2/8.3/9/9.1/9.2 与 U3-H GREEN_DESIGN 是依据；本次按实际 RuntimeMetrics/Controller P2/Host/Hidden/ContactInput/Laser 源码收束。没有修改源码、工程、其它报告、spec或账本，没有 Build/EXE/Git/GUI/Computer Use/递归派发。Root 的 Host late Subscribe标记与 dxgi.h修补保留；当前Host.cpp身份64BB790A…DF7EC28。既有U1/U2/B3审查不重做，U3-H新smoke动态结果由Root另记，不凭本合同判通过。

## 1. Root已批准的局部选择与实施顺序

本轮Root明确批准：pipelineCompositeComplete + 当前权威整页L2 + 真实dirty包含末项全部可见投影 + 此前16/216顺序final回执已真成功即可封期。现FreezeCandidates的fullComposite参数特指整viewport dirty；不硬要求末笔天然变全viewport，不为metrics追加FullPresent/重放。Cold是初始化clear→原全合成→真实成功返回的独立冷证明；不能从尚未进入Run的L2 stamp猜测。失败、取消、无可见投影保留原分母并incomplete；blank eraser与Laser不混成普通笔可见延迟。

最少下一写入单元 N1：一个明确writer独占 RuntimeMetrics.cppm/.cpp + DrawingController.cppm/.cpp，共同完成有限phase POD/离线报告、独立末项final receipt、owner回执callback和真实frame CPU/批次成本。不得拆成两个同时写Controller的worker。Root独占Main/项目/spec/账本/build-run。

N1接口冻结和独立检查后才交 N2：另一个writer独占 Host.h/.cpp + HiddenWindowTest.h/.cpp，接owner phase值、2160包source有限receipt、固定core runner/私有导出；消费N1冻结接口，不回写N1四源。先CPU合同probe与Debug真实Host，后Release每tool/backend三轮。首批core性能取得之前，N3 Move/Up与N4功能等价的职责和合同保持如下，不能当永久excluded。

## 2. 实际生产接缝与关键缺口

| 现有真实符号/位置（行号对应本次读取，以符号为准） | 可接的事实 |
| --- | --- |
| Controller::ClearCanvas 6446；PresentFrame 6403 | 全透明clear与原全canvas合成已在GPU owner执行；PresentFrame的returnQpc在真实Present紧邻返回、observer之前取得，external frame在observer之前Finish。 |
| Controller::Run 6554 | document_.emplace/blank page在Run才建立；启动ClearCanvas时没有Run document/history。Run 11234 WarmUpLaserShaders/WarmUpShapeShaders后才用initialL2Cleared/尺寸/empty history初始化Run权威stamp。Cold不得伪造此stamp或page ordinal。 |
| initializeStroke / ControllerContentMetrics::Register 1975 | 一次实际Down登记；RegisterContact把contactSeen赋给MetricContact.ordinal。返回bool不含ordinal；重复key不推进seen。可在同owner真实Register前后取Snapshot锁存新增ordinal，不加新ordinal框架/getter。 |
| consumeLatestSnapshot 8252 / completeModelUp 8160 | sequence/qpc在消费时真实复制；Up有80ms deferred/reconnect和终态fallback，consumed不等于Stored/GPU完成，Cancelled单列。 |
| CommitRuntimeStoredStrokeCpu 1741；Run NormalUp 12526 | 成功返回真实strokeIndex/RenderItemId/before/after，随后可取Find(id).contentGeneration/visible/afterStates。这个点早于DrawStoredStroke/ApplyOperatorLayers，也早于runtime回收。 |
| CaptureStored 2055 | source->landingConfirmed为true时直接清live note返回，不保留Stored landing note。因此confirmed16/pending0/occupied0并不能证明最后一笔Up的权威Stored成功。 |
| CompleteLocalWrite 2194 / CompleteVisibleReplay 2213 / CompleteFullReplay 2178 | 已有l2.valid+完整ContentMetricRasterSignature；失败局部链不能被后来的成功补成权威，partial replay须仍匹配原签名。复用此权威，不借trusted snapshot或hasContent。 |
| Run 12635 / 12677 | CPU rasterState即使GPU失败也会推进；CompleteLocalWrite结算真实submitted。随后active.erase_if清handle/快照，独立final receipt必须此前复制。 |
| Run 12917–12965 | trusted fallback与正常CompositeLayersToBackBuffer分开；叠加Laser/particle/cursor全部成功后FreezeCandidates，最后真实PresentFrame。实际dirty和完整pipeline结果在这里可冻结。 |
| PresentReturned 2405 | 先记实际attempt/result，再核frozenFrameSerial/output/composite/signature，最后清candidateCount/compositeReady。phase-final成功必须在清掉这些事实之前取同一returnQpc。 |
| Run 13039–13049 / MetricsLoopExit 11292 | physicalAfter与所有终态/帧原因在Finish前收敛；Finish记录本帧后、pacing/wait前是phase callback边界。RAII早退/异常也须结算成本，但不封成功phase。 |
| Host::PublishHiddenTestContact 1814 | WndProc owner真正QueryPerformanceCounter、构造snapshot、调用Coordinator；当前bool返回0/-1只证明Publish接受与否，不提供实际发布sequence。 |
| ContactRecordAccess::PublishSnapshot 265 | sequence是每record seqlock的偶数（写入加2），不是packet ordinal；PublishMove失败亦可能已写snapshot后因route改变返回false，不能以false推“从未写入”。 |

## 3. N1：独立final receipt与最薄owner回调

首批core一次只有一笔、下一Down等上一笔final回执。因此只需一个当前pending final POD及累计facts；不建立216项ancestry/通用事件registry。多笔/意外新Down在旧final未闭合时保留产品行为、标本fixture incomplete。普通多contact另有独立场景，不限制正式输入。

在真实NormalUp CPU commit成功后、现CaptureStored及active回收前，复制：
contactOrdinal、opaque key+generation（只供内部值匹配）、terminal sequence/qpc/admission、canvas anonymous identity、item id、strokeIndex、contentGeneration、afterState、原可见投影。Down已confirmed仍必须复制；Cancel/CPU commit失败/无可见投影分别记reason和原seen，不补一个成功final。terminalConsumed只在实际首次消费Up累计；cpuStoredCompleted只在NormalUp真实CPU commit成功累计；合成页边界/Stop的伪terminal不能当核心source Up。

每次真正绘制候选先复用当前Signature/Find/afterStates验证同一final item未隐藏或改内容，l2.valid且signature==当前CPUhistory/raster/viewport/尺寸；viewportVisibleClear真、refresh/recovery均否。冻结实际当前output存在位/raw tuple/非零匿名编号、frameSerial、dirty覆盖final整可见投影、完整pipeline composite成功。fullViewportComposite只是观察值。尽量共享现PruneStored的精确item谓词（必要时只提取该小谓词，旧U2测试保持），不复制第二套accept算法。

PresentFrame紧邻真实返回的同一returnQpc在PresentReturned末尾清frozen facts之前验证；called+success、当前frame/output/signature全部吻合才推进authoritativeFinalPresented与finalCompletedOrdinal，恰一次释放pending final。失败/trusted fallback/局部覆盖/旧output/错history只保留pending或按真实失效记incomplete；不抹实际Present分母、不注册第二个Down landing。后续恢复成功仍可闭合相同final，不用回收后的record读字段。

需要冻结到runtime_metrics.cppm的有限POD建议（以下均新提案，未实现）：

~~~cpp
enum class RuntimeMetricsPhase : uint8_t { Cold, Warmup, Measured };
enum class RuntimeMetricsBoundaryAuthority : uint8_t {
    None, StartupClearedSurface, StoredHistoryChain, LaserLifecycleComplete
};
struct RuntimeMetricsTerminalFacts {
    uint64_t upConsumed = 0, cpuStoredCompleted = 0;
    uint64_t authoritativeFinalPresented = 0;
    uint64_t cancelled = 0, rejected = 0, noVisibleProjection = 0;
    uint64_t excludedLaser = 0, laserLifecycleCompleted = 0;
    uint32_t activeRuntimes = 0, awaitingReconnect = 0, pendingFinal = 0;
};
struct RuntimeMetricsPhaseBoundary {
    bool exists = false;
    RuntimeMetricsBoundaryAuthority authority = RuntimeMetricsBoundaryAuthority::None;
    int64_t ownerQpc = 0;                  // 同一次真实成功Present的即时returnQpc
    uint64_t frameSerial = 0, contactSeenOrdinal = 0;
    RuntimeMetricsSnapshot metrics;
    ContactInputDiagnosticsSnapshot input;
    RuntimeMetricsTerminalFacts terminal;
    RuntimeMetricsCanvasIdentity canvas;   // Cold允许全0，authority明确是初始化表面
    uint64_t historyRevision = 0, rasterState = 0, rawOutputRevision = 0;
    float viewportX = 0, viewportY = 0, viewportScale = 1;
    uint32_t width = 0, height = 0, rawOutputTarget = 0;
    bool pipelineCompositeComplete = false, finalProjectionCovered = false;
    bool fullViewportComposite = false;
};
struct RuntimeMetricsPhaseBoundaries {
    bool enabled = false, incomplete = false;
    uint32_t failureFlags = 0;             // 固定枚举：取消/拒收/丢样/身份变化/无投影/未封期
    ContactInputDiagnosticsSnapshot runInputBaseline;
    RuntimeMetricsPhaseBoundary coldEnd, warmEnd, measuredEnd;
};
struct RuntimeMetricsPhaseProgress {
    uint64_t finalCompletedOrdinal = 0;    // 每笔实际final后发布，runner才能发下一Down
    RuntimeMetricsTerminalFacts terminal;
    RuntimeMetricsPhaseBoundaries boundaries;
};
~~~

rawOutputTarget只能由当前TransparentOutputTarget真实符号转换；在报告实现用符号映射，不猜枚举数字。DTO不含GPU、document、GUID、pointer、HWND、字符串或callback owner。opaque key只存在Controller内部pending POD；POD的actual sizeof/count/capacity纳入共同预算，static_assert trivially_copyable，host进度不是延迟源。

DrawingControllerRuntimeObserver尾追加一个默认null字段，避免多个重复phase/event getter：

~~~cpp
void (*runtimeMetricsPhaseProgress)(
    void* context, const RuntimeMetricsPhaseProgress&) noexcept = nullptr;
~~~

callback非空且metrics实际Prepare成功才启用有限core phase状态；正常defaultoff/null callback不新增phase状态分配/时钟。调用只发生在Cold成功和每笔真正final闭合/明确失败，以及第16/216边界封口；先Finish记录当前frame，再发布值。callback不得Stop/join/磁盘/GPU query/等待；Host只短锁复制POD+发布少量原子progress。cold external frame先保存“本次clear及全pipeline已完成”事实，真return成功、其frame Finish后发Cold；不能因initialL2Cleared标志事后变真就补Cold。

## 4. 三界必须满足的实际条件

- ColdEnd：首startup clear全canvas+全部原pipeline合成成功，第一次成功Present即时returnQpc/非零serial，contactSeen=0；明确StartupClearedSurface。不是空Snapshot推断，也不借尚未创建的Run CPUhistory。Run shader prewarm在此后执行，另列startup/pre-source wall；不得混成一张虚拟render frame。runner依现主输出ready握手等待Run已消费产品状态/工具及prewarm完成后才发第一Warm Down。
- 每笔推进：真实Down新ordinal，恰一次真实Up消费/NormalUp commit；所有对应final receipt沿权威L2/实际全pipeline和output成功闭合，active/awaitReconnect/pendingFinal为0才发布finalCompletedOrdinal。等待120ms及poll返回只是发包/静止预算，不是Present终点。
- WarmEnd：contactSeenOrdinal恰16，非Laser upConsumed/cpuStoredCompleted/authoritativeFinalPresented累计均16，顺序final1..16无缺口；无active/deferred/pendingFinal/未确认warm Down，drop/invalid/cancel/reject/无投影均保账且完整标志不得真。只在第16笔真正闭合的既有成功帧封，不能等空闲后的poll时间补QPC。Warm失败没有WarmEnd，禁止发Measured第17笔。
- MeasuredEnd：同理contactSeen恰216、非Laser各终态/final累计216；Measured本群是17..216共200，不是另外200个新Session。允许原GPU帧重试，禁止重发/替换Down凑200。phase sealed值是当时Session/input prefix；真正Host join后最终export再核本run源包/无新contact/旧pending未跨界。
- fixed source未被消费、模型拒收/Cancel、metadata/tool/source/viewport/scene/raw output不符，或迟到第217笔：保留allRun/raw及incomplete原因；不得Reset修齐。成功return但l2无效/trusted/旧签名、Down早确认但Up栅格失败是必须红的反例。
- Laser：正式landing/Up latency excluded；底层当前Desktop CPUhistory仍须权威L2，不能用trusted fallback替代，只有Laser本体不要求Stored项。仍216真实contact/Up和逐笔Active→Hold→Fade→Inactive/粒子结束。每笔等实际EndLaserContact及FinalizeLaserStrokeLayer、原Bake结果、layer/particle清理、expiry旧投影真正脏化/原pipeline成功Present输出匹配，才累计laserLifecycleCompleted。不能在Hold wait、opacity0或activeContactCount0时提前算完成。16/216按实际生命周期终帧封，不要求不存在的Stored L2项。取消/bake失败/残留资源仍incomplete，不能只让excluded掩盖它。
- blank eraser：core空Desktop上的擦除是“空白画布擦除操作提交”population；即使history/operation projection可证明，也只报软件operation-to-PresentReturn。changed-pixel/光学擦除延迟=null且未测。无operation投影不补零延迟；擦已有1000笔/真实清除效果属于下一场景。

## 5. N1离线报告：同Session216、有限phase分母

保留现两参API和schema2/allRun内容；新增已冻结形状：

~~~cpp
bool RuntimeMetricsSession::WriteJson(const wchar_t* outputPath,
    const ContactInputDiagnosticsSnapshot& input,
    const RuntimeMetricsPhaseBoundaries& phases) const;
~~~

新重载仍只joined/sealed之后排序/格式化/create-new，旧两参原样可用。以现Median/Percentile/WriteDistribution共用实现，新增phaseBoundaries/finite phaseSummaries和coverage；不建立标签registry。

必须把现impl.contacts保留项序列化为匿名contacts raw：ordinal、generation、device/tool/downQpc、status/retained，不写opaque指针；目前JSON只有successful landings及summary，没有未确认contact逐项raw。按Down ordinal1..16/17..216归landing/contact，晚确认仍归原Down群；phase contactSeen原分母不可按成功数量缩小。dropped未保留项至少由源包/phase prefix差明确留在原群，不生成假的contact row。

frames/presents按ColdEnd/WarmEnd/MeasuredEnd owner serial区间归群。Cold或Warm界缺失相应startup/warmTruncated/unclassified保留，Warm缺界禁止伪Measured；Warm已封而Measured缺界保留measuredTruncated raw与原计划200/incomplete。MeasuredEnd之后Stop/drain尾帧单列postMeasured，不混steady。旧frameIntervalsMs/activeFrameWallMs数组没有serial，不硬切成steady；phase interval未有带serial新记录时null/not collected。Frame raw可尾追加presentReturnQpc（仅实际返回存在时赋值），不拿host poll time或帧结束时间补它。

现Snapshot没有impl.presentDropped/presentInvalid/durationDropped：N1至少尾追加presentRetained/presentDropped/presentInvalid/durationDropped，使三界prefix可记录这些真实丢失；Snapshot implementation从实际impl抄值，不从差额估计。每群seen/retained/dropped/invalid、attempt/success/failure/noPresent/rasterFailed、terminal/final/excluded均分列。Laser当前allRun unpresented/coverage结果保持事实，phase层另给明确excluded denominator/lifecycle，不改成普通笔complete。

空值null；每项各自有效样本<1000时P99=null，三轮200 landing合计600仍不足1000；Laser frame/cost若各自>=1000可给该项P99，不能套contact分母。每轮原数据与失败轮保留，不选最好轮。frameWallGe50Ms用新>=50ms统计；旧16.67ms longFrame继续明确legacy。releaseVerdictAvailable仍false，软件return不叫光学/GPU时长。

当前Host::WriteRuntimeMetrics拒startupFailed/runFailed。N2的显式phase fixture需另一个窄导出方法（提案WriteHiddenTestRuntimeMetrics，见下节），允许已真join/sealed且Session准备成功的失败run导出这个生产三参重载并强制phases.incomplete；现普通wrapper不改语义。未join/prepare失败/强退没有可信Session尾，只有外部manifest标missing/incomplete，不能伪造空成功raw。

## 6. N2 Host/Hidden：固定core source与有限接线

HostStartOptions尾加enableHiddenTestRuntimeMetricsPhases=false，仅可信新fixture使用且要求metrics/injection真实开启。Host private Impl存N1 PhaseBoundaries/POD，observer只复制CPU值和原子cold/warm/measured/finalCompletedOrdinal进度；Host.h的RuntimeSnapshot尾只加这些少量数值，不import GPU/Session getter到传统头。

~~~cpp
struct HostHiddenTestSourceReceipt {
    bool observed = false, publishAttempted = false, accepted = false;
    uint32_t packetIndex = 0;               // 0..2159，plan contact ordinal另为1..216
    uint32_t contactPlanOrdinal = 0;
    HiddenTestContactPhase phase = HiddenTestContactPhase::Down;
    int16_t x = 0, y = 0;
    float pressure = 0;
    int64_t ownerSourceQpc = 0;
    bool sequenceKnown = false;            // N3 publisher receipt前固定false，JSON为null
    uint64_t inputSequence = 0;
};
// 串行生命周期owner，要求本run明确phase fixture、真join/sealed和prepared Session；失败run必须incomplete
bool Host::WriteHiddenTestRuntimeMetrics(const wchar_t* absoluteOutputPath) const noexcept;
// private source receipt只读值，不暴露opaque contact key；deadline并非source timestamp
bool Host::CopyHiddenTestSourceReceipt(uint32_t packetIndex,
    HostHiddenTestSourceReceipt& out) const noexcept;
~~~

Host private预分配恰2160个source row；row存packetIndex/contactPlanOrdinal/phase/coords/实际pressure/ownerSourceQpc/Publish结果存在位；writer只有原WndProc owner，在既有PublishHiddenTestContact QueryPerformanceCounter点复制，after Publish锁存结果。caller计划tick/sendQpc/timeout/rejected另用有限runner row按packetIndex对照，不能倒填owner timestamp。timeout可能稍后被owner执行，不重发Down；默认/普通hidden入口新gate=false，不写新row、不增加采样或改snapshot.qpc。

沿冻结core-linear-8：320×240/viewport(0,0,1)、Desktop/page0/Primary、external PenTip、Down压力0.8/其余0.7，Down(40,48)，j=1..8 Move(40+24j,48+12j)，Up(232,144)，每包计划8ms/每笔至少120ms静止，真实终态/必要final回执最迟2s；Laser等真实hold/fade/particles最迟4s。16+200×10=2160包（Down216/Move1728/Up216）。单次超时停止计划并标incomplete，不增笔补齐。非Laser总预算300s、Laser900s。所有工具按实际Bridge/DrawingTool/effective eraser width mode白名单；model/prediction/pacing/hold/particle/输入策略不降质。caption标合成mailbox源，不是实笔RTS测量。

runner在每笔finalCompletedOrdinal到达且120ms静止之后才发下一笔；第16后还须WarmEnd真，再第17。该progress新增是必要薄接线：只等confirmed/pen.active/occupied仍不能保证权威Stored。原Down落笔延迟继续用Session已绿producer时间，source receipt用于完整性；N3前逐Move/Up latency明确null。

复用原private路径/safety/四窗/样式/同Host Stop join/Service生命周期。DComp创建期NOREDIRECTIONBITMAP与fresh legacy ULW两个run，不能同绑定DComp HWND强改ULW。未知/缺/重复CLI参数在普通初始化前拒；root只在本任务ignore目录create-new，报告/manifest/source-packets固定文件名create-new，保留sentinel/失败轮，不接EXE邻目录/Office/Computer Use。Root Main/项目为唯一writer，新完整runner签名/CLI沿已审§8，本报告不授权运行。

## 7. N1真实成本与Laser：可得数值必须采，不做GPU等待

RuntimeMetricsFrameSample尾追加固定cost POD即可，不新recorder。建议最小字段：

~~~cpp
struct RuntimeMetricsFrameCosts {
    bool cpuAvailable = false;
    double threadCpuMs = 0;
    uint32_t stageAvailableMask = 0;
    double ingressWallMs = 0, modelPredictionWallMs = 0;
    double geometryRasterSubmitWallMs = 0, compositeWallMs = 0;
    uint32_t modelUpdateCalls = 0, predictionCalls = 0;
};
struct RuntimeMetricsLaserFrame {
    bool collected = false;
    uint32_t trailPhase = 0, coverageMode = 0, activeContactCount = 0;
    int64_t lastAllUpQpc = 0;
    double effectiveHoldSeconds = 0, fadeSeconds = 0;
    float opacity = 0;
    uint32_t layerCount = 0, particleRequestedCount = 0;
    bool particlesEnabled = false, particlesAvailable = false, particlesActive = false;
    uint32_t incrementalCalls = 0, bakeCalls = 0, bakeFailures = 0;
    uint32_t particleStepCalls = 0, particleDrawCalls = 0;
    double incrementalWallMs = 0, bakeWallMs = 0, particleStepWallMs = 0, particleDrawWallMs = 0;
};
// RuntimeMetricsFrameSample尾：costs、laser、int64_t presentReturnQpc=0
~~~

trailPhase/coverageMode值必须取现LaserTrailPhase/LaserCoverageMode符号并在writer按符号映射。collect=false/不支持项导出null，不捏造0；当前FrameReason枚举bit0..20已真实存在，保留它，NoPresent与实际Present结果仍互斥。

| 真实计时接缝 | 统计口径 |
| --- | --- |
| DrawingControllerMetricsState::Begin/Finish；MetricsLoopExit/MetricsRunExit | 只metrics实际Prepare成功才GetThreadTimes(GetCurrentThread)读取user+kernel累计FILETIME；Finish/每个早退在pacing/idle wait之前取delta/10000为ms，任一API失败/倒退/溢出→cpuAvailable=false/null。frame.wall包括Present与同步CPU工作，CPU是实际本线程值，两者不相减代GPU。 |
| drainIngressBatch / tryConsumeOneIngress / processCommandAndReconcile 8585；consumeLatestSnapshot 8252 | 有界batch wall覆盖真实消费/准入，若包含initialize/model工作则明确inclusive。不把queue wait/计划发包sleep计为工作；private packet/input published不是已消费。 |
| updateContactModel 7325及初始化/reconnect的直接Reset/Update；三处以上实际Predict 8456/12167/12205/12380 | wrap真实Update/Reset/Predict批次，按实调用累计wall+calls；Append/Extract按真实geometry部分计。不能只围最后一个Update而漏初始化/deferredUp/停笔/续接；未知未覆盖时stageAvailableMask不置位。 |
| Run active geometry/raster批次；CommitStablePrefixToL1/CommitEraserRealPointsToL1/RebuildL0DrawPoints/DrawStoredStroke/ApplyOperatorLayers/visible replay | 实际调用段累计，或以outer active批次包围并明确包括内部model/prediction子段。保留异常/失败/terminal帧，父子inclusive不可相加。命令/resize/recovery旁路未放入该batch时单列reason/other，不伪称完整分解。 |
| Run final CompositeLayersToBackBuffer / trusted fallback + Laser/particle/cursor叠加 12917–12965 | 一次完整最终composite batch wall；失败无Present仍记录。particleDraw子段包含在其中，注inclusive。 |
| PresentFrame 6403 | 现真实wall/result/即时returnQpc复用；不以FrameEnd或poll终点代替。 |
| wait/pacing尾 13062以后，idle/Hold wait 11858以后 | 不计render frame.wall/threadCpu；如需等待分析另存有限loop/wait计数+wall，不把纯idle/maintenance早continue造零render帧。 |

Laser真实软件来源：BeginLaserContact/EndLaserContact、EvaluateLaserTrailOpacity、EffectiveLaserHoldDurationSeconds、UpdateLaserStrokeLayerIncremental、BakeLaserStrokeLayers(bool)、ResolveLaserCompositedColor/DrawLaserStrokeLayers、StepLaserParticles/DrawLaserParticles，以及laserParticleDirtyTracker的hasActive/expiredAny/原bounds。Hold无render需固定loop/phase-transition计数，Fade/expiry/bake失败等真实render仍计帧/cost；每笔lastAllUp与实际hold/fade值保留。Warm/Measured的最后生命周期帧按§4确认。

particleRequestedCount只能取Controller actual emission requests，不叫GPU emitted。现Renderer::StepLaserParticles是void，LaserParticleSystem::Step会校验/钳count、Map失败Release，只有Dispatch后实际spawnCursor推进；因此真实emit/update提交数与资源create/failure/release目前不能由Controller反推。需要后续Root明确窄L资源writer（Renderer.cppm/RendererLaser.cpp + LaserParticles.cppm/LaserParticleSystem.cpp）加纯CPU的LastLaserParticleStepReceipt/资源累计快照，在既有Create/Map/Dispatch/Release处锁存attempt/submittedCount/failure，不GPU query、不新增等待/改变循环；getter只同GPU owner读取。这个未接项保留not collected，不能把整个Laser调查记完成。N1能立即采的phase/wall/calls/CPU/bake结果必须真实采齐，不能全写unavailable。

自进程private/workingSet/HANDLE/GDI/USER由N2在pre-warm/warm-end/measured-end查真实API，另标runner实际采样QPC与owner cut差；不冒称恰同owner时刻。known scratch=4×width×height仅估算，不叫完整显存。无GPU timestamp/disjoint/显存/cache hook继续null；60–120分钟资源/1000笔真实历史为独立后续场景。

## 8. N3 Move/Up：下一小unit的真正接口与职责

现metrics Live在Down confirmed后仍可Adopt，但Run ObserveLiveRaster和FreezeCandidates均跳过landingConfirmed，CaptureStored又不保留final。因此N3须独立事件gate让同一adopted geometry/raster/proof接缝继续工作；Down重复stage/确认逻辑仍保留原语义。不能从Down延迟、sequence差、全局成功Present或rawPublish bool派生Move/Up。

最少固定事件DTO/Session接口提案（仍由同一RuntimeMetrics+Controller writer串行实施）：

~~~cpp
enum class RuntimeMetricsInputEventKind : uint8_t { Move, UpFinal };
struct RuntimeMetricsInputEventKey {
    uint64_t contactOrdinal = 0, sequence = 0;
    RuntimeMetricsInputEventKind kind = RuntimeMetricsInputEventKind::Move;
};
bool RuntimeMetricsSession::RegisterInputEvent(
    const RuntimeMetricsInputEventKey&, int64_t sourceQpc) noexcept;
bool RuntimeMetricsSession::StageVerifiedInputEvent(
    const RuntimeMetricsInputEventKey&, const RuntimeMetricsLandingProof&) noexcept;
void RuntimeMetricsSession::CommitVerifiedInputEvents(
    bool succeeded, int64_t returnQpc, const RuntimeMetricsLandingProof&) noexcept;
~~~

event数组/去重/pending均构造预分配、共同32MiB；取消/覆盖/未采纳/未呈现保持source与coverage原分母。消费者只能为实际消费snapshot.sequence/qpc登记，与真实Adopt kind、非预测几何和本帧共享raster成功序号完全匹配才stage；成功proof同一即时returnQpc确认，失败Live必须新帧restage。多个已发布但未消费Move不伪造event latency；publish/consumed/adopted/confirmed各自count。

为精确publish/覆盖/争用分母，另由Root明确ContactInput.cppm/.cpp唯一writer添加默认null的同步out receipt，不改输入策略：

~~~cpp
struct ContactInputPublicationReceipt {
    ContactHandle opaqueHandle;         // 内部值键，不导出/解引用延长寿命
    bool snapshotWritten = false, sequenceKnown = false, accepted = false;
    uint64_t sequence = 0;
    int64_t sourceQpc = 0;
    ContactPhase phase = ContactPhase::Down;
    uint32_t outcome = 0;              // 固定真实publish拒绝原因
};
// 现PublishDown/Move/Up/Cancelled尾可追加 ContactInputPublicationReceipt* receipt=nullptr
~~~

必须在真实writer latch/Initialize/PublishSnapshot/Close处抄实际最终sequence与qpc，不在bool API返回后重读可能复用record。receipt=false但snapshotWritten=true的Move/Close竞态保持事实；seqlock sequence加2不等于一次精确coalesced，只有完整发布row与消费row按opaque+generation+actual sequence匹配后可统计未消费/覆盖。默认null不新增clock/heap/诊断写入。窗口producer写Host固定source receipt，只有drawing owner写Session；joined后离线把完整source row与event raw关联，导出剥离pointer，仅匿名ordinal。N1/N2阶段尚无此receipt，sourceSequence=null必须诚实标not collected。

Up使用§3独立final item/contentGeneration/terminal sourceQpc，与当前权威L2/actual output/合成覆盖和真实final returnQpc确认；额外120ms安静窗口内token/visibility/signature未再变才stable。保留firstFinalPresentQpc作为延迟终点，另列quietVerifiedQpc/quietDuration；不能以quiet/poll结束时间算Up→Present。不请求额外帧，利用已有loop/wake读取已采状态，host如在120ms未达提前Stop则stable incomplete。Laser这个分布仍excluded、生命周期另报。功能gate使N4 off-run也复用终态事实而不创建metrics Session；不得把timed source改为fixedQPC。

## 9. N4 functional Source/owner checkpoint/生产UInk

沿既有§8.3冻结接口：RequestHiddenTestCheckpoint / CopyHiddenTestCheckpoint / CopyHiddenTestPersistenceReceipt；observer hiddenTestCheckpoint尾默认null。四Shape/core单笔固定source replay另gate，timed benchmark gate=false、不做readback。checkpoint的cutoff应直接复用本报告§3final receipt+权威signature/pipeline/output事实，函数不从confirmed/hasContent另猜。

metrics-off功能run仅启用共用stamp/final/test-frame/cutoff，defaultoff两功能gate均false、仍无Session/指标clock。fixedSource十包只用于功能，仍真实WndProc→Coordinator→Controller，在owner锁存epoch+0/8/...72ms sourceQPC并按计划发，不瞬时灌future包；timed source保持真实ownerQPC。GPU still alive时CopyResource staging→Map按RowPitch复制307200自有BGRA→Unmap→CPU receipt；一次功能检查、无每帧GPU query。Shape full BGRA与真实Stored两点/width/style exact；其它工具允许差异必须有实际契约，否则not covered。

随后相同功能run/private sandbox实际Clear→ObserveDesktopAutoSave提交前锁存snapshot.fileGuid→PumpDesktopCompletions同Save/Committed receipt，completion仍交产品；Stop真drain后fresh生产DesktopAutoSaveService::SubmitLoad→Loaded与ReadUInkFile Complete/逐项identity/content比较。孤儿/文件存在/hash差异不够，freshLoad不接Office。off/on不同sandbox、各自随机GUID匿名配对，不要求跨run GUID相同。N4 Host/Hidden writer与N2串行，Controller功能接口由N1/N3同一个writer依Root交接；不并发改同源。

## 10. 最小真实测试入口与门

建议N1新 int RunDraw3PhaseAndCostProductionProbe() noexcept 放现Controller.cppm/.cpp，Root从既有--draw3-parked-desktop-exit-test注册调用，不新工程/生产普通入口。probe调用Run共用的新final/phase helper、实际Coordinator/CPUhistory/现L2/output helper；固定success/failure只验证数值合同，不能冒称真实GPU。增量测试，不重写已绿U1/U2算法：

1. Down已经confirmed；真实NormalUp CPU commit成功但submitted=false / trusted fallback真Present，WarmEnd必须不存在；当前完整L2恢复+同final投影/输出真成功后才计final与封口。
2. exact item/contentGeneration/afterState、viewport/raster/raw output/scene错；partial region、Cancel、无投影、early stop、64slot/retention损失各保seen/incomplete，不凑16/200。
3. 同一真实Session known16+200：contacts/landings原ordinal216、warm16/measured200、frame/present serial按三界、late landing归原群、failure/drop不消失、P99门/legacy50ms区分。Cold独立clear证明；Warm未封第17禁止；Laser走生命周期证明不伪Stored。
4. cpu delta和stage合法/失败/倒退/早退/异常/wait剔除；测试实际共用采样/单位转换/有效位，fixture输入只证明转换合同，真正CPU producer由hidden raw核。
5. N2实际ProductHost→value mailbox→真实Controller→actual Presenter→owner callback→真Stop→生产三参WriteJson，Debug core成功与incomplete/raw-negative；只独立self PID/privateRoot，没有expected success stub。原U3H smoke保留，不能替fullcore。
6. N3真实publisher out receipt→消费/去抖/失败Adopt/真实proof，stale generation/Down已confirmed后Move继续、Up final失败恢复/quiet提前结束。N4 owner readback失败/错request/早Stop、四Shape全bytes、生产Committed→freshLoaded各实际接口验证。

顺序：N1声明/预算proposal Root批准→N1四源write→独立代码检查/CPU红绿→N2四源对准确接口实施与early CLI安全检查→Root完整InkeysRepo.sln Debug|ARM64（本机原生MSBuild、同PowerShell PATH workaround、>=5min）/strictHeadless/parked/PptCOM/真实core→N3/N4增量与独立复核→最后同Release/device/backend/effects/source三轮。性能时停build/scan/其它benchmark。Laser窄资源receipt、复杂轨迹/点慢快折返/Cancel换工具/多contact/压感同型Touch/擦已有1000笔/长期资源/Win7/光学/GPU/HC-H2可比性仍明确后续，core不掩盖这些门。

## 11. 预算、身份与未验证

Session、Controller当前45296B状态以及N1 phase/final/cost增长、N2 source2160行、runner已拥有的raw数组、N3 event预分配与N4功能307200像素分别列actual sizeof/capacity。在构造前算全部planned auxiliary，再核Session allocatedBytes<=32MiB-auxiliary，Controller原共同FitsBudget保留；若新增Controller状态超原64KiB静态上限，Root须明确同步Host保守allowance，不能静默沿旧预算。所有准备/容量失败产品继续、fixture incomplete，默认null/false不新增热分配/clock/I/O。没有先越预算再回收的“成功”证明。不要把process外manifest大字符串计成GPU成本，也不要漏进程内source/event payload。

本轮只读快照：

| 源 | SHA-256 |
| --- | --- |
| RuntimeMetrics.cppm | 1E935AC95020E0B01987D02BC95D762ABD17850832DDDBD63A41FD9004F7D37F |
| RuntimeMetrics.cpp | C4A0DB015CF6F66EA1BF7DBBE904A8AA902E1A5844CA62B288184EA761456932 |
| DrawingController.cppm | B119EE5B50F9649DD2C29A69F4045F4EE4C26CA39087893E3EB7AF7BC70670FA |
| DrawingController.cpp | 0CEF8C50C6D8C71BBF2DA1281F26337757B4C2729B78307A825C742521661876 |
| Host.cpp（Root late Subscribe/dxgi修） | 64BB790A407C25578B5BB90C2F21B5B1C1DD7AD42DC0C7059B9871129DF7EC28 |
| Host.h | 88F13F2A85E83CF71BC9AFC4F618B99BE26B0D1B1A795D1147398693163064A1 |
| HiddenWindowTest.cpp | F930AFB0A867CECCBD67D5D487EB0E0E7B2E9F7F2C4976E751430AFEC4702200 |

上述是本次阅读时点身份，不是最终HF/产物/benchmark身份。本报告的DTO/新callback/phase三参writer/全core/source receipt/event/CPU字段/功能接口均未实现；没有新build、runtime PASS或发布结论。只支持Root决定下一最小writer与先后依赖；没有源码WRITE授权。
