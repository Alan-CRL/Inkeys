# 后续阶段本轮执行记录

2026-10-22当前授权/进度：用户已允许专用可见GUI、原生操作和截图，禁止Computer Use；以下不可见限制仅为历史。正式共享Mag91帧/20轮/取消/Freeze和真实空页系统交接通过，实际墨迹/长滚动/应用兼容性继续验收；任务保持in_progress，不commit/push/finish/archive。

基线feature/move / 3ace726b。用户批准全部后续实施；新测试只能无窗口或严格不可见HWND，禁止Computer Use、可见窗口/图案、前台和置顶测试。无新commit/push授权。具体见implementation-authorization.md。

## 执行顺序

1. 正式取帧：复用原Mag与MagnifierThread串行更新/读回、异步状态/监督/Stop/Freeze互斥、质量和元数据、无窗口测试、顺序check。
2. VisualMotionTracker：纯图像/OpenCV、可信平移与特征接力、离线序列与误差、顺序check。
3. 产品接入：ScrollFollow会话、Draw3绝对目标邮箱/结束屏障、输入/辅助呈现交接、More按钮/i18n/SVG、生命周期停止、无窗口或隐藏HWND验证、顺序check。
4. 保存回归：A->follow->B->编辑历史->保存重载的定向验证，既有Draw3/UInk/形状/定格/白板/PPT相关可执行检查，ARM64完整solution与x64尝试、i18n/PE/编码，顺序check。
5. 必要可见验收：新正式路径真实变化内容、桌面穿透和滚动墨迹、浏览器/Word/PDF/惯性/工具交接。当前未授权，若成为剩余必要步骤则停止并报告，不标全任务完成。

## 本轮起始状态（历史）

- 正式取帧实施中；线程方案已核准：原初始化/创建owner与readyPromise不变，原MagnifierThread串行执行采样/PW与Freeze，UI不等待PW。
- Draw3/Bar/持久化和Tracker正在只读调查，为后续顺序实现准备；无生产代码并行冲突。
- 上轮原Mag owner读回像素通过仅作基础能力证据，本轮新协调线程读回与完整功能需另作验收。

## 正式取帧已完成实现与check

FreezeCapture Start/Stop/GetSnapshot已接原MagnifierThread，普通Mag初始化/创建/readyPromise不变。无callback正式路径不注册回调。审查补就绪atomic与监督停止通知同步，最终完整ARM64构建29.32s exit0、定向exit0、完整--no-window 109923ms exit0。实际transform identity核验，映射1:1，不按裁切宽比缩放。详捕获子任务production-capture与production-capture-check。原异步生产读回实屏/性能/异常系统销毁未验收，非软件证据的剩余项保留到必要可见门槛。现在进入独立纯离线Tracker，不宣称产品真实屏幕验收完成。

## 全隐藏实际Host基线

在Tracker实施期间复用现有严格早退 --draw3-host-metrics-smoke，正常宿主运行exit0，约1.63s；真实WindowService/Host默认输出、五次start/stop、实际contact与present及sealed metrics通过，所有自建HWND全程visible=0、topmost=0，无Computer Use和用户数据读写。私有日志Build/ScrollFollow/hidden-baseline-8aa93957a1724173bbe29945f6cc2636/{stdout,stderr}.log。此证据只证明允许的全隐藏产品测试可执行，不等于follow或实屏验收。

## Tracker实施完成，待独立check

VisualMotionTracker生产API与严格离线矩阵freeze。完整ARM64最终build exit0/17.61s/0warning0error；最终--no-window真实exit0/157453ms/stderr0B，含6918合法帧与全部负例，末尾PASS animation correctness。所有合法prefix严格误差通过，点数<=320，具体参数/逐序列数字/代码和exe哈希见Tracker research/implementation-current.md。本轮没有实屏操作。接下来独立trellis-check，完成后再进入产品接入。

## Tracker独立审查发现并修复中

审查扩充majority-fixed中央小区域运动1/2/4/10原图px。4px被误接受零位移，6918合法帧仍过；根因为次流候选>2*1.5处理px才检查，2处理px次流被跳过。主会话批准零主模式独立>1.5候选+原15%空间/6中格的局部修复，不改普通runner-up门或严格精度。1/2原图px落原1.5处理px合并半径，作为当前分辨边界记录，不扩张新的多模态算法。修复后重跑完整ARM64/定向/--no-window，再进入产品。具体证据/修订由Tracker check-current记录。

## Tracker检查通过并进入产品

Tracker检查最终完整ARM64 build真实exit0/18.99s(外部19470ms)/0warning0error；target exit0/90771ms；full --no-window exit0/153486ms/stderr0B；7文件hash未变，原6918合法诊断逐项一致。4/10px误接受修复，1/2px合并边界明示。具体见Tracker check-current.md。现在开始产品代码与严格隐藏验证；完整实屏Capture/穿透仍未验收，不标父任务完成。

## 产品实施中与共享存储基线

Core统一Business/Draw3/IdtState/Bar交互/CLI/工程，资源实现者独立Config/enum/SVG/rc/i18n；严格不可见测试边界不变。Business接口已定稿RequestToggle/RequestStop/GetSnapshot/Available和强类型ActionTicket/hold等；实际实现由产品报告记录。

主会话同时用既有inkStrokeModelerTest.sln Debug ARM64完整构建共享storage基线exit0/55.10s(23既有warning/0error)，没有启动demo/benchmark。纯CLI在各自私有TEMP：--uink-file-only exit0/100433ms(包含page-file)，--uink-presentation-only exit0/149ms，--desktop-autosave-only exit0/2135ms。--presentation-autosave-only原长TEMP约束导致大量失败；改为短48字符Build/sfp-94de65ce后exit1/3654ms，仅剩independentlySaved Committed+SlideIdSession断言；原因尚在只读调查，不能先归为当前feature或环境。日志Build/ScrollFollow/storage-baseline-*。不为独立PPT基线问题改生产代码。


## 共享存储基线已全部通过

只读研究确认Presentation fixture在TEMP之后还追加深层session路径，48字符TEMP使最终路径达到265字符。相同二进制、相同--presentation-autosave-only，改用39字符私有TEMP后exit0/3537ms，stdout/stderr均0B。未经采集的原失败completion只作源码推断，不能报成已测GetLastError。四组既有storage CLI现在全部通过；相关legacy工程/实现/tests相对HEAD diff为空，无源码修复。证据见产品research/storage-baseline-current.md。后续legacy PPT CLI继续使用<=40字符新建私有TEMP，不修改系统或生产路径策略。

## 产品第二轮编译与定向检查

完整InkeysRepo.sln Debug ARM64第二轮exit0/91.4s，--scroll-follow-protocol-test、--bar-view-adjust-config-test、--state-mode-production-test均exit0。真实全隐藏Host首跑在Start安全拒绝；补原WindowService要求的MagHost/Freeze隐藏角色后进行第三轮构建。最终跨帧End、原动作发布receipt与精确窗口ACK仍在验证，尚未冻结或声称产品check通过。


## 产品第四轮全隐藏Host通过

第三轮C++编译已过但link.exe的IncrBuildImage出现C0000005/LNK1000；只删除已核准workspace生成Inkeys.ilk后按原工程重链，第四轮完整Solution exit0/73.5s。没有修改依赖/工具链/源码绕过链接器。新--scroll-follow-hidden-test真实ULW Host与RTS通过A起笔、被gate拒绝Down/真Up回收、latest200、End后原Undo FIFO阻挡、base-total/迟到拒绝、原输出ready及shutdown最后CPU target。End经历14帧，其中13帧仍在恢复；所有创建/销毁HWND visible=0。实际窗口显隐ACK用严格seam，不等于系统穿透已测。Core尚在补最终严格argv和投影回归，冻结后独立check。


## 产品独立check补齐Business证据与跨Host代问题

Core源码冻结26.661s exit0及报告已交独立trellis-check；full --no-window尚未跑，由最终check统一执行。主会话批准文件内harness-only最小外部依赖/clock接缝，直接运行同一个Service::Tick/Frame和真实Tracker，补F0/ACK前状态、初始化重试/失败回滚、严重与普通拒绝Pause、票据取消/退休及Shutdown软件矩阵；外部Capture/Host/Window是seam，真实Host另有全隐藏证据。

主会话读Host/Business发现跨Host旧hold风险，checker独立确认：Start重建follow并清hold没有保护门，旧Context/action release可递减新代hold。批准最小修复：Start command→presentation保护重置和Channel复制；Begin/End/Hold携带可选expected Channel，Business持旧所有者凭据，Host同门不匹配拒绝。补实际全隐藏Host重启旧End/hold/Begin不能干扰新代用例。修复/验证仍在进行，不提前标check通过。


## 产品独立check通过，保存阶段启动

产品check-current.md完整记录两项修复（跨Host通道/hold；SetOffSignal先NoWait/先Arm）和实际Tick/Frame/Tracker控制矩阵。最后完整ARM64 solution exit0/15.332s，四定向通过；唯一full --no-window exit0/157.972s，stderr35B仅protocol failures=0；原8项无GUI失败接管exit0/95.773s。46文本/4XML/diff/i18n426检查通过，未新增已确认源码缺口。新expected Channel合同已同步spec。当前实际Mag/系统ACK/Bar可见尚未验收。

保存子任务已validate/start in_progress，新的implement agent统一拥有真实私有文件/编辑测试和必要hidden有限receipt，源码预期不改原保存/历史/格式。以产品check-current及viewport-only-save-current为真实前置，不新增冷载功能、不复制已有seq/跨代专测。完整x64/最终PE由主会话等最终源码统筹，期间不并发MSBuild。


## 保存实施冻结并进入独立check

最终完整ARM64 solution exit0/7.926s（原配置重链），新--scroll-follow-persistence-test四场景Shape/Pen viewport-only/edit exit0/5.237s，共8个实际UInk。每个场景确认Clear Committed被Controller消费、CPUfallback释放后，Undo真实worker读取并安装A（diskInstalled=1、memoryRestored=0）；A所有点/宽/style/GUID不变，最终viewport(144,-64,1)，B真实Down(80,100)锁存新视口得到(224,36)。Pen实际23/22点，不以2点Shape代普通笔。viewport-only最后Publish立即Stop、不预等Applied。mask实际文件几何正覆盖而非屏幕像素证明；7badargv全exit2/0B，9文本/2XML/diff通过。详细见保存research/implementation-current.md。源码冻结后已交原产品checker独立审查本阶段及全范围交界；主会话随后串行x64与PE，不并发MSBuild。正式Mag/系统ACK/实屏仍为必要未测门槛。


## 最终软件验收完成，停在必要现场门槛

保存独立check仅加强equalStroke零偏移A/codec逐值精确比较，无生产逻辑变化；最终完整ARM64 exit0/8.143s，四场景exit0/4.194s、8真UInk、Pen23/23点；protocol/Business/state/config/隐藏Host/shape/capture短回归及badargv通过。源码冻结后主会话串行完整x64：首次109.839s在Headless生成PDB损坏LNK1285失败，主程序已成功；核实普通/ignored/非reparse工作区PDB后同目录留档，同配置solution重建exit0/6.791s，五项x64纯CLI通过。两架构PE确认Mag延迟加载、无OpenCV/WGC动态依赖，版本查询动态，无扩旧系统已测承诺。详细见final-build-and-pe.md。四阶段软件check均已结束；规范、manifest、总矩阵与用法已更新。当前只剩正式Mag连续像素/排除/FPS与系统窗口/实际内容/硬件现场门槛，按本轮限制停止，不Computer Use/显示/前置窗口、不归档全任务、不commit/push。


## 2026-10-22 本地提交授权，保留任务

用户要求“为改动提交 commit，不结束任务，后面再开始进行测试”。本次仅提交既有软件实现、规范及验证记录，不启动后续测试、不push、不调用finish/archive；父任务和当前保存子任务保持in_progress，正式Mag/系统窗口/现场验收仍待后续。此授权仅适用于本次本地提交。


## 2026-10-22 可见测试安全停止

专用早退harness完成原worker实际17次NearBlack观测，但源画面全屏GT未证实；后续owner/worker可分辨实验在Start前被屏幕GT门阻止。normal启动、cloak0/Default/Active仍不足：确认全屏LockScreenBackstopFrame(explorer.exe)遮挡，source GT0/54、蓝层0/9。未越过/隐藏/最小化安全窗口；停止所有GUI，不再截图。仅补harness安全早退和无GUI验证，等待用户手动解锁后重跑，不将有限黑帧观测误归因PrintWindow/Mag刷新。


## 用户已手动解锁，恢复测试

用户通过异步提问明确回复“已手动解锁，继续测试”。此前安全停止解除，仍禁止Computer Use；解锁前结果作为独立历史保留，不将17NearBlack归因于PW/Mag，也不追溯认定全由锁屏导致。独立checker统一接管source/build/test，先安全静态核对，再无probe正常正式路径，以完整source/overlay屏幕GT为前提；需要时再分步对照。若安全层重现立即停止，不尝试绕过。


## 解锁后正式取帧首轮通过（2026-10-22）

原共享Mag pair、原MagnifierThread、无scaling callback、Host alpha=0路径真实通过。私有日志 `Build/ScrollFollow/live-2484-210352218/result.log`，正式运行 `live-check-unlocked-formal.log`：exit0/6.364s，12帧、4个变化阶段各3帧全部匹配，accepted12、rejected/API/unmatched/old/wrongThread均0；owner22192、worker79832同PID2484，MagInitialize/窗口创建/MagUninitialize仍在原owner，正式读回保持原worker。5张专用ROI图逐像素与对应公式RGB完全一致，alpha255。原17NearBlack保留为前置GT不足的历史观察，不据此归因PW/Mag/线程。

12次Backend::Read测量包含geometry检查、清DIB、PW(2)+GdiFlush和alpha整理，并非纯PrintWindow耗时：min58.146、mean70.975、median65.974、P95(nearest-rank)/max85.867ms。首次Start计时至首Read完成972.785ms，含固定800ms准备等待，首Read84.800ms；不包含更早窗口创建。排除phase切换后8个同phase样本间隔mean149.822ms，短夹具约6.67次/s，不能声称达到20FPS或长期稳定性能。

当前进入同进程20次启停/取消/Busy/资源与实际Freeze互斥门槛；尚未标记真实产品窗口ACK、屏幕墨迹跟随和应用兼容性通过。正常路径已经成功，不再运行不必要的owner-only侵入probe。源码/build/GUI串行由implement→check交接，任务保持in_progress，不commit/push。


## 扩展测试构建通过，运行前再次安全停止（2026-10-22）

仅正常live夹具已扩展20轮同pair启停、Busy、新session/seq、停止后迟到、5秒静止分布、两次实际Freeze与中途取消；未运行owner-only probe及其private消息/worker hook已从源码移除。完整ARM64 solution `live-lifecycle-first` exit0/23.820s。随后独立子进程PID53668在安全界面最前门即退出4/1.204s，stdout0、stderr `blocked=security-interface-before-initialization`；没有初始化Display/创建夹具窗/Start Capture。故新20轮和Freeze实际验收仍未执行，不把编译写成通过，也不推翻此前解锁后的12帧正确结果。

当前已停止GUI并再次请求人工解锁，等待回复期间仅离线检查/后续产品夹具暂存，不重试安全层、不阻止锁屏、不操作安全窗口。


## 用户再次手动解锁，立即执行已构建矩阵（2026-10-22）

用户对第二次安全停止明确回复“已解锁，继续测试”。恢复同轮专用GUI/原生操作/截图授权，仍禁止Computer Use；不尝试阻止再次锁屏。完整ARM64 `product-live-include-order` exit0/24.609s已包含read-stop严格修正和最小product夹具，源码/build冻结；先串行运行正常capture20轮/Freeze矩阵，独立checker只读审查后续产品夹具。实际结果另记，不由本授权预先标通过。


## 正式生命周期矩阵实际通过（2026-10-22，再次解锁后）

`live-lifecycle-unlocked-test.json`：PID26916，exit0/41.388s，当前EXE SHA256 F84BF2037B033876AB3EF8BAC2DC7A67D4F24CEB4319A416A6B1DBA80E1BA8C6；私有原始 `Build/ScrollFollow/live-26916-212248453/result.log` 与5张专用PNG。无callback、原共享pair、原worker，共91个正式callback，unmatched/old/wrongThread/apiFailures/sequenceErrors均0，原pair销毁、owner MagUninitialize成功。

20轮session2–21均验证Busy、真实当前图案、seq、Stop/drain后150ms无新回调；热身后每轮GDI108/USER61完全相同。它证明这20轮热态资源计数稳定，不能称初始零泄漏；末尾进程资源总数11→84/4→34另保留，不以pair销毁代替所有进程资源归零。立即Stop无callback；session23首试真正Read撤销命中accepted_at_revoke0/final0/callback0/lateResults1/actual_read_revoke1，无race-missed。

两次实际原Freeze Reveal(alpha255)/Conceal，以及活跃capture→Freeze先撤销并排空(reason2)、Freeze active拒绝Start(status3)、退出后再捕获3正确帧通过。此为原owner/coordinator与显示状态验收，尚非Freeze屏幕像素或真实产品双画布ACK。

静止34帧覆盖4.840s，约6.818次/s；Backend::Read均值67.448ms、lower-empirical P95 97.556ms、max102.241ms；33间隔均值146.679ms、同口径P95 174.406ms。完整Read仍含DIB/检查/PW/GdiFlush，不称纯PW或长期FPS，也未达到20Hz目标。新旧百分位口径不同，面向用户优先用均值和实测帧率，不混用。

捕获GUI进程已退出，真实产品最小夹具由checker先补取消/实际cursor命中/安全异常关闭Show入口等有限安全检查、完整ARM64build，再串行实际运行。父/本任务保持in_progress，无ComputerUse/commit/push。


## 真实空页产品整链通过（2026-10-22）

实际测试先在own-source-foreground处安全失败，后一次真实Host虽启动却Initializing停住并cleanup AV（PID72368，真实root为product-live-72368-213134453）。WER只确认本进程c0000005/ntdll+0x2CB00，没有dump或调用栈；不把该地址映射为某函数或声称唯一根因。调查确认Business自带16ms worker，并非缺Tick；早期裸Bar UpdateRendering在未初始化粗细文字时有确定危险访问，Initializing/Stopping/Off均可达，现最小改用已有RefreshToolState就绪门。fixture原STA导致RTS RPC_E_CHANGED_MODE，改为与普通Main一致的MTA。原设计报告已加勘误，不再引用“preset早退足以安全”的旧推断。

当前完整ARM64 `product-live-ui-ready-mta` exit0/25.297s，8坏argv与protocol/state短回归通过；EXE SHA256 84041939F4BEC23BBCCB4840D312354C6EFB0A81DF7905C2CEE84D34633AE9B2。随后真实GUI `live-product-ui-ready-mta-unlocked-test.json` PID53500 exit0/14.081s，私有root从stdout首行确认为 `Build/ScrollFollow/product-live-53500-213622515`。真实RTS RtsExtended/input_mode1，Business/Capture session1、accepted30/rejected0/API0，实际系统ACK1。

OS滚轮delta-120被source接收，使内容世界offset从(256,512)到(256,528)，真实Tracker累计(0,-16)、owner latest/applied30、最终Viewport(0,16)，无clamp。Tracking空页真实pair(0,0)，Off逆交接真实ACK1/pair(1,0)，最终视口再次对GT通过；Source前后和Off的99点+精确ROI均正确。State/Mag返回并join、failed0，product/service/magReady/captureOccupied/ownLeftHeld清理为0，正常result0。

这只通过空页真实协议/滚轮/视口/退出，不是可见墨迹、真实Pen contact或文件读回验收。core现扩有墨迹A/正反滚动/Off后B与屏幕像素，checker独立离线复核本结果，任务仍in_progress。


## 2026-10-23 当前软件与实际长程进度

最小生产校准双向LK maxLevel3→5，原6918帧、新704帧及4大图安全负例通过独立check。测试GT批量内存快照后，原99点/精确ROI/128px/19200px/480s监督保持；完整原生long PID6740 exit0/160.123s，302步十屏去返、初点退休后持续1112帧、A最大1px、正常Off/B及实际v10文件/cleanup通过独立核。旧失败和取消保持原结果。

更新CV后完整Headless --no-window PID20032、CC3F产物、exit0/307.752s；私有短TEMP与900000ms预算，原始日志native-lk5-final-no-window-1023*。i18n en-US/zh-TW各426/426通过。当前最小external-empty Edge验证入口在实施，未运行浏览器；该case只核网页底图/空页视口，浏览器墨迹/Word/PDF/物理硬件及旧系统仍待分别验收。所有验证无Computer Use，不commit/push/finish/archive。


## 2026-10-23 外部Edge入口实测进展

最小external-empty入口已构建。最终ARM64完整solution exit0/26.523s，x64先有确定u8path源码错误（最小改为显式u8string路径），修正后遭生成PDB LNK1285；核实并保留损坏生成文件后原配置重链exit0/10.871s。两架构35组坏argv全拒绝，协议/状态短回归通过；新ARM64 3CD17F64原生short PID24580 exit0/6.428s、8PNG/两Pen/实际v10独立通过。未变更工程、SDK、依赖或已有native长程helper。

四次专用Edge测试保留失败：首例PowerShell异步VoidTaskResult污染返回导致连接调用失败（留存私有Edge经严格身份核对只关闭其CDP）；第二、三例32格标记被同一私有Edge附加root覆盖，均在启动Inkeys前停止。第四例external-239978d8a132425f9113b9683f62d82b单次受控Escape关闭其附加层，32格屏幕颜色全部精确；Inkeys37692到真实Tracking/session1、窗口ACK1/空pair(0,0)，但OS滚轮前foreground/hit组合门失败，零滚轮。两进程正常退出，ownEscapeHeld=false、cleanupErrors=[]。当前准备具体foreground/hit事实与仅对本次持有身份的普通前台交接，不削弱像素、输入、安全或窗口门。尚不能称真实Edge滚动通过。


## 第五次Edge：晚出的同进程前台窗口，追踪按规则暂停

external-7bcbada4b4a242cd856c52993d5943d0 / Edge22348 / Inkeys33788 / 3CD17F64，runner exit1/9863ms。单次Esc及32标记全部通过；Tracking-ready后5.611–5.716s实际mainroot2494056前台/内容命中正确，无需也未请求再激活。6.680s原32色全部精确；8.339sWheel入口首检查前，前台成为同Edge另一个root594280、Chrome_WidgetWin_1；内容中心和cursor仍命中mainroot，尚未执行Move或wheel。没有os_input/wheel-result。

独立核stderr：frame1 Initialized及2–19 None共19可信帧保持total0；20–22连续InsufficientFeatures后真实Paused，最后可信/owner序号19，Capture22质量/API均0。不能省略这个暂停，也不能仅凭当前元数据判定额外UI类型或Mag某帧像素错误。两进程正常退出/ownEscapeHeld=false/cleanupErrors=[]。下一步仅识别本私有Edge晚出UI，不削弱前台/像素/暂停门，不改变产品捕获源或用GT回填。


## Edge晚出UI独立元数据观察（非产品测试）

`external-f34856995592475b86d94c393c42f365`，显式SetupDiagnostic，Edge29808，receipt exit0/15423ms，实际观察12016ms、产品not_run，InkeysPid0。初始32色通过，2.801s主窗2690650/内容3346030正常；4.951s本进程额外前台root2429132出现，rect(916,48,1964,776)、owner2690650、标题空，持续到观察结束。没有滚轮/新激活/后续Escape/截图。正常关闭自身浏览器，cleanupErrors=[]/ownEscapeHeld=false。这证明专用Edge在没有Inkeys时也会晚出该UI，仍不能仅凭类名/空标题确认其具体内容，也不追溯认定先前每次暂停均唯一由它造成。

下一步只截取严格身份及主窗覆盖核验过的该popup局部，辨识普通测试准备界面；不保存桌面全图，不更换生产捕获源。


## 同步提示识别与Guest准备（2026-10-23）

第1张局部BMP仅见压暗底图，不能识别UI；新增仅诊断的1000ms同popup受检等待后，`external-55987efebf1b4089b4c77eb960bd6fa9`局部图由主会话视觉确认为Edge自动使用Windows登录账号并同步的通知（文档不记录账号标识）。该例receipt exit0/14969ms/not_run、guarded_wait1006ms，仍未启动Inkeys/发wheel；没有点击Got it/Customize、改变账号或读取同步数据。它识别这次额外UI，不反推所有旧失败/暂停的唯一原因。

已仅增加专用Edge启动`--guest`请求，保留独立profile/端口0、原所有身份/像素/输入/安全门，不改prefs/策略/注册表。微软Guest说明与上游开关primary来源见保存子任务external实现报告。新metadata-only `external-04993c976cb847a9941205d846838ec2`，Edge35184实际标题为本nonce加[Guest]；观察12002ms前台/真实内容命中保持，未再出现晚出UI。receipt exit0/15768ms、Inkeys0/noWheel/noScreenshot、正常close/cleanupErrors=[]。这是Guest准备通过，仍不是外部滚动或墨迹通过。

本任务此前创建的8个普通测试profile尝试本地清理：2个已删、6个因AccessDenied未完全删除，准确路径见`Build/ScrollFollow/external-entry-stage/private-profile-cleanup-1023.json`。只操作明确workspace自建profile，事前核绝对路径/无reparse/原root退出收据；未改ACL、用户常用profile或云数据。CIM命令行不可读，已知task root/直接子进程为0不足证明全树零残留，收据明确不作全树静止保证。源码/日志/截图保留，无重复绕过尝试。


## Guest首次真实滚动未通过（2026-10-23）

`external-5a936e9fd223411ebce0e72857c18354`，Edge30812/Inkeys31152/3CD17F64，receipt exit1/9238ms（末cleanup日志9252ms）。原setup一次Esc/精确32色通过，Tracking/ACK后实际前台/内容命中一直正确，无先前同步UI。6.520s实际OS Move及wheel(-120)各一次；网页wheelCount0→1、scrollCount0→10，稳定后CSS scrollY0→100、DPR2，即内容向上200个物理像素。before/after32屏幕标记全部精确，独立观测仅作断言，未写目标。

真正未通过点：Tracker frame1 Initialize及2–15接受零位移；16–18连续InconsistentTranslation后真实Paused，owner latest/applied15、total/viewport均0。失败时133inliers/177fbValid；此比值不能代替空间权重，当前verbose没有输出全部既有统计，暂不能区分两处同名拒绝门。Capture18质量/API失败0与屏幕标记正确不能证明实际Mag三帧逐像素正确。没有Off-ready/final产品PASS；正常失败收尾两进程退出、线程/service/capture/自身按键释放、cleanupErrors=[]。

当前仅追加已有统计到verbose，先定位门槛，再决定是否需要保存实际Mag帧回放；不改步长/ROI/阈值或以GT补偿。该实测失败保留，不用原生纹理长程成功替代真实浏览器兼容。


## 相同200px一成一败，现已定位空间权重门

仅追加既有统计的完整ARM64 build external-motion-gates-1023 exit0/15.728s，3条既有hashlib警告；Business67E1844B、Tracker仍BF3409F8，EXE29BB15DD。`external-750dddd0b78443b58ea7f25796eeb1cd` receipt exit0/9658ms，真实同200px GT、四阶段128RGB精确、frame9接受−140.0001、10接受−200.0001、15最终视口200.000061，原Off精确ACK1/pair(1,0)/final、全部清理通过独立核。此为限定空页Guest单次PASS，不证明墨迹/保存，也不能把加日志说成修复。DiagnosticOutput累计dropped最大2 contention，不能声称所有诊断零丢弃。

随后`external-46cc7129b8cd4621b8a47d1d8842a840` receipt exit1/9110ms，同一真实120滚轮/独立200px GT。frame9–11都134inliers/178fbValid、point比.752809、survival.611684；但spatial比.611565<.65，明确触发Coverage前的一致性if，reference8不变，三拒绝后Paused/viewport0。不追溯替旧5a936补具体门，不再无意义重复第二次定位。问题尚未解决，正实现受检、有界、只读的实际Tracker输入全前缀，用于图像核对及精确回放；不改ROI/步长/阈值/Target。


## 录制版已构建，续测在安全界面前停止（2026-10-23）

仅显式 -RecordMotionFrames 启用实际输入录制，不改变正式Mag、追踪参数、ROI或目标。ARM64/x64完整solution分别exit0/27.351s和25.809s；每架构35坏argv、协议/状态及共18项录制token门禁通过。PE未发现新增受限硬导入。ARM64 EXE B7B0E655…，x64 A4D40BE7…。录制器及独立回放器已编译和静态检查，尚无实际帧数据，不能标记回放通过。

收到“继续”后，原默认短测试 product-ink-trace-resume-1023 / PID38308 于156ms内exit4，唯一错误 security-before-initialization；没有创建夹具窗口、绘制或截屏。随后仅查询窗口元数据：中央命中可见、全屏、未cloak的LockScreenBackstopFrame（HWND1118762/PID13084）；当前与输入桌面同名Default不代表安全层已消失。只读helper首次字符串声明缺Unicode导致名字仅首字母，修正声明后重新查询，准确收据为 Build/ScrollFollow/desktop-readonly-resume-1023.json；未据无效首字母结果作判断。没有修改产品安全判断或绕过安全层。

已停止所有GUI/输入/截图，等待人工解锁。下次先原默认短回归，再一次Guest实际输入录制、原算法完整前缀回放和图像核对。现有Edge单次200px成功与两次失败均保留；最近失败空间一致性约61.2%，低于65%门槛，原因仍待实际图像确认，不把录制辅助代码当算法修复。Word/PDF、外部墨迹及硬件/旧系统仍未测。不commit/push/finish/archive，父子任务保持in_progress。


## 解锁后实际录制与精确回放（2026-10-23）

用户手动解锁后，原默认short PID33048/B7B0 exit0/6264ms，独立核A辅助交接0px、后续最大1px、两笔224/222点、实际UInk/索引/viewport(0,16,1)与清理通过。两次Edge初始化准备失败未启动Inkeys/无trace，不是Mag失败；第二次白名单诊断确认首次oracle尚不存在，未获得document.readyState证据。测试脚本增加仅首个8s总预算内、确切URL/target且visible的有限准备等待；loading/interactive未发布或身份全匹配ready=false才可等，首有效观察永久关闭豁免，后续检查严格。两次review修正flag位置后runner627AD382冻结，无生产源码修改。

actual external-a6ce9b4f7df64b77951793715eba066c：真实Guest OS wheel一次，独立CSS100×DPR2=200物理px；Inkeys13172/Edge36724，receipt exit1/9401ms，线程/自身按键/两进程正常清理。捕获13有效结果；Tracker实际Initialize1、AwaitTracking2（不调用Process）、Process10，8次接受、3次拒绝。frame10可信totalY=-49.0067138671875；11..13均143内点/177FB、37cells、空间支持24/37=.6486486486<.65，第三次Paused，保持最后可信位置。产品本次仍失败，不能把录制成功当跟随通过。

完整actual前缀保存在 Build/ScrollFollow/product-live-13172-19751750：13张原完整分辨率Gray8 PGM，ROI(0,48)/2880×1808，actual源2880×1919，像素67691520B，complete/drain true、skipped0，manifest SHA D6F61369…。原算法89CCDE6D回放exit0/492ms，13个操作全部结果/非计时统计差异0、累计位移差0，明确可离线复现相同失败，不依赖实时Mag调用。

独立像素对应不调用LK：对已知专用正文区域(400,250)-(2500,1400)，搜索整数上移并逐像素核对2415000px，frame1/9=0px、10=49px、11..13=200px，各正确对齐全像素一致。进一步ref10→11的151px对应在正文下部也正确；ref9对底边不能跨浏览器固定水平滚动条外推。结论限已核专用正文区域：本run截图确实更新到200px，失败直接位于追踪一致性门，不能据此承诺全部桌面像素/所有应用无捕获问题。

仅ignored原算法副本加只读逐点/逐格打印，编译exit0、actual回放仍differences0；生产Tracker仍BF3409F8未变。37个FB占用格中24格全主流、13格全错误，错误格集中底两行且多为1..4点，每格原票仍为1。正在核对稀疏格票对结果的影响，未应用权重/门槛修复。仍不commit/push/finish/archive，父子任务in_progress。


## 稀疏票最小修正与正式默认路径复测（2026-10-23）

实际13帧已证明正文正确200px更新、原算法精确回放停49px；只校准fit/出生锚点的每点票1/max(格内FB点数,3)，spatial分母改实际fit总票，每格仍最多1票。原单位格1/n票独立用于全部歧义检查，原.65/40/FB/NonTranslation/Coverage/跳变/可信参考/三拒绝Paused等保留。源7936941A、测试E72D3158；原全部旧case body逐字不改，添加4个真实像素保护case。

隔离候选原14组6918+native22组704合法帧和4大图负例全部通过（matrix0/202135ms）；实际旧trace候选跟随199.999939px，对旧失败expected的89字段差异为预期，不称exact replay PASS。新保护夹具V1曾15断言失败；BF原算法对照也选择相同稀疏丢失的次流（.333不足门），并非候选新增回归。修正夹具中两清晰反向流从±8改±4全图px以保证稀疏点真实FB存活，仍相距4处理px>双半径3，且删不等价的单项affineVariation前提，保留原拒绝GT及所有产品门。V2四case/16Process全部通过：minority原unit票.25、双密度opposite各.5、8格56FB因Coverage拒绝、最后参考及第三次Paused和不恢复均通过。V1旧失败完整保留。

完整ARM solution默认沙箱第一次C1902三错误，随后不改源码/配置/cache在正常宿主原命令exit0/15130ms；x64完整原配置exit0/23919ms。ARM EXE8E3005FA、x64 DA0D0726。两架构各35坏argv均exit2/无输出，协议/状态0；PE没有新增受限硬导入，Mag仍delay，x64无WaitOnAddress。i18n两语言426/426。完整新Headless B09ABE8B --no-window 单次exit0/501287ms，原私有短TEMP Build/Hc2e，原全部无窗口套件和新保护case通过；不能把先前307752ms旧binary测试当本次结果。

新正式默认native short PID38824/6071ms通过：A核心四次最大0px，B与实际v10文件/索引身份、两Pen224/231、viewport(0,16,1)及实际输入量化界均核准，线程/按键/捕获正常清理。浏览器准备的hidden初态是产品启动前问题：先验证本次唯一nonce Guest主窗并单次既有activation，再在原8s内只读等待其报告visible+ready；hidden过渡仅ready=false/noInkeys/未有效Observe且全部身份正确可等，绝不hidden读屏或启动捕获，后续hidden仍硬fail。旧7be/a810准备失败保留，不记产品失败。

最终无录制辅助的正式default实跑 external-dd5ff406117a43adb551108e1b4ee9d7，receipt exit0/16129ms，Inkeys3384/Edge27584。页面真实OS滚轮一次，独立CSS100×DPR2=200物理px；实际Tracker经3.0023/151.0022到200px，16捕获全接受、无拒绝/暂停，owner latest/applied16，正常Off实际ACK及pair(1,0)、最终viewport(0,200)、阶段1..5身份与4×32屏幕颜色全匹配，两进程/原线程/捕获/自身按键全部释放。准备初态其他前台PID19032只读元数据，未操作它；约4s过渡后本次页面真正可见才进入读屏/产品。这是限定Guest专用空页默认跟随PASS，不证明浏览器可见墨迹/保存或所有页面兼容。

共用原定格Mag及PrintWindow(child,...,2)保持不变，无缩放回调依赖/第二套Mag/替代截图方案。新模型未重新跑原生真实十屏GUI，旧模型十屏证据不追溯当新版通过；新版十屏离线/默认短墨迹及Guest空页实际通过。浏览器可见墨迹/普通tab、Word/PDF、真实笔触控/旧系统/显示和设备现场项仍待测；20FPS目标未达证明，原专用实测约6.8FPS仍保留。旧6个临时profile拒绝访问未完全清理事实不改判，当前不绕过ACL。不commit/push/finish/archive，父子任务in_progress。
