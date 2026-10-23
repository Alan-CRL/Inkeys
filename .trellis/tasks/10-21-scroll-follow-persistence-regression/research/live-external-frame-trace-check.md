# 实际Tracker输入有限录制：独立审查

日期：2026-10-23。checker只读源码/原始结果，不构建、不运行CLI/GUI；root独占执行。

## 录制前实际失败证据

`external-46cc7129b8cd4621b8a47d1d8842a840` / ARM64 EXE29BB15DD：receipt exit1/9110ms，真实wheel1，独立CSS100×DPR2=200px。frames9–11连续InconsistentTranslation，reference8，inliers134、fbValid178、pointRatio.752809、survival.611684、occupied35、spatialRatio.611565，连续reject1/2/3后Paused。

原Tracker第一道一致性if为 inliers<40 || spatial<.65 || survival<.35。此run点数/存活均过，唯一失败项明确为空间比例.611565<.65，尚未执行Coverage和后续锚点一致性检查；前面的NonTranslation/Ambiguous没有先拒绝。不把点数75.3%冒充空间通过，也不以此给旧5a936…无这些字段的日志补写精确原因。

Guest750ddd…同200px一次成功（140+60采样、viewport200.000061）保留为限定PASS；新改变仅verbose日志，不是已修复。实际Mag拒绝帧尚无图像证据，捕获质量/API成功与固定marker不能证明该帧逐像素正确。

## 已审合同 / 等待实现

已读本任务design.md与implement.md新“实际Tracker输入的有限只读录制合同”。静态要求：

- 仅显式runner选项/有限env、严格external早退harness、自建Guest身份/安全门；默认未启用，宏0无新引用或产品API。
- 观察同一个const MotionFrame及真实result；包括从实际Initialize开始的每个构造输入及未调用等待Tracking阶段。记录明确operation（Initialize/Process/NoCall），不能仅由reason反推：Initialize可能先失败重试，NoCall默认NotInitialized的统计inputSequence可能0，但input.sequence仍真实。
- fullres Gray8必须与生产COLOR_BGRA2GRAY等价，不能先半分辨率再让原Tracker重复缩小。真实seq/time/ROI/session/phase及Stats完整保存；未调用帧回放也不能偷偷调用Process。
- at most64帧、像素内存≤384MiB，溢出/漏帧/异常明确incomplete并使显式诊断失败。同步callback只做有界内存转换/复制，无PGM/JSON/磁盘I/O，无生产FIFO；开销另计不当正常FPS。
- 原Stop/drain/线程join后保存CreateNew PGM/有限JSON；不持有借用frame指针。shared lifetime+原子安装/卸载，不捕获Fixture指针或迟到影响新会话。
- 原cleanup会先offSignal=1、后join并关闭Display。专用保存门不能直接复用会因自身offSignal必败的CheckDesktop/CheckFixture，也不能DisplayShutdown后假设Display快照仍在；应在原drain后、DisplayShutdown前独立核安全/Edge身份/显示几何/已知区域，不重置原停止信号绕门。

当前实现尚未交付；未放行构建或GUI录制。算法、阈值、ROI、步长和实际目标保持原样，待精确diff审查。

## 追加隔离约束（root已明确批准）

trace必须在启用/每次复制前，以精确held own Edge root的有界GetWindowTextW校验nonce和当前本机实际后缀 ` - [Guest]`；缺失/本地化不匹配即unsupported fail，不伪造CDP Guest字段，不将该局部凭据当全局账户零副作用证明。

仅启动进程增加 `--disable-sync` + request日志已获root批准，保留privateprofile/Guest/CDP，不读写profile preference、registry或账户。checker只读确认[Chromium官方switch头](https://chromium.googlesource.com/chromium/src/+/HEAD/components/sync/base/command_line_switches.h)确有kDisableSync；其注释范围是Google账户同步，不能据此保证Edge155的MSA同步或OS账户行为完全停用。后续报告只能记录请求模式及本run实际观测。

## 独立回放器预审（尚无数据/运行）

root已用现有原生ARM64 cl/静态OpenCV+JsonCpp编译ignored `recorded-motion-replay/replay.cpp`，原代码SHA CC22CAAD86058BC851C2FAE326EC4CB4923BBEBC5BEE8B31B9B61610B08FD550，EXE E96CE392E6D8D8953E12244AE71D35ADFE0AB4E580AA7D26852039FB8339605D。checker读取compile.log/原cmd：已有工具链、实际BF3409 Tracker、无新工程；1条OpenCV C4864，未将编译当回放通过，checker不执行。

原回放逐帧Initialize/Process/AwaitTracking（后者默认结果不调用Tracker），完整比较4个uint64/14个int/12个double非计时Stats及结果/total；4个耗时字段忽略、浮点比较1e-6+1e-8相对界。64帧/384MiB、PGM单纯P5/完整payload/固定文件名、完整性/序号与时间门总体正确。

待修的具体差异已交root：schema skippedInputs为十进制字符串，原.asUInt64不能直接转换JsonCpp string，须Decimal；observedCount同样Decimal并与recordedCount/frames一致；若干bool/整数需严格类型检查。原loader未核ROI/source/stride/scale与逐帧稳定，也不能用sourceRect.right简单等于bufferWidth假设。

root已决定在trace metadata最小追加原Capture.Frame.geometry的sourceRect/workArea四边对象，并由与生产相同clamp推导实际ROI；其余字段sourceWidth/Height为buffer维度、originalStride/Size为原输入视图边界等以最终schema为准。仅harness metadata，不改变正式源/ROI/算法。待最终schema和回放器精确修正再审，无回放PASS。

## Trace V1 实现独立检查（构建前放行）

冻结 `frame-trace-v1.patch/manifest.json` 最终五文件实际hash/格式已独立核：Business12DDD331…/h18BAD924…/IdtMag5421ED52…/inl1EDCD7E0…/PS139EBB5F…。Tracker仍BF3409F8，原依赖/算法/ROI/门槛/步长/Target不变。

### Findings (fixed)

首版Observe误要求buffer height==主屏1920、Save又写主屏height。checker通过原FreezeCapture::IsValidGeometry确认原定格buffer高为primary.bottom-1（本机1919）；implement已仅修collector：复用原Geometry校验、原sourceRect/workArea和实际bufferclamp/stride/ROIoffset span，保存Entry实际sourceWidth/Height。没有改Mag迎合诊断。构造roi也按原height-1裁剪。

回放器的skippedInputs/observedCount字符串类型、ROI/stride完整性和strictbool问题由root修正，新源CF565438…已独立审过并放行重编译；尚无实际数据回放。

### 最终路径

- 宏内private Observer，atomic shared_ptr在RequestToggle/F0前安装；Frame真实Init/Process/AwaitTracking分支设置operation，完成原结果存储/Target/Paused/日志后才同步传const input/result。NoCall默认NotInitialized也记录，未制造输入。
- 自有duplicated Edge/runner句柄和immutable身份/窗口/显示数据，无Fixture借用指针。每次复制前后核Guest实窗nonce+[Guest]、安全桌面/进程/几何/原排除的两canvas HWND与5×5knownROI；不合格unsafe禁止图像保存。
- 64帧/384MiB先检查再分配；vector直接作为完整分辨率Gray8目标，以原COLOR_BGRA2GRAY转换，无预resize/额外半分辨率。回调仅有界内存/只读门，无压缩/文件I/O，异常/gap/超限不完整会失败。observerCopyMilliseconds包含安全核验+分配/转换，非正常性能。
- 原Stop/state/Mag/WindowService join及occupied/inflight drain后卸载；DisplayShutdown前独立Guard(false)保存CreateNew PGM/precision17≤512KiB JSON，不读自身offSignal或重置它。原productExitBeforeTraceSave独立保存；complete前缀不是产品PASS。
- 新env只接受当前strict external的32hex nonce，native/坏标记早失败；PS仅显式RecordMotionFrames设子进程env，与SetupDiagnostic互斥，默认移除继承标记。disable-sync仅已批准的独立启动请求/日志，无账户/prefs/reg操作。
- 静态筛除同harness宏块后，Business.cpp/h与pretrace文本相同（仅末尾空白），宏0无新observer引用；IdtMag只有3个原harness guard内include。默认没有observer、输入/输出语义保持。
- 按统一 `void CheckDesktop`→`static RunCase` 锚点，原native区间实际**34890B/SHA72907FD9…**逐字节不变；implement报告33617B为另取片段，已请仅改文档标签避免混淆，不是源码问题。

### Verification

静态/哈希/格式/PS AST：pass。未发现剩余源码阻断，已放行root完整ARM64构建；实际CLI/GUI录制及回放尚未运行/验证，checker不自行执行。该辅助录制会改变时序，不能拿录制run的采样性能当默认捕获性能，旧失败和一次成功均保持。

## 录制版构建、PE、CLI 与实际安全停止（2026-10-23）

checker仅读取root已执行结果，无构建/新CLI/GUI/安全绕过。完整solution ARM64 `motion-tracker-build-external-frame-trace-1023*` exit0、外层27.351s（MSBuild内部26.92s）；x64 `solution-build-x64-external-frame-trace-1023*` exit0、外层25.809s（内部25.52s）。两份原log各0errors/19warnings；诊断实际位置仅既有IdtMain.cpp(3117) C4244及hashlib++三处C4267，重复展开/汇总不能算19个新问题。没有为warning改无关源。

当前binary独立SHA256：ARM64 **B7B0E6553808F7BF05D1F048CD2C5FCBF6C9B4A13A0EC25F76C7E1E5BA9ECC2E**；x64 **A4D40BE7BEB4603DCFD02E7FAE1E411846AED5246EECC6EAEACB81E00B3FDAD7**。

`pe-{arm64,x64}-frame-trace-1023-{headers,imports,dependents}.log`：ARM64 AA64/subsystem6.02，x64 8664/subsystem6.00；两者Magnification均delayload。ARM64原有WaitOnAddress仍在，x64没有；两者未见SetThreadDpiAwarenessContext/SetWindowDisplayAffinity/CreateDirect3D11DeviceFromDXGIDevice/RoGetActivationFactory硬导入。仅当前二进制静态范围，不证明旧Windows真机可运行。

独立读取 `product-live-stage/offline-18377546/result.json` 与 `offline-x64-18436828/result.json`：每架构35坏argv均exit2/stdout0B/stderr0B；protocol/state均exit0（ARM237/103ms，x64414/145ms）。新增 `external-entry-stage/trace-env-guards-18430312/result.json` 共18组（每架构9组），无效token/错误native live入口全部exit2/双0B；未启动窗口验证。

实际默认native短回归（没有启用observer）`live-product-ink-trace-default-regression-1023-test.json`：启动hashB7B0，PID33604，exit4/135ms，stdout0B/stderr54B。唯一stderr为 `[ProductLive] blocked=security-before-initialization`，没有case root/窗口/输入。root已停止全部GUI并等待用户手动解锁。此项是环境安全停止，**不是短回归PASS，也不是observer或Mag失败**。

当前未执行RecordMotionFrames，尚无实际dataset；回放器89CCDE6D编译成功仍不等于运行或逐帧一致性通过。旧真实200px成功/失败均保持，不因新构建改判。

## 当前轮续测：初始化前再次安全停止（2026-10-23）

只读核对 `live-product-ink-trace-resume-1023-test.json` 与实际两个输出文件：同B7B0E655录制版、默认native ink参数（未启用observer），PID38308，exit4/156ms、stdout0B/stderr54B。stderr仅 `security-before-initialization`（CRLF）。源码早门位于Fixture构造及Display/COM/source/产品HWND初始化之前，因此没有本case窗口、输入或截图；没有运行RecordMotionFrames，也未生成回放数据。短回归仍未完成，不能改判为产品失败或PASS。

`desktop-readonly-resume-1023.json` 当前完整类名收据显示：线程/输入桌面名称均Default且读取成功（openError0）；中心命中可见、未cloaked的 `LockScreenBackstopFrame`（PID13084，rect0,0,1440,960），前台为 `Windows.UI.Core.CoreWindow`（PID28244，cloaked2）。Default名称本身不足以证明安全层已解除；此处像素坐标来自该只读helper自身DPI上下文，不能与产品主屏物理2880尺寸混为一谈。

只读检查 `read-desktop-metadata.ps1`：GetClassNameW与GetUserObjectInformationW现明确CharSet.Unicode，输出完整类名；只查询桌面/命中窗口/类名/矩形/PID/cloak元数据并关闭自身OpenInputDesktop句柄，没有激活、输入、截图、锁屏规避或产品操作。首次内联PInvoke缺Unicode的结果不作为当前完整类名证据。root已停止GUI并请求人工解锁；checker未执行脚本、CLI、GUI或构建，未改源码。

## 人工解锁后：默认native短回归通过，录制尚未启动

实际 `live-product-ink-trace-unlocked-1023b-test.json`：启动hashB7B0E655，PID33048，exit0/6264ms，case root `product-live-33048-19249234`。checker只读复用原独立图像/标准MessagePack读取器，在内存中仅允许新增这一个已核binary hash、禁用输出文件写入，未修改任何checker脚本、源码或artifact。

8PNG全部独立核准：A原/selection core19082，handoff逐像素0误差；forward32/reverse16/Off core19081，双向38163点，中位0/最大1px。B core18571、unknown0、已知小alpha边缘17，独立逐通道alpha区间法通过（记录residual0.4412）；所有alpha255。两张专用source crop与纹理公式RGB精确一致。实际两次OS墨迹、真实双窗口ACK、Off后B输入、实际Exit/import/codec与cleanup均有原日志；11帧均accepted（含Initialize），无Paused。

实际文件 `AutoSave/desktop/2026-10-23/123244247_fb4e729a.uink` 12233B，SHA256 **9F945A4382CA79DC7AEF41B20190B6B6B47ED8A593B958C6CEA12E635FFA60C6**。只读MessagePack逐record到EOF核v10/GUID/workspace/page/index一致、Exit单条/sequence1、两Pen224/222点、viewport(0,16,1)。A/B首点与真实RTS raw精确float映射一致；native mouse受限1:1/DPI192量化界内，未推广到真实笔硬件。此为新observer默认关闭路径短回归PASS，不覆盖长滚动/编辑全矩阵/外部浏览器或录制模式。

随后唯一 `-RecordMotionFrames` 请求 `external-8b9c8d4996d44759b649e60be67d1bf1`：receipt exit1/5126ms，Edge812，Inkeys0；runner2.165s在 `Page identity/readiness/visibility changed` 通用页面准备门失败。Guest/disable-sync只记录启动请求。正常关闭自有Edge，cleanupErrors[]、ownEscapeHeld=false，未创建Inkeys/启动Mag/Tracker/实际轮滚或trace；不能算录制数据或产品Mag失败，也不能凭通用错误确定具体DOM字段。后续仅ignored有界状态诊断待交付，没有改变等待语义或产品算法。

### 页面准备失败的有界诊断增量

已只读核 `external-entry-stage/observe-diagnostic.patch/json`，runner当前SHA **62B422F820CFD24E4596D0EF51199BB207BAEC72C033F263470A36FC0253343D**。仅原同次CDP表达式追加documentReadyState，原失败分支记录oracleNull、URL/run/fixtureId精确匹配bool、bool ready或nonboolean、有限visibility/documentReadyState枚举；null保留unavailable。不输出身份原文、不重复查询、不将后一次状态替换失败瞬间。原判定/throw/等待/guards/input不动，Log仍Console.WriteLine不污染函数返回。AST/格式收据通过，checker未执行runner；已放行root一次新录制请求，不需要重建产品，也不是实际录制PASS。

### 第二次准备失败与一次性初始就绪等待

`external-fe3b8064939a4d798fcc773ad1583445` receipt实际exit1/**6481ms**（runner最后cleanup6504ms），Edge19896/Inkeys0、cleanupErrors[]/正常关闭。1.703s原返回oracleNull=true，visibility/documentReadyState均unavailable；旧表达式无fixture直接null，不能从该数据断言当时loading或interactive。仍无trace/Mag/Tracker运行。

root明确授权仅第一次Stable的有限启动等待。首候选误将-InitialReady加到第二Stable，checker发现真正首Stable仍严格、会在原位置早fail；implement已仅移动flag，未执行有误候选。当前候选 `initial-ready.candidate.ps1` SHA **627AD3825CDCB741E9970E4EE68BA75F6FD156ABFD51B8BB230EF867570C62D8**，精确patch/json与AST/格式收据已核准。

新同次Runtime只读envelope即使fixture未发布也提供document URL/visibility/readyState；每次仍先核原CDP target+URL。仅首个 `Stable8000 -InitialReady` 且无Inkeys、无此前有效观察：允许精确URL且visible的loading/interactive无fixture，或全fixture身份/原finite数值/DPR门通过而bool ready=false。隐藏/错身份/complete缺fixture仍立即失败。固定该次Stable的8s起点，不重置；第一有效Observe立即永久关闭豁免，之后fixture消失/导航仍硬fail；第二Stable和所有后续调用无flag。原输入/激活/JS写入/产品算法未动。静态放行应用ignored runner及root一次录制请求，不构成实际录制或回放通过。

## 实际录制与原算法逐帧回放：已重现失败（a6ce）

实际 `external-a6ce9b4f7df64b77951793715eba066c`：B7B0产品，Edge36724/Inkeys13172，receipt **exit1/9401ms**，normal cleanup/ownEscapeHeld=false/cleanupErrors[]。本次首个观察已ready、初始豁免直接永久关闭；原一次Escape归还后32点通过。原三阶段共96个marker采样精确匹配，一次真实OS轮滚，DOM CSSY0→100、DPR2、物理200px。真实Tracking/ACK empty pair(0,0)已到，未到Off-ready/最终PASS，不能改判为产品通过。

case root `product-live-13172-19751750` 的 `motion-trace.json` SHA256 **D6F6136912CBCA53F07874364F7BBAC018EC47A0E5E13EB3C75BD941CF5488A3**。checker独立逐文件只读核schema/整数字符串/geometry/完整payload：complete=true、failure空、originalThreadsDrained=true、skippedInputs0、observed=recorded=13；sequence1..13/session=captureSession=displayGeneration1、timestamp递增；13张P5 Gray8原分辨率2880×1808，各5207057B（17B头+5207040B像素），总像素67691520B，在64帧/384MiB门内。实际原source buffer2880×1919，sourceRect(0,0,2879,1919)、workArea(0,48,2880,1856)、ROI(0,48,2880,1808)，stride11520/原剩余span21553920均与生产clamp及ROIoffset精确一致。

操作准确计数：1次Initialize accepted、2次AwaitTracking默认NotInitialized（不调用Tracker）、10次Process（7 accepted/3 rejected）；不能把Capture accepted13叫作Tracker accepted13。frame10接受total=(-0.00018310546875,-49.0067138671875)。frame11..13各143内点/177FB、point ratio0.80791、survival0.608247≥.35，原空间比例 **0.64864864864864769 < .65**；原第一可信门唯一未满足项为空间比例，reference固定10、连续拒绝1/2/3，frame13进入Paused，total仍锁在frame10。coverage0及initialSurvivors0在拒绝结果中是未完成阶段/默认字段，不是特征真实全部退休证据。Capture质量/API错误0不能代替图像核对。

原回放器源CF565438、EXE **89CCDE6DBC3B3C972889ACDFF0C1510269C0BFDD560422C3F6771E30593B4AAB**，root实际 `recorded-motion-replay/actual-a6ce-replay-result.json` exit0/492ms，receipt manifest hash与实际文件一致；13行原log及summary均differences0/max_total_difference0，Initialize1/Process10/Await2。checker只读复核Compare实际覆盖accepted/state/reason/delta/total/confidence、4个u64/14个int/12个double非计时Stats；仅preprocessMs/opticalFlowMs/fitMs/refillMs忽略、浮点界1e-6+1e-8相对。因而**原算法可在已录制实输入上逐帧重现本次失败**，不是仅编译成功；不说明产品已修复。生产Tracker仍BF3409F8，checker未重跑回放、GUI或构建。

### 专用正文像素对应（root执行，checker只读审脚本/结果）

`recorded-motion-replay/analyze_actual_pixels.py` 不调用LK/Tracker；对frame9，在正文crop x[400,2500)、y[250,1400)内搜索0..220整数向上位移，三像素采样仅用于搜索，再以正确shift核整片 **2415000像素**。方向current[y,x]→reference[y+shift,x]正确，crop及最大参考shift均在实际Edge内容内、避开chrome/nonce/新露出区域，int16差值安全。

`pixel-correspondence.json`：frame1/9位移0，frame10位移49，frame11..13位移200；各正确shift整crop exactFraction1.0/MAE0。frame11..13实际PGM同SHA **78C9C2DF6F82C3FF128FB572FC1C1A84A592431C208EFCB21C5650AC93F77A36**；frame1/9同AF50B43F…。此独立像素对应支持：**本run该专用正文在失败帧已正确更新到200px，未停在0或49px旧内容**。不证明每个桌面像素/任意应用，也不追溯其它未录制run。Tracker此时只跟到约49px后拒绝，问题边界已缩到该实输入上的运动估计/可信判定；尚未修改算法。

### 仅ignored匹配网格诊断静态放行

`match-frame11-diagnostic/VisualMotionTracker.cpp` SHA **DCB34E07A6F1C3447ED313A96D2C3D0A4DEE48416427BA77110187504A8CDD11**：只新增cstdio和frame11/reference10的原match/cell/weight输出。独立删除两处增加后逐字节恢复生产BF3409，header复制精确813509；不写matches/indices/best/stats/tracks或原门。局部数组仅汇总原cell/inlier权重，输出在NonTranslation前，坐标附processing尺寸/fullres scale。printf会计入fitMs，属于原回放明确忽略的timing；不能用诊断耗时当产品性能。仅独立ignored输出exe/原replay读取，已放行root编译离线运行，未执行/未改生产。

### 实际匹配网格与floor3隔离候选

root已编译/运行ignored matchdiag，`actual-a6ce-result.json` exit0、EXE B58BD884…，原回放仍13帧differences0/maxTotal0。checker只读解析原177条Match：143主流的处理图Y位移[-76.07635,-74.98797]，34次流全部位于网格底两行（各17点）、Y位移[+33.01392,+67.19629]、最大FB0.82618，确属通过FB的反向错误匹配，不能把FB通过等同运动正确。37占用格中24格全部主流、13格全部次流，原单位格票正好24/37=.64864865。root另作正文重复纹理/纵向周期及分带pixel诊断，不能将跨固定横bar的reference9底部差异直接当撕裂证据；ref10→11底部按151px对应可达99.9946%匹配。此解释限定已录专用内容，不是固定工具栏的泛化结论。

只读审 `density-floor3-candidate/candidate.patch/manifest.json`：候选79A1617F…仅ignored四hunks。fit/MedianFlow/anchor使用1/max(cellCount,3)，每格至多一票；spatial ratio分母为实际全fit权重之和。matches≥40且实际cell计数均>0，分母正。新OriginalCellWeight使用原1/cellCount及occupiedCells分母，零主流次模式与runnerUp两个原歧义门保留单位格语义；.65/FB/coverage/NonTranslation/jump/3Pause/可信参考事务均保持。header813509、原testsEF45、原replayCF56未改；生产仍BF3409。

静态放行root编译隔离replay与原完整Tracker矩阵，不是生产放行。算法候选预期不同于录制的旧失败结果，原strict replay可因差异exit1，必须观察候选真实accepted/total与原全部负例，不伪称与旧结果逐字段零差等于改进。原‘每参考格总票为一’注释若将来入生产需同步为‘至多一票、稀疏格降权’；现仅ignored实验不为注释重建阻断。checker未编译或运行候选。
