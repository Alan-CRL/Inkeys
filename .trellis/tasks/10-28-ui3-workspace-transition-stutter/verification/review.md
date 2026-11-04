# SVG独立数值计量扩展：最终复核（2026-10-28）

本次计量扩展可接受，未发现产品阻断。保持当前批次产品修复/正确性可接受、整体性能验收尚未完成的结论：上一批Release热态预算24/54，R4未完成。此次Debug短pilot只提供剩余成本计量，不能替代三倍率三轮性能验收或外推Release/原生视觉。

只读复核`verification/svg-numeric-timing.md`、spec、result JSON、日志及两份完整小型raw/sidecar；未重跑构建、测试、性能或像素负载，未改产品代码。上一批冻结证据仍按原EXE/hash保留，不能自动称为新instrumented版本全套通过。

## Findings (fixed)

无产品修复。报告两处精度建议已由主会话修正：N06实际为96×96（36864bytes）单SVG真实renderer全幅BGRA；排除结论仅限本次Debug 100%/200%短pilot的所测同步四段，明确不能外推150%或Release。

## 静态与版本复核

- `SvgStageTimingScope`只写独立TLS数值sink/clock，不安装或修改proof scope；timer无任何sink时零读钟，原probe时钟启用时仍使用同一对probe读钟，numeric共存不额外推进probe时钟/计数。无分配、COM、字符串或I/O，nested/null与早退按RAII恢复，缓存/hidden/culling/绘制决策未改。
- 固定32768槽在Start前一次分配，render owner只按ordinal写数字；Stop真join后输出CSV，对齐ordinal/batch/frame_ticks/phase并检查scope身份、缺配及溢出。环境变量、svg-pilot倍率/轮数和spec签名一致；未改RawCaptureReport schema。
- Debug绿版实际EXE SHA256=`0F5BD6755DAB9E533999A5B8D922D2C3C1A1D9701724BE0BA25C11A8169635C5`，与绿测和pilot结果JSON一致。红版实际hash=`20C7A0C8968DC8EC4C8C8D872CB128B1A2FCA7BC19B3FD8B9E49A8ABC5A44083`，红测OS exit1且恰为N02/N04/N06三项新合同失败；绿版完整Solution构建和offscreen均可靠exit0、failures=0。
- 本次4源码SHA256：PresentationProbe.h=`9a3dcafd63e3d6199ae9f0f0b9881599008a3f975a5edc63be8900307eaca526`；PresentationProbe.cpp=`722c91132ec13efde31387c07ba919448a56f4be06ac1112e60573faf70f356e`；EraserAttribute.Test.cpp=`8e798d046b2e34154f910510986d0e57227a474e06ee2f4b84f83582652c6aa9`；MultiTarget.Test.cpp=`3cd9143f8abe49ae31b2e515f5fbb306084c1add661781a31b95e2c61e3c99be`。均保留HEAD BOM、工作区CRLF且bare LF=0；RenderLoop/FramePacing/RenderPipeline产品修复源码仍与上一冻结manifest hash一致。

## 原始计量复核

- 两份metadata均sealed、callback/batch/svgTiming掉样0，clock period为1/1e9；958行sidecar（496/462）全部与raw Bar callback逐行匹配ordinal、batch、frame_ticks、committed，各自12段phase range也完全一致。观察scope保持null且未变化，每行clock_reads=2×四段calls之和；真实commit戳均位于对应callback内。301/272次成功提交，非commit行四段calls全0。
- 独立复算成功帧分母：100%为129.8003ms四段/2829.1816ms Bar=4.5879%；200%为113.4505/3747.8427=3.0271%。仅P/R/U为4.4467%/2.9256%。全callback分母另得3.8085%/2.6250%，报告已以成功帧口径为主，未用空Continue/Idle冒充提交或稀释主结论。
- N01–N07真实offscreen通过；N06是已有真实单SVG cache复用及96×96全幅timing off/on逐字节相同，不是新增整套workspace像素矩阵。N07的4096次微控制报告增量0.712us/阶段，只是一次Debug观测，未从阶段时间中扣除，也不作为产品延迟承诺。
- 长尾完整保留：100%最慢batch115.399ms、GetDC51.056ms、SVG0.021ms；另有WhiteboardToPpt batch38.551ms、SVG17.6825ms（Raster16.5302ms）。200%最慢batch110.1201ms、GetDC86.0832ms、SVG6.0016ms。后续77.6824/53.2241ms批次和SVG/Draw/GetDC分项均在报告，不因累计占比小抹掉真实离群。
- 所测同步四段未呈现主要普遍占比，因此当前没有证据支持直接改SVG算法/缓存。Parse之前的UTF/替色未包含，Raster含lunasvg内部工作，Draw只是CPU提交，GetDC仍可包含延迟执行的SVG/WARP；不能由此排除全部SVG执行，不能把GetDC全称纯复制。spec及报告均保留此边界。

## Findings (not fixed) / 仍未完成

R4和原生/Freeze/Office/Draw3/fresh资源验证边界不变；新版本没有重新运行Release、270次矩阵或完整Headless。当前纯计量扩展以完整Debug编译和真实offscreen全部合同验证，旧Headless可靠exit0仅对应上一冻结hash。继续选择产品候选必须有具体GetDC归属证据，当前未追加猜测优化。

## Verification

- Lint/格式：`git diff --ignore-submodules --check`退出0，4份新增改动源码编码/换行检查通过。
- TypeCheck/编译：新增计量版完整Debug|ARM64 Solution pass（已有可靠exit0）；本代理未重编。
- Tests：新增计量版真实offscreen合同pass，100%/200%各一轮每向2cycle短pilot pass，raw/sidecar关联及零掉样pass；本代理只读复核。
- 整体性能验收：尚未完成，任务保持in_progress。

---

# 冻结批次审查结论（2026-10-28）

当前批次实现、编译、正确性和确定性容量门禁可接受，未发现阻断性产品问题；整体性能验收尚未完成。最终Release热态仅24/54组达到既定batch P95≤16.67ms且P99≤33.4ms，PRD的R4仍未完成，任务保持in_progress。剩余GetDC/flush及SVG计量接缝继续由主会话研究，不能把本批handoff当任务完成或稳定60FPS承诺。

本轮按handoff只读复核`verification/final-verification.md`、已有JSON/日志/CSV及少量原始记录；没有重新执行构建、Headless、性能矩阵或像素比较，没有修改产品源码。

## Findings (fixed)

无。未发现需机械修复的产品问题。仅更新本审查记录；早两轮结论保留在下方作为历史过程，最终数据以本节为准。

## 已核证据

- 版本一致性：`final-source-manifest.json`的6份改动代码源SHA256与当前工作区逐项一致，BOM/CRLF一致且bare LF为0。实际Debug final.exe哈希为`4E9B53F6…FCBAC`，隔离Release为`C0BAD63F…7413BE`，最终Headless为`2E5583F0…25CD7`，均与对应结果JSON一致。最终源码另外恢复了`requestedWidth > capacitySize.cx`的可见内容重扫守卫，未见其绕过fixed映射或full replacement。
- 编译/正确性：`widthguard-build-result.json`、`release-build-result.json`为完整Solution的OS exit0，日志包含Inkeys、PptCOM及正确输出路径；构建沿既有ARM64原生MSBuild和配置，未改项目/依赖。`final-cli-results.json`的state-mode-production、bar-eraser-offscreen、bottom-dock三个生产CLI均exit0；centering像素模式exit0；最终`headless-direct-result.json`为同步直接调用后的exit0，19:41:15–19:47:02，日志末尾PASS animation correctness。旧ExitCode=null wrapper未作为通过证据。
- 性能证据：四版本共36份metadata均sealed、零callback/batch drop、时钟周期1/1e9、5cycles，nativeUlw/Freeze/nativeCoordinator/freshCapacity四项均false。每版54组且保留all/cold/hot；summary成功commit总数baseline/timer/Debug-final/Release-final分别4630/6645/6828/6951，failures均0。Debug A/B组commit间隔P95中位37.7388→20.4514ms与报告一致，这不是全部帧聚合P95。Debug热预算22/54，Release热24/54，未删失败组或放宽预算。
- 容量反例：最终Debug与Release各9次变化均为每fixture首pending placement允许的一次，其余稳定段零变化。抽查Debug 150%-run1和Release 200%-run3完整30阶段原始target序列，均为[1,0,…,0]，commit戳处于各真实callback起止内；保留旧150%290→292失败候选，未把旧exit0当新资源门禁通过。
- 像素/成功关联：`final-pixel-comparison.json`记录324张中276张与同次成功CSV关联，48张无成功row明确排除；抽查差异最多的三个样本，其case/phase/frame的serial、attempt、source与结果记录一致，CSV只在完整成功serial推进后写出。231/276帧有差异，40245/49188996像素（0.0818%），RGB/alpha最大1LSB，geometry mismatch和source containment failure均0；全量分类结果为baseline 3×3非恒定邻域40245、平坦区0。结合未改绘制/AA公式，有限浮点/8bit舍入是合理推断，未证明严格因果，也不是bit-exact。
- 清理/范围：两份FXC .cso当前bytes均与`git show HEAD:<exact path>`逐字节一致，亦与restoration.json hash一致。generated-artifact-cleanup.json列出的10张PNG目标hash逐项吻合。当前status仅两份spec、6份代码源及本任务目录；无shader/工程/依赖改动、commit或push。

## Findings (not fixed) / 验收边界

1. R4性能预算尚未完成：最终Release全部22/54、热态24/54通过；GetDC包含D2D提交前flush，尚未分离全部剩余来源，SVG细分probe也未挂入当前fixture。不得把GetDC全部称为纯位图拷贝或宣称已排除SVG。
2. fixture直接发布Committed workspace，覆盖真实Bar/公共renderer/PageControl/Scheduler和假ULW sink，不覆盖Window/Freeze/Office/Draw3业务ready总延迟、fresh资源、原生ULW/DWM可见手感及显示器切换。5cycle是实际范围，不能写成原30cycle计划已完成；cold首cycle已Desktop预热，不是fresh初始化。
3. 一致性比较仅覆盖脚本明确列出的几何、目标窗口及完整替换字段；source/target存储映射实际改变，分别作包含校验，不是source_x/y逐值相同。CSV没有独立hit列，不能将像素几何比较扩大成逐字段hit快照等价证明；已有生产几何/拖放断言的通过范围单独保留。该报告措辞精度建议已交主会话处理。

## Verification

- Lint/格式：`git diff --ignore-submodules --check`退出0；源码hash/BOM/CRLF与冻结清单一致。
- TypeCheck/编译：已有最终完整Debug|ARM64与隔离Release|ARM64 Solution均pass（可靠OS exit0）；本代理未重复编译。
- Tests：已有最终生产CLI、centering及direct Headless均pass，确定性资源/成功事务门禁pass；本代理复核结果与必要raw小样本，未重复运行。
- 性能验收：未完成。Debug A/B收益可报告，Release绝对预算未全面通过；任务仍in_progress。

---

# 独立审查（第一轮：只读静态）

2026-10-28。按主会话要求，容量性能/像素采样期间仅做静态阅读及轻量格式检查；没有改产品代码、启动构建/测试/GUI或干扰采样。

## Findings (fixed)

无。本轮未发现可直接确认的阻断性产品源码问题，也未自行改写并行实现。

## 静态复核结论

- `Bar.FramePacing.cppm:268,592`：新增导出bool接缝只接受正有限等待，复用线程私有已有timer；内部没有Legacy自旋回退。high-res创建/操作失败后的兼容路径及线程退出CloseHandle沿原封装。
- `RenderPipeline.cpp:739`：剩余预算按原batchstart deadline计算；失败回退原绝对deadline，而非重新等一整帧。没有新timeBeginPeriod/线程、没有客户端本地限帧，idle event、请求合并、串行顺序和Stop/join不变。
- `Bar.RenderLoop.cpp:7745,9453`：只删不必要的隐藏面板初始容量启发式，最终容量仍覆盖current、完整实际曲线预测、手势预留、candidate viewport。显示终点倍率乘独立required size后max历史容量，未对历史容量重复放大；fixed held/released表面分支仍原样。
- `Bar.RenderLoop.cpp:8086,9518,9607`：已知root真实移动在Observe前全量校准；晚期纯target/source映射变化不再将tracker误当全观察，但仍通过full mapping对完整viewport清绘、`presentDirty`及ULW整窗替换。失败仍RequireFullDirtyRetry，成功后才提交dirty、viewport、mapping及命中。
- `Bar.MultiTarget.Test.cpp:594`：新的六向性能驱动使用真实Scheduler/steady_clock，所有状态和Page快照在同一render owner，主线程只等atomic complete。raw封口/零掉样/有效时间/生产stamp Valid及epoch/generation/success serial连续性均检查。每方向及每transition重置间隔/resize统计，空Continue不算成功提交。
- `dirty_region_tests.cpp`使用真实tracker覆盖稳定对象bounds保留及root提前全观察后子项隐藏；它证明tracker边界合同，生产接线另由源码审查和真实几何/像素回归核验，不能说该单测独立覆盖完整coordinator。
- 修改保持公共Button/BarUIRendering、默认WARP/光效/AA/字体与动画曲线，未触碰三态ACK/fence/失败补偿或Window/Draw3生命周期。

## Findings (not fixed) / 验证边界

1. 像素图片在ULW sink内、ReleaseDC/EndDraw之前保存（`Bar.MultiTarget.Test.cpp:1594`），是attempt source。`Step`在无成功serial增长时可直接返回，因此“几何测试exit0”本身不能保证每个已保存文件对应完整成功事务。建议实施记录将每张所选图片与成功`case/phase/frame/attempt/serial`关联；主会话已将spec描述改为attempt source+必须额外核完整事务。此项先记为验证证明缺口，不改生产呈现。
2. 324对图片仅38对bit-identical，其余报告max通道差1/255；不能称严格像素等价。只读抽查三个差异最多样本：right/left150 FreezeHide25分别685/684个像素变化（总351024），alpha零支持13点0↔1；LeaveWhiteboard6为704/268456像素变化，alpha零支持61点。代码未改绘制公式，差异稀疏且界限1LSB，符合storage整数原点变化后的浮点/8bit数值舍入形态；这是有依据的推断，不是全量因果证明。保留全量通道/位置统计，并检查无尺寸/裁剪/大范围支持差异，不用均值掩盖差异。
3. fixture的workspace由测试Scope直接置Committed，Freeze/native/freshCapacity明确未覆盖；不证明真实业务请求→ACK总延迟或Office/DWM视觉。cold首循环与hot其余循环需分别保留，也保留all原始样本及超50ms记录；未达预算不得用仅间隔改善宣称完整性能验收。
4. 性能summary的batch分位来自实际Bar成功提交对应批次，700ms场景等待及空Continue排除在活动提交样本外。报告应保持该口径，不把它说成所有空闲/恢复批次或光学帧率；完整raw仍应保留以审查跨客户端延迟。

## Verification

- Lint/格式：`git diff --ignore-submodules --check`退出0。8个已跟踪改动文件UTF-8/BOM状态与HEAD一致，工作区为纯CRLF；HEAD的LF是`core.autocrlf=true`规范化，未把它误判为换行污染。
- TypeCheck/编译：本审查阶段未执行（遵守并行采样只读要求）。已读相关成功构建记录不等同本代理独立重编译；最终待实施完成后复核。
- Tests：本代理未执行（同上）。待主会话handoff后读取最终测试结果、必要时最小补测；不重复完整性能矩阵。

## 上下文补读

已读取保存的hook输出（含截断提示）和清单、PRD/design/implement及四份研究/规范地图；补读原始rendering文档的公共按钮/统一状态、idle/共享串行调度、PageControl单一来源、脏区/完整容量与成功呈现、共享WARP/光影、批次中点/复合面板、workspace/Window、连续字体章节；ui3-render-diagnostics（含新增测试env合同）、whiteboard统一交接、canvas-freeze场景准入/窗口期限/恢复写集、PPT session/UI-ready及C++/构建规范。未将注入前缀视作全文。

本轮结论：产品静态审查没有阻断性发现；最终构建/测试/全量像素证明及性能预算结果仍待复核。


# 独立审查（第二轮：最终容量候选，只读静态）

2026-10-28。主会话通知旧的未来X方案及较早容量候选未满足每转场零追扩，已撤回；本节复核当前源码，第一轮容量/像素数字仅保留为历史过程，不能当本候选最终验收。当前完整性能矩阵正在实施代理处运行，本代理没有编译、测试、启动GUI或修改产品源码。

## Findings (fixed)

无。当前约112行的RenderLoop差异未发现需立即修复的产品问题。

## 第二轮复核结论

- `Bar.RenderLoop.cpp:7744`将已有`ValueRange`提前复用；`ResolveBarWindowAnimationRange`同时包含current和实际一段/中点两段曲线极值，稳定属性不重新贡献历史段。横向仍为旧对称预约及原有边距，只把宽度上界改为完整段极值再加固定Back余量，不在已过冲current上继续乘1.06。
- `Bar.RenderLoop.cpp:9144`的未来Y与当前锚点空间一致。`displayCenterY`初始化为root DIP×zoom；跨屏先改monitorOrigin再存start/target local center；白板/任务栏目标减monitorOrigin，拖动也先扣monitor top/直接平移。因此它已经是monitor-local物理像素，新代码只把`mainButtonY`的DIP范围乘一次frameZoom，与displayAnchorY求floor/ceil边界，没有再乘DPI或叠加screen origin。
- `Bar.RenderLoop.cpp:9470`先从`SIZE{1,1}`独立求当前/两端未来Y锚点对同一完整content/viewport的对称reach，再乘目标倍率并max历史容量。三次Resolve均不消费历史容量，故ratio不会指数放大；最终Resolve仍按实际当前锚点、当前布局和完整candidateViewport兜底。横向没有新增未验证的future X预测。整数anchor两端向外舍入，source仍在实际bitmap上作宽整数包含校验，不能越界时钳source呈现。
- `Bar.RenderLoop.cpp:7726,7765,9464`：固定held/released表面的size、origin和viewport仍来自既有冻结快照，绕过新容量预约；显示serial/zoom匹配、释放表面失配重准备、手势越界取消分支未改。普通分支只保留历史容量，不在动画中缩小target。
- `Bar.RenderLoop.cpp:8093,9537,9629`：已知root layout变化在任何ShouldObserve/Observe前要求全观察；新bitmap/target/source变化虽晚于选择性观察，完整mapping tuple仍强制完整candidateViewport清绘及整窗ULW。删除晚置tracker ForceFull避免成功时清掉稳定对象旧bounds。资源失败、无效source、defer和四API事务失败均RetainForRetry(true)/RequireFullDirtyRetry，未成功不提交dirty、viewport、mapping或命中；下一帧全观察恢复，未削弱device lost重建。
- `Bar.MultiTarget.Test.cpp:705`新增每transition target变化断言，只有整轮首transition（begin==1）允许明确pending placement带来一次后续尺寸变化，其余transition允许0次。旧D→P每次3次增长会使首次及后续阶段失败，不能只报告resize统计后仍exit0。统计来自真实成功commit的target尺寸；任一presentFailed、无效stamp或掉样本身也使运行失败。此断言覆盖当前fixture的重复转场反例，不证明fresh初始化之前未采样的分配或native/Freeze链。
- 两份spec保留原有预算/失败分母，并已明确像素是ULW sink内的attempt source、必须另核完整事务成功；未把1LSB差异称为bit-exact。实现仍保持公共按钮、统一renderer、曲线/光效/字体和三态ACK/fence路径。

## Findings (not fixed) / 尚待实证

1. 需要最终候选的完整三倍率三轮性能、headless/几何/公共renderer回归及完整Solution构建结果；此前候选的成功构建或pilot不能替代此结果。未启动本代理重复负载。
2. 需要最终候选的全部viewport BGRA对照，以及每个所选case/phase/frame对应成功serial的关联证明。第一轮记录的324对/1LSB仅为较早候选数据，不能直接迁移。当前Step在serial不增长时可返回，故仍应以最终成功行关联完成证明。
3. 当前未来Y预约的比例与锚点计算静态上保守；100/150/200%实际序列是否首placement之外零追扩、是否达到热态batch预算，以正在运行的最终矩阵为准。无native/Freeze/fresh容量和真实显示器切换证据的边界保持不变。

## Verification

- Lint/格式：本轮`git diff --ignore-submodules --check`退出0；未进行无关格式化。
- TypeCheck/编译：未执行，遵守性能矩阵运行期间只读、不争抢构建资源的安排。
- Tests：未执行；完成源码路径和新增反例断言的静态检查，最终结果待主会话handoff后复核。


第二轮边界补审：150%首P→D暴露290→292的2px增长后，实施保留失败数据并在`Bar.RenderLoop.cpp:9480`补充grow-only余量。当前源码先独立计算plannedHeight，仅`plannedHeight > state.capacitySize.cy`时采用planned+2×viewportPadding；否则直接复用history，未写成max(history,planned+4)，后续帧不会因反复padding追扩。加法有LONG上界保护，fixed分支继续绕过；原per-transition断言未放宽。此窄补丁静态无阻断，最终全矩阵/像素仍待重跑结果；`git diff --ignore-submodules --check`再次退出0。
