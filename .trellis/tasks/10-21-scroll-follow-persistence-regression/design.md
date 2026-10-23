# 滚动批注保存与完整回归设计

## 当前范围

依赖产品接入代码和顺序check。只补验证工具、必要最小bugfix与报告；沿UInk v10与原Clear/Exit保存，不另建保存链。用户批准全部后续实现，但本轮只允许无窗口或完全不可见HWND，禁止Computer Use、可见内容和前台。无commit/push授权；必要实屏验收时停止，父任务保留未完成。

实际研究：../10-21-scroll-follow-mag-capture/research/product-integration-current.md第8/9节。全隐藏基线：Inkeys.exe --draw3-host-metrics-smoke已exit0，含真实WARP/Host输入与present，所有自建窗visible=0。

## 独立专用早退入口

新增严格harness gated的follow隐藏专测，必须在配置、单实例和普通UI初始化前返回；坏argv返回2，不落普通启动。调用既有WindowService/真实Host/真实contact mailbox，全部HWND保持IsWindowVisible=false，不能调用Reconcile的实际show或SetDrawpadSurfaceVisibility显示分支。不运行名字含hidden却会ShowWindow的旧集成/eraser测试。

输出仅任务私有新建目录，逐组件拒reparse、不覆盖，autoSaveRoot显式私有。Stop guard在callback context/窗口前执行真实Host drain，再WindowService退出。正常/异常都自动关闭私有资源，不触碰用户配置或文档。

可注入生成帧/生产绝对目标验证owner、历史和文件，但不能把注入视为真实Mag截图/屏幕滚动验收。双窗只验证真实输出ready与纯决策/受控ACK，不能为了跑通而显示。

## A -> 跟随 -> B -> 编辑 -> 保存重载

1. 普通Desktop画笔，真实Down/Move/Up生成A；等待stored/recycle和canUndo，记录原始点/style、Workspace/Page GUID和viewport。
2. 生产Begin关闭准入并seal已接受contact；注入后续Down拒绝且真实Up回收。发重复/乱序/跳号、旧session、错scene/page/host、非finite、latest单槽压力，核对合法绝对目标只应用一次。
3. 非零base、水平/垂直/反向、clamp、模拟1/1.25/2DPI但同物理位移测试不重复缩放；若帧映射2则正确一次倍数。clamp明确报告，不能当完整跟踪成功。
4. End可靠携带最后可信量，等实际tile恢复/viewport完整present与特定session/sequence回执，再退出override和释放本会话gate；其它blocker不被清。跟随不增历史、不清已有Redo、不改A点/Page GUID。
5. 同client位置画B，Bstored=client+最终viewport，A点仍完全一致。Undo只去B，再去A；Redo顺序恢复。跟随前的Redo分支单独保留验证；正常新B应沿原语义取消Redo。
6. 在所有前序屏障之后真实StopProduct，沿PrepareExitAutoSave和worker drain保存；读私有索引/.uink，Read=Complete、Import=Success，比较v10、原页/工作区、最终viewport、A/B点/style/顺序。
7. 用真实codec重载到同功能Host，再启动跟随并验证基准是重载viewport。Clear保存另测，保留Clear/Exit触发语义。不得用人工另造三点快照代替A/B证明。

## 生命周期与系统边界

最新目标与End竞态、普通队列满仍可End、End后迟到frame、初始化失败回滚、重复Stop、新Host旧代、工作区/页/文档替换、display/DPI/resize/device retirement；shutdown最后目标先于最终保存，不能等关闭WindowService显示ACK。纯测试覆盖工具/shortcut/Freeze的先End后动作顺序，保留原租约/场景拒绝。实际系统输入路由和窗口视觉另列未测。

## 构建和现有检查

完整InkeysRepo.sln Debug ARM64，vswhere定位ARM64原生MSBuild，同PS规范Path/PATH并禁node reuse，>=5min；正常宿主避免已确认sandbox C1902，不改项目绕环境。ARM64通过再尝试完整x64，依赖/环境/源码错误分别记录，不自动升级vcpkg/toolchain。

运行新增定向和--no-window，以及真正不显示窗口的既有Draw3自检/形状/工具/定格协调/白板PPT合同测试。共享storage现有inkStrokeModelerTestTests的--uink-file-only、--uink-page-file-only、--uink-presentation-only、--desktop-autosave-only、--presentation-autosave-only可按现有测试工程构建/运行；不运行demo/benchmark可见入口。i18n sync由UI阶段执行，最后check；检查三工程XML、PE hard imports、git diff与实际文件BOM/EOL。

## 完成门槛

报告各阶段实际文件/行为、最终Tracker参数及严格每序列误差/耗时、捕获质量/暂停条件、viewport/GPU/历史/保存结果和console方法。软件允许检查完成不等于端到端通过。必要实屏剩余门槛包括正式Mag worker真实图案变化/排除/FPS/资源、实际穿透/辅助交接/墨迹稳定/浏览器WordPDF与主栏视觉。到此停止并给明确人工步骤；旧系统和缺硬件逐项未测，不伪记通过。


## Viewport-only和重载证据边界（实际源码研究补充）

见产品research/viewport-only-save-current.md。现有Desktop Exit不按dirty跳过，当前canvas.viewport三值每次进入新请求；不需要改UInk格式、dirty或请求去重。必须增加真实用例：A经Clear实际Committed后，确定Controller已经接收完成且释放CPU fallback，再Undo走worker磁盘读取安装A；此后不发送新Down/B，仅Follow并在最新目标尚未预等Applied时Stop，严格读最后Exit索引/.uink，单独比较A的style/点、workspace/PageGUID和最终viewport。完整SnapshotReceipt哈希还含fileGuid/viewport，不能用它断言几何不变。

当前Host没有任意外部Desktop快照冷载/替换API，只有现有Clear Undo匹配恢复路径；该路径有意保持当前视口。共享ReadUInkFile->ImportDraw3UInkDocument能验证文件viewport往返，不能写成跨进程Host冷载已测。步骤7应使用现有磁盘恢复后再次Follow验证真实Host，再分别验证codec的保存viewport，明确这两个层级；不为验收增建产品导入功能或绕过恢复资格。若无现成确定性恢复分支证据，可加严格hidden gated的有限receipt，不以sleep或worker提交计数冒充Controller已安装。

既有共享storage四组CLI已由主会话在原代码通过，详storage-baseline-current.md；PPT legacy fixture私有TEMP须<=40字符，否则其深层session路径会超旧路径预算。除有相关生产变更外不用重复基线。后续最终完整ARM64/x64与PE检查由主会话统筹，避免并发MSBuild。


## 普通笔与形状路径必须分别说明

SolidLine在Bridge中是形状工具，真实完成类型为Shape，导出通常只有两个端点；不能把这个用例称作普通笔Drawing/建模点路径通过。保存回归至少包括实际Pen或HardPen，完成Kind Drawing、导出Pen，A的完整点/width/style/GUID在Follow前后及Exit文件保持一致；Follow后普通笔B必须核对client Down与最终viewport对应的真实首点/锚点。普通笔建模受采样时间影响，不强迫A和B整条点数组是完全相同的平移；只分别断言A前后无改写、B锁存当前视口一次。保留Shape严格平移用例可单独证明形状路径。

现有Eraser持久化为蒙版stroke，不必删除A/B原始点。测试应证明真实mask覆盖到目标及Undo/Redo的可见历史语义，不能以工具发布或收到Eraser完成kind本身声称已擦中；最终文件对象数须按既有表示而不是假定删除笔划解释。


## 实屏测试继续（2026-10-22）

授权与边界见父research/live-test-authorization.md。顺序：原Mag正式worker专用图案像素/持续更新/排除及启停Freeze门槛 -> 独立check -> 真实Business/WindowService ACK/Draw3专用内容跟随整链 -> 应用及具备条件的现场项。保持原Mag线程设计及私有数据，禁止Computer Use；新增harness不改变生产渲染/保存语义。每一步以实际日志/图片和原始退出码记录；必要harness修改后完整ARM64 solution与最小定向回归，独立check。


## 实测驱动的最小 LK 层数校准（2026-10-23）

首long128px步骤失败后，用受控TexturePixel纯生成图及实际主屏ROI尺寸独立重放，原3层稳定复现64/128不一致；L4不足以覆盖128四向，隔离副本L5在本机2880×1808处理前ROI上使128/192四向与姿态路径通过。没有原失败Mag帧，不能宣称唯一还原其根因；这证明的是独立算法能力缺口。小图实际金字塔会被OpenCV限制，960×540/1920×1080仍可拒绝128，不承诺任意屏幕/内容的大步范围。

本次选择仅前后双向LK maxLevel3→5与关键中文注释，不改窗口21、FB/生存/一致性/覆盖/跳变/锚点门、暂停/参考提交、捕获、GPU、DPI或输入/保存。不引入phaseCorrelate/新依赖/配置API；若后续实测仍不能满足，不用降低long步长/总距离求通过。新的原生ROI大步、静止300、长relay/初点0与返程及可复用安全负例补充旧14组6918合法帧，旧断言/阈值不修改。合成正例须能证明旧L3确实失败。全部定向回归/完整ARM64/独立check通过后，先真实short，再同样128阶跃与19200px十屏long。小尺寸失败作为能力边界记录，不烘焙成未来算法必须拒绝合法平移的产品需求。


## 可用外部应用的最小验证入口（2026-10-23）

在原生完整长程通过后，按research/live-external-minimal-entry.md实施有限external-empty case。该入口复用现有真实Business/共享Mag/StateMonitoring/WindowService/Draw3 Channel/Off结束屏障；只替换原自建Source的创建、输入和只读GT观察。严格单参CLI，缺私有runner数据直接失败，不回落普通Main。正常native短/long/ink逻辑与全部oracle必须保留。

私有runner独占新Edge app/profile/CDP和真实OS wheel，C++不加入CDP依赖。通过新建、拒reparse的Build/ScrollFollow私有目录，以有界且带run/PID/阶段身份的文件交换browser-ready、tracking-ready、wheel-result、off-ready、final。C++核活进程及精确owned root/content HWND，绝不DestroyWindow外进程窗口；runner只关闭自己的browser。输入前检查安全层、实际前台/命中、无物理按键持有；安全层出现即停止，不自动解锁或保活。

实际滚动差来自同一专用页面只读CDP状态，DPR/物理content尺寸/布局需交叉验证，不能假设滚轮固定像素或重复DPI换算。专用页面小图案与实际屏幕像素独立核对，rAF不冒充DWM更新完成。GT只能断言latest=-delta、viewport=base+delta及稳定序号，绝不写入Tracker/Target/ACK。新case仅验证真实Edge底图带动空画布视口及正常退出，明确不声称浏览器墨迹/存储/普通tab/Word/PDF通过。依旧先审查diff、完整ARM64构建及严格坏argv，再运行拥有私有资源的自动关闭GUI。


## 实际Tracker输入的有限只读录制合同（2026-10-23）

Guest相同200px场景出现一败一成后再次失败。最新46cc7129b8cd4621b8a47d1d8842a840/29BB15DD/9110ms，frame9–11空间比例.611565<.65，134/178点内点、存活.611684、3次拒绝，明确触发Coverage之前的一致性门；不是由点数比例75.3%判通过，也未到累计锚点检查。750dddd0的140+60采样一次成功、视口200.000061保留为限定PASS，不把只加日志说成修复。

下一步仅可选、严格external早退harness的只读输入录制，不改正式Mag、Tracker、Target或ROI/门槛。显式runner开关+有限环境标记启用，且须当前自建Guest窗口/身份/安全检查。harness宏内在原Frame使用同一MotionFrame并完成真实Tracker后，observer只接收const input/result/session，复制本次同步借用，不产生或修改图像输入/跟随目标。观察器非原TestPorts，正式业务仍走真实Capture/Host/ACK。

记录从Initialize开始连续全部输入及原result（含尚未Tracking的NotInitialized），原seq/time/尺寸/ROI/会话/阶段及已有统计；不得只录最后可信后重新Initialize冒充精确会话。允许复用同一OpenCV版本COLOR_BGRA2GRAY得完整分辨率Gray8，离线原Tracker Gray8入口仍执行同一resize；不能预先半分辨率再令Tracker二次缩小。最多64帧且像素总内存≤384MiB，溢出/漏帧/异常标明incomplete并使显式诊断失败；不建立生产图像FIFO。复制/转换耗时另计，不把该次性能当正常捕获性能。

同步回调只做有界内存复制，不压缩或写文件；原Stop/drain和所有回调线程退出后才保存自己root内CreateNew的PGM和有限JSON，不保存源借用指针。观察器拥有shared lifetime并原子挂载/卸载，不能迟到访问Fixture或影响新场景；宏关闭时无新引用/导入/链接。默认入口未启用，不新增设置/产品API。保存前仍核本次专用身份/几何、已知区域覆盖和安全；实际外部ROI内容不能由固定标记单独证明逐像素正确，原图用于后续独立比对/回放。


## 实际重复纹理的离线候选校准（2026-10-23）

已获actual a6ce完整输入、原算法精确回放及正文正确200px像素证据。原单位格票把底部13个仅有少量错误匹配的格子各算1票，143正确匹配只占24/37票而拒绝；错误流方向相反且与重复段落周期吻合，属于有依据的别名解释，不认定所有历史失败同因。

先在ignored副本检验fit/anchor每点1/max(格内FB有效点数,3)、分母实际fit总票；每格仍最多1票，仍要求.65一致性、40点、原FB/NonTranslation/Coverage/跳变/锚点/事务/三拒绝暂停。原单位格1/n歧义票和occupiedCells分母单独保留，尤其零主流的.15/6中部格和.35门不得因密度降权消失。该权重改变统计含义，不能说数值.65未变就等于原模型。

候选不直接写生产：先原实际13帧与独立200px GT、原封不动6918+704帧和4项大图负例，再有意义的真实像素密度反转/少数真实流/局部密集外点回归。旧trace的expected记录原失败结果；候选回放差异属预期模型变化，不能标exact replay PASS。原算法精确回放器/原数据保持冻结。只有结果支持且经check后才进入生产最小diff与完整构建/必要重测。
