# Research: UI3 首批有限目标、SVG proof 与私有 fixture 接口冻结提案

- Query: 闭合 ui3-real-presentation-design-review 的 U01/U02/U03/U04，先实施 MainFold 与 DrawAttribute 两个真实场景，冻结 DTO、发布顺序、资源 proof、输入来源、鉴权与停止。
- Scope: internal；仅源代码/已保存 review/父 handoff 与 performance；无 Git、build、test、GUI、基准。
- Date: 2026-09-30
- Active task: .trellis/tasks/09-27-integration-and-release-check
- Writer: trellis-research，只写本文件。
- Status: **接口提案，以下新增类型/方法/CLI 均尚不存在。** B1 已由 root 判 GREEN/PATCH_READY；本文只复用其 schema2/StampBarCommit，未重做 B1。
- Revision: R2，按 ui3-finite-target-and-fixture-design-review 的 F01–F06 修订。B3计数的设计GREEN保留；B2、paint proof、F仍需以本版交独立复审，未获运行CLEAR。

## Findings

### 1. 此批覆盖与证据定位

仅两个固定 scene：
- MainFold：实际主按钮 Touch-tagged tap 经 Seek/真实 toggle，交替展开/收起，包含原 Main click pulse 和原自动居中/回弹，不能直接设置 fold。
- DrawAttribute：在已通过正常 ChangeStateModeToPen 初始化的同一画笔模式、主栏已展开时，tap 实际 Draw preset，执行真实 clickFunc 的打开/关闭画笔属性；不测试切笔、颜色拖动、FineDial、鼠标光、Settings竞争、PPT或其它 panel。

scope名为 LayoutAndSvgProofV1。即便这两个 scene 通过，E04 的其它 scene/family/GPU/真笔/HC/H2 仍未完成。

实际代码事实：
- Main 接受支路在 Bar.Interaction.cpp::DispatchPointerStages 3294–3316：TryBeginToggle后，mainButtonClickPulseSerial先增加，再修改fold/中心请求/其它浮层，并UpdateRendering。
- DrawAttribute 真正业务写入在 Bar.Button.cpp::PresetInitialization 的 Draw callback 296–319；不是 Interaction 的通用按钮loop。Interaction 3388–3425先关menu、调用clickFunc，随后UpdateRendering。
- Bar.Main.cpp::UpdateRendering 209–227 调StateUpdate/ThicknessDisplayUpdate，之后Notify/Request；StateUpdate→PresetHoming（Bar.Button.cpp:839、1315）还会因fold/tool/visibility使drawAttribute=false。
- 原Submit的Main条件在 Bar.RenderLoop.cpp:2223–2315，相关timeline/pulse在1229、624–626；Advance原shape/SVG/word/button循环5542–5663、5851–5938已取得实际IsSame/active结果。
- SVG真实cache lookup/draw在 Bar.Rendering.cpp::Svg 2723–2819；CacheBitmap parse/raster/upload在Bar.UI.cpp:400–489，ResetCache:387，content真实提交在AdvanceContentTransition:354/365及ApplyContentDirect:393、普通ChangeString.ApplyTar在RenderLoop:5623。
- B1 当前源码确有FrameDiagnostics.hasBarCommitStamp/barCommitTicks/barAttemptSerial/barCommitEpoch，RawCaptureReport schema2，StampBarCommit在真实CompleteAttempt后。原proxy仍保留。
- 原QueueWindowMessageInLayoutSpace读取GetMessagePos:6790；真实WM_TOUCH:785–919构造screen-marked ExMessage、SetKeyBoardDown及Window::Enqueue；PrepareBarInteractionMessage:368在真正消费时逆映射。
- Window.cppm::WindowSpec有windowProc、messageCallback、visible、bindMessages；fixture可接private callback并保留真实生产Bar.WindowProc。
- 现E02 copied-child鉴权是精确parent HANDLE/PID/镜像文件身份、private非reparse bin/current copiedEXE、三继承HANDLE+固定mapping；fatal purpose不能授权新fixture。
- BarButtonSet::Load:1287可能config.Write；Other.Config.cpp::GetFilePath:729返回globalPath+Inkeys/Config/main.json，必须先设置private root。
- 本机Win11 ARM64/Oray可能虚拟屏、WARP UI3；父performance已冻结三轮/噪声门槛。新软件tick不能证明Win7、光学延迟、指定Canary或Inkeys2胜出。

### 2. U02：有限数值 DTO（无需全 animation registry）

按root常规取舍冻结到独立 Inkeys/Inkeys/UI/Bar/Bar.PresentationProbe.h：有限DTO、publication/observer/source/latch的纯数值声明与生产小helper都归Bar内部。一个Bar.Presentation.Test.cpp在同module组装fixture，必要的私有bootstrap实现可放Bar.PresentationProbe.cpp；不新增通用测试框架/独立telemetry module。RenderPipeline.cppm不import Bar、不加入有限业务DTO，R schema2保持现B1字段；finite-targets是独立离线记录。Header不导出业务setter；allraw无COM/HWND/指针/string。root独占新文件/project登记并冻结include/import位置，保持当前module/ODR约定。

    enum class Ui3FiniteScene : uint32_t { MainFold=1, DrawAttribute=2 };
    enum class Ui3FiniteStatus : uint32_t {
        Pending, Accepted, CompletedLayoutAndSvg, AcceptedNoChange,
        RejectedByBusiness, Superseded, AmbiguousPublication,
        ResourceUnverified, SourceRejected, UnsupportedState, Stopped, TimedOut, Overflow
    };
    enum class Ui3BusinessWriteWitness : uint32_t { Unknown, NoBusinessWrite, WriteOccurred };
    struct Ui3FiniteSignature {
        uint32_t flags;              // fold/drawAttribute/geometry/more/eraser + auxClosed
        uint32_t stateMode, penMode, penColorRgb, penWidthBits;
        uint32_t mainSide, primarySide, thicknessView, darkStyle, dpi;
        uint64_t toolRevision, displaySerial, configZoomBits;
        uint64_t validMask;          // 当前schema所有required位齐全才可匹配
    };
    struct Ui3FiniteAccepted {
        uint64_t runSerial, stepId, revision, publicationSerial, sourceSequence;
        Ui3FiniteScene scene;
        Ui3FiniteStatus status;
        int64_t ownerReceiveTicks, acceptedTicks;
        Ui3FiniteSignature signature;
    };
    struct Ui3FiniteCandidate {
        Ui3FiniteAccepted accepted;   // numeric copy from stable publication
        uint64_t epoch, surfaceSerial, frameAttemptSerial;
        uint64_t rootBatchRevision, drawBatchRevision;
        uint32_t pendingRoles, mismatchRoles;
        uint32_t requiredSvg, verifiedSvg, failedSvg, unverifiedSvg;
        uint32_t firstUnverifiedSvgTag, firstUnverifiedReason;
        uint32_t targetWidth, targetHeight;
        int64_t targetConsumedTicks, settledTicks;
        bool stablePublication, settled, svgProofComplete;
    };
    struct Ui3FiniteTargetRecord {
        Ui3FiniteAccepted accepted;
        Ui3FiniteStatus terminalStatus;
        uint64_t epoch, surfaceSerial, trueBarAttemptSerial;
        int64_t consumedTicks, settledTicks, finalCommitTicks;
        uint32_t pendingRoles, proofMask, failureFlags;
        uint64_t reusedRevision;     // no-change可引用旧goal，不能伪造新commit
        bool timingValid;           // capture-off时间字段不参与统计，离线输出null
        uint32_t requiredSvg, verifiedSvg, firstUnverifiedSvgTag;
    };

flags/schema1位宽冻结：bit0 fold、1 drawAttribute、2 geometryAttribute、3 moreExpanded、4 eraserAttribute、5 eraserSensitivityOpen；bit6 auxClosed由本scene要求的menu/color/slider/tooltip关闭旗标全部成立生成；其它bit必须0。mainSide/primarySide/darkStyle为0或1，thicknessView限定生产enum，dpi有限非零，width/color沿同一版本tool snapshot。validMask bit0 flags、1 tool、2 side、3 thicknessView、4 theme、5 dpi、6 displaySerial、7 zoom；完整candidate required=0xFF，未知字段不能补0装匹配。float/double固定bit_cast到32/64位，finite/range先核。Tool signature来自同一个GetStateModeVersionedSnapshot，不能组合多个独立getter。stateMode/penMode/width/color在两scene冻结不变化；未知出口、非批准mode/aux/display变化均保留request并拒确认，不能筛掉分母。flags/side等正常演进由当前production target锁存，不能commit时倒读新值。

字段owner再明确：accepted semantic goal核Interaction写入的flags/tool/thickness与已冻结theme/configZoom；display/dpi必须匹配initialStable环境。side是原PositionUpdate/自动居中/Submit的render派生值，accepted时只作快照信息，不将合法自动换边误作用户新request。目标消费后由真实Submit锁存实际side/rootBatch/drawBatch到candidate，pending包括该派生运动；未知renderer写fold或工作区/display变化仍拒确认。SameSemanticGoal比较source字段及环境，完整paint/property proof比较当前候选的全部派生target；绝不能在commit时补最新side或人为禁止自动居中来让签名不变。

目标只有最多512个record（16warm+200measured=216；startup token0另计），输入最多1024条；容量在所有owner启动前分配，合并R callback/batch/有限target预算核<=64MiB或root明确更小总预算。满后保留前缀并继续seen/dropped分母，capture失败不能更改产品是否启动。draw/candidate stack固定数值，所有offline统计只在join后。

建议生产方法signature（全部提案）：

    class Ui3FinitePublication {
      Mutation BeginMutation(uint64_t stepId, Ui3FiniteScene, uint64_t sourceSequence) noexcept;
      void MarkBusinessAccepted(Mutation&) noexcept;
      void FinishAtRenderRequest(Mutation&, const Ui3FiniteSignature&, int64_t acceptedTicks) noexcept;
      void FinishRejected(Mutation&, Ui3FiniteStatus,
          Ui3BusinessWriteWitness witness=Ui3BusinessWriteWitness::Unknown) noexcept;
      bool TryReadStable(Ui3FiniteAccepted&) const noexcept;
      bool StillSameSemanticGoal(const Ui3FiniteAccepted& candidate,
          Ui3FiniteAccepted& currentStable) const noexcept;
    };
    class Ui3FiniteObserver {
      void BeginFrame(const Ui3FiniteAccepted&, uint64_t epoch, uint64_t frameAttempt) noexcept;
      void MarkConsumed(const Ui3FiniteSignature&, uint64_t rootBatch, uint64_t drawBatch) noexcept;
      void ObserveProperty(Ui3PropertyRole, bool activeAfterAdvance, bool sameAfterAdvance) noexcept;
      void ObserveLifecycle(uint32_t pendingFlags) noexcept;
      void ObserveSvg(const Ui3SvgDrawObservation&) noexcept;
      void SettleCandidate(uint64_t surfaceSerial, uint32_t width, uint32_t height) noexcept;
      void CompleteAttempt(const Ui3FiniteCandidate&, bool committed, int64_t trueCommitTicks) noexcept;
      void Stop(Ui3FiniteStatus) noexcept;
    };

Mutation只是数字ticket+nullable当前observer，不拥有业务锁、资源或callback；default observer=null，Begin/Mark/Finish立即no-op、无读钟/分配。fixture capture-off仍安装有限身份/终态证明（不读阶段时钟、不配置R数组），用于on/off等价；普通产品没有fixture probe。

### 3. U02：写入/发布次序（闭合“旧token但新业务值”）

仅在已鉴权fixture完成初始布局并进入Warmup/Measurement之后，Interaction才是这两scene的semantic writer。真实RenderLoop2986仍会在initial/Whiteboard/PPT placement写fold=false，Main415退出Whiteboard可fold=true；fixture必须等initialBottomDockPlacement/初始layout实际稳定、Whiteboard/PPT=false、displaySerial不变，再开有限测量phase。任何未跟踪renderer业务写、工作区/显示变化使scene UnknownInterference/UnsupportedState，保留分母；不能忽略它们或仅读pulse推定singlewriter。普通raw capture没有这些来源合同则finiteProof=unverified。

1. Main分支在TryBeginToggle/第一项pulse或fold写入前创建Mutation；即使toggle拒绝也以RejectedByBusiness结束。Draw分支在明确Up、temp.preset==Draw的真实业务接受块开始Mutation，包住ClosePenTypeMenu、真实clickFunc与其调用链；Draw callback在真正TryBeginToggle成功处MarkBusinessAccepted。非Pen→Pen切模式不属于这批，记录unsupported/rejected，真实业务仍沿原函数，不能用probe代替它。
2. Begin先odd fetch_add(acq_rel)，**紧接release fence，然后才允许第一业务写及atomic payload写**；Mutation在thread-local slot（该phase唯一Interaction线程）保留到原UpdateRendering。Main的pulse也在奇数区内。不能仅凭odd RMW的release语义代替后续字段写前的fence；这个顺序与项目已有seqlock发布合同一致。
3. 原UpdateRendering保持StateUpdate/ThicknessDisplayUpdate原位置。在两者结束后、原Notify/Request之前调用FinishAtRenderRequest：按最终实际业务值与同一versioned tool snapshot填accepted signature、step/source ID、唯一semantic revision，最后发布偶数serial。这样PresetHoming对drawAttribute的合法规范化也包含在接受事务内；原Wake/Request没有移动或新增。
4. 无updateState分支也在原Wake之前结束对应Mutation；普通UpdateRendering没有activeMutation时不碰publication。新request ledger与“最后accepted semantic goal”分开：确认没发生业务写的rejected request仅结束自己的ledger，旧goal的step/revision/signature保持；serial虽换成新偶数，旧goal不因此Superseded。Begin后未知出口/已经半写/无法证明无写则记AmbiguousPublication并使该phase旧candidate不可确认，绝不能按最终字段偶然一样追认。guard destructor默认Unknown，只有实际拒绝点明确NoBusinessWrite witness才恢复旧goal。无新增Request/忙等。
5. publication payload全部atomic<uint64_t>/atomic<uint32_t>或atomic word数组，不能seqlock并发读写普通struct。writer **odd(acq_rel)→release fence→业务atomic写/字段relaxed→even release**；末尾可保留release fence但不能替代前置fence。reader首serial acquire→atomic字段copy→acquire fence→复核serial，同一偶数才返回。读到新字段的原子read需与写侧前置release fence对应，再以读侧acquire fence约束serial复核；不以实机“看起来不混”代替内存序证明。TryRead只一次，奇数/不一致返回unverified，不等待业务/GPU/磁盘。
6. RenderFrame在Wake/Submit前锁存stable accepted。在真实Submit逻辑作出目标后读取实际业务signature并校验原serial仍同偶数，且与accepted签名匹配；root/drawBatchRevision只在原timeline Restart/实际target签名改变时旁挂增加，不按每callback计。MarkConsumed读取的是这次真实target，不是假测试重建layout。
7. 原Advance完成后核稳定publication和关联pending，候选带已生成epoch/surface/dims/target。Complete附近的StillSameSemanticGoal仅作稳定goal校验：同semantic revision+完整signature+无UnknownInterference；不重读业务倒填candidate。单纯NoBusinessWrite rejection改变serial时允许这个已消费的旧goal继续确认。真正新revision/签名变化才Superseded；odd/混合/未知写仅Ambiguous，不臆造成功。若该唯一成功帧恰落odd窗口且没有自然后继，保留unverified，不为计量Request补帧。
8. actualbusiness接受且signature未改变时可AcceptedNoChange并引用reusedRevision：旧goal已成功则timingUnavailable/无新commit；旧goal还pending则列“no-change引用pending”，不能冒称已呈现或0ms完成。无业务写的toggle/coalescer拒绝仍RejectedByBusiness。真正改变后的规范化异常/未知出口拒确认，所有rows/总计保留。

pause测试分别在odd后前置fence前/后、业务半写、payload半写、even前、Finish后原Request前；读取只能完整旧/新或unverified。另外验证旧goal正在动画时新toggle无写拒绝，不使旧goalSuperseded；no-change旧goalpending/committed两种结果分别保留。真实ARM64竞争压力是补充，静态核fence/atomic字段/owner证明必需。实际未知renderer写、signature缺required位、mixed数据拒确认且计入ledger；不增加补渲染请求。

### 4. U02：两个scene的有限settled角色

不新增全animation registry、不二次扫描整个图。复用现Advance循环，在本来读取/推进的属性旁挂role，将activeAfterAdvance/IsSame数字汇入一个固定pending mask；同帧末再检查真实timeline与physics。若只有active-before数据，它最后一帧保守pending，多等原自然下一帧可以，但不为了观测Request；必须尽量使用真实advance后态。

| Role | MainFold | DrawAttribute |
| --- | --- | --- |
| MainRootGeometry | MainBar x/y/w/h/rw/rh/ft/enable/pct，mainBarTimeline；已visible按钮/分隔线实际geometry+visibility（原button loop） | 主栏根作为属性锚点，仅确实发生相关layout改变者 |
| MainClickPulse | MainButton w/h/n、logo1/logoInk w/h/content transition；原pulse批次settled | 不受不相关新Main pulse影响；若发生则scene被新request取代 |
| DrawRootGeometry | fold引起DrawAttributeBar的实际退出、drawAttributeTimeline | DrawAttributeBar/root与其children geometry/visibility、drawAttributeTimeline |
| AttributePreview | fold合拢真实popup/slider/fine/color/pen menu等已有progress，不人为删动画 | pen thickness/previewMorph/preset number及extension visibility、previewpopup/menu/slider/color关闭/默认预览progress，按本次实际target可见性 |
| Dock/Display | 自动居中触发的bottomDockCenterSpring/phase/recovery、bottomDockSpring、displayTransitionActive，mainButton x/y/displayCenterX/Y | 若实际布局引发同样移动则计；冻结环境变化的另报 |
| SvgSemantic | 本域当前可见SVG的enable/pct/geometry/color1/2/angle/svg.IsSame/contentTransition稳定 | 属性可见SVG及主栏Draw icon的真实语义和颜色稳定 |
| Feedback/Lighting | hover fill/pct、mouse/primary持续光源不计布局settled；有记录但不能使布局永不完成 | 同样排除无关hover/light；Up后的真实pressed release可单列Feedback |
| 未开放gesture | 所有source不触发FineDial拖动/hold/inertia、color拖动、eraser、更多/PPT | 任一thicknessFineDialPhysicsActive/Dragging、colorPointerCapture、toolMode变化意外出现→UnsupportedState/unverified，不把其pending筛掉 |

具体preview成员已在BarRenderLoopState:643–731：drawAttributeLaserShellProgress、PenThickness/PenPreviewMorph、ThicknessPresetNumberProgress、PenTypeExtensionProgress[3]、ThicknessSliderProgress、FineDialProgress/Dwell/Selection、PreviewPopupProgress/Retarget、NumberInside、HoldExchange、AnnotationPopup、OverflowPopup/Badge、PenTypeMenu、ColorPickerProgress。首次scope闭合/默认预览的相关值必须IsSame；不能只看drawAttributeTimeline。

generic map loop只为固定识别的Main/Draw域的Geometry/Visibility/Content/必要语义Color角色归因。hover hit shapes/反馈色明确Feedback，frameLightPct归Lighting。不按dirtyKey指针或“凡不是IsSame”粗略阻塞所有域。button loop复用temp注册稳定tag，不为动画建新map。contentTransition需一个只读IsContentTransitionActive accessor，不能直接跨锁读protected payload。

Settled只有pending=0+signature match，尚未完成像素证明；后面的SVG proof和真实ULW commit仍为必需。其它resource family（path/PNG/text/lighting）没有完整计量，本批完成结论限定LayoutAndSvgProof；不得写完整UI视觉资源/全场景通过。

### 5. U01/U04：首个SVG family DTO与producer

只统计SVG（包括svgMap与registered button SVG icon）；不同时做全部mask/path/brush families。

    enum class Ui3SvgFailure : uint32_t { None, Parse, Raster, Upload, DrawRejected, ProofUnknown };
    enum class Ui3SvgUse : uint32_t {
      DrawnVerified, HiddenExpected, RetainedVerified, QualityFallback,
      Missing, SemanticMismatch, EpochMismatch, SizeMismatch, Unverified
    };
    struct Ui3SvgBitmapProof {
      uint32_t tag, flags, colorMask, color1Rgb, color2Rgb, pixelWidth, pixelHeight;
      uint64_t valueRevision, epoch, surfaceSerial;
      uint64_t requestedWBits, requestedHBits;
      bool ready, semanticKnown;
    };
    struct Ui3SvgDrawObservation {
      Ui3SvgBitmapProof used;
      Ui3SvgUse use;
      Ui3SvgFailure failure;
      uint64_t frameAttemptSerial;
      uint32_t destBits[4], transformBits[6];
      uint32_t finalOpacityBits, windowPresentationAlpha;
      uint32_t effectiveClipBits[4], expectedVisibleBoundsBits[4];
      uint64_t bufferMutationSerial, targetInvalidationSerial;
      uint32_t coverage;             // Unknown / FullVisibleCoverage / Partial / Empty
      bool expectedVisible, qualityMatches, clearCoversOldBounds;
    };

coverage固定0 Unknown、1 FullVisibleCoverage、2 Partial、3 Empty；opacity/rect/transform字段均float32的uint32 bits，numericRootActor核finite/alpha[0,1]、windowAlpha[0,255]与target rect范围。bitmap宽高/bytes乘法用checked uint64；计数溢出标invalid不绕回ready=0。tag space每个枚举/ordinal范围先核，不能用超大tag索引256表。普通DTO bool只内部使用，不进入128/64B IPC packet；离线有显式schema，不能二进制dump C++padding。
    struct Ui3SvgCounters {
      uint64_t lookupHit, lookupMiss, createAttempt, createSuccess, createFailure;
      uint64_t parseCalls, parseFailure, rasterCalls, rasterFailure;
      uint64_t uploadCalls, uploadFailure, drawSubmit, drawRejected;
      uint64_t invalidation, replacement, capacityEvict;
      uint64_t readyEntries, logicalReadyBytes, unknownReadyEntries;
      double parseMs, rasterAndInternalGeometryMs, uploadMs, drawSubmitMs;
    };

capacityEvict是这个family实际不适用（per-object只有一张当前bitmap），报N/A及理由，不把ResetCache/成功替换叫capacity eviction。readyEntries/logicalBytes与真实cacheBitmap创建/Reset同步；SVG首family不声称整个renderer缓存/VRAM容量已经测全。

方法提案：
- SvgObservationScope(Ui3SvgCounters*, Ui3FiniteObserver*, bool clocksEnabled)，只由当前init/render owner设置TLS，退出恢复前scope。
- BarUiSVGClass::BindObservationTag(uint32_t tag) noexcept；在auth init完成图/preset后、Rendering前赋tag。冻结不交叉tag空间：svgMap为0x10000+生产enum无符号值，registered button SVG为0x20000+实际注册稳定ordinal（同一对象重复引用只绑定一次），上限256对象。只有本owned init生产方法确实完成该对象的value构建、没有bitmap、已知owner时，Bind建立valueRevision=1/semanticKnown=true；它只是首次已知初始化基线，不追认旧cache。已有未知bitmap或未受控value写则semanticKnown=false。后续观察到的实际value replacement再递增，溢出使proof unknown，不复用0。
- BarUiSVGClass::NotifyObservedContentValueWrite() noexcept；在真实value replacement后递增per-object valueRevision，marker default无observer不工作。ApplyContentDirect、AdvanceContentTransition真实contentCommitted、普通ChangeString.ApplyTar后分别接线；SetTar只改变target不是value，ResetCache另记invalidated。protected content timeline加只读active方法。
- BarUiSVGClass::ObservedBitmapProof() const noexcept；只在已知owner+scope读。先前未观察/外部未跟踪写入的cache是semanticKnown=false，不能给ready证明。
- BarUIRendering::Svg使用原实际needUpdate/CacheBitmap结果和实际DrawBitmap包围点调用ObserveSvg；不重写needUpdate、不修改失败处理。

production CacheBitmap在原取得svg.GetVal/当前color/目标尺寸时锁存valueRevision与数值意图；所有本scene SVG value写由已知init/render owner执行。真正成功的newBitmap+cW/cH/cColor1/2提交之后才发布ready proof；parse/raster/upload任一失败不改成ready。epochs来自实际BarUIRendering.deviceGeneration/current frame scope，非CPU架构猜值。value revision与content IsSame/timeline必须一致，任何未知写入或owner冲突记ProofUnknown。

计量边界：
- Bar.UI.cpp::CacheBitmap loadFromData一次parse；CalcWH/AdvanceContentTransition的loadFromData在同init/render scope分别计parse。utf16/color准备可选单列，不把它猜成几何。
- renderToBitmap计rasterAndInternalGeometry，不拆lunasvg内部tess/geometry，不改第三方。
- D2D CreateBitmap一次upload；原newBitmap/cW/cColor成功之后createSuccess/replacement/ready bytes。
- Rendering::Svg实际needUpdate为miss；有效ready且无需update为hit；失败旧图继续仅QualityFallback，不能归hit/create。
- 最终实际DrawBitmap计drawSubmit（CPU submission wall），不是GPU raster执行。计时在对象/API边界，无每Bezier片段或mask slice时钟。
- ResetCache只有真实有bitmap时减ready/bytes并记invalidation，理由Content/Epoch/Other；未来metadata不改变原资源生命周期。invalidated perobject proof为not ready；未知析构后统计不可逆读取标unknown，不能负计数。
- 初始化scope在当前初始化线程、Render scope在Scheduler；跨bootstrap handoff经Rendering/线程启动 happens-before，不用所有资源线程共享全局map。init parse summary单列，不塞第一callback。

#### 必须闭合的“实际bitmap使用”证明

coordinator candidate要求的SVG名单为固定family tags（上限256）；只在当前域可能可见/实际被其target改变的SVG列required。required count与每tag outcomes都保留，不能按成功图筛名单。最小raw每frame仅required/verified/failed/unverified/firstTag/reason计数；内部固定proof/table留owner，target终态记录summary，不把256个大proof塞每callback。

expectedReady包括：valueRevision已到真实target且svg.IsSame、content transition结束、optional colors存在mask与实际RGB一致、实际device epoch、有限requested/raster尺寸质量符合原cache条件、原dest/transform来自本帧真实绘制计算。颜色/content更新失败而return false→Missing/SemanticMismatch；质量刷新失败继续旧图→QualityFallback且本目标qualityMatches=false，不完成。整体ULW成功不改变这些结果。

**首版选择review F03的保守选项2：不产生RetainedVerified，不完成unknown retained/partial paint。** B3计数设计GREEN不等于paint绿色；不能用DrawBitmap调用次数冒充有效clip覆盖。以下metadata为最小可执行记录，不启新dirty/draw/flush：
- 在原target资源真正Recreate/resize/epoch切换时surfaceSerial与targetInvalidationSerial递增，所有旧paint proof unknown。
- 在任何本帧BeginDraw/实际Clear/Draw可能改写backing之前递增bufferMutationSerial并使被触及的旧proof unknown；首版可保守把所有旧proof未知。**failed/deferred事务也可能改写backing**：失败不会恢复“最后成功窗口像素”等于未变target的假设，旧proof继续unknown；EndDraw/ULW后只完整CompleteAttempt成功才提交这次新paint proof。
- 在真实Svg DrawBitmap前后锁存该次实际bitmap value/color/尺寸/epoch、dest、当前有效transform、最终tarPct opacity及window presentation alpha、真正effective clip、expected visible bounds。effective clip来自原PushFrameDirtyClip/已有clip调用栈提供的数值scope；不支持的非矩形/nested clip、缺少clip状态、非finite或不明partial写→coverage Unknown。不能用dirty矩形替代未知真正clip。
- FullVisibleCoverage要求实际本candidate中DrawBitmap的有效clip确实包含这个tag最终预期可见区域（原dest/transform映射并裁到target/viewport的区域）、bitmap proof匹配、opacity/颜色匹配该真实候选，并无之后未知覆盖写。只“API called”或clip碰到一点为Partial/Unverified；geometry/opacity变化不能复用旧paint。
- HiddenExpected首版只认证当前candidate真实Clear覆盖上次已知可见bounds（包括原antialias边界）、本次expected invisible、之后没有不明绘制把旧语义带回，且事务完整成功。没有旧bounds或仅enable=false不能认证hidden；既有hidden proof保留路径本版也unverified。
- frame未重画/partial clip/unknown retained全部Ui3SvgUse::Unverified，保留required/failed/unverified分母。下一自然帧full coverage时可以认证，不强制重绘；一直没有证据则ResourceUnverified/timeout整轮，不挑成功subset。
- 指针只能在本callback内固定candidate队列供owner提交，外部/raw只有tag。256溢出不扩容；所有unknown/failure使用固定reason bit，不新增逐primitive时钟。

以后确需RetainedVerified再单独freeze最小Ui3SvgPaintProof：bitmap typed proof、surface/epoch/dims、dest/有效transform、final opacity/windowAlpha、effectiveClip/verifiedCoverage、visible/hidden状态、bufferMutation与invalidation写版本、后续Clear/Draw触及bounds的完整lineage。不在本批隐式启用。首版有保守unverified也必须准确报告；不能将LayoutAndSvgProofV1命名成全paint/完整UI性能已通过。

未被SVG family覆盖的其它视觉资源不升级结论；独立最终BGRA on/off证明仪器等价，不自动证明两版共同存在的其它视觉缺陷已修。

Accepted step若layout settled但SVG未证实，状态ResourceUnverified、finalCommitTicks不设置；保留ULW成功次数、资源失败/理由与accepted/unfinished分母。沿原自然retry后匹配proof的真成功可完成；不会为采样新增Request。原生产不再出帧时该step到有限截止TimedOut/ResourceUnverified，整轮FAIL/NOT VERIFIED，不能剔除或等下一页误绑。

### 6. U03：exact purpose、packet和CLI

对外有限入口提案（当前不存在）：

    Inkeys.exe --ui3-presentation-benchmark --scene main-fold --round 1 --capture on --capacity 32768
    Inkeys.exe --ui3-presentation-benchmark --scene draw-attribute --round 1 --capture on --capacity 32768
    Inkeys.exe --ui3-presentation-benchmark --scene main-fold --round 1 --capture off --capacity 0

仅scene main-fold/draw-attribute、round1..3、captureon/off；on capacity256..65536且预算校验，off必须0。没有任意脚本/外部文件/任意坐标/任意输出路径参数。parent在本仓忽略TestResults/release-hardening/ui3-finite-<unique>/rN/s<scene>/bin独占新目录复制当前EXE，新config seed保留默认graph/效果/动画质量。

内部模式提案：
--inkeys-internal-ui3-fixture-child-v1 <parentPid> <parentHandle> <ackHandle> <mappingHandle> <privateRoot> <expectedParentImage>
argc固定8（含EXE）。三个HANDLE各唯一、非零、uintptr范围、具有继承标志；parentPID有效且非self、GetProcessId(handle)精确匹配；实际parentimage与expectedParentImage按file identity相等；childimage必须精确等于privateRoot/bin/Inkeys.exe。只复用E02的exactPID/HANDLE/file identity/limited inheritance原语；**新purpose不调用、不放宽旧IsTestDirectory**。旧Shutdown/UEF目录校验继续原命名/层级语义；下面专用validator覆盖新结构。

新增方法提案：

    bool ValidateUi3FixtureOwnedLeaf(const std::wstring& absoluteLeaf,
        const std::wstring& actualParentImage, uint32_t round,
        Ui3FiniteScene scene, Ui3OwnedDirectoryProof& out) noexcept;

absoluteLeaf固定是 <Repository>/TestResults/release-hardening/ui3-finite-<32lowerhex>/r<1..3>/s<1|2>，bin在leaf下。对应CLI scene/packet tuple须逐字/数字匹配。publicparent从GetCurrentDirectory核本仓标记后只create-new这个固定树；不接受任意output/root参数，master已存在一律失败，不能将字符串同prefix当ownership。

专用校验步骤：
1. 正规化为完整绝对路径，拒相对、空段/traversal/额外suffix；按leaf的精确5个固定尾组件拆出repo（TestResults、release-hardening、ui3-finite-32hex、rN、sScene），bin是其下另查的第6层，不能用任意rfind同prefix绕到另一个root。
2. repo本身是普通非reparse目录；repo/InkeysRepo.sln是普通非reparse文件，repo/.trellis是普通非reparse目录。确认repo边界是由这棵尾组件推导的同一根，separator-aware比较，不允许Repo2字符串前缀。parent选择的repo directory/file identity记录于其localcap；child推导结果必须匹配该次拥有的root，不把另一个标记目录当同一个任务。
3. 从repo到leaf/bin的每一层分别核directory/no reparse（含TestResults、release-hardening、master、rN、sScene、bin）；Inkeys.exe是regular/no reparse且当前child image=file identity同一。不能只查最后sScene。private bin/Inkeys、Config、opt、log等seed/output目录由parent新建后也逐层核；配置路径和真实写入路径按cap核到此root。
4. parent创建树使用CreateDirectory(create-new)并保持唯一root记录；身份校验打开相应directory/file HANDLE（目录用既有Win32目录打开方式），不共享DELETE，cap生命周期内保留必要层句柄，降低验证后替换风险。若平台/权限不能持有必要句柄或已有组件不符，fail closed。实际写入前复核即将用的child/config/output文件边界；不宣称这是完全OS沙箱，也不清理未知目录。
5. 创建/检查都不触旧C/E目录validator或放宽其允许前缀。坏round/scene、穿越、reparse任一层、同名不同file、wrongrepo/parent都在config/互斥/HWND前early拒绝。正例新层级必须有实际授权单测，不能沿用旧UEF正例当通过。

首版不加任何第四HANDLE/额外mapping/packet扩展。child从精确leaf尾组件得到repo，actualParentImage由继承parent进程HANDLE查询，必须为这个repo边界内的regular/no-reparse源EXE，且与expectedParentImage exact file identity同一；任一ancestor穿reparse、源EXE在其它repo/Temp/安装目录则拒绝该fixture（不影响普通产品）。repo及源EXE/owned master/bin/currentEXE的directory/file identity分别写入local Ui3OwnedDirectoryProof并保持必要HANDLE，不导出为普通产品配置。publicparent在已验证cwd repo中只使用自己的当前源EXE生成copy并记录同一组identity；parent-only状态和child由实际parent HANDLE推导的root必须一致。未知root/identity只拒绝，不靠字符串追认，也不借机放宽旧C/E校验。

固定128-byte packet建议：

    struct alignas(8) Ui3FixturePacketV1 {
      uint32_t magic, version, bytes, purpose;  // 0x1430FB21,1,128,0x55493301
      uint32_t scene, capture, capacity, round;
      uint32_t sourceVersion, trajectoryCount, authorized, stage;
      uint32_t result, received, enqueued, consumed; // first64bytes
      uint64_t nonceLo, nonceHi, sourceHash, expectedSteps;
      int64_t startedTicks, finishedTicks;
      uint64_t completedSteps, unverifiedSteps;      // last64bytes
    };

compile-time static_assert sizeof==128、align==8、offsetof(nonceLo)==64；没有pointer、HANDLE、bool、enum直接ABI或strings。第一十字段为immutable输入（magic..trajectoryCount），nonce/sourceHash/expectedSteps也在auth时复制本地；authorized/stage/result及统计只child写、parent用Interlocked/明确acquire读取，不并发访问普通可变字段。输入header/auth初态全0只允许授权一次。sourceVersion=1，tableHash为程序内fixed scene table版本一致性（非来源认证），expectedSteps=216，trajectoryCount按编译内表核，reserved/unknown拒绝。

mapping FILE_MAP_WRITE仅映射精确128B；魔数/版本/长度/purpose、有限scene/capture/capacity/round、非零runNonce/正确tableHash/steps/count、initialauthorized0全部核。只有全部通过后创建私有授权对象、写authorized并ack；ack失败直接early reject，不继续任何初始化。parent使用STARTUPINFOEX名单只继承parent/ack/mapping，不全局继承进程所有句柄；actualcommandline参数用既有Quote。

root Main在任何normal路径前调用新early dispatcher，顺序与现TryRunShutdownSupervisorEarly协调。新分支返回NotRecognized/Rejected/ParentFinished/AuthorizedChild；Rejected直接return非0，AuthorizedChild直接return Bar::RunAuthorizedPresentationFixture(capability)，**绝不继续普通wWinMain**。内部word出现但解析失败也fail closed，不落GUI。授权对象只能由helper成功验证构造，持有private paths/scene/frozenlocalpacket；C++类型限制不是OS防攻击沙箱，运行前仍要独立代码safetyreview。

### 7. U03：fixture actual bootstrap（同Bar module，唯一实现）

新增一个Bar.Presentation.Test.cpp（提案）module Inkeys.UI.Bar/import :Main。对外只RunAuthorizedPresentationFixture(const Ui3FixtureAuthorization&)；内部直接InitializeWindow/InitializeUI，不公开它们、不调用完整Initialization。root拥有Main/Helper/项目登记，Bar worker只有冻结授权的Bar文件。

完整实际前置：
1. auth→private globalPath/bin尾separator、hInstance、private user/config metadata、offSignal0；in-memory setlist/config合法默认。前置根先查是本capability目录。config.GetFilePath/opt路径分别解析并验证仍在private bin；图Load规范化写允许且日志记其relative路径，不能读真实用户main/deploy/UInk。
2. CPU渲染观察buffer/source/family/finite slots预分配；ConfigureRawCapture在RenderPipeline::Initialize前。captureoff不分配R数组；source有限身份/终态台账仍保留以做等价。
3. actualEnsureProcessDpiAwareness、CoInitializeEx、Display::Initialize、RenderPipeline::Initialize、IdtFontFileLoader/CollectionLoader+TTF1/7/3/8、I18n::load内嵌zh-CN。logger必要时private根；不启动StartupPreview、SuperTop、registry、自启、shortcut/DDB、更新/PPT/Office/TopWindow/globalhook。
4. WindowService仅真实Bar role，visible=false，生产WS_POPUP/CLIPCHILDREN和LAYERED/NOACTIVATE/TOOLWINDOW，WindowProc=Bar::WindowProc；messageCallback=private dispatcher wrapper。source/latch授权对象和immutable table必须在callback可能调用前安装；floating_window从created/Service.Handle(Bar)发布，OwnerThreadId/actualPID准确绑定前所有index输入拒绝。其它role不存在不称全产品startup通过。
5. InitializeWindow→ConfigureLocalizedTypography→InitializeUI→StartDisplayTracking→LoadFormat→PresetInitialization/RegisterBuiltInComponents/Load/StateUpdate→SetContentStateUpdatesReady(true)→PositionUpdate→Bind SVG tags/sourcecap→Rendering。保持所有默认图/图标/光影；builtin callback不执行外部动作。
6. WindowService已经只拥有private Bar角色后、PresetInitialization/StateUpdate及Rendering之前，通过真实ChangeStateModeToPen设initialbaseline。独立review已核这条链在ProductRunning=false时Reconcile立即Waiting，未见Host/Office/自启/I/O/新业务线程；真实Setting owner命令和bridge发布仍需在实码核目标只指向自有/不存在role。保持ProductRunning=false/NotReady guard，不为fixture启动Host或开放白板。privateconfig seed及config.GetFilePath/opt边界仍需实码核，不用合法Pen链审查代替文件证明。
7. 实际Interact线程发布自己的threadId后调用barUISet.Interact；在真实BarInteractionSession::Run入口新增auth-only纯值NotifyFixtureInteractionReady，不能依赖没有startup tracker时Startup::Report的返回值。等待下面独立latch的interactionReady、第一次真实完整事务和initialLayoutStable，再进入setup/warm/measurement；**capture-off不等B1 stamp**。MouseHook没有调用入口，inputsource只owned private callback；不使用computer-use/SendInput/SetCursorPos。

#### capture-on/off独立latch（首版必需）

在Bar.PresentationProbe.h冻结：

    struct Ui3FixtureReadyValue {
      uint64_t generation, committedCount, lastCommittedAttempt, epoch, surfaceSerial;
      uint64_t mainAnchorBits[2], drawAnchorBits[2], anchorMappingSerial;
      uint32_t targetWidth, targetHeight, flags; // registered/interaction/txn/layoutStable
      Ui3FiniteSignature initialStableSignature;
      bool timingValid;
      int64_t commitTicks;  // valid=false时离线null，绝非0ms
    };
    void NotifyFixtureInteractionReady() noexcept;
    void PublishFixtureBarCommit(const Ui3FiniteCandidate&, bool committed,
        bool timingValid, int64_t commitTicks) noexcept;
    bool TryReadFixtureReady(Ui3FixtureReadyValue&) noexcept;

私有observer/latch只在已auth earlyfixture安装。registered来自真实Register成功，interaction来自实际Run入口；committedCount/attempt/epoch/anchor来自真实CompleteAttempt success分支。publish纯值即使CurrentFrameDiagnostics()==nullptr也生效；off时不调用StampBarCommit、不配置raw、不加计量clock，timingValid=false/time=null，只证明软件事务身份与有限layout/SVG结果。on时沿现B1 hasBarCommitStamp且有效trueTick，不另发明API时刻。

initialLayoutStable是在真实Submit/Advance后所有初始Main/Draw/Dock相关值settled、initial placement已结束、无PPT/Whiteboard/display变化并有成功事务时由owner写的独立flag；不是“window创建了”或第一次ULW回调。captureoff的finite Completed也用纯commit身份，timingUnavailable，不因无B1 timestamp阻塞；resource unknown仍unverified，不升级paint。

latch共享字段为atomic纯数值publication（按第3节前置fence+even），或通过生产observer的同owner snapshot发布；TryRead一次，所有source/Interact/Render真正join前不撤。schedule必要steady clock与阶段/B1 timing分开，不能临时打开raw或clock参数绕默认gate。已无tracker的Startup::Report只保留原行为，不拿其不存在的ready当证据。

### 8. U03：immutable index→own HWND→production queue

提案固定输入DTO每项64B：

    struct alignas(8) Ui3FixtureInputV1 {
      uint32_t index, stepId, phase, anchor; // phase Down1/Move2/Up3/Cancel4
      int32_t offsetXDip, offsetYDip;
      uint32_t flags, reserved0;
      int64_t dueOffsetTicks;
      uint64_t sourceSequence, expectedBaseCommitSerial, reserved1;
    };
    static_assert(sizeof(Ui3FixtureInputV1)==64 && alignof(Ui3FixtureInputV1)==8);
    static_assert(offsetof(Ui3FixtureInputV1,dueOffsetTicks)==32);
    static_assert(offsetof(Ui3FixtureInputV1,sourceSequence)==40);
    static_assert(offsetof(Ui3FixtureInputV1,expectedBaseCommitSerial)==48);
    static_assert(offsetof(Ui3FixtureInputV1,reserved1)==56);

显式字段已占满64字节，reserved1是最后8字节，不再追加padding字段；按上面的static_assert核三架构布局。table构造后immutable；sourcepayload仅有限MainGrip/DrawButton anchor、零modifier、无其它button/key/tool/action。flags低3bit固定Ui3FixturePhase（Setup=1/Warmup=2/Measurement=3/Teardown=4），bit3 SetupOnlyIfFolded，bit8–9 expectedAction（MainFold=1/DrawAttribute=2），其它bits必须0；reserved0/reserved1=0。WM_APP+0x4B0为提案private消息（本轮在Bar/Window/Main未发现同常量），wParam为input index，lParam=0，不带pointer。root最终全repo核冲突后冻结。

anchor来自实际CompleteAttempt成功后完整几何快照发布结束的私有numeric latch（MainGrip/Draw preset中心、映射serial），不能读live button/Shape/SVG。**编译内expectedBaseCommitSerial冻结为0**，含义只是在ownedWindow接收Down时绑定当次完整committed latch；不预测运行时successSerial，不把actual serial写回immutable table。source sidecar记录实际boundCommittedCount/attempt/anchorMappingSerial/epoch/surface；Up/Cancel引用同contact的Down屏幕点与绑定proof，即使窗口已动画也不改它。若将来传非0，必须是明确定义的新source版本；首版拒非0。Down时latch奇数/不稳定、epoch失效、anchor无效/过期均SourceRejected保留分母，不以OS光标或最新live geometry补救。

**Draw scene的setup phase明确批准真实Main action**：
- MainFold scene无额外setup input，先等independent initialStable再warm/measure Main。
- DrawAttribute scene包含固定预留Setup Main Down/Up rows，expectedAction=MainFold、anchor=MainGrip、SetupOnlyIfFolded=1，setup stepId用phase命名空间与warm/measured分开。如果initialStable证明已展开，owner将这两行记SkippedAlreadyOpen、未发送/未消费计数为0并按协议推进reserved index，不伪造Main tap；若folded则正常真实Seek/Toggle/accepted Main目标完成后再开Warmup。
- Setup允许的只有该finite Main action（相同actual action gate）；DrawAttribute的Warmup/Measurement只允许Draw preset。Setup不得直接写fold/drawAttribute，不可绕动作allowlist。若真实Main setup变成close、没有完成/资源unverified或出现其它action，run失败/未验证，不能带错误baseline进入统计。
- 216为16warm+200measured目标数，不含setup；Main input table433行（432实际计划Down/Up+1保留Cancel），Draw435行（另加2预留setup），1024上限不变。编译内tableHash/count/purpose核相应scene，不把保留/跳过row计成执行成功输入。

privateWindow messageCallback：
- 核自己的HWND/PID/ownerThreadId、activeauth、index==expectedIndex、固定phase/order/sourceSequence且reserved为0。
- 用真正screen point构造与WM_TOUCH相同的ExMessage、SetKeyBoardDown(VK_LBUTTON)过程、MarkBarTouchPointerMessage(cancel?,screen=true)、Window::Enqueue；Enqueue false记录SourceRejected并fail整轮，不改变样本丢弃策略。
- 返回Discard/Consumed，不让privateWM_APP再进入HiMsg，不调用普通QueueWindowMessageInLayoutSpace/GetMessagePos，也不经过WM_MOUSEMOVE的RawInput注册。其它普通OS coordinate/input消息在这一个auth hiddenfixture记录UnexpectedSource并使run invalid，避免和专用序列混杂；不影响正常产品WndProc。
- 固定source sidecar在Enqueue前发布对应expected ExMessage数字指纹/sequence，消费的公共WaitForBarInteractionMessage/TryGet helpers在Prepare前匹配下一expected原screen-tagged消息，更新Interaction TLS当前event/contact。只有one producer，禁止取Window latestUp倒填Interaction正在处理的Down。
- existingClear可能删除消息：source按实际consumed ack顺序送下一条，允许Down后必须的Up但不提前排下一gesture；若source行未consumed却被Clear删除，记录Dropped/SourceRejected而非重发掩盖。startupOS/非source消息标invalid。nestedWait也用同一个真实dequeue helper，不能只在顶层Poll计。
- 限制不能只靠发送端坐标：实际Main toggle/Draw callback/其它temp->clickFunc执行前，加auth-only action gate核当前consumed contact的phase+expectedAction。Main scenewarm/measure只允许Main；Draw sceneSetup仅批准预留Main，warm/measure仅Draw；Teardown只允许对应Cancel/正式Close。误命中其它按钮/key/rightclick在外部callback前拒绝，SourceRejected并正常Cancel/关闭。NotInstalled原业务照常。gate不替代Hit/Seek或设置目标；screen坐标先核short范围，不clamp成其它按钮点击。
- parent只负责exactchild生命周期；child runner按编译内相对时间向自己的HWND发送数字index。建议step固定1000ms，Up+20ms、16warm+200measured，短轮process budget300s（含退出15s）；actualsend jitter记录。更长动态动画未在周期内完成会Superseded/unfinished，保留分母，不为了完成把输入间隔悄悄改大。采样schedule固定Release轮间相同。

方法signature提案：

    bool InstallAuthorizedFixtureSource(const Ui3FixtureAuthorization&,
        std::span<const Ui3FixtureInputV1> immutableTable) noexcept;
    Message::Reply DispatchAuthorizedFixtureIndex(HWND, UINT, WPARAM, LPARAM) noexcept;
    bool ObserveAuthorizedFixtureDequeue(const ExMessage&) noexcept; // before Prepare
    FixturePointResult ReadFixturePointerForCurrentOwner(POINT&, bool& leftDown) noexcept;
    FixtureActionResult PermitAuthorizedFixtureAction(Ui3FiniteScene actualAction) noexcept;
    void StopAuthorizedFixturePublication() noexcept;
    void RemoveAuthorizedFixtureSourceAfterJoin() noexcept;

FixturePointResult=NotInstalled/Available/Unavailable；Unavailable在authfixture内返回失败，不偷偷fallback OS。NotInstalled才调用原系统。Installation仅beforeBarclient/inputowner启动，Remove必须source/Interact/Render真正join后，不能仅offSignal或running=false。Window owner读自己已enqueue的snapshot（给timer），Interaction读自己刚consumed/currentcontact TLS（nestedSeek）而非跨线程最新；render不读mutable source指针，消费固定numeric可验证snapshot。

### 9. 两scene逐读取点接线（不是“概念provider足够”）

| 实际读取入口 | MainFold | DrawAttribute | auth source接线/未覆盖 |
| --- | --- | --- | --- |
| QueueWindowMessageInLayoutSpace/GetMessagePos6790 | 不调用其coord path | 同样 | privateWM_APP callback构造原touch-tagged ExMessage；普通产品原函数不改。 |
| Seek6070/6220/6630/6648 | Touch-tag进入原路径，按当前consumed contact屏幕点/left state | Draw按钮不走Main Seek | mouse GetAsyncKeyState/GetCursorPos分支不冒称覆盖；auth helper只能提供source自己的touch point，不能强迫原touch改走mouse。 |
| final indicator veto6732 | 将该GetCursorPos接ReadBarPointerPosition，auth读当前Up/Cancel点，保留原BarScreenToLayout/veto | 不触发 | NotInstalled→原GetCursorPos；不能删veto或固定返回false。 |
| SuppressHover1416 | auth Interaction latestconsumed Up点 | 同样 | 保留原hover suppression规则，source读当前event。 |
| CommonHover2381 | 如source有Move，读该Move的consumed point | 同样 | core仅Down/Up；seam仍覆盖真实common helper。不能用Window最新位置消掉旧Move。 |
| WM_TIMER552/合成Up437/455 | 可读authWindow snapshot；合法cancel用activecontact点 | 同样 | core不触发tooltip，意外timer/自动cancel仍有计数；未匹配activecontact时Unavailable/run invalid，不触碰OS。 |
| FineDial4293/颜色4698 | 不可达 | 不可达 | 本批没有此gesture/phase；若触发停止scene并记UnsupportedState；没有用已覆盖宣称完成。 |
| cursor light5711/5774/5897 | 没有实际mouse/raw消息 | 同样 | source是Touch，生产本就不等于mouse light。保持Edge/Dynamic配置开启，primary light照常；cursorLightActivated=false，MouseLightScene未覆盖。不能假造RawInputRegistered=true、改Snapshot强亮或关闭动态光。 |
| RegisterRawInput5630/5667、WM_INPUT、WindowFromPoint/GetForegroundWindow6820 | 不应被此table调用 | 同样 | callback禁止ordinarymouse/key输入，只touch queue；若实际执行登记/外部焦点依赖即run contaminated并保留证据，不记mouse/UI完整PASS。 |
| globalMouseHook Initialization171 | 不调用完整Initialization | 同样 | early bootstrap没有hook；正常产品Initialization原样。 |
| SetCapture666/685 | maintouchSeek不应打开color/slider capture | 同样 | 本批无capturegesture；意外调用非批准目标判source scope失败，不替换为假capture成功。 |

最低新增ReadBarPointerPosition函数只替换上表确实会被core/取消路径调用的GetCursorPos点，保持返回值和原分支；它有auth-only有限来源，普通产品仍OS。未替换/没有来源的future scene显式未验证，不使finite helper成为通用全局光标模拟。

### 10. Stop、封口、离线输出与验证

source table末尾若仍activecontact，先送对应Teardown Cancel并等实际consumed；无activecontact不伪造Down/Up。停止新publication→正式Close原SetOffSignal/15s监督→Interact真正join→Display stop→同步Unregister→WindowService与RenderPipeline真join→Take schema2 raw一次（off无raw，合法absent）→finiteTargets/source/SVG和private latch纯值封口→离线输出→撤probe/table。off的时间列null/unavailable，证明来自私有purevalue commit/interaction/geometry，不凭B1 stamp/Poll返回。未join前不撤backing，任何timeout/被parentkill保持FAIL/unsealed。

resource/target当前owner数据在真正Unregister/停止期间纯数值封口，不加磁盘等待。ownparent持exact child hProcess，300s总budget/15s关机单列，超时仅cleanup该HANDLE；parent杀死不是naturalpass。child不自然封口时不写成功summary，不读半成品。后续Host/Window停止合同引用其worker最终方案，本文不改其文件。

输出唯一private目录：meta.json、raw-callbacks.csv、raw-batches.csv、finite-targets.csv、source-events.csv、svg-cold.json、svg-warm.json、summary.json、独立equivalence.png/BGRAhash。schema2整数tick不经double丢精度。每request有accepted/rejected/sourceFailed/superseded/resourceUnverified/completed/timeout之一，seen=retained+dropped；成功分位数和全请求结局分母同时报告。

Required red→green（以下对应新代码须由root实现登记；本研究没有执行）：
- actualMain branch在odd/fence/业务半写/payload半写/even前/原Request前暂停，真实helper读旧/新完整态或unverified；Draw真实callback+PresetHoming、NoBusinessWrite rejected保留旧goal、Unknown reject不确认、no-change引用pending/committed区分、快速反向/late Restart、最终settled失败→自然真成功、epoch/idle、持续hover/light。
- 实际SVG first/reuse/content/color/size/epoch与parse/raster/upload受限失败；成功ULW missing不Completed；opacity/clip变化、空clip/partial clip、failed/deferred backing写、Hidden清理、Bind-after-owned-init/未知旧bitmap、tag冲突/256cap。首版retained只能unverified；BGRA只独立equivalence。
- exactpurpose/packet错版本/magic/size/scene/capacity/parent/继承/root/EXE/file identity、每层reparse/错repo/rN/sScene/parent source外根负例earlyreject；新目录合法正例独立通过且旧Shutdown/UEF正负例不变；core无hook/Office/更新/系统设置，privateConfig.Write路径实码可核。
- actualowned HWND→screen-tagged ExMessage→Window queue→nested/top Interaction→accepted→realcandidate→B1 truecommit；Wrongsequence/Enqueue false/消息clear/Unavailablepoint/DroppedSource按FAIL。
- captureoff/noStartupTracker：无raw配置、无B1stamp，真实Run/Complete/完整geometry纯latch仍能ready/completed identity，timing全null；sourceSerial0绑定当次actualanchor、Up旧Down点、非0拒绝、Draw setup已展开跳过/折叠真实Main action和warmDraw严格allowlist。on/off的tool/color/width/fold/panel/输入/业务数与最终BGRA一致，不拿source Poll作commit。
- 构建/测试root串行完整InkeysRepo.sln Debug|ARM64、strictheadless和真实HWND分开；Release|ARM64适用三架构。仅docs无本轮build。
- 先单scene safety/actualdiff独立CLEAR，再两个scene每scene fresh三轮Release、16warm+200变化、原始样本/median/P95/50ms长帧/truecommit gap/完成wall/CPU+资源；不足1000有效样本P99=null。收集三轮而非最好轮，停build/扫描/其它bench。
- Hardware/光学输入、鼠标光、Settings、真实笔、其余families、Win7 FL11/WARP/FLIP及HC/H2保持未验证。fixedTouch software结果不能外推。

### 11. 最小writer拆分/依赖和待审决定

| 阶段 | 唯一写入者/文件 | 必需先冻结 |
| --- | --- | --- |
| B2数值与pub | Bar.PresentationProbe.h/最小同module实现；Bar owner写Main.cppm/Main.cpp、Interaction.cpp、**Bar.Button.cpp**、RenderLoop | 前置release fence+atomic payload、request ledger与旧semantic goal、初始稳定phase、有限roles；B1不重做。 |
| B3首SVG | 同Bar owner写UI.cppm/UI.cpp/Rendering.cpp(m)、必要RenderLoop当前scope/tag/paintproof | 单family计数GREEN；首版只full coverage/当前实际hidden clear认证，retained/partial/unknown保守unverified；不扩全部cache。 |
| F主入口/权限 | root独占IdtMain/Helper/项目；Bar owner一个Presentation.Test.cpp | exact128packet/argc8/三HANDLE/earlyreturn/source table、private configs、allowed business baseline。 |
| F输入/生命周期 | root冻结seam；Bar ownerInteraction、fixture纯numeric源 | currentconsumed与Window snapshot区别、不可覆盖点failclosed、stop后真实join撤probe。 |
| check/run | independentchecker看冻结实际diff；root独占build/run/report | B2/B3/F分别review，F运行前CLEAR，先单scene再三轮。 |

不需再问不可逆产品取舍。此版冻结常规落点Bar.PresentationProbe.h/同modulefixture、128/64B布局、独立UI3目录validator、private on/off latch、serial0动态anchor、Draw setup phase/allowlist、首版保守paint策略；Pen调用链已有review。仍需实码独立核privateconfig seed/GetFilePath、directory HANDLE/fileidentity、module登记、pointer源/ready/stop完整调用链与首版实际coverage。它们是后续实现/check门，不能因文档已修订自动PASS；Win7人工不是这些工程工作的前置。

### 12. R1–R8 修订映射与可接续测试入口

| 修订 | 对应review | 本版闭合点 | 生产实现后的可执行验证 |
| --- | --- | --- | --- |
| R1 | F01 | odd后业务/payload前release fence；atomic字段+一次reader复核 | strict headless中生产publication pause：odd/fence/业务/payload/even五站点与真实ARM64压力；核实际fence源码。 |
| R2 | F02 | request ledger独立、NoBusinessWrite rejection保旧goal；未知出口/初始renderer写拒确认 | 生产observer rejected/no-change/pending/committed、missing validMask、Main initial placement/PPT/Whiteboard/Display反例；所有rows保留。 |
| R3 | F03 | opacity/effectiveclip/currentcandidate coverage、backing partial/fail invalidation；首版retained Unknown | 真Svg/实际Clear/PushClip/DrawBitmap/Complete offscreen合同：full/partial/emptyclip、opacity、failure/defer后旧proof、hiddenclear；不叫完整ULW。 |
| R4 | F04 | 独立UI3树validator；旧IsTestDirectory不放宽 | 新purpose auth suite合法层级及错root/parent/reparse逐层/Scene tuple负例，旧C/E suites回归。 |
| R5 | F05 | auth-only interaction/committed/anchor纯latch，off无B1与null时间 | 无startuptracker/cap0真实Bar fixture就绪/关闭；raw absent合法，计量clock gate实际diff；on/off相同最终BGRA/业务。 |
| R6 | F06 | immutable serial0绑定实际sidecar、Up沿Down；Draw Setup只批准Main、measure只Draw | source/helper index/phase/anchor0/过期/非0、SetupSkipped/真实Main/错误action前拒绝；实际own HWND一scene。 |
| R7 | F02/F03 | Bar内部Probe.h、typed validmask/tag初始绑定/预算 | 完整Solution编译及sizeof/offset/scope/no-defaultclock；Tag namespace/coldknown/旧cacheunknown/总预算上限。 |
| R8 | 生命周期/边界 | captureoffTake absent、真join后撤probe、保留其它scene/family/Win7门 | Stop/Cancel/Interact/Unregister/Window/Scheduler真实次序与privatechild超时；仅本两scene三轮统计，其余继续。 |

现有可用入口只可在root新实码登记/完整build后复用：InkeysHeadlessTests.exe --no-window（新增numeric helper断言登记于现严格no-window入口，不复制算法）；Inkeys.exe --bar-eraser-offscreen-test可承载已有真实SVG/D2D资源断言，不能声称真主栏ULW；现ShutdownSupervisor/failed-cleanup CLI作为旧purpose回归，参数按当前代码存在值由root核。

新提案的 Inkeys.exe --ui3-fixture-auth-tests（负例/合法目录）与第6节--ui3-presentation-benchmark两个scene命令，**当前不存在，未执行**；source/helper/no-tracker/raw-off/stop cases登记由root决定现测试入口，但必须触新生产实现而非在test复制正确算法。F实际diff运行前需独立CLEAR，再一owned scene，最后两个scene×三Release轮。最终raw/finite/SVG/on-off证据+HF fingerprint才计进度；缺coverage只能准确unverified。

## Related Specs / External References

已读activePRD/design/implement、workflow与前份research、独立designreview U01–U04、父handoff/performance，并核实际Bar/Main/Helper/Window/Config。适用native-desktop rendering-and-ui、ui3-render-diagnostics、input-and-ink、errors/resources/build/cpp与reuse指南；研究角色不读implement/check JSONL，不改spec。

无新增外部网页/API支持声称；Win7用户FLIP约束、DComp/ULW与两DWM禁用保持，不加高版本API/最低OS/第三方修改。B1源码GREEN不自动给新增B2/B3/F编译或运行PASS。

## Caveats / Not Found

- 本文全为待实现接口/运行形状，不能提前记来源安全、完整目标或性能通过。
- DrawAttribute接受点需要Bar.Button.cpp额外明确owner；只有Interaction/Main pulse计数不闭合U02。
- C++ seqlock普通payload有UB；必须atomic数值payload和一次有限读取，不忙等。
- realcacheReady不等于目标surface已经包含该bitmap；retained/hidden lineage不能省略，无法证明时ResourceUnverified保留分母。
- Touch source不激活mouse light；本批只两个scene，其它工程验收仍继续。
- 本轮只写本research；未操作Git、源码/spec/项目/taskmeta或运行验证。
