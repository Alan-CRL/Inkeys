# Research: UI3 U04-B/F 真实 Bar 成功呈现与隔离采样合同

- Query: U04-R 已完成后，怎样以最小增量取得真实 Bar ULW 事务、布局/动画完成、资源成本和可重复场景证据，且避开普通启动的全局副作用？
- Scope: internal；实际生产代码、E02/U04-R 独立复审、父任务冻结性能门槛；无新增外部设备结论。
- Date: 2026-09-30
- Active task: .trellis/tasks/09-27-integration-and-release-check
- Owner: research-only；只写本文件。未运行 Git、构建、测试、GUI 或基准，未改源码/规范/工程。

## Findings

### 1. 下一实施单元与不变合同

U04-R 的固定容量 Scheduler recorder 可复用，其成功时间仍是 callback-end proxy。U04-B 在真实 BarRenderLoopCoordinator 补真事务提交、职责阶段、有限目标完成和实际资源计数；U04-F 在已鉴权私有 copied child 中直接组装生产 Bar 初始化子函数、Rendering、Interact。early fixture 直接返回，不调用完整 Bar::Initialization，不进入普通 wWinMain 后续启动。root 同意该方向，并要求 producer/timer 接口分别冻结后实施。

默认关闭：只复制固定数值、不持有 COM/业务对象，不增加 allocation、I/O、逐帧日志、GPU flush、渲染请求或关闭等待。FrameStageTimer(nullptr) 不读时钟；target/resource/source probe 只在显式有效 capture/fixture 下存在。保留原 Tick/duration/dt clamp/idle/pacing、dirty/present/retry、cache key/budget/失效、分辨率和效果；新增观测本身不称优化收益。

### 2. Files Found / Code Patterns

| 文件与实际符号/行 | 职责与事实 |
| --- | --- |
| Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cppm:91、115、166、201 | 当前 FrameStage 六项；FrameDiagnostics 纯数值；RawCallbackSample 含 frame；RawCaptureReport schema1、steady_clock ticks/period、固定容量/drop。 |
| RenderPipeline.cpp:279、281、406、771 | TLS、nullptr不读钟timer、真实raw callback记录；callback返回后、activeCallbacks--之前抄样本。旧previousActiveCommitTicks是callback end。 |
| Bar/Bar.RenderLoop.cpp:959、13234 | WakeAndSnapshot→ApplyDisplayTransition→SubmitTargetsAndLayout→AdvanceAnimationsAndDeriveLayout→PrepareLightingAndDemand→CalculateDirtyAndDrawPresent。 |
| 同文件:9202、9880、12557 | EnsureDeviceResources实际调用；BeginDraw前开始Draw timer，12557停止。 |
| 同文件:12623、12695、12701 | GetDC/ULW/ReleaseDC/EndDraw全部结束后调用CompleteAttempt并写presentCommitted；deferred分支12670不确认事务。 |
| 同文件:12717、12905 | 只有committed才发布dirty/eraser/viewport/mapping/窗口几何/startup/debug成功快照。 |
| 同文件:2223、2286、5333、7781 | 主栏真实fold/layout/side批次条件；已有AdvanceAnimation.result.active/changed；三timeline同帧末推进。 |
| Bar/Bar.Main.cpp:209；Bar.WakeSignal.cppm:18 | UpdateRendering通知/Request包含hover、lighting等；demandGeneration不是semantic target revision。 |
| Bar/Bar.Main.cppm:968、983、1026 | barUISet、WindowProc、Rendering/Interact可由导出类/接口使用；InitializeWindow/InitializeUI/SetContentStateUpdatesReady仅module内部声明。 |
| Bar/Bar.Initialization.cpp:121、171、208、232 | 完整Initialization组装图、启动MouseHook、Rendering和Interact；InitializeWindow复用floating_window/Service::SetBounds；InitializeUI真实图/layout/theme/Zoom。 |
| Bar/Bar.Main.cpp:200；Bar.Button.cpp:686、1215、1287 | LoadFormat需共享DWrite；builtin注册创建按钮+外部动作callback；Load读取config且规范化时可能config.Write。 |
| Bar/Bar.Interaction.cpp:395、5432、5595 | 真实Window/HiMsg队列和BarInteractionSession；fixture必须用这些实现，不复制交互算法。 |
| 同文件:6070、6220、6732 | mouse Seek读取GetAsyncKeyState/GetCursorPos；Touch-tagged路线读取屏幕点，但final indicator click veto仍读GetCursorPos。 |
| 同文件:944、5711、5727 | WM_MOUSEMOVE会激活真实cursor/Raw Input。单纯Post鼠标消息不是固定物理轨迹。 |
| Bar/Bar.Rendering.cpp:1469、1767、2080 | rounded/geometry mask已有hit/miss/create/failure/time，exact已有hit/fallback；erase/覆盖是补evict的真实边界。 |
| 同文件:2533、2723 | superellipse local/translated geometry缓存；SVG真实needUpdate/reuse/DrawBitmap。 |
| Bar/Bar.UI.cpp:329、400、458、463、470、525 | 内容transition/CalcWH解析，CacheBitmap的着色/解析、lunasvg栅格、D2D上传和成功缓存值提交。parse owner不是Rendering.cpp。 |
| Bar/Bar.Scene.cpp；Bar.EraserAttribute.Test.cpp:85、642 | 适用的Scene/offscreen资源测试；现fixture自建部分图，不能升级为真实Bar ULW/完整scheduler基准。 |
| Inkeys/IdtMain.cpp:655、970、1349、2071、2137、2232、2726 | early parser位于普通业务前；普通启动有更新、SuperTop、自启、shortcut/DDB、PPT及后台/全局线程。仅复制EXE不足以隔离。 |
| Helper/ShutdownSupervisor.cpp:983、1035、1460、1751 | 可复用E02 exact父HANDLE/PID/镜像身份、private bin、limited inheritance、mapping magic/version/size和fail closed；现capability只覆盖四fatal sites。 |

行号为本轮读取时状态，后续并行改动以符号定位。本轮未重复全历史研究。

### 3. U04-B1：真实事务确认时间与分段

真提交点就在CompleteAttempt返回后的原diagnostics块（12699附近）：若presentCompletion.IsCommitted()且diagnostics有效，立刻读取一次steady_clock，记录barCommitTicks/hasBarCommitStamp、当前device epoch、原presentAttemptFrameSerial和当前候选target revision。位置先于StartupPreview通知和成功快照发布。名称software-Bar-transaction-commit；它证明四API结束并经决策确认，不证明光学像素可见。

失败/defer/backoff/Idle/无调用hasBarCommitStamp=false，不以callbackEnd代填。原attempt serial每callback/backoff也推进，不可当实际GetDC或ULW次数；沿原presentAttempted/ulwAttempted分母并保留原serial名字。只有真实commit增加独立successSerial。

保留旧start/end/proxy链，另建立true-Bar commit链：同(run,callbackGeneration,epoch,activitySegment)内连接有效真实成功；idle/重注册/epoch切断。first-after-idle raw interval单列，不进active gap。验证start<=commit<=end；逆序/缺失保留invalid分母，不clamp为0。建议schemaVersion=2，旧proxy与新stamp报告两列；sizeof预算随新增数值重新计算，render thread仍不扩容。

FrameStage在既有六项后追加并同步formatter stageNames/镜像/test，保留原序号：

| 阶段 | 实际原调用边界 | 解释 |
| --- | --- | --- |
| WakeAndSnapshot | 同名函数/调用 | snapshot/状态锁/Tick wall，不移动Tick。 |
| DisplayTransition | ApplyDisplayTransition | DPI/monitor转换。 |
| SubmitTargetsAndLayout | 同名函数 | 目标和layout计算。 |
| AdvanceAnimationsAndDeriveLayout | 同名函数 | 现动画循环/physics/timeline。 |
| PrepareLightingAndDemand | 同名函数 | 两路真实光源/需求/共享lighting数值。 |
| DirtyAndPrepare | CalculateDirtyAndDrawPresent入口到原Draw timer前 | dirty/viewport/容量/映射/资源准备的合并父段。 |
| Resources | 原EnsureDeviceResources调用 | no-op/hit/create/失败/重建；属于DirtyAndPrepare子段。 |

已有Draw/GetDC/ULW/ReleaseDC/EndDraw/present lock保留。直接drag吸收与未细分成本先标callback余量。Draw/Resources/SVG/path等inclusive父子wall不要相加；GetDC可包含此前GPU等待，软件wall不等于GPU执行。

输入段必须由F真实owner probe提供：send→owned HWND receive/publish→实际interaction accepted→target consume→truecommit。demand generation只能说明唤醒，不是每输入延迟；未提供的段标unavailable。

### 4. U04-B2：固定domain目标与动画实际完成

不能用第一次Idle、needRendering=false或三timeline停止判断整项完成：hover/light可持续，timeline终点也不必等于所有关联val/tar/关键帧/回弹完成。

用固定domains（MainLayout、DrawAttributeLayout、GeometryAttributeLayout、EraserAttributeLayout、DockLayout、SvgContent）和数字request token，不建全动画registry：

1. fixture发布stepId/有限expected target，真实Interaction对应业务分支确认意图时旁挂acceptedTargetRevision；Main click可复用mainButtonClickPulseSerial作接受佐证。其它目标在实际接受分支计，不在每个UpdateRendering计。
2. 原SubmitTargetsAndLayout作出真实目标后，将consumedTargetRevision/domain/有限target signature锁进当前候选。BarRenderLoopState含共享状态引用；commit时不可重读最新输入倒填旧帧。前后revision不一致标superseded/ambiguous，保持原业务渲染行为。
3. 在已有AdvanceAnimation调用/循环中利用生产result.active和完成后的IsSame积累对应domain pending位；包括该场景实际影响的x/y/w/h/enable/pct、属性root/progress、关联timeline/content transition、Dock spring/recovery。不要再扫描所有animation，不修改result.active。
4. hover/press反馈颜色、持续mouse/primary光源不阻塞MainLayout完成；它们测量时归自己的Feedback/Lighting域。FineDial dwell/hold/inertia属于该gesture完成，不能过滤必要2秒保持或physics。
5. domain pending=0、有限target已消费、无更新同domain revision且完整事务成功，才在B1发布completedRevision/completeTicks。最后settled帧失败不完成，恢复成功等待计入动画wall。通知只有固定数值/release发布，不增加请求。
6. 快速反向旧step记superseded/cancelled；相同target/no-op单列acceptedNoOp/state-already-committed，不当0ms动画样本，不伪造新commit。stop/device failure/timeout都有terminal status和分母。

最小实现可在现ChangeValue/State/Pct和内容transition调用点旁挂固定role/domain，或只在当前scene选定属性观察；仅sample active时做。先覆盖Main+DrawAttribute是可独立review单元，其余domain需保持未覆盖，不能据此关闭整个E04。

输入/accepted归消息或Interaction owner；candidate/pending/success归唯一Scheduler；runner只读发布快照及停后report。probe在Start前安装、owner join后清除，禁止持有TLS到下一帧或跨线程传COM/context。

### 5. U04-B3：真实缓存、SVG和path边界

| family | 实际插点 | 必需口径 |
| --- | --- | --- |
| Rounded/geometry diffuse parent | 原GetRoundedRectDiffuseMask/GetGeometryDiffuseMask的hit/miss/create；capacity erase | 复用原计数；实际erase新增capacityEvict；epoch/reset另记invalidate。失败不计create。 |
| Exact mask | ready hit、CreateRoundedRectExactMask、parent/global两个erase | lookupMiss/createAttempt/success/failure/耗时；每ready删除计evict，parent删除携带children数量；fallback/warming沿原原因。 |
| Gradient/label/preview | 原32项erase、LRU目标覆盖、实际Create API | hit/miss/create/failure/capacityEvict；实测0与unavailable不同。 |
| SVG lookup | Rendering::Svg的needUpdate/reuse | 无刷新且有效bitmap为hit；刷新为miss；失败旧图保留单列fallbackRetained，不叫hit/create。替换是replacement，ResetCache是invalidate。 |
| SVG prep/parse | UI::CacheBitmap utf16/color和loadFromData；CalcWH及内容apply解析 | prep/parse分别count/wall，实际owner Bar.UI.cpp必须加入写入所有权。 |
| SVG raster/upload/draw | renderToBitmap、D2D CreateBitmap、最终DrawBitmap | rasterAndInternalGeometry/upload/drawSubmit各count/wall/失败；lunasvg内部geometry/tess不可由公开API拆猜值，drawSubmit不是GPU cost。 |
| Superellipse path | GetSuperellipseGeometry local key/new geometry/sink、translated geometry | local hit/miss/build/failure；transform hit/miss/create/replace；原segments/key不变。 |
| Preview/FineDial | GetThicknessPreviewPath/GetThicknessFineDialSelectorGeometry及实际DrawGeometry | once/build/failure/submit；GPU stroke/raster内部unavailable。 |
| PNG | CacheBitmap/DrawBitmap和资源decode | upload hit/miss/create/failure；初始化decode单列；epoch仅丢上传保持CPU像素。 |

frame数值嵌入现FrameDiagnostics；这些阶段可能是Draw/Advance/Resources子项，报告明确父子关系。按实际cache lookup/create/draw对象边界累计；不得每Bezier片段、像素或light slice读钟，不改预算/分辨率/质量/key/失效。

InitializeUI/按钮加载的SVG parse发生在无callback TLS的线程。cold资源若要记录，在fixture初始化线程安装一个固定ResourceObservationScope，结束后把纯数值summary放离线报告；不倒填第一callback，不建跨所有资源线程的原子map。只在scope/capture开启计数。logical texture bytes/entries不叫显存；失败半成品不计ready资源。

### 6. U04-F：最小early生产Bar夹具与来源安全

以下命令形状均尚不存在，实施后才能执行或记结果：

    Inkeys.exe --ui3-presentation-benchmark --round 1 --scenario-set core-v1 --capture on
    Inkeys.exe --ui3-presentation-benchmark --round 2 --scenario-set core-v1 --capture on
    Inkeys.exe --ui3-presentation-benchmark --round 3 --scenario-set core-v1 --capture on
    Inkeys.exe --ui3-presentation-benchmark --round 1 --scenario-set equivalence-v1 --capture off

public CLI只跑parent。内部child有单独purpose/version，复用现exact parent HANDLE/PID/image file identity、private bin、limited inherited handle list、ack和固定mapping magic/bytes；现fatal-site capability不能自动授权benchmark。错参、无继承HANDLE、wrong parent file、未知scene/capture、非法capacity/nonprivate/reparse路径fail closed，直接early return非0。

parent在本仓TestResults/release-hardening/ui3-real-<unique>/rN/bin独占新目录复制EXE，每轮fresh process/cache/static barUISet，不覆盖existing文件。EXE/hash/workspace身份由root核。仅新增一个Bar.Presentation.Test.cpp（建议名、当前不存在，module Inkeys.UI.Bar/import :Main），因此能访问内部InitializeWindow/UI，无需导出两内部函数。对外最多导出一个RunAuthorizedPresentationFixture。复用现Helper identity/parent cleanup，不另造测试框架或复制RenderLoop/Scenegraph。

#### 实际bootstrap次序/前提

1. auth成功→early fixture→返回；无普通Main后续。设private globalPath/bin、当前hInstance、offSignal=0与fixture默认业务/config；读取/写入只用private seed。Inkeys::config.UI.Bar.Zoom/FixedButtons和setlist component开关先就绪。BarButtonSet::Load可能config.Write；GetFilePath实际是globalPath+Inkeys/Config/main.json，所以先证明private root有效。
2. ConfigureRawCapture在任何RenderPipeline::Initialize前（普通startup preview可能早启动，fixture不启动preview）。复用EnsureProcessDpiAwareness、CoInitializeEx、Display::Initialize、RenderPipeline::Initialize；实际IdtFontFileLoader/IdtFontCollectionLoader+TTF1/7/3/8建立DWriteFontCollection，I18n::load加载内嵌JSON。字体/文本不能省掉减少负载。logger如需使用private根，raw不卸载生产异常sink。
3. Window Service创建真实Bar role，生产WindowProc+QueueWindowMessageInLayoutSpace/HiMsg，WS_POPUP|WS_CLIPCHILDREN、WS_EX_LAYERED|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW、visible=false。floating_window取Service::Handle(Bar)，核PID/HWND。初次只建Bar即可，无需Draw3/RTS/PPT/Freeze/Magnifier/TopWindow来测主栏；正常图上的builtin控件/图标/layout仍加载。
4. 同module fixture按普通Initialization生产顺序调用InitializeWindow→spec.ConfigureLocalizedTypography→InitializeUI→StartDisplayTracking→barMedia.LoadFormat→PresetInitialization/RegisterBuiltInComponents/Load/StateUpdate→SetContentStateUpdatesReady(true)→barState.PositionUpdate→barUISet.Rendering。Rendering注册真实coordinator和共享Scheduler。不是现offscreen手工搭图。
5. 一个实际worker调用barUISet.Interact，原poll/Hit/业务路由保持。不调用普通Bar::Initialization，所以不调用MouseHook；PPTLinkage/更新/SuperTop/DDB/自启/shortcut/系统component执行均无bootstrap入口。图仍保留，runner只触达已审工具/layout/颜色/粗细，不能点击Explorer/Lock/ESC/AltF4等外部callback。
6. 首次真实commit、InteractionReady分别确认后才送轨迹；第一次冷场景单列。parent监控任何fixture窗意外可见/前置/拦截则FAIL并有界关闭自己的child。ULW可能把hidden窗重新定位，但不应变可见；必须核visible flag。hiddenULW结果仍是该fixture的软件事务。
7. Settings竞争必须单独建自有Setting角色、Initialize/真实RenderSettingFrame/ImGui session；现Show会入business/show路径，需要root另冻结private hidden activation，首次Bar-only不能称Settings已测。假Settings callback只验证Scheduler合同。

#### producer独立冻结

单纯自有HWND的WM_MOUSE序列不够：Seek读取系统按键/光标，WM_MOUSEMOVE会注册Raw Input。不能使用global SendInput/SetCursorPos干扰用户，也不能伪造WM_TOUCH句柄。

建议在Bar.Interaction.cpp增加只在auth early fixture有效的有限producer；fixtureWndProc接收数字index，读取child预建immutable轨迹，构造现WM_TOUCH实际生成的ExMessage，复用MarkBarTouchPointerMessage、屏幕坐标tag与Window::Enqueue。Down/Move/Up/Cancel顺序/button flag/坐标走现生产Interaction/Hit/Layout/业务逻辑。不要以lParam传任意跨进程指针。该source名称synthetic-touch-owned-HWND，不能称硬件Touch/真实WM_TOUCH。

final indicator veto以及其它GetCursorPos仍会受真实用户指针影响。完整drag/cursor-light若要固定轨迹，可用仅auth fixture有效、绑定当前synthetic contact的有限screen-point/left-state provider；默认生产仍读OS，无普通产品开关，所有依赖点由root先冻结/review。未实现时相关scene标AMBIENT_INPUT_CONTAMINATED/NOT VERIFIED，不能删除veto/关闭光源/绕Seek制造快结果。本研究没有确认该残余读是产品缺陷。

sender/owner各读一次同域tick；真正终点是B1，不是SendMessageTimeout返回或runner Poll结束。own HWND身份先核，PostMessage/SendMessageTimeout有界。source时间/actualsend jitter和实际accept数量分别记录。固定单outstanding target+bounded sidecar足够；rapid reverse允许明确两请求，缺可靠关联不进latency分布。capture-off等价fixture仍需要有限轨迹身份证明，只关闭U04采样；off轮不能由poll造frame分位数。

#### 停止/数据边界

source停止→必要Cancel→owner接受Close并沿既有SetOffSignal/15s监督→SetContentStateUpdatesReady(false)/Wake→Interact join→StopDisplayTracking→StopRendering同步Unregister→自己的Setting Shutdown如有→Service::StopAndJoin→RenderPipeline::Shutdown/join→TakeRawCapture→离线write/sort。probe只在全部owner join后撤销。E02 Host/Window停止修补由其owner协调，本单元不改它们。

child未封口、parent因timeout清理、Start/Take失败、overflow/owner仍活，不能标正常run。parent始终持exact hProcess与unique目录，不能按名称杀进程，不碰用户Office/Inkeys。E02的25s不能直接当200次动画的总timeout；core需显式合适budget如240s并单列15s关机deadline，长run另预算。

raw arrays/target numeric summary在child内，postjoin一次write unique tmp/flush/rename；parent核natural exit0、sealed/drop/scene terminal/文件身份后统计。marker/mapping不做每frame I/O/IPC，不影响真实document保存。

### 7. 三轮Release、统计与证据

目录：TestResults/release-hardening/ui3-real-<unique>/rN/{raw-callbacks.csv,raw-batches.csv,raw-targets.csv,resources.json,summary.json,bin/...}。每轮meta含EXE/hash/workspace/schema/clockPeriod/origin、run/round/scene、OS补丁/CPU/GPUdriver/architecture、DPI/分辨率/刷新率/多屏/供电、actualUI3 backend/FL/epoch、BarULW/hidden、主题/animation/dynamiclighting/defaulteffects、capture/capacity/allocated/drop/invalid/accepted/cancelled/superseded/timeout/source种类。

每scene三轮fresh相同Release/设备/效果/轨迹，≥16次完整目标warm-up后≥200次目标变化与足量成功活动帧。cold/first/warm/reversal/idle/long各population分开；core依probe/domain覆盖展开/收起/快速反向/drag吸附/属性/颜色粗细FineDial/SVG光影/settings/idle。未写scene不记通过。32K是否够以真实drop为准，截断需分场景/在既有上限提高容量后复验，不叫完整尾数据。

输出每population count、median/P95、P99、>=50ms longframe数量/比例、active callback/truecommit gap、rawsuccess gap、accepted→final successfulcommit动画wall、各父子软件stage、attempt/failure/legalRetry/deferred/rebuild/cache计数。小样本P99=null/insufficientSamples；≥1000有效样本才报告探索性P99并说明轮间噪声。零样本median/P95也null；不以callback、attempt、块均值冒充有效fps或frame P95。

线程CPU取真正render线程start/end GetThreadTimes等delta，不把callback wall称CPU time；必要时capture启动写一次真实threadId数字meta，owner只持该child线程HANDLE，不每帧查询。Process private bytes/WS/GDI/USER/handles在warm前后与长run低频边界采；显存/GPU能力缺失标unavailable，logical cache bytes不是显存。

正式采样root独占run slot，所有build/checker扫描/Draw3及其它bench停止；低频监控不忙poll制造负载。沿父冻结门槛，median超噪声且>5%、P95超噪声且>10%才是实质退化候选；尾延迟、画质与输入语义分别判。当前仅观测，不承诺收益。

### 8. 验证与独立review门

| 项目 | 实际生产入口/断言 |
| --- | --- |
| B01提交/时钟 | 同一生产CompleteAttempt+timestamp helper可注入clock：任一API失败/defer无stamp，success一次，start<=commit<=callbackEnd；实际Bar fixture证明确实由真实ULW调用。fake callback手写success只属Scheduler合同。 |
| B02默认off | 现R01/TLS null，timer null无clock，无数组/targetprobe；不改Tick、request、dirty、ULW、sink。 |
| B03目标 | 真实AdvanceAnimation/timeline与生产domainobserver：dt0/clamp/关键帧/最终失败→成功、快速反向、same-target、epoch/idle；hover/light持续时布局可独立完成，失败像素不确认final。 |
| B04资源 | 实际SVG/geometry/mask API和readback核首次miss/repeat hit/颜色内容尺寸epoch失效/失败/容量evict/discard；初始化及lunasvg内部未测明确。 |
| F01鉴权/safety | wrongparent/noinherited/nonprivate/unknownscene拒绝且不落普通Main，核仅自有目录/HWND；无registry/shortcut/DDB/PPT/globalhook启动。 |
| F02实际pipeline | productionBar Rendering+WindowService+ULW，send/receive/accepted/consumed/completed/superseded/rawcommit分别计，actualticks有因果身份。 |
| F03退出/封口 | source stop/Cancel、Interact退出、Unregister后释放，join后Take一次；parentcleanup不能冒称naturalpass。 |
| F04 on/off等价 | 相同轨迹/效果/设备freshchild：最终tool/color/width/fold/panel、Down/Up/Cancel/业务调用数、BGRA相同。readback只在独立验证postcommit，不能每帧污染性能run。中间动画按真实advance可注入时间合同核，两进程wall不同不可要求同tickframe完全一致。 |
| F05统计/覆盖 | 未执行/缺backend/少样本/drop/invalid/ambient contamination正确FAIL/NOT VERIFIED/null；统计helper用已知数组断言，不复制另一套算法互证。 |

root串行完整InkeysRepo.sln Debug|ARM64：ARM64原生MSBuild，同invocation PATH规范，≥5min；strict --no-window、资源/真实HWND分别記。随后Release|ARM64，module/工程改动适用Win32/x64 Release编译。实际F不是strictno-window。

B checker看实际diff/defaultoff/timer/revision/cache；F首次运行前独立safetyreviewauth/root/source/lifetime，单scene green后扩大三轮。后续修补重冻EXE并复跑相关验证，不能沿旧pass。

### 9. 所有权/依赖

| 唯一writer | 拟写入范围 | 出口 |
| --- | --- | --- |
| root/UI3计量owner | RenderPipeline.cppm/.cpp/Diagnostics.h、Bar.RenderLoop.cpp | R已review；B1真commit+timer/schema先行，B2domain后行。 |
| root/资源owner | Bar.Rendering.cpp、Bar.UI.cpp（必要interface） | 完整parse需要UI.cpp；cache算法/quality/key冻结。 |
| root/producer owner | Bar.Interaction.cpp、Bar.Main.cppm及最小固定probe | root单独冻结auth/source/accepted/OS读取前提后再写。 |
| root/CLI/lifetime | IdtMain.cpp、Helper/ShutdownSupervisor.*、一个新Bar.Presentation.Test.cpp、project/filters | 同module访问内部init；earlyauth/返回；协调已交回Main/Helper所有权。 |
| independentchecker | render_scheduler_tests.cpp/真实observerprobe | 看实际最终diff，root独占build/run；不递归agent。 |
| root/证据 | 父performance/validation/ledger/HF | 三轮Release/raw/限制；workspace fingerprint+EXE hash。 |

Draw3 Session/Controller writer、Host/Window lifetime researcher独立，本文不写其文件；F引用它们最终停止合同，不合并UI3/Draw3设备/线程。

### 10. 规范、外部对照与限制

已读workflow、active PRD/design/implement、spec/native-desktop index、ui3-render-diagnostics/rendering-and-ui相关段、cpp-conventions/build-and-compatibility、native quality/guides code-reuse；对照四个指定research。未读取implement.jsonl/check.jsonl（research角色隔离）。

ui3-render-diagnostics的“只有sink有TLS”是R前文字，实际已sink||raw，root之后update-spec同步。native quality里的standalone旧Solution不替代本产品InkeysRepo.sln。

HF是e32a5fc0+本轮工作区改动，身份由root核；本研究不执行Git。HC候选run31487748238/82f7b7c0尚未证明用户指定Canary，H2为Release20260713a。旧binary没有新raw，HF内部tick不能和旧主观评分直接证明胜出。H0源码加同一诊断补丁必须标H0+instrumentation；原binary需共同外部观察。

Oray虚拟display/remote和hiddenULW只能证明HF该环境的软件事务/CPU成本，不能升级光学输入、硬件Touch/Pen、真实桌面主观流畅度、DComp compositor、完整GPU或指定HC/H2体验。meta要记录actualadapter/display。

Win7 SP1仅KB2670838，Hardware FL11有/无→WARP FL11，保留FLIP_SEQUENTIAL、只DComp/ULW且两个DWM禁用；不因微软泛化文档改bitblt，不静态增加高版本可选API；本研究无Win7运行证据。

## External References

没有新增网页引用。lunasvg/Win32/D2D描述限于当前源码调用边界，未凭版本/通用文档猜内部几何/GPU/Win7结论。后续新增GPU采样API需另行官方接口/目标可用性核验。

## Caveats / Not Found

- 本文下一实施合同；新CLI、truecommit/domain/cache payload与earlyfixture尚未实现，不能记PASS。
- 普通copied启动及现fatal-siteauth不能自动授权真实Bar；只能earlyreturn+所列调用chain成立才可运行。
- mouse/最终indicator等系统cursor依赖需要source合同，缺失则scene未验证；未据此确认产品bug或改功能。
- 完整SVG parse必涉Bar.UI.cpp；lunasvg内部geometry/tess/raster无法独立测，GPU执行/像素可见未得证。
- 此轮只创建本research文件；无code/spec/taskmetadata修改、无Git/build/test/GUI。root可沿B1→B2/B3→Fauth/source→review/test→三轮Release接续。
