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
