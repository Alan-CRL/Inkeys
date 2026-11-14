# Touch释放回归修复方案：时钟语义、短拖资格与手势交接

2026-11-14。用户要求具体修复位置、各输入设备目标行为、回归根因及必要小重构/API报告。用户已明确批准并完成本轮实现、独立检查及自动验证；当前待新版本真机验收，不启动GUI、不commit/push/归档。既有任务继续in_progress。

## 1. 回归归属与证据

| 现象 | 已确认代码来源 | 结论 |
| --- | --- | --- |
| Touch约50ms平台后选速减半、禁续甩 | 当前未提交Touch修复将Step/Snapshot的LastSampleQpc改为LastMotionQpc，仍用原30–70ms门，并新增Touch age>30时booster.Reset | 明确近期引入的语义变化：原始观测新鲜度与空间未进展时长混用。需要修正；但完整端到端版本优劣仍须同轨迹native A/B，不能假称已运行bisect |
| 已拖9/19mm却few-samples/short-span | 4b4edb91已加入三点/20ms硬门；4706ec0e及c2f21bd0延续，当前仍在Reason/ReleaseSupport中使用 | 较早的保守资格设计缺口，非最近一轮才出现。7b213d90旧估速器允许至少两点且无此20ms门，须回放确认具体首次回归；不能恢复旧不安全估速器整个实现 |
| 多指最后一指数据不足 | 7b213d90已在最后Up按sample.key个人估速；当前仍如此 | 原有架构边界；本次未证实其它指本已合格却丢失，不能误报为已证实的速度缓存丢失 |
| Up前约50ms坐标平台 | RTS已交付重复位置；Up→consume中位约.087ms；未见应用收到Up后固定等待 | 上游交付语义/物理停手尚未区分；不是已确认系统API故障 |

历史源码核对：7b213d90的CanvasPanVelocityTracker::Estimate第273行允许m>=2；4b4edb91的Reason第471–477行要求三点/20ms；c2f21bd0第993–1003行对LastSampleQpc做年龄衰减。当前第1128–1147行改为LastMotionQpc并对Touch年龄禁boost。当前行号可能随下一次修改移动，以函数名和提交为准。

必须先将key4/25正常交接、26折损、64单指硬零、47/48双指硬零、31/187真实慢尾与前两轮平台/尖峰作为同一回放集，对比已保存版本和当前版本，输出selected/boosted/距离/停止原因。隔离导出历史纯核并复用现有工具链/测试入口，不checkout/reset当前工作区；无实际native回放前不宣布所有症状已确认为最近回归。

## 2. 最小重构范围

范围限CanvasNavigation既有模块，不重写渲染器/输入服务，也不恢复已删除曲线。

- **证据层**：现有逐指raw/progress历史继续保留。增加有界Touch手势组证据：同一接触组/来源/单位/epoch内的真实ScrollUpdate位移、方向、相关时间和仍有效的个人候选。拓扑增减指只重基，不制造位移/样本；不得把未更新指latest伪造为新测量。
- **决策层**：新增纯值`TouchReleaseDecision`（名称为拟议），集中输入新鲜度、位置平台、明确停止、速度置信度、组证据及旧残余资格；输出mode、velocitySeed、response参数来源、boost策略和原因。Snapshot只做呈现估计，不修改释放候选。
- **执行层**：现有FlingController/唯一摩擦曲线消费一次决策。Measured可按规则接力；ResidualOnly和DistanceBounded是已决定的最终seed，不能再叠旧残余，也不能冒充实测高速触发大屏快甩档。

这解决Step/Reason/ReleaseSupport/booster分别作出局部判断、叠加后突然归零的问题。所有状态仍由现有render owner持有，固定容量、无新增线程/per-Move heap。

## 3. 具体修复位置

| 文件/函数（当前行号） | 修改 |
| --- | --- |
| Draw3.CanvasNavigation.cpp:1128 Step终态，1145 Touch年龄禁boost；868 Snapshot | 将raw freshness、lastProgress age、terminal plateau和stop evidence分别交给纯决策；撤销单凭motionAge>30机械折半/清boost的因果。明确停手仍停止，不能无条件借回旧速。held继续1:1、不在Up前擅自滑行 |
| 同文件:512 Reason/528 Estimate/464 ReleaseSupport；cppm:36时间常量 | Touch采用分级资格。标准充分证据仍原拟合；短但方向一致、独立区间且位移显著超噪声的轨迹可走短证据分支，不一律卡20ms。只有极短两个点不能用其到达dt算出巨大手速 |
| 同文件:945 Step，645 GestureDetector::Up | 收集/评估同组真实运动，而不是仅以最后指决定整组资格；保留每指证据来源，排除拓扑重基、相反运动和已失效候选。Up不当速度点 |
| 同文件:708 ObserveGestureEvent / FlingStart应用处 | Touch终态平台与接力期限由同一个Decision裁决，避免去掉age禁boost后仍撞50ms滚动截止。真实反向、tap、长停、取消、来源/epoch改变仍清候选；非Touch原行为保持 |
| Draw3.CanvasNavigation.cppm | 显式证据/决策类型与诊断枚举，区分Measured/ShortSupported/ResidualOnly/DistanceBounded/Stop，限制新增接口在该模块 |
| Draw3.DrawingController.cpp:16591 release-quality及既有生产probe | 打印选择分支、指/组证据来源、raw-age/motion-age/platform、selected/boosted、停止原因。区分未评估与评估后0；不只打印few-samples。gate关闭无额外采样/格式化 |
| InkeysHeadlessTests/draw3_canvas_pan_tests.cpp | 固化历史A/B、短单指/多指与连续甩动对照，检查速度交接和实际距离，不只检查Ready |
| Draw3.RealtimeStylus.cpp:1338 StylusUp、1451 Packets、2801 packet-description初始化 | 先做能力与时序诊断：实际属性GUID/metrics、可选timer/serial支持、packetCount和原始值。只有证实可信才接入真实时间；不立刻换producer或猜造时间 |

## 4. Touch释放行为方案

### 4.1 正常甩动与末端平台

保留最近被多段真实运动支持的候选速度。raw仍新鲜、未发生反向/明确制动/长期停住时，不再仅因约50ms位置平台把它减半。位置平台单独参与停止置信度，不等同原始输入陈旧。key26要以最后可信估计约678mm/s作为交接参照，不把未经支持的最后段约1286当硬件真速；正常key4/25也必须保持。

短平台和真正短暂停手在当前数据上可能不可区分。方案取Touch漫游优先，但保留明确停住约0.1秒的无惯性验收。平台识别/停止确认的最终规则须在标注动作对照上确定，不能只换一个更大毫秒常量。不得在RTS仍报告held时自动开始惯性或倒回曲线起点补位移；Up前观测平台不能凭释放参数消除。

首轮建议采用以下可执行状态分工，再用对照收敛临界行为：

| 证据状态 | Touch释放动作 |
| --- | --- |
| raw观测新鲜、已有受支持运动、短终端平台、没有确认反向/制动 | 保留最后受支持候选，不仅因motionAge约50ms再乘.5；仅保原raw新鲜度保护 |
| 多个真实进展区间显示减速 | 按这些运动证据降低候选，不用“重复坐标持续时间”冒充减速曲线 |
| 连续同位观察确认停住达到现有70ms退出边界 | Stop，清候选/预算/接力；0.1秒明确停住测试必须无惯性 |
| raw确实陈旧/断报、取消、域或来源变化 | 原保护退出，不以平台容忍复活 |
| 新Down | 立即停止旧曲线；仅保存可验证的当前残余候选，按新动作方向/证据决定是否允许续接 |

该建议有明确产品代价：真实停指不足70ms也可能保留较明显续滑；它选择Touch漫游的一致性，不能声称完美识别用户意图。平台期接力只能保留此前合格的同源方向候选至同一退出边界，不能用重复0位移无限刷新资格；50ms滚动期限与终态平台必须由同一Decision处理，不能只删age Reset而遗漏另一处超时清零。

### 4.2 短拖资格与保守降级

建议按以下顺序作决策，而非够20ms/归零二选一：

1. **充分速度证据**：原可靠向量拟合/支持、最后真实减速与最终cap，正常接力。
2. **可靠短证据**：至少多个独立实际区间，方向及位移相对于噪声可证明；允许短于原20ms。独立性/方向/信噪门用反例收敛，不能直接全局把20改10。
3. **只有方向、合法同向旧残余**：已越slop产生实际scroll、未停住/取消、同来源/度量/epoch且接力资格有效时，可只续当前残余正投影，不加未知新速度、不恢复历史初速。反向旧残余绝不能用。
4. **没有可靠速度和可用残余，但有明确实际短拖**：建议Touch专用DistanceBounded退化。沿已确认的新运动腿方向，额外尾距不超过该腿已产生的画布位移，并受profilecap可达距离约束；用现有基础摩擦D(v)单调逆解seed，不触发快甩增强、不boost。它是明确的距离策略，不声称测得手速。纯tap/未越slop/非法数据不启用。该规则须在实现前量化通过方向、跳点、反向及停止对照。
5. **无有效滚动意图、明确停止/取消/失效**：0，停止旧运动。

key64旧残余约(217.45,-77.98)mm/s，新方向投影约-229.4mm/s，因此第3项不适用；不能说“恢复旧残余就修好”。第4项可让它在新方向最多保守续约9mm，避免拿11mm/.32ms造出36m/s。若不采用这种明确的低置信产品降级，又拿不到更好的时间证据，必须承认此类数据无法保证可靠非零新惯性。

多指组证据与跨新Down旧fling residual是不同数据，不可互相冒充。任何已定最终seed不能再进入普通boost加法，否则会重复叠加。

距离预算必须是已应用的位移，不能把kernel尚未被视口限位处理的requested delta当成实际位移。建议在DrawingController现有ApplyToCanvas边界做最小确认反馈，携带group/运动腿/epoch/metric标签，仅累积可归属的已应用净位移；混组同帧或标签不匹配不借用。保守版本只用此前已确认帧的预算，不预支同帧尚未呈现的Move；因此同帧Down/Move/Up若没有可确认预算，仍可拒绝距离降级。ClearAxis/限位、反向新腿、Stop、取消和换域同步清相应预算。不得为了修短拖重复应用位移或产生第二条曲线。

## 5. 各输入设备目标

| 输入 | 修复完成后的目标 |
| --- | --- |
| 单指触摸 | 正常快甩速度自然交接；短但明确的滑动按置信度给出受限续滑，不再机械硬零；tap/明确停手仍停 |
| 多指触摸 | 以同组真实运动与有效指证据结束手势，最后一指抬起不成为唯一资格来源；增减指不跳变，真实反向不继承旧方向 |
| 屏幕笔 | 保留本机已正常的精细短尾与1:1；不套Touch平台宽容/短拖降级 |
| 笔尾 | 保持已确认Down后的1.5mm起步门槛与1:1，无新增压力阈值；惯性开关当前关闭，保持用户设置，开启后仍按笔设备关系 |
| 外置数位板 | 保留Indirect DIP手感；走Pen入口服从Pen开关，模拟Mouse入口服从Mouse开关，不擅自改识别政策 |
| 鼠标 | 按住1:1；当前Mouse惯性默认关闭，开启时保持Indirect策略 |
| 触控板 | 双指导航继续延期/quarantine；被系统当Mouse的按压拖动仍按Mouse策略，不将本任务当触控板已实现 |

可信EDID/mm与DPI回退继续现有尺度分工，Touch两种单位采用同样证据合同，不复制物理尺寸或橡皮增益。

## 6. 系统API调查结论与条件分支

已确认应用使用IStylusSyncPlugin；Up到消费中位约.087ms，不支持应用收到Up后固定等待50ms。关键短数据reported=1，没有本回调内被丢掉的多packet；当前日志没有更早可确认Touch PointerUp，不能随便以另个通道伪造RTS终态。

官方[PacketProperty GUIDs](https://learn.microsoft.com/en-us/windows/win32/tablet/packetproperty-guids)定义TIMER_TICK为packet生成时间、SERIAL_NUMBER为packet标识。当前没有接入这些时间属性；设备实际支持、单位、分辨率、回卷及与QPC映射都尚未证实。只做可回退能力探测，无法验证则明确保留arrival时间并降低置信度，不能均分批内时间。若确认当前API确实缺少所需终态/时间能力，再单独报告Pointer等后端方案与共存成本；不在此次小重构中静默替换RTS或解除PTP隔离。

## 7. 执行顺序与验收

1. 导出历史/已验证快照，做同轨迹实际核回放，确认clock语义回归与早期资格回归；不得清理当前未提交实现。
2. 先定义纯Decision与红例，再实现Touch-only证据/交接小重构。依次处理key26交接、48短多指、64低置信反向短拖，保留4/25正常和31/187真实慢尾；距离降级单独标记可观测。
3. 增加单指/2–5指增减、低/高报点率、停手、反向、单位切换、单点突跳、连续甩动与旧残余去重；验证实际位移、首帧、交接比及最终cap，不把Up当样本、不倒回动画、不只追Ready率。
4. 维持Pen/Tail/Indirect明确黄金/原路径，完整ARM64 Solution与专项/全套无窗口。测试允许观察正常短暂停顿的取舍，不能以自写规则PASS替代用户手感。
5. 提供独立测试产物及短动作标注方案，再由用户复测Touch与笔；任务保持in_progress，未获新commit/push/归档授权。

本方案仍需在实施阶段量化短证据信噪判据与Touch停止确认，且DistanceBounded为建议的新降级策略，不冒称已经验证成熟。完成标准是当前真实反例和安全对照共同通过，而非把任何触摸都强行做成滑动。

独立复审：[方案检查](research/touch-release-repair-plan-review-2026-11-14.md)；历史源码证据：[版本审计](research/touch-release-history-audit-2026-11-14.json)。两者均未修改产品或执行native跨版本回放。

## 本轮实现完成状态

已完成集中TouchDecision、短强证据、组候选、ResidualOnly/DistanceBounded及真实Apply确认，正式RTS时钟/属性请求未改。额外审查修复慢Scroll影子残余/旧预算资格、slop内呈现速度、新腿时钟和同帧诊断串用。最终完整构建与五专项通过；green4完整套件通过后，仅对两项Touch保护和一个probe条件作最终专项复验，未冒称最终字节重跑完整长套件。精确证据见[验证](validation.md)和[交付步骤](device-test-touch-release-2026-11-14.md)。原文“建议/需量化”保留为方案沿革，已实现合同以当前spec和本轮review为准；手感仍待用户验证。
