# External-empty 独立审查与实测记录

日期：2026-10-23。范围：冻结 C++/CS/PS/fixture 的只读审查；checker 未构建、未执行产品 CLI、未调用 Win32 或启动 GUI。


## 当前结论（2026-10-23）

- Guest空页实际run750ddd…完整通过一次：真实OS wheel/独立200px GT→共享Mag/Tracker/owner→实际Off ACK，最终viewport200.000061；无墨迹/保存验收。
- 先前同Guest实际200px run5a936…连续3拒绝Paused；新46cc…已复现并定位空间比例.611565<.65门，图像/特征根因仍未明。新增仅verbose日志，不能声称已修复或Guest稳定普遍通过。当前仅证一次成功采样分成140+60px。
- 原非Guest私有profile晚出账户UI已由root视觉识别，未点击账户提示；后续仅Guest，隔离和私有profile残留限制见下文。所有旧失败按原样保留。

## 最初 V2 静态结论（历史）

**V2 未发现阻断，可以完整 ARM64/x64 串行构建；实际构建及严格 CLI 通过后，可执行一次 external-empty 专用 Edge 案例。** 此结论是运行前审查，不是浏览器跟随通过；没有浏览器墨迹、UInk、普通 tab、其它应用或硬件输入证据。

依据 `Build/ScrollFollow/external-entry-stage/manifest-v2.json` / `source-v2.patch`，独立读取实际文件计算哈希全部相等：

| 文件 | SHA256 前缀 |
| --- | --- |
| Draw3.ProductLiveTest.inl | EFD2B78BC35BF0AE |
| IdtMain.cpp | 84816190C46E1289 |
| IdtMagnification.h | 3AAB14948A1F0CE5 |
| external-native.cs（ignored） | 0D870BBD41051154 |
| run-live-external.ps1（ignored） | 800DC2046D1BD2EE |
| browser-fixture/index.html（ignored） | D90A878A4F2605E1 |
| run_product_live_offline.ps1（ignored） | D1B1FF6399679709 |

## Findings (fixed)

以下由 implement 代理修正，checker 核 V2 源码确认：

- 原阶段身份没有比对产品 session。`CheckExternalIdentity` 现在接收基准 session，seq3/5 均传本次 `base.session` 并严格比较十进制字符串；runner 两次回包明确使用 tracking session，Off 也核同一 session。
- 原 C# 激活/关闭仅凭 HWND/PID；早期尚无 content 时无法关闭。V2 Activate/Wheel/CloseOwned 使用持有的 Process、存活状态、完整 FILETIME/image；早期只允许唯一同 PID、GA_ROOT 且等于该 Process.MainWindowHandle 的根窗口 WM_CLOSE。无法证明即失败，不退回按进程名 kill。
- 原 PowerShell Log 输出可能进入 WaitPhase 返回管道，argv 拼接也需显式括号。V2 用 Console.WriteLine 和明确括号，阶段返回只含 JSON 对象。

## 关键路径

- 严格第九个 flag 在正常 Main 前整段 argv 检查；缺 env、额外参数、malformed/prefix、harness-off 均不能回落普通启动。C++ 不增加 HTTP/CDP 客户端或产品 API。
- 独立提取 05D 基线和当前 `CheckDesktop` 至 `CreateRoot` 连续 **34,890 字节完全相等**，SHA256 `72907FD9BCC58EEEE8B18EA75EC967AF985D18AFB8AEFFC2A409038CEEA29E69`，覆盖原 Wait/批量99点GT/全部 ink/long/Exit helpers。RunCase 新分支单独审查：原 native 源创建、GT、输入与清理由 `!externalEmpty` 保留，外窗不 DestroyWindow/subclass/setuserdata。
- 外部 GT 只参与断言；真实产品 WindowService/Mag/Business/owner/channel/ACK/Off 仍执行原路径。空页 pair(0,0)、precise ACK、至少三个新增 accepted、两个不同 sequence 收敛和完整结束屏障保留；无 GT 写目标、手工 ACK、注入 tracker 图像。
- C++ ready root 仅仓库 Build/ScrollFollow 下新 nonce 目录，祖先拒 reparse；阶段文件拒 reparse/目录/空文件/>16KiB/重复 key/注释/多余 JSON。阶段固定顺序/run/target/URL/PID/FILETIME/HWND/session，对进程保活并重核窗口/几何；finite GT、DPR、尺寸、缩放、resize 和页面滚动计数有界检查。
- runner 只启动 fresh private profile 的 msedge，持有原 Process，核实际 image/creation；从该 profile 的 DevToolsActivePort 取得专用 loopback port，browser websocket path 与文件一致；唯一页面 URL+nonce+target 全程重核。Runtime.evaluate 只读取状态，未调用页面现成 scrollTo helper、CDP Input 或合成 WM_MOUSEWHEEL。
- 真实输入使用 SendInput，先核安全桌面、PMv2 线程、held process、内容几何、foreground、实际 cursor/hit、无物理鼠标/修饰键按住；只移动指针和一次 wheel，不产生 held Down。实际 DOM 差值乘 DPR 作独立 GT，不能把 wheelDelta 当像素。
- 32 个 nonce 位色格由实际屏幕 GetPixel 核验，COLORREF 顺序与 fixture CSS 两色一致；各阶段稳定的 DOM 真值与屏幕身份分开。固定色格本身不证明移动底图或特定 Mag 帧像素正确；产品位移与独立 GT 的一致性仍须实测。
- finally 先发布撤销并等待唯一 Inkeys 的内部监督退出，再只向 held private Edge 的 CDP Browser.close 或窄根窗口 WM_CLOSE；关闭无法证明写失败 receipt，不关用户浏览器、不按全名 kill。receipt 的 ownedEdgeExited 仅证明持有根进程退出，不自动证明任意进程树资源全回收。
- PMv2 新接口仅 ignored Win11 helper，Safe 每次确认实际线程 DPI context，finally 恢复；未加入产品 EXE 硬导入。C++ 新使用进程/文件/窗口 API 均已有 Win7 可用版本，最终产物 PE 核验仍交构建阶段。

## Verification / 限制

- 独立哈希/34890B 字节保留检查：pass。
- 独立 PS AST：0 errors；`git diff --check --ignore-submodules=all` exit0。输出仅已有 Markdown LF→CRLF 提示，三份 C++ 原 BOM/EOL/EOF 与 V2 manifest 一致。
- implement `static-compile-v2.json`：Add-Type 内存编译 exit0、INPUT40；仅调用 managed InputSize，无 Win32/GUI。checker 读取此收据，未重复编译。
- C++ 构建/新35坏argv及协议/state：本报告时尚未取得执行结果，不能标记通过。没有本次 Edge 版本、DPR/HWND 同 PID 适配、屏幕色格或实际 wheel 运行证据。
- 首案故意要求 content 与 root 同一已验证 Edge PID；不支持的进程布局安全失败。SetForegroundWindow 被系统拒绝也失败，不绕过。
- 完整 argv 在固定 ArgumentList 中持有；日志有 private_profile，未声称已有独立完整 argv 审计记录。初始化主 try 前极早失败会由专用 PS 进程结束回收；不将其当用户常驻会话恢复保证。

任务保持 in_progress；未提交、归档或把任何历史失败/取消改判为通过。

## V2 构建后续事实（保留失败）

- root 执行完整 ARM64 V2 solution，报告 exit0/20.919s。checker 读取 `motion-tracker-build-external-empty-1023-preflight{,-console}.log`，结尾 65 warnings/0 errors、MSBuild 内部20.08s；其中新增 xutility C4244 来自 LoadExternal 已严格验证为十六进制 ASCII 的 wchar leaf → string 比较，另有原 Main/Setting/hashlib 等既有 warning。这里没有零警告声明。
- root 随后完整 x64 V2 build 失败。checker 读取 `solution-build-x64-external-empty-1023-preflight-console.log`，实际两条源码 C4996 位于新 inl307/310：C++20 已弃用 `std::filesystem::u8path`，按 error 处理。它是本次源码兼容问题，不是本机环境，也不能用 silence 宏或修改配置绕过。
- root 已指派 implement 做 UTF-8 path 标准构造及相邻已验证 hex leaf 比较的最小修正；此时未执行新 CLI/GUI。V2 原构建和失败日志保留，后续成功不能追溯覆盖。

## V3 最小兼容修正静态检查

- `utf8-path-fix.patch/json`：inl 当前 SHA256 `D3F997D21363C7B43F3DA1EAEAF5C4BD5467B72BE5537216DA90DE3BDF0A44AB`，checker 独立计算相等。仅 LoadExternal 修改；其余六份 V2 文件哈希不变，原34890B native helper完全相同；无 BOM/纯 CRLF/末尾换行保留。
- 两处 deprecated u8path 改为 `path(std::u8string(...))`，原 UTF-8 string 在 range 构造期间保活；char→char8_t 按码元保留字节，并由标准 path 以 UTF-8 解释，不退回本地代码页。hex 叶名比较改为将输入 run 加宽后与已验证的 ASCII hex 叶名比较，非 ASCII 输入不能匹配。
- 未改 suppression 宏、项目配置、阶段身份、GT 或安全门。静态放行两架构重构建；V3 尚无本报告独立读取的新构建/CLI/GUI结果。

## V3 实际构建、严格 CLI 与原 native short 回归

checker 仅读取 root 的新原始日志/收据，未重跑产品：

- ARM64 完整 `utf8-fixed`：root receipt exit0/26.523s；原 log 结尾 Build succeeded、3条既有 hashlib++ warnings/0 errors，MSBuild 内部26.27s。实际 Inkeys SHA256 `3CD17F64DE177088499EC3DA2A4FFB0C94B51843F8DF7008BED333E9893865B8`；Headless 仍 CC3F3154，与先前完整 no-window 通过版本相同。
- x64 `utf8-fixed` 已消除 C4996，随后原 log 明确 LNK1285：generated `Build/x64/Debug/Inkeys.pdb` 损坏。`x64-external-pdb-quarantine-1023.json` 保留204,976,128B坏PDB（SHA26AF8C75…）和同目录隔离路径；root 未改源码/配置，原配置 relink。`pdb-relink` root receipt exit0/10.871s；原 log Build succeeded/0 warnings/0 errors、内部10.61s。实际 x64 Inkeys SHA256 `40263953CBB9EE4E7B20758137CE6B437E50A50884437DAD1B85EEC406EF984A`。这是本次链接环境故障，与 V2 两处源码错误分开。
- `offline-11203984/result.json` 与 `offline-x64-11214937/result.json` 独立解析：各35条坏argv均exit2、stdout/stderr均0B；ARM64 protocol/state exit0(218/99ms)，x64 exit0(405/148ms)。没有执行 external 有效入口来冒充离线检查。
- 同一3CD17F64 EXE 原 native short：PID24580，`live-product-ink-external-entry-regression-1023-test.json` exit0/6428ms/100000ms预算，root `product-live-24580-11245046`。外层与result.log一致；真实A→非空pair(0,1)→32/-16→Off pair(1,0)→B→Exit；capture14/quality拒绝0/API0，tracker accepted12不含初始化未就绪帧，不能把14说成全tracker接受。
- 独立复用 checker，唯一增加此精确 EXE hash 白名单，未改像素/坐标门槛：8PNG（5A+1B+2源）通过，4次双向37310点均完全相等、max0；B unknown0/knownEdge37，受限已知色 alpha 区间法成立，不泛放未知像素。源PNG与独立 TexturePixel公式精确一致。
- 实际文件 `AutoSave/desktop/2026-10-23/101920281_56b59df9.uink` 12233B，SHA256 `9002a4fa51d6a34a777bc0b2b8e12a70d8a3d613264168665e290eaf53b4b534`。标准MessagePack完整解析/原codec回读：v10、两Pen226/220点、viewport(0,16,1)、GUID/索引及原受限RTS坐标oracle通过；真实状态/Mag线程返回，service/product/occupied/held/logfailed全清。

详细独立输出：`product-live-24580-11245046/checker-complete-verification.json`。本节仍不是 Edge 实测；root 后续唯一 external 运行与本次 native 回归分案记录。

## 首次 external 实际失败（不是产品 Mag 失败）

- root `external-be833ba7cc86413e9788f88165ea353c`，runner800DC204/cs0D870BBD。checker 读取原 runner.log：Edge PID18812、creation134371955877222764，专用 profile，Edg/155.0.4283.62、CDP1.3/port50068；1441ms失败，实际返回对象含 `System.Threading.Tasks.VoidTaskResult`，没有 SendAsync。
- PowerShell 对 async Task.GetAwaiter().GetResult() 的返回仍进入管道，ConnectWs 返回值变为混合数组；我前次静态审查未识别此运行行为。静态通过不等于实际运行通过，原结论不能用作 Edge 验收。
- finally 的 CDP close 同因失败，6481ms narrow WM_CLOSE 未能证明唯一main HWND而拒绝，11493ms仍私有 Edge alive。随后 Dispose 混合对象再抛异常，**没有 receipt**；目录没有 browser-ready/任何 Inkeys阶段或产品日志，未启动 Inkeys，因此没有本次 Capture/Mag/Tracker失败证据。
- root 已接管原 PID精确创建时间/image/profile 的只读核对和专用 Browser.close；关闭确认前不得新run。已指派 implement 仅修 Task<void>返回抑制、各资源Dispose异常隔离/receipt保证和早期owned主窗缓存刷新；无产品源修正需求。此失败保持原始状态，不能由后续重试追溯改判。

## 首次失败后的最小 runner 修正

- checker 读取 `root-cleanup.json`：root 后续核原PID18812/FILETIME134371955877222764/port50068，仅向 pinned private browser 发送 CDP Browser.close，ownedRootExited=true。原失败/无receipt事实保留；此后独立清理成功不是case通过。
- `void-task-fix.patch/json` 仅 ignored runner/CS：runner `7BC79B8270FADBB3CBF1B568FC073FE8B3A8BD46AD6CB14983AA12AB1C487529`，CS `BEB188BA69B5D956B58D5CF10604961993041C8AA274738BE92E65ACFEC334C6`；checker 独立哈希/纯CRLF相等，三份C++仍D3F/848/3AAB不变。
- ConnectAsync/SendAsync 两个 voidTask GetResult 明确 `$null=` 吸收；Receive/GetAsync/字符串结果仍正常使用。资源Dispose和DPI恢复逐项catch，将cleanupErrors/exit1写入最终receipt，不再让单项Dispose异常吞掉收据。早期CloseOwned先Refresh+held精确身份、只选唯一同PID GA_ROOT；移除可能为0的缓存MainWindowHandle额外等式，没有按全名kill。
- checker PS AST0；implement 报告新 Add-Type managed 编译/Input40通过，未执行native。原PID关闭确认后，静态放行一次新唯一external，不重建产品。此时没有新实际Edge跟随结果，不能保证任何文件系统故障下receipt必达。

## 第二次 external：标记失败，清理成功

- `external-ed3791b2bb2e4c7e858025b47a463c62/receipt.json` 独立核：runner elapsed2431ms/exit1，Edge37328/creation134371958805388755，InkeysPid0，ownedInkeysExited/ownedEdgeExited均true、cleanupErrors空。实际runner log记录 Marker col1,row3 actual00242424 失败，最后cleanup result1；两种elapsed采样时点不同（最终日志2466ms），不混成产品耗时。
- Task返回污染已越过，但此例未产生browser-ready、未启动Inkeys，不能称Mag/Tracker失败或Edge跟随通过。现有证据不能唯一确定窗口覆盖、坐标映射或画面更新时间原因。
- root指派implement仅加只读有界诊断：原32点actual/expected/坐标、坏点class/root/PID与cursor，Marker前已验证geometry/DOM及只读marker DOMrect/computedRGB。不得改变颜色容差、点、等待或截图未知内容。等待最小增量审查，原失败保留。

## 有界 marker 诊断静态放行

`marker-diagnostic.patch/json`：CS BBE79265…、PS CFF71548…、fixture88D4C82F…实际hash/CRLF相等，产品inl仍D3F997D2。独立核原32点坐标表达式、两精确RGB及任一点失配predicate不变；完整32点后统一throw，仅坏点加cursor/HWND/root/PID及至多64字类名，不取title/截图。DOM只读本nonce页面的marker rect/32cell中心+RGB且响应≤8KiB；fixture只加id，未改像素/style/等待。PS AST0（checker Python首读误用本机默认GBK，改显式UTF8后坐标/predicate静态断言通过；这是checker读取问题，未改受审文件）。静态放行一次新定位run，不重建产品；原因仍待实际raw。

## 第三次 external：同一私有进程附加顶层遮挡已定位

`external-c50233dc4cab434a8a2f7dd34d4bfc2a`：checker 独立解析32个DOM cell/nonce位色/4×4CSS尺寸/中心及32个屏幕坐标，全部映射正确（content origin0,94/size2880×1762/DPR2）。前25个实际屏幕颜色精确匹配；最后row3 col1..7（y154/x44..92）均242424，WindowFromPoint hit/root790860、Chrome_WidgetWin_1、PID25344，区别于同PID主root266600/content332178。cursor2879,1168远离采样点。可确认该时同私有PID另一顶层窗口覆盖7点，不确认具体popup类型。

receipt exit1/3451ms/InkeysPid0，ownedEdgeExited/ownedInkeysExited均true、cleanupErrors空；仍无Mag运行。root已授权仅setup的一次真实Escape：先核32点hit、只允许held exact Process身份的同PID覆盖、main foreground/无held键，ownDown/Up账本，至多2s回main；foreign或未消失仍失败，不改颜色/点/位置/flags/循环重试。实现增量待审。

## 单次 Escape setup 静态检查

`escape-setup.patch/json`：CS45CEF3DE…/PSF5E9BFE6…实际hash与CRLF相等；原fixture88D4C82F、产品inlD3F997D2未动，PS AST0。仅Inkeys创建前检查原32点hit；held Process精确creation/image、主窗前台、无物理鼠标/修饰键/VK_ESCAPE按住，foreign root立即失败。覆盖集合重核后最多一次Down，成功后才置ownEscapeHeld；Up由方法finally与runnerfinally有限补发，不受之后Safe/前台失败跳过。每次Release最多3次Up，但多个finally可继续补Up；不声称整个流程总Up尝试只3次。≤2s所有32hit回主root后仍执行原32点精确RGB，未改采样/色彩/浏览器flags/产品源。

新增INPUT明确联合布局40B，mouse/keyboard offset均8；Keyboard内部布局匹配ARM64/x64，managed布局编译收据由implement提供。静态放行一次新run；不是弹窗消失或Edge跟随已通过。

## 第四次 external：Tracking 已到，wheel 前前台/命中门失败

`external-239978d8a132425f9113b9683f62d82b` 原日志/phase/receipt独立核：一次 Escape Down/Up，246ms后32hit回主root，原32RGB全部精确；Edge34680，Inkeys37692以3CD17F64启动，真实Tracking(session1/capture1/accepted3)、ACK1、空pair(0,0)、content0、base(0,0)已到，并发布seq2。4.489s Marker 的组合 foreground/hit严格门失败；该raw没有具体foreground/hit句柄，不能断言是哪一项或由哪个窗口导致。

没有os_input记录、没有browser-wheel-result；wheel没有发送。runner发布cancel99，产品以external-runner-cancelled退出（失败时capture accepted5/quality拒绝0/API0）。最终receipt exit1/7509ms、两进程退出、cleanupErrors空、ownEscapeHeld=false；产品真实线程/service/occupied也归还。此例证明已能初始化到实际Tracking，不证明滚动/Off成功，也不归因Mag失败。root交implement仅加有界fg/hit事实和Tracking后本owned浏览器普通一次reactivation；实际严格输入门保留，待最小diff审查。

## 当前产物 PE / 格式只读核验

已读 root `pe-{arm64,x64}-external-1023-final-{headers,imports,dependents}.log` 和 summary：当前3CD17F64 ARM64=AA64、OS/subsystem6.02；40263953 x64=8664、OS/subsystem6.00。两者 MAGNIFICATION.dll均delay-load；QueryFullProcessImageNameW/PrintWindow为硬导入，ARM64额外有WaitOnAddress/x64无。关注的 SetThreadDpiAwarenessContext/RoInitialize/WindowsCreateString/DwmFlush 等未见于这两份硬导入，ignored helper的PMv2没有进入产品EXE。此为静态产物事实，不证明旧Windows运行或任意间接依赖兼容。

`external-entry-stage/current-source-format-1023.json` 共8条当前源码，checker重新计算hash/BOM/CRLF/EOF均与收据匹配；当前checkout均纯CRLF，Git baseline为LF由autocrlf处理的差别不当作源码格式回退。未执行任何新构建/CLI/GUI。

## Tracking 后前台诊断/激活增量预审

`foreground-fix.patch/json` CS7352CA76…/PS764F53B5…实际hash/CRLF相等。新增facts仅有界 HWND/root/PID/类名/visible/cursor，无title/图像；普通一次SetForegroundWindow前核held Edge+Inkeys完整身份，当前前台只接受这两个PID、未知/0前台拒绝，等待≤1s且不重复request；无ShowWindow/Resize/ALT/AttachThreadInput/点击，后续原requireInput/32RGB/Wheel门保持。

checker指出一个最小GT顺序问题：新3行reactivation在 `$before=Stable` 之后，应移到Tracking-ready后、before快照之前，确保独立pre-wheel状态在最后setup动作后重新观察，激活造成的任何页面变化不能混入wheel delta。已通知root/implement，等待仅移3行的新hash；其余CS无阻断，不需C++构建。

顺序修正已由implement仅移动PS三行，checker独立核新PS `CB1E4E11D55360BE2835CDD474873C03237747E3DCF2545ADB9A1967D941C6C7`、CS仍7352CA76；Tracking-ready→诊断/一次正常reactivation→新Stable before/Geometry/unchanged→原Marker→OS wheel，AST0。静态放行一次fresh run；未执行或宣称新实际通过。

## 第五次 external：延迟出现另一前台 root，真实 tracker 已暂停

`external-7bcbada4b4a242cd856c52993d5943d0`：receipt exit1/9863ms、Edge22348/Inkeys33788，正常关闭两进程、cleanupErrors空、ownEscapeHeld=false。5.611/5.698/5.716s实际主root2494056前台，normal_foreground_request=0（未激活）；6.680s before-wheel原32RGB仍全match。8.339s Wheel首个Check失败：前台为同PID22348另一root594280；center/cursor hit回main2494056，但类从Chrome_RenderWidgetHostHWND变成Chrome_WidgetWin_1，cursor仍2879,1168。无os_input/无wheel-result；按源代码失败位于Move前，与未移动/未滚轮相符。不确定该附加UI类型，不能追加循环Esc/抢前台规避。

**不能只记录runner失败而漏掉实际暂停。** 独立解析stderr连续frame1..22：1个Initialized+18个None被接受，total均(0,0)，frame20/21/22连续InsufficientFeatures后event=paused；最后可信/owner seq19、viewport(0,0)。Capture22质量拒绝0/API0仅证明捕获质量门通过，不证明具体帧有效内容或独立归因Mag。没有该时帧图像，不能断言Popup导致图像为何无特征或Mag本身故障。产品随后因runner取消安全结束，真实线程/service/occupied归还。

root仅指派进一步owned-extra-UI metadata/定位提案；此时没有新输入或门槛放宽授权，待精确只读增量。

## SetupDiagnostic 只读分支静态审查

`setup-diagnostic.patch/json`：PS CF39E9DB…/CS77AD95BC…实际hash/纯CRLF相等、AST0、产品inl仍D3F997D2。显式 `-SetupDiagnostic` 在原私有Edge/一次Esc/32RGB后观察；不publish browser-ready、不启动Inkeys、无wheel/截图/后续Esc或激活。仅held精确Edge的foreground/root同PID才取≤128标题、rect/owner，外进程或security立即停止；默认产品分支保留原逻辑。receipt mode=setup-metadata-diagnostic、完成日志product_result=not_run，diagnostic exit0不能算产品PASS。

静态放行一次仅diagnostic；12s是观察目标时窗、sleep至多100ms，每轮DOM/CDP也有耗时，不能称严格100ms采样或恰好12s墙钟。实际时长据raw报告。

## Metadata-only 实测：晚出 owned modal 仍待识别

`external-f34856995592475b86d94c393c42f365`：checker读receipt/log，mode=setup-metadata-diagnostic、exit0、receipt15423ms（末cleanup log15430ms），InkeysPid0/noWheel/无截图、ownEscapeHeld=false、两进程关闭、cleanupErrors空。实际观察12016ms：2.801s主root2690650正常；4.951s出现同held Edge29808另一个fgroot2429132，rect(916,48,1964,776)，owner为main2690650/ownerPID29808，标题空；后续没有metadata变化直至观察结束。主内容中心命中rootmain而非RenderWidget。仅能确认晚出owned UI稳定存在，未确认类型；diagnostic完成不是产品PASS。

root进一步仅授权显式diagnostic-only的一次owned popup裁剪截图：同held进程/owner/main与主屏bounds、安全及前后fg/rect/hitgrid，不存全屏、不读其它app；GDI必须finally释放。待精准实现静态审，GUI/实际截图和view_image由root独占。

## Owned popup 单crop预审

`popup-crop.patch/json` 初版PS DEB2B30B…/CS4374C6FD…hash/CRLF相等。只显式SetupDiagnostic+CaptureOwnedPopup可截图：held精确Edge、安全/内容几何、popup前台且owner==main；crop在primary及main实际window/client内（不是网页content），前后同popup/rect/owner、5×5 hit只允许本PID main/popup。仅crop大小的top-down32DIB、BitBlt/GdiFlush，前后门及GDI清理完成后再CreateNew固定owned-popup.bmp，不建全屏buffer；原普通/metadata-only分支不截图。选择旧bitmap恢复、DeleteObject/DeleteDC/ReleaseDC都在finally且失败不落图；BMP54B头/负height/BI_RGB与拷贝内存方向相符。

checker指出初版只限pixel data16MiB，BMP总长可额外54B；已要求仅将guard改为long `width*height*4+54<=16*1024*1024`，与root总文件≤16MiB一致。等待这一边界修正新hash，其余静态无阻断，不重建产品。

总长边界已仅在同guard修正，最终CS `E63B8EB259EEA286A548BE7EA37CC2A27A873B371E21E0B5BAAD919253D3A461`、PS仍DEB2B30B…，checker独立hash/总BMP long边界/产品inl不变及PS AST0通过。静态放行一次显式双switch新diagnostic，截图和查看由root独占；不是产品验收。

## 首次单crop实测（仅诊断）

`external-b5cbefcf123d42029792f8e949329845`：receipt exit0/15887ms（cleanup末日志15897ms）、mode诊断、Inkeys0/noWheel、ownedPopupScreenshot=true、ownEscapeHeld=false、正常cleanup。4.312s晚出ownedpopup1904890/Edge20032/mainowner266608/rect916,48..1964,776，4.368s完成唯一crop；56ms仅相邻日志时差，不能证明当时绘制是否完成。

checker仅读文件结构/像素，不view：BMP签名/54B头/BI_RGB32/负height/topdown1048×728/长度3051830B均正确（SHA dc4b28e41acbfcb95dd84f17f2713df1aed46279f7b3f1ff5433eeefcb025501）；root转换PNG（SHA e168e41d4cf4df25a39b4a6b2e761791996a75ef9ab08273acd8e3b2b7f238f2）与BMP的RGB逐像素相同。root view观察为dimmed自建页面而非可辨识对话内容，这是root视觉观察；“可能过早截取”仅推断，UI类型尚未确认。

12.862s日志cursor改至1650,636，12.988s前台回main；没有本脚本对应输入，不能据此断言人操作或某种dismissal。原结果仅诊断完成，非featurePASS。root指派仅诊断crop前加1000ms同popup/同guard稳定等待，变化则abort；这仍不能作为paint完成标志，待增量审查。

## Crop 延后1000ms最小增量

`popup-settle.patch/json`：仅CS `48E49D042B0470457C2DBA7E0DAB524AC1DD6B6FC02E5F13800BCF20697B9689`，PS仍DEB2B30B。checker独立hash/CRLF与单对象manifest相符。任何GDI分配/读取前，目标1000ms内sleep≤50ms、每轮完整Verify同popup/held身份/安全/fg/rect/owner/maincontainment/5×5；变化即throw，不重找或重试。原crop/后验/总长/GDI finally保持。静态放行一次fresh双flag诊断；日志guarded_wait_ms记录实际时长，不当作paint完成保证。

## 延后单crop实测：root识别为自动账户登录/同步提示

`external-55987efebf1b4089b4c77eb960bd6fa9`：checker独立raw/结构核exit0/14969ms，mode诊断/Inkeys0/noWheel/正常cleanup/ownEscapeHeld=false。3.422s出现同Edge37184额外root13570326（owner main15864100），4.514s唯一crop，实际guarded_wait1006ms。BMP1048×728/topdown32/3051830B头及长度正确，SHA417eb6ac9d2e4dccfce5f11dfc18e6002de32bf31f81624cfc8db3fab29ef9e6。

root经本地转换/view_image识别为私有Edge中Windows自动账户登录/同步通知；checker没有view图，不记录账户邮箱/标识。未点击确认/自定义、未输入凭据，root已正常关闭实例；原诊断完成仍不是产品PASS。此视觉证据解释本次观察到的晚出UI身份，不证明此前全部Mag/Tracker失败唯一由此导致。

root指派最小启动 `--guest` 请求及明确argv日志，保留私有profile/CDP/所有身份和输入门，先做12s metadata-only。不得点击账户提示或修改用户profile preferences/注册表绕过；flag只能证明请求的启动模式，不能保证实际Guest成功或全局账户零副作用。待精确增量审查。已创建测试profile清理由root在精确owned进程静止确认后负责，checker不执行删除/GUI。

## Guest 启动请求最小静态审查

`guest-mode.patch/json` 仅PS `834C704475E0EE80E4B6B2D2D055254FE380F4A62570DFA9EEE8535189DA17FA`；checker独立hash/CRLF/AST0通过，CS仍48E49D04。唯一变化为ArgumentList增加 `--guest` 及Start前guest_requested日志；没有账户操作、profile preference、policy、registry或浏览器disable-feature变更。静态放行下一次仅SetupDiagnostic（无Capture/Inkeys/wheel）。这只证明argv请求Guest，实际Edge155生效及账户隔离不能从flag或一般文档推定，不声称全局账户零副作用。

## 已创建私有 profile 清理限制

root执行后 `external-entry-stage/private-profile-cleanup-1023.json` 独立读取：8个任务私有profile中c502/ed379两项removed=true，其余6项AccessDenied而未完全清理（可能已有部分普通文件移除，不能声称完整保留或完整删除）。root声明目标均已核绝对路径/无reparse/closed receipts，未改ACL/全局用户profile/cloud，也未读browser data内容；checker不执行删除。

CIM可见PID/父PID/创建时间，但CommandLine均不可见，`allCommandLinesObservable=false/fullTreeQuiescenceClaim=false`。当前没有已知任务root或direct child，不等于整个进程树已静止或零Edge进程；现存其它Edge归较早root19032。不能把首次空过滤结果作为全面清理证明，不再尝试绕过ACL或关闭无关浏览器。新Guest metadata测试与未完全清理限制分开记录，无账户标识写入。

## Guest metadata 实测通过，仅放行一次产品验证

`external-04993c976cb847a9941205d846838ec2`：checker独立raw/receipt/当前hash核验，PS834C7044/CS48E49D04不变；exit0/15768ms，mode诊断/Inkeys0/noWheel/无截图、正常cleanup、ownEscapeHeld=false。**仍使用了原setup一次Escape**：1.702s初始额外owned root1642912，1.967s Down/Up/239ms回主root，32RGB精确通过，不能写noEsc。

2.539s主root14291222/Edge35184标题实际含nonce及[Guest]，内容/光标命中原RenderWidget；观察12002ms只有这一条未变化facts，没有再次观测到晚出UI。可证明这次12s窗口的Guest标题与稳定状态，不能证明全局账户零副作用或永不出现附加UI。静态及本次前置诊断通过，放行同current Guest脚本默认product一次fresh GUI（仍原所有输入/GT/Off门），无C++构建；尚未把Edge产品验收标记通过。

## Guest 首次实际产品滚动失败

`external-5a936e9fd223411ebce0e72857c18354`：receipt exit1/9238ms（末cleanup9252ms），不是单纯runner前台失败。6.520s真实move1/wheel1命中自建内容；同run/target/PID/FILETIME/HWND/session1的seq3独立GT：before CSS(0,0)，after(0,100)，DPR2→200原图像素，wheel0→1/scrollCount10，stable12帧/191.7ms。checker逐点核pre/post共64个原RGB精确匹配；当前没有额外popup/fg失败证据。

stderr连续frame1..18：1Initialized+14None被接受，total均0；16/17/18均InconsistentTranslation，最后两帧133inliers、177fbValid（stats tracked），连续3拒绝后Paused，owner停15/viewport(0,0)。没有Off-ready或browser-final/boundedPASS；正常cleanup不等于结束验收通过。Capture18质量/API拒绝0及固定marker正确，不能证明Mag该三帧图像内容无误。

原reason在算法中可由前置点数/空间权重/存活门或后续锚点一致性门产生；133/177点数比例不能替代空间权重。coverage日志0提示前置门可能触发，但仍需现有Stats诊断，不能据此改阈值或先排除Mag。root仅授权在现有verbose行追加已存在Stats定位，不改算法/step/ROI/参数；待精确增量审查。两进程关闭/cleanupErrors空/ownEscapeHeld=false。

## 现有 Stats verbose-only 增量审查

`motion-gates.patch/json`：Business ScrollFollow.cpp SHA `67E1844BFD4647475D99371720EFA19830474CEEBB28F68F9A4EA81A6AED6459`，checker独立hash/BOM无/纯CRLF/EOF通过；Tracker仍BF3409F8。只在唯一原verbose Printf的旧pw_us后追加16个现成统计（attempted/forward/backward/fbValid、点/空间/存活比例、4种cell、runnerUp、affine两值、refSequence/reject数），类型与format/cast匹配，4096B文本/唯一有界异步队列复用。原前缀字段不变，原非行尾pw_us parser仍兼容；没有算法/条件/threshold/ROI/步长/采样节奏/公开接口变化。

静态放行完整ARM64构建及成功后一次相同Guest200px定位，不预测通过，不用新增日志追溯改判旧失败。当前尚未读取该增量实际构建/GUI结果。

## 新Stats构建后一次Guest产品完整通过（限定run）

`external-750dddd0b78443b58ea7f25796eeb1cd`，receipt exit0/9658ms，Edge38108/Inkeys37536；当前ARM64实际EXE SHA `29BB15DD01271F7DF058B1689F4A67470D2C4BB89A5A0DA5D47A327C91294755` 与runner启动前hash相同。新Stats完整ARM64 root receipt15.728s，原MSBuild内部15.36s/3既有hashlib++ warnings/0errors；Tracker仍BF3409F8，无算法改变。

checker独立解析phase1..5：run/URL/target/PID/FILETIME/HWND/session1全关联一致；真实move+wheel1，beforeCSS0/afterCSS100×DPR2=200px，final DOM与after一致。ready/beforewheel/afterwheel/afterOff四阶段128个原RGB逐点精确匹配。stderr连续frame1..15全accepted（1Initialized+14None），9到−140.0001、10到−200.0001，其后保持；min logged confidence0.6893，无Paused。实际owner/Off Complete phase5、latest=applied=15、handoff/ACK1/pair(1,0)，boundedPASS最终viewport(0,200.000061)，与独立GT的六位日志差约0.000061px；不是墨迹视觉误差。

两进程/真实线程/service/occupied均归还，ownEscapeHeld=false/cleanupErrors空。DiagnosticOutput dropped1→2为累积计数，最终可见max2 contention、full/closing/truncated0，不相加成3；15条frame全在不证明其它日志无缺失。尚无严格采样首尾时间窗，不从帧数/整例9658ms推FPS。

此run仅证明一次Guest私有app空页纵向真实200px跟随与Off完成；非浏览器墨迹/保存、普通tab、其它应用或硬件通过。先前5a936…200px失败仍未解决，新改变仅日志且本次140+60采样与旧单跳路径不同，不能称日志修复了bug。root最多再两次同脚本/同120wheel的串行定位，仍不改算法/step/ROI/门槛。

## 同Guest200px再次失败：空间门已明确

`external-46cc7129b8cd4621b8a47d1d8842a840` exit1/9110ms，frames9–11空间比例.611565<.65，134/178点内点、存活.611684，reference8、连续3拒绝Paused；明确触发Coverage前的一致性门。原一次140+60成功不覆盖此失败。后续仅受限实际输入录制，审查移至 `live-external-frame-trace-check.md`；未改算法/阈值/step/ROI。
