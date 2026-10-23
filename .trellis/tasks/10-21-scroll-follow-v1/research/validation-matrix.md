# 验收矩阵（本轮持续更新）

软件基线feature/move / 3ace726b，后续用户要求的提交为10abcac3。2026-10-22当前授权允许专用可见窗口、原生输入和截图，禁止Computer Use；历史不可见限制仅适用于当时。正式共享Mag91帧/20轮和真实空页整链已经通过；可见墨迹、长滚动与应用矩阵继续逐项验证，安全层出现即停止GUI。任务in_progress，不自动commit/push。旧诊断和当前正式生产路径分别记录，不相互代替。

| 阶段 | 项目 | 当前状态 / 证据 |
| --- | --- | --- |
| 原Mag设计 | 同线程初始化/窗口创建、readyPromise | 保留原WindowService设计；普通Freeze分工不改，无第二Mag/lease/owner RPC |
| 基础读回（上轮） | 真Inkeys原Mag无callback冷首帧/变化/静止/排除 | 3ace726b已验证，两新进程全部接受样本63/63、14张PNG整图零差异；允许延迟，非当前worker正式连续证据 |
| 正式取帧 | Start/Stop/监督/质量/元数据/单DIB/Freeze互斥 | 已实现并顺序check；原MagnifierThread串行PW，不依赖callback。API与边界见production-capture[-check].md |
| 正式取帧测试 | fake backend、阻塞消费者/超时/迟到/Busy/停止竞态 | 定向exit0约2.48s；原source/client差异但identity映射1:1。新监督idle/active各64次join通过，无HWND |
| 正式取帧构建/回归 | ARM64完整solution、--no-window、格式 | check最终29.32s exit0/0error，62既有warning；--no-window约109.92s exit0/stderr空；XML/BOM/CRLF/diff通过 |
| 正式取帧实屏 | 新worker真实变化/排除/持续FPS/资源/异常OS销毁 | 正式原worker无callback91回调/20轮/取消/Freeze互斥实测通过；静止约6.8FPS、Read均值67ms，热态20轮资源稳定。20FPS/长期性能与异常OS销毁仍待实测 |
| Tracker | 独立LK/纯平移/可信度/特征接力及严格离线误差 | 实现/独立check通过；6918合法帧全prefix通过，4px误接受修复，ARM64/target/--no-window全exit0；明确1/2px少数运动合并边界 |
| 隐藏测试基线 | 实际WindowService/Host输入/present/重复Stop | 既有--draw3-host-metrics-smoke exit0，私有日志hidden-baseline-8aa93957a1724173bbe29945f6cc2636，所有自建窗visible0/topmost0 |
| 会话/Draw3 | Begin/absolute latest槽/seq和scene/End/present/gate/Tile | 实现/独立check通过：实际Tick/Frame/Tracker控制矩阵、全隐藏真Host/跨代/输入/End恢复/clamp/final；新所有权与退出监督问题已修，系统窗口ACK仍seam |
| 主栏/生命周期 | More按钮/三语SVG/补缺配置/工具工作区退出 | 资源/迁移及Core接线独立check通过，config/state-mode/shortcut纯业务CLI exit0，i18n426/426；实际Bar可见/系统交接仍未验 |
| 存储/编辑 | A->follow->B->history->真实Exit/Clear保存重载->再follow | 四场景Shape/Pen独立check exit0/4.194s：8真UInk、CPUfallback释放后disk安装、原A/codec点/宽/style逐值相等/GUID、B新视口、真mask覆盖及Undo/Redo，viewport-only最后Publish立即Stop均通过。四组既有storage基线通过；任意跨进程冷载仍非已有产品能力 |
| 最终检查 | ARM64+x64尝试、全部允许测试、i18n/PE/格式 | 最终ARM64 exit0/8.143s；x64同配置修复生成PDB后完整solution exit0/6.791s及5纯CLI通过；两架构PE/47文本/4XML/i18n/diff通过。产品full --no-window exit0/157.972s及退出8例复用理由见保存check |
| 必要可见验收 | 正式取帧、桌面穿透/辅助交接/墨迹、浏览器WordPDF/主栏 | 当前已授权并继续：正式捕获与空页真实系统交接通过；墨迹/应用/主栏仍逐项验收，不标全功能通过 |
| 硬件/平台 | 笔触控、Win8.1/Win7/WOW64、锁屏/多屏/DPI现场 | 未实测，须分别给补测步骤；不由ARM64或隐藏通过推定 |

## 2026-10-22 实屏验收继续

当前基线10abcac3。用户新授权允许创建可见窗口、原生操作和截图，禁止Computer Use；上表“本轮不覆盖/不可见”描述2026-10-21软件交付时权限，当前以live-test-authorization.md为准。正式worker最小夹具已实测失败：补齐原owner链后Start=Started，但约5秒17次Read全部NearBlack、accepted=0/rejected=17、API失败=0；进程正常清理exit1。当前仅继续同一Mag内owner/worker读回和刷新位置对照定位；未取得F0，暂停20次启停/Freeze矩阵与真实产品整链，不把状态/API成功当像素通过。

只读normal-host预检：threadDesktop=Default、inputDesktop=Default、nameQuery=True；未发现既有Inkeys进程。未切换/解锁desktop或修改用户进程。此预检只确认桌面名称一致，不代替真正像素/呈现测试。

## 当前证据

- 捕获子任务research/production-capture.md与production-capture-check.md：正式接口、线程/寿命、实现/审查结果及实屏边界。
- research/execution-current.md：当前阶段、每阶段顺序交接和本轮授权。
- 捕获子任务research/tracker-design-current.md和product-integration-current.md：只读研究与实施建议，不等于测试通过。
- Build/ScrollFollow/production-capture-*和hidden-baseline-*：当前日志（gitignored）。

## 上轮与历史证据

原Mag owner图案/回调/整图像素详捕获research/freeze-printwindow.md；约47ms读回、首等800ms不等于20FPS通过。旧独立服务shared Filter50/NearBlack、旧cold、快速WGC/PW和x64 C1060/LNK1140保留于capture-poc/check/quick报告，原型生产代码已删除，不构成当前正式服务成功或失败的判断。先前Computer Use授权仅其轮次有效，本轮不使用。

## 最终软件交付

四阶段软件部分与顺序独立check已完成；正式worker实际画面及系统交接仍未验收。交付总说明见delivery-current.md，最终双架构构建/PDB留档/PE见final-build-and-pe.md，使用与具体现场步骤见usage-and-live-acceptance.md。本轮未创建可见/前台窗口、未Computer Use、未commit/push，不归档父任务。

正式首轮日志：Build/ScrollFollow/live-owner-chain-test*、live-16424-207981343/result.log。worker37736、owner63032；Filter/Source/Invalidate/Redraw成功，MagUninitialize在原owner成功，pair已销毁。首次live-first-test仅缺夹具owner链导致overlay-show失败，在Start前结束，不能算一次PW失败。

## 历史安全停止：锁屏遮挡层（2026-10-22，已手动解锁）

后续可见性对照识别中心外部命中 HWND=0x7E1046 / PID13716 / class=LockScreenBackstopFrame / basename=explorer.exe，bounds全主屏(0,0,2880,1920)，Topmost=1。专用source在蓝层外屏幕GT为0/54、蓝层为0/9；窗口逻辑visible/topmost、DWM cloak0、Default桌面和WTS Active均不足证明测试内容实际显示。只对自身窗口提升/激活，未动该安全窗口，全部测试窗口自动退出。

已停止一切GUI/截图测试，等待用户手动解锁。此前17NearBlack是有效原始观测，但仅有蓝中心+painted1而无完整source屏幕GT，不能由此认定PW/Mag刷新或跨线程故障，也不能追溯断言全由锁屏造成。owner-only Flush/PW/Redraw的对照实际上没有执行；正式首帧/连续变化/20次启停/Freeze/真实产品整链仍未通过。下一步只做安全class早退与无GUI静态/编译检查，然后保留任务等待解锁，不绕过锁屏。


## 用户已手动解锁，恢复测试

用户通过异步提问明确回复“已手动解锁，继续测试”。此前安全停止解除，仍禁止Computer Use；解锁前结果作为独立历史保留，不将17NearBlack归因于PW/Mag，也不追溯认定全由锁屏导致。独立checker统一接管source/build/test，先安全静态核对，再无probe正常正式路径，以完整source/overlay屏幕GT为前提；需要时再分步对照。若安全层重现立即停止，不尝试绕过。


## 解锁后正式取帧首轮通过（2026-10-22）

原共享Mag pair、原MagnifierThread、无scaling callback、Host alpha=0路径真实通过。私有日志 `Build/ScrollFollow/live-2484-210352218/result.log`，正式运行 `live-check-unlocked-formal.log`：exit0/6.364s，12帧、4个变化阶段各3帧全部匹配，accepted12、rejected/API/unmatched/old/wrongThread均0；owner22192、worker79832同PID2484，MagInitialize/窗口创建/MagUninitialize仍在原owner，正式读回保持原worker。5张专用ROI图逐像素与对应公式RGB完全一致，alpha255。原17NearBlack保留为前置GT不足的历史观察，不据此归因PW/Mag/线程。

12次Backend::Read测量包含geometry检查、清DIB、PW(2)+GdiFlush和alpha整理，并非纯PrintWindow耗时：min58.146、mean70.975、median65.974、P95(nearest-rank)/max85.867ms。首次Start计时至首Read完成972.785ms，含固定800ms准备等待，首Read84.800ms；不包含更早窗口创建。排除phase切换后8个同phase样本间隔mean149.822ms，短夹具约6.67次/s，不能声称达到20FPS或长期稳定性能。

当前进入同进程20次启停/取消/Busy/资源与实际Freeze互斥门槛；尚未标记真实产品窗口ACK、屏幕墨迹跟随和应用兼容性通过。正常路径已经成功，不再运行不必要的owner-only侵入probe。源码/build/GUI串行由implement→check交接，任务保持in_progress，不commit/push。


## 扩展测试构建通过，运行前再次安全停止（2026-10-22）

仅正常live夹具已扩展20轮同pair启停、Busy、新session/seq、停止后迟到、5秒静止分布、两次实际Freeze与中途取消；未运行owner-only probe及其private消息/worker hook已从源码移除。完整ARM64 solution `live-lifecycle-first` exit0/23.820s。随后独立子进程PID53668在安全界面最前门即退出4/1.204s，stdout0、stderr `blocked=security-interface-before-initialization`；没有初始化Display/创建夹具窗/Start Capture。故新20轮和Freeze实际验收仍未执行，不把编译写成通过，也不推翻此前解锁后的12帧正确结果。

当前已停止GUI并再次请求人工解锁，等待回复期间仅离线检查/后续产品夹具暂存，不重试安全层、不阻止锁屏、不操作安全窗口。


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


## 样式回执修复与本轮最终离线检查（2026-10-22）

真实OS墨迹夹具首次PID29296在A Down前的样式精确ACK等待失败，不能称RTS不消费输入。最小修复由StateMonitoring按当前过期ACK事实沿原循环重新Reconcile；保持普通setter、原回执协议和测试门槛。D6二进制ARM64完整构建与短CLI通过；完整无窗口156.914秒通过，x64完整solution27.330秒与五项短CLI通过。i18n en-US/zh-TW各426/426，两架构PE与文本格式检查通过，详保存子任务research/live-final-offline-verification.md。

用户最新明确解锁后直接运行同一D6墨迹入口，PID59040仍在创建窗口前退出4/174ms，唯一诊断security-before-initialization；无A/B输入、图像或保存文件，不计样式运行收敛或墨迹验收结果。当前仅核对安全窗口元数据，不再次GUI重试。长滚动提案只保存在ignored staging，未应用/构建/运行；真实Edge适配只完成设计和可用程序定向调查，没有启动。


## 用户确认正常桌面后的新尝试（2026-10-22）

D6的PID56220、fresh Round product-ink-desktop-confirmed-220145046：exit1/783ms，越过最前安全检查，创建source及原Mag pair；随后window-service阶段fixture-owner-failure-or-cancel，security0。只有CPU source_paint成功，没有SourceGT/StartProduct/A输入/Tracking/PNG或UInk；不能据此判断Mag Read、样式ACK或RTS失败。正常销毁自建hidden pair，无AV/超时，state/mag线程未启动。当前implement只读定位具体取消项，必要时只补有界诊断、保留原保护后构建/check；不放宽guard求过。


## 真实墨迹局部链首次到达（2026-10-22）

诊断版FC28/EXE23090258，PID43872、root product-live-43872-229909937：实际OS A由原RTS消费，样式setter后的mode3/bridge5精确ACK由0恢复1，真实非空辅助窗pair(0,1)。主窗/辅助窗专用PNG一致，forward32核心18772点按(0,-32)平移，比较双向最大0px；实际Source wheel/Tracker/owner一致。不是hardware pen证明。独立checker正在核原始日志/PNG，最终仅有限局部成功，不以总exit1判定这些已到达观察无效。

反向输入前SourceGT78/99且精确ROI失败，原保护立即停止；无reverse/B/完整Exit验收。Capture62/rejected0/API0，失败清理两线程returned1/failed0、held0等资源状态清空。GT变化来源未知，不能归因用户操作、锁屏、Mag或渲染；只补失配点/命中窗口的有界诊断后重新构建/check，不放宽验收或调整GPU。


## 2026-10-23 夹具预期校正续测

PID62840已到达真实A/正反两次wheel/正常Off/view(0,16)/真实B，A向前最大1px、其它交接0px；PID10832同链全部A误差0px。B原分类分别unknown12/4导致原规则失败，无成功ReadInkExit，旧run不追溯改判。四个已记录坏像素均属于严格色差4与背景容差3间的漏分类区，已知灰底+固定蓝笔低覆盖模型每通道残差<1可解释；新规则仅补这个已知边缘，不改强core/GT/位置/count容差。实际两笔UInk独立解码还确认A/B首点与RTS原始量化一致，整数OS光标+.001的预期会误拒约.016/.035px；仅专用鼠标的HIMETRIC量化oracle改用已有稳定有效DPI的推导界，不扩称任意RTS或硬件。源D83E9F65预审通过、完整ARM64构建与CLI进行中，不能视为新二进制或实屏通过。


## 2026-10-23 原生短墨迹完整门槛实际通过

PID22200 / 32.320s / exit0，round product-ink-oracle-fixed-1023-5561046，root product-live-22200-5561515，启动二进制03DEA84F。实际OS/RTS A、非空辅助呈现ACK、forward32/reverse16、正常Off与主窗恢复、真实B、原Exit index/UInk ReadComplete+ImportSuccess全部完成。独立checker核8张专用PNG：5A+1B+2底图，所有unknown0，B13个小覆盖边缘由独立alpha区间模型确认；四次A比较核心集合严格平移/max0。实际v10文件12389B、一Desktop页、两Pen227/225点、viewport(0,16,1)、GUID与唯一Exit索引一致。两线程returned1/failed0，产品/服务/Mag/捕获/自身输入持有均清空。

Native DPI192/192已实录；B实际首点1361.965332、整数期望1362，受限OS鼠标一HIMETRIC+float界0.075918408px内，文件viewport仍用.001。本例只证明该专用鼠标和1:1主屏映射，不是硬件/任意RTS验证；旧失败run不追溯改判。详保存子任务research/live-product-ink-complete-check.md。长滚动stage须重放到最新D83E，不覆盖诊断/oracle；真实初始特征退休用既有verbose事实补充，不以relay计数代替。当前只开始实施新长测试，尚无长程PASS。


## 首次实屏long失败及定位方向（2026-10-23）

新24D8构建的旧short PID26356完整复验通过。随后唯一long PID16860/50.590s/exit1：纵向与水平32/-16先通过，五次A核心双向误差0。首long128px真实wheel/CPU paint到(272,656)后，199–201连续InconsistentTranslation/inliers2，正确Paused，owner保留最后可信(16,16)；后置GT未到达，不能由Capture201质量/API0推断那几帧Mag像素正确。198可信帧初点仍286，拒绝帧默认initial0不是退休证据。十屏/返程/B均未完成，正常清理及失败Exit一Pen保存最后可信位置通过独立观察。详保存子任务research/live-long-runtime-check.md。

不减总19200px距离、不放宽可信度或自动恢复求过。当前先用受控合成TexturePixel及现有一般纹理分别诊断16/32/64/128/192/256px，生产Tracker未改；合成重放不等于原失败Mag帧。后续参数校准或初值方案需基于可重复结果并重新跑离线矩阵/实屏。


## LK大单步最小校准自测（2026-10-23）

双向LK仅maxLevel从3改5（从0计最高层，实际层数仍受图像尺寸限制），原可信/FB/空间覆盖/跳变/暂停门槛不变。完整ARM64 solution exit0/18.935s；tracker-only PID4204 exit0/334.743s，原14组6918合法帧与新增native ROI2880×1808的704合法帧全部接受。新增静止300漂移0；128px单步往返284帧，总路程36352px、峰值18176px，初始特征第14帧归0，之后270可信帧，最大/最终误差约.00118原图px。该峰值是离线ROI十屏，不是实际主屏19200px门槛。

新增真正允许maxLevel5的大图负例：多数固定+中部4px次流、无重叠新页、缩放1.02、旋转1度，分别AmbiguousMotion/ForwardBackward/NonTranslation/NonTranslation，三拒绝后粘滞Paused且保留参考。原测试正文去掉新增块后字节一致；旧L3运行新增128px正例真实断言失败，非编译或工具故障。更小分辨率可能裁剪实际金字塔层数，不承诺任意分辨率128px必通过。

当前为实施自测事实，独立审查及新参数真实短/原128px、19200px长程仍待进行。没有用合成结果反向改判PID16860失败，没有降低实际步长、总距离、像素或可信门槛。


## 新参数实际短流程通过、长流程夹具成本（2026-10-23）

新8DAF构建short PID32848 exit0/37.844s，独立核8PNG/两Pen229与228点/原Exit v10视口(0,16,1)/真实RTS首点及清理通过。随后同128/19200long PID30628已完成16/151去程步，到实际相对(16,1968)，所有已完成GT保持99/99+精确ROI，未因新参数发生三拒绝暂停；但每步约5.8秒，预计超过原480秒监督。主会话只向本次PID、精确专用纹理类的HWND发WM_CLOSE安全取消，exit1/143.324s，正常退出两线程、无持有输入/产品/捕获资源。它是人工安全取消的部分观察，不是十屏通过，也不是新LK再次失败。

ScreenSourceGt每步两次、每次99个屏幕GetPixel可能造成同步读取成本；当前只是源码与步耗时支持的推断，没有旧版分阶段性能计时。准备仅修改夹具为一次内存屏幕快照，在相同快照上保留原99点和全256×192像素ROI检查；全屏缓冲不落盘，仅全像素确认的原ROI可保存。安全/源绘制身份/GT阈值/128步长/19200距离/480秒监督不改，随后审查、构建、短程和完整长程重测。


## 完整原生十屏往返实测通过（2026-10-23）

D58E/round product-ink-gt-batch-long-1023-current/PID6740/root product-live-6740-8519687：exit0/160.123s，原最大128px步长、151去程19200px与完全反向151返程，共302长程步；另有先行纵横四步，共306真实tagged wheel逐条重建Source一致。峰值相对(16,19216)、返(16,16)，626次GT均99/99+原ROI逐像素正确。11PNG独立公式及alpha区间核准，A初始交接core max0，后续纵横/长程返回/Off core最大1px、中位0，满足原2px门；不能写全程max0。

独立核同session的1192条连续可信追踪记录，frame80初始特征归0后仍有1112可信帧并持续补点；不是拿rejected默认0或relay序号充当接力。实际Exit UInk12519B、v10/一Desktop页/两Pen228与229点，view(15.998664856,15.999662399,1)，workspace/page/file GUID与唯一索引及原ReadComplete/ImportSuccess通过；受限native鼠标首点量化仍在计算界内。最终视口距GT(16,16)约.00138原图px，不等于视觉像素误差。线程/产品/捕获/自身输入持有正常清理。

此结果限定专用真实OS鼠标和纹理，不证明物理笔/触控、浏览器/Word/PDF、完整现场编辑历史、真实DPI/设备变更或任意外部Desktop冷载。原PID16860失败和30628取消不追溯改判。详保存子任务research/live-long-complete-check.md；外部兼容继续，父/子任务仍in_progress，不commit/push/finish/archive。


## 外部入口编译与首次Edge脚本失败（2026-10-23）

外部empty仅新增测试入口与私有runner，不改生产Mag/CV。V2 x64首编因两处新u8path在C++20按C4996失败，属新增源码问题；已改标准u8string路径构造且消除已验证ASCII叶名比较的窄化，未加silence宏或改项目。V3 ARM64完整exit0/26.523s、3条既有hashlib警告；x64源码通过后出现生成main PDB的LNK1285，本机204976128B坏文件经普通ignored路径及祖先核实后留档，原配置重链exit0/10.871s，未改源码绕环境。两架构各35坏argv双0B/exit2，协议/状态均0。

V3 ARM64 3CD17F64的新原生short PID24580/6.428s/exit0，独立核8图/两Pen226与220/view0,16及窗口交接/cleanup通过，原native链未因外部case回归。

首external root external-be833ba7cc86413e9788f88165ea353c，专用Edge18812/Edg155.0.4283.62/私有port50068已启动；PS ConnectAsync Task的VoidTaskResult漏入pipeline，导致websocket变array，SendAsync与finally Dispose失败。未启动Inkeys、未进入Capture/Tracker，不是Mag失败；finally早期退出未产receipt。root随后核PID、精确FILETIME134371955877222764/image/私有端点与browserId，只对该实例发CDP Browser.close并确认根进程退出，root-cleanup.json保存。正在仅修私有脚本返回抑制、Dispose收敛与早期自建窗口清理，审查后再新nonce单次测试，原失败不追溯改判。


## 外部Edge最小入口与前台交接（2026-10-23）

新ARM64 3CD17F64、x64 40263953完整solution均已成功；x64 u8path真实源码错误已修复，随后损坏生成PDB独立保留并原配置重链。两架构各35坏argv全部exit2，协议/状态纯CLI通过；当前ARM64原生short PID24580/6.428s及独立8PNG/真实两Pen文件再通过。完整Headless CC3F未改变，无须由测试runner脚本改动重复运行。

Edge首三例均在启动Inkeys前失败：PS异步返回污染、以及附加窗口覆盖精确标记；原始证据/自身关闭收据保留。第四root external-239978d8a132425f9113b9683f62d82b，Edge34680：单次Escape Down/Up后246ms全32hit回主root，32颜色正确；Inkeys37692真实Tracking及ACK1到达。4.489s在wheel前Marker的foreground/hit门失败（raw尚不能区分具体哪项），无os_input、无wheel-result；撤销后两process正常退出，无持有键/清理错误。不是Mag运动成功/失败证据，不是外部墨迹验收。


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
