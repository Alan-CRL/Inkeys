# 实际墨迹短流程：独立运行证据检查（PID43872）

2026-10-22；Active task `.trellis/tasks/10-21-scroll-follow-persistence-regression`，继续 in_progress。本 checker 只读本次现有日志、专用PNG及私有索引，运行独立离线像素计算并写本报告；没有修改产品源码、构建、产品CLI、GUI、系统输入或新截图，没有派代理、commit/push/finish/archive。GT失败原因交core调查，本报告不重复推测来源。

## 结论和边界

本次**部分正向证据通过，但整个短墨迹案例失败、未验收**：真实OS输入成功生成A，笔样式后的精确ACK确实恢复；主窗到非空辅助窗交接及真实前进32像素的墨迹屏幕像素正确。随后在反向滚轮发送之前，原SourceGT从99/99变成78/99且精确ROI不一致，按原严格条件终止。没有发生反向滚轮、B笔划或完整成功路径的Off/Exit读回。不能把A和前进32像素两段通过记作整个滚动批注功能通过。

## 版本与运行关联

- launcher前缀：`Build/ScrollFollow/live-product-ink-fixture-diagnostic-229909500-test`。
- 主进程PID43872，实际root `Build/ScrollFollow/product-live-43872-229909937`，主TID35092。
- launcher JSON：exit1，elapsedMs22889，stdout20141字节，stderr19057字节。
- `hashScope=file-before-StartProcess`，EXE SHA256 `230902583FBACC179F1F36CBB8323EAB627463C26DB513448392B8F4D1080791`，LastWriteUtc `2026-10-22T15:11:31.2263123Z`。这是启动前磁盘文件关联，不称进程内存hash。
- 对应冻结inl SHA256 `FC28A0BE3C5D2F3625BC74890FF81B244A9599B57D10193E13838A45BFF38BDE`，相对6C基线只增加此前已审六处诊断，不包含手动Follow目标、ACK或HiddenTestContact输入。
- 私有result.log为5414字节；它与launcher stdout中全部 `[ProductLive]` / `[ProductLiveInk]` 行逐行一致。stdout还含Draw3/RTS等直接诊断，所以两个文件总字节不同，不能声称整体文件相同。

## 实际到达的正向证据

1. 初始Source屏幕GT99/99、精确256×192专用ROI逐像素一致。Source激活使用真实OS移动/点击，实际cursor命中及foreground均为本夹具Source。该激活点击不算Pen输入。
2. RTS初始化 `RtsExtended` 成功。A-after-setters显示width/color accepted1、PrimaryReady1、精确ACK0；A-after-wait保持相同mode revision3/bridge revision5/runtime revision4并变成ACK1。说明**本次普通样式setter到真实ACK已收敛**，不是只读静态推断；没有据此推广到所有样式/生命周期。
3. A实际OS SendInput后：down/terminal/recycled各从0到1，contactSeen从0到1，CompletedDrawing、canUndo及ready均成立；clientDown=(768,706)。RTS trace真实收到一次StylusDown、16次Packets及StylusUp，publishOk18、decodeFailures0；decodedY约706.016说明RTS坐标本身有采样精度，不能把它改写成文件坐标验证。这里是**OS鼠标注入经当前RTS producer消费**，不是物理触控笔硬件测试。
4. A画出后真实currentPageHasContent1，Tracking中真实窗口pair=(0,1)、selection1、contentRevision/presented=1、精确ACK1。并非空页pair=(0,0)或手写ACK。
5. 主窗与辅助窗保存的A图片完全同bytes；每图18772个红色核心像素。交接前后核心集合精确相同，而非仅肉眼相似。
6. 一次实际tagged WM_MOUSEWHEEL delta=-240，Source世界offset从(256,512)到(256,544)，paint sequence从1到2。Tracker/owner已实际应用，辅助窗墨迹核心集合在屏幕精确向上32像素，SourceGT当时仍99/99且精确ROI一致。

## 独立PNG复算

checker没有消费core的 `ink-image-offline-check.json` 作为验收依据。另写忽略目录脚本 `Build/ScrollFollow/product-live-stage/check_ink_runtime_images.py`，用Pillow/NumPy独立复算已保存PNG：按C++ uint32模2^32重建世界纹理；全部ROI像素只允许背景RGB误差≤3或已知红色走廊，核心阈值独立重算，核心集合直接比较整数平移。输出 `product-live-43872-229909937/checker-image-verification.json`。

| 已有PNG | 尺寸 | 红色核心数 | 未知像素 | 核心走廊外 | alpha全255 |
|---|---|---:|---:|---:|---|
| ink-a-primary.png | 1440×197 | 18772 | 0 | 0 | 是 |
| ink-a-selection.png | 1440×197 | 18772 | 0 | 0 | 是 |
| ink-a-forward32.png | 1440×197 | 18772 | 0 | 0 | 是 |

三个墨迹ROI均为屏幕(720,541,1440,197)，初始lineY706，前进后lineY674。初始/selection Source offset(256,512)，前进后(256,544)。灰度背景不能满足红色核心条件。每图263068个像素符合背景±3条件，少数抗锯齿过渡像素在红色走廊接受；不把这些过渡区说成精确纯色。

- primary/selection文件SHA256均为 `835711d8ac277c3819e32f46d6c051b962f7c185c61b4f1b04d07f85854ecf80`，字节完全相同。
- forward32文件SHA256 `9266c3f76dfe7768bac535bb2e884edc4c7f347705299c3ceeafd2ae016fd674`。
- 独立mask比较：handoff零平移完全一致；forward32严格(0,-32)平移完全一致，没有越界丢核心。每次双向共37544个精确匹配，对应最大/中位误差0像素；这比原允许2像素条件更严格，是**这三张专用采样图**的结果，不证明采样之间无闪烁。
- `source-before-product.png` 256×192，屏幕(64,64)，offset(256,512)，独立公式逐RGB精确一致/alpha255，SHA256 `03d80a02f7731b0b7fdcbccb93301457a708f960a10c7201e5f0b8b08229c649`。
- 只查看已有forward32 PNG以作视觉核对；没有获取新屏幕内容，没有未知失败画面落盘。

## 捕获、追踪与失败位置

失败facts记录Capture accepted62、rejected0、API failures0；逐帧日志sequence1–62各一次。逐帧Tracker accepted是61条：frame1 Initialized成功，frame2在尚未进入Tracking时NotInitialized/accepted0，frame3–62 accepted1。**Capture质量通过62帧与Tracker接纳61帧是不同指标**，不能混称62个移动输出都接受。

最后实际owner latest/applied均62，Tracker累计(-0.0025,-31.9993)，Viewport(0.0025,31.9993)，clamp0，仍Tracking、ACK1/pair=(0,1)。与真实Source(0,+32)相比本次最终位置误差约0.003原图像素；它是单步专用case瞬时值，不是十屏累计精度或参数校准结果。62条记录的完整backend read均值约78.064毫秒，包含原Backend Read开销，不是纯PrintWindow计时；本例混入像素检查等待，不能用总运行秒数推断实际捕获FPS。

随后 `screen_gt stage=reverse16 samples=78/99 dedicated_roi_exact=0`，立即FAIL `stage=ink-wheel-reverse-16 reason=ink-wheel-source-gt error=0 security=0`。本次仅一条Source wheel与一次Source重绘到+32，没有第二条tagged反向wheel。没有新的fixture_failure/source_cancel来源日志。

security0仅说明既有安全层检测没有命中；不能因此断定是安全桌面、用户遮挡、GDI、辅助窗或Mag某一方的确定问题。实际失败图按原隐私/GT规则没有保存，所以没有可回放的错误RGB或窗口身份。本报告留因果判断给core下一步有界诊断，不降低99/99或精确ROI门槛。

## 退出和保存证据

异常后真实Business RequestStop的日志到stopping/off，真实窗口提交恢复primary；但本例没有执行正常主流程 `off-real-reverse-ack` facts、最终Viewport/GT断言及Off墨迹PNG，因此不记作完整Off验收。

cleanup实际state TID32456和mag TID52580均returned1/failed0；productRunning/serviceRunning/magReady/captureOccupied/ownLeftHeld/logFailed全0，result仍1。没有AV、watchdog超时或自己的鼠标Down未归还记录。

stdout存在真实Exit capture/commit：strokes1、bytes3215、trigger exit committed。私有目录实际有一个UInk和schemaVersion1 Desktop index，唯一entry dailySequence/sequenceInSession均1、trigger=exit。这是**失败清理所保存的一条A**的落盘存在/提交证据。没有进入 `ReadInkExit` 的两笔B首点/页GUID/Viewport/v10 ReadComplete/ImportSuccess断言；本checker也没有调用codec或人工解析几何。因此不能把实际文件存在、commit或索引唯一性称作完整A→follow→B→Exit内容验收。

## Findings (fixed)

无产品源码修改。独立离线检查脚本初稿的一处括号语法错误已在忽略测试脚本内修正，随后完整执行通过；它没有运行产品或影响原始证据。

## Findings (not fixed)

- 反向输入前SourceGT失配原因未定，core拥有调查；现有证据不足以唯一归因。
- 反向滚动、完整Off、B真实笔划/新视口锁存、成功路径真实UInk读回、十屏长滚动和外部应用仍未验收。本次部分正证据不能填补这些项目。
- 实际RTS/OS输入、有限样式ACK、辅助交接和前进32像素的采样屏幕结果可单独记通过；不代表硬件笔/触控、动画全程、所有形状/工具或旧系统均通过。

## Verification

- 原始PID/版本/退出码/日志关联：pass。
- 独立背景公式、核心集合、零平移/32像素位移、alpha和未知像素检查：pass。
- 原始CLI整体短墨迹结果：**fail（exit1，reverse输入前GT失配）**。
- 新build/typecheck/产品测试：本checker未运行，沿用之前FC28/23090258诊断版检查报告，不重复完整回归。

原始指纹：launcher JSON `1F19959265477932D916F12471526D5ADE233A47FFCCC0A4DF5CBDC2DABC0B57`；stdout `FFA490AF073B055ECCCFCBC62A57C2F0D0B38C41C87EBEF4878951B1A13E7005`；stderr `5497B6237F35C63AC807A769BDFA9721CBF3DD50C40852338B3F4011D7563181`；私有result.log `E93C8C441C6A240BA29BE9AE1F84D3E1477E9040045EBD1B8C9D2542F28157E2`。

## 后续GT诊断增量预审（尚非运行证据）

主会话随后授权core只补GT失配来源及像素比较耗时。checker读取精确FC28→E969 patch、before快照/hash、最终源码、完整build日志及最小CLI结果，未运行产品。

- before快照精确 `FC28A0BE3C5D2F3625BC74890FF81B244A9599B57D10193E13838A45BFF38BDE`；新inl `E969322714EA844B92A01CE14CFA47297CA49584FE19CEFBC1E87807E119597A`；新EXE `EF5A2E598F37C4FB255FD4FDFDB00DA24C25758D41BD9E2FE1706B33B6AA849C`。
- 增量仅在原GT检查已失败时记录最多3个既有grid点，以及既有crop首个坏像素的actual/expected RGB和当前命中HWND/class/PID/knownRole/rect，另记Source paint/rect现状；不记录标题、不额外截图、不保存未知失败图。WindowFromPoint是记录当时被动元数据，不保证是先前取样瞬间的覆盖归属。
- 99个grid点仍全部检查，原99/99及精确crop不满足仍return false，没有重试或绕过；CompareInk只加QPC elapsed_us，核心比较/误差/数量门槛不改。无新系统输入、显示、捕获、合成contact或手工视口/ACK。
- 新inl57925字节、892个CRLF、无裸LF/无BOM、EOF保留；scoped diff --check通过。
- 完整ARM64 `product-live-gt-mismatch`实际日志Build succeeded/0 Error/3原hashlib++ warning，外部36.269s、MSBuild内部35.77s。
- `offline-230171343/result.json`：16坏argv全exit2/0输出，protocol exit0/450ms，state exit0/115ms。

未发现阻止该有限nativeGUI诊断重试的新问题，已向主会话放行。**本节是新版本运行前预审，不把旧PID43872图片归给新EXE，也没有预判GT失败原因或新运行结果。**

## 第二个实际case：PID62840，新增反向与正常Off证据

新run launcher `live-product-ink-gt-diagnostic-230285000-test`，实际root `Build/ScrollFollow/product-live-62840-230285718`。JSON为PID62840、exit1/32681ms，EXE启动前hash `EF5A2E598F37C4FB255FD4FDFDB00DA24C25758D41BD9E2FE1706B33B6AA849C`，LastWriteUtc `2026-10-22T15:18:01.7158450Z`；对应此前GT诊断E969源码。这是独立新run，**不覆盖前一PID43872的失败历史**。

本次所有记录的SourceGT均99/99+精确ROI1，没有GT失配日志。A真实笔划完成、非空辅助交接后，两次真实tagged wheel先-240再+120，Source offset按(256,512)→(256,544)→(256,528)。正常主流程到达 `off-real-reverse-ack`：Business Off、Follow phaseComplete、latest/applied75、Viewport显示(0,16)、clamp0、selection0、content1、实际ACK1、窗口pair=(1,0)、Capture drained；Off后SourceGT和墨迹PNG也实际取得。比前一case新增了**反向滚动、正常final barrier／反向成对ACK及Off屏幕保持**的有限正证据。

### 独立已有PNG计算

新增忽略脚本 `Build/ScrollFollow/product-live-stage/check_ink_runtime_62840.py`，复用独立纹理公式定义，不运行上例IO或依赖core检查结果；按本run日志逐图提取ROI/offset重算背景与核心，再独立计算双向±2像素邻域距离。输出本root `checker-image-verification.json`。

| 实际保存的A图片 | 红色核心数 | 对初始A预期平移 | 独立最大误差 | 中位误差 | 未知像素 |
|---|---:|---|---:|---:|---:|
| primary | 18612 | 基准 | — | — | 0 |
| selection | 18612 | (0,0) | 0 | 0 | 0 |
| forward32 | 19110 | (0,-32) | 1 | 0 | 0 |
| reverse16 | 18612 | (0,-16) | 0 | 0 | 0 |
| off-primary | 18612 | (0,-16) | 0 | 0 | 0 |

五图均1440×197且alpha255。primary/selection文件相同，SHA256 `e507b2a32bb4cf22b036ee37ae286bdbb9185511ff28576f76af857a2717ccca`；reverse/Off文件相同，SHA256 `99ae1151f0c12d224091fb6096aeffeb53e9fb15427d96957b113d6249431c6d`。forward32 SHA256 `b0b61c79d2c3d3ed067211e3f4eda8464d8d6b49931fabc6ffcdab36f60ec0fa`。本run forward不是精确核心集合平移，核心数增加约2.68%，最大1像素，仍满足原2像素及数量±10%验收条件；不能沿用上一run的“前进最大0”描述。

before-product与after-off两个256×192 Source PNG分别按offset(256,512)/(256,528)独立公式逐RGB精确一致、alpha255。after-off SHA256 `4eb817eef3ef901ae40f38c7f309873227c183e2bd18da5d48ab667310cad58c`。只处理这些已保存专用图，没有新截图或未知失败图。

CompareInk实际日志QPC耗时：handoff11.692ms、forward11.228ms、reverse11.044ms、Off14.913ms；它是本机该次离线式CPU比较开销，不是取图耗时，也没有据此改算法或门槛。

### B输入与最终失败边界

A和B工具setter后均记录真实ACK0→1。B实际OS Down/terminal/recycled/contact计数从1到2、Drawing完成、canUndo/ready成立；RTS trace真实两接触各18callbacks（Down+16Packets+Up）、publishOk18、decodeFailures0。B客户端Down=(768,1346)，RTS解码约(768,1345.965)。这里只证明系统输入经当前RTS消费，不宣称硬件笔或实际文件首点锁存已经验证。

B截图读回的日志为 `core=19105 unknown=12`，随后立即FAIL `stage=real-os-ink-b reason=ink-pixels-or-dedicated-roi-unproven security=0`。**文件名出现在screen日志不代表PNG已经保存**；实际目录没有ink-b-primary.png，原检查在未知像素存在时先throw。所以checker没有B像素可独立复算，不将这12点归为抗锯齿、背景、光标或外部遮挡。core另行添加有界existingROI诊断，原阈值和失败规则保留。

本case没有 `actual_exit=PASS`、没有整体 `bounded_case=PASS`，未进入ReadInkExit。因此完整短流程仍fail，不允许将局部A／反向／Off和B输入拼成完整通过。

### 捕获、日志与清理

最终facts Capture accepted75/rejected0/API0，Follow已Off后不再发布，Viewport最终日志(0,16)，Capture occupied/inflight0。真实cleanup state TID83716/mag TID47136 returned1/failed0，所有running/ready/occupied/held/logFailed清零，result1。

stdout另有唯一原Exit capture/commit，strokes2、estimated bytes5903，实际UInk文件12285字节及index481字节。该bytes5903是capture诊断的估算量，不能说磁盘文件5903字节。它只证明失败清理确实提交两条笔划；没有本case成功路径的v10 ReadComplete/Import、页/工作区GUID、最终Viewport及B世界Down核对，不标文件内容验收通过。

诊断输出队列报告dropped3、contention3、full0。RTS自身有限trace stored8/dropped10是每接触18事件的有界保留计数，与诊断队列丢3条不是同一指标。不能声称本run所有诊断无丢失；所需的A/B实际完成、GT／像素、正常Off及最终失败均有明确现存证据。

### 此case核验结论

- 版本、PID、实际退出及日志：pass；独立PNG公式和有界位移：pass。
- 新增反向16／正常Off的真实窗口与采样墨迹：有限case通过；B真实输入消费通过。
- B像素、完整A→Follow→B→Exit文件读回及整个短流程：**未通过（exit1，B unknown12）**。
- 无产品源码修改、构建、CLI、GUI或新截图；本次default shell启动短暂失败后改用明确系统PowerShell只读运行，属于runner入口问题，没有修改项目应对。

原始指纹：launcher JSON `844E450715BDFDBA98F3567A9DEB4EC872A6677D8A72BED8A3165E8B8EA33DD5`；stdout `8C7FD568B8288908988A02769CF565520B65218F596B67DC8E4E32F17FA174E3`；stderr `694E26DE4753C3D71E3E0EBCC16012E2AA635076F3E867DC72753BD542AE3F9C`。

## 未知像素诊断版预审（尚非新run证据）

core随后冻结inl `E7C25A4E83D85D7ABA68FE619DF4C1E4CF1CFBE8ACAF1934FFB2D2E340CD3A43`／EXE `F568FE30399BDAFBC04228F4FDBF588733465E549873585279FF7D072D61F543`。checker核原E969 before快照/hash和精确delta：仅从ReadInkScreen原existingROI循环缓存前12个未知像素，并在原失败throw前输出RGB、期望灰度、ink/core/corridor及位置；原分类表达式、全部unknown累加、core计数、unknown或core不足即失败及未知图不保存保持不变。没有新截图、系统输入、重试、GPU或生产路径修改。

完整ARM64 product-live-ink-unknown日志Build succeeded/0 Error/3原warning，外部23.228s、内部22.82s；offline-230520984的16坏argv全2/0B，protocol0/301ms、state0/121ms。源码58828字节、909CRLF、无裸LF/无BOM且EOF保留，scoped diff --check通过。已向root放行该有限nativeGUI诊断重试；本checker没有运行新GUI或产品CLI。此预审不改变PID62840的整体失败结果，也没有推断其12个未知点的原因。

## 第三个实际case：PID10832，严格规则下保留4个B边缘未知点

run `live-product-ink-unknown-diagnostic-230606140-test`，root `Build/ScrollFollow/product-live-10832-230606640`，EXE F568FE30（启动前文件hash同上，LastWriteUtc2026-10-22T15:23:33.7765293Z）。JSON为PID10832、exit1/34528ms，stdout30690字节、stderr22043字节；对应E7C诊断版，不能将之后的像素规则修订追溯套入本次结果。

本次A／两滚轮／正常Off有限步骤再次完成：所有SourceGT99/99+精确ROI1，真实非空pair(0,1)/ACK1，Source先+32再反向16，正常Off Viewport日志(0,16)、实际ACK1/pair(1,0)，Capture accepted75/rejected0/API0。A、B真实OS输入计数依次到1、2，均完成Drawing，样式ACK两次0→1。state/mag returned1/failed0，所有running/ready/occupied/held/logFailed清零，整体结果保持1。

独立忽略脚本 `check_ink_runtime_10832.py`按本run已有PNG重算，输出本root/checker-image-verification.json：五张A图均18939核心、unknown0、alpha255；handoff0、forward(0,-32)、reverse及Off(0,-16)双向各37878个核心匹配，最大/中位误差均0像素。主辅同bytes、reverse/Off同bytes。两张Source PNG按世界公式逐RGB精确一致。不是只照抄运行内CompareInk日志。

B记录core18972/unknown4，实际没有保存B PNG；新增existingROI日志完整保留四个未知点：

| 屏幕点 | 实际RGB | 期望灰底 | corridor | ink/core | B-R / B-G |
|---|---|---:|---:|---|---|
| (771,1337) | (31,32,36) | 32 | 1 | 0/0 | 5/4 |
| (2113,1348) | (56,57,61) | 57 | 1 | 0/0 | 5/4 |
| (1944,1353) | (198,199,203) | 202 | 1 | 0/0 | 5/4 |
| (770,1354) | (193,194,198) | 197 | 1 | 0/0 | 5/4 |

这些值确实触发当时严格规则：蓝色条件要求B>G+4，而四点B-G恰好4；同时至少一个通道偏离期望背景超过3。因此本run按原规则失败是确定的。日志证明这些是原ROI内的观察值，不能仅凭接近笔色就追溯称为已通过或已保存的B图；分类修订须明确单独说明并重新验证。

本case仍未进入ReadInkExit，异常清理两条笔划Exit commit不替代文件身份/首点/codec验收。另research拥有实际文件RTS量化调查，本checker不重复该路径。

结论：A屏幕跟随、反向和Off采样有限通过；B真实输入通过，B像素及整个短流程仍**未通过（exit1）**。没有新build、产品CLI、GUI、新截图或产品源码修改。
