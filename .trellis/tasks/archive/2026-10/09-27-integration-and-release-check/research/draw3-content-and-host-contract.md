# Research: E04 Draw3 U2 内容归属与 U3 Host / hidden HWND 合同

- Query: 冻结真实 Controller 内容 producer、失败后的 Stored 纯值保留、Host opt-in 生命周期和现有 hidden HWND 固定轨迹 runner 的最小实施合同。
- Scope: mixed；以内仓生产源码、任务和 spec 为主，补查四个 Microsoft Win32 API 文档。
- Date: 2026-09-30
- Active task: .trellis/tasks/09-27-integration-and-release-check。
- 状态: 设计交付 / 尚待 root 独立 review 和实施；本报告不计 PASS。未改源码/工程/spec/账本，未执行 Git、构建、EXE、GUI 或 benchmark。
- 读取快照: U1已按独立review冻结绿色；本报告重新读取了实际接口/实现，未重跑U1。源码行号对应各次只读位置，以符号为准；不重新开放已冻U1。
- 本次收敛: 已完整读取draw3-content-and-host-design-review.md的R1–R5并重新核真实代码。以下为修订合同，等待独立复审；不修改该review或C-P1/B1/C-P2。

## Findings

### 1. 最小结论

复用现有 RuntimeMetricsSession、Controller::Run / PresentFrame、Host、ProductHost 和 HiddenWindowTest。U2 只增加 opt-in 的固定数值表、已采纳 sequence 和权威 L2 stamp，在真实合成候选处冻结多 contact proof。PresentFrame 在真实 presentation_.Present 返回后立即捕获 QPC，成功后按同一 frameSerial / returnQpc 逐 proof 调用 CommitVerifiedLandings；RecordVerifiedPresent 每次实际调用只记一次。

U1 的 record + generation 可以继续作为不解引用的 opaque 值键。没有证据要求强制新增 ordinal Stage 或另一个 RegisterActualDown 公共 API。Session 与 Controller 的旧键不享有 ContactRecord 生存期：不得通过该键读 DownSnapshot / Generation / ContactId、调用 TryReadSnapshot / Discard / Recycle，或把旧 runtime 指针留到下一帧。Stored 保留的只能是数值键和自有 proof。Host 必须在所有 producer 静止且绘制线程 join 后 Reset，并为新 run 创建新 Session，禁止采样中 Reset。

U2身份终结所需的Session小扩展是精确单contact失效：现在只有InvalidatePending()，无法取消A同时保留B的失败Stored pending；不匹配的成功proof也不得失效B。未补时取消会占住pending到全局封口，64槽可溢出，须列覆盖缺口。U3有限phase报告与Laser数值扩展另外在§8/9冻结，仍由原Session唯一writer获root批准后实施。

Laser 的 layerId / bounds / lifecycle 当前不形成可靠逐 contact 栅格版本。第一版明确排除 Laser 的正式 landing / Up 稳定分布，保留其 frame / attempt / 成功失败与生命周期计数和 excluded 分母；不得借 currentContentRevision_、同页或普通 history proof 确认 Laser。

### 2. Files found 与已确认的代码模式

以下路径均相对仓库根；Draw3 下的短文件名指 Inkeys/Inkeys/Drawing/Draw3/。

| 文件 / 符号 | 位置与事实 |
| --- | --- |
| Draw3.RuntimeMetrics.cppm / .cpp | 现有 Session 和 U1 新 proof / frame / snapshot；本题复用，不另起 recorder。 |
| Draw3.DrawingController.cppm | :79 的构造函数已有可选 RuntimeMetricsSession*；:136 附近只有借用指针，没有 Session 所有权。 |
| Draw3.DrawingController.cpp | :387 CanvasPageRuntimeState 含 history / rasterState；:1539 RuntimeStroke 含 owner GUID、handle、输入快照、lastConsumedSequence；:1741 CommitRuntimeStoredStrokeCpu 是正常 Up / fatal 共用 CPU 封口。 |
| Draw3.DrawingController.cpp | :5838 initializeStroke 是真实已出队 Down 初始化入口；当前不存在 RegisterActualDown 符号。:6256 断触接受时回收旧 handle，再接管新的 handle / Down；这是新物理 contact。 |
| Draw3.DrawingController.cpp | :6711 consumeLatestSnapshot 先推进 lastConsumedSequence / lastInputSnapshot；:6759 去抖 return，:6817 模型更新成功与 :6838 模型失败各有分支；lastModelSnapshot 也会在失败后更新。因此两者均不能直接充当 adopted sequence。 |
| Draw3.DrawingController.cpp | :10500 Shape 非空 bounds 即设 metricVisible；:10754 普通笔的几何/dirty 非空即设 metricVisible。实际共享 L0 raster 在 :11030 后，故该 bool 不是完整栅格证明。 |
| Draw3.DrawingController.cpp | :10831 CPU commit 返回 RenderItemId / beforeState / afterState；:10864 DrawStoredStroke，:10925 ApplyOperatorLayers 到 L2；:10934 即使 submitted=false 仍更新 rasterState，:10943 标记不清晰并安排 replay。 |
| Draw3.DrawingController.cpp | :10967 结束项在 Present 前移出 active，清 handle/metric 字段并交 input 回收；纯值 Stored note 必须在此之前复制。 |
| Draw3.DrawingController.cpp | :4970 PresentFrame 调用真实 Presenter 后更新耗时、调用 observer；:11281 才在帧末读旧 landing QPC。U2 终点应移到前者紧邻返回。 |
| Draw3.DrawingController.cpp | :5156 publishCurrentPageContent 只有 hasContent 布尔变化才推进 revision；它服务 selection handshake，不能逐笔/逐 Move 标识。 |
| Draw3.InkHistory.cppm / .cpp | .cppm:74 RenderItemId(index,generation)，:82 RenderItemState(visible,strokeIndex,contentGeneration)；.cpp:902 append、:949 Undo、:974 Redo、:1000 geometry 更新，:1042 Find 精确核 id。visibility / geometry 更新推进 contentGeneration。 |
| Draw3.InkHistoryGpu.cppm | :24 HistoryCanvasIdentity；:37 InkHistoryRasterKey 含 pipelineGeneration；:51 InkRasterStateToken；:121 CompositionRestoreRequest / result。 |
| Draw3.Host.h / .cpp | .h:213 HostStartOptions 当前无 metrics 开关；.cpp:138 独立 GraphicsDeviceResources / Renderer / Presenter；:1166 构造 Controller 未传 Session；:1363 正常 Stop 真 join，之后 detach HWND。 |
| Draw3.GraphicsInitialization.cppm | :18 GraphicsDeviceResources 已有真实 driverType / featureLevel / adapter，无需按 CPU 架构推测。 |
| Draw3.TransparentPresentation.cppm / .cpp | .cppm:109 RequestedOutputTarget / Revision；.cpp:820 目标变化推进 output revision；:925 Present 选择主 presenter 或 selection ULW，:951 连失败调用也回填 observation。只在 bool success 时认可该 observation。 |
| Draw3.HiddenWindowTest.cpp / .h | .cpp:83 WaitUntil 每 10ms poll；:94 MakeHiddenSpec；:846 RunMode；:2491 RunHiddenWindowIntegrationTest 创建现有四窗并 StopProduct → StopAndJoin。 |
| Draw3.Host.cpp | :1490 PublishHiddenTestContact，owner 收到消息后读 QPC、发布真实 Down/Move/Up/Cancel 到 ContactInput，并发布 control wake；:1612 ForwardMessage 复用此入口。 |
| Draw3.Product.h / .cpp | 已有唯一 ProductHost、StartProduct / StopProduct、ForwardProductMessage 和产品 state / command 发布入口。 |
| Inkeys/IdtMain.cpp | :738 等 CPU 子段 benchmark、:750 / :753 eraser-hidden / hidden CLI 已存在；新完整呈现 benchmark CLI 当前不存在。 |
| Inkeys/Inkeys.vcxproj / .filters | vcxproj:1068 / :1084 已编译 RuntimeMetrics / HiddenWindowTest；复用这些文件不需要新增第三个测试工程。 |

### 3. U1 接口接线与最小扩展

以下已有签名保持：

~~~cpp
bool RegisterContact(ContactRecord*, uint64_t generation,
    InputDeviceType, uint32_t tool, int64_t downQpc) noexcept;
bool StageVerifiedLanding(ContactRecord*, uint64_t generation,
    const RuntimeMetricsLandingProof&) noexcept;
void CommitVerifiedLandings(bool succeeded, int64_t presentReturnQpc,
    const RuntimeMetricsLandingProof&) noexcept;
void RecordVerifiedPresent(double presentWallMs, bool succeeded) noexcept;
void RecordRenderFrame(const RuntimeMetricsFrameSample&) noexcept;
void BeginFrame() noexcept;
void InvalidatePending() noexcept;
RuntimeMetricsSnapshot Snapshot() const noexcept;
~~~

worker 确认 PATCH_READY：Stage 只查已注册 opaque 键；proof 当前 frameSerial、五项 canvas identity、contentToken、consumedSequence 非零，Stored itemToken 非零。Commit 成功要求本帧已有 RecordVerifiedPresent(true)，按 canvas + kind + contentToken + itemToken + consumedSequence 精确匹配。Stored 跨失败帧保留，Live 必须本帧重新 stage。一次真实成功可逐项调用 Commit，不匹配不清其它 pending，已确认项只计一次。

提案，尚不存在，仅按以下实际缺口扩展 U1；不强制 ordinal / getter / RegisterActualDown：

~~~cpp
bool InvalidateContact(ContactRecord* opaqueRecord,
    uint64_t generation) noexcept;
~~~

返回true仅表示该Registered/Pending项此次转为Unpresented：unpresented加1，原为Pending才释放其唯一槽/pending减1；false表示未登记、已confirmed/invalid/legacy/unpresented或旧代次，所有计数保持。保留contact/index去重键，不删除记录，不改currentPresentSucceeded、frameSerial、其它pending或本帧成功资格。只按opaque键查找，不解引用。U2固定reason counters在true时记Cancelled / InitRejected / ReconnectSuperseded / ContentSuperseded等原因；重复失效不放大分母。旧宽接口保留给U1 probe，正常Run的旧StageLanding / CommitStagedLandings全部迁到verified入口。

R2成功重连接管点已核在initializeStroke当前:6289 Recycle旧handle→:6290接新handle之前：先用旧值键调用精确InvalidateContact，释放旧未确认Live，记reconnectSuperseded/unpresented，再沿原回收和接管流程登记B新Down。A已confirmed时false/no-op，不新增未呈现；同帧Stored C不受影响。不把合并几何/新B序号反填A，也不建立ancestry registry。modeler续接失败未接管时不失效A；新B按原后续初始化结果处理。验收须有A失败deferredUp→B真接管、C仍pending、A已confirmed反例、重复和同址新代次，以及>64次交接不因遗漏释放制造overflow。

### 4. 真实 producer 与新增 token

新增小状态放Controller .cpp的opt-in私有旁挂对象；在Controller构造/Run前一次准备固定容量，无热vector扩容/字符串/日志/文件/GPU query等待。活跃proof与Stored note各最多64项；表满计ProducerProofOverflow/unpresented，不改contact pool/输入准入，不在每帧重注册未保留键。先计算Controller表、output映射、有限phase/checkpoint/replay POD与预分配像素缓冲的plannedAuxBytes；构造Session后核Snapshot().allocatedBytes <= 32MiB-plannedAuxBytes，满足才分配旁挂，失败关闭该诊断并标unavailable。不能先超出共同32MiB再回收，也不能只报Session预算而漏旁挂；benchmark不存在像素缓冲，功能checkpoint限定320×240×4并纳入共同payload。

| 值 | 唯一 writer / 更新时机 | 不能替代它的值 |
| --- | --- | --- |
| runSerial | Host 每个新的 opt-in Start；Session 不跨 Reset 复用 | PID / record 地址 / 旧 run 的 frame serial |
| contactKey、source Down、admissionRevision | initializeStroke 在实际 handle/Down 存活时按值取一次；在模型/池初始化拒收前 Register，失败按精确键失效；重连新 Down单独登记 | 当前全局 AdmissionRevision，旧 runtime 的 Down QPC |
| workspace / page 匿名 ordinal | Controller 比较当前 document.WorkspaceGuid、PageGuid、kDefaultDeviceKey；ordinal 映射只在本 run，不导出 GUID | pageIndex、Bridge workspace enum、PPT页数 |
| metricSceneGeneration | 新页/slot、replace/load/Clear 的成功 CPU 事务处推进；保留原 GUID 对应的值，返回旧页也是新 scene；溢出停止诊断而不绕回旧键 | currentContentRevision_、rasterPipelineGeneration |
| adoptedSequence / adoptedQpc | Down/Move/model Update 成功且真实结果 Append/Extract 后推进；raw terminal fallback 单独标 adoptionKind；最后只消费/去抖/拒绝的 seq 不推进 | lastConsumedSequence、lastModelSnapshot、modelInputThisFrame |
| liveGeometryToken / rasteredSequence | 对该 contact 实际 real 点 / Down fallback / Shape 几何进入本帧；共享 L0/L1 draw 全成功后锁存已采纳序号 | 非空 dirty、metricVisible、预测点数 |
| Stored item | CPU commit 返回的 renderItem 与 strokeIndex；复制 Find(id).contentGeneration、afterStates[id.index]、terminal sequence/QPC，再回收 runtime | strokeCompleted observer、inputRecycled |
| authoritativeL2Stamp | 完成同 CPU history 的 L2 resolve / replay 后才形成；包含 scene、history.Revision、rasterState、pipelineGeneration、viewport x/y、当前 width/height、有效标志 | 单独 rasterState，trusted snapshot 或模糊 fallback |
| rasterGeneration | 当前 rasterPipelineGeneration；resize/device recovery/page/clear 的已有 GPU 失效边界核实后复制 | 独立的硬件 device epoch |
| outputGeneration / target | 绘制owner对真实RequestedOutputTarget/rawRevision建立checked非零匿名编号，候选取此编号与raw值/存在位；当前success observation须完全匹配 | 把合法raw0当unknown，rawRevision+1或Host异步快照 |
| frameSerial / contentToken | BeginFrame serial；contentToken 是旁挂对象分配的几何/Stored内容身份，不是“GPU完成”标志 | Present计数，任意同页 revision |

Live 第一版严格要求 rasteredSequence == adoptedSequence == runtime.lastConsumedSequence，已采纳 admission 与 Down一致，有实际非预测几何在 viewport 内且落在本次 dirty。忽略抖动/模型失败的 latest sequence 不倒填成“已呈现”，另计 ConsumedNotAdopted。raw terminal fallback 可以成为 Stored 的真实 CPU几何，但不能声称 modeler Update 成功。

itemToken 用 RenderItemId 两个 uint32 按无损方式组合为 uint64；同页 history 重建会复用 index/generation，必须同时匹配 scene generation。Stored contentToken 标识该 item 的固定内容；U2 note另持 contentGeneration、strokeIndex、afterState 以核权威当前页。不能把 hash 碰撞当身份证明。

R3 raw output身份与metrics编码分开：Presenter::Initialize当前:793–794明确Primary/rawRevision=0，SetOutputTarget同目标不推进，0是合法值。旁挂仅持当前POD {exists, target, rawRevision, metricOutputGeneration}及checked下一编号；第一次合法getter取值exists=true并编号1，每次真实tuple改变分配下一非零编号，相同tuple保持编号。不存在时exists=false/proof不产生；不把0钳成1，不使用rawRevision+1，不绕回已用编号。编号用尽记identityExhausted并停诊断，产品Presenter不改。恢复/重初始化事件先撤销当前映射；再次观察即使raw tuple相同也分配新编号。候选与返回须核exists、raw target/revision和当前匿名编号三者。回归真实Primary/raw0→Selection/raw1→Primary/raw2三身份、重复raw0、模拟极大raw不做加法、耗尽拒绝，以及旧Stored在新输出重stage成功恰一次。

authoritativeL2Stamp 在以下成功处推进/重新建立：正常 Stored 的 DrawStoredStroke + L2 ApplyOperatorLayers；完整 restorePageContent 成功；visible Tile replay 全部完成且 viewportVisibleClear=true、viewportRefreshPending=false；Undo/Redo 已成功完成候选 raster 和 CPU visibility提交。已有 GPU失败/清层/部分重放开始立即撤销 stamp。保守第一版可要求 !viewportRecoveryPending，排除仅 offscreen cache维护未结束的帧，并计 withheld；不能为采样强制更多 replay / full present。

局部 Stored / Undo / Redo 写成功只能沿“写前stamp有效且精确对应before history/raster/viewport”的连续链推进；先前某项失败导致stamp无效时，后来另一笔成功不能把整页恢复成权威。必须等同一CPU history的完整可见重放成功。这与现有 CanvasVisibleClarityAfterAuthoritativeWrite(viewportVisibleClear, true) 保留旧不清晰状态的语义一致。

跨帧tile replay另锁存计划开始的scene/history revision/viewport/尺寸/raster signature；每个必要可见tile成功以及尾部当前signature仍一致才建立stamp。期间CPU内容变化且不能证明整组tile覆盖新history时withheld，不能在末尾复制“最新revision”认证旧plan；不为metrics额外请求重放或改变原budget。

Stored note跨失败帧保留原 item/content/terminal 键。下一帧先从当前 document/page 取 canvas/history，Find(exact id) 必须 visible、strokeIndex和contentGeneration未变、afterState仍属该项，且当前 L2 stamp精确对应当前history/raster/viewport/尺寸。intervening append不要求整页 revision仍等于旧值：同项仍匹配且新权威 L2 stamp覆盖当前 history即可 restage；若未能证明则 pending / withheld。Undo后重新Redo导致该item contentGeneration变化，旧 landing不得复活。

成功 Present 只能证明此次真实栅格提交/合成包含该项的操作内容；不声称读取了最终非零像素、GPU执行时长或屏幕光子。完全视口外几何不记零毫秒成功。Up final要求该项当前可见投影被成功候选完整覆盖；Down first可以是已知真实贡献区域进入本次dirty。失败触发的原 full-present / replay策略原样执行。

### 5. 多 contact 候选与 PresentFrame 顺序

1. BeginFrame后处理实际 ingress/model/geometry；结束项在回收前复制 Stored note，取消项精确失效。不能在 observer后从最新 pen snapshot猜回帧。
2. 所有L0/L1/L2、Laser旁路和整体composite完成后，把合格Live + Stored proof复制到本帧固定数组，并对已注册精确键StageVerifiedLanding（Stage失败项不加入确认数组，保留实际invalid/overflow分母）。canvas五字段取当前权威值、frameSerial取当前Session值；不得一个全页proof确认全部笔。Stage在真实Present之前完成，失败Present的Stored已是纯值pending。
3. 候选数组冻结后，所有业务调用顺序保留。PresentFrame调用真实 presentation_.Present；其返回后第一个 metrics操作是 QPC采集，先于观察回调/ready/diagnostic/帧末。
4. 每次真实调用无条件RecordVerifiedPresent一次，记真实返回bool及wall。随后仅success且output target/rawRevision/匿名编号与冻结值匹配、候选确实完整合成时逐已stage proof Commit(true,sameReturnQpc,proof)。失败Commit(false)保留已stage的Stored；Live下帧重新验证。output不匹配只记outputMismatch/withheld并拒landing，不抹attempt/result，不否认真实success或failure。
5. 只有未调用Presenter时不RecordVerifiedPresent；raster/composite失败且没有调用为noPresent/rasterFailed，仍RecordRenderFrame，不合成一次API false。已调用后再发现proof不完整/错identity仍保留真实Present分母。四例no-call/call-false/call-true-match/call-true-mismatch须在Run共用assembler分别验。
6. PresentFrame的Host startup ClearCanvas / PresentFullCanvas也是实际尝试，开有效BeginFrame serial后空候选记录冷帧；没有Run帧上下文的外部Present新开边界，已有Run上下文不重复BeginFrame。frameSerial=0、沿用旧候选均拒绝；该serial是诊断帧/loop边界，不冒称GPU ID。

确认后及时清本帧数组和已确认Stored note。用唯一contentToken确保一次逐proof Commit至多匹配一个contact；各调用前后读取已有Snapshot().confirmed，增加恰1才置该项landingConfirmed并释放note。Live已确认的contact结束时不再创建Stored landing note。这样复用现有API，不能把已经确认的note长期占在64槽，也无需为此新增Commit返回值或复制Session正确算法。

### 6. 失败保持 / 失效矩阵与真实入口

| 边界 | U2行为 / 必须核的入口 |
| --- | --- |
| 首次Up已回收，Present失败 | note仍纯值pending；PresentFrame失败后原RequestFullPresent下一帧；同item当前L2成功恰一次确认。 |
| Stored raster失败，无Present | :10943的原viewport replay恢复，CPU history保留；不能把afterState推进当成功，失败帧计noPresent/rasterFailed。 |
| 另一笔追加后恢复 | 旧项visible/contentGeneration不变＋更新后的当前authoritative stamp允许确认；另一笔的Live proof不能替旧项。 |
| Cancel / 三初始化拒收 / reconnect | consumeLatestSnapshot / completeModelUp / RejectStrokeInitialization及成功reconnect接管前按精确key失效；不改E03 Discard/物理回收、不伪造Cancel；其它Stored保留，confirmed旧项幂等。 |
| Clear / cross-Clear恢复 | clearCurrentPage(:8802)成功后推进scene并失效旧pending；ReplacePage/恢复安装新scene，旧identity不复活；空Clear no-op不失效。 |
| Undo / Redo | 成功visibility提交后Find同item visible/contentGeneration反例；至少旧item隐藏即失效。undo未提交失败不伪造失效成功；Redo不能替旧Down补迟到landing。 |
| 页面 / workspace / PPT slot / load安装 | :8093 restoreAfterDocumentSlotSwitch与 :9575页切换实际CPU事务推进scene，失效旧scene；绝不按同pageIndex确认。 |
| Selection输出切换 / raw0 | 撤销旧匿名output proof；raw0合法且exists=true；旧Stored可保留，必须用新非零编号和当前raw tuple重新stage再真正成功，已confirmed不重计。错proof不抹实际attempt/result。 |
| DPI / viewport / resize | 真实尺寸、viewport、DisplayScale变化撤销L2/live stamp；当前rasterGeneration重建成功才restageStored。Host当前没有可靠逐帧dpi epoch，不能标完整DPI场景通过。 |
| device-lost / backend切换 | 清旧stamp，重建设备和真实history重放后再证明同item；rasterPipelineGeneration不冒充独立device epoch。Laser排除；帧报告真实新backend。 |
| fatal / stop / exception | captureGraphicsFatalExit的CPU封口只关保存，不给成功Present；未呈现项显式unpresented。真正join后全局封口。 |
| identity/token/表容量耗尽 | invalid / overflow / unpresented计账，诊断可以停止保留，产品采样与绘制不变。 |

拟增加的无 HWND probe（尚不存在）：int RunDraw3ContentProofProductionProbe() noexcept，放现有Controller .cpp，沿既有RunParkedDesktopExitAutoSaveTest / --draw3-parked-desktop-exit-test调用。probe必须调用Run共用的stamp / candidate helper、真实Coordinator注册回收、真实InkCanvas / CanvasRuntimeHistory append/find/Undo等；不能在测试复制另一套accept算法。CPU固定成功/失败值只证明匹配合同。

隐藏正例沿真实StartProduct→mailbox→modeler→renderer→PresentFrame→Session。off/on等价另用§8.3的固定source功能replay与Renderer仍活的owner checkpoint；Shape才首批exact BGRA，其它modeler工具不能因轨迹坐标相同就逐位像素PASS。真实API失败 / 同帧Down+Up / DPI / recovery若需hook，须另冻调用点/寿命/放行；false stub不算真Presenter故障。现RunRendererFailureCommitTest / RunLaserRasterFailureTest仍只覆盖其WARP子路径。

### 7. U3 Host 所有权和封口

提案，当前均不存在；附加到Host.h已有结构尾部，保持现有aggregate初始化：

~~~cpp
// HostStartOptions新增字段
bool enableRuntimeMetrics = false;
size_t runtimeMetricsMaximumSamples = 32768;
// Host新增方法；只由串行Start/Stop生命周期owner调用
bool WriteRuntimeMetrics(const wchar_t* absoluteOutputPath) const noexcept;
~~~

Host::Impl增加unique_ptr<RuntimeMetricsSession>；必须在Controller unique_ptr构造和Run前完成Session全部预分配。指标准备失败仅标metricsUnavailable，继续原Host启动；fixture把这次run记不完整，不报漂亮空数据。Controller借用Session的寿命覆盖startup帧、Run、异常收敛和析构。正常defaultoff不创建Session/旁挂表、不读额外QPC/ThreadTimes、不增加输入计数或样本；enable diagnostics用原hidden开关 OR 明确metrics开关，不能覆盖既有hidden诊断开关。

Meta一次按值抄真实graphics.driverType、featureLevel、adapter描述、presentation.ActiveMode、requested target、QPC频率；恢复时在绘制owner更新数值backend/raster事件。graphics在绘制线程退出清空前保存final tuple，runner不持有device/context/adapter。正常Start / product设备独立，不接UI RenderPipeline。

Stop保持原停止command→stylus Shutdown→退出保存屏障→worker drain→request_exit→drawingThread.join→detach顺序，不把metrics写盘插入其中。只有join完成、drawing对象销毁、owner不再写Session，才InvalidatePending/设置sealed并允许离线WriteJson。Running=false / request_stop / timeout / observer看到inactive都不是封口证据。Start失败的两条join路径也必须标startupFailed封口；没有join完成不能导出或复用Session。再次Start前上一run已join和报告处理结束，创建新Session，不将同址generation=1混到旧run。

WriteRuntimeMetrics调用现有Session WriteJson，输入diagnostics取joined run最后快照/本轮baseline差；不在活跃run读取非原子Session Snapshot。若runner需要进度，Host observer在绘制owner采样后发布少量atomic confirmed/seen/overflow计数（追加HostRuntimeSnapshot字段，尚不存在）；这些仅用于等待，不是逐笔时间。

WriteRuntimeMetrics是noexcept wrapper，包围离线WriteJson/排序/格式化的异常并返回false/导出不可用，不能terminate。导出把§8.1已sealed边界传入同一Session离线报告扩展，不在热路径排序或Reset。checkpoint由§8.3另一显式功能gate控制：callback内不得Stop/join；timed benchmark不安装它。

### 8. 固定轨迹runner、安全与输出

提案，当前函数/CLI不存在：

~~~cpp
// Draw3.HiddenWindowTest.h；实现仍在已有.cpp
int RunHiddenWindowPresentationBenchmark(const wchar_t* privateOutputRoot,
    HostPresentationMode requiredMode, Bridge::Tool tool,
    unsigned round) noexcept; // 首批scenario固定core-linear-8
int RunHiddenWindowMetricsEquivalenceTest(const wchar_t* privateOutputRoot,
    HostPresentationMode requiredMode) noexcept; // §8.3；尚不存在
~~~

建议root CLI形状：Inkeys.exe --draw3-presentation-benchmark --output-root <私有root> --presenter <dcomp|ulw> --tool <固定产品tool名> --scenario core-linear-8 --round <1|2|3>；功能入口另为--draw3-metrics-equivalence-test --output-root <私有root> --presenter <dcomp|ulw>。全部尚不存在，不能运行或记PASS。CommandLineToArgvW严格解析，未知/重复/缺失tool/scenario/source组合失败；只接受下表本批固定population，不自动降为其它场景。分派在配置、single-instance、Office和普通启动之前，仍用现wWinMain。

复用MakeHiddenSpec / StyleContext / Window::Service四窗创建和StartProduct，提取makeSpecs这段小共享代码即可。窗口均自有PID、坐标-32000、visible=false；测量不调用RunMode里显式显示窗口/SetCapture/Office/PPT持久化那些正确性场景。主DComp创建期含NOREDIRECTIONBITMAP；ULW新建legacy-compatible链，不能把已绑定DComp的主HWND强改为ULW。requested强制模式启动/恢复不符，记录backend mismatch，不能回退后叫指定backend PASS。

注入沿kDraw3HiddenTestContactMessage、DrawpadMsgCallback、Host::PublishHiddenTestContact。runner用有界SendMessageTimeoutW(SMTO_BLOCK|SMTO_ABORTIFHUNG|SMTO_ERRORONEXIT)，核HWND属本次Window::Service、PID一致和message结果0=accepted/-1=rejected；超时不自动重发Down。定时等待只是发包节拍，实际owner生成QPC保留，不能将计划轨迹时间伪装sourceQPC。原message的鼠标左右/笔尖尾/Touch flags可区分五入口，但同一Touch仅一个contactId；现入口不支持两根同类型Touch、任意压力轨迹或每包source回执，未补接口前相应场景明确not available。

每population独立run，固定device/effectiveTool/真实eraserWidthMode/scenario/viewport/backend/output；不引入通用scenario registry。本批16 warm-up+200 measured连续同一Session，切界见§8.1；三轮Release串行。非Laser正常无失败时steady landing=200；不足仍保留分母并标incomplete，不能多画到漂亮200或把Move补成Down。Laser同样16+200 contact/lifecycle，只把正式landing/Up列excluded。任一群体有效事件不足1000时P99=null，三轮200合并600也不够。点/慢/快/折返/停动/Cancel/换工具/多接触属于后续独立场景门；首批CLI不认识它们就拒绝并明确未覆盖，不得省去后续职责。

WaitUntil / RuntimeRevision / atomic进度只等完成与超时。延迟源自owner Down.qpc→同笔首个匹配success returnQpc，终点绝不取poll结束/SendMessage返回。原U1报告仅Down landing；Move/Up逐事件latency若未新增真实sequence/QPC/final-item记录，必须null/not collected，不能从Down或全局present计数派生。为补Move/Up应在同一Session扩展固定事件记录并冻结producer，当前U2第一版不宣称已覆盖这些分布。

输出root由root预创建在本任务忽略TestResults/release-hardening下的唯一目录。校验绝对路径、各父组件和目标非reparse、本轮create-new成功，不接受用户文档/配置路径；报告文件固定相对名、CREATE_NEW，不覆盖旧sentinel/报告。原有自动保存fixture会在EXE旁创建目录，新runner不能继承该路径；只需Desktop画布时autoSaveRoot为空/自动保存关，若验证document恢复则只用privateRoot下专门sandbox根。保留raw、manifest、失败样本；超时只处理核身份的自建测试PID，不删除未知文件、不全局SendInput/ComputerUse，不接Office。

runner scope让StyleContext / Service / startup callbacks活到Host Stop真join，之后WriteRuntimeMetrics，最后Service StopAndJoin；异常路径同样先StopProduct再销毁Service。尚未完成join的run标incomplete，不通过强退后的假报告补尾数据。

#### 8.1 R4：有限阶段与sealed ordinal边界（提案，尚不存在）

选择有限Cold / Warmup / Measured边界，复用U1 raw contactOrdinal/frameSerial，不新增ordinal API，不在warmup后Reset。旁挂POD最多保存ColdEnd、WarmEnd、MeasuredEnd三项：exists、ownerQpc、frameSerial、contactSeenOrdinal、Session Snapshot、实际input diagnostics baseline、terminal/Stored-completed累计数。初始Cold无输入；第一张真实startup成功空帧的即时returnQpc/serial封ColdEnd。首次交互是warm ordinal1，另标firstInteraction，不重复分母。

Warmup计划恰16，下一个计划Down是Measured的17。WarmEnd只由绘制owner在第16个终态已处理、所有warm runtime（含80ms deferred Up）已收尾、必要Stored进入当前权威L2并成功提交、无warm未确认pending时封口，取实际success returnQpc和当前Snapshot.contactSeen=16；Laser以必要终态/生命周期结束成功帧封口，不要求excluded landing。失败/超时使WarmEnd不存在，Measured不开始，raw保留为warm incomplete。runner等待存在位，不用poll时间封口。

MeasuredEnd同理在第200个计划measured终态/最终提交后封口，取contactSeen=216；中间drop/失效/失败可重试原帧，但不重发/替换原笔或新增计划Down。按Down ordinal归群：1..16 Warmup、17..216 Measured；首批没有其它contact/source。landing晚确认仍归原Down群，不按returnQpc择好样本。frame/present按owner serial的ColdEnd/WarmEnd/MeasuredEnd分界归群；冷/warm/measured的raw/seen/dropped/invalid、真实API失败及无Present都保留。跨界旧pending、source计数/ordinal不符或未达边界是incomplete，不用Reset修齐。

离线由唯一Session writer扩展以下签名（尚不存在），旧两参原样保留：

~~~cpp
bool RuntimeMetricsSession::WriteJson(const wchar_t* outputPath,
    const ContactInputDiagnosticsSnapshot& input,
    const RuntimeMetricsPhaseBoundaries& phases) const;
~~~

RuntimeMetricsPhaseBoundaries就是固定三界/基线POD；调用者必须joined/sealed。同一生产Median/Percentile/WriteDistribution复用于phase summaries，schema2全run summary保留并明确allRun。禁止用现device/tool allRun toolSummaries冒充steady；新增finite phaseSummaries及phase coverage。无完整MeasuredEnd则steady标incomplete，原raw仍导出。50ms帧wall阈值名frameWallGe50Ms，既有16.67ms legacy longFrame单列，不混称同指标。已知正常216笔回归需raw landing216、warm16、measured200；插入drop/取消/错proof后原群体seen不缩，cold帧/失败/无Present保留。

#### 8.2 固定core source packet合同（提案，尚不存在）

| 项 | 首批固定值 / 核对 |
| --- | --- |
| 场景与表面 | scenarioVersion=1 / core-linear-8，Desktop/page0，320×240，viewport(0,0,1)，Primary output、selection=false；同run不Clear/换页/resize；报告实际DPI，fixed logical surface不冒称物理屏幕。 |
| 一笔10包 | Down(40,48)；Move j=1..8为(40+24j,48+12j)，Up(232,144)。相邻计划包8ms，Down→Up计划72ms；每笔后固定120ms无新contact并等必要终态/成功确认，最迟2s；Laser等真实Hold/Fade/粒子结束最迟4s。deferred Up未闭合不得发下一Down。 |
| source/压感 | 现有kHiddenTestExternalPenFlag，非倒转PenTip；真实隐藏入口Down压力0.8、Move/Up0.7，源为synthetic external-pen mailbox，不是硬件。无NoPressure/DelayedUp/鼠标/Touch/任意source别名。 |
| tools | Bridge::Tool的Pen、HardPen、Highlighter、FixedEraser、SpeedEraser、四Shape、Laser逐一白名单；实际Controller DrawingTool与entry宽度模式在Down锁存后写meta，Fixed/Speed须实际ResolvedInput模式吻合；未知/不吻合拒绝该population，不用整数猜映射。 |
| 样式/效果 | ProductState明确colorRgba=0x000000FF、widthDip=2、autoSave=false；正式prediction、模型、pacing、Laser hold/粒子和erase配置沿已有值并记实际值，不关闭效果取得分数。 |
| 分母/预算 | Warmup=16×10包、Measured=200×10包；应有216 Down、1728 Move、216 Up，共2160包。记录每包sent/accepted/rejected/timeout与计划tick/实际owner sourceQpc；覆盖/去抖/模型未采纳另计，不承诺1728 Move均进模型/像素。非Laser整轮300s、Laser900s；Warm失败不开始Measured，timeout不重发Down。 |

发送可等计划deadline，但bench实际snapshot.qpc仍在Host消息owner取QPC；计划tick/runner sendQpc只作偏差，不替代sourceQpc。functional fixed-source replay采用§8.3另一入口；fixtureKind分开，functional不进入三轮性能群。其它source/轨迹未开放，列not covered。

#### 8.3 R5：Renderer仍活时的一次owner checkpoint / 私有UInk

拟新增有限功能接口，全部尚不存在：HostStartOptions尾部enableHiddenTestEquivalenceCheckpoint=false、enableHiddenTestFixedSourceReplay=false；只允许显式equivalence fixture安装，正常defaultoff和timed benchmark均false。

~~~cpp
// Host.h：纯数值request/receipt，不含Renderer/COM/getter
bool Host::RequestHiddenTestCheckpoint(
    uint64_t requestId, uint32_t expectedTerminalDelta,
    uint32_t expectedVisibleStrokeCount) noexcept;
bool Host::CopyHiddenTestCheckpoint(uint64_t requestId,
    HostHiddenTestCheckpointReceipt& out, std::span<uint8_t> bgra) const noexcept;
bool Host::CopyHiddenTestPersistenceReceipt(uint64_t requestId,
    HostHiddenTestPersistenceReceipt& out) const noexcept;
// DrawingController.cppm；附加observer字段，默认null
void (*hiddenTestCheckpoint)(void* context,
    const HiddenTestCheckpointObservation& value) noexcept = nullptr;
~~~

requestId非0，每功能run最多1个；目标固定当前Desktop authoritative L2、320×240 BGRA，bgra须307200字节。receipt固定Pending/Complete/Failed、requestId、实际owner test-frame serial、success returnQpc、actual output raw tuple/匿名编号、history/stamp/terminal累计值与pixelBytes；true只表示取得同id Complete，false为Pending/Failed/错id/容量，不返回旧run数据。buffer由Host在Start前自有预分配，Copy只读CPU自有值/bytes，不访问GPU。

Controller在真实终态CPU Stored提交、raster/composite成功并成功Present后，当前无active/awaitReconnect、stamp有效/输出匹配时，经可选DrawingControllerRuntimeObserver::hiddenTestCheckpoint传有限owner payload。payload仅含request/cutoff判定必需的test-frame serial、success QPC、history/stamp/output/terminal数值，不含GPU/context/document借用。callback仅功能gate安装；metrics-off功能run只启用共用stamp/cutoff和test-frame计数，不构造Session/landing表，正式off两gate都关，不新增时钟/状态采样。

Host在同绘制线程核request/cutoff后读仍活renderer.layerL2Texture：CopyResource到预先准备的staging，Map READ按RowPitch逐行复制自有BGRA，Unmap后发布Complete。现WARP probe已有该模式（Controller当前:2526–2540、HiddenWindowTest:2182–2203）。这是一次功能检查，禁止timed benchmark/每帧/GPU查询或每帧等待。检查点先于Host:1224 drawing.reset / :1226 renderer.ReleaseResources；post-Stop只读已复制CPU结果。失败/错尺寸/未到cutoff/Stop提前/强退不算等价；context/buffer活到Stop真join，callback不得反向Stop。

固定source功能仍走Window Service WndProc→Host→真实Coordinator→Controller。最小新固定replay flag与scenarioVersion=1只接受内部core-linear-8十包cursor：Host首Down owner设本run epoch，后续snapshot.qpc=epoch+预定0/8/.../72ms offset（用实际QPC频率，有界转换）；发送仍按计划deadline，不能把全部future-QPC包瞬时灌入。coords/phase/pressure/flags逐包须匹配内部计划，非法/额外/重复包拒绝；只对该功能route使用固定snapshot.qpc，原PublishDown/Move/Up、工具、模型和采样策略保留。性能flag关时原owner QPC完全不变。flag/messages/observer payload尚不存在，由U3与U2接口writer一次交接；消息沿现有phase/flags/packed coords等数值参数，Host自有cursor决定packet index，不传可能因timeout晚到而悬空的栈payload指针。功能run的计划/实发/发布和终态计数必须核，未处理/未匹配/timeout不得算等价。

首批exact功能格子限定四Shape（每tool/backend各off/on独立run、单笔上述十包）。Controller现completeModelUp:6725和consumeLatestSnapshot:6908最终强制raw Up端点；FinalizeStoredShape:515仅保存raw start/end/width。finalL2不含prediction/光标，在相同QPC相对source、样式、DPI/viewport和实际device/backend下要求全部307200 BGRA逐位比较及Stored两点/width/style一致；hash仅辅助，不能代替bytes。Run不为checkpoint再Present/改dirty/画质，取终态成功后的既有L2。

其它modeler工具沿同一固定source实际入口核Down/Move/Up发布、必要终态消费/回收、visible stroke数、顺序/工具/样式/有效宽度/有限终点和生产UInk语义。框架帧时刻仍真实推进，Pen/HardPen display-time、prediction/停笔老化可改变几何；首批不要求逐位pixel或宣称它们像素等价。端点/宽度允差必须另有工具实际contract才冻结，未冻即not covered；Shape exact不扩成全工具/全source PASS。

UInk功能格子用privateRoot/persistence-off、privateRoot/persistence-on两个非reparse独立sandbox，显式autoSaveRoot和同autoSaveEnabled=true，不调用旧CheckPresentationPersistence的EXE邻目录创建段。checkpoint Complete后经真实PublishProductCommand(Clear)→Controller前序捕获→Host worker；在Host既有ObserveDesktopAutoSave / PumpDesktopCompletions旁挂仅一个匹配requestId/fileGuid的receipt，必须DesktopPersistenceCompletion Save/Committed，随后StopProduct走原final barrier/CloseAndDrain（Clear后空页不新增假尾保存）。该方法避免Stop期间已提交completion未被owner领取的竞态被误当有证。PPT格如增加，同样须其Save/Committed exact target/mutation/slot/fileGuid；不抢走产品completion。

生产ReadUInkFile须status=Complete，再由fresh同sandbox生产DesktopAutoSaveService::SubmitLoad(fileGuid)→Loaded→新自有snapshot逐项比较；或新Host同root走既有PPT冷载，descriptor只合成，不启动Office。HostHiddenTestPersistenceReceipt是有限Pending/Committed/Failed的CPU载荷，含匹配requestId和内部fileGuid；内部GUID只用于生产load，不导出到metrics raw。先核本run保存/Loaded身份相等，再按匿名对应页比visible順序/工具/样式/宽度/端点；不要求两run新建随机GUID相等。Shape两点及宽度可exact；modeler只按实际语义限界。文件存在/孤儿/不同hash不够，receipt/Loaded未到即失败。当前这些gate/receipt不存在，实施后才可PASS；不证明Office/跨进程恢复，功能I/O/readback成本不混性能，性能run仍autoSaveRoot空。

### 9. 报告数值与证据门

| 数据 | 真producer / 最小口径 |
| --- | --- |
| Identity / 环境 | 本轮EXE散列、源码/工作区fingerprint、Release/架构、OS/补丁/CPU/GPU/驱动、供电、DPI/刷新率/多屏、实际driverType/FL/presenter/output、QPC频率。外部身份写manifest，raw不输出指针/GUID/文稿路径。 |
| 每群分母 | sent / accepted / rejected / contactSeen / retained / dropped / invalid / pendingOverflow / cancelled / superseded / unpresented / excludedLaser；attempt / success / failure / noPresent / rasterFailed / frames retained/dropped。所有群体给count、median/P95、P99 null门。 |
| wall / CPU | FrameSample原wall包括Present，不能称CPU。实际GetThreadTimes(GetCurrentThread)的user+kernel前后差另记threadCpuMs，API失败null；这是尚未添加的数值字段。run总线程CPU也可独立取delta，不冒充逐帧CPU分位数。 |
| 阶段 | 现调用交错，不移动模型/几何/D3D调用；新增batch边界计时只报真实可分的ingress、model/prediction、geometry+rasterSubmit、composite、Present、wait。未采到的字段null，不互相扣减伪造纯GPU。 |
| queue / slots | 当前DiagnosticsSnapshot有occupiedSlots/slotCapacity，EnableDiagnostics保持defaultoff；内部queue.size_approx()未导出且只含Down/marker，不是Move队列。若要数值，只加只读ApproximateIngressQueueDepth() const noexcept返回它（提案，尚不存在）；否则queue字段null，不用published-recycled反推。 |
| 资源 | runner在warmup前后/测量后取进程private bytes、working set、handle/GDI/USER快照；只查询自进程。显存、GPU timestamp、cache evict尚无生产hook则null；Capture/Restore真实status/path可以旁挂计数。 |

把thread CPU / Stage wall / source事件补入RuntimeMetricsFrameSample或同一Session的值记录，由唯一Session owner扩展并重跑U1受影响门；不能由runner从sleep / poll / aggregate maxima自算。测量期间停构建、扫描和其它基准；保留三轮数据和失败轮。三轮短场景不证明60–120分钟资源稳定，也不证明长期约1000笔历史成本。

#### 9.1 Laser必须保留的有限帧/成本/生命周期合同

正式landing/Up分布excluded不等于跳过工具。在同一FrameSample添加数值laserTrailPhase / laserCoverageMode，分别取真实InkPrediction.cppm:111的LaserTrailPhase {Inactive,Active,Hold,Fade}和:129的LaserCoverageMode {Inactive,Incremental,FullRedraw}，离线用真实符号映射，不另猜整数表。还记录activeContactCount、lastAllUpQpc、实际hold/fade时长、opacity、layer count、bake attempted/result、particles实际enable/emitted count与资源事件；未得到真实字段时not collected，不能报0。

固定reasonFlags提案由Session唯一writer登记，U2装配：bit0 PhysicalBefore、1 PhysicalAfter、2 Terminal、3 Command、4 Page、5 Resize、6 Recovery、7 Hover、8 LaserActive、9 LaserBake、10 LaserHold、11 LaserFade、12 LaserExpiry、13 LaserParticles、14 NoPresent、15 RasterFailed、16 PresentFailed、17 PresentSucceeded、18 OutputMismatch、19 AuthoritativeWithheld、20 PartialReplay。NoPresent与真实Present结果互斥；各字位只来自实际本帧行为。pure idle / Hold wait / maintenance早continue另计loop/wake/phase transition，不造零耗时render frame。上述新增FrameSample字段/enum尚不存在，不能取未固定的reasonFlags直接宣布覆盖。

batch计时包括inclusive frame wall（含Present、不含后续pacing wait），model/prediction、geometry+rasterSubmit、composite和Present；Laser incremental Update、BakeLaserStrokeLayers当前:1983的bool结果、Resolve/DrawLaserStrokeLayers和particle Emit/Update/Draw在其真实调用批次旁计wall/count。Laser子段可能包含于geometry/composite父段，报告注明inclusive，不把父子相加。Up-bake失败、FullRedraw失败、expiry清层、backend recovery、Hold无渲染和Fade/粒子有渲染都分开；线程CPU用真实thread-time差，不用wall扣Present代算。

Laser资源在warm前后/measured结束取自进程private/WS/handle/GDI/USER，记录实际renderer资源create/failure/release事件和known scratch尺寸（4×width×height只叫已知scratch估算，非完整显存）。没有GPU timestamp/disjoint、显存或cache hook继续null，但不能把可得软件帧/段/phase/资源全部留空后声称Laser调查完成。三个Release轮次也保留每轮16+200 lifecycle源包、成功失败与phase边界；excluded landing的count=0/null分位数和excluded分母清楚列出。

#### 9.2 首批未覆盖的Move/Up必须继续实施

现U1只记录Down→first verified Present。Move→Present需要真实消费snapshot的sequence/qpc、实际Append/Extract采纳标志、该Move非预测几何/raster token、冻结的对应成功候选和即时returnQpc；源Move发布/覆盖/争用/未消费/未采纳各自分母还需生产publisher/consumer有限事件记录。仅有contactOrdinal的Down landing、seq差或全局Present计数不能生成逐Move延迟，也不能把global seq差命名精确coalesced数量。

Up→final stability需要真实terminal sequence/qpc、Stored item/contentGeneration/final raster stamp、首次成功final proof回执，以及事先固定的安静检查窗口（建议120ms）内该final item token未再变；Laser另有生命周期，仍excluded。安静检查只观测已有事件/时钟，不请求额外帧、不从poll结束算成功QPC。实际Up已消费/回收而Final未成功时保留失败/未呈现，不拿Down时间补Up。

这些事件数组/导出尚不存在；由Session writer＋U2 Controller writer冻结同一Session固定容量载荷，U3仅复制/导出真实source和配场景，必要publisher只读hook再单独指定ContactInput/Host writer。首批明确moveLatency/terminalStableLatency=null、not covered；Frame/cost终态已记录不关闭该欠账。点/慢快/折返停动/Cancel换工具/多contact、压感/同型Touch、长文档/长期资源亦继续独立门，不由core-linear-8/Shape exact代替。

### 10. 唯一writer拆分与实施门

| owner | 最小文件责任 |
| --- | --- |
| resume_draw3_metrics（当前已冻U1） | RuntimeMetrics.cppm/.cpp：root批准后bool精确InvalidateContact、finite phase POD/WriteJson重载、FrameReason/Laser数值字段和实际报告；Controller现有U1 probe须与U2串行交接；不新增ordinal框架。 |
| root明确交接后的U2唯一owner | DrawingController.cppm/.cpp：adopted/L2 stamp、output匿名存在位、真Present分母、reconnect精确失效、owner phase边界/有限checkpoint payload；Run共用probe；保留E03三拒收。 |
| root / 明确指定U3唯一owner | Host.h/.cpp、HiddenWindowTest.h/.cpp：Session生命周期、phase meta、固定core runner、功能replay/checkpoint/persistence receipt、私有root；必要Product仅窄转发，不给Renderer/COM getter。 |
| root | IdtMain.cpp parser、项目/filters（现文件无需新登记）、构建/CLI槽、spec和父账本。Helper/Main的C生命周期工作不在本研究所有权。 |
| 若批准queue只读字段 | 另明确ContactInput.cppm/.cpp唯一owner；只导出既有诊断，不改发布/采样/准入策略。 |

顺序：root审本合同→U1 GREEN/独立检查→转交Controller→U2真实producer/反例probe→独立代码检查→Host接线/hidden off-on功能一致→root串行完整InkeysRepo.sln Debug|ARM64及相关Release/三架构→Release三轮raw。ARM64原生MSBuild、同PowerShell规范PATH、至少5分钟构建时限；本research不占构建/运行槽。

最小新验收按R1–R5冻结：①真实call三结果均计attempt，no-call只计无Present；②A Registered/Pending精确失效、A Confirmed/重复/stale no-op，C Stored和本帧成功资格保留，>64 reconnect不泄指标槽；③合法output0/1/2、重复/恢复新编号、极大raw/耗尽；④已知同Session16+200 raw216、phase16/200、warm失败不切、跨界/丢样/incomplete和P99门；⑤实际owner终态后checkpoint、off/on固定source Shape full BGRA、提前stop/错request/读失败反例、matched Committed→fresh Loaded/UInk。CPUprobe仅证明其helper合同，实际hidden再证生产owner调用链；没有实际入口的格子不PASS。

### 11. Related specs 与上下文

- 已读根AGENTS与.trellis/workflow.md、当前prd/design/implement；没有读implement.jsonl/check.jsonl。
- 父handoff.md最新恢复/所有权段，performance.md环境、HC/H2身份、门槛；上述身份来自账本，本研究没有Git复核。
- 当前research/performance-sampling-postcommit-contract.md、draw3-metrics-implementation-design.md、draw3-metrics-design-review.md、draw3-metrics-session-implementation.md；旧draw3-hidden-end-to-end-benchmark-design.md的snapshot非因果结论保留，本文件把旧每轮100门提高到已冻结200/群体。
- 本次完整读draw3-content-and-host-design-review.md并逐R1–R5回核；该review文件保持原样，修订合同须另由独立reviewer给出结论。
- .trellis/spec/native-desktop/draw3-integration.md：单Host/设备线程、selection output handshake、Clear/runtime history和保存屏障。
- .trellis/spec/native-desktop/input-and-ink.md：Contact Closing/E03精确代次拒收，Reset必须producer静止+consumer join。
- .trellis/spec/native-desktop/cpp-conventions.md、build-and-compatibility.md；native/platform-and-resources.md、quality-and-validation.md：格式、ABI、资源和证据等级。standalone命令/窗口模型不机械套入产品。
- guides/index.md、cross-layer-thinking-guide.md、code-reuse-thinking-guide.md：唯一权威行为、三层合成、多笔独立Stored顺序和实际呈现边界。

### 12. External references（官方接口，访问于2026-09-30）

- [QueryPerformanceCounter](https://learn.microsoft.com/en-us/windows/win32/api/profileapi/nf-profileapi-queryperformancecounter)：计数用于时间间隔，不能证明显示器光子。
- [GetThreadTimes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getthreadtimes)：user/kernel时间为100ns单位累计；本合同用delta，失败不可用。
- [SendMessageTimeoutW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendmessagetimeoutw)：跨线程等消息处理/超时；失败不一定设置LastError，调用前清LastError并记录通用失败；同queue直接调用忽略timeout，runner应独立于Window Service owner。
- [GetProcessMemoryInfo](https://learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-getprocessmemoryinfo)：PSAPI_VERSION影响K32/wrapper导入。新增资源查询沿项目既有Win7可用方式，不引入Win8+静态接口或EX2要求。

## Caveats / Not Found

- 本研究没有新的动态PASS；既有U1绿色引用其独立review，本修订不升级U2/U3 producer/性能/像素门，R1–R5等待独立复审。
- 当前没有提议的Host options/export、两个新CLI、bool精确per-contact失效、phase POD/报告重载、output映射、checkpoint/fixed replay/persistence receipt、FrameReason/Laser字段、queue getter、逐Move/Up raw、逐帧thread CPU或完善DPI/device epoch。全部提案需root批准、唯一writer实施后才能运行。
- 成功值源于真实栅格提交/合成和软件PresentReturn，不证明GPU duration、光学延迟、RTS真实packet或物理笔/Touch/HW覆盖。隐藏坐标/遮挡可能改变OS合成调度。
- Laser完整因果token、同类型多Touch注入、任意压感和源事件回执尚未可靠，明确排除/缺失，不能据frame成功补其landing或Up尾。
- Win7 SP1仅KB2670838、Hardware FL11.0有/无→WARP、DComp缺失→ULW、FLIP_SEQUENTIAL和两个DWM禁用保持；真Win7透明/resize/device-lost/输入不由本机隐藏结果替代。
- HC仍是run31487748238 / 82f7b7c0候选，未证就是用户安装版；H2 20260713a只有公开身份和隔离启动证据。HF内部新QPC不能与旧二进制无相同观察口混算性能胜负。
- 长期资源、Office/UInk durable与可见恢复、GPU/光学/实际硬件/HC-H2可比性均保留原门禁；本报告没有新增性能数字。
