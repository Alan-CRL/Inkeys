# 白板惯性尺寸与输入设备适配：设计 v1

> 2026-11-14修正：用户日志证明屏幕笔到达簇被短dt保护反复清窗；本轮边界见[反馈计划](feedback-2026-11-14.md)，ScreenPen可靠性合同以当前[canvas-pan规范](../../spec/native-desktop/canvas-pan.md)为准。曲线参数和设备分工不变。

状态：in_progress，用户已在完整规划v1摘要后批准并要求开始实现（2026-11-13）。需求以 prd.md R1–R10 为准。首版数值是可验证的设计初值，未宣称经真机达到最佳；Surface Pro 11 仅是连续手势正确性的回归设备。

## 1. 保留的行为与设计边界

- 继续唯一 ContactNavigationHistory→CanvasPanInput→CanvasPanDynamics→画布视口链，不更换输入后端、估速算法或曲线族。
- 保留事件水位、QPC/phase 稳定次序、真实逐指尾段、多指拓扑重置、Up 可靠闭合、反向清旧残余、Down 停旧动画、当前残余正投影续甩和模长上限。
- 移动模式 hover 继续不刹停；活动 Touch 与后来 Pen 的互斥规则保留。笔尾只影响入口偏好，不改变它属于屏幕笔还是外置板的事实。
- 直接输入按住后的内容位移保持 1:1；起步仍径向扣除一次 slop。不额外乘“大屏初速增益”。
- 设备开关、默认值及 Touchpad quarantine 不变；不增加设置页、手动标尺 UI 或每机首次启动自动调参。
- 已授权按本方案实现及非交互验证；GUI、commit、push、归档仍未授权，不沿用上个已结束任务的提交授权。

## 2. 事实与设备解析

### 2.1 只复用事实

输入使用真实 Down 的 InputDeviceType、inverted、ContactSnapshot.source（kind、context/cursor、generation、mapped monitor/rect）。显示使用当前 Drawpad HWND 对应的 DisplayScale：DIP/px、业务 cm/px、physicalAvailable、logicalOutputKnown、monitor/bounds、像素宽高、旋转。paintDevice 的正式两模式继续由 Host 映射 LargeScreen/Laptop；不增加 Automatic UI。

在现有 WindowController 的 speedEraserConfigMutex_ 下提供一次性的 display+mode 只读快照，避免两个独立 getter 拼接；绘制 owner 获取后在锁外解析。它表示该锁截面的有效配置，不宣称 Display 与 RTS 已拥有同一硬件事务。Host 可沿现有 setter 发布，不为本功能重构全局显示服务。

不读取橡皮 Fixed/Speed、PenResponseChoice、Sensitivity、面积、ManualSurface、ResponseOverride、penBeta 或调试 force-unavailable 来改变惯性。优先把现有完整输入映射/物理轴校验提取为小型纯事实 helper，供现有橡皮和惯性共用；helper 不负责响应参数，橡皮全部分支与结果必须保持。若抽取会扩大改动，允许在现有 Draw3 文件内消费同一事实合同并以共有测试锁住规则，不复制 EDID 枚举。

### 2.2 物理资格与回退

物理资格要求：直接 Touch 或已可靠识别的 IntegratedPen；Display 业务尺寸可用；非 fallback 且 Single/Extended 映射可靠；窗口输出已知；真实 source.monitor 及 left/top/width/height 与实际输出完整一致；两轴 mm/px、DIP/px 和尺寸有限且正，保留现有宽范围 rho 校验。惯性另对 motionPerDipX/Y 各自要求有限且落在[.01,10] mm/DIP的宽安全范围，防几何均值掩盖异常单轴；此附加guard不改共享橡皮的既有结果。任何失败输出可诊断原因并回退 DIP，不从 Primary、单屏数量或产品模式猜来源。

| 接触与真实关系 | 可信尺度 | 无物理/映射失败 |
| --- | --- | --- |
| Touch，来源确认为该屏直接接触 | TouchPhysical / mm | TouchFallback / 系统 DIP，仍区分桌面/大屏 |
| IntegratedPen / PenTail，真实映射可靠 | ScreenPenPhysical / mm | 已知屏幕笔用 ScreenPenFallback / DIP |
| ExternalPen / PenTail、Unknown Pen | IndirectDip / DIP | 同左；不把显示器毫米当板面毫米 |
| MouseLeft | IndirectDip / DIP | 同左 |
| TouchPad | 不增加本任务准入 | 保持既有隔离，不转换成新 Touch 物理输入 |

真实 InputDeviceType=Touch 但 source 证据不足时，只允许 TouchFallback，不能声称已经确认该屏毫米。Unknown Pen 维持间接输入，未来识别补全只在新手势重新选择。

当前 Display 只解析基础 EDID 整数 cm、校验头/checksum及下限，尚未合并 EDID_OVERRIDE、详细时序/DisplayID 尺寸或图像黑边区域；这些是物理精度边界，不在本次扩大显示服务。保留每轴密度，不按常见长宽比硬裁或先取平均。新增解析遇到非有限/越界轴保守回退；不能把“通过现有业务策略”宣传成外部尺量认证。

## 3. 单位、状态与有序消费

### 3.1 内核运动域与对外系统 DIP

选择直接在 mm 或 DIP 运动域计算，不再为复制 Surface 旧数值引入标定锚点。定义数值型 CanvasPanMotionContext（名称可按现有风格调整）：unit、motionPerDipX/Y、空间门槛、限速、响应类型、尺寸权重/模式意图参数及兼容身份。默认构造为现有 IndirectDip，旧纯内核测试和间接路径保持原参数。

```text
公共输入：p_DIP_i = p_px_i * systemDipPerPixel_i
物理内核：p_motion_i = p_DIP_i * (mmPerPixel_i / systemDipPerPixel_i)
DIP 内核：p_motion_i = p_DIP_i
公共增量：delta_DIP_i = delta_motion_i / motionPerDip_i
画布增量：delta_px_i = delta_DIP_i / systemDipPerPixel_i
```

同一逆变换用于速度。CanvasPanFrameOutput 的 contentDelta/contentVelocity、VelocityDip、focus、span、逐指诊断和已有 raw/selected/boosted 释放字段继续是真实系统 DIP；额外物理诊断另列 unit 和 mm 数值，不用旧字段装毫米。span 必须逐指还原 DIP 后重算，不能用一个平均比例转换各向异性的标量 span。每个 ScrollUpdate 在 Dispatch 时按当时 context 转 DIP 后累加，不能整帧末尾只用最后 context 逆变换。

viewport 的正尺寸检查在对应运动域转换后执行；世界坐标、安全限位、页面存储、tile、预取及 blur 的单位保持。清轴仍是同一像素/运动轴；dot/norm/释放上限在当前运动域统一计算。

### 3.2 一帧内多来源

不能在 Register 的 publication 顺序或每帧开始只选择一个 profile：同帧可能含 Touch Up→Mouse Down/Move，反向 publication 次序也存在。集成审查确定需要把动态类别/尺度裁决集中到唯一有序Down，不只给旧前置裁决增加帧末拒收反馈。

- HandleSelectionDown保留白板移动模式、合法设备和native Running历史等前置静态资格。尚未消费的候选只Register为Pending，不按Pending做最终跨类拒收、永久笔抑制或SuppressLaterPendingContacts。
- Register仅关联该Down候选事实/数值context，不切换当前单位、不清未来才生成的旧boost；Pending不得计入OwnerClass、TouchPanHeld或有效导航Active。
- sample 带不可变数值 context 引用（帧内数组 index 或等价轻量句柄）；数组复用容量，内核选中时复制需要的数值，禁止保留悬空 span/指针。
- Dynamics沿现有稳定QPC/phase顺序，在Down分支唯一裁决类别/metric准入：没有accepted held时选择新context，否则只接纳同类别且兼容的来源。只有被接受的Down才改变拓扑/sourceKey；拒收不改变残余或有效持有。后续Move/Up用本批变换，拒收键直到真实终态不可凭Move或重复Down复活。最后Up形成的curve单独锁存本次friction参数。
- 新一批 Down 的来源、单位或有效 policy 不兼容时，先清旧 residual/曲线/历史，再按新 context 开始；保持 sourceKey 高 32 位的设备类别，不能为存哈希破坏热禁设备解码。
- 一批Touch后加入指沿首指度量。mm批次要求该附加来源可证的同输出精确映射；未知映射或已知冲突拒收。首指Unknown仍可独立DIP回退；DIP批次的后来可信指只要无输出冲突就继承DIP，不能半途升级。profile偏好变化不令兼容第二指改写阈值。
- Step按真实处理顺序回传admission decisions（key/Down QPC/accepted/reason及所需来源身份）。Controller据此更新Accepted或Suppressed/直到终态，再发布held/笔反馈；不重新计算赢家。decision scratch由caller在帧前预留，容量覆盖本批全部Down而非仅16个同时持有指；生产调用先预检容量，不足时走既有Stop/旧触点吞到终态，不能只应用部分决定。旧无context的纯DIP调用可省略决策缓冲，新生产适配调用须提供合法context和足额缓冲。
- 更早的Pen mailbox只能读已准入事实；合法pan-managed Pen持续contact不能每帧Stop自身。窗口按前帧Touch bool的即时反馈抑制只是provisional，不作为核心准入真值。核心Pen裁决通过真实contact身份/Down QPC/navigation epoch纠正对应反馈；旧确认不能清新一笔抑制，真正Touch期间拒收Pen仍锁到Up。RTS当前不因TouchPanActive丢真实Down，无需新输入后端；必要时小型传递现有cursor/source身份，不能靠相近时间猜同指。
- 不能逐样本重复调用旧 Step 来切 profile：frameQpc 单调门会丢同帧数据，或多次推进动画。保持一帧一次统一推进。

同QPC仍按Down<Move<Up/Cancelled，故Touch Up与Mouse Down同QPC是重叠，Mouse须被拒；只有严格晚于真实Up才接手。切context前flush旧Move并转为DIP，清惯性用不重置lastFrameQpc的routine，不在Step循环调用完整Stop。忽略/拒收接触只等终态，不可作为把两个已结束手势桥接起来的owner。

内部velocity_/tracker保持motion，Snapshot及ContentVelocity仅在对外边界转DIP。现有`velocity_=Snapshot(...).handVelocity`路径须改为取内部motion值，避免双重逆变换。VelocityTracker调试接口要显式Motion语义或提供canonical查询；releaseSpeed/tailLimit先在motion域判门，再生成DIP诊断。释放保留独立releaseSource/context快照，不能用本帧最后Down的sourceKey标前一次释放。

### 3.3 变化与锁存

有效签名比较输出身份/像素/bounds/方向、system DIP、当前使用的物理密度、来源真实映射与已解析参数；Display 与 RTS 的 generation 分别记录，不能比较两者数值相等。

签名中的响应部分是profile家族/版本、模式、固定空间门槛及w/G等策略输入，不包括每次释放按手速得到的a/tau/c。正常同源同向续甩可以因手速不同选择另一组本次曲线参数；不能因此把合法残余一律清掉。

- 真正输出、DPI、映射变化，或正在使用的物理依据失效：沿 Stop 栅栏清旧运动/残余，旧触点吞到终态，下一完整新 Down 解析。禁止用新标尺解释旧尾段。
- 单纯取得更完整身份/新物理资格而当前正在 DIP 批次：不在半途升级单位；下个完整手势选择新域，并清跨域旧残余。
- 仅模式/响应偏好变化：本批及其 curve 继续锁存；下次第一 Down 应用新值并隔离旧 policy 的 residual。总/子惯性热关闭维持现有立即停对应 coast 合同。
- 同值重发、橡皮开发选项、别的 monitor 及无关 revision 不误停。

## 4. 首版参数 proposal-v1

这些是供实现和真人验证的初值，不是从橡皮抄来的速度阈值。时间窗口仍用现有 50ms 尾段、3点/20ms、40ms gap、30–70ms age、2ms 时间异常和50ms续甩资格；本次不调这些窗口。

| 空间参数 | TouchPhysical | ScreenPenPhysical | TouchFallback | ScreenPenFallback | IndirectDip |
| --- | ---: | ---: | ---: | ---: | ---: |
| 动作单位 | mm | mm | DIP | DIP | DIP |
| 起步 slop | 2.5 | 1.5 | 15 | 8 | 15 |
| slop epsilon / scroll epsilon | .01 / .02 | .01 / .02 | .05 / .1 | .05 / .1 | .05 / .1 |
| 反向及末段可靠距离 | .25 | .15 | 1 | .75 | 1 |
| 最小释放速度（单位/s） | 10 | 15 | 30 | 60 | 30 |
| 续甩新/旧速度资格（单位/s） | 70 | 100 | 350 | 500 | 350 |
| 续甩滚动速度资格（单位/s） | 30 | 45 | 150 | 225 | 150 |
| 最终模长上限（单位/s） | 2400 | 1800 | 12000 | 9000 | 12000 |
| 基础 tau（秒） | .30 | .20 | .30 | .20 | .30 |
| 基础 c（单位/s²） | 200 | 350 | 1000 | 1750 | 1000 |

PenTail 依真实笔关系选同一笔 profile，仅当用户已开启该入口惯性才释放。间接输入采用现行数值是兼容范围选择，不表示用户已认定其最佳。空间门槛应包含现有所有相同语义用途；不能遗漏末段速度 cap 的可靠距离或异常短 dt 的限速量纲。保留 float 像素误差经变换传播的界，不无依据加“至少一像素”破坏物理一致，也不假定 EDID 能解决硬件触摸噪声。

### 4.1 TouchPhysical 的模式响应

```text
S(x) = smoothstep(clamp(x,0,1))
w_size = S((physicalLongEdge_mm - 320)/880)
w = Laptop ? min(w_size,.25) : max(w_size,.25)  // 正式只有两模式
s = reliableSelectedHandSpeed_mm_s             // boost 前、已限速
a = w * S((s - 600)/900)
tau = .30 + .12*a
c = 200 * (1 - .25*a)
```

同一真实手速与 w 下参数确定；门槛不随 w 反复移位，保证尺寸权重和速度增加时响应不倒退。600mm/s以下没有大屏快甩增强，1500mm/s以上达到该 w 的完整增强，中间平滑。模式只影响响应，不改真实 mm/px。

### 4.2 TouchFallback 的模式响应

始终用系统 DIP 跟手/估速/boost，首版只让 LargeScreen 对明确快甩增加受限续滑；Laptop 保持中性响应。

```text
G = 1 / clamp(min(longOutputDip/1920, shortOutputDip/1080), .5, 4)
G = 1  // logicalOutputKnown=false 或宽高无效时
s_intent = reliableSelectedHandSpeed_DIP_s * G
a = LargeScreen ? .5 * S((s_intent - 750)/1500) : 0
tau = .30 + .12*a
c = 1000 * (1 - .25*a)
```

G 只用于选择快甩响应，数值范围 [.25,2]；不乘位置、实际初速、残余或输出位移，不标毫米。完整输出尺寸用于启发式，不能把白板窗口缩小就当换了一块更小的屏。DIP 空间门槛在回退下不保证物理等价，这是缺少标尺的能力边界，不用伪造英寸补齐。

ScreenPen 不套 Touch 快甩增强，保持更短尾滑；间接设备也不受它影响。此处借鉴橡皮“回退仍考虑模式”的分层，响应函数和阈值属于惯性自己的设计。

### 4.3 曲线与界限

继续 `ds/dt=-s/tau-c`，按选定本次参数用闭式 log1p/expm1 积分，方向沿释放向量。可靠手速先选参数，再进行现有正投影 boost 与模长限幅；动画中不随速度下降重新选参，也不因 boost 自己把响应提档。

```text
T = tau * log1p(v0/(c*tau))
D = tau*v0 - c*tau*T
```

同一有效初速 v0 下，Physical Touch 单次自由滑行的额外距离/时长不超过基础的40%，Fallback不超过20%；这是每条曲线的比较，不是相同连续手势序列或多次续甩的总行程保证。增强曲线可能留下更多残余，下一次有效初速会不同。参数最大处 c*tau 仍不小于基础值，可直接得到该上界。cap必须在boost加法之后、Curve Start之前按本profile对向量模长生效，不能只限制原始估速。

最大理论自由停时约1.540s（TouchPhysical）、.657s（ScreenPenPhysical/ScreenPenFallback）、1.320s（TouchFallback），间接基准1.114s；没有额外硬截断或重新计时。这仅从数学曲线起点到解析终点，不包括Up排队、按住、多次接力、呈现卡顿或最后一帧迟到，不承诺1.6s墙钟完成。

“慢动作精准”首版定义为独立轻放/低于续甩资格时不引入大屏快甩增强；仍通过boost门的低速同向接力可能继承较大残余。保留现有正投影规则，单列独立轻放、同向接力、主动减速和反向定位验收，不以a=0断言它们尾距都短。若真人反馈需要“轻放无条件截断残余”，那是新增产品取舍，须重新评审而非暗改。

无 boost、无限位、理想可靠释放的物理对照：

| 手速 mm/s | 小屏 Touch：距离/时间 | 大屏 w=1 Touch：距离/时间 | ScreenPen：距离/时间 |
| ---: | ---: | ---: | ---: |
| 100 | 12.35mm / .294s | 12.35mm / .294s | 7.58mm / .178s |
| 500 | 109.80mm / .670s | 109.80mm / .670s | 70.64mm / .419s |
| 1000 | 248.31mm / .862s | 287.92mm / .991s | 161.82mm / .545s |
| 1500 | 391.35mm / .977s | 545.03mm / 1.349s | 256.45mm / .622s |

数据来自 research/evaluate-pan-profiles.py 和 proposed-profile-analysis.json；数学审查另用积分复核。只证明理想公式的有限值、单调性和界限，不证明最小释放/boost离散门处整条状态机连续、生产接线、帧率、像素输入噪声或舒适性。尺寸权重min/max在.25可有导数折点，值连续，不宣称处处C1。

## 5. 诊断与性能边界

复用 CanvasPan 诊断 gate 和异步 sink。上下文/Down 输出类别、真实来源、输出身份、mode、profile、unit、两轴 DIP/px与mm/px、fallback reason、G/w、policy版本。释放输出同一条事件的可靠手速、boost前后速度、单位、所选 tau/c、理论终点/时长；帧末其它来源不能覆盖该释放的单位标签。

关闭 gate 不格式化、不增高频取时。解析仅用已缓存事实，不在输入 Move、动画帧、持锁区域枚举 EDID/设备。复用已排序 span 快路径和缓冲容量；不新增线程、timer、per-Move heap allocation 或 UI cursor/control wake。没有测量就不宣称延迟/功耗改善。

## 6. 验证、校准与范围延期

PRD 验收矩阵与 implement.md 对应。纯策略/公式、生产入口合成回放、用户真实设备体验三种证据分开记录。当前 Surface 28×19cm 是近似描述，不能预填本机DPI或trusted标定；历史教室数据为场景先验，不是惯性采样。

数值对比容差：纯double公式相对1e-9；经float DIP/像素生产链转换的单位/方向等价采用相对1e-4或可传播的两端float量化误差界，门槛边界按明确的同侧/越限样本测试，不用宽松epsilon掩盖逻辑差。已证实错误抵消、反向旧速复活、跨源/跨域boost与终态丢失均为零容忍。

真人验收重点：Surface小屏轻拖/轻放/快甩/多次续甩/反向；真实大屏普通移动与明确快甩；屏幕笔定位；EDID无效下两模式。未有大屏/笔硬件证据时如实列未验证，不用离线PASS代替。参数初值需在这些标注动作的轨迹上校准；允许在已批准目标和界限内调整数值并保存证据，改变产品目标/上限/来源政策必须重新评审。

延期：Touchpad与新输入后端、缩放、绘制模式漫游、EDID_OVERRIDE/DisplayID/黑边映射增强、手动校准UI、额外设备调参设置及平台支持扩大。

## 后续修正：超速历史与输出上限分离（待实施）

用户已明确高速度应延续历史，重点防异常暴增。按[后续方案](overspeed-history-plan-2026-11-14.md)分离Tracker输入质量与释放/boost输出cap；按住继续1:1，保留Snapshot渲染估计边界。孤立末点的多区间支持规则须先以真实/合成反例收敛，不能仅删guard便宣称完成；当前2ms跨段清窗是待移除的现状，不是后续目标。

质量规则已收敛为[执行计划末节](overspeed-history-plan-2026-11-14.md)的双邻接区间空间预算+A：至少两正向实际区间；仅实际支持限幅禁旧boost。448量化对照否决了零段直接清零的B；完整可执行公式、代价与native验收边界见该节。

## Touch末端平台专用证据（已实现，待真机）

用户已批准按[Touch执行合同](touch-terminal-fix-2026-11-14.md)分离raw观察与真实空间进展：raw40ms断报、motion70ms失效和Up/Frame时钟不混用，Touch年龄30–70ms衰减期间禁旧boost。Pen及其它路径不改；短暂停顿仍可能明显续滑是明确待真机验证的漫游取舍。

## 当前交付入口：Touch释放回归（自动通过，待真机）

按用户要求形成[具体修复方案](touch-release-repair-plan-2026-11-14.md)，包含历史A/B归属、模块内证据/决策/执行分工、各设备目标和API条件分支。前轮自动PASS不覆盖新真机失败；不得直接按旧统一20ms/运动age规则结束验收。用户已批准开始，按该计划实施和验证；同一Touch模块内集中决策，非Touch保留。
