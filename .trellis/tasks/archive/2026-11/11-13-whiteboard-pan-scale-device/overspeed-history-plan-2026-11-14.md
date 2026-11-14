# 超速保留历史与异常暴增防护：后续修改点

日期：2026-11-14。用户已明确批准开始；质量规则与反例已收敛，实施、独立检查及完整自动验证通过，待用户真机测试。继续现有in_progress任务，不另建任务，不commit/push/归档。本方案以[第二轮调查](research/intermittent-release-investigation-summary-2026-11-14.md)和[契约复审](research/intermittent-timing-contract-review-2026-11-14.md)为依据。

## 需求与本轮解释

用户明确：速度很快也应该延续历史；首要目标是避免原来的速度突然异常暴增。用户对按住时是否限速尚未想清楚，倾向不限。本方案建议继续既有按住1:1跟手，不添加人工速度瓶颈；抬手后的惯性仍保留现有profile安全上限，1800mm/s只是屏幕笔当前初值，不是人的物理最高速度，也不在本次先调整数值。

可靠超速运动应保留真实位置、时间、方向和续甩资格，在输出端封顶；不能仅因高于输出cap清空历史或boost。并非所有超过1800的输入目前都会被拒绝：旧分支只在相邻短dt进入，故相同真实速度会因报点率/到达节奏不同而被限速或清零。

## 明确修改点与文件边界

| 项目 | 文件/入口 | 目标变化 |
| --- | --- | --- |
| 1. 输出cap不再判坏输入 | Draw3.CanvasNavigation.cpp/.cppm：Tracker Configure/AddSample及私有成员 | 去掉distance>velocityLimit*dt导致的破坏性清窗及其最近2ms救援分支；Tracker不再拿输出cap作输入时间门限。超速本身不返回TimingRejected，不触发Dynamics对boost的重置。不能删除context/booster的输出cap。 |
| 2. 独立真实时间证据 | Tracker Reason/Estimate/RecentSpeedLimit | 保留有界50ms原始尾窗、真实最新末点及逐raw反向；独立≥2ms时间锚点用于释放证据，仍至少3点/20ms。将ScreenPen规则扩展到直接Touch physical/fallback；不把2ms改4/8ms作为修复，也不为Up补Move/硬件时间。 |
| 3. 单点尖峰与真实快动作分开 | Tracker估速/末段支持检查 | 多个实际时间区间应支持释放速度；孤立极短末点不能把低速拟合直接推到cap或制造新boost。保留末段减速约束和真实反向切断。具体稳健支持规则须用下方反例定量收敛，本方案不虚构一个新速度阈值代替1800。只完成第1项不能宣称异常暴增已修复。 |
| 4. 只在合适出口限幅 | Dynamics Step/Snapshot、FlingBooster GetVelocityForFlingStart | 保留释放选速及boost相加后的向量模长cap；保持方向，不逐轴裁剪。续甩只投影当前剩余速度，不累加历史初速。Snapshot给预取/模糊使用的估计速度仍可有界；它不改变按住时实际contentDelta的1:1几何。 |
| 5. 原因能追到具体事件 | DrawingController及CanvasPanFrameOutput必要小量诊断字段 | 区分输入时间非法、独立证据不足、反向/过期/取消、正常超速被输出限幅。关键记录包含key/profile/unit、raw/selected/boosted、锚点时间/跨度、点数、限制原因和实际Stop触发者；仅既有诊断gate开启且关键事件时输出。 |
| 6. 先补反例，再验证实现 | draw3_canvas_pan_tests.cpp；必要的现有生产probe | 以两份真实日志、共同10MHz时钟的多报点率合成轨迹、Touch单位/回退、多次续甩及异常末段建立红绿对照，再做完整ARM64 solution和无窗口验证。 |

独立审查见[计划复审](research/overspeed-history-plan-review-2026-11-14.md)。移除cap清窗后，应收口仅由旧guard写入的History::timingRejected及其恢复死分支；公开TimingRejected仍用于非法终态，不能整体删除。正常限速不能计作timingReset。

第3项的具体数值/判据是实现前需要完成的技术收敛，不是等待用户替我们选择一个毫秒阈值。不采用统一加速度限幅或固定慢启动来压制所有合法快甩；若确需改变这种手感行为，须单独提出依据。没有真实packet时钟时，不能承诺区分一切速度异常与交付抖动；必须给出有界降级和诊断。

## 保留与有意变化

- 保留正常真实Move、按住1:1、真实Down立即停旧动画、每条raw反向、stale Up、至少3独立点/20ms、取消/异常终态闭合、输出/来源/epoch切换和最终向量cap。
- 允许合法输入快于惯性输出cap，输出有限且保留方向。常规Mouse/外置板profile和曲线不调；移除共用cap清窗因果后，旧“极短段超过输出cap必须拒绝”的Indirect/Touch断言会有意改变，不能承诺所有旧异常场景逐字节不变。
- 其它已确认的输入非法（非有限值、重复/倒退时间、取消/溢出等）及真实长gap按对应独立规则处理；不能把“历史延续”理解成跨反向/跨来源复活旧速度。
- 本阶段不改后端/PTP、EDID读取、笔速橡皮、设置或惯性曲线参数；真实packet时间能力保留后续研究。

## 必须通过的验收

| 场景 | 预期 |
| --- | --- |
| 共同10MHz时钟，30/60/120/240/480/960Hz，同向匀速位于cap下方/等于cap/1801及2500mm/s | 有足够真实证据时均允许可靠释放；高于cap只限输出，不因报点率变0；有限且方向正确 |
| 新日志19ms空档→五点3.8ms到达→Up；前份五条完整真实轨迹 | 不因簇内最近2ms速度超过输出cap毁掉历史；释放后确实推进coast |
| TouchPhysical/TouchFallback两种单位的成对/多点簇、同帧/逐帧消费 | 同一合同闭环，正常跟手/释放；不能用Pen PASS冒充实际Touch硬件验收 |
| 正常低速历史后孤立末点尖峰、真实持续快速加速、突然减速 | 单点异常不制造顶格释放；多区间支持的真实快动作能限幅续滑；主动减速不继承旧快速度 |
| 同簇反向、新方向尾段不足、重复/逆QPC、全段证据不足、stale/取消/overflow | 不复活旧方向/旧初速；异常终态可靠闭合，无永久held |
| 连续同向/反向快甩、多个输入来源、普通DIP基线 | 无错误抵消，无残余无界累加；最终boost后cap仍生效。预期变化只发生在本次有意纠正的质量规则 |

实际Touch反馈仍缺对应接触生命周期，需要补取class1 Down→Move→Up及随后release/帧；该缺证不妨碍先补已知条件风险的合成回归，但不能宣布其实际根因已确认。

## 执行次序

1. 把本次真实失败和报点率断崖、孤立尖峰等先写成明确失败/对照用例。
2. 在真实数据与对照轨迹上确定独立证据/尖峰规则；输出cap和质量规则职责分开。
3. 实施第1–5项最小源码变化，独立检查；保留原编码/CRLF与中文关键注释。
4. 原生ARM64完整InkeysRepo.sln Debug|ARM64、适用生产专项及完整无窗口套件；先按规则规范Path环境，不为工具链环境故障改源码。
5. 报告已通过的自动属性与未做的真机手感，继续现有任务；不自动commit/push/归档。

## 已实现的质量规则（native验证通过，待真机）

依据[数值研究](research/overspeed-quality-design-2026-11-14.md)，保留raw尾窗LSQ；直接ScreenPen/Touch采用≥2ms真实锚点，Indirect的raw点序列保持。所有profile的质量支持使用真实独立锚点尾窗。

1. 沿raw拟合方向，尾窗中至少两个相邻锚点区间具有超过可计算float误差的正向真实位移，才允许非零支持。只发生在最新一个区间的运动保守不释放；不得用多个共享同一末点的长跨段充数。
2. 最新三锚点为A/B/C。对AB和BC分别计算沿raw方向的上界：`upper=(max(0,projectionDelta)+context.reversalDistance+floatError)/(actualDt−oneQpcTick)`；分母无效或非有限时保守不足。取两个上界较小者。现有reversalDistance是工程空间噪声预算，不声称已测得设备量化。
3. age衰减前先取`baseline=min(rawSpeed,existingTailLimit,profileOutputCap)`，再取`min(baseline,qualityUpper)`。仅质量支持实际压低可输出速度时禁止本次旧residual boost，保留历史；仅profilecap限幅不影响资格。仍保留booster之后最终向量cap。Snapshot采用相同质量约束，但不裁剪held实际位移。
4. 不采用“限制区间位移近零就直接降零”的B变体：448个量化算例中会新增6个正常慢移误拒。上述至少两正向区间的A变体无此新增误拒，并能拒绝全静止历史+单次跳点/其后重复位置。

明确代价：持续真实加速需多个区间支持后才认可；尤其低报告率仅一个新快区间可能延后。已有运动后再跳点/平台仍可能保留噪声预算内尾流，上限随噪声预算/实际区间时间变化：10ms时Pen约15mm/s，最短约2ms时Pen约75、Touch约125mm/s，不能统称不可见微小尾流或完美停笔。最终cap与禁止质量受限时继承旧boost保证有界，不保证识别一切相关异常。后续真人需专测低速轻放、快停与持续加速。

研究数据是110基础、448量化及8真实尾窗的离线算例，不等于产品C++或本机硬件PASS；本轮已完成新增回归、原生构建和独立审查，结果见validation.md；真实设备手感仍待交接验证。
