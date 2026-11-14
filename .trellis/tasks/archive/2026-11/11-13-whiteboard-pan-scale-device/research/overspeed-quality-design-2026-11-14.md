# Research: 超速保留历史的双区间速度支持界

- Query: 在不以惯性输出cap判坏输入的前提下，给出最小、可执行的孤立末点防暴增规则，并量化真实快速动作、到达簇、低速量化及续甩代价。
- Scope: internal；只读当前CanvasNavigation与两份已落盘轨迹，离线Python算例，不构建、不运行产品/GUI、不操作Git，不改产品/测试/spec。
- Date: 2026-11-14（Asia/Shanghai）。

## Findings

### 1. 收敛结果：双区间 + 既有空间噪声预算 + A

Main最终选择双区间空间预算加第5节A规则，不选择B。JSON的chosenVariant=withA、selectedField=selectedWithA；每个结果的selectedWithA才是最终候选输出。旧selected字段仅保留无A双区间版本作比较，不能混为最终值；selectedWithAB保留未采用B的反例。该标签收尾未重跑或改动数值算法。保留最近50ms原始尾窗线性拟合raw，不增加新曲线、绝对输入速度阈值或统一加速度上限。

1. 仍逐raw Move处理几何、真实反向、gap、来源/epoch和终态。独立证据从最新真实点向前选择相隔至少2ms的实际点；可靠释放仍需至少三个独立点、20ms。没有给Up补Move或伪造硬件时刻。
2. 取独立证据的最新三点A/B/C，C为最新真实末点。A→B不含C，B→C为最新一段；两个相邻区间只共享B，不是多个区间共同引用同一个可疑末点C。
3. `u = rawFit / |rawFit|`。对每个区间P→Q，计算以下沿释放方向的上界：

```text
dt = (Q.qpc - P.qpc) / frequency
tick = 1 / frequency
d = dot(Q.position - P.position, u)
floatError = float_epsilon * (hypot(P.position) + hypot(Q.position))
noiseBudget = context.reversalDistance
upper(P,Q) = (max(0,d) + noiseBudget + floatError) / (dt - tick)
qualityUpper = min(upper(A,B), upper(B,C))
```

`noiseBudget`复用已有运动域空间噪声/反向确认预算，ScreenPenPhysical为.15mm、TouchPhysical为.25mm；fallback使用各自DIP预算。它是明确的工程容忍政策，不等于已测出的硬件量化误差，也不是一个新速度cap。floatError和tick另外处理可计算的数值误差。

若频率非法、锚点不足、dt≤tick、结果非有限，标为支持不足，保守不输出释放速度/不继承旧boost，不把无效分母变成无限上界。零投影区间允许空间预算，否则低速整数坐标会被当作停笔；微小负投影按现有未达反向阈值的噪声处理，达到真实反向阈值的历史已由raw路径切断。

4. 在age gate之前计算：

```text
baseline = min(rawSpeed, existingRecentTailLimit, profileOutputCap)
supported = min(baseline, qualityUpper)
qualityLimited = supported < baseline
selected = supported * existingAgeGate
```

纯输出cap不算质量坏，单纯cap不清history/boost。只有支持界实际收紧基准释放时，取消本次旧residual继承，避免booster把被压低的速度重新抬高；保持当前已支持速度作为新曲线起点，后续新手势仍可正常续甩。比较应使用相同的cap后、age前速度域；不能拿age后的速度和未衰减的支持界混比。

5. 最终仍保留booster后的向量模长cap、方向保持、真实反向清旧方向、stale和取消/异常终态闭合。Snapshot供预取/blur的有界速度与held实际1:1几何保持原边界。

实现成本是沿≤64点固定历史选锚及常数两区间计算，O(n)时间/固定scratch，不需要全部点对、排序或新长期状态机。对正向单调样本的离线脚本保持当前raw LSQ求和和尾段约束；它不是生产C++等价性的完整证明。

### 2. 为什么不采用两个更简单候选

| 候选 | 发现的缺口 |
| --- | --- |
| 仅A→B上界 + 旧RecentSpeedLimit | 能挡最新一个异常C，但位置阶跃后重复D时，A→B可能已包含阶跃；旧RecentSpeedLimit跳过零位移最新段，会让旧jump再次支持高速。需同时考虑最新B→C。 |
| 双段上界只加float/tick误差 | 正常低速整数坐标有零/小位移区间，易错误压零。448组量化算例中，93个实际≥20mm/s的结果降到屏幕笔15mm/s门以下。不能把这些全说成真实停笔。 |

反例 `(ms,mm)=(0,0),(10,3),(20,6),(30,9),(40,102),(50,102),Up51`：raw约2314.29，旧RecentSpeedLimit约4650，仅前区间候选会得到1800。最终双区间空间预算版本约15.0026mm/s，禁旧boost；严格float-only双段约.00243。选择空间预算版是为了保留正常低速量化动作，其代价见下文。

### 3. 离线数值结果

配套[脚本](overspeed-quality-design-2026-11-14.py)及[JSON](overspeed-quality-design-2026-11-14.json)保存输入、多个候选输出与最终选择。共同QPC假设10MHz；这些是离线Python计算，不是native构建、生产probe或真实硬件结果。

- 110组基础/阶跃/加减速样本：报点30/60/120/240/480/960Hz，稳态20/300/1700/1800/1801/2500/5000/12000mm/s；合法持续超cap均可释放并封顶，未因超速清历史。
- 正常300mm/s历史，仅最后点增加1–300mm：最终受前区间支持限制，不会直接顶到1800；10ms支持跨度的两个复审例(末点42或102mm)均≤315.1mm/s。空间预算允许的增幅是noiseBudget/dt，不能宣称严格保持300不变。
- 448组量化：120/240/480/960Hz × 速度10/15/20/30/60/100/300 × 量化步长.05/.1/.15/.25mm × 四个相位。最终空间预算版没有新增“旧末段约束下≥15而新规则降到<15”的例子。三个粗量化且实际20mm/s的例子旧RecentSpeedLimit本来已略低于15，最终仍拒绝；不能说448组全部成功起滑。
- 真实持续加速在已有两个支持区间时可被认可；一步骤加速只发生在最后一个区间时会延后认可，不永久锁旧低速。30Hz下最后20ms的快动作仍可能只有一个新区间，保守输出约304.5，而非立即认可2500；这是数据不足与抗末点脉冲的明确代价。
- 急减速仍由旧末段约束和新最新区间上界共同限制，不以早先快速度续滑。

真实日志尾段对照如下，均为mm/s，不含新硬件实测：

| 来源/key | raw LSQ | 旧末段约束后（不再清窗） | 最终双段空间支持后 |
| --- | ---: | ---: | ---: |
| 第一份76 | 1029.25 | 851.81 | 840.88 |
| 第一份77 | 1190.80 | 600.13 | 600.13 |
| 第一份78 | 879.28 | 596.60 | 596.60 |
| 第一份79 | 769.42 | 511.18 | 511.18 |
| 第一份80 | 509.16 | 163.04 | 163.04 |
| 第二份75 | 676.53 | 676.53 | 326.61 |
| 第二份76 | 647.34 | 256.76 | 256.76 |
| 第二份77 | 840.96 | 282.04 | 282.04 |

第一份五条为已导出的完整轨迹。第二份75早期缺input ordinal2120，但该缺失在最终50ms之外，不影响末窗；第二份76缺2177，raw回算与生产日志不能宣称全程精确等价，表中是可见轨迹的近似尾窗；第二份77输入完整。第一份截断key75没有补造Down，未放进“八条尾段”计数。

### 4. 必须透明的代价与未保证事项

- **空间预算会允许少量零位移尾流。** 上述10ms plateau约15mm/s只针对该跨度；约2ms时预算上界可达Pen约75mm/s、Touch约125mm/s，加float/tick宽容，不能推广成“所有静止突跳最多15”。Touch的预算级尾流仍可能肉眼可见，需真人测试。完全静止历史后单个异常末点也可能产生预算级释放；保证是不会凭该点直接顶格或绕过质量限制叠旧boost，不是所有异常都零输出。
- 两个连续、几何一致的相关异常点可能形成支持；没有独立硬件时钟不能将它们与真快动作完全区分。本规则也不声称找回真实采样时刻。
- 多个同向时间区间支持仍可受callback到达簇影响；第二份75由raw676降到326是有意保守，而不是测出了用户真实手速326。
- 预算以内的小幅尖峰不会被标为质量坏，可参与原来正常续甩；不能声称任意大小的末点噪声都禁止boost。超支持界实际压低时才禁旧boost。
- 所有加减速、曲线和真实手感仍需生产C++回归、完整构建以及用户真机确认。不能用本报告的离线PASS替代它们。

### 5. 补充A/B压力测：静止单跳与平台尾流

Main在收敛时追加两项小规则对照，代码尚未由本研究实现。结果已写同一脚本/JSON的`selectedWithA`、`selectedWithAB`及`plateauProbes`，都只是离线计算。

- **A：至少两个实际正向区间。** 在整个可靠独立锚点尾窗中，沿raw方向投影位移大于可计算float误差的相邻区间至少两个，才允许非零释放；不要求每个区间超过reversalDistance。448量化对照无新增≥15→<15误拒，八份真实尾段全部不变（有5–7个正向区间）。全静止历史+单个末点阶跃，以及该阶跃后重复同值平台，在2.1ms/10ms均只有一个正向区间，因此都降为0。这项不会解决此前确实有多段真实运动的“运动→jump→平台”。
- **B：零投影限制段直接降零。** 当支持界确实比min(raw,oldTail,cap)更严，且实际限制区间的沿raw位移仅在float误差内为零时，将预算级释放降为0。它能将300mm/s运动→jump→hold在2.1ms的约71.445mm/s、10ms的约15.004mm/s降为0，八份真实尾段也不变；但会新误拒6个正常粗量化例，不能宣称无代价。

B的新增误拒例：20mm/s@120Hz、.25mm量化，raw20.357/原候选18.000→0；30@120Hz/.25mm，28.571/18→0；60@240Hz/.25mm，59.077/36→0；100@480/960Hz/.25mm，约100/72→0。这说明“raw超过noise/dt就把最新零位移判为停”仍可伤正常量化尾段。研究建议A可纳入，B不宜无条件加入；如果Main选择B，必须公开这6/448的对照代价并安排实测，不能仅报告真实日志仍通过。

| 输入 | 区间ms | 双段预算输出 | 加A | 加A+B |
| --- | ---: | ---: | ---: | ---: |
| 全静止→单jump | 2.1 | 71.432 | 0 | 0 |
| 全静止→jump→hold | 2.1 | 71.443 | 0 | 0 |
| 300运动→单jump | 2.1 | 371.448 | 371.448 | 371.448 |
| 300运动→jump→hold | 2.1 | 71.445 | 71.445 | 0 |
| 全静止→单jump | 10 | 15.000 | 0 | 0 |
| 全静止→jump→hold | 10 | 15.003 | 0 | 0 |
| 300运动→单jump | 10 | 315.005 | 315.005 | 315.005 |
| 300运动→jump→hold | 10 | 15.004 | 15.004 | 0 |

本次不再扩展第三种启发式。Main最终决定纳入A，不纳入B：B新增6/448个正常量化误拒，与用户减少偶发拒绝的目标冲突。最终规则只有一个实际运动区间时保守为0；此前已有运动再jump后平台仍可能有预算级尾流，不能称完美停笔识别。110基础、448量化、8真实尾段的既有计算均保留；chosenVariant/selectedField明确最终输出选择，不用新一轮研究重算掩盖候选差异。

## Files Found / Code Patterns

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.CanvasNavigation.cpp:149`：当前独立≥2ms真实锚点选择，使用固定容量索引scratch。
- 同文件`:274–382`：当前profile cap与短dt清窗耦合、raw反向及50ms尾窗；后续实现负责解除cap清窗。
- 同文件`:421–444`：旧RecentSpeedLimit会跳过小位移区间，是plateau反例需要补最新段支持的原因。
- 同文件`:448–500`：当前可靠资格与raw LSQ/方向一致性；保留作raw事实。
- 同文件`CanvasPanDynamics::Step`与`CanvasPanFlingBooster::GetVelocityForFlingStart`：释放选速、年龄、当前残余与最终向量cap；质量限制不能在这些出口被旧残余绕过。
- 本任务 `overspeed-history-plan-2026-11-14.md` / `research/overspeed-history-plan-review-2026-11-14.md`：批准方向与需要收敛的技术边界。
- `research/pen-release-trace-2026-11-14.json` / `pen-intermittent-release-investigation-2026-11-14.json`：两份真实日志导出及缺点说明。

行号对应本研究开始读到的源码，后续implement改动会移动；函数名/概念比固定行号更稳定。

## External References

无。本题基于当前本地源码、已有原始日志与离线数字分析，未新增外部API事实或硬件支持承诺。

## Related Specs

- `.trellis/spec/native-desktop/canvas-pan.md`：可靠释放、逐raw反向、单位、旧连续甩动、输出cap与诊断合同。
- `.trellis/spec/native-desktop/input-and-ink.md`：真实输入历史、终态和原绘制mailbox边界。
- `.trellis/spec/native-desktop/cpp-conventions.md`：固定热路径成本、既有线程/模块及最小修改。

## Caveats / Not Found

没有运行native程序、GUI、构建或Git，没有修改产品、测试、spec。Python只覆盖本报告列出的观测形状和数值界；真实Touch本机故障仍缺其Down/Move/Up日志。空间预算不是硬件测量真值，短窗口静止尾流和持续异常观测歧义须在实现/验证记录中保留。
