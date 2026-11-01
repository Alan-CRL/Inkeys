# 实施计划：PPT 动态 GIF 页翻页卡顿修复

> 原修复已提交为 `97e61925`，结果见 [research/validation.md](research/validation.md)。本轮延续三项审阅修复，记录见 [research/review-fixes-validation.md](research/review-fixes-validation.md)；本轮不 commit/push，原实机像素未验项保留。

## 1. 诊断与回归基线

- [x] 在 managed 现有 opt-in 诊断中增加 fast/current、full topology 和 owner command 的聚合计时/计数，默认关闭。
- [x] 扩展 FakeAccessor/FakeGraph 的调用计数，记录 `Slides.Item`、当前 `View.Slide` 和 command/refresh 顺序，不创建平行 COM 框架。
- [x] 保留指定 PPTX 的原始 QPC 基线：冷鼠标最大 descriptor 5.584544 s、input_open 5.657881 s；冷键盘分别 3.963921/4.035565 s。样本仅作对照，不当分位数。
- [x] 增加普通翻页与固定周期维护的 fast-path 测试：同 binding、同页数、当前 ID 匹配时，`Slides.Item` 全量读取次数为 0。
- [x] 增加完整路径触发矩阵：无缓存、换 binding、页数变化、当前 ID 不匹配、EndScreen、显式失效、busy/普通失败、迟到 generation。
- [x] 验证完整成功才替换缓存，失败或部分读取不污染最后一次稳定拓扑；所有 temporary acquisition 恰好释放一次。

## 2. 双层 descriptor 与命令优先级

- [x] 在 `PresentationDescriptorReader` 内抽取共享的当前页读取，保留 `ReadShow` 完整拓扑行为和公开结果 schema。
- [x] 在 `PptCOMServer` 内实现纯值拓扑快照、快速路径准入和按需完整校验；周期时间到期只执行快速校验。
- [x] 处理非当前页重排合同：访问到不一致页时，在完整校验成功前不得发布该页为可信 `StableSlideIds` 目标。
- [x] 调整 `PptComService` 每轮顺序，让有界 owner 命令先于非必要 descriptor 维护；保留队列、超时、会话戳和不可取消语义。
- [x] 增加 owner 顺序测试和反复 fast/full 交错的 acquisition-release ledger 测试。
- [x] 运行 managed 测试，确认原有 `StableSlideIds`、`PageIndexFallback`、`TransientBusy`、EndScreen 和 WPS/PowerPoint fake 差异不回归。

## 3. Native 等待反馈

- [x] 在 `IdtPlug-in.cpp` 成功入队、去重、完成、异常、退场和 shutdown 的对应位置维护带 session/epoch 的导航等待纯值。
- [x] 通过 `Inkeys.UI.Ppt` 快照把状态交给四个 PageControl 客户端；不在业务锁、渲染锁或窗口锁内跨层回调。
- [x] 在 Next/Previous 按钮绘制不确定等待标记，保留真实旧页码、布局和点击语义；不伪造百分比或页事务完成。
- [x] 将等待视觉与 `PageStateMode.Pending` 正确组合：COM 命令等待结束不等于 Draw3 ready，页面 ready 后仍须原有 ack。
- [x] 在现有 headless/PageControl 测试中覆盖重复点击、命令完成未翻页、换场、旧 epoch、退出和 shutdown 清除。

## 4. 构建与自动验证

- [x] 使用本机原生 ARM64 host `MSBuild.exe`，完整构建 `InkeysRepo.sln`，配置 `Debug|ARM64`，超时至少 5 分钟；不单独用 native 项目代替 Solution。
- [x] 独立构建 `PptCOM/PptCOM.csproj Release|AnyCPU` 与 `PptCOM.Tests/PptCOM.Tests.csproj Release|AnyCPU`，运行 `PptCOM.Tests.exe`。
- [x] 按现有产物交接合同同步同次 `PptCOM.dll`/TLB 到 native 使用位置，再核对实际运行 EXE 包含新 managed 实现；接口不变也不得以旧 DLL 做验证。
- [x] 运行 `InkeysHeadlessTests.exe --no-window` 及适用的 PPT/Draw3 hidden 测试。
- [x] 若等待反馈新增文本，运行 `Scripts/i18n.ps1 sync` 后 `Scripts/i18n.ps1 check`；纯图形等待标记不新增文案。
- [x] 运行 `git diff --check`，检查只包含任务需要的源码、测试和明确的产物交接，不提交 Build/Cache 临时输出。

## 5. 真实 Office 验收

- [x] 使用同一指定 PPTX、同一 ARM64 测试产物，开启 `INKEYS_PPT_TIMING=1` 和新增 managed 聚合诊断。
- [x] 冷启动与暖态分别执行至少 5 次第 3→4、第 4→5，覆盖鼠标与键盘；报告 descriptor、input_open、fast/full 次数和最大/中位/长尾，不只保留最好样本。
- [ ] 在 descriptor pending 和 owner outstanding 时连续点击，确认等待标记可见、重复命令仍受控、后续不会永久无响应。重复命令与状态生命周期已由生产/实机路径覆盖；fast 后等待过短，三点标记的真实长阻塞像素截图仍待补验。
- [x] 确认等待期间旧画布不接受新笔迹，旧 contact 不跨页；只有 Draw3 Present + PageControl ack 后才开放新页。
- [x] 验证 SlideID 重排/增删页或对应生产夹具：快速路径不掩盖当前页不匹配，完整校验后拓扑与墨迹槽正确。
- [x] 验证普通无 GIF 页、结束页、Office busy/超时和退出，不出现等待标记残留或命令饿死。
- [x] WPS、其他 Office 版本和 Release 未实际运行时明确标为 `NOT VERIFIED`，不从 fake 测试扩展支持声明。

## Review Gates and Rollback

- 阶段 1 未证明 fast/full 调用计数和释放账本可靠，不进入阶段 2。
- 阶段 2 若出现拓扑身份回归，优先回退双层缓存，保留诊断；不得通过删除 native 身份门修复测试。
- 阶段 3 可独立回退视觉反馈，不影响阶段 2 性能修复。
- 真实 Office 若仍有长尾，依据分段诊断区分单个 Office 调用与剩余全量扫描；不得宣称剩余阻塞已由缓存方案消除。
- 实施后的最终检查通过前，不标记完成、不归档、不提交或推送。

## 6. 2026-10-31 审阅修复（本轮）

- [x] 修复文稿身份新鲜度与跨场拓扑复用，补 managed 行为/释放回归。
- [x] 修复导航完成先到竞态，补乱序/跨场回归。
- [x] 修复白板失败补呈现跨工作区命令，补同场/过期回归。
- [x] managed 构建测试、DLL/TLB 交接、完整 Debug|ARM64 solution 与相关无窗口测试。
- [x] 独立质量审查、最小 diff/BOM/EOL、更新规范与本轮验证记录。
本轮用户未授权 commit/push；留存未提交修改和原实机未验边界。

## 2026-11-01 分段诊断实施

用户已批准设计并准备。- [x] managed/native分段诊断与对应回归。
- [x] 一键采集、离线分析与PrepareOnly/解析自测。
- [x] 独立review、managed构建测试、DLL/TLB交接、完整Debug|ARM64和无窗口验证。
- [x] 交接用户人工测试；AI不操控桌面、不commit/push。

诊断版准备验证见 [navigation-perf-validation-20261101.md](research/navigation-perf-validation-20261101.md)，人工操作见 [navigation-perf-handoff-20261101.md](research/navigation-perf-handoff-20261101.md)。

用户已完成分段采样，捕获1.356s Previous与1.144s Next，主要空档为managed邮箱等待ROT/observe等owner维护；详情见 [navigation-perf-results-20261101.md](research/navigation-perf-results-20261101.md)。调度修复尚未实施，原像素/Office退出验收保留。

## 2026-11-01 Observe精确定位执行

用户已要求准确定位并执行，dynamic/确定性COM释放为硬约束。先补既有调用子阶段计时与分析展示，人工采样后据证据收窄调度修复；不盲目跳过校验。方案和文件边界见 [observe-substage-plan-20261101.md](research/observe-substage-plan-20261101.md)。AI不操控GUI、不自动提交。

- [x] Observe既有读取/释放子阶段、16个开关等价场景、分析器子阶段树与旧日志兼容。
- [x] managed构建/测试、完整Debug|ARM64、DLL/TLB/内嵌产物、PrepareOnly与独立COM释放审查。
- [x] 用户细分版人工采样并据成员耗时收窄实施ROT安全让出；完整Observe长尾仍待后续解决。

本阶段结果见 [observe-substage-validation-20261101.md](research/observe-substage-validation-20261101.md)，人工步骤见 [observe-substage-handoff-20261101.md](research/observe-substage-handoff-20261101.md)。

## 2026-11-01 细分实测后的最小ROT修复

用户已批准执行。新数据表明Observe为多个慢COM查询累计；先按 [rot-yield-fix-plan-20261101.md](research/rot-yield-fix-plan-20261101.md) 实施健康binding下ROT安全取消/一次让出公平预算，保留原完整Observe与全部COM释放/墨迹准入，不同时重写观察逻辑。

- [x] ROT安全检查点Deferred、partial winner取消、一次让出公平预算与原动态/释放保持。
- [x] 实际scanner/系统边界/真实bindctx释放、资格/公平性/旧请求拒绝回归，独立审查、完整solution/DLL/TLB/PrepareOnly。
- [x] 用户修复版人工性能/退出采样；导航长尾未通过，原正常退出触发兜底，不能据已采样标修复验收通过。

结果见 [rot-yield-validation-20261101.md](research/rot-yield-validation-20261101.md)，操作见 [rot-yield-handoff-20261101.md](research/rot-yield-handoff-20261101.md)。

## 2026-11-01 正常退出超时定位

修复版用户正常退出触发0xE1430016，退出验收不通过。先按 [normal-exit-trace-plan-20261101.md](research/normal-exit-trace-plan-20261101.md) 包裹原清理阶段、保持行为和COM释放；AI不操控GUI、不修改退出策略，不把问题直接归为COM泄漏。

- [x] 用户确认正常退出失败；独立性能/退出证据分析，不将整体测试标通过。
- [x] native/managed默认关闭退出阶段、Arm-first/LastError/COM约束、完整solution与core/PPT定向/PrepareOnly/资源哈希验证。
- [x] 用户同路径正常退出采样：本轮code0、所有退出阶段闭合；旧故障未复现，尚不能定位或宣称修复。

准备验证见 [normal-exit-trace-validation-20261101.md](research/normal-exit-trace-validation-20261101.md)，操作见 [normal-exit-trace-handoff-20261101.md](research/normal-exit-trace-handoff-20261101.md)。

本轮数据与用户三点不可见反馈见 [normal-exit-trace-results-20261101.md](research/normal-exit-trace-results-20261101.md)。导航仍存在1.239秒邮箱排队与1.637秒总长尾，继续调查等待渲染链路和Observe现场proof/详细刷新分界；无新产品修改，不追加无依据退出修复。

## 2026-11-01 等待标记不可见的边界定位

最新长等待实机三点不可见，静态代码尚不足证明单一根因。按 [navigation-wait-trace-plan-20261101.md](research/navigation-wait-trace-plan-20261101.md) 只补Ppt发布→PageControl合并/配置/绘制/真实呈现的固定数值证据；默认关闭，渲染线程无I/O/分配/等待，停产后flush，不改PptCOM或UI/页码/ready行为。准备验证通过后交用户少量操作采样，不重复同版大矩阵。

- [x] 等待发布/合并/配置/实际点绘制/像素/原pending回执有限数值记录，真实停产后输出。
- [x] 24组合实际像素、明确启用recorder的BGRA等价、并发/overflow/TLS/LastError回归与独立审查。
- [x] 最终完整ARM64 solution、PPT定向/离屏、managed源/DLL/TLB/内嵌哈希、PrepareOnly/真实配置SHA检查。
- [ ] 用户新版短测，按接受→model→绘制→真实提交证据修实际消失边界；本轮不标已修复。

结果见 [navigation-wait-trace-validation-20261101.md](research/navigation-wait-trace-validation-20261101.md)，短测见 [navigation-wait-trace-handoff-20261101.md](research/navigation-wait-trace-handoff-20261101.md)。

## 2026-11-01 用户澄清后的页面准备提示修复

三点实际出现，但随命令Complete提前消失；后续raw/descriptor失配及Draw3/UI ACK阶段仍Pending。按 [page-preparation-wait-fix-plan-20261101.md](research/page-preparation-wait-fix-plan-20261101.md) 独立投影真实Pending为通用准备提示，原命令tracker/水位、COM和输入ready门均不改，不引入soft TTL或猜测完成。

- [x] 页面准备pagePreparing同快照投影、状态比较及PPT双向generic三点；原command/ready/COM不变。
- [x] Complete后Pending真实像素/新页同帧去点/BasicControl target0/trace启停等价与原水位、旧ACK回归。
- [x] 独立review、完整ARM64（新测试限定名修正后exit0）、page-control/PPT定向、离屏、managed/内嵌哈希与PrepareOnly配置不变。
- [x] 用户第3→4短测确认真正Pending阶段三点正常保留/清除，1744记录零掉样并有同attempt软件证据；性能长尾和旧退出未知仍单列。

结果见 [page-preparation-wait-fix-validation-20261101.md](research/page-preparation-wait-fix-validation-20261101.md)，操作见 [page-preparation-wait-fix-handoff-20261101.md](research/page-preparation-wait-fix-handoff-20261101.md)。

用户反馈与本轮数据见 [page-preparation-wait-manual-results-20261101.md](research/page-preparation-wait-manual-results-20261101.md)。等待反馈验收通过，不再重复该项测试；实际延迟未消除，任务保持in_progress。

## 2026-11-01 页码动画最小复现与取证

用户同意按 [page-number-animation-repro-plan-20261101.md](research/page-number-animation-repro-plan-20261101.md) 先复现短Status返回Numeric，并准备默认关闭的实际内容策略数值记录；不改变动画决策、不恢复文字替代三点、不改COM/ready，不操作GUI或自动提交。

- [x] 页码短Status机制真实离屏复现及PPT内容policy默认关闭数字采样，保持原动画策略。
- [x] 独立审查、完整ARM64/定向/真实跨module hook与采样BGRA等价、内嵌/配置SHA验证。
- [x] 163049已完成现场取证并定因，随后修复并获171213人工通过。

结果见 [page-number-animation-capture-validation-20261101.md](research/page-number-animation-capture-validation-20261101.md)。

## 2026-11-01 恢复进度与页码角标

用户已批准准备/恢复进行中使用三点，并指定左右翻页均在页码区域右上角一处显示。按 [page-badge-progress-fix-plan-20261101.md](research/page-badge-progress-fix-plan-20261101.md) 只改显示投影，保留真实故障/重试文字及控制恢复态；Pending/Recovering保持同场稳定显示数字，不能用目标页提前替代。原COM/业务currentPage/ACK/Host门不变，不自动提交。

- [x] Recovering视觉进度与控制态分离、稳定数字override与Page右上角单角标。
- [x] 独立check、完整ARM64、两组定向和真实离屏/旧ACK/缓存/最大位数/启停BGRA回归、哈希/PrepareOnly配置不变。
- [x] 用户短测确认无问题；171213样本433记录零掉样，16条Numeric→Numeric即时策略，实际每面3点/1branch，正常exit0。

结果见 [page-badge-progress-fix-validation-20261101.md](research/page-badge-progress-fix-validation-20261101.md)。

本轮人工验收见 [page-badge-progress-manual-results-20261101.md](research/page-badge-progress-manual-results-20261101.md)，视觉修复通过，性能及原未验边界仍保留。
