# 保存与完整回归实施顺序

## 前置和本轮权限

依赖产品阶段顺序check通过后validate/start。本轮批准全部后续代码，仅允许无窗口或全程不可见HWND；禁止Computer Use、可见内容、前台/置顶。必要实屏验收成为下一步时停止，父任务保留未完成。无commit/push授权，最小diff与既有编码/EOL。

## 顺序和所有权

- [x] 读父/本任务prd/design/implement、全部implement.jsonl、产品最终core-implementation/check报告与viewport-only-save-current研究。不得把旧owner诊断、单协议或空持久化root的Host测试当作正式文件验收。
- [x] 保存实现者统一拥有新增真实全隐藏文件/编辑test与必要最小hidden观察receipt、严格CLI/Main/工程登记；产品其它源码已冻结，不回退他人变更。发现生产缺口先定位原路径并最小修复，不另建保存链或冷载UI，不改UInk版本/Shader/墨迹算法。
- [x] 独立私有create-new root、逐组件拒reparse，所有HWND一直visible=0、非激活。autoSaveRoot显式私有、autoSaveEnabled=true；cleanup在窗口/回调context销毁前StopProduct drain。严格早退及harness-off/bad argv验证后才运行专测，不跑旧会ShowWindow的hidden/eraser入口。
- [x] 真实输入生成A并沿Clear保存，严格读实际Committed文件取得A的点/style/页/工作区/视口基准；可靠确认Controller已消费完成并释放CPU fallback，再Undo走真正worker磁盘读取安装A。优先复用现有确定性接缝，必要有限hidden receipt，不以sleep或worker计数冒充已安装。
- [x] 沿生产Begin/latest absolute/End真实owner平移；可信A点/style/页不变，非零基准/反向/水平/钳制/Seq/退休及跨帧顺序优先复用产品已有专测，不重复复制协议实现。允许明确ACK seam，不能宣称真实系统穿透。
- [x] Viewport-only独立场景：A恢复后不新Down/B，仅Follow，Publish最后目标后不预等Applied立即Stop；真实ExitAutoSave/worker drain后按严格索引读Exit .uink，Read Complete/Import Success，核对v10、原workspace/PageGUID、逐点/style和最终base-total/scale1。
- [x] A->Follow->End->真实B->擦除/Undo/Redo：核对B锁存新视口、A原点不改、Follow不建历史/不清既有Redo、新B按原语义清Redo。再沿真实Clear或Exit文件读回各最终内容。共享Read/Import验证文件viewport往返；现有Clear Undo磁盘恢复后再次Follow验证Host，任意跨进程冷载仍未提供，不扩张该能力。
- [x] 单一完整InkeysRepo.sln Debug ARM64原生MSBuild；同PowerShell规范Path/禁node reuse，>=5分钟，正常宿主，不改依赖/SDK绕环境。新增定向及必要相关既有纯CLI/严格隐藏回归，保存日志exit/耗时/边界。
- [x] 顺序trellis-check覆盖本阶段及产品接口，安全机械修复后复验。主会话统筹最终ARM64是否需重编、x64完整尝试、PE、i18n/XML/差异/BOM/EOL和全范围最终check，禁止并发MSBuild。
- [x] 更新实际参数/实测/console用法/未测及现场步骤，所有软件证据齐备后停在真实Mag连续路径与系统窗口/可见内容门槛，不标全功能通过。

## 已有基线与验证记录

产品core-implementation.md记录生产协议/模式快捷键/配置/全隐藏真实owner定向exit0，full --no-window交产品check，不在保存阶段无故重复。既有inkStrokeModelerTest.sln ARM64与四组storage CLI均已主会话通过，见storage-baseline-current.md；当前相关legacy源码相对HEAD无变。PPT legacy fixture私有TEMP必须<=40字符以避开原深层路径预算，禁止为此改生产路径策略。四场景真实Shape/Pen保存用例已通过实施验证，详research/implementation-current.md；独立check已通过，零位移A/codec比较已强化为逐值精确相等。

本轮权限完整记录见../10-21-scroll-follow-v1/research/implementation-authorization.md；先前仅Mag准备和旧可见临时授权均不沿用。最终验收矩阵必须区分真实owner、fake/合成图、ACK seam、文件往返与实屏。

## 最终软件验收与停止点

独立check完整ARM64 exit0/8.143s，新保存四场景exit0/4.194s，8真UInk，普通Pen23/23真实点逐值不变；相关短回归及7坏argv通过。主会话x64完整solution同配置重建exit0/6.791s，5纯CLI通过，两架构PE已查，详细见父research/final-build-and-pe.md。当前仅剩必要正式Mag/系统窗口/实际内容与硬件现场验收，按本轮限制停止；父/本任务不标全功能完成、不commit/push。


## 实屏测试继续（2026-10-22）

授权与边界见父research/live-test-authorization.md。顺序：原Mag正式worker专用图案像素/持续更新/排除及启停Freeze门槛 -> 独立check -> 真实Business/WindowService ACK/Draw3专用内容跟随整链 -> 应用及具备条件的现场项。保持原Mag线程设计及私有数据，禁止Computer Use；新增harness不改变生产渲染/保存语义。每一步以实际日志/图片和原始退出码记录；必要harness修改后完整ARM64 solution与最小定向回归，独立check。


## 最小外部应用继续项（2026-10-23）

- [x] 原native long PID6740独立核通过：11PNG/302步/19200px峰值/初点退休后1112帧/实际两Pen文件，详live-long-complete-check.md；旧失败/取消保留。
- [x] 已按design增加严格external-empty入口，复用真实产品链、不接管外部HWND生命周期；ARM64/x64各35坏argv、协议/状态与新ARM64原native short通过，详live-external-entry-check.md。
- [ ] 私有runner创建新Edge/profile/loopback CDP/fixture，窗口与target所有权、DPR映射、真实屏幕标记、实际OS wheel和各阶段有限身份交叉检查；无完整身份就失败，不附着用户browser或修改策略。
- [ ] 先check精确diff/strict argv/权限和资源，再原完整ARM64 solution及最小CLI，最后独占GUI跑一次external真实小步；无条件记录成功或准确失败边界。不声称该空页case证明墨迹/存储或Word/PDF。
- [x] 当前源冻结后ARM64/x64完整构建、最小CLI、最终PE/i18n/源码格式通过（x64损坏生成PDB保留后原配置重链）。硬件/旧系统未测，任何新源码变更仍须复核相关检查；不commit/push/finish/archive。

外部GUI仍未通过：第五次专用Edge在wheel入口前出现同进程另一前台root；Tracker随后真实三拒绝Paused，保留lasttrusted。当前仅诊断晚出浏览器UI；不把空页初始化、构建或native通过替代外部滚动/墨迹验收。


## 实际Tracker输入的有限只读录制合同（2026-10-23）

Guest相同200px场景出现一败一成后再次失败。最新46cc7129b8cd4621b8a47d1d8842a840/29BB15DD/9110ms，frame9–11空间比例.611565<.65，134/178点内点、存活.611684、3次拒绝，明确触发Coverage之前的一致性门；不是由点数比例75.3%判通过，也未到累计锚点检查。750dddd0的140+60采样一次成功、视口200.000061保留为限定PASS，不把只加日志说成修复。

下一步仅可选、严格external早退harness的只读输入录制，不改正式Mag、Tracker、Target或ROI/门槛。显式runner开关+有限环境标记启用，且须当前自建Guest窗口/身份/安全检查。harness宏内在原Frame使用同一MotionFrame并完成真实Tracker后，observer只接收const input/result/session，复制本次同步借用，不产生或修改图像输入/跟随目标。观察器非原TestPorts，正式业务仍走真实Capture/Host/ACK。

记录从Initialize开始连续全部输入及原result（含尚未Tracking的NotInitialized），原seq/time/尺寸/ROI/会话/阶段及已有统计；不得只录最后可信后重新Initialize冒充精确会话。允许复用同一OpenCV版本COLOR_BGRA2GRAY得完整分辨率Gray8，离线原Tracker Gray8入口仍执行同一resize；不能预先半分辨率再令Tracker二次缩小。最多64帧且像素总内存≤384MiB，溢出/漏帧/异常标明incomplete并使显式诊断失败；不建立生产图像FIFO。复制/转换耗时另计，不把该次性能当正常捕获性能。

同步回调只做有界内存复制，不压缩或写文件；原Stop/drain和所有回调线程退出后才保存自己root内CreateNew的PGM和有限JSON，不保存源借用指针。观察器拥有shared lifetime并原子挂载/卸载，不能迟到访问Fixture或影响新场景；宏关闭时无新引用/导入/链接。默认入口未启用，不新增设置/产品API。保存前仍核本次专用身份/几何、已知区域覆盖和安全；实际外部ROI内容不能由固定标记单独证明逐像素正确，原图用于后续独立比对/回放。


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
