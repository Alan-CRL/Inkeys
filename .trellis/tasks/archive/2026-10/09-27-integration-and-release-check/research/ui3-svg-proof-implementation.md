# UI3 B3 SVG proof implementation

日期：2026-10-01（Asia/Shanghai）。Active task：`.trellis/tasks/09-27-integration-and-release-check`。Writer：`ui3_svg_proof_impl`。

## 当前状态与写入边界

**READ_ONLY_PREPARE。** 本轮只创建本报告；没有写产品/测试源，没有构建、运行测试、EXE、GUI、性能、Computer Use、Git 暂存/提交/归档或递归派发。Root 正在 B222 合法前提的红→绿，暂时拥有 Probe.h/RenderLoop/tests；本报告的 source hashes 是读取时快照，后续接管前重新核当前源，不能覆盖 Root 的修补。

适用 R2 合同完整 439 行，SHA256 `B32BF41F5EF31F9B8DF6B0488D0F8A6074839800A8CF9B1A7C8E7213054D1097`；独立 design-review 的 B3 首 SVG 计数与 NoRetainedVerified 保守 paint 为 GREEN_DESIGN，尚未产生本 B3 的动态证据。已读实际 AGENTS/workflow、G PRD/design/implement/manifest、native-desktop rendering/UI、diagnostics、C++、resources、build 与 reuse 指南，并核 B2-P2 implementation/code-review、F066 root 报告和当前生产接口。

未来写入只在 Root 明确 WRITE_ALLOWED 后开始：Bar.PresentationProbe.h/.cpp、Bar.UI.cppm/.cpp、Bar.Rendering.cpp、Bar.RenderLoop.cpp、现有 Bar.EraserAttribute.Test.cpp、本报告。Root 唯一构建/运行/GUI 槽；其它模块/头/工程/spec/账本若确有必要先给 Root 精确依赖。没有新增通用 registry、遥测模块、CLI、auth/bootstrap/source 或其它 family。

## 已存在的真实生产接缝

| 原符号（当前行仅定位） | 已确认事实与 B3 最小旁挂位置 |
| --- | --- |
| UI.cpp::ApplyContentDirect 393；AdvanceContentTransition 332/354/365；CalcWH 527 | 真正 value replacement 在 Initialization/实际 contentCommitted；SetTar 本身只改变 target。CalcWH 和 transition 的 loadFromData 也要计真实 parse，初始化 summary 不塞进第一 callback。protected timeline 可添只读 active accessor，保留原锁/算法。 |
| UI.cpp::CacheBitmap 400–489 | 原 GetVal/当前可选色/请求尺寸 → loadFromData → renderToBitmap → CreateBitmap → newBitmap/cW/cH/cColor 成功提交。只在最终真实成功后发布 typed ready proof；三个失败边界分别留 parse/raster/upload 计数与失败。 |
| UI.cpp::ResetCache 387 | 真实 bitmap 才扣 readyEntries/bytes 并记 invalidation；释放仍由原资源 owner 执行。per-object 单 bitmap 无 capacity eviction，报 N/A。 |
| Rendering.cpp::Svg 2723 | 原 needUpdate 是 lookup miss；无需更新且已知有效缓存才 hit。颜色更新失败 return false；尺寸质量刷新失败可继续旧 bitmap，只能 QualityFallback。保持条件求值、画质、缓存阈值与返回行为。最终 DrawBitmap CPU submission 单独计数/计时。 |
| Rendering.cpp::RecreateDeviceResources / DiscardDeviceDependentCaches 118/167 | 新资源三项成功后替换；ResetCache 覆盖 svgMap 与 registered button icons。epoch 来自真正 deviceGeneration；不能由 CPU 架构猜值。 |
| Rendering.cpp::PushFrameDirtyClip / PopFrameDirtyClip 222/242 | 在实际 Push 时读取当时 transform，把真正 clip 转成 backing 坐标；不是将 dirty rect 直接当任意 clip。已有 nested clip 要同步观测；不支持/nonrect/未知栈即 Unknown。 |
| RenderLoop.cpp::ChangeString；svgMap 5669 与 registered icon 5996 原 Advance 循环 | 真实 ApplyTar 之后递增该对象 valueRevision；不改通用 Word/String 框架，不二次扫描全图。名单只在固定 Main/Draw 域顺手收集，当前可见/target 可能可见/本目标真实消失的 tag 都保留。 |
| RenderLoop.cpp 9339 / 10023 / 10033 / 10040 | 现 B2 在原资源成功后旁挂 surfaceSerial，在 BeginDraw 前 SettleCandidate，然后原 BeginDraw/Push/Clear。B3 在可能改写 backing 前推进 mutation serial、使旧 paint 未知；surface/target/epoch 真实失效时推进 invalidation serial。失败/deferred也不能恢复旧成功像素假设。 |
| RenderLoop.cpp 12708 / 12847 / 13072 | 原 Pop clip、GetDC/ULW/ReleaseDC/EndDraw、CompleteAttempt/B1 stamp 与几何发布保持原位。资源 finalize 必须在真实绘制证据已经齐全后重锁候选的资源字段，再由完整事务成功决定提交；只晚调用 ObserveResources 会沿用已经 Settle 的旧 candidate，必须修正这一接缝。 |
| Probe.cpp::ObserveResources / SettleCandidate / CompleteAttempt 477/481/535 | 当前 ObserveResources 只存 resource_；Settle 才写内部 candidate；Complete 核供给候选一致性。增加固定身份的资源 finalize，不重新消费业务/推进动画，不新增 clock，不重复 layoutSettled 分母；无 producer 保持原 {} 拒证。 |
| EraserAttribute.Test.cpp::RunEraserAttributeOffscreenTest 85 | 现 --bar-eraser-offscreen-test 已有真实 UI D2D/WARP、Svg/缓存、独立 device epoch 与 BGRA readback；在同入口增有限断言，不能说它运行了主栏 ULW 或实际两个 scene。 |

## 最小实现顺序与数据形状

1. Probe.h/.cpp 增 R2 typed Ui3SvgBitmapProof/DrawObservation/Counters、固定 256 slot 的首 family owner 与 nullable TLS SvgObservationScope。tag 先核 namespace/ordinal，再线性固定映射；0x10000+svg enum、0x20000+稳定 registered ordinal 不作为数组索引。同对象重复引用只绑定一次、tag 冲突/257th/计数与 checked bytes 溢出使证明 invalid，不扩容。
2. Tag baseline 只在当前 init owner 的已知 value 构建已经发生且没有 bitmap 时建立 valueRevision=1。未观察初始化、已有未知 bitmap、未受控 value 写、owner/epoch冲突不追认证。以后真实 value replacement 才递增，SetTar/Reset 不伪造新语义 revision。普通无 scope marker立即返回。
3. CacheBitmap/CalcWH/transition 三个原 parse 点与原 raster/upload、最终缓存提交、Reset 旁挂数值计数。新 timer 必须 nonnull scope 且 clocksEnabled 才读钟；capture-off 仍可有身份 proof，但四个 Ms 不采时，普通 observer absent无 clock/分配/新资源操作。
4. 在当前原 svgMap/button animation 遍历收集本 candidate 必需 tags。绘制点锁存真实 value/color mask+RGB、cW/cH、raster pixel size、DPI/epoch/surface、dest/effective transform、tarPct/window alpha、真正 clip 与 expected visible bounds。没有实际 Draw 的 required tag 保留 Unverified；unknown retained不会用 readyEntries 代替。
5. 首版 NoRetainedVerified。只认证当前完整 coverage 的 DrawnVerified，或当前实际 Clear 完全覆盖上次已知可见 bounds（含边界）且最终 invisible 的 HiddenExpected。partial/empty/未知 clip、几何/opacity 不匹配、质量 fallback/缺图/未知覆盖写和失败或 deferred backing 都保持 unverified。新 tag 初始就是 hidden 且没有旧 bounds，不能只凭 enable=false认证。
6. 后续 target 写入必须保守使被覆盖的 staged proof 未知：复用实际 Shape/Superellipse/Png/Word 与 RenderLoop 直接绘制边界；能证明 bounds时按有效 transform/clip 求交，不能证明时作 Unknown。仅观察当前 backing context，不把 mask 专用 DC 绘制混入。无法完整追踪的分支允许 Unverified，不能忽略后续覆盖来制造 full proof。
7. 在原绘制/clip pop后生成本 attempt 的 typed summary；finalize只校验已锁存 revision/epoch/surface/attempt/viewport/layout，不重读 business 倒填。完整事务失败只清 staged，不恢复旧 paint；真正成功后提交 known visible bounds和有限资源结果。无 B3 producer 路径仍 ObserveResources({})；真实 required>0 且全部 full proof 才可能完成。
8. sizeof/checked预算包括 R callback+batch、publication+observer、256 SVG slots+source预留；本 B3固定对象上限另 static_assert。合并上限不超过64MiB，不在渲染中分配表。当前没有 F installer，未来 init/render handoff与撤 scope须走 owners 真join；不在此阶段添加安装/bootstrap。

## RED→GREEN 的最小归因验证

先以同实际 CacheBitmap/Svg/clip/clear/Complete入口增加断言并有限 RED stub；Root 完整新 Debug|ARM64构建，核真实新 root/Build产物及 source hash，再运行 --bar-eraser-offscreen-test取得明确 B3 failures。不能把非法 publication、旧 EXE、D2D前提失败或其余 Eraser 失败冒称 B3 红。Root确认 RED后再给 GREEN 写许可；期望与前提保持。

- first creation 与 reuse：同 scope 已知 init/tag、真实 parse/raster/upload/create/ready bytes和下帧 lookup hit/draw；旧未知 bitmap bind 不认证。
- value/color/尺寸/实际 DPI与epoch：真实 content commit revision；色mask/两个RGB；原尺寸阈值/质量 fallback；Reset/成功 replacement；独立 epoch资源重建。资源存在和 API 次数不等于已画 full target。
- parse/raster/upload失败：优先自然有限失败输入；需要受控 failure seam时先冻结具体 fault和实际调用边界，不能大尺寸/OOM随机试错，不把 synthetic injection说成自然 OS failure。失败不能ready，不伪造upload success或改变普通路径。
- full/partial/empty/unknown clip、真正旋转/平移、不同 tarPct/windowAlpha、后续 target写覆盖、实际 hidden Clear、failed/deferred backing之后留旧图：核 typed outcomes与所有 required/failed/unverified/firstTag/reason分母；没有 RetainedVerified。
- 实际 producer 的 full proof 接 B2之后，先原 layout settled且成功软件事务但无/partial/missing proof拒完成，再同真实 CacheBitmap/Svg full coverage后 finalize/Complete成功；candidate identity mismatch/zero required仍拒证，不复制生产判定。
- capture-off/默认 null：无详细钟/raw/新 Request/fullDirty/draw/Flush；独立 on/off相同原绘制后 BGRA逐字等价。BGRA读取只独立验证，不塞进计时段。

后续 Root运行严格 Headless、完整Solution/PptCOM与独立checker；F真实 owned input/GUI/两个scene和三轮Release、CPU/GPU/光学/Win7/HC-H2仍是后续门。本worker不会运行或据旧B1/P1/P2记录本 B3 PASS。

## 读取时源码身份与格式

全部允许源为严格UTF-8/CRLF；EraserAttribute.Test.cpp保留原UTF-8 BOM。R2合同自身LF保持。接管前重新核Probe.h/RenderLoop的Root F066变化。

| 文件 | 读取时 SHA256 |
| --- | --- |
| Bar.PresentationProbe.h | AD1F8485C3AB5D039AF6D2E5561D5A33189EC8D74A838EF0E80FF1FCCC918B93 |
| Bar.PresentationProbe.cpp | 9483172BE5130A18B8757A2A40519ABB3A07279AF973FAF90EAA5941DE1686C2 |
| Bar.UI.cppm | F49260455458D566ECD1A32A07DCB0CCE004A06A7060E0E7DF97424E6FC480FE |
| Bar.UI.cpp | B03EA591A21B3E614A88BFB1E1C8A1FE24C1CA781D3647176227AC63FF1551DA |
| Bar.Rendering.cpp | A9D84D0FD9DC3526830C91AFAAE5C1FBE8BD4BB8BBDB7268F0CEF48423DDE66A |
| Bar.RenderLoop.cpp | F59F9A59BEC13C773C3E4CB9A91AC902EE1555F584790D13806D783AEA8228A8 |
| Bar.EraserAttribute.Test.cpp | B20F3D3A73EFFB292EDA1382AC15C7BDF515E5CF92FE4F1C012278398050295D |

## 已检/未检与依赖

已做读取/rg调用链、R2 SHA、源编码换行与 source hash检查。未做本 B3 lint/typecheck/build/测试/GUI；原因是 Root 的 READ_ONLY_PREPARE与唯一 build/run槽，以及 B3尚不存在。没有请求扩大module或Header写入范围；Rendering.cppm可保持，因为clip/state numeric scope放现Probe头及.cpp，并在已有实际方法体旁挂。未来 F必须在 auth init scope内实际创建/绑定tags，当前不做这一安装，不自动给正常产品观察资格。

## 2026-10-01 SAFE_BUILD_CHECKPOINT — B3 RED PATCH_READY

Root已授权 WRITE_ALLOWED_B3_RED；接管输入AD1F/F59F/9483/B20F已核，未改B222合法66、Snapshot/MarkConsumed前置、PNG映射或namespace限定。当前七源全部停止写入，可供Root与C3诊断统一完整Solution构建。这里说编译接口/定义已齐，不声称尚未运行的MSBuild通过。

当前实际改动：Probe头/cpp新增固定256槽、tag映射、init/render独立计数、nullable TLS与门控CPU timer、typed bitmap/draw/clip/Clear来源。UI在原三个parse、raster、upload及成功缓存提交/Reset和实际value替换点旁挂；已有未知bitmap不追认，拷贝对象不继承原对象初始化资格。Rendering保留原needUpdate/质量fallback/单次DrawBitmap，观察真实effective transform/clip/opacity；原其它18个D2D draw/fill和RenderLoop29个直接draw/fill以逗号表达式旁挂Unknown写，不改变原if条件或原API次数。未知后续覆盖仅保守拒证；本批未做所有shape/light/text的精确覆盖归因。

RenderLoop在原Advance遍历顺手收集required，scope只在已存在private observer附加B3 owner时启用。原Settle仍在Draw前，新增FinalizeResources在真实绘制结束后只补资源字段；不重消费、读取新business、计layoutSettled或新增clock。无B3 pointer仍ObserveResources({})。真正Complete失败/早退不恢复retained，首版无RetainedVerified；没有F installer/auth/source/bootstrap/主GUI或其它family。

**唯一paint RED桩是 Ui3SvgProbe::FinishDrawing：已观察真实bitmap/geometry/clip，但full/hidden认证尚未实现，所以required保守unverified。** Cache/API计数、tag绑定和实际绘制都是实码前提，不把绑定失败、非法publication或空图当RED。

最小现断言：
- B300所有前提应PASS：真实epoch、96x96 D2D target、owned初始化/tag/no-old-cache、真正CreateBitmap/Svg/EndDraw、非空BGRA；finite bridge是合法MainFold flags65→64，stable Snapshot/MarkConsumed/layout为显式前提。
- B301应PASS：实际first parse/raster/upload/create/lookup/draw各1、readyEntries1/4096bytes、初始化parse另1。
- **应红仅 B302、B303（预期总2项）：** B302要求真实full clip后typed full paint；B303把这一真实SVG summary在Draw后finalize同一合法candidate，要求CompletedLayoutAndSvg。RED桩只产verified0，资源未证不得完成。
- B304应PASS：finalize不重复layoutSettled；B305应PASS：真正partial dirty clip coverage为Partial且verified0；B306应PASS：既有bitmap两次真实reuse、on/off门控clock和同一BGRA像素；它只是单像素smoke，未证明整张BGRA等价。B307应PASS：真实sizeof的32768默认合并预算<=64MiB、巨量size拒绝溢出。

Root运行时须核B300/B301前提全部成立，失败行明确B302/B303，并保留 Build/eraser-b/offscreen-results.log；若旧Eraser/设备/前提另失败，不冒称准确两项因果RED。CLI仍现有 --bar-eraser-offscreen-test，不需新purpose或工程项。Headless/PptCOM由Root串行验证；本worker未build/run/Git/GUI/ComputerUse/递归。

后续尚未写的测试/实现：真正hidden Clear、empty/unknown/nested clip、内容/颜色/尺寸/DPI/epoch与failure/deferred/覆盖谱系、未知旧bitmap/tag冲突/257th、自然parse和受限raster/upload失败、默认null测量以及整张BGRA等价。当前仅最小RED checkpoint，不宣称完整B3 GREEN或真实两scene。Root先验证本批并继续C3诊断后再决定何时交回GREEN/补强。

静态已核：全部新声明有合法同名定义，真实callers全限定namespace；UTF8解码通过，七源LF-only均0，Eraser测试原BOM保留；没有新Request/fullDirty/GPUFlush或第二套算法。依Root禁止Git指令未运行git diff/check，请Root在其总检查中审七源diff。以下为冻结身份：

| RED checkpoint source | SHA256 |
| --- | --- |
| Bar.PresentationProbe.h | 5E9D3B266934A8AF035968DFB2A367DCB9EAE9D1223A900F4F676D112B1F9749 |
| Bar.PresentationProbe.cpp | FF098371E5502B6B47443283C3313A84DFBDBC0342B7304FE5329861566DF39F |
| Bar.UI.cppm | 55E4A2369884F7F151AFD6196B56D83F9E0D84BF2233D59A2FA02653A7A51205 |
| Bar.UI.cpp | 24525B2E3A425E8A62B348F7907E8B9E620098C90A0BECCC9199F809A0007702 |
| Bar.Rendering.cpp | 8A69FCD6E2CFCD9CF5747B890AD22C01D274E67C3E32E10459AEFE40ADEABE2C |
| Bar.RenderLoop.cpp | DD64B44553097DB0C323DED3D3436045B38DFDC176DA0D2EB6D657531E9CAF1F |
| Bar.EraserAttribute.Test.cpp | 6A37845003BAE429497EB984554785E580CEF8EF7E7F7483F071138CBE3775B4 |

## 2026-10-01 GREEN执行方案（Root WRITE_ALLOWED）

Root有效RED：完整c3b-index-probes-b3-red Debug|ARM64 exit0/159warn/0error；offscreen pid16476自然1，精确B302/B303 failures2，B300/301/304–307及旧用例前提均绿。独立ui3-svg-proof-code-review SHA57380013ED466DD89156F35E284BA2B88969E1AF8C4523C81A6300CEABA65861为CLEAR_RED_SCOPE/RED_VALID，完整GREEN仍R1/R2/R3三NEEDS。Root随后c3b-private-log-b3-red完整Build0，明确交回原七源GREEN独占写；本worker没有运行以上命令。

最小修补按实码顺序：
1. R1：record追加failed/unverified/firstReason并补现firstTag赋值；StoreOutcome和停后Absorb保全同目标资源分母。NoChange只搬原goal的引用summary/reusedRevision，时间与新commit仍无效。sizeof和64MiB预算以新类型重新计算；unbound/mapping/cap有明确非零reason。
2. R2：原ApplyDisplayTransition已有同帧targetDisplay.dpi→activeDisplayDpi锁存，利用该原值和同Submit已验consumed signature一致性，给纯candidate增加renderDpi。initial pub0仍不造accepted目标。BeginBackingWrite用锁存值，0只拒证、不96兜底、不反推zoom或在完成点读最新business；原cache reuse/needUpdate保持。测试用initial publication0的真实CacheBitmap生成DPI proof，随后合法目标复用同bitmap且create/upload不增。
3. R3：在原Svg写点由实际dest/effective matrix/clip/viewport算写域，后SVG使先前相交/未知域staged proof失效，可证不相交保持。failed/deferred的backing已可能改写新位置，所有受保护旧Hidden lineage失效；只有原真实full backing Clear并成功事务或真实surface重建才恢复，partial旧bounds清除不能追认证。成功帧旧bounds保守union，不丢前次未清完整区域。
4. classifier实际验证bitmap value/color mask+RGB/请求及pixel尺寸/DPI/epoch/surface、有限matrix/opacity/alpha、full有效coverage及未后覆写，才给DrawnVerified；Hidden必须真正Clear覆盖已知旧bounds、无未知写谱系/之后覆盖且成功提交。retained/partial/empty/未重画/未知一律未证；无新Request/fullDirty/draw/Flush或quality量化。
5. 故障仅explicit离屏probe默认None：自然坏SVG parse；真实renderToBitmap之后受限reject其result（synthetic consumer fault，非自然OOM）；真实CreateBitmap固定非法alpha参数，验证真正HRESULT失败。数量/尺寸固定小，不试探大分配、不改普通API参数。所有fault对外明确injected，默认null/captureoff clock门不变。

分批：先实现R1/R2/R3+full/hidden保守classifier，保持B302/B303及其合法前提逐字；同现offscreen块补ledger/initial→reuse/真正A-B相交与不相交/hidden失败谱系。然后补empty/unknown/nestedclip、transform/opacity/content/color/size/DPI/真实epoch、自然/受限fault、unknown旧cache/tag/257th、整张BGRA on/off与预算。只七源和本报告；无F/Window/其它family框架。最后全部停写交SHA/字节/未测，由Root独占新Build/offscreen/Headless/PptCOM并独立review；未取得实际新结果不记GREEN。

## GREEN源码 PATCH_READY（尚未新Build/Run）

七源已停止写入。完整B3 GREEN源码与R1/R2/R3机械修补已做，Root独占新Build/运行/独立review后才能记动态GREEN。本worker没有执行Build、EXE、GUI、Git、Computer Use或递归。

R1 actual：record新增failedSvg/unverifiedSvg/firstUnverifiedReason，StoreOutcome完整复制包括原firstTag；Absorb在NoChange分支前搬同goal的resource summary及失败flags，仍不造新commit时间。B331用真实full/partial/no-draw/自然parse/实际upload-failure summary经同production observer/Complete/Seal/Absorb检分母和tag/reason、NoChange引用。新增record/observer sizeof由offscreen输出并参与现Fits预算，不凭旧sizeof外推。

R2 actual：MarkConsumed仅在validated signature消费成功后锁存candidate.renderDpi；原Submit对同帧ApplyDisplayTransition既有activeDisplayDpi核一致性，BeginBackingWrite不再读accepted.signature.dpi。initial pub0也带同一纯值DPI，仍无accepted step/revision，不96兜底、不从zoom反推、不额外upload。B332/B333同真实CacheBitmap/Svg及initial publication0/纯commit ready→合法goal→同bitmap reuse；DPI为显式192目标参数，未启动OS显示变化/真实Bar HWND，实际production caller来源靠代码审查与完整编译。B334/B335变DPI/原zoom尺寸政策及DPI不匹配且无新增upload拒证。

R3 actual：Svg实际mapped dest写域（含AA边界/真实clip）与其它staged槽求交；相交/无法证明失效，可证不相交保持。unrequired/unknown SVG写也走Unknown而不偷放行A。singular/nonfinite transform拒证。failed/deferred使Hidden lineage未知，不用旧bounds追认；成功帧未清完整旧域时旧bounds保守union，只有真实full backing Clear并成功事务或真实surface重建恢复。B336/B337真正两SVG不相交/相交；B319–321真D2D写过新位置再显式Complete(false)模型失败/deferred、旧bounds小Clear留实际新像素不能Hidden、full backing Clear才修复。它们不是实机ULW故障。

classifier只Current Full DrawnVerified/真Clear HiddenExpected，核value/color mask+RGB/request-pixel尺寸/DPI/epoch/surface/finite matrix/final opacity/windowAlpha/current clip/full coverage/no later overwrite；未重画/partial/empty/unknown/旧bitmap/unknown lineage保留未证，没有RetainedVerified。Hidden无Draw，raw used只记录当前纯值意图且ready=false，原bitmap不冒充提交。所有new timers/default-null/capoff原门保留，原资源/动画/pacing/dirty/DrawBitmap/四API顺序不改，不加Request/fullDirty/Flush/质量量化。

Root所需F最小接口 CompletedRevision() acquire getter已追加h，仅读现atomic receipt。B303断言真实resource transaction前0/完整目标后对应revision；B331 partial/missing/API故障不发布receipt。NoteCompletedGoal门原位，不读活plain rows，不新增F/bootstrap/source。

现新增B310–B341正式断言涵盖empty/absent/nestedclip、no-retained/no-draw、rotation/translation/halfOpacity/windowAlpha0、未知后写、Hidden真Clear与fail/defer新区域、color/content/size、全96x96 BGRA on/off、default-null真实draw/no timer、自然parse/受限raster/upload及原QualityFallback、ledger/nochange、initial0+DPI+reuse、真实独立WARP/D2D epoch（局部SVG调用原ResetCache显式释放）、unknown旧cache、256/257th/冲突/巨大及unknown enum ordinal、预算。受限raster是实际raster后reject result；upload为小32x32真实CreateBitmap非法alphaHRESULT，均explicit injected、fault默认None，不是自然OOM/provider失败。原B302/B303期望和合法前提没有降低。

范围限制：其它Shape/PNG/Word/lighting及direct target写仍保守Unknown，未认证其它family，可能使实际两scene未证；F/主GUI/全实际ULW目标/源停止整链/三Release轮、CPU/GPU/光学/Win7/HC-H2都未测。更严格paint原因不通过补帧/强制全脏改变产品来换PASS。没有性能收益数字。

静态：新符号声明/定义及调用位置、plain DTO/固定256表、tag实际svg enum尾值31的static_assert、checked64MiB预算、no-change时间、Clock门已读核。全部七源严格UTF8/CRLF、LF-only=0；测试BOM保持。Git diff gate由Root总复审执行，本worker按禁止Git未运行。冻结源如下：

| GREEN source | bytes | SHA256 |
| --- | ---: | --- |
| Bar.PresentationProbe.h | 25061 | E2DE55BE5A73C7CB7AC5DCFCFA3488D3E52F54B89D984D79AB8A72A4BAE570F7 |
| Bar.PresentationProbe.cpp | 64421 | FD276C005491B4BB00DB28FDB4766B118C525273B1AC1A2202E4C9E4DE3201EE |
| Bar.UI.cppm | 15345 | 55E4A2369884F7F151AFD6196B56D83F9E0D84BF2233D59A2FA02653A7A51205 |
| Bar.UI.cpp | 37676 | 10B29A341D99C731E2533134B1C53AAA71927B93182918BFBEEDBE8F4707CF27 |
| Bar.Rendering.cpp | 119586 | 8A69FCD6E2CFCD9CF5747B890AD22C01D274E67C3E32E10459AEFE40ADEABE2C |
| Bar.RenderLoop.cpp | 632036 | AB1E5B8B5CEDC58F90961EEBB1B3632E2F93A71912DA5A59DB718A71CA89474F |
| Bar.EraserAttribute.Test.cpp | 101919 | 93616F3856FE4E9DCFF2EF94EEE527D24F735B01B82BEFF15B799D9F7D5BD096 |

## H1因果RED PATCH_READY（仅新增测试，生产行为未修）

Root候选已有完整Build0/155warning/0error、offscreen24948自然0/全部现断言、Headless31860/parked29972/PptCOM1784均0；它们不覆盖新H1。独立报告BBC6FCCFF8E23F8235998EDD284D5771E031E700E63A810B11E87DFCFD1BB18A确认原R1/R2/直接SVG覆盖已闭，但ObserveClear的ever fullBackingCleared_在后UnknownWrite后仍恢复Hidden lineage，完整Green仍NEEDS。

Root WRITE_ALLOWED_H1_RED仅Probe.cpp（必要h）/现Eraser测试/本报告，本批实际只写测试和本报告。Probe.h E2DE55BE…E570F7、Probe.cpp FD276C00…3201EE保持逐字旧行为；UI/Rendering/RenderLoop均未改。新增H100/H101/H102在已过B3真实96x96 block内用原production probe/API，不创建窗口、不改普通API或计量时钟。

真实三帧：seed真Svg/full clip/EndDraw得到已知14..50旧bounds；隐藏A后真full backing Clear，ObserveUnknownWrite→真实dc.DrawBitmap同一旧bitmap到64..80外域，成功EndDraw/Complete(true)但本帧Unverified；下一帧只真Clear0..52旧域，72,72读回alpha仍非零，必须禁止HiddenExpected。H100分别核seed、source cache、真D2D提交、unverified、外域真实像素和小Clear仍残留，前提失败不冒称因果RED。**应新红仅H101一项**；H102独立反向正确序unknown replay→最后fullClear→成功完成→小Clear应恢复并72,72为0。旧B302/B303与其它期望不降。

Eraser测试冻结SHA256 `12731E2341AC776FA706AF820C672787567D7A70755435F5530899C1DD9B975F`，105211bytes、UTF8 BOM/CRLF/LF-only0。七源现在均停写，Root可新完整Solution/offscreen取得H101单红；本worker未build/run/Git/GUI/Computer Use/递归。

待真实RED后最小GREEN：UnknownWrite撤销此前full-clear恢复资格；只有它之后的真正full backing Clear才重建资格。tracked SVG的unknown写域也须沿同顺序传播unknown lineage；已知partial写按实际bounds保持保守、fail/deferred仍不能恢复旧bounds。候选record/initial DPI/getter/Clock/null原门保留，不改变正常绘制/quality/Request/fullDirty/Flush，不扩大Retained。新结果只证明观察资格顺序，不冒称确认用户视觉bug或性能提升。

## H1 GREEN最小修补 PATCH_READY（待Root新复验）

Root实际ui3-b3-h1-red完整Debug|ARM64 exit0、offscreen pid16204自然1，精确仅H101 FAIL，H100/H102及旧断言均绿，确认真实外域像素/旧bounds前提。Root据此授权H1 GREEN。

只修改Bar.PresentationProbe.cpp两点（新增3代码行+中文注释）：ObserveUnknownWrite在原context/drawing验证后撤销fullBackingCleared_，所以早先full Clear不能在后来Unknown之后恢复资格；ObserveDraw的writeKnown=false分支调用同一UnknownWrite并补当次mutation snapshot，tracked SVG未知域与unrequired/unbound SVG、其它真实unknown target写共享顺序。ObserveClear原真实full覆盖仍可在该Unknown之后重新赋资格。已知partial/相交按原数值域保守判断，Complete(false)仍失效；成功Complete不再用过期full-clear资格抹掉后来unknown。

没有修改绘制/资源API/dirty/Request/Flush/quality/pacing/clock门。没有新增registry/retained/业务writer。h/测试及其它六Bar源保持冻结身份，H100/H101/H102/B302/B303/初始DPI/record/getter断言逐字未改。未知SVG多记单调mutation见证只为观察版本，不算实际API次数或性能收益。

静态已核helper全部unknown分支共用、不明clip/transform不能偷保恢复资格、Clear/unknown顺序与failed/deferred/partial不同分类；UTF8/CRLF/bareLF0，cpp无BOM，测试原BOM。本worker未build/run/Git/GUI/ComputerUse/递归，Root唯一槽须新完整Solution/offscreen+适用Headless/PptCOM和独立增量复审；尚未写动态GREEN，F/真Bar/两scene/性能/Win7仍未验证。

冻结：
- Probe.cpp 64632bytes，SHA256 `EB99F2A10C13B337408382C567C33FA8E898514FC53B2F3EC8BB2C4D3C2FF4B9`。
- Probe.h保持25061bytes，`E2DE55BE5A73C7CB7AC5DCFCFA3488D3E52F54B89D984D79AB8A72A4BAE570F7`。
- Eraser测试保持105211bytes，`12731E2341AC776FA706AF820C672787567D7A70755435F5530899C1DD9B975F`。

现在全部允许源停写交Root复验。此修补只修观察资格错误，不声称已复现用户可见bug或带来性能提升。
