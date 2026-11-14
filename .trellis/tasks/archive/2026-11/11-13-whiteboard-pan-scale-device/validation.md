# 白板惯性尺寸与设备适配：验证记录

> 最新授权（2026-11-14）：用户要求修复移动模式默认系统光标，并结合日志处理屏幕笔无惯性；继续现有任务，允许最小源码修改及无窗口验证，不commit/push、不归档。详见 [本轮反馈与边界](feedback-2026-11-14.md)。

状态：源码与自动验证完成；任务保留in_progress，待真人设备体验验收。未启动GUI、未commit/push、未归档。

## 2026-11-14 Touch释放回归小重构（自动通过，待真机）

已按用户批准[修复计划](touch-release-repair-plan-2026-11-14.md)实施，本轮实际5文件：CanvasNavigation.cpp/.cppm、DrawingController.cpp、RealtimeStylus.cpp、canvas测试。历史8个原修改中的其它4文件保持；新增RTS只有诊断。当前Touch纯值Decision统一证据/平台/停止/资格/最终seed，Controller确认真实viewport应用位移，分级低置信降级不重复boost。非Touch数值路径保留。

[历史native](research/touch-release-native-history-2026-11-14.md)六版真实编译/回放确认64/47–48从4b起硬零；旧7b虽滑但64约956mm且26错向，不能整体回滚。历史probe归一frame>=最新sample的薄适配首跑错误已纠正并另存，未隐藏；legacy-DIP/物理profile与无前序boost/缺日志限制明确。repair-final纯核模拟真实ApplyChecked ACK后：26约677.65mm/s/158.13mm；64预算9.03096781mm、实际9.03096233mm；47/48 ShortSupported约1305.61mm/s/335.43mm。该导出对应green4前后快照，后续收口版本不冒称hash相同；最终相同真实数据又由交付headless-pan实际验证。

所有正式构建为ARM64原生MSBuild、完整InkeysRepo.sln Debug|ARM64，独立OutDir PanTouchReleaseValidation，1800秒预算；同PowerShell规范Path并禁node reuse。原始argv、源/产物/日志hash、退出码在Build/Logs/pan-touch-release-2026-11-14/。

| 阶段 | 结果 |
| --- | --- |
| build-red / pan-red | build exit0；pan exit1，8条新交接/短支持/多指红例真实失败 |
| green1 | build0；pan1，52断言失败：48个cap-only诊断误报，4个旧个人资格与新组行为冲突 |
| green2 | build/pan/control均0；确认合法catchup约420.8、stale末指正向group500，而非旧反向复活 |
| green3 | build/pan均0；新腿意向时钟、Short邻接同向与更精确断言通过 |
| green4 + 五专项 + full | 全部0；完整--no-window 444.156s、末PASS animation correctness |
| green5 | build/pan/control均0；独立native反证后修慢Scroll shadow残余/预算旁路、slop内呈现0；feel32失败为旧稳态断言误计首次越slop启动 |
| green6 delivery | 完整build0（14.328s）；最终pan/control/feel/penwidth/diag全0（1.047/1.984/5.375/.234/1.953s） |

独立检查还修同帧release metadata串用，区分local support未评估与组候选。慢Scroll native反证为20mm/s新动作错误恢复500；收口同步撤shadow残余和低置信预算，平台0delta不触发，可靠慢候选保留。pan-feel只将稳定速度断言限定actual scroll前后都成立，产品起步/slop不变、其它断言保留。

完整套件复用边界：最终与green4仅3文件差，两个Touch核心保护及对应测试、一个DrawingController probe-only条件；接口/RTS/其它模块未变。最终五专项覆盖新增路径，未重跑无关WARP/设置长套件，不声称最终字节全套运行。精确差分check-followups.diff及check-feel-scroll-boundary.diff保留。最终SourceBefore/After和当前9源逐项匹配。

最终pan-feel对上一TouchTerminal：646行变化、615个maxdv变化（478降/137升），不是都舍入；常速96变化仅估计瞬态。48个release差最大.027954DIP/s，仅stagger组均值/float差，尾距差≤.007243DIP，无旧非零→零；max_delta/stop/residual/lockederror/nonfinite/failures不变。jitter-rest32场景呈现maxdv/实际delta/release均0。不能用该矩阵代替新实机交接验收。

[最终机器证据](research/touch-release-validation-results-2026-11-14.json)与[独立检查](research/touch-release-implementation-review-2026-11-14.md)均通过；[真机步骤](device-test-touch-release-2026-11-14.md)已准备。新目录仅复制前一测试目录的两份设置，原设置hash未改，未复制白板文档。API正式请求/时钟未改，仅actual GUID/metrics和终态raw诊断；未确认硬件timer能力或系统缺陷。

以下为先前阶段历史，不能替代本轮交付验收。

## 最新TouchTerminal真机复测：仍有快速定住，验收未通过

用户反馈改善但单指/多指仍会快速定住或极短尾滑；不是碰撞。新日志确认约50ms末平台后的motion-age衰减/禁boost形成速度折损，以及两个明显拖动后因个体证据门槛归零的实例。详见[交接复测汇总](research/touch-handoff-summary-2026-11-14.md)。本轮只读，源码前后hash相同，未新增构建/产品运行/GUI/commit。以下自动PASS不代表Touch真实体验验收通过，任务继续in_progress。

## 2026-11-14 Touch末端平台修复（自动通过，待新版本真机）

本轮按[Touch执行合同](touch-terminal-fix-2026-11-14.md)实现raw观察与真实位置进展分离，保留raw断报/反向/终态、motion长停失效，按Up-motion age衰减并防旧boost绕过；显式stationary呈现速度0不影响释放候选。Pen/PenTail/Indirect保留原路径和计算次序，笔尾压力/slop/开关不改。本轮只有4源码/测试文件相对前版变化，其余4个前轮文件逐字节保留。

原始命令/退出码/源码及日志hash保存在 `Build/Logs/pan-touch-terminal-2026-11-14/`；全部为同一ARM64原生MSBuild、完整InkeysRepo.sln Debug|ARM64、命令行独立OutDir PanTouchTerminalValidation，未修改工程/SDK/工具链或关闭用户程序。

| 记录 | exit | 秒 | 说明 |
| --- | ---: | ---: | --- |
| build-red | 0 | 19.828 | 只加Touch时效/尾距反例、产品未改 |
| headless-pan-red | 1 | 1.453 | 预期32条失败，实际证明旧重复点刷新运动age/短尾问题 |
| build-green-1 | 0 | 24.344 | 完整Solution，11项既有warning/0error |
| headless-pan-green-1 | 0 | 1.610 | 新真实/合成回归及旧pan专项 |
| control-fence-green-1 | 0 | 1.297 | 真实生产导航/控制栅栏 |
| pan-feel-green-1 | 0 | 3.594 | 2240场景语义通过 |
| pen-width-green-1 | 0 | .281 | 笔/光标回归 |
| diagnostic-output-green-1 | 0 | 1.922 | 原gate/sink无窗口回归 |
| headless-all-green-1 | 0 | 332.266 | 完整--no-window，末行PASS animation correctness |

所有green构建和运行的8源hash相同，全套后未改源码。4条代表真实trace74/108/107/109共67事件已与固定28MB快照逐点核对，Down→Up完整、无key内导航缺口，按逐事件/整批消费断言raw/selected/age/尾距（日志打印和float传播容差明确）。74/108尾距由约1.1mm变为约42.63/80.36mm；107/109正常长尾保持预期。其余120/121离线分析不冒充全native真实回放；缺失/多指数据仍有限定。

旧pan-feel与上一版2241行比较仅7条jitter-rest的max_dv估计变化，实际release/滑距/stop/held位移等指标逐字节相同；7条并非都减小，非等比scale240fps一条85.726→98.717，仍无实际位移/起滑。非Touch兼容另外由源码路径/算术次序不变及既有Pen黄金/生产probe佐证，不能只凭TouchFallback为主的pan-feel证明。

明确体验边界：30–70ms的运动年龄衰减沿用旧数值，真实1000mm/s运动后停40ms仍可能约725mm/s/171mm尾滑，70ms才归零；不能称立即刹车。60ms平台+C可能不足3运动锚而保守0。新版本需要用户确认快甩、短暂停顿、明确停住约0.1秒及Pen兼容。

最终机器证据：[Touch结果](research/touch-terminal-validation-results-2026-11-14.json)，审查：[独立检查](research/touch-terminal-implementation-review-2026-11-14.md)。测试产物/配置及手动动作见[新版本交接](device-test-touch-terminal-2026-11-14.md)。本轮未启动GUI、未commit/push/归档，任务继续in_progress。

## 最新真机反馈：Pen通过，Touch/笔尾需跟进

用户已完成手动测试。本轮完整console分析见[复测汇总](research/device-feedback-summary-2026-11-14.md)：屏幕笔本机正常；Touch多数Ready但部分1–3mm短尾，统一末段支持规则对Touch平台过保守的代价已实测暴露；笔尾Down后slop外1:1未见异常，接触识别/1.5mm起步门槛需区分。前后Pen因果未确诊。本轮源码前后hash相同，只做离线分析，未新增构建/GUI/commit。任务保持in_progress；以下自动PASS不代表Touch真机验收通过。

## 2026-11-14 超速历史修复（自动验证通过，待真机）

用户已批准开始后续计划，并要求需要真机测试时通知。本轮新增修改仅在CanvasNavigation.cpp/.cppm、DrawingController.cpp和canvas测试四文件；保留前轮八文件其余内容。算法为[质量规则定稿](overspeed-history-plan-2026-11-14.md)的双邻接区间空间预算+A，公开invalid-time与unsupported-tail/输出限幅分开，held仍1:1。新增真实非零旧coast接力回归，防旧residual绕过质量限制，纯cap-only继续正常接力。

原始命令/退出码/源码与日志hash位于 `Build/Logs/pan-overspeed-history-2026-11-14/`。源码冻结验证：

| 检查 | exit | 秒 | 说明 |
| --- | ---: | ---: | --- |
| build-red | 0 | 6.704 | 仅新增反例，产品仍旧逻辑 |
| headless-pan-red | 1 | 1.625 | 预期24条失败，实际复现新key75和高Hz超cap归零 |
| build-green-1 | 0 | 23.047 | 第一版核心+回归 |
| headless-pan-green-1 | 0 | 1.531 | 红例转绿 |
| build-green-2 | 1 | 15.250 | 首个有效错误LNK1104，默认Inkeys.exe输出无法打开，C++编译无源码错误 |
| build-green-3-isolated | 0 | 18.641 | 同Solution/配置/工具链，仅命令行OutDir隔离，0errors/0warnings |
| headless-pan-final | 0 | 1.688 | 含非零旧residual追加回归 |
| control-fence-final | 0 | 1.531 | 真实生产入环/控制栅栏 |
| pan-feel-final | 0 | 3.718 | 2240场景语义通过 |
| pen-width-final | 0 | .218 | 前轮光标/设备接线回归 |
| diagnostic-output-final | 0 | 1.969 | gate/sink/无HWND回归 |

最终产物目录 `Build/ARM64/Debug/PanOverspeedValidation/`。没有关闭用户进程，没有为了输出访问问题改源码/工程配置。默认目录的旧EXE不冒充最终隔离产物；最终测试均在隔离版本执行。完整headless-all-final已exit0（382.750s），末行PASS animation correctness。最终完整构建、五专项和全套源码hash完全一致，未在全套后更改源码。

旧pan-feel整份输出不再逐字节相同：2241行含2240场景和汇总，602行变化，其中525行max_dv变化、154行release速度变化；没有任何原非零release变成0，held几何误差/最大单帧位移/非有限/失败字段均未变化。常速64条变化中56条仅max_dv，另8条释放/滑距只有微小舍入；非加速场景release最大差0.001343DIP/s、glide小于0.000976DIP。实质释放变化是8条accelerate-lift：30fps/frame四条约2749.998→2655.016，符合AB段+空间预算的支持上界；240fps/subframe四条约2808.594→2812.500，符合独立锚拟合窗相位。估计max_dv并非全部下降：525条变化中480减小、32条topology增加（最大426.70DIP/s），属资格/支持估计瞬态，需真机校准，不能称全常规手感无变化。详细行差保存在pan-feel-diff.json，不能再宣称旧golden字节兼容。

工程容差/能力边界：孤立末点+静止历史不足两个正向区间时不启动滑行；先前已运动后出现相关jump/平台仍可能有noiseBudget/dt范围内的尾流，最短区间Touch可约125mm/s，不能统称微不可见。B变体会新增量化误拒而未采纳，真机需重点验证低速轻放、快停、连续快甩。完整算法不宣称恢复真实硬件采样时间。

最终机器证据见[本轮结果](research/overspeed-history-validation-results-2026-11-14.json)，独立结论见[审查记录](research/overspeed-history-implementation-review-2026-11-14.md)。本轮仅4个源码/测试文件相对前轮基线变化；其余前轮光标/窗口改动逐字节保留。手动测试见[交接步骤](device-test-overspeed-2026-11-14.md)。独立测试目录仅复制当前main.json与opt/deploy.json用于保持偏好和日志gate，原文件hash不变，未复制白板文档。

最终产物SHA256：
- `Build/ARM64/Debug/PanOverspeedValidation/Inkeys.exe`：`2f371c243d1ec57bcf69dad413ec9008bb077e38df15ebfbe4ec422a50291ae8`。
- `Build/ARM64/Debug/PanOverspeedValidation/InkeysHeadlessTests.exe`：`17a20bc67eb5123923f806833ece2fcacc03168cbb7525cb934b548110306d1c`。

以下研究/构建为前轮历史，不能替代本轮最终验证和真人反馈。

## 第二轮只读反馈调查（不代表已修复）

用户复测确认屏幕笔已有惯性，但仍会偶发拒绝；纯手指且笔远离时Touch似有同感。第二份截断日志的确定分支、Touch证据缺口、报点率反例及后续方向见[调查汇总](research/intermittent-release-investigation-summary-2026-11-14.md)。当前判据仍将输出cap兼作输入时间可靠性，最近2ms锚点可能留在较长到达簇内部；当前自动验证PASS不证明此剩余形状或所有设备已覆盖。

本轮只更新Trellis；八份源码前后SHA256完全相同，未构建、未运行产品/GUI、未commit/push/归档。离线Python算例不是新生产测试证据。下列先前构建/测试结果保留为历史实证，不改写为本轮已经修复。

## 2026-11-14 本轮反馈修复

当前实现针对移动模式默认系统光标和屏幕笔成簇到达导致的释放误拒绝，详见[本轮边界](feedback-2026-11-14.md)、[日志研究](research/pen-release-trace-2026-11-14.md)与[独立审查](research/pen-feedback-fix-review-2026-11-14.md)。8个产品/测试文件最小修改；曲线参数、cap、Touch与Indirect算法、配置、EDID和后端保持。本轮不提交、不推送、不归档。

五个完整手势的338条真实事件逐条/整批回放进入生产CanvasPanDynamics，检查非零释放并实际推进coast。截断key75没有补造Down。频率10MHz为日志delay/QPC推得的回放假设，不冒称硬件时间。共享光标策略和真实未附着Window的Touch终态抑制→新Pen接管→退出移动链均有覆盖。

原始命令、退出码、源/产物/日志哈希保存在 `Build/Logs/pan-pen-feedback-2026-11-14/`。正常默认目录完整 `InkeysRepo.sln Debug|ARM64` 已通过，五专项headless-pan/control-fence/pan-feel/pen-width/diagnostic-output均exit0，构建和每次运行的源码哈希不变。旧DIP矩阵708090字节与本轮基线逐字节相等（SHA256 c3c3e6975bac0b8998c98a291a72f71e823bf708b8a1e01701238df01e67d9df）。完整无窗口套件headless-all也已exit0（382.343s），末尾PASS animation correctness；源码全程冻结。之后仅将新测试局部变量injected改为injectedTablet（两个identifier token）；最终build-4-final与headless-pan-final均exit0。全部产品源码与canvas回归与完整套件版本逐字节一致，完整套件证据据此复用，不把最后pan专项冒充光标矩阵执行。

环境记录：build-1-default受限runner在两个cppm首行报C1902；相同源码/配置/工具链的正常主机build-2-default-host通过C++编译，但旧增量链接ILK触发LNK1000。确认产品和编译/链接进程未运行、源/备份绝对路径在workspace内、目标为普通单文件后，只备份本轮失败ILK至日志目录；build-3-default-host完整通过。没有改变SDK、工程或调试格式。该次75 warnings中1项为新测试变量遮蔽，独立检查已作上述仅变量名修正；最终增量构建仅余既有ContactRecord对齐C4324、0 error，不含C4456。


最终机器证据：[本轮结果](research/pen-feedback-validation-results-2026-11-14.json)。完整套件耗时382.343s，最终增量构建6.985s，最后pan专项1.547s，全部exit0。源码BOM/CRLF/UTF-8及diff检查通过；没有启动GUI或执行硬件视觉验收。本轮修改保留工作区，任务继续in_progress，未commit/push/归档。

默认最终产物SHA256：
- `Build/ARM64/Debug/Inkeys.exe`：`3464d88f79dd34232ea632f6428973d3fde091c21c31edebb18ef7011b388f31`。
- `Build/ARM64/Debug/InkeysHeadlessTests.exe`：`283ccbbe2154e9763653d9c3be2d4fc417a278e6ca87ee9a8ca112d87eff2306`。

以下为2026-11-13首版实现的历史验证，不代表本轮最终状态。

## 实现与证据

已接入可信直接Touch/屏幕Pen的毫米动作度量、无可信标尺的模式化DIP回退、五profile参数、唯一有序Down的Pending准入、逐事件canonical DIP出口、真实RTS Pen token及配对反馈。原可靠尾段、当前残余正投影、反向与Down中断保持。没有修改输入后端、橡皮、持久配置、触控板隔离、solution/vcxproj、依赖或SDK。

八个产品/测试文件与三份规范修改，保留原BOM和CRLF，尤其DrawingController原本UTF-8无BOM。独立审查见[implementation-review.md](research/implementation-review.md)，全部命令、退出码、源码/产物/日志哈希见[机器证据](research/implementation-validation-results.json)。完整原始日志在 `Build/Logs/pan-scale-device-2026-11-13/`。

## 开工基线

Git基线 `1b819d2df656f84cb9481b8e7aab1321a31c73ae`；起始仅本任务规划目录未跟踪，没有其它脏文件。原源码格式/哈希见baseline.result.json。

旧独立验证目录的Inkeys.exe已不存在，默认EXE是另一哈希。本轮保存该产物哈希并运行pan-feel/control-fence，两项exit0且输出与上次已验证基线逐字节一致；已知旧Headless的pan专项exit0。这些是开工观察，不冒充本轮新产物。

## 完整构建与环境恢复

全部用vswhere动态定位的ARM64原生MSBuild，`InkeysRepo.sln Debug|ARM64 /m:1 /nr:false`，同一PowerShell先规范Path/PATH并禁node reuse，超时1800秒。

| 构建 | exit | 秒 | 结果 |
| --- | ---: | ---: | --- |
| build-1 | 1 | 34.937 | 默认增量链接器LNK1000 |
| build-2 | 0 | 35.172 | 完整solution通过，产品源哈希冻结 |
| build-3 | 0 | 24.875 | 完整solution通过，产品源哈希冻结 |
| build-4 | 0 | 23.687 | 完整solution通过，产品源哈希冻结 |
| build-5-default | 0 | 15.875 | 完整solution通过，产品源哈希冻结 |

准备阶段曾修正运行器的多Git路径选择，并恢复Controller原本无BOM；当时未启动MSBuild，不能算源码编译失败。build-1的主程序C++编译通过，首个有意义错误是link.exe 14.44.35207在IncrBuildImage内部C0000005/LNK1000。没有修改代码/SDK/工程配置规避它。

build-2/3/4以独立OutDir完成全solution。最后确认默认EXE没有运行、源/目标均在workspace内，将本次失败生成的268435456字节 `Build/ARM64/Debug/Inkeys.ilk` 备份到日志目录，再按原配置重建默认输出。build-5-default成功，0errors/0warnings；没有递归删除缓存。先前编译中现有第三方/IdtMain转换、nodiscard与ContactRecord对齐警告未顺手修改。

## 审查与首轮专项修复

- 身份/来源选择与mailbox发布、清除原先分锁：改同一短事务和配对读取，UI/触觉/wake锁外，新增双publisher生产CPU回归。
- Pending毫米Down在Step前的EDID资格/密度变化：按真实Down source重验并Stop旧批；模式/无害更新和DIP到可信不半途升级。
- 新适配把重复/逆frame当失败而Stop：恢复Core Snapshot no-op，Controller在Drain前保留队列，下一有效帧完整准入/闭合；原128条失败断言完整保留。
- Stop死指旧断言仍要求Touch owner：改None/非held/非Active，仍检查物理HasContacts、BatchDead、位移不变和Up回收。
- Pen夹具raw身份错配可能空样本误通过：对齐真实raw IDs并验证实际发布，未放宽生产token资格。
- 异常终态即使排序在同key Down之前也可靠取消闭合，不遗留held或拒收屏障。

初轮新产物headless-pan/diagnostic/config通过，control-fence只有两次旧owner断言失败，pan-feel只有上述128次重复/逆帧失败；全部失败原始记录保留，没有只摘PASS。

## R3完整无窗口验证

| 记录名 | exit | 秒 |
| --- | ---: | ---: |
| headless-pan-r3 | 0 | 1.047 |
| control-fence-r3 | 0 | 1.312 |
| pan-feel-r3 | 0 | 2.891 |
| diagnostic-output-r3 | 0 | 1.859 |
| config-boundary-r3 | 0 | 2.140 |
| setting-general-r3 | 0 | 18.313 |
| headless-all-r3 | 0 | 325.265 |

全部运行于build-3的新产物，产品源码哈希全程一致。完整套件末尾PASS animation correctness。生产probe实际经过ContactInput→CanvasPanInput→Window，不以公式/末端helper替代真实接线。

## 最终收尾及默认产物复测

全量通过后仅修两处单位注释、source-conflict/source-not-direct-touch诊断reason及十个故障fixture的精确标签断言。其它六源文件与R3快照字节一致，resolver去除reason语句后的文本一致；无运动/profile/准入/估速/渲染逻辑变化。差分在 `closeout-vs-r3.diff`，R3完整源快照也保留。因此完整套件证据复用，最终收尾完整build-4并做对应专项；默认目录正常build-5后再次验证实际默认产物。

| 最终/默认记录名 | exit | 秒 |
| --- | ---: | ---: |
| headless-pan-final | 0 | 1.500 |
| control-fence-final | 0 | 1.500 |
| pan-feel-final | 0 | 2.922 |
| diagnostic-output-final | 0 | 2.110 |
| config-boundary-final | 0 | 1.922 |
| headless-pan-default | 0 | 1.203 |
| control-fence-default | 0 | 1.188 |
| pan-feel-default | 0 | 2.813 |

所有最终运行产品源哈希一致。原DIP基线实际2240场景、整份708090字节与开工和上次已验证输出完全一致，SHA256 `c3c3e6975bac0b8998c98a291a72f71e823bf708b8a1e01701238df01e67d9df`。程序旧汇总rows=2112未包含部分追加场景，本任务未顺手改计数格式。新物理/大屏/屏幕笔profile另有生产和纯内核回归；旧DIP等价不代表它们的最佳手感已验证。

## 最终正常产物

默认目录已正常构建，并通过pan/control-fence/feel专项：

- `Build/ARM64/Debug/Inkeys.exe`：SHA256 `7716d646564cfa73145dfbb2c35b5418e3a48681dc47755f15eda9670abdf12a`。
- `Build/ARM64/Debug/InkeysHeadlessTests.exe`：SHA256 `23f0338c31c1628107b6c2176914e11e506218270a6daefbfaf07168851d940f`。

独立PanScaleValidationFinal及R3产物、失败ILK备份保留在Build中供追踪。没有关闭或操作用户应用。

## 真人验收与能力边界

- 未真实测试Surface Pro 11触摸、教室大屏、屏幕笔/笔尾、外置板及无EDID两模式手感。约28×19cm是用户描述，不是本轮读取的硬件标定。初值可按真实动作继续校准，不声明最优。
- EDID仍是现有业务策略级可信，基础整数厘米、override/黑边等边界保留；DPI回退保证单位闭环与有界模式响应，不承诺真实毫米一致。
- Pointer cursor/range不证明同一次RTS Down；缺token/歧义/容量不足时保守降低反馈纠正，不丢core输入，不凭旧RTSack全局清Pointer-local latch。
- Touchpad继续quarantine，未实现新后端/缩放/绘制模式漫游或显示服务增强。未做GUI/光学/硬件延迟测量。

代码与自动验证完成，任务继续in_progress；commit/push/归档未授权，真人体验验收待后续记录。

## 最终用户验收（2026-11-14）

用户使用PanTouchReleaseValidation完成测试并反馈“基本没有问题”，批准收尾、提交及推送。当前9个源码/测试文件与最终五专项验证hash一致，无新增代码，复用已记录自动验证。该验收限本轮用户设备和动作，不声称全部硬件已验证；触控板及未验证API能力仍延期。
