# 2026-10-03 单次执行

起点 chore/publish @4e004923；原 .gitignore 与未跟踪记录/日志/CSO 保留。沿用既有三个任务，Win7 ULW 延期。

顺序：1 UI3 剩余入口/资源复用的最小修正和 Debug ARM64 构建；2 当前源码 Release 同场景基线、UI3 热点候选及三轮对照；3 Draw3 独立热点同场景三轮对照；4 直接相关审查、必要测试与最终差异检查。

仅使用现有无窗口入口；不创建产品 GUI，不恢复像素不等价 exact A8 或已退化的 ULW fused copy。不 commit/push/发布；一份本记录承载交接。

阶段1清单：已符合——画笔/形状/橡皮/PPT业务统一入口、Scene/设备epoch已有生命周期；需改——StateUpdate五次快照读导致同次刷新混代，改为一份快照传内部方法；保留——外部UpdateDrawButtonStyle无参入口、Whiteboard/Freeze独立owner与PPT条件revision。初次构建TEMP权限问题用本次进程TEMP/TMP指向Build目录解决；首次Release编译捕获新声明缺IdtState.h，已由实现者修正，未称通过。

阶段1：完整 InkeysRepo.sln Debug|ARM64 build退出0；Release no-window 在工作区TEMP/TMP退出0，原默认TEMP失败11项属于环境，未删除/放宽测试。明确统一调用为 StateUpdate→CalcState/PresetHoming/UpdateDrawButtonStyle/UpdateEraserButtonStyle/UpdateGeometryButtonStyle，共用const StateModeClass&。
阶段2待量候选：主按钮n=3/10反向与尺寸脉冲导致超椭圆路径重建；保持原theta浮点计算，仅缓存同cornerSegments的sin/cos，先测生产路径和完整光效离屏再决定。闲置时钟已有SuspendForIdle→resumeFromIdle_归零首dt，本轮保留。

阶段2结果（Release ARM64/WARP，无HWND）：生产动画算法脚本48帧，n=3→10/第12帧反向/第24帧前向，duration0.4s，pulse1.1（产品Main为1.05，此为固定合成场景），zoom1/1.5，256×192。每轮7块×1536路径调用及192完整draw，前后各3进程；每个zoom每轮1344次EndDraw成功。BGRA共48帧逐字节前后相同。

| zoom | 基线三轮路径µs | 候选三轮路径µs | 基线三轮完整draw ms | 候选三轮完整draw ms |
|---|---|---|---|---|
|1|3.11745/3.42793/3.84089|3.08971/3.47441/3.30326|0.471944/0.476775/0.482519|0.415627/0.456880/0.477720|
|1.5|3.96100/4.60625/3.99694|3.46699/4.02760/3.85671|0.434057/0.446518/0.381454|0.423945/0.468781/0.433477|

路径中位收益3.6%/3.5%，低于基线轮间16–23%波动，完整draw亦无一致改善，已要求撤回仅Rendering两文件候选。未证实UI3显著性能收益；保留85行既有离屏夹具场景方便审查，不新建探针框架。数据/像素位于Build/automation-perf/ui3-{baseline,candidate}/{1,2,3}。真实ULW/光学/长idle体验、Canary包同机对照未测；局部路径并非本场景主要成本，不继续优化它。

阶段2撤回确认：Bar.Rendering.cpp/cppm git diff为零；StateUpdate与现有夹具对照保留。阶段3只针对真实GetOrBuildNode调用的CompositionCachePlanner满载miss；其131328次重复槽比较(512容量)发生在绘制线程合成节点准备阶段，不是整文档构建。先通过现有HistoryProbe接入窄场景，原Acquire基线保持，量后才改。

阶段3实现子代理额度限制未写代码；主会话按已授权的两文件边界接续。现有HistoryProbe仅增加100行窄场景、复用原Fail/计时/摘要。首次夹具构建发现Fail接受Scenario而非名称，已局部适配；基线复建退出0，原Acquire未改的3轮CLI均退出0。满载512容量分配每次CPU中位数63.348/66.545/64.193µs；128容量4.488/4.508/4.490µs。生产改动仅6行：计当前预算内的唯一resident槽，满载跳过空槽嵌套扫描，原LRU/空槽/pin保留，不添分配/索引/容量配置。候选待对照。

阶段3结果（Release ARM64，纯生产planner CPU，hwnd/gpu/present=0）：前后各3进程，原3warmup/11measured块；每块256次Acquire，released hole为8独立满载夹具各一次，构造/断言/hash在计时外。每次成本为块平均值，不是单帧P99。

| 场景 | 改前三轮µs/次中位数 | 改后三轮µs/次中位数 | 33块合并中位变化 |
|---|---|---|---|
| full miss 128 |4.48828/4.50781/4.49023|0.33555/0.33320/0.33320|4.49648→0.33516，-92.55%|
| full miss 512 |63.34844/66.54531/64.19258|1.32891/1.33203/1.32539|64.58203→1.32891，-97.94%|
| all pinned 128 |4.40312/4.41016/4.41250|0.24414/0.24414/0.23789|4.41016→0.24414，-94.46%|
| released hole 512 |16.51250/18.16250/16.47500|16.63750/16.56250/16.78750|16.58750→16.63750，+0.30%|

full512块平均P95 77.81328→2.77461µs/次（不当作逐帧尾延迟）。所有case status/slot/evicted-key摘要一致，原6个真实document/history笔/橡皮场景内容摘要也一致；缩预算高pin空槽/disable/release断言全通过。hit128合并37.89ns不变，hit512初次66.80→70.31ns，新增疑虑用已冻结EXE串行交错3对复测：baseline 68.36/66.02/67.58ns，candidate 66.02/65.62/66.02ns，未见稳定命中退化；满载512交错baseline62.64844/64.34844/63.40664µs，candidate1.33555/1.33164/1.32422µs再次确认收益。hole交错约16.4→16.7µs，仍在已观测轮间波动范围内。

原始样本与冻结exe位于Build/automation-perf/draw3-{baseline,candidate}及draw3-interleaved。候选保留；只宣称满载合成缓存分配CPU改善，GPU等待/上传/最终呈现、首次落笔/抬笔/实时Laser帧尾未测。没有关闭效果/改笔迹/改混合/改擦除/改输入顺序/改持久化、缓存容量和设备策略。

阶段4与最终验证：独立 trellis-check 复审未发现本轮引入的具体回归，快照引用同步使用且不逃逸，预算内槽计数保持原空槽/LRU/pin语义。最终完整 Debug|ARM64 与 Release|ARM64 solution 构建退出0；原 no-window 检查、UI前后及撤回后离屏检查、Draw3前后与交错复测均退出0。最终UI像素与原基线逐字节一致。git diff --check通过，源文件BOM/CRLF保持，Rendering候选差异为零；既有警告未扩修。原TEMP权限失败明确为环境失败，工作区TEMP/TMP复验通过。未运行产品GUI、Hardware/Win7、其它架构或真实首笔/抬笔/Laser呈现检查；未找到20260811a Canary可用包。不宣称可见帧率、GPU耗时或Win7通过。Win7 ULW及无关历史审查明确延期。

收尾：2026-10-03 09:45 Asia/Shanghai，本轮单次执行完成。实际分支/HEAD与原未提交内容保留，代码差异可审查；不commit、push、发布或追加运行。UI3无可信显著性能收益，Draw3满载512分配CPU下降97.94%，交错复测确认。自动任务记忆仅保存本次结果与验证边界。
后续授权：用户明确要求提交本轮改动但不结束任务。仅提交本轮五个源码/夹具文件及两份本轮记录；原.gitignore与其它原未跟踪文件保留，不push，不执行finish-work或归档，draw3-performance保持in_progress。


## 后续有限 UI3 场景（2026-10-03）

实际基线 dde6cdf7ef74ebbd8daeaf1e4ca8363bedc702c8，原 .gitignore/未跟踪文件保留。顺序为主按钮框架/资源小范围核对、实际 GUI 原版采样、单一有证据候选及对照、直接复审；不继续 Draw3/Win7/全仓审查，不commit/push。

规范化：TryBeginToggle→UpdateRendering→RenderPipeline::Request、动画时钟/路径/SVG/渐变及模糊context/effect已有机制，保留。GetGeometryDiffuseMask 每次 miss 的局部恒白画刷改为复用现有 frameDiffuseExactMaskBrush（无颜色/透明度/变换修改，设备资源清理统一释放）。最终仅 Rendering.cpp 4增4删。

真实GUI由用户完成每版3轮展开/收起/快速反向/脉冲及闲置后展开。Computer Use两次启动/枚举无法返回tool/noactivate悬浮窗口，用户确认可见并实际操作；不伪称自动GUI完成。设备Adreno X1-85，2880×1920/60Hz；UI3实际WARP+ULW、zoom2/lightFlags3，Animation SpeedRate1，光影开启/动态光影关闭，配置和时长不改。正常产品健康摘要默认关闭，临时启用现有 HealthySummariesEnabled 同条件构建双方采样版，最终恢复false（无代码残留）。Canary不作为门槛。

改前16摘要窗口/694回调/650成功呈现：GetDC均值4.5697ms/成功呈现，回调均值5.8839ms，窗口回调均值范围4.135–9.770ms，中位5.464ms；回调max23.642ms，活动成功提交间隔max46.219ms。几何父遮罩118 miss/create，创建78.141ms，占回调总4083.425ms的1.91%，GetDC占72.74%。这些是API段/回调与摘要窗口统计，不是逐帧分位数或可见FPS；GetDC包含提交执行/同步，未单独分离CPU/GPU等待。

仅依据上述代表性证据试过后备目标容量候选：复用现有预测包络，减少隐藏More/ColorPicker预留高度，目标2796×1892→2796×368；保持画质、DPI、横向预热、grow-only及原GetDC/ULW事务。第一段改后操作与像素测试有时间交叠，已排除其性能结论。安静再次3轮得到19窗口/822回调/774成功呈现：GetDC4.5358ms（-0.74%），回调5.7547ms（-2.20%），窗口均值4.659–7.523ms/中位5.821ms，回调max30.273ms，活动成功提交间隔max41.545ms。双方窗口内回调max均<50ms；峰值没有改善。均值差低于波动，不证明显著动画提升，整个容量候选撤回。复审发现派生More/颜色面板初开逐帧扩容风险，临时条件预留修补亦随候选全部撤回；未把该补丁当作已验收成果。最终RenderLoop和Diagnostics差异为零，CRLF/BOM恢复原工作副本格式。

验证：最终InkeysRepo.sln Release|ARM64原生MSBuild增量构建退出0（6既有类型转换warning，runner输出pwsh找不到未升级工具链）；受限首次构建依赖节点失败/零源码错误，完整权限环境构建通过。复用已有window_geometry/dirty_region/present_decision的已编译对象，仅Build内极小driver运行这三组，退出0，未改测试源码或构建体系。现有--bar-eraser-offscreen-test退出0，48帧光影BGRA在zoom1与1.5逐字节一致，只验证画刷/renderer像素，不能替代真实GUI或宣称容量候选GUI逐帧等价。用户反馈候选主按钮操作未见视觉/交互异常。独立trellis-check确认最终白画刷同资源域/常量且无剩余容量或诊断变更；git diff --check通过。不跑全CLI/旧More216目标/其它架构。

结论：未获得显著动画性能改善。几何父遮罩创建不是本场景主耗时；缩小后备目标虽省面积，GetDC/整回调收益未成立，已撤回。保留明确资源复用，尚缺GetDC内部WARP执行/同步更细归因，不新增本轮探针或扩大搜索。现有快照和Draw3优化、HTTP回退/PPT/数据保护保留，Win7 ULW延期。正式体验程序 Build/ARM64/Release/Inkeys.exe；样本/冻结双方程序/统计/撤回diff存 Build/automation-perf/ui3-product-20261003。


## 连续发布前范围（本轮冻结清单，基线 dde6cdf7）

仅以下项进入执行；除直接回归/必要前置外不扩项，原修改全部保留，不提交。
- N1 UI3主栏/按钮/属性状态与唤醒：检查重复读取、重复刷新、入口绕行；统一明确重复调用；验收调用点差异与相关交互。
- N2 UI3动画/资源/失效：复核恒白画刷与路径/SVG/光影生命周期、owner/epoch/更新顺序；只修有证据的问题；验收设备清理、相关像素及直接复审。
- U1 分别确定正常UI3与Draw3设备/适配器/呈现及回退原因，检查测试配置；正常配置基线保存且不强制改后端。
- U2 主按钮/属性/光影/idle/使用后变慢真实GUI：用既有阶段统计配合短WPR采样拆GetDC执行/等待；正确分母，保存前后分布/慢帧/首交互，采样不并发构建或图形测试。
- U3 UI3主要热点最多两个有源码和实测依据的方案，每个至多一次候选+一次噪声复核；无稳定收益撤回。既有低占比遮罩、sin/cos、容量、像素不等价A8不重试；资源随使用趋势单独记录。
- D1 UI3结果或具体阻塞后，建立Draw3首笔/连续/较多墨迹/橡皮/Laser/历史切换真实场景和独立线程指标；已有局部allocator收益只按真实miss适用条件解释。
- D2 Draw3按证据最多两个独立生产方案，每方案一次候选+一次噪声复核；检查排队/几何/上传/复用/拷贝/呈现，输入/笔迹/擦除/历史保存语义不变。
- R1 集中复审本轮差异与直接依赖及仍有效相关待办，修真实状态/线程/生命周期/异常/数据问题；不重审历史全仓/PPT正常模块/更新策略。
- V1 最终实际版本Debug与Release ARM64 solution及受影响必要检查，视觉像素/实际交互、输入/历史语义回归；其它环境未测列明，输出Release位置与生产/测试/记录diff。

每项最终标记验证完成/已调查未达标/具体阻塞/用户延期。人工或设备阻塞不结束其余独立项。Win7 SP1+KB2670838专项ULW延期，正常ULW优化仅凭当前实测；HTTP兼容/PPT/UInk政策不改。

进度交接（11:42）：N1已完成8文件状态/悬停入口统一，Release及用户真实工具/属性操作通过；N2白刷复用扩到rounded父遮罩，48帧zoom1/1.5与保留原像素逐字节一致，设备/SVG现有检查exit0。U1 UI3源码正常选WARP+ULW；Draw3独立实际Hardware FL11_1+DirectCompositionVisualTree，waitable、FLIP_SEQUENTIAL，辅助Selection另走ULW，无启动失败回退。采样只临时启用已有HealthySummaries及RuntimeMetrics，IdtMain/Diagnostics原bytes已存Build，最终全部恢复。
U2 WPR CPU+GPU两种执行均因policy 0xc5585011失败、未记录，不改系统安全；用户前后三轮及属性/闲置操作均未见异常。当前较宽UI操作基线53窗口2536回调2488成功，mean7.948ms/GetDC5.771ms，占71.23%，geometryCreate占0.34%；候选23窗口1130回调1104成功，8.517/6.144ms，切片17.34→14.74每成功提交。0失败/0reset；两组人工用时不同，窗口mean范围3.109–12.347与3.257–12.698，max89.463→43.221及activegap441.677→38.855不能据较少曝光宣称改善；只是窗口/软件提交指标，非逐帧分位/光学帧率。
U3透明光源外整片跳过候选已Release和像素通过，但实际耗时无可信收益，要求完整撤回、保留白刷。off-dirty mask方向低于0.5%成本不追加；不重试旧容量/sincos/A8/fusedcopy。UI3尚未达到显著性能目标/使用后偶发变慢根因未重现。样本及exe：Build/automation-perf/release-continued-20261003。接下来具体：用户正常退出候选→保存Draw3 metadata/session→用冻结规范化基线开始真实笔/橡皮/Laser/UndoRedo→按独立CPU/wall/present数据选最多2个Draw3候选→最终相关复审、恢复采样改动、Debug/Release与必要回归。禁止commit/push。

Draw3真实基线（11:45正常退出）：Adreno Hardware+主DComp正常路径；102 Down/8158 Move/102 Terminal均发布，0拒绝/争用/指标丢失，6969帧/5357成功Present/0失败。非Laser83个Down全部确认（Pen54/HardPen1/Highlighter15/Eraser13），19 Laser按源码现有排除规则Unpresented，所以coverage.complete=false不能称整个指标验收通过；Laser Active/Hold/Fade有2008成功帧，用户全部操作未见异常。首笔闲置后software1.677ms；全部nonLaser landing median1.064/P95 9.043ms，非光学延迟。绘制循环间隔median8.337/P95 8.346ms是120Hz产品节奏，显示器仍60Hz，非可见FPS。
Physical2923帧wall mean0.706/P95 1.232/max6.915ms，Present均0.285ms；Terminal98帧0.798ms；Laser lifecycle2009帧0.398ms，均无>16.67ms工作帧。Command46帧mean3.157/median1.789/P95 7.791/max12.660ms，其中stdout43次Undo（20 hot_preimage、23 composition_rebuild），未捕获Redo。现有四cost spans未覆盖命令RestoreComposition，准备仅复用GeometryRasterSubmit加窄子段计时；GetThreadTimes15.625ms量化不能用逐帧CPU差值冒称GPU等待。下一步保留基线exe和rawJSON，细分冷Undo成本后只试首可见item identity合成冗余候选，若必要添加复用现有GPU初始化的窄像素检查，不建框架。不把planner微基准当实际首笔收益。其它独立热点当前工作帧已小，无证据不硬改。

交接（12:35）：Draw3候选A仅将冷leaf首个可见RasterItem直接用作累积operator，空leaf仍identity，后续顺序/失败回退不变。Debug候选首次链接LNK1000 IncrBuildImage/C0000005，证实本次link等待WerFault，仅结束该失败link；命令行LinkIncremental=false完整链接exit0，不改工具链或工程。新窄pixel CLI复用现有InkRenderer/无HWND交换链，在Hardware/WARP FL11_0五场景各32项4tile严格比较Add/Retain/最终L2，exit0全等；未把离屏结果计作真实收益。
直接trellis-check已审本轮全部生产调用与fixture，无功能缺陷；修Bar.Main粗细门控注释1行，BOM/CRLF/diff-check通过。临时IdtMain runtime metrics/metadata/export、DrawingController命令子段计时、Diagnostics Healthy最终必须按Build字节备份移除，Main若保留pixel只剩CLI3行。UI径向切片已完整撤回。
当前旧采样进程PID31156尚未正常退出，未收到新冷UndoRedo操作；Computer Use再次枚举仍无可操作Inkeys窗口。当前构建/图形检查期间数据排除正式对照，等待人工正常退出后切Release候选做安静同场景。Release候选用现有solution+Release ARM64，仅命令行OutDir=Build/ARM64/Release-draw3-candidate以免覆盖运行exe。尚无实际改后Draw3收益证据，不宣称目标达成。接下来：Release build/pixel→人工退出/安静实际前后→保留或撤回A→恢复临时源→最终Debug/Release及差异分类/自动任务memory，禁止commit。

追加交接（用户新要求：后续不再手动测试）：冷历史手动采样已正常退出并保存draw3-command-baseline.json，78 nonLaser contacts全部确认且零drop，仅日志11次hot_preimage、未记录cold/Redo，不能据操作反馈伪称覆盖。操作可能与前段构建时间交叠，不用于正式性能对照。CU第四次仍无Inkeys可操作窗口；旧demo SendInput benchmark依赖不兼容CLI/数字键且超时强杀，禁套产品。既有history benchmark只有CPU、host-smoke隐藏单笔、hidden-test太宽，不冒充可见冷链验收。
必要最小前置：worker仅Main临时显式visible-auto场景，复用原Host mailbox/UndoRedo/成功Present确认，在新私有draw3-auto-before/after目录（只配置和DLL，无原AutoSave/Memory）正常可见产品跑3轮40笔冷撤回/重做，正常退出，非RTS真实笔尖。不解除原owned-ULW隐窗门、不改backend/default/数据政策，最终整段Main按字节恢复，仅pixel CLI可留。Release候选+Hardware/WARP pixel已exit0；A全bytes已存InkHistoryGpu-with-leaf-seed.bin，当前GPU源码回到pre-leaf-seed（fixture仍在）供自动改前build；worker独占Main，我不并改。待driver完成→Release改前/自动3轮→恢复A Release改后/自动3轮→判收益撤退或保留→恢复全部临时指标/driver→最终Debug+Release与review。

## 最终结果 2026-10-03 13:28 Asia/Shanghai（停止，不追加）

- N1/N2已完成并验证：9个Bar生产文件83增79删。UpdateRendering原3次读取→单snapshot传StateUpdate(snapshot)/ThicknessDisplayUpdate(GetPenWidth(snapshot))；删SelectPenTool/SelectLaserTool先行style两调用；Shape hover复用Scene指针入口并保留门控/曲线/时长/禁用/异常语义；geometry和rounded父mask复用既有恒白刷，同maskDC/统一epoch释放。修粗细门控注释；既有SVG机制及Whiteboard/Freeze/PPT所有权保留，不据此认证全部UI3。
- U1已完成：UI3正常源码WARP+ULW、zoom2/light3；Draw3独立Adreno X1-85 Hardware FL11_1、正常主DComp/FLIP_SEQUENTIAL，辅助Selection ULW单算，无初始化回退。2880x1920/60Hz及画质/动画/配置保持。
- U2/U3已调查未达性能目标：宽UI场景基线回调/GetDC7.948/5.771ms，径向support8.517/6.144，geometry create占0.34%，切片17.34→14.74不证明实益，已撤回。53与23摘要窗口曝光不同，不将max减少当尾改善。WPR policy0xc5585011阻断CPU/GPU更细归因；使用后偶发慢未复现，GetDC内部执行/同步尚未拆开。旧容量/sincos/A8/fusedcopy未重试。
- D1代表采样完成、精确终态有限：真实首笔/连续/橡皮/Laser/历史共6969帧5357成功Present，nonLaser83确认、Laser19按旧排除规则Unpresented，不能称全coverage。Physical mean0.706/P95 1.232ms；idle首笔1.677ms仅软件代理。后续完全自动，私有可见产品3轮nonRTS共120Down/720Move/120Up回收、150成功Undo/90Redo；双方2048/2048与2018/2018成功Present，0drop/failed，2Clear+Exit各20笔保存成功。逐点等待不代表自然吞吐/笔尖延迟。Host两参数JSON未导出精确terminal proof；依据逐笔Up锁定收尾+新Present/history/save，不拿Down confirmed冒充终态。
- D2已调查无可信收益：首visible冷leaf直接Raster候选，Hardware/WARP各五场景32项4tile Add/Retain/L2逐字节等价。可见同驱动3轮30cold恢复mean2.140→2.316ms，对应frame2.538→2.805/P95 3.781→5.292/max5.427→9.946ms，round frame mean before2.837/2.270/2.506、after3.547/2.547/2.320，方向混杂；双方cold无>16.67ms工作帧，A完整撤回。不归因不相关Redo变化。第二方向Compose本无重复clear，RasterItem/identity clear必要，不实施。旧满512 allocator局部64.58→1.33us保留、不外推整体。
- R1/V1已完成：最终trellis-check无剩余生产finding；Draw3/Main/Diagnostics/RenderLoop与初始bytes相同，所有临时驱动/指标/计时和仅候选新fixture撤回，原测试无删禁放宽。自动前两次因错误内容门版本判定exit7、正常保存私有数据；修驱动后R3通过，不是生产回归。默认Debug链接器曾LNK1000内部崩溃，命令行LinkIncremental=false完整Debug ARM64及标准Release ARM64均exit0，未改工具链/工程。最终旧bar-eraser CLI0，zoom1/1.5各48帧9437184字节逐字节同基线；diffcheck/BOM/CRLF通过，无遗留GUI进程。
- 差异分类：生产9Bar；测试源码0（新临时fixture移除，旧测试保留）；本记录及real-paths研究；数据/冻结exe/撤回patch仅Build忽略目录。原.gitignore/旧untracked保留。HTTP/智绘教exe/PPT/UInk/依赖/更新/default不变。Release Build/ARM64/Release/Inkeys.exe，43036160bytes，SHA256 293eb4e8fc639a7859ddfacad7cb33e7555822fadcaf7767c687f31962d0aba8。
- 具体未达/阻塞：无显著UI3/Draw3性能收益；UI3偶发慢未复现、WPR权限阻断CPU/GPU拆分；当前JSON无精确terminal phase proof；Canary未找到，仅当前基线比较；其它架构/Win7未测。Win7 SP1+KB2670838专项ULW按用户延期。后面各类不计全部完成，task维持in_progress，不commit/push/archive/发布/新automation，报告后停止。
