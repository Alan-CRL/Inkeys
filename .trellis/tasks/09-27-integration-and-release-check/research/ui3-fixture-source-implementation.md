# UI3 F：实际 Bar bootstrap、immutable Source 与 owner 生命周期准备

Active task: `.trellis/tasks/09-27-integration-and-release-check`。日期：2026-10-01。Writer：`ui3_fixture_source_impl`。

状态：**READ_ONLY_PREPARE / SOURCE_NOT_WRITTEN / NOT_RUN**。本轮唯一写入本报告。没有写源、项目、Main、Helper、Probe、spec、父账本或其它 writer 文件，没有运行 build、产品/测试 EXE、GUI、Computer Use 或 Git。Root 唯一 shared 入口、工程和 build/run；B3 七源仍由原 writer GREEN 独占，C3 存储调查独立。以下新增 Source/runner/接口都是待 Root 冻结、独立审查和 WRITE_ALLOWED 的方案，不称实现通过。

## 已读实际上下文与身份

已读保存的 full hook 1206 行、真实 AGENTS/workflow、trellis-before-dev、G prd/design/implement/implement.jsonl 的20条上下文；过长未内联的设计/合同已分段续读。另读 native-desktop index、rendering/UI（Scheduler/窗口/事务/光影/点击批次等相关合同）、diagnostics、resources、C++、build、configuration/assets/input 与 reuse/cross-layer 指南。已完整读 R2 439 行，特别是 §6–10、独立 design-review F GREEN_DESIGN，B2-P2/F066 code-review 与 B3 最新 code-review/implementation。源码证据以实际符号为准，不把历史设计中的“尚不存在”覆盖当前代码。

| 冻结输入 | SHA256 |
| --- | --- |
| R2 contract | B32BF41F5EF31F9B8DF6B0488D0F8A6074839800A8CF9B1A7C8E7213054D1097 |
| B2-P2/F066 code-review | 220E4CAB708FF17CAE6B0F4A71BBC066BED13E407797734B84ECEB5068994D15 |
| B3 code-review | 57380013ED466DD89156F35E284BA2B88969E1AF8C4523C81A6300CEABA65861 |
| B3 implementation 读取时 | 4CFDD352B3102917341E5E2FE1B1CB831B46C1D1DAFB5644AD18435C307EEF2E |
| Auth.h | 20AB2AF89744BF52871239891CE97399762CA7FC46B21E63D5FFC62EFA087D13 |
| Auth.cpp | 552EEC5AB854D239D5354680DB13433C38D831A2DD428C5D6ED7D5C07737F5EB |
| Auth implementation report | 05A7F1906B460E9854951FC11BCFA4A797054179ED2EF2FD26CEEB20D2420530 |

实际只读 caller 四源保持 B2 交付身份：Interaction `8F7B1A76…CC9684`、Main.cpp `5AA66C10…29E8C`、Button `AC565499…D4F47`、Main.cppm `9B11FDD6…AB76F3`。B3 源还在写，不能拿其读取时 hash 当最终冻结或覆盖它。

## 最小职责与未来文件边界

当前缺口是 F 尚无真实 Bar bootstrap、受鉴权 index Source、消费/动作权限接缝和最终所有 owner 的真实 join。B2 publication/observer/ready 与 B3 SVG 已有实现，复用它们。

未来建议唯一写入：

- 新 `Bar.Presentation.Test.cpp`，同 `module Inkeys.UI.Bar; import :Main;`：受信 bootstrap、source/Interact 线程所有权、唯一 cleanup、离线输出，提供 Auth.h 中两个普通链接定义。
- 新 `Bar.Presentation.Source.h/.cpp`：64B DTO、真实完整编译表/hash、有限纯数值 Source 状态/sidecar、index/consume/phase/point/action 校验；不建通用输入/场景/遥测 registry。
- `Bar.Interaction.cpp`：真实 Touch message builder 的窄调用、Wait/TryGet 的 before-Prepare 验证、四类实际 pointer 读取接缝、真实 Main/Draw/其它动作门、Clear 丢消息记录、Interact TLS 绑定与结束。
- `Bar.Button.cpp`：实际 Draw callback 首步再验动作及同一 Pen baseline，第一业务写之前拒错动作。
- `Bar.Main.cpp`：只在确有必要时加受信 source 的窄转发；ReadFiniteSignature/StateUpdate/UpdateRendering 原位置保留。当前公共方法和同 module forward declaration 已足以多数 bootstrap 调用，不先扩 Main.cppm。

我不写 RenderLoop/UI/Rendering/Probe/Helper/Window/State/Draw3/工程。Root/B3 的必要小接口另见下节；Root 最后工程登记、Main 最早新 early dispatcher 后旧 Supervisor、整体 actualdiff/CLEAR、构建和运行。没有成功 stub、没有 authOnly purpose 或普通产品故障开关。

两普通定义精确沿冻结 Auth.h：

```cpp
Ui3FixtureSourceDescriptorV1 GetCompiledUi3FixtureSourceV1(Ui3FiniteScene) noexcept;
int RunAuthorizedPresentationFixture(const Ui3FixtureAuthorization&) noexcept;
```

普通头在 global module fragment include；与 module implementation 的普通链接身份须完整 Solution 验证。Source 普通头不重复声明 named-module 的 Message::Reply/Window 类型；用纯数字 disposition/receipt，由同 module 的 callback 转为真正 Message::Reply。有限纯校验可供 Headless 共用，不能把 Auth/Supervisor/Main/真实 GUI runner 链到 Headless 来凑测试。

## Root/B3 最小接口依赖

1. **Root 已接受的 DPI wrapper**：`bool EnsureAuthorizedUi3FixtureDpiAwareness(const Ui3FixtureAuthorization&) noexcept`，Root 在 Main 核 exact IsAuthorized 后调用现私有 EnsureProcessDpiAwareness。没有复制 DLL 加载、额外 SDK/静态高版本 API或 Win7 要求。
2. **Root 已批准 CompletedRevision getter**：`Ui3FinitePublication::CompletedRevision() const noexcept`，只 acquire 读现 completedRevision_；仅真实 Layout+SVG+四API成功 NoteCompletedGoal 的 receipt。结合 TryReadStable 的 run/step/source/revision核 setup/warm16/final，不读活 plain rows、不取 poll 尾时刻、不改固定节拍。Root 交 B3 唯一 Probe owner实现。
3. **初始化 Fit baseline 必须闭合，精确 Bootstrap 提案如下**。它还待 Root 冻结；需要 Probe 内两小方法/flag，复用现 ready，不新增 registry、Request、时钟或业务 setter。
4. **Root 已接受 equivalence checkpoint 形状**，具体运行授权/尺寸预算还要按下节冻结。RenderLoop由 Root/B3 串行接；本 writer 不写。

### 真实初始 Fit 与一次冻结

实际 `SubmitTargetsAndLayout` 私有 CalculateButtonLayoutWidth（RenderLoop2259–2296）才取得完整 layoutTotalWidth；2949调用现 `Zoom::FitInitialAfterMainBarLayout(owner_, totalWidth)`。`StateUpdate -> CalcState/PresetHoming` 只更新状态/显隐；PositionUpdate只判 side，不取得完整宽度。不能在 pre-owner 猜宽度或复制布局算法。

Fit先 exchange(initialZoomFitPending,false)，使用实际 main/root几何、window宽高与dpiZoom；configZoom约1且完整所需宽/高超出window时，生产 FloorConfigZoom -> ApplyConfigZoom -> config.Write。它确实可改 configZoom，不能先把默认1冻结，再倒读新值补过去目标。

建议只新 auth fixture 在 owners 开始前调用 `Ui3FiniteObserver::EnableBootstrapBaselineBeforeOwnersStart()`。Publication暂为未冻结 baseline；默认其它 observer 沿既有构造行为。此模式只有 publicationSerial0、无 accepted/request/record、Source未开、Interaction线程尚未创建时有效。initial MarkConsumed可在当前 render owner暂接受实际合法/受支持 signature，原Submit/Fit/Advance继续原算法；原完整四API成功、layoutCurrent、initial placement/相关角色已settled时，Complete内一次 `FreezeBootstrapSignature(consumedSignature_)` 并发布现 ready.initialStableSignature。

冻结只发生在真正初始成功事务，signature取同帧真实 ReadFiniteSignature/tool/display/configZoom；不是 main线程跨 owner读活业务。初始几何仍变化就继续自然帧；无自然完成/环境未知则有限超时失败，不补 Request。Freeze之后所有 FrozenFiniteInputsMatch、unknown/interference、有效mask0xFF仍按原规则，禁止再次改 baseline。

Fixture先等 Registered+Transaction+LayoutStable+Anchors 的 bootstrap latch（尚不要求 Interaction bit），再创建实际 Interaction thread；acquire读ready与线程创建使已冻结plain baseline先行于Interaction读取。该线程进入真实 Run发布InteractionReady后，才允许 Source发第一index。初始pub0不伪造 accepted/revision/测量goal，既有512 rows和22word ready继续复用。需要对应反例：实际小屏高DPI触发Fit后按真实新baseline就绪；未settled/错display/半写/有accepted后请求freeze均拒绝；普通未启bootstrap行为保持。

## 128B/Frozen72、预算与 private bootstrap

先 IsAuthorizedUi3Fixture(cap) 并只读 `cap.Input()` 的72B本地冻结 tuple、路径getter；Packet只输出数值，不能从可变 shared 输入重新决定 scene/capacity/path/权限。固定 scene1/2、sourceVersion1、steps216、Main433/Draw435、sourceHash/nonzero nonce、captureoff0/on容量256..65536；所有普通链接 descriptor来自真正immutable表。

所有finite/publication/observer/SVG slots/object metadata/两scene编译表/Source sidecar/expected wire指纹/phase receipt/Source固定rows共同按实际sizeof checked相加，不超过Auth保留4MiB；raw实际 callback+batch stride按除法校验，fixed+raw≤64MiB，所有owner启动前准备。off不Configure非零容量、不分配R数组、不读新增阶段钟/B1 stamp；仍保留身份/ready/目标证明。像素功能缓冲另明确单次cap和共同预算，不能漏算。

私有路径：`globalPath = cap.BinaryDirectory()+L"\\"`；`Config::GetFilePath()`必须精确为bin/Inkeys/Config/main.json，`WriteSettingJson`为bin/opt/deploy.json。真实写之前核已leased parent目录、regular/no-reparse文件或确实absent；输出固定名CREATE_NEW。Auth已保活drive→repo/source/bin/Config/opt/log必要祖先，Source不放宽旧validator、不读真实用户配置/Memory/UInk。Config::Write会production atomic temporary/rename，不持目标file的no-delete lease挡自己的原提交；目录lease继续保活。

新config用实际 `config.ResetToDefaults()`/schema及 `config.Write()`，不能手拼UI图；旧setlist无默认constructor，多数字段仅静态零值，必须在owner前按真实Main的合法Bar消费者默认值明确seed。当前笔型/宽色来自StateMode真实constructor（SoftPen、3 DIP、原红色），再走真实 ChangeStateModeToPen；不直接setter/fold/预设目标。legacy选项与16组件开关、语言、SkinMode/scale等要记录具体seed，不把默认零当完整配置已初始化；可用真实 CaptureSettingJson/WriteSettingJson生成私有deploy，不调用含系统自启/shortcut分支的正常Main读取块。

Root已冻结 **新 auth fixture profile EdgeLighting=true/Dynamic=true**，只私有config与元数据；正式产品schema默认Dynamic=false保持。通过真实 SetAnimationOptions(default enable/speed1)、SetEdgeLightingOptions(true,true)、SetDebugOptions(default)应用，默认UI图/字体/光影/quality/fps/动画保持。H0/HF比较必须同true/true profile。Touch不触发mouse light，鼠标/Dynamic实际场景仍未覆盖。

实际顺序：

1. Auth、本地tuple/paths/table/budget准备；hInstance/current module、private metadata/globalPath先建立；普通offSignal初始0，不覆盖已有退出意图。
2. Root DPI wrapper、CoInitializeEx按真实S_OK/S_FALSE拥有COM、Display::Initialize、ConfigureRawCapture(on)/off不配置数组、RenderPipeline::Initialize；真实WARP/D2D/DWrite/Scheduler，不能另建device/pacing。
3. IdtFontFileLoader/CollectionLoader按原IsLoaderInitialized/GetLoader，真实 InitializeFontCollection({IDR_TTF1,IDR_TTF7,IDR_TTF3,IDR_TTF8})；I18n::load(1,L"JSON",L"zh-CN")读取内嵌245 JSON。失败收束，不用临时字体/空图通过。
4. 安装Source owning state/table/cap及unready binding后，真实 GetService().Start仅一个 Bar spec：WS_POPUP|WS_CLIPCHILDREN、LAYERED|NOACTIVATE|TOOLWINDOW、visible=false、原WindowProc、private messageCallback。created在真正owner核HWND/PID/thread、绑定；此前index拒绝。Start用现 FailedCleanupDeadline/PublishFatalFailedCleanupNoWait 的owning Signal保护已知失败rollback，所有test gates空。虽然没有Setting role，Service仍建立overlay与empty-setting两个线程，最终均要真join。
5. Service只拥有private Bar后真实 ChangeStateModeToPen。它走mode mutex -> Setting owner desired command -> Bridge PublishProductState -> ProductRunning=false的Reconcile Waiting；missing Setting/Drawpad返回false不操作外部窗。保持NotReady/noHost，无Office或新业务worker；不把该合法Pen链当文件隔离证明。
6. 直接InitializeWindow -> ConfigureLocalizedTypography -> InitializeUI -> StartDisplayTracking -> LoadFormat -> PresetInitialization/RegisterBuiltInComponents/Load/StateUpdate -> SetContentStateUpdatesReady(true) -> PositionUpdate。这两个初始化函数已在Main.cppm声明，不是另一个Initialization.cppm。`SvgObservationScope(&svgProbe,on,true)`覆盖真正value初始化/资源parse；bind前无旧bitmap，对svgMap 0x10000+真实enum、所有实际SVG preset/registered（含More、Setting、隐藏EndShow）的稳定ordinal 0x20000+ordinal绑定一次，PNG按钮不冒SVG。256 cap/未知旧bitmap拒证。Frame scope由现B3 RenderLoop沿同一probe接管。
7. 绑定Bootstrap observer+SVG、Rendering真实Register/Request；等待实际完整初始stable latch，再启动Interaction并等待Run真实ready，最后source线程/固定setup/warm/measured。没有StartupPreview/tracker、MouseHook、PPT/Office、完整Initialization、SuperTop、自启、shortcut/DDB、update、全局输入或Computer Use。

Display subscription可能在renderer运行中发布目标，只用既有原子display publisher；真正环境变化在freeze后使run invalid，不能刷新initialStable来接住新环境。Fit的私有config.Write仍发生在原render调用点，归启动成本，不修改普通生产同步位置。

## 真64B immutable编译表与 Source sidecar

按R2字段逐项static_assert size64/align8及offset32/40/48/56；reserved0/1=0、expectedBaseCommitSerial=0。整数offset均0，仅MainGrip/DrawButton，零modifier。phase字段Down1/Up3/Cancel4，flags低3bit Setup1/Warmup2/Measurement3/Teardown4、bit3 SetupOnlyIfFolded、bit8–9有限expectedAction1/2，其余0。表构造完成后const，runtime从不写回anchor/实际serial/tick。

Main为216 Down/Up对+1保留Cancel=433；Draw另有2个预留Setup Main rows=435。warm步骤1..16、measured17..216；Setup用独立非零stepId命名空间，不能混进216。每step1000ms、Up+20ms。提案Main首Down=0ms、第216Down=215000ms、保留Cancel=216000ms；Draw setup0/20ms、首warmDown=1000ms、第216Down=216000ms、Cancel217000ms。skip setup仍沿同一表/固定due，不能压缩另一轮节拍；Root冻结具体原点后完整hash固定。

Descriptor的FNV1a64不手编hash：offsetBasis14695981039346656037/prime1099511628211，uint64模运算；先编码 sourceVersion(uint32)、scene(uint32)、trajectoryCount(uint32)、expectedSteps(uint64)、steady_clock period num/den各int64，再按表顺序逐字段显式LE编码全部64B逻辑字段（signed按固定bits），不hash padding。dueOffsetTicks由固定ms与真实clock::period checked换算，所有计划表字段/clock period都纳入hash。unknown scene返回无效descriptor，不能给假hash。

Source先按真实ready证明核initial scope，Down一次读同一stable latch、generation/anchors/epoch/surface/mapping/dims有效、signature与冻结环境匹配，finite/range转short前校验；不clamp成别的按钮。实际boundCommittedCount/attempt/mapping/epoch/surface/屏幕点归sidecar。Up/Cancel沿对应Down固定屏幕点，即使动画/窗口变化也不取最新几何。保留Cancel只清已有自有contact，不启动新的业务动作；无active时SkippedNoActive不发假Up。Cancel不能因msg==WM_LBUTTONUP而进入Draw业务，需要actual phase gate拒绝全部semantic callback。

Draw Setup仅真实Main action。已展开时两row=SkippedAlreadyOpen、sent/received/enqueued/consumed均0；折叠时原Seek/Toggle打开并核真实CompletedRevision+stable目标，才开Warmup。Setup失败不直接置fold、不替换其它tool。Warm16未真完成不能进入Measured；固定下一due到达仍未完成，记录unfinished并停止新输入，不延长/补发以凑通过。

## own HWND index、队列与真正消费

Source线程只向本代 HWND PostMessage(WM_APP+0x4B0,index,0)，无指针/SendInput/cursor移动。全repo只读搜索目前仅R2文档声明该常量，无生产冲突；Root仍做最后全diff核。schedule必要steady_clock与raw阶段时钟分开，实际send jitter/source Window receive/dequeue分别保存；off导出性能时间为null，内部必要调度值不伪造成0ms软件完成。

Private callback核 exact cap、HWND/PID/owner thread、完整index/order/sourceSequence/表flags/phase/base0。借同module原 MarkBarTouchPointerMessage(cancel?,screen=true)，构造真正ExMessage WM_LBUTTONDOWN/UP、短屏幕点、lbutton与零modifier，走真实 Inputs::SetKeyBoardDown及 Window::Enqueue。expected wire指纹/sidecar在Enqueue之前发布；false记SourceRejected并fail，不调用普通 QueueWindowMessageInLayoutSpace/GetMessagePos或mouse/RawInput路径。Window latest snapshot只由Window owner写，Interaction绝不拿latestUp覆盖正在消费的Down。

**HiMsg Discard继续原WndProc，必须用Handled。** Root已接受private index和拒OS输入的 callback返回真正Message::Reply{Action::Handled,...}。ordinary鼠标/兼容mouse/WM_TOUCH/WM_POINTER、key/syskey/char、WM_INPUT等记录UnexpectedSource并不进入HiMsg或业务WndProc；WM_TOUCH先CloseTouchInputHandle，WM_INPUT沿DefWindowProc必要清理后阻业务。窗口create/destroy/DPI/paint等系统生命周期消息按其真正必要处理保留，不能把所有消息屏蔽；测量中DPI/display/setting change使run失效。关闭/捕获取消消息由自有cleanup保留，停止门后不重开source；外部未授权关闭输入记污染。callback不能抛到HiMsg后被其catch退回Default。

全局真实WaitForBarInteractionMessage/TryGet在Window::TryGet成功之后、Prepare之前调用 ObserveAuthorizedFixtureDequeue，核下一已enqueued原screen-tag message全部数值字段/phase/contact/sequence。one producer、每row至多一次；成功才推进consumer ack、Interaction TLS当前event/contact与Ui3FiniteOwnerContext request（Up沿Down step，ownerReceiveTicks取真实source owner receipt，on才timing有效）。top/nested Seek/属性loop全复用这两个helper，不能只hook Poll。准备后的layout消息不反向当source原消息比较。

Clear前检查是否有已enqueued未consume行；实际Clear删掉本轮行则Dropped/SourceRejected，保留prefix/分母，不重发。sender最多一个未消费wire行；允许Down的必要Up，禁止提前排下一gesture。固定due到达而前一row未consume记录失败/超时，绝不悄悄重排时间。

## 实际 pointer 与动作接缝

| 实际入口 | 本批接法与边界 |
| --- | --- |
| Seek6074/6242/6629/6661 | Touch marker走原Down/nested screen path，保留原Hit/拖动/取消/left判断；core不强迫Touch走GetAsyncKeyState/GetCursorPos mouse分支，OS mouse分支不声称覆盖。 |
| Seek final indicator veto6745 | ReadBarPointerPosition三态，auth Interaction取当前consumed Up/Cancel屏幕点，保留原BarScreenToLayout和veto；Unavailable取消/记失败，不能删veto或固定false。 |
| SuppressHover1417 / CommonHover2382 | Interaction TLS最新自己消费的事件点，保留hover suppression。core仅Down/Up，未触Move也不拿Window最新位置冒覆盖。 |
| WM_TIMER553 / syntheticUp438、456 | 只Window owner自身snapshot/active contact，Unavailable不fallback系统；无批准capture时unexpected timer/合成Up记污染/Unsupported，不用默认(0,0)入队。 |
| FineDial4304 / color4709 | 本批不可达；在进入该gesture的实际action前拒绝并计账，不替其GetCursorPos造万能provider。 |
| cursor light5724/5787/5910、RawInput/WindowFromPoint/foreground6833 | 没有mouse/raw输入；保持primary/dynamic配置，cursorLightActivated=false。若普通光标/焦点登记路径实际进入则污染，未覆盖MouseLight/FineDial/Settings。 |
| SetCapture667/686 | core无capturegesture，实际start capture在privatecallback之前拒绝；取消/销毁按原owner cleanup，不假报capture成功。 |

ReadFixturePointerForCurrentOwner返回NotInstalled/Available/Unavailable；NotInstalled才沿原GetCursorPos，authUnavailable不能回落OS。判owner使用actual注册thread id；Window读own snapshot，Interaction读current-consumed TLS，未知线程拒绝。render不读mutable source对象，继续现纯值accepted/latch。

最小业务门位置：Main真正命中Down在Seek前验Main contact；Seek返回allowClick后、Ui3FiniteMutationScope/TryBeginToggle/pulse第一写之前再验当前真实Up及Main expectedAction。Draw/其它主栏preset在真正命中Down前验，Up在ClosePenTypeMenu/More/Clean/clickFunc第一写之前再验；只有Draw permitted，Button Draw callback首步再验同Pen baseline/actualAction。

其它真实出口：More列表clickFunc3227、rightclick MessageBox/Close3340–3351、geometry3528/3531、width/颜色 setters1562/1998/2199/4809/4840/5128、tool选择4965/5300及其后各PenTool、eraser panel与keyboard均须在外部动作前阻断。做法不重写Hit：Run在pointer业务前核auth frozen tool/aux flags（More/menu/color/geometry/eraser/FineDial均未开放），初始这些早期stage自然PassThrough；实际More/主栏callback及Main/Draw各有门。auth core Main/Draw阶段未consume则SourceRejected返回，不继续进入后面geometry/width/tool分支；不能绕原Main Hit/Seek或Draw Hit/clickFunc直接改变目标。若aux非法在其早期stage前停止，不能让错误UI先执行ClosePenTypeMenu/颜色写等副作用后才判失败。

ui3 OwnerContext仅Interaction TLS，当前request scene来自row expectedAction；Draw Setup的Main不是Draw measurement mutation。请求消费一次，cancel和普通OS都不给semantic permission。普通NotInstalled行为沿原路径；默认无Source不新增时钟/表/线程。

## equivalence checkpoint 的完整目标、授权与预算

提案普通Bar窄接口：

```cpp
struct Ui3FixturePixelReceipt {
    uint64_t generation, committedAttempt, epoch, surface;
    uint64_t bufferMutationSerial, targetInvalidationSerial, pixelBytes;
    uint32_t width, height, stride, status;
    int32_t sourceX, sourceY; // 真正最终ULW source/viewport，非任意裁图
};
bool CaptureAuthorizedFixtureFinalBgra(const Ui3FixtureAuthorization&,
    uint64_t expectedCommittedAttempt, uint64_t expectedEpoch,
    uint64_t expectedSurface, std::span<uint8_t>, Ui3FixturePixelReceipt&) noexcept;
```

Root/B3在原Scheduler PostControl owner执行一次，取完整成功四API后锁存的viewport/source/真实target tuple，保留本次 context/bitmap本地COM lease；checked宽高/stride/span与目标尺寸，真实Copy/Map逐行复制CPU背板，Unmap后纯receipt封口。只成功commit身份不够：若之后BeginDraw/失败/deferred已改backing，bufferMutation与上次完整commit不等必须拒旧帧；epoch/resize/invalidation/早stop也拒绝。不跨owner getter，不Request/fullDirty/draw/Flush/额外present。

比较完整最终已提交 Bar viewport全部BGRA，不缩放、不遗漏边界；读ULW实际source rect是完整输出目标，不把容量padding当可见frame，也不裁掉目标的一部分通过。WIC PNG/hash在alljoin后输出；像素bytes逐位比较，hash只辅助。Tool/color/width/fold/panel、source/event/action/accepted结果也需等价；snapshot初始/最终状态不靠直接setter复现。两个版本共同的未覆盖视觉缺陷不由像素相同自动关闭。

64B/finite/SVG/source固定data仍≤4MiB。建议checkpoint优先核本轮剩余4MiB预算是否放得下整张frame；超出时明确NOT_VERIFIED，不能低分辨率/裁图。若Root选择另外显式single功能cap（建议最多32MiB且再核actual fixed+raw+pixel≤64MiB），必须在owner前准备、meta单列allocation，真frame超cap同样失败；不可随需无限扩容。读回只功能检查、成本统计外，不拿单像素smoke当全BGRA。

**当前Auth信封缺functional/timed run标识。** Frozen72/argv8仅scene/capture/capacity/round/table版本，无run-kind。不能从mutable Packet、round==1/capacity猜授权，更不能偷偷新增purpose/字段。已向Root提交最小取舍：明确冻结原216目标实际完成后进入本地EquivalenceAfterRun阶段，统计截止最后真commit，之后readback/control batch完全不入性能；若必须整run非timed，Root须另冻结真正显式功能授权字段/入口。当前checkpoint声明只是提案，不拿任意bool绕过“非timed”门。

SVG已有owned初始化counter与render总counter；停后两个输出须明确cold-init/render-all语义，不能把含warm+measured的总计称steady。若需要准确warm/measurement资源差分，Root/B3再提供一次原render owner的纯counter checkpoint：warm16真completion后、下一fixed due之前PostControl一次copy，不读活AfterOwnerStopped getter、不加新clock/Request；未获冻结前该拆分明确not collected。

## stop、真join、封口与输出

同一owning fixture state持有cap借用、immutable表、Source sidecars、observer/SVG、event与线程；Window callback捕获同state，所有Source/Interaction/Render可能引用的对象活到真实join或进程死亡。phase/status不是join证明。

正常/失败统一：停止新Source publication并唤醒producer；若已有自有activecontact，reserved Teardown Cancel沿Down点实际入队/消费，有限等待。随后真实 SetOffSignal(1)建立原普通15秒监督（同Close入口），在任何可能阻塞的join/I/O前接受退场；未能Cancel消费则记录失败后也先Close，不能在Close前无限等Source。顺序为Source线程真实join -> Interaction真实join -> SetContentStateUpdatesReady(false)/Display subscription Reset -> Bar.StopRendering同步Unregister/内部Seal -> WindowService.StopAndJoin的overlay和empty-setting两真join -> Display::Shutdown真实drain -> RenderPipeline::Shutdown Scheduler真join -> Take raw一次(on)/off合法absent -> AbsorbAfterOwnersStopped与纯值final snapshot。

StopRendering在Unregister后立即reset coordinator，像素checkpoint必须在此前已完成且无活callback借用CPU背板；pending PostControl/task也必须Scheduler真实join后才撤。OOM/线程创建异常/部分初始化均走同owner逆序cleanup；Window.Start内部已知false/rollback由本次FailedCleanup scope保护，不能仅bool返回后保护。scope成功/cancel真实monitor join完成，旧Signal不会跨下一代；新F不构造第二代产品。

实际join前cap/table/probe不析构、不detach/TerminateThread、不将running=false或Stop请求当完成。同步join卡住已有普通15秒监督/自己的父300s；若停止证据无法建立，保留仍借用栈/对象直到自进程死亡，绝不返回到Auth destructor。父强杀或特殊self失败不是Passed，无Sealed成功summary。初始无owner即失败可真证明空owner集合后Sealed Failed；创建过任一owner必须沿实join链。

封口owner在alljoin后一次填Packet统计/64bit ticks、result，最后Interlocked发布Sealed7；Passed1/Failed2，stageNone0..Sealed7沿Auth固定值。off started/finished内部0、离线所有性能time null；on起止定义/clock period须明确，软件完成只取最后真实匹配目标finalCommitTicks，不从source Poll/等待尾采钟替代。已接收/入队/消费分别计数，433/435是表上限，保留Cancel/SkippedSetup不伪执行。

唯一private leaf内CREATE_NEW：meta、finite-targets、source-events、svg-cold、svg-warm、summary，on另raw-callbacks/raw-batches；可得全图等价另PNG/BGRAhash。schema2整数ticks用整数输出，null/invalid/drop/fail未完成同时保留。216 planned、16warm、200measurement及setup分母分开，未启动/SourceRejected/RejectedByBusiness/Superseded/ResourceUnverified/timeout不能筛掉。成功median/P95与全部请求结局同报，样本不足1000的P99=null，不挑最好轮。GPU/光学/真实笔/长时/Win7/HC-H2/其它family和MouseLight/FineDial/Settings均继续未验证。

## Root 后续最小验证门与未完成项

1. 先冻结Bootstrap/CompletedRevision/功能checkpoint的少量接口、64B表时序/hash、owning Source声明；B3唯一writer停写并按Root串行交接。当前我仅准备，未写任何声明/源。
2. Source纯共用校验直接测64B/LE hash/flags/reserved/base0、unknown scene、source phase/order/index、short溢出/Unavailable/旧anchor、Window snapshot与Interaction Down/Up TLS、duplicate/Enqueue false/Clear dropped；测试必须触共用实现，不复制“正确算法”。Auth完整suite仍用真实positive Bar runner与旧purpose回归，不造authOnly stub。
3. 独立actualdiff safety核exactcap/组件file boundary/普通OS全类别拒绝与Win32资源清理、实际每动作前门、完整ownerjoin/partial初始化/exception、off no raw/noStamp/no计量clock，以及Bootstrap真Fit完成后freeze的happens-before。CLEAR前不运行新F。
4. Root完整InkeysRepo.sln Debug|ARM64，原生ARM64 MSBuild、同PowerShell PATH workaround、至少5分钟；三架构sizeof/offset/普通链接与DLL/TLB，strict no-window/PptCOM/既有B门。构建与真实HWND结果分别记录，Source runner只能主工程登记。
5. 先一个off/on bounded实际scene、16+200完整输入/accepted/资源与alljoin，真实全BGRA及业务等价；再两scene各fresh三轮Release相同环境true/true profile。每失败/未验证原始产物保留，不用历史B2/B3绿色代替F。最终phase/资源差分欠接口不假称已采到。

本轮检查只有只读rg调用链/接口/身份和本报告格式；产品lint/typecheck/build/tests/EXE/GUI均未执行，原因是READ_ONLY_PREPARE、依赖接口尚待冻结、Root唯一build/run与实际diff CLEAR门。仅本报告新增，继续等待Root明确WRITE_ALLOWED。

## 2026-10-01 Root 增量冻结：统计结束后一次功能阶段

Root已明确批准同一个既定Auth run在216步真实全部完成、Source真的停止后，锁定真实 MeasuredEnd（精确最后完整目标commit tuple/整数ticks与source终态），关闭timed统计边界，再进入本地 EquivalenceAfterRun。此phase只由本run已完成事实与仍有效cap产生，不从mutable Packet/round/capacity选择；不新增128B/purpose/argv。前文“信封缺run-kind”作为发现历史保留，此选择已闭合相应授权，不再需要新functional信封。

readback/control若仍进入原始R数组，就保留完整prefix并标afterEnd，从所有timed frame/latency/CPU分布排除。真实Source/Receive/Accept/consume/layout/resources/完整commit各边界及timingAvailable分别输出；功能readback费用/CPU/字节另列，不计优化收益。off这些性能边界为null，功能图/元数据仍可核。没有最后goal全完成、Source未真停、backing/epoch变化或memory超cap则checkpoint fails/unverified，不补Request/多画/降分辨率或用旧hash冒通过。

CompletedRevision与ready的两个独立读取不能拼成精确完成tuple：getter只证明该rev真完成；ready.lastCommittedAttempt/commitTicks可被同goal随后未认证资源帧更新。最小新增提案是 `Ui3FiniteObserver::CopyCompletedOutcomeForCurrentOwner(run,step,source,revision,Ui3FiniteTargetRecord&) noexcept`，仅在原Scheduler PostControl owner、cap已核的phase checkpoint内copy现512prefix中精确CompletedLayoutAndSvg的row，保全该目标actual epoch/surface/attempt/ticks/proof；不新建registry/读钟/倒填业务，不由Source线程直接读plain rows。Warm16/final调用至多各一次；错tuple/no-change引用未完成/资源未证/前缀缺row都拒绝。最终alljoin后仍Absorb/完整records复核全部216目标分母。

该owner copy接口尚待Root/B3串行冻结/实现，Bootstrap一次freeze也仍是前文待审具体方案。本writer没有写Probe/Source/RenderLoop。功能phase中首先真正source join（自然完成且无active contact）；任何source未停不能读像素，先按普通Close保护进入失败cleanup。成功读回后才正式Close/剩余Interaction、Display、Unregister、Window两owner、Scheduler全join与离线输出/Sealed，cap和CPU背板保活到底。

## 2026-10-01 WRITE_ALLOWED_F_SOURCE 实施交付（ui3_fixture_source_impl2）

状态：**PATCH_READY / STATIC_CHECKED / NOT_BUILT / NOT_RUN**。本节覆盖旧准备方案中的 Close 顺序；以 SHA `45AA6DB476559729A17258A3BDB8BFB4AA812AF7D4AE29BFA9C74B0FF905A7C8` 的新 Source 设计复审和 Root A/B/C 最终接口为准。只写本节及下面五个源；未改 Main、Probe、RenderLoop、UI/Rendering、Helper、工程、spec、父账本、Draw3，未进行 Git/build/测试 EXE/GUI/Computer Use 或递归派发。

### 已实现的真实链接与输入路径

- 新 `Bar.Presentation.Source.h/.cpp`：真实 immutable 64B 表、Main433/Draw435、Setup step 0x10000、warm1..16、measure17..216、1000ms/+20ms、保留 Cancel。Main首 Down0ms/Cancel216000ms；Draw Setup0/20ms、首 warm1000ms/Cancel217000ms。编译表全部字段、sourceVersion/scene/count/steps、实际 steady_clock period 经显式 LE/FNV1a64；不 hash padding，不手编常数，不回填表。
- Auth 普通链接 `GetCompiledUi3FixtureSourceV1(Ui3FiniteScene) noexcept` 定义在 Source.cpp；`RunAuthorizedPresentationFixture(const Ui3FixtureAuthorization&) noexcept` 定义在同 Bar module 的 Test.cpp。均为真实实现，没有 authOnly、skip-init 或成功桩。
- 新 Test.cpp owning state 在 Window 注册前安装 unready binding，实际 UI/Pen/Display seed 后 emplace publication/observer，调用 Root A Enable；自然 Fit/四 API/稳定 anchor ready 的 acquire 之后才创建真实 Interact，再等它原 Run 发布 InteractionReady。
- source 只 PostMessage 自有 HWND 数字 index；dispatcher 实核 cap、PID、thread、index/order/flags、ready/short range，由真实 MarkBarTouchPointerMessage、Inputs::SetKeyBoardDown 和 Window::Enqueue 入队。原 Wait/TryGet 成功之后、Prepare 之前比较完整 screen 指纹；TLS 只保存 Interaction 已消费行，窗口最新 Up 不替代该线程的 Down。Received fingerprint 在 Enqueue 前 release，真实 consumer 可能先于 Window 的成功回执运行。
- Main actual Hit 的 Down 在 Seek 前许可；Seek 真 Up 后在 finite scope/coalescer/pulse/fold 第一写前再许可。Draw actual Hit Down 在 pressed visual 前许可，Up 在 ClosePenTypeMenu 前许可，真实 Draw callback 首步再许可同一 Pen baseline。阶段至多一次，Callback 必须已有 Commit/对应 Down 许可。closed aux/frozen tool 门在早期 keyboard/overlay/eraser/属性逻辑前，Main stage 未命中时拒绝再进入其它动作。
- 普通 mouse/nonclient mouse/hover/leave/pointer/touch/key/syskey/char/raw/timer/capture/关闭与 Bar capture 私有消息均 Handled 拒证；WM_TOUCH/WM_GESTURE 结算原 HANDLE，WM_INPUT 调 DefWindowProc 完成必要清理。生命周期 create/destroy/DPI/display 继续原必要处理；测量中的环境改变拒证。合成 slider/color Up 在本 fixture 拒绝，Core 没有它们的 capture；已知取消/退出保留原 owner cleanup。Unavailable 不走 OS cursor 回退。
- Clear 使用真实删除计数；删除 source 行记录 Failed/Dropped，不重发。表/计划行均输出，Setup 跳过和没有 contact 的 Cancel 不冒充发送/消费。

### 私有真实 bootstrap 与预算

只消费 exact cap 的 Frozen72 和路径getter。目录租约/写前 ordinary/no-reparse/单 link 检查，实际 Config::GetFilePath 精确为私有 bin/Inkeys/Config/main.json；legacy WriteSettingJson 精确为 bin/opt/deploy.json。真实 ResetToDefaults/Write、完整 Main 合法 legacy 默认与16组件关闭、真实字体 TTF1/7/3/8、内嵌 zh-CN、ConfigureLocalizedTypography/InitializeUI/LoadFormat/PresetInitialization/RegisterBuiltInComponents/Load/StateUpdate/PositionUpdate；所有实际 SVG map/preset/registered 对象在 owned initialization scope 内稳定标签绑定。

fixture profile明确 EdgeLighting=true/Dynamic=true、原 Animation true/speed1、原 Debug false/ShowFrameRate true；正式 schema 默认未改。仅 Bar WindowSpec；Window Service 仍创建 overlay+empty-setting 两个 owner。失败 rollback 传现 owning FailedCleanupSignal、所有 test gates 默认空。不调用完整 Initialization、MouseHook、StartupPreview、Office/PPT、update、自启、shortcut/DDB、Host/global input；真实 ChangeStateModeToPen 在已建立本 child Window Service 且 ProductRunning=false 前提内执行。

owner 前按真实 `sizeof(FixtureState)`（含两 finite 表、SVG、sidecars、expected wire、单 job、216目标 receipts）、两编译表、cap/packet/frozen/path metadata与64KiB格式 scratch核 fixed≤4MiB；raw按真实两 sample stride，另计 capacity×2×sizeof(double) 的两个预分配统计数组。destination固定16MiB、同时 readable预留16MiB，功能峰值32MiB；共同总量≤64MiB，经除法检查后分配。大 capacity 诚实拒绝，不缩 R 数组/图。实际 sizeof、合法 capacity 和数据仍待 Root 构建/运行，不将静态公式当分配实证。

### 真完成、Close、控制任务与封口

Source 对每个实际 Up 的 exact run/step/source/accepted revision 等真正 CompletedRevision；固定下一 due 到达仍未完成即保留失败并停止输入。Setup若实际发送、warm16、final 至多各一次 owning PostControl，调用 Root B 复制第一次完整完成行；MeasuredEnd取 final 的精确原 finalCommitTicks，不拼另一次 ready。

正常与部分初始化/异常都统一：停新 publication/有限取消自有 contact → **正式 SetOffSignal(1) 在任何 blocking join/readback/清理 I/O 前建立原15秒** → Source真 join → Interact真 join → 本地 release EquivalenceReady → 同一个 owning job 一次调用 Root C → job最后相关借用结束/真实 event 回执 → content/display subscription停 → 同步 StopRendering/Unregister/Seal/reset → Window两 owner真 join → Display callback drain → Pipeline/Scheduler真 join → TakeRaw一次(on)/off不配置数组 → Absorb/纯值读取/CREATE_NEW离线输出 → Sealed。

job 内 catch异常并回执；B/C等待超时先正式 Close，保留 cap/table/probe/span/job/coordinator，继续等真正回执或自身原监督死亡。不能在任务还可能执行时 Seal/reset，不 detach、强杀线程或以状态代替 join。真实 join 异常进入保持活对象的 noreturn 等待，不能返回 Auth。Window.Start内部 failed cleanup依原 primitive，不添加新的启动故障开关。

Root C 为同步 render owner-only；本 writer PostControl 不在 C 中递归排队。普通 `AuthorizedFixtureEquivalenceReady(cap)` 仅 exact cap、216完成、本地停止/contact清、正式Close、两个实际join后为真。PixelReceipt来自 Source.h，包含原 generation/attempt/epoch/surface/两个 backing serial、完整 width/height/stride/source/bytes/status，以及 Root要求的真实 `presentationAlpha`；不进128B包。

### 离线数据与证据限制

CREATE_NEW在同一 private leaf 输出 meta.json、source-events.csv、finite-targets.csv、svg-cold.json、svg-warm.json、summary.json；on另输出 schema2 raw-callbacks/raw-batches全部保留前缀、失败/invalid/drop/afterEnd。必要目标216及 Setup分母独立，缺失、SourceRejected、其它真实 terminal status保留。

精确 MeasuredEnd 与 BGRA当前成功提交 tuple分别保存。原完整 BGRA保存为 equivalence.bgra，receipt含 alpha/source/stride；BGRAhash只是辅助 FNV，Root须 fresh on/off全字节与实际状态/事件/动作配对。没有 PNG伪成功、裁图/降分辨率或额外 Request/Flush/present。

summary包含真实成功目标 Up/Down owner→完成、measured interval、matching原 callback/batch CPU成本、各 stage、真 commit gap、推进/尝试/成功分开与全部状态分母。边界重叠 callback/batch 不混入timed成本；读回费用/原始尾段 retained且标afterEnd。P99不足1000为null；off全部性能time为null。掉样/invalid/缺精确end不冒完整分布或 Passed。CPU在精确MeasEnd、GPU、光学仍not collected。

svg-cold只owned_initialization；svg-warm明确scope=render_all、steady_phase_collected=false、warm/measured_phase_counters=null。未活调用AfterOwnerStopped getter，不把总量冒稳态差分。未知shape/text/light沿原B3严格资源判定，无法自然完成就保留NOT VERIFIED，不补帧/放宽Completed/关闭光影造绿。

### Root登记与共用纯测试提案

主产品待 Root登记2 Auth、Source.h/.cpp/Test.cpp与原 caller修改，最早dispatcher/DPI依Root已冻实码。Source.cpp是普通纯编译表/validator单元，可单独加入现 Headless并直接测试：
unknown scene/count；所有rows exact index/order/phase/flags/reserved/base0；逐逻辑字段hash变动；Setup/Main/Draw/Cancel动作门；错generation/ready flags/odd mapping/短整型边界/非finite anchor；完整 wire比较。不能把纯校验结果叫真实 queue/TLS/action 或 GUI通过，不把 Auth/runner链接进 Headless，也不复制正确算法。

Root A/B/C调用签名与新45AA review一致，本 writer没有新增B/SVG cache差分接口。完整 Solution type/link、strict Headless/PptCOM、Source actualdiff auth/OS清理/动作及job/partial-init生命期独立safety，之后才能跑第一真实 bounded scene和on/off，再两scene三轮Release。整体F当前NO RUN。

### 实际静态检查与最终源码身份

已做严格UTF8/无BOM/全CRLF/no lone LF/no trailing whitespace、三新源词法括号平衡、真实符号/Window/HiMsg queue与callback语义、许可前业务写、Source TLS/flags release-acquire、default/off clock门、alljoin/control/I/O顺序与文件所有权检查。新Source没有 SendInput/SetCursorPos/TerminateThread/detach、目标 setter或追加Request。没有执行产品lint/typecheck/build/tests/EXE/GUI；Root唯一build/run，独立整体F安全门尚未通过。按分工未执行git diff，Root仍须核actualdiff编码/无无关大面积变化。

| 文件 | bytes / lines | SHA256 |
| --- | --- | --- |
| Bar.Presentation.Source.h | 5168 / 93 | 75BBFB73910CA4635B1E297D13161DD5C2FE40AD899E5BC5B6EE358C2405A4A0 |
| Bar.Presentation.Source.cpp | 8064 / 164 | 3CE4506F911CCBC5AE366169B94119AC5DEBB47AE905B5CC977A1CF46F2088E5 |
| Bar.Presentation.Test.cpp | 81023 / 1355 | 8BAB2A1A1C36C136CFB87118ED06F1AAA00C0EF1AE8C6E873BE8EC8A3F0E5A6E |
| Bar.Interaction.cpp | 267471 / 6975 | B45F5849B508B835509E1D8AEAAC74DECA6ACCF88CE8669E5D848029855AD076 |
| Bar.Button.cpp | 52059 / 1416 | 97F4CCCA57515679331BEF700DAE8DE75014A5D014C42D145DBF4579F78363DA |

这是源码准备身份，不是编译表运行hash或HF。剩余真实类型/链接、所有动作/输入/ready/config boundary/全owner寿命、完整图/on-off等价、216目标/资源及性能验证需 Root与独立checker完成；Win7、鼠标光/FineDial/Settings、其余family/HC-H2/发布仍未验证。本 writer PATCH_READY后停写，修补需Root交回或明确接管。

## 2026-10-01 Root 构建首错的单行类型修补

WRITE_ALLOWED_F_COMPILE_FIX；仅 Test.cpp 的 wire message 聚合实参显式类型化及本节。实际 Source.h 顺序为 uintptr_t hwnd、uint32_t message/buttons/modifiers/category；Test.cpp405第二实参的 WM_LBUTTONDOWN/UP 条件表达式宏为 int。Root第二次完整 Solution 的唯一首错 C2397 对应该 int→uint32_t list initialization。

最小修改为 static_cast<std::uint32_t>(row.phase == 1 ? WM_LBUTTONDOWN : WM_LBUTTONUP)，其余聚合字段、WM 常量、flags、clock、actions与所有行为逐字保留。将新表达式逆替换后，与修改前完整文本严格相等；编码无BOM UTF8/CRLF保持。未修改其它四源、Root文件或工程，未 Build/run/Git/GUI/递归，类型显式化尚未经新构建，不能记编译或整体F PASS。

Test.cpp 最终冻结 SHA256：ECDB68FF6B58F3D1D5A9B19D8EF88876EA0A26B896B131AE860EE34A0C8FD23F（81051B，1355行）。其余四源沿上节身份。本 writer已再停写，交Root新的完整Solution构建与正在进行的独立actualdiff safety；整体F仍NO RUN。

## 2026-10-01 raw invalid Bar stamp 完整性门补齐

WRITE_ALLOWED_F_RAW_GATE；只改 Test.cpp1134及本节，其它四源与Root files冻结。已查看live agent列表，除Root、Draw3 host writer、Failed cleanup checker外没有其它当前Source writer；并在写前核Test起点为ECDB68FF…C8FD23F。

现 clocks-on raw完整性门仅增加 || raw->invalidBarCommitStamps。RawCaptureReport实际 uint64_t 专计字段已在 RenderPipeline.cppm238核实；有无效真 Bar stamp 即保留失败，不能因216目标/图成立仍Passed。普通unverified/failed没有扩大为一概禁止，所有原raw/失败前缀与nullable字段、输出统计保持；没有筛数据、改SDK、fakeSource或新增DTO/helper。

单行逆替换后完整文件严格等于起点；UTF8无BOM/CRLF/no trailing whitespace静态核通过。最终Test SHA256 108E2B29B677A6CBC2F7DD597C774ECAE931C1E3BCBEE06855FA5DEFE3324D20（81082B，1355行）。本writer停写。Root报告上一版本第三次完整Build0/core与U3smoke0/PptCOM三轮0属于旧源证据，不能移用于此新增门；本writer未build/run/Git/GUI/递归。已有B04/B1 actual invalidstamp producer测试不等于本consumer门的故障GUI验收，本门仍未动态验证。整体F须Root新fullBuild与SourceSpecific独立safety后才真实运行，当前NO RUN。
