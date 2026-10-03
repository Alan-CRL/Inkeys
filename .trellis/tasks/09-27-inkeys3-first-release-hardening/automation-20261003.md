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
