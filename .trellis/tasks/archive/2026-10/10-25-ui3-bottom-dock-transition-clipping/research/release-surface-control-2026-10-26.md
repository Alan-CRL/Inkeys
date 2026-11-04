# 松手原点交接：单变量对照

当前用户授权继续吸附闪动后续。拖动段已人工确认通过，静止按住后松手仍闪；保持任务进行中，不提交、不归档。继续使用 trellis-before-dev 与 trellis-check；本轮只隔离松手几何阶跃，不能把软件坐标检查当作视觉修复。

## 变更边界

- 缺口：Up 恢复 viewport 屏幕包含与 backing 重定位，已存在的成功 CSV 显示源点和 HWND 原点同时跳变，旧工具缺少 Up 后采样。
- 归属：几何候选和成功状态仍由 RenderLoop owner 独占，读取成功窗口基准在既有 directWindowDragMutex 内，不改变输入 tuple、弹簧、成功命中事务。
- Bar.RenderLoop.cpp：仅 Harness 且显式 INKEYS_UI3_DIAGNOSTIC_HOLD_RELEASE=1 时启用。Up 捕获成功 HWND 绝对矩形 W、成功源点 S、capacity size；之后 V=W−monitorOrigin−frameT，C=V−S，恢复 UI 而不收拢 HWND。不会在动画末尾再次搬移。
- Bar.MultiTarget.Test.cpp：沿真实 Seek/RenderLoop 检查 Up 和恢复阶段 HWND/source/size/target 恒定，覆盖当前已有缩放、负原点、工作区、再次 Down 和失败序列。
- research/live-dock-probe.cpp：正式36秒保持，附加松手前静止与松手后原生窗口采样；原有命中/锁屏/接管/异常释放保护保留。
- 新 Down、环境身份变化、工作区转换或包络不足退出实验保留状态，重试普通路径，不能持已失效基准无限循环。产品默认路径保持原行为。
- 本轮不设计最终收拢、不改PPT返回动画、不增加共享原子/后端/线程/配置项，不扩展无关任务。

## 验证计划

复用完整 ARM64 Debug Solution 和初始化前的 bottom-dock/workspace-centering；候选开关关闭/开启对照软件数据。独立 helper 先编译并仅输出路线计划。之后核对私有窗口版本与几何，用户准备好才执行36秒V加松手观察，不录屏/抓屏/Computer Use。视觉结论留待用户反馈。

## 当前实现与软件结果

- 已完成上述诊断候选，未启用时产品松手逻辑仍走原路径。W/source/capacity在既有几何锁内只捕获同一成功手势一次，失败不推进它；失效后不能同session重复捕获。scene/minimize、新Down、环境identity不匹配与包络不足均退出实验。
- ARM64原生MSBuild完整Solution最终exit0；既有hashlib++第三方C4267保留。首轮新增条件把锁保护的bool误作atomic调用，编译捕获后改为锁内直接读bool，没有修改共享字段类型。
- 默认关闭候选的bottom-dock入口exit0；候选开启的bottom-dock入口exit0；候选开启的workspace-centering入口exit0，日志无error。初始化前离屏，不创建GUI。
- 19个真实Seek/RenderLoop释放对，Up前后destination/source/windowSize/targetSize差均为0；默认对照right-100仍为dstY/sourceY −43px，whiteboard −149px。默认路径与候选的差异被同一版本同一fixture独立CSV检查确认。
- 19组各125帧、合计2375个恢复采样点，成功提交tuple恒定，容量不增长，UI底端正常恢复；动画关闭的例子立即稳定，只有一张必要恢复提交。新增测试一度错误要求动画关闭也至少两次提交且误将bool Fail返回为int成功码，已修正预期及失败退出，最终重跑无error。旧日志单独保留，不能将旧exit0记作通过。
- 编码为UTF-8无BOM/CRLF，git diff --check通过。独立agent核对冻结公式、失败提交与失效门禁，没有遗留阻断项。
- 这些自动tail最终为Floating释放；不据此泛化Docked/Centered的真实ULW/DWM显示。此轮不重复无相关改动的完整headless/input-worker，保留此前已通过的记录，不将它们标作本轮运行。
- 证据集中于TestResults/ui3-release-handoff-2026-10-26/：validation-summary.json、default/candidate日志与samples.csv、各case-release-tail.csv、helper编译/plan-v记录。没有抓屏、录屏或Computer Use。

## 实机准备（尚未重放）

已启动本任务私有窗口PID37288，环境变量INKEYS_UI3_DIAGNOSTIC_HOLD_RELEASE=1，启动外框[876,1676,2004,1874]、dpi192、monitor[0,0,2880,1920]、work_area[0,48,2880,1856]，Logo命中975/1775。

正式36秒路线仍12圈，新增release-at-vertex在正式路线结束后另用750ms移至975/1815，再静止按住1200ms、松手观察2000ms；这些不计入36秒。工具仅在完成尾段后统一写CSV。已向用户询问准备状态，收到确认后才注入鼠标。

候选EXE SHA256：B90D3EA366D5984858A28F2B43D17EC38D54A8E6AA38981668F25864EB4C8B0A；helper：CFF705FC82C3B7D64634210A8062F1C9A5536CA6578C93D489A14185A06113C9。

## 首次静止松手实机结果

用户确认准备后执行本轮原生V＋release-at-vertex。12圈正式路线36001.7ms，19110个采样、8041次移动，完整结束；无录屏/抓屏。到vertex后静止1200ms再松手，Up后2000.571ms共991个样本外框均为[636,1546,2244,2050]，与松手前末500ms相同，没有native origin/size变化。正式held尺寸仍1608×504。静止段开头28.199ms内存在最后一段合法移动的5px呈现收敛（仍按住）；已单列，不能把它算作Up阶跃或要求整段prehold无变化。

用户反馈：**“松手未见闪动”**；同时指出拖动段已解决，不需要再长V字，后续应多试“到目标后按住再松手”和“到目标立即松手”。这次仅确认一次静止Up对照通过；仍是默认关闭候选，不把单次结果泛化或认定Windows根因。

当前按用户新要求改为短多次释放重放。仍保持同一候选EXE，不再长V；工具准备完成后先通知用户观察。还需核对重复Down是否使保留包络累计增大，最终收拢/复用策略未在这次对照中实现。任务继续，不提交、不归档。

首次测试窗口PID37288现已正常退出exit0，随后bar_count=0；本agent没有发送关闭消息。后续使用同一EXE的全新私有fixture，不能在已退出PID上重放。

## 六次短释放对照准备

按用户要求不再重复36秒V。新release-cycles工具：6次分别为左立即、右静止900ms、左静止900ms、右立即、左立即、右静止900ms；每次Down于975/1775，300ms移至815/1655或1135/1655，450ms移至975/1815。确认原生光标精确到达终点的同一循环立即Up，或等900ms再Up；均追加1200ms采样，计划14.4s。它不等待render确认，CSV独立记录trial、endpoint_ack_to_up_us及phase40..44。每次Down前重新验证目标PID和grip命中，不命中或锁屏/接管就释放并停止。

helper ARM64 /W4编译exit0无warning，plan-release、V兼容计划、非法参数、UTF8无BOM/CRLF与diff检查通过。新helper SHA256 7869C0345787AFEBE59719B5F790FAB146272C94F2A7887AD6A4887E69151887；首次V的旧helper已另存live-dock-probe-v-release-tested.exe，hash仍为CFF705...。产品EXE仍B90D3E...，不夹入另一产品变量。

已确认诊断版重复Down存在包络递增：先Union(current,predicted,previousViewport)，再对整体padding，故旧透明余量也重复加边；同DPI稳定下每个新session至少宽+324、高+180像素。此前Up正常收拢掩盖了这一后果。六次仅用于单变量视觉对照并记录实际增长，不将它直接产品化。最终应在允许重准备的下一Down以当前成功可见内容与该手势预测范围算fresh、有界包络，避免旧透明viewport成为新预留基础；只调整padding顺序仍保留无限历史范围，不能作为最终有界方案。

已启动新的私有fixture PID16692/session97819，同一候选开关，启动rect[876,1676,2004,1874]，dpi192，Logo命中975/1775。已向用户发出短测试准备询问，收到确认前不发输入。首次窗口已退出，不复用旧PID。

## 六次短释放实机结果及正式收口边界

六次完整完成，14558.255ms、6903样本、1722次移动。用户确认 **“6次松手均未见闪动”**，覆盖立即Up三次和静止900ms后Up三次、左右进入。立即Up原生终点确认到Up返回分别约0.967/1.142/1.139ms，未等待render。立即Up第一例在Up后数毫秒仍有最后正常输入7px呈现收敛，随后固定；这不是大幅归位跳变，不能把鼠标Up时刻等同render已消费最后Move。其余例postUp外框稳定。完整native表保存在release-cycles-live-01-summary.json；只记录HWND，不把它当合成像素证明。

重复Down包络实测依次1608×504、1932×684、2256×864、2580×1044、2904×1226、3228×1406，验证了旧透明边距递归膨胀。当前诊断视觉方向通过，但不能带此性能问题发布。PID16692现已正常退出exit0，本agent未发送关闭消息。

正式实现最小边界：移除诊断开关使Up保留W/source/size成为默认；下一Down用当前完整内容、当次预测和最后成功的真实content边界重准备，不再将旧透明viewport作为padding输入。新增成功content记录与committedAnchor同一成功事务保存，失败不推进；重基准沿该anchor搬运，而不是引用不稳定候选。其余UI/root/弹簧/输入/dirty事务不改；显示、scene/minimize、包络不适用仍退出保留状态。测试默认检查Up和恢复期，并新增同一owner连续Down/Up以及无末点Render的立即Up、有界重复包络断言。当前不提交、不结束任务，改后先软件回归再相同短测试，不再长V。

## 正式回归首次失败：末笔输入仍待呈现

正式构建exit0，新增bottom-dock回归exit1后停止后续测试（未记作通过）。首个right-100在第5次立即释放Free：最后成功Outside pointer535/940、W[363,802,1397,1060]，终点680/1012未render便Up，尚有145/72px待呈现。Up冻结旧W后约144ms恢复内容超出fresh包络，随后普通重准备改变S/size，被严格tail断言捕获。静止同目标W[508,874,1542,1132]可以容纳。原因是恢复目标包含尚未呈现的大位移，不是随机ULW失配；不能用任意扩大margin或放宽断言解决。

新增最小交接：Up尚有pendingT≠presentedT且同一成功held session/环境时，先在原固定held表面呈现末笔正常输入，保持S/size/target，只允许正常delta移动W。该帧不exchange、不吸收root、不捕获releaseW，不处理workspace/minimize；稳定tuple的session/serial仍匹配才用帧局部held标志，真实指针active保持false。显式visual demand保证提交，四API成功后才自然发布成功held快照；下一帧才吸收并冻结最终W进入Up恢复。失败重试仍保留pending和成功快照，不提前伪造成功。

实现仅Up交接时额外一次既有几何锁采样（要求phaseIdle及偶数serial），随后稳定tuple再次校验，最终ULW继续沿原serial/deferred屏障；无新共享字段/线程/等待。新Down改变session/serial时旧flush Retry，不套到新手势。

测试独立验证最终heldW=旧成功W+pendingT−presentedT，S/size/target不变，再验证真实释放与125帧tail不变。增长比较先做3次真实手势warmup，再按相同3目标重复两轮，避免legacy极端grab留下的首个root1px差被误认作递增；不增加误差容限。首次失败日志和RenderLoop快照保存在TestResults/ui3-release-formal-2026-10-26/first-*。

最后补交的并发门禁已收口：Seek的新Down先CAS到Dragging、再过一小段计算才发布session/serial，不能只校验tuple。虚拟held前检查phaseIdle；最终几何锁内仅flush用RAII CAS Idle→Absorbing，保护GetDC/ULW/ReleaseDC/EndDraw与成功snapshot发布，所有失败/早退自动还Idle。CAS失败不覆盖别人的Dragging，serial不相等严格延期。保护范围不含D2D绘制，Seek继续异步coawait1ms让出worker而不阻塞窗口线程。额外Up锁等待计入现有PresentLockWait。零translation但旧成功P非零时，也用releasePending保证先等原phase交接，再判断flush，避免绕过吸收。

## 正式版本最终软件验证（尚待短实机复核）

- 正式版本不再读取诊断开关，释放保留W/S/size成为默认；新Down的成功真实content seed消除递归透明边距。保留直到下一次完整成功Down，失败准备不提前退休旧基准。
- ARM64原生完整Solution Debug|ARM64 exit0；既有hashlib++/Setting数值转换warning未改，不宣称零warning。
- bottom-dock exit0：19组合，各3次真实warmup＋6次测量，共171次Up；57次末笔正常位移补交，均独立满足W_final=W_old+pendingT−presentedT且S/size/target不变。随后真实释放和每次125帧恢复，共21375个重复tail采样点保持该tuple，9次使用同owner不重新Initialize。稳定同起点的两轮尺寸/容量严格不增长；另原始FinalRelease各125帧仍保留（2375点）。
- workspace-centering exit0、input-worker exit0、完整InkeysHeadlessTests --no-window exit0且最终PASS animation correctness。后者在workspace TEMP/TMP运行。没有启动Office/GUI来替代这些软件检查。
- 独立Python对全部CSV再次验证末笔delta、source/size/capacity、每次Up/125tail、三目标重复不增长；编码/BOM与HEAD一致（Main.cppm保留原BOM，其余源码无BOM，均CRLF），git diff --check通过。首次失败保留在first-*，不混为本次PASS。
- 最终所有源码/EXE/helper hash及逐case数据在TestResults/ui3-release-formal-2026-10-26/validation-summary.json。产品EXE SHA256 2A02462EE25A6167FC791576F0D5F3691D49631BCF10FEFE584A2830F18501E6，helper仍7869C034...。本次没有再扩大margin、改弹簧参数、UI根位置或调度后端；仅补末笔呈现顺序和释放几何保留。

正式实机窗口已启动PID23396/session75291，无诊断环境变量，启动rect[876,1676,2004,1874]、dpi192、相同monitor/work-area。版本hash核对一致，但Win32命中975/1775显示Windows Default Lock Screen，故未发送任何本轮鼠标输入。已请求用户解锁并准备好后执行同一14.4秒6次短释放，未复跑长V。当前视觉通过结论只属于此前诊断版7次Up，不能提前归为正式版验收。任务保持in_progress，不commit/push/archive。

## 2026-10-27 正式短释放实机通过

解锁后旧PID23396已退出，未向其他窗口发输入。新私有fixture live-bin-02/PID30512版本hash仍2A02462E...，Logo与外框复核正确。六次重放完整完成14521.388ms、7087样本、1750次移动，无录屏/抓屏。立即Up三次确认到Up约0.961/1.068/1.264ms，静止三次约901–903ms。用户反馈：**“6 次松手均未见闪动”**。

六次准备后的held及postUp尺寸均1608×504，无诊断版累计扩窗。Up初段原生采样仍有1–6px位移，在10.660–17.367ms内收敛，之后每次超过1秒外框稳定；如实保留，不能宣称每个Up样本绝对不动，亦不凭HWND推断系统合成根因。视觉结论来自用户观察。

数据见TestResults/ui3-release-formal-2026-10-26/live-release-cycles-01-summary.json及CSV，manifest/validation-summary已同步。仅向PID30512的DrawpadWindow发WM_CLOSE，进程退出6、后续bar_count=0。此前软件验证仍有效，本轮未改产品代码；任务保持in_progress，未commit/push/archive。

## 2026-10-27 后续纠正：必须恢复完成后收窗

上述6次无闪候选被用户指出存在不可接受回归：释放/吸附完成后仍保留大HWND蓝框，影响下层绘制负担。因此“6次无闪＋重复不扩窗”不等于满足原设计，旧视觉PASS不能作为本阶段最终验收。

直接缺口：releasedWindowSurface没有正常完成的退场条件，fixedReleaseGeometry使最后idle帧也绕过既有ViewportController收缩。本轮仅在ShouldSettleBarWindowViewport(idlePending, dragActive)成立时Reset释放表面，交回原viewport/source/capacity与四API成功事务。Up恢复期仍暂保留，按住期间不变；允许最后收窗时原点/source变化及闪烁可能返回，不引入延时或长期大窗。

原恢复tail断言错误地要求永远固定窗口，已改为每次125帧尾段必须观察到HWND相对held严格缩小；保留末笔/Up及held固定断言。19组合各原始释放＋9次连续释放，共190次收窗通过，171次连续释放仍含57次末笔补交。right-100测量轮的三种目标均从1034×258收至794×105。新证据目录TestResults/ui3-release-shrink-2026-10-27；旧候选源码/CSV仍保留，不覆盖先前试验。最终完整ARM64构建、吸附、居中、输入与完整no-window均exit0，最终PASS animation correctness。独立CSV比较旧候选171次连续释放全部未收窗，而本轮190次原始/连续释放均严格缩小；格式/BOM和diff检查通过。产品EXE SHA256 DADDFFD3137A1AAE78F25B1316138E9BDF689402E69379BE9DE372EAF031BEBA。无新GUI重放，不commit/push/archive，任务继续in_progress。

## 2026-10-27 最终收尾与归档范围

用户已明确要求结束本任务、相关父任务并归档提交。以下为本次最终范围，覆盖此前“保持开启/不提交”的阶段说明；历史验收清单保留其原含义，不将未确认项一律勾为通过。

- [x] 固定手势表面已由36秒V实机确认拖动无位置跳变，顶边与果冻正常；既有提示/脏区外修复结论保持。
- [x] 释放末笔先在成功表面完成，再交接恢复；恢复期暂保留窗口，最后idle必须按内容收窗；新Down用成功真实content避免递归扩窗。
- [x] 最新完整ARM64 Solution、吸附19组合、居中、输入与完整no-window均exit0，最终PASS animation correctness；190次收窗、57次末笔补交、21375连续恢复采样通过。
- [x] 6次短释放无闪是恢复idle收窗前的实机结果；最终收窗版没有新GUI验证。用户明确接受为保留原设计/绘制性能而可能重新出现收窗闪动，不宣称ULW/DWM系统根因已被证明或所有环境完全消闪。
- [x] 本次归档当前吸附子任务与10-25-ui3-bar-centering-dock-geometry父任务；已归档居中子任务维持原状，不触及不相关任务。

源码/EXE核对仍对应TestResults/ui3-release-shrink-2026-10-27/validation-summary.json，EXE SHA256 DADDFFD3137A1AAE78F25B1316138E9BDF689402E69379BE9DE372EAF031BEBA。最终合同已写入rendering-and-ui.md。收尾只修改文档和任务状态，不重新运行无相关改动的构建，不自动推送。

## Bug Analysis: 释放缓解不能改变窗口生命周期

### 1. Root Cause Category
- B/D/E：跨输入/渲染交接合同、完整生命周期测试缺口与“长期保留也可接受”的隐含假设。releasedWindowSurface遗漏正常idle退场，确定导致大HWND常驻；Windows内部合成成因未唯一确认。

### 2. Why Fixes Failed
1. 原点变化整窗提交、上移200px均经用户同轨迹观察无效，不能继续当作已修复。
2. 诊断释放保留消除可见Up闪动，但旧viewport再次padding导致逐次扩窗；成功真实content seed消除增长。
3. 只检查无闪与跨手势不增长仍遗漏最终收窗；旧tail把窗口永远固定当不变量，恰好掩盖设计回归。

### 3. Prevention Mechanisms
| Priority | Mechanism | Specific Action | Status |
| --- | --- | --- | --- |
| P0 | 生命周期合同 | held固定、Up补末笔、恢复暂保留、最后idle收窗 | DONE |
| P0 | 生产回归 | 同owner真实Seek/RenderLoop，19组×10释放必须最终缩小 | DONE |
| P1 | 证据分级 | HWND/源DC/离屏PASS与人工视觉分开；不以录屏缺帧否定肉眼 | DONE |

### 4. Systematic Expansion
- 透明预留、缓存和成功快照都须同时定义获取、失败重试与正常退休；本次仅修本任务窗口，不扩大到其他UI或后端重构。
- 收尾以用户最新设计约束为准，消闪不能覆盖收窗与下层绘制负担。

### 5. Knowledge Capture
- [x] rendering-and-ui.md已同步生命周期、错误矩阵、恢复末尾收窗与必要回归；本仓库没有Trellis源模板目录，不创建复制模板。
- [x] 本记录与任务最终范围随本次工作提交；用户授权按已知限制关闭，不新建问题或远程issue。
