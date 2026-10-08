# Implementation Plan: Win7 主画布恢复

## Phase 0 — Preserve and Identify

- [x] 复核当前 HEAD、branch、tracked/untracked 状态；保留已有 research 文件和 `inkStrokeModelerTest/` 四个 `.cso`。
- [x] 记录候选输入的 SHA-256、文件版本、PE 架构、配置及修改时间。将 `G:\Inkeys` 当前 EXE 与本机旧 x64 Release 同 hash 作为“二进制相同”记录；未获得可验证的 Git SHA 前不写成“由 f420 构建”。
- [x] 选取可隔离的新构建输出目录并检查 `PptCOM.csproj` 的 `ZhjOutputDir` 和 Draw3 shader `ObjectFileOutput`；确认不覆盖旧测试包、既有输出或四个 Demo `.cso` 后再构建。

## Phase 1 — Trace and Reproduce

- [x] 复核 `IdtMain.cpp` 窗口创建、Draw3 启动/首帧检查和启动预览边界；复核 `Window.cpp` 生命周期/双窗显隐，以及 `IdtState.cpp` Primary/Presentation/Hidden 决策和重试门控。
- [x] 复核 `Draw3.Host.cpp` Graphics/Presenter/Controller/FirstFrame stage，`Draw3.GraphicsInitialization.cpp` Hardware/WARP 尝试，以及 `Draw3.TransparentPresentation.cpp` 普通回退、PrimaryDrawpad 与 SelectionUlw 两套 ULW presenter 和最终参数。
- [x] 使用当前产品路径可用的 headless/非交互测试，对主窗生命周期、书写态可见、选择态穿透、目标切换和首帧失败分类做修复前验证。先保存任何自动失败输出。
- [x] 对照 Win7 附件判定：当前启动已到达普通窗口/线程初始化；硬件失败 HR/阶段、Draw3 主 target 可见/提交结果和状态机显隐尚缺。不要将 waitable `0x887a0001` 或 Bar 成功帧记为根因。

## Phase 2 — Minimal Resolution

- [ ] 若失败可稳定复现且因果路径已证实，修改最少生产代码/相应测试，保留修复前失败证据，并以同一自动检查确认恢复。当前环境未满足该条件。
- [x] 若当前机器不可复现，增加有限 Draw3-only 诊断：构建 identifier；Hardware/WARP 初始化失败阶段/HRESULT；两窗身份与生命周期/状态转换；首帧及目标/门控状态；实际主画布 presenter 参数、HRESULT/GetLastError 和成功/失败结果。
- [x] 日志只在 startup、surface/target 状态变化、首帧 attempt、失败/恢复记录；复用现有接口，不记录每帧、不新建通用诊断层。
- [x] 修改/审查期间保证交换模式、DComp/ULW、HTTP(S)、旧 EXE 兼容、PPT 数据保护、画质/动画设置和四个 Demo `.cso` 均符合 PRD。

## Phase 3 — Verify and Handoff

- [x] 先确认本机构建系统及 ARM64 MSBuild 可执行路径；按根 `AGENTS.md` 在同一 PowerShell invocation 规范化 `PATH`，构建完整 `InkeysRepo.sln Debug|ARM64 /m:1`，超时至少 5 分钟。尽量使用隔离 `OutDir`/`IntDir` 并重定向 PptCOM 输出；若无法保证不覆盖现场，停止该构建并报告具体冲突。
- [x] 运行适用的 `InkeysHeadlessTests.exe --no-window` 与受影响现有 Draw3 测试；只运行不要求人工关闭交互窗口的检查。
- [x] 查看 `git diff` 与状态：四个用户留存的 Demo `.cso`、既有 research 文件、旧测试包及未提交文件须原样保留；确认没有无关格式/源码变化。
- [x] 生成交付记录：当前完整源码 SHA / working tree 状态、EXE SHA-256、PE 架构、配置、diagnostic build identifier、构建/测试退出码。
- [x] 给用户最短实机步骤：Win7 SP1 + 仅 KB2670838 启动当前书写模式；确认主画布出现并画一笔；切换选择确认桌面可点击；定格开启/关闭后回到书写并续画；有限确认既有 PPT 流程。附期望现象、需回传的完整 IDT log 与 console output，并逐项标注“待用户实测”。
- [x] 只有发现受影响的 PPT 连接点时才运行对应有限回归；不重新展开整套 PPT 重构。当前未发现可将隐藏 fixture 失败归因到本轮主画布诊断的连接点。

## Stop Conditions

- 已交付经当前环境构建/回归的诊断候选；仍需 Win7 现场反馈才能定因。
- 不 commit、push 或发布。

## 2026-10-03 — 输入与实际像素继续调查

- [x] 复用原任务；核验新日志、测试 EXE SHA-256 与四个嵌入 shader；不改动 G: 现场包。
- [x] 分离 WM_MOUSE 光标反馈、RTS contact 落笔、document/history、GPU 像素和 USER32/桌面可见五类证据。
- [x] 发现产品未编译/启用 DRAW3_RTS_DIAGNOSTICS，纠正“开启现有开关即可看到 RTS 回调”的诊断建议。
- [x] 在现有有界 Cursor 队列补充 RTS Down/Up 及限频 Packets；不改变输入准入或 producer。
- [x] 在现有 ULW 读回/复制过程中限频统计原始源像素和最终 DIB；不改变窗口、swapchain、alpha 底层或 dirty 更新策略。
- [x] 完整 Solution Debug|ARM64 验证；额外构建 x64 Release 实机诊断候选；运行无窗口 Headless 和现有 ULW copy benchmark。
- [x] 完成独立检查并记录调查结论、证据限制、候选身份及 Win7 最短再现/判别步骤。实机恢复前保持任务 in_progress。

## Win7 完整采集 17:28:38 反馈

- [x] 核验EXE身份；RTS与controller均有13次contact正证据，15个ULW源采样全零、最终Primary只有alpha=1命中底层。
- [x] 独立输入时间线及共享GPU/资源源码审查；已排除本次鼠标RTS缺席，具体GPU/读回根因仍未证明。
- [ ] 同Win7设备进行known-color clear/readback与正式cursor draw/readback分界自检；保持任务in_progress。

## GPU 分界测试与失败日志（用户已要求）

当前行为差距：已有证据证明输入被消费而读回透明，但缺少Clear与正式Draw的分界、Cursor Map失败和实际管线状态。最小修改边界：HiddenWindowTest.cpp/.h与IdtMain.cpp注册一个早退诊断命令，RendererPrimitives.cpp补限频Cursor GPU日志，Collect-Win7脚本自动采集独立测试与正常应用。复用生产renderer/shader和普通FLIP_SEQUENTIAL；测试只建永不显示的隔离HWND，不向生产画布注入标记；不改shader、swapchain、输入准入或项目配置。

- [x] 新增Clear/readback、正式cursor/readback与隔离混合对照的GPU像素测试。
- [x] 补Cursor资源/Map/viewport/RTV/device状态，关闭诊断时不查询管线。
- [x] Win7 PS2采集先跑诊断、记录失败后仍采正常应用。
- [x] 独立审查、完整ARM64 Debug构建、适用无交互GUI验证；构建x64 Release候选，记录身份与最短Win7步骤。

## 2026-10-04 — Win7 22:48:36 采集反馈

用户要求继续定位。新日志EXE身份06CD58C7...DF8AE与GPU候选一致；正常应用有输入和39次原始ULW全零采样，15条Cursor GPU记录证明上传/viewport/目标/VS/PS/CB/SRV绑定正常。但identity pixel-test-error为PS2参数集冲突，独立Clear/正式Draw自检没有启动。已证明的诊断缺陷位于Collect-Win7.ps1的probe启动参数，以及随后实际WinPS复验发现的IdtMain.cpp原始CLI严格比较；本轮最小修补采集器与早退参数入口，重建并验证实际WinPS采集，再依源码/日志明确余下GPU边界。G:原始数据只读；保留旧EXE和复现身份，新候选独立记录源码delta和新hash。不猜测改输入、shader、blend或backend，不创建commit/push。

- [x] 独立新采集输入/像素时间线与共享GPU管线静态核对。
- [x] 修PS2 probe启动冲突，保留完整输出和失败继续采集；增加只跑自检的简短入口以避免再次重复手动输入测试。
- [x] 实际新版ARM64/x64 EXE经WinPS5.1 selfprobe采集、超时/大输出/非零/等待异常边界验证，独立检查；Win7 PS2现场不冒称通过。
- [x] 交付新x64 EXE与脚本修正、准确定位结论和完整身份，等待同Win7自检缺失证据，保持任务in_progress。

### 参数入口追加边界（实际无窗口证明后）

WinPS5.1真实wWinMain参数dumper证明Start-Process给单参数附加U+0020，pwsh7没有。旧GPU命令对lpCmdLine全串严格比较会失配并落入普通启动；一轮仅本任务子进程的复验因此启动了普通应用，已由精确子进程timeout结束，后续改用从不创建HWND的dumper证明。需要同步最小IdtMain.cpp修补：仅将GPU flag放入已有CommandLineToArgvW分支，正确token且argc2执行自检，额外参数返回2早退，不触碰其他CLI、产品输入或GPU呈现。因入口生产文件改变，按仓库规则重建完整Debug|ARM64与Win7 x64 Release候选，再运行新版早退CLI及实际WinPS采集；不再沿用旧EXE身份。

验证与候选身份：research/verification-followup-20261004.md。本机两配置完整构建、Headless、WinPS真实六例及collector边界和独立检查通过；8个CSO与G:采集hash保持。只待同Win7 Run-Win7-PixelTest.cmd结果，不重复正常画布手动序列。

## 2026-10-04 — 等待晚间实测期间的静态定位

用户明确本阶段只继续静态查找；Win7独立像素实测须等晚上。RTS/document已确认，仍未证明GPU非透明输出。最小工作边界为原任务research报告与知识记录：比较demo/原Inkeys3-Draw3和主产品GPU→ULW差异、审核Win7 DXGI能力/FLIP与创建成功的证据边界、资源/线程/Copy/Map/DIB生命周期以及编译shader合同。源码、候选EXE、脚本及先前修正包保持不变；不编译、GUI、commit、push、archive。仅有可因果证明的新源码缺陷才单列其最小修复建议；不凭OS标签改backend或FLIP。

- [x] demo/原独立仓库与产品的GPU→ULW差异和实测身份边界。
- [x] Win7官方DXGI/FLIP能力与实际ULW无Present1路径核验。
- [x] 更深资源/线程/Copy/Map/USER32生命周期与compiled shader合同审核。
- [x] 独立结论审查，保存优先级、反证及晚间已有自检如何分界；保持任务in_progress。

静态汇总：research/static-investigation-summary-20261004.md；独立审查check-static-investigation-20261004.md限定范围PASS。原独立Demo核心路径继承、实际两EXE嵌入shader身份/执行块已核，source统计早于USER32，未证明唯一GPU缺陷。保留prior候选/source/脚本与8个CSO/ZIP hash，不构建或运行本轮GPU/GUI。原任务等待晚间Win7六例分界。

## 2026-10-04 — 用户要求进一步比较原独立版与嵌入版

- [x] 40对模块规范化diff；四个Renderer implementation共54个同名函数体的token比较，42相同、12差异及3个新增Laser Bake方法逐项审核。
- [x] Cursor成功路径15个context调用/9个b0赋值逐项一致；原版亦是独立HWND线程，核对产品Host独立context、预热、帧门控/metrics/回调顺序。
- [x] 原仓只读历史核验：dual-source在嵌入前已有；保留Win7实际执行与旧实测版本身份边界。
- [x] 研究embedding-deep-comparison-20261004.md保存新证据与晚间既有六例的判别；本轮不改变候选或生产行为，保持原任务in_progress。

## 2026-10-04 — 第二台 Win7 11:38:50 实测反馈

新采集 EXE SHA 2D468B...D72C 与已交付候选相同，PS2 早退自检实际启动。WARP 两个已知色 Clear 通过，而正式 Cursor 和关闭混合的四个 Draw 全透明，四次 VS=6/PS=1368。正常应用使用 VMware 硬件 D3D，已存在红色笔迹和白色橡皮光标的非零源/DIB；用户仍报告几乎不显示、切到橡皮才刷新。不能把硬件非零误记为画布恢复，也不能把 WARP 自检与硬件应用混成同一设备。

本轮最小诊断边界：
- HiddenWindowTest.cpp 现有显式早退自检中，保留六例，再读回刚上传的 b0/t0，并用未变的生产 VS + 三个仅自检使用的微型预编译 PS（常色、字段校验、Cursor-only）分开写入、shader 输入和完整生产 PS。诊断 PS 源与可复现生成说明/生成头保存；不替换生产 HLSL/CSO/SHADER301–304，不添加运行时 D3DCompiler 或项目配置。
- TransparentPresentation.cpp 增加一次读取的显式诊断环境变量 INKEYS_DRAW3_ULW_FULL_UPDATE=1；唯一控制差异是 ULW 的 prcDirty 置空，GPU dirty 拷贝/DIB/输入/渲染不变。标注覆盖已启用、原请求 full/dirty 和实际提交参数，缺省行为不变；这用于判别硬件已生成像素但 Win7 桌面不及时刷新的现象，不能当作已证明生产修复。
- 原任务 Collect-Win7.ps1 最小补系统 D3D/WARP、DXGI、USER32 文件版本元数据；这些元数据不等于进程实际加载模块或完整补丁清单；新增 Full-ULW 对照启动脚本，复用已验证的 PS2 采集/ProbeOnly 入口。
- 完整 Solution Debug|ARM64 与额外 Release|x64；仅本机不显示的早退 GPU 自检和 Headless，不启动产品 GUI。交付新独立候选/身份/最短两个分界操作；保持原日志、旧包、8个CSO和无关未跟踪文件。任务 in_progress，不commit/push/archive。


追加诊断依据：新硬件日志存在 selection frame→ULW 返回6.677秒、gate wait→ready5.261秒，但缺少分段耗时，不能先归因 dirty ULW。只在现有 ULW 像素采样记录补 Copy+Map、CPU rows、USER32 调用三段毫秒耗时，诊断关闭时不计时/输出，不增加 GPU query 或更改提交/扫描。

扩展上传/PS对照后，自检最坏情况包含多次3秒 query；collector 单独 probe 等候上限从30秒改90秒，提示与identity同步，精确子进程超时清理/普通分支与ProbeOnly退出合同不变。

本轮最终验证及交付：research/verification-113850-20261004.md。
- [x] 三个诊断 PS 独立复编/header/signature 一致；正式 VS/PS/8个CSO未改。
- [x] 完整 ARM64 Debug、x64 Release；各13例GPU自检、Headless及ARM64 ULW-copy、实际WinPS ProbeOnly通过。
- [x] 额外橡皮隐藏fixture默认/全量均通过：首次隔离cwd缺Build造成保存断言失败，预建目录后同EXE全部通过，原失败证据保留。
- [x] 新包规范化为单个当前manifest；EXE A18828...EE5D，ZIP 604118...E2B9；CRC/逐文件及15项原资料保留校验通过。
- [ ] 同Win7新shader分段与VMware正常/Full-ULW视觉对照；光学恢复前维持in_progress，不commit/push/archive。

## 2026-10-05 — Collect-only 22:49 实测分界

- [x] 新采集A18828...EE5D身份核对；Collect已自动运行原六例加新七例，不要求用户重复PixelTest。
- [x] 七项上传/常色/字段/Cursor-only均PASS，正式完整PS两个目标及关混合仍透明；范围收敛到完整PS执行/程序差异，保留用例先后时序和唯一opcode未证实边界。
- [x] 正常应用Hardware失败后WARP；42个ULW源全零，1529条mouseaccepted及25对RTS已发布，Clear前17笔；不是此前VMware硬件非零源路径。采样未见秒级ULW阻塞，粗时钟及采样不当作所有帧性能证明。
- [x] 原始六文件hash保持，仅研究/会话记录，无产品修改、构建或应用/GUI测试。见research/capture-224927-summary-20261005.md与两份独立研究。
- [ ] 下一候选首先在成功对照后恢复原PS重测，排除序列/暖机效应，再分解完整PS路由；不直接以Circle-only替换所有生产功能。
- [ ] 此前Hardware源/DIB非零却不刷新的设备仍需同候选普通/Full-ULW视觉对照；当前WARP旧包不再重复，任务in_progress。

## 2026-10-05 — 单CMD shader序列及两轮ULW候选

用户授权实现；所有权/合同见research/one-click-plan-20261005.md。保留正式功能/原资料/旧包；原任务in_progress，不commit/push/archive。

- [x] 原13项与正式四项退出保持，五sequence完整PS对照追加、独立FXC/signature/header复编通过。
- [x] 单CMD三阶段、自动局部日志、环境隔离/恢复、诚实失败/不完整采集；实际WinPS mock与唯一CMD通过。
- [x] 完整Debug ARM64 + Release x64、两架构18项GPU/Headless、新EXE实际WinPS ProbeOnly、正式资源与16项原资料保持校验通过。
- [x] 独立FINAL PASS、spec合同同步、新独立单入口ZIP/manifest/CRC与逐文件核验完成。见research/verification-one-click-20261005.md。
- [ ] 两台Win7新候选三阶段采集/PS2实测/视觉分界；原任务in_progress，无commit/push/archive。

## 2026-10-06 — 22:46:20 OneClick EXE 新采集

- [x] 新EXE1CB7...C0FA身份、18项完整结果与六文件hash核对；只读现场及源码，未改产品/构建/GUI/commit。
- [x] 七diagnostic PASS、五sequence FAIL：原PS反向复验/原样复编/type4常色/前移仍0；定位完整PS Draw/readback分界，非唯一opcode。
- [x] normal Hardware失败→WARP；411mouse接受/9对RTS、6Packets，源10样本0；原生full不等于full override。详见research/capture-224620-summary-20261006.md及两份独立研究。
- [ ] compact type-route与同原PS完整typed绑定反向复验尚待下一实现；不凭哨兵FAIL宣称进入type4、不凭NULL宣称非法。
- [ ] 缺三stage及观察，VMware硬件普通/full同候选A/B未提供；Win7恢复/唯一根因仍未验收，任务in_progress。

## 2026-10-06 — 全面单CMD双后端矩阵候选

- [x] 用户授权继续实现；每可用后端87=旧18+matrix69，四路由双绑定、16资源组合+恢复、有限CB、13功能群双绑定和末尾控制；Hardware独立且仅指定初始化不可用exit3。正式shader不替换。
- [x] 双probe+两正常轮唯一CMD；schema阻止旧EXE未知hardware CLI，严格raw身份与manifest SHA/package/87/69，PS2 API兼容，失败继续/完整原流/环境恢复；实际WinPS mock通过。
- [x] 完整Debug ARM64/Release x64，两架构双后端各87 PASS、Headless、真实WinPS ProbeOnly和argv；23DXBC/static/旧六/正式资源/24原资料保持通过，独立FINAL与Trellis check限定PASS。
- [x] 新包manifest/ZIP CRC逐文件身份完成；详见research/verification-comprehensive-20261006.md。未commit/push/archive。
- [ ] 两台Win7各一次完整三阶段/观察，核WARP shader路由/绑定/功能群和VMware GPU-DIB-视觉ULW分界，未宣称唯一根因/修复；任务in_progress。
## 2026-10-08 — 全面现场 23:37:15：DefaultInk 双向分界

- [x] 原始21文件/三stage/EXE5D5254身份完整，WARP87终态；Hardware明确unavailable，未扩大为VMware验收。
- [x] matrix69: 50FAIL/19PASS，binding均PASS；16资源组及恢复、finiteCB、floatroute均不恢复。CursorWithDefaultInk独自复现；FullWithoutDefaultInk独自恢复848，末尾控制再次反向确认。
- [x] 正常/full两轮RTS已发布、笔迹保存、源62采样全零；主窗alpha1为命中底层，full prcDirty=null未改善。
- [x] 同正式源/DXBC对照：未guard的DefaultInk导数/discard尾部与SV_Position薄线helper关联，Fields position数据PASS；不把源码群分界升格为唯一JIT/opcode结论。
- [x] 临时GuardedDefaultOutput与诊断FullNoThin /ps_5_0 /O3均编译成功；正向guard候选ISGN/OSGN与正式相同，导数/discard实际移入positive block；未改生产/正式资源，Win7效果待验。
- [ ] 下一现场只隔离上述小段及真实type0/3、非零底图Retain/擦除；此前VMware硬件源非零光学路径仍待同包普通/full。保留任务in_progress，无生产变更/commit/push。

详见research/capture-233715-summary-20261008.md、逐项matrix/normal/full/hashes JSON、独立shader-review。


2026-10-08 实施增量：按research/guarded-package-plan-20261008.md分工实现GPU与collector，再独立check、两架构solution/GPU/headless及单CMD身份交付；任务保持in_progress，禁止自动commit。


2026-10-08：交付完整数学guarded shader同EXE原版/候选单CMD包；两架构双后端105项及Headless/collector/资源身份通过，Win7恢复与视觉验收仍待测。详见 research/verification-guarded-20261008.md。未commit/push，任务保持in_progress。

2026-10-09：用户授权开始修复已证实部分；按 research/formal-warp-plan-20261009.md 将实际 Win7 WARP 的默认 PS 改为已实测的完整 guarded 字节码，诊断显式 0/1 保留原版对照；荧光定格偶发与 Win7 Hardware 保留待验证，任务不结束、不提交。

2026-10-09：正式Win7 WARP默认完整shader选择已实现；两架构完整Solution、420 GPU、27策略/后端、Headless及WinPS ProbeOnly/50mock通过，2171字节与87不可变文件保持。新ZIP与EXE身份见research/verification-formal-warp-20261009.md。旧EXE同shader已在Aero两条件验收，新EXE普通启动/HW/定格偶发/PPT仍待验证；任务in_progress，无commit/push。
