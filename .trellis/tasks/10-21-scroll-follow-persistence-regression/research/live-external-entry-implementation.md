# External-empty 测试入口实施记录

日期：2026-10-23。任务仍进行，本记录不是外部应用验收通过声明。

## 范围与当前文件

新增严格单参 `--scroll-follow-live-external-test`，复用真实 Business、原共享定格 Mag、Tracker、Draw3 owner、WindowService ACK 与 Off。只验证专用 Edge app 窗口驱动**空画布**视口的一次实际滚动；不证明浏览器墨迹、UInk、普通 tab、Word/PDF 或硬件笔。

外部入口产品侧原只涉及 `Draw3.ProductLiveTest.inl`、`IdtMain.cpp`、`IdtMagnification.h`；本次额外仅 `Business/ScrollFollow.cpp` 的原verbose Printf追加现成统计。当前源码 SHA 前缀分别 D3F997D2 / 84816190 / 3AAB1494；主会话当前 ARM64 EXE 前缀 3CD17F64。本次 Escape 整理不改这些源码或二进制。原 CheckDesktop→CreateRoot 的 34,890 字节逐字保持，包含原安全等待、批量 GT/99 点/精确 ROI、全部 ink/long/Exit helpers；该段 SHA 为 72907FD9BCC58EEEE8B18EA75EC967AF985D18AFB8AEFFC2A409038CEEA29E69。

私有辅助文件位于 ignored `Build/ScrollFollow/`：

- `run-live-external.ps1`：834C704475E0EE80E4B6B2D2D055254FE380F4A62570DFA9EEE8535189DA17FA。
- `external-native.cs`：48E49D042B0470457C2DBA7E0DAB524AC1DD6B6FC02E5F13800BCF20697B9689。
- `browser-fixture/index.html`：88D4C82F0DDD89A19792B08924545AFEA709F60CCE706336048E133821E8702F。
- `product-live-stage/run_product_live_offline.ps1`：原25坏 argv 保留，另备10个 external 坏 argv 与协议/state。实际统一构建/CLI 收据由主会话记录，本次 helper 调整未重编产品。

精确前后文件、diff、格式和布局记录在 `Build/ScrollFollow/external-entry-stage/`。本轮最新差异为 `motion-gates.patch/json`（仅Business现有verbose一行）；前一Guest启动参数为 `guest-mode.patch/json`；前一helper等待为 `popup-settle.patch/json`；单张局部截图主体为 `popup-crop.patch/json`；前一metadata-only观察为 `setup-diagnostic.patch/json`；前一版正常前台交回为 `foreground-fix.patch/json`；此前的单次Escape整理为 `escape-setup.patch/json`；此前依次保留 `utf8-path-fix`、`void-task-fix`、`marker-diagnostic`，没有覆盖失败 raw。

## 身份、只读 GT 与清理

每轮新建普通目录 `Build/ScrollFollow/external-<32hex>`、独立 profile 和 fixture。Edge 使用真实 app 窗口、port0 与自己的 loopback CDP，不能附着用户 profile/tab、修改浏览器策略/GPU/缩放或截图源。

阶段文件限制16KiB，完整临时写入后原子发布，不覆盖：browser-ready1 → 实际 tracking-ready2 → browser-wheel-result3 → 实际 Off-ready4 → browser-final5。取消99单向停止。绑定 nonce、URL/target、PID、GetProcessTimes 精确 FILETIME、image 和 root/content HWND；64位身份均为字符串。wheel/final 必须匹配原 `base.session`。原始 ProcessStartInfo/私有启动参数被保留；未把“完整 argv 已独立落日志”作为既有事实。

首例保守要求 root/content 都属于同一个仍被持有的精确 Edge PID；content client 与 DOM innerWidth/Height×DPR 交叉核对。runner 仅只读页面实际 scroll/geometry/stable，发送一次真实系统 wheel 后使用实际 CSS 差×DPR断言。GT 不输入捕获、Tracker、Follow Target 或 ACK。固定32格 nonce 标记取真实屏幕 GetPixel，原32坐标及每点精确颜色不放宽；不保存未知/全屏图像。rAF稳定不是 Mag/DWM 完成凭据。

启动 Edge 之前及窗口/输入/屏幕操作前检查安全桌面和已知安全层。Win11-only helper 显式设置并核对当前线程 PMv2，finally恢复；这些 helper DPI API 不进入产品 EXE 硬导入。激活、输入和关闭前核 held Process alive、精确 FILETIME/image。C++不 DestroyWindow 外部窗口；runner只关闭自己的私有 CDP，或核唯一同PID root后 WM_CLOSE，不能按进程名批量 kill。

异步 stdout/stderr 防管道阻塞；取消后等自己的 Inkeys 90秒监督。资源 Dispose 逐项 catch，异常记 cleanupErrors/失败状态，最终写 receipt。无法证明退出必须明确记录，不能记正常清理。

## 已发生的失败及修复

1. x64 编译把新增 `filesystem::u8path` 的 C++20 deprecated 警告 C4996 当作错误。两处改为 `path(std::u8string(bytes.begin(),bytes.end()))`，保留 UTF-8 语义；hex leaf 比较改为扩宽 run，避免 wchar→char 窄化。没有 silence 宏、项目/SDK或本地代码页绕过。
2. `external-be833ba7cc86413e9788f88165ea353c`：约1441ms，ConnectAsync 的 VoidTaskResult 进入 PowerShell pipeline，使 WebSocket 变量成数组；后续 SendAsync/Dispose 失败。没有 Inkeys/ready/Mag。root 另行核精确身份和私有端点后关闭旧 Edge，留 root-cleanup.json。修复 ConnectAsync/SendAsync 的空结果为显式 `$null=`，并使逐资源释放失败不阻断 receipt。
3. `external-ed3791b2bb2e4c7e858025b47a463c62`：约2.4s，已读屏幕 marker，在 col1,row3 得到00242424失配。没有创建 Inkeys；finally正常，cleanupErrors为空。随后只补只读诊断，没有放宽颜色或加等待。
4. `external-c50233dc4cab434a8a2f7dd34d4bfc2a`：主 root266600、content332178、origin(0,94)、2880×1762、DPR2。DOM32格均4×4 CSS，前25个屏幕点精确匹配；末行 col1..7 的 y154 被同 PID25344 的额外 root790860 / Chrome_WidgetWin_1 遮挡，读成242424。cursor(2879,1168)不在 marker。诊断只读实际 rect/32中心/computedRGB 和失配点窗口元数据，不读未知标题或保存图像。该轮也未启动 Inkeys，资源正常关闭。证据支持“该专用 Edge 的额外顶层窗挡住 marker”；不支持称 Mag/Tracker 失败。

## 本次单次 Escape setup 候选

只在初次 Marker、browser-ready和 Inkeys 创建之前检查原32点的实际 GA_ROOT。若有覆盖，必须仍是 held Edge 的精确 FILETIME/image/同PID root；main root当前前台，鼠标/修饰键和Escape未 held。先日志附加 root的 class/rect/owner，再重核覆盖集合；外进程覆盖立即失败。

仅发送一次真实 Escape Down，SendInput确认成功后才置 ownEscapeHeld。Up最多三次有限释放；方法 finally 与 runner finally 只归还自身成功 Down，不受后来安全/前台检查失败阻断，不再发Down。receipt记录 ownEscapeHeld。等待至多2秒，要求32个 hit全部回main root；否则失败。之后仍执行原32点精确RGB校验，不改点、不改颜色、不隐藏窗口、不循环Escape或改浏览器feature。后续 Tracking/Off 阶段不会重复 setup。

Keyboard/Mouse使用40字节INPUT union，二者offset均8。私有TEMP内 Add-Type编译exit0，纯managed布局检查40/8/8，PowerShell AST零错误；没有执行 Safe、Win32或输入来验证本候选。产品没有重构建。

## 第四轮已到真实 Tracking，但未发送滚轮

`external-239978d8a132425f9113b9683f62d82b` 中，单次 Escape setup 实际 Down/Up 通过，246ms后32个hit回main root，原32点RGB全部精确匹配。Inkeys PID37692实际启动；session1/capture1，accepted3，真实 Tracking、ACK1和空页窗口pair(0,0)到达。随后滚轮前的 Marker(requireInput)以“owned content not foreground/hit”停止；没有 `os_input` 或 wheel-result，取消与两进程清理正常。

旧日志未分别给出当时 foreground/hit HWND，因此不能把该失败归因某个隐藏窗口或 overlay。原始stderr另有 Initialized290角点及后续 InsufficientFeatures；这不独自证明 Mag 错误，亦不证明该时每个浏览器像素都已正确捕获。本轮没有实际滚动后的独立GT/owner比较，不记完整外部PASS。

本次只补有界元数据和普通前台交回：Tracking-ready后，先记实际 foreground、中心hit和cursor hit的 HWND/root/PID/class/visible，无窗口标题或图像。若已经是main root就不激活；否则只有当前前台可核实属于仍被持有的本次Edge或Inkeys精确FILETIME/image，且没有物理按键持有，才请求一次正常 SetForegroundWindow。未知第三方或初始foreground0保守失败；不ShowWindow/resize，不重复请求，不用点击、ALT或AttachThreadInput。最多等1秒验证实际main前台，中途第三方占用立即停止。激活setup之后才重新取得Stable/Geometry/未变化检查的独立before真值，避免setup变化混入wheel delta。之后仍完整执行原Marker(requireInput)、32点精确RGB和系统Wheel全部门禁；同一严格失败也附有界窗口facts。

本次helper AST0、内存编译0及managed INPUT40/offset8/8通过；未执行Win32、GUI、系统输入或产品构建。最新独立review和下一次实际run仍由checker/root串行处理。

## 第五轮的两层失败与最小观察模式

`external-7bcbada4b4a242cd856c52993d5943d0` 最终exit1，约9.86s，两进程正常关闭。5.611s时 foreground已经是Edge main2494056，中心命中child11145510/root2494056，普通交回返回already_main而未发激活。6.680s滚轮前32点仍全部精确匹配；8.339s Wheel首个Check在Move之前失败，前台变为同Edge PID22348的另一个root594280 / Chrome_WidgetWin_1，中心/实际cursor命中仍回mainroot2494056，cursor未移。没有os_input或wheel-result。仅凭class/PID无法确定这个晚出UI的具体类型，也没有重复激活/Escape去隐藏失败。

同时，真实Tracker frame1 Initialized，2..19 reasonNone，frame20/21/22 InsufficientFeatures后进入Paused；最后可信owner sequence19、total(0,0)。这层产品暂停事实必须与runner前台失败同时保留。Capture22帧、quality/API失败0不能排除该时Mag实际图像内容问题，不能只把本轮称作runner失败，也不能反过来称已证Mag故障。尚无实际wheel后的外部GT比较。

为识别晚出UI，增加显式 `run-live-external.ps1 -SetupDiagnostic`。复用原私有Edge启动、唯一一次Esc与初次精确32RGB后，仅12秒观察：每次安全/held进程/窗口几何门后，读取foreground/中心hit；只有仍属于本次精确heldEdge PID的foreground/root才读最多128字标题、rect、owner。仅变化时输出有界元数据，间隔最多100ms；外进程foreground或安全层立即停止。页面URL/target/几何/DPR/scroll/wheel等独立观测仍核不变。

该模式不发布browser-ready、不创建Inkeys、不发wheel，观察期间不再激活或按Esc，不截图。默认产品分支保持原样。receipt标记 `mode=setup-metadata-diagnostic`，日志 `product_result=not_run`；观察完成exit0仅表示诊断完成，不表示产品测试通过。此候选AST零错误、Add-Type内存编译0，managed INPUT40/offset8/8；实施者未调用Win32/GUI/输入或重构建产品，等待checker与root单次新nonce实际运行。

当前driver仍在ignored Build下；功能链真实跑通后需另行安排可复现的tracked存放位置，此时不扩移植或通用框架。

## Metadata-only实际观察及显式单张局部截图候选

root实际运行 `-SetupDiagnostic` 的 `external-f34856995592475b86d94c393c42f365`：exit0/15430ms，scope=not_run，没有Inkeys或wheel。4.951s前台出现同heldEdge PID29808的新root2429132，rect(916,48,1964,776)，owner2690650正是该run mainroot，title为空；该附加UI持续观察窗口。此证据仍不能凭类名和空标题命名具体UI，也不能替代外部产品跟随验收。

按明确批准，增加独立开关 `-SetupDiagnostic -CaptureOwnedPopup`；仅captureflag而无diagnostic时在GUI创建前拒绝。metadata默认和普通产品模式仍不截图。第一次额外前台root出现时，只有以下门全部通过才保存一次 `owned-popup.bmp`：

- heldEdge存活且精确PID/FILETIME/image相符；安全桌面、main/content可见及原geometry不变。
- popup就是实际foreground的GA_ROOT、同heldEdge PID、visible、owner恰为main；输入动作不增加。
- rect完全在主屏和main**实际window/client物理边界**交集内，不把Chrome_RenderWidget网页content边界误当main client。若不包含直接失败，不静默裁掉或放松。
- capture前、读回后、落盘前重核同popup/rect/foreground/owner及5×5 WindowFromPoint网格；每点只能命中同heldEdge的popup或mainroot，外进程遮挡立即失败。
- 只分配该crop尺寸的topdown32bitDIB，BMP总长含54字节头≤16MiB；screen BitBlt/GdiFlush任失败都不落图。所有GDI对象在finally恢复/释放，无法确认清理就失败。全部门通过后才CreateNew写54字节头+crop像素BMP，输出绑定本run普通目录/固定basename。

没有全屏DIB/图像保存、没有额外Escape/点击/激活、没有Inkeys或wheel。只保存一个私有popup局部，receipt记录是否实际保存；图像转换/查看由root离线处理。此候选AST零错误、C#内存编译0、managed INPUT40/offset8/8通过，实施者没有运行Native、GUI、截屏或产品build。该版本随后已由root实际执行，单张BMP结果见下一节。

## 单张crop实际结果及受检一秒延后候选

root双flag运行 `external-b5cbefcf123d42029792f8e949329845`：exit0，正常清理约15.9s，scope仍not_run，无Inkeys/wheel。4.312s首次观察到own Edge PID20032的popup前台root1904890，owner266608为mainroot；4.368s已保存唯一1048×728 BMP，总3,051,830字节，距首次观察仅56ms。原始BMP及root离线转换PNG保留。

root查看该crop为变暗的自建底层测试页面和蓝条，没有可识别的弹窗文字。“可能弹窗内容尚未完成绘制”是推断，不能由此确定UI类型或屏幕捕获故障。12.988s前台回main，期间cursor位置也发生变化；本记录仅保留观测，不解释谁或什么导致变化。诊断观察期没有新增夹具输入。

新的最小候选只在crop读取之前增加约1000ms受检延后：每≤50ms重新执行完整原 VerifyPopupCrop，持有进程/安全桌面/同popup前台/rect/owner/main边界/5×5own网格任何变化均失败，不重新寻找popup或自动重试。等待在GDI分配前完成，日志记录 `guarded_wait_ms`；这仍不保证绘制已完成。单张crop、总BMP≤16MiB、原pre/post门和全部GDI finally保持。PS不变，只有CS helper变更，AST0/内存编译0/布局40-8-8；实施者未运行Native、GUI、截屏或产品build，等待checker后由root下一次新nonce运行。

## 受检等待后的UI识别与Guest隔离候选

root在 `external-55987efebf1b4089b4c77eb960bd6fa9` 的受检一秒BMP中识别出Edge账户登录/同步的信息提示，含确认与自定义选项。这里不记录账户邮箱、身份或同步数据，也不从提示推出同步后台具体完成了什么。该私有browser已正常关闭；没有由夹具点击/确认提示、退出账户或读取同步内容。自建user-data-dir本身不足以阻止本机Edge的这类自动账户交互，之前的晚出UI因此不能仅当作普通视觉遮挡处理。

按root明确批准，仅给原ArgumentList增加一个 `--guest`，并在启动前记录 `guest_requested=1 startup_flag=--guest`。保留全新私有user-data-dir、CDP、fixture、几何/真实输入/安全/清理全部条件，不写注册表、policy或preference，不使用disable-feature，不处理账户。

Microsoft官方[隐私说明](https://learn.microsoft.com/en-us/legal/microsoft-edge/privacy)的Guest相关段落说明Guest是临时新profile、不能登录Edge，Guest浏览数据不向Microsoft账户同步。上游[Chromium switches源码](https://chromium.googlesource.com/chromium/src/+/lkgr/chrome/common/chrome_switches.h)定义 `kGuest="guest"` 为直接以Guest启动。后一个是上游开关定义，不冒充当前Edge155的已运行保证；本轮日志只称requested。

下一次只允许先运行12秒 `-SetupDiagnostic` 元数据模式，不带截图flag、不启动Inkeys或wheel，验证Guest请求下是否还有自动登录/同步模态和额外前台。新PS AST0，CS helper未变（48E49D04），实施者未启动GUI、账户操作、读取profile内容或产品构建。自建profile目录的后续清理由root在持有进程彻底结束后统一处理，本文不扩展文件操作或自动修改既有账户。

## Guest请求下首次真实wheel及原verbose统计补充

root实际产品轮 `external-5a936e9fd223411ebce0e72857c18354`，Edge30812/Inkeys31152，exit1/9252ms。6.520s实际系统wheel120已发，页面scrollY由0到100 CSS，DPR2，对应200物理像素；wheelCount1/scrollCount10且稳定。前后marker32点全部精确匹配，未观察到此前额外UI遮挡。不得将这次实际200px换成更小输入或改变ROI来求通过。

真实Tracker frame1..15接受零位移；16..18以InconsistentTranslation拒绝并Paused，可信total保持(0,0)，内点133。旧逐帧字段不足以完整区分pre-Coverage空间票/存活门与Coverage之后的出生锚点总位移一致性检查。该轮证明真实输入与独立网页GT到达，未证明owner已跟随；仍是失败，不能以API/画质统计正常排除实际Mag图像内容问题。

本次代码只给 `Inkeys/Inkeys/Business/ScrollFollow.cpp` 原verbose Printf在旧pw_us后追加16个现成 MotionStatistics：attempted/forward/backward/fbValid，point/spatial/survival ratios，reference/occupied/inlier/interior cells，runnerUpRatio，affineVariation/affineResidual，referenceSequence和consecutiveRejects。旧log前缀/字段、唯一有界队列、关闭verbose无新增逐帧输出全部保持，referenceSequence用明确unsigned long long匹配%llu。未增加stage reason、观察器或产品接口，也不改算法/门槛/参考事务/捕获/ROI/wheel/GPU/存储。

该文件C50A18FA56892319D82B92AAF76B7DA886F86207B6076F74BF7DE03B6DF9B0F7→67E1844BFD4647475D99371720EFA19830474CEEBB28F68F9A4EA81A6AED6459，无BOM/CRLF/EOF保持。静态35个format与35个参数、末16个字段绑定已核，继续使用TextCapacity4096和原队列/drop语义，不声称诊断全量无损。Tracker实现仍BF3409F8。本实施者未build/GUI，待checker/root统一构建和同120wheel/Guest/fixture重跑收据。

若新增统计仍不能解释图像来源与拟合结果，可再批准最窄harness-only只读tap：在现有Business::Frame即将调用Initialize/Process处，借用寿命内复制**同一MotionFrame ROI**和真实time/sequence/geometry/session；不增加截图源/GPU路径或替代Capture。记录必须从Initialize起保持连续前缀到首组三拒绝，不能只把最后可信图重新Initialize而改变锚点/接力状态。私有Guest源须经窗口/安全/nonce画面身份门，禁止保存ROI外未知桌面或账户UI；需先明确有界队列/内存/磁盘上限，漏帧即标不可精确重放。离线复用既有Tracker/OpenCV诊断编译方式逐帧重放、只作比较不写Target。本轮没有实现这一collector，先用新增统计定位门，再决定是否需要图像取证。

## 当前门槛

当前等待 checker 核verbose统计delta，再由主会话构建并独占一次相同120wheel/Guest/fixture的真实产品重跑。前三次未到Inkeys；第四、五次已到真实Tracking/ACK但没有发送滚轮，第五次还明确出现三帧特征不足后Paused。Guest首次产品已取得独立200px网页滚动GT，但owner保持0并Paused；仍无GT→owner→Off的完整外部通过证据。不能以自建 native long 通过或这些夹具失败代替完整外部应用结果。若下一轮仍失败，应保留准确阶段、身份、原始日志和清理状态；不静默自动重试或降低门槛。本任务不 commit/push/finish/archive。
