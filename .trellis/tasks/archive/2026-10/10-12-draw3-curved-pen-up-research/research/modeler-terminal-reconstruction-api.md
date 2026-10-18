# Research: Ink Stroke Modeler 库内终态尾段重构 API 与状态合同

- Query: 保留 Ink Stroke Modeler 的运动/预测算法，在库内增加专门的抬笔后大窗口重构阶段，能否调整尚未进入 L1 的 pre-Up 尾段、固定真实 Up；比较独立模块与 StrokeModeler 自动缓存/API，列出状态、几何及集成难点。
- Scope: internal；读取主产品当前代码、公开头文件及已确认来源的本地原始 `.cc`，只规划，不修改产品、库、测试、配置或 spec，不编译，不运行 GUI，不操作 Git。
- Date: 2026-10-12（Asia/Shanghai）

## Findings

### 1. 结论与证据等级

**可设计成库内模块，但现有公开 API 不能直接完成方案二。** 最小且可解释的扩展是给本地 Ink Stroke Modeler 源码边界新增独立 `terminal_tail_reconstructor.{h,cc}`（名称仅提案），由产品传入原始输入尾部、已接纳的平滑参考窗口、不可修改的入口和真实 Up，显式返回 `TailPatch`。计算归库所有，什么时候能替换哪段几何仍归产品所有。

第一版不必改变 `StrokeModeler` 布局，不必将所有模型历史存入类内，也不必重写原 spring 积分。现有优秀算法通过窗口里的原始平滑参考保留，而新模块在已知终点后分配修正。这是**前向建模 + 终态非因果几何约束**，不是依靠加速度参数让旧因果输出自然改变。

以下“源码事实”来自实际读取；“建议/推断”是尚未实施的设计。几何求解与真实数据效果没有运行验证，不保证大曲率/S 弯/自交/稀疏采样都能改善。工程源码迁移、纯 Abseil archive 拆分由另一个研究主题负责；本报告不以“新增一个 `.cc`”推断当前构建已允许或已可链接。

### 2. Files found

`D3/` 表示 `Inkeys/Inkeys/Drawing/Draw3/`；`H/` 表示 `Inkeys/additional/ink_stroke_modeler/`。`O/` 表示实际读取的原始源码工作树 `D:/Project/Inkeys/inkStrokeModelerTest/Inkeys3-Draw3/inkStrokeModelerTest/additional/ink_stroke_modeler/`，**不是产品当前编译的 `.cc`**。

| 文件 | 一行说明 |
| --- | --- |
| `.trellis/tasks/10-12-draw3-curved-pen-up-research/{prd,design,implement}.md` | 已选择真实 Up 与未固化尾段；上一轮首选库外候选，最新用户方向转为库内模块，仍 planning。 |
| `H/stroke_modeler.h` | 旧 Update 只追加合同、Reset/Predict/Save/Restore 与私有所有权。 |
| `O/stroke_modeler.cc` | Down/Move/Up 的阶段调用、有限当前状态与模型快照。 |
| `H/internal/position_modeler.h`、`O/internal/position_modeler.cc` | spring/drag 前向积分与 ModelEndOfStroke，不保留可反改的整条位置历史。 |
| `O/internal/stylus_state_modeler.cc` | 有序投影、压力/倾角插值及已消费 raw 段淘汰。 |
| `O/internal/wobble_smoother.cc` | 防抖有限时间 deque，存过时窗口前点并非完整轨迹。 |
| `O/internal/prediction/input_predictor.h`、`O/internal/prediction/kalman_predictor.{h,cc}` | predictor 只有输入更新、构造未来预测与复制，不提供端点反向修订。 |
| `O/internal/utils.h` | 可复用基本向量插值、最近点和法线几何函数；不是现成尾部优化器。 |
| `D3/Draw3.InkPrediction.cppm` | 模型、真实点、scratch、半径、时钟、Up 标记与 L1 游标分离。 |
| `D3/Draw3.StrokeGeometry.cpp` | 终点接纳/pin、Result 转换、taper、完成尾段和 Stored 拼接。 |
| `D3/Draw3.DrawingController.cpp` | physical Up/deferred Up/reconnect、真实模型调用与文档提交。 |

### 3. 原库能做与不能做什么

#### 3.1 抬笔阶段只计算未来，不回看已有大窗口

- `H/stroke_modeler.h:62-80` 明确 Update 向调用方 vector 追加新 Result，旧 Result 保持有效，不检查已存在内容。不能静默让 `Update(kUp, results)` 删除或替换历史结果。
- `O/stroke_modeler.cc:233-277` 的 Up 先从前一 corrected anchor 线性积分到 raw Up，再调用 `ModelEndOfStroke`；最后更新 stylus 并令 `last_input_` 为空。只在 `tip_state_buffer_` 中构造这一批未来状态，没有 pre-Up 大窗口。
- `H/internal/position_modeler.h:97-127` 的末端模拟检测 overshoot 后缩小步长重试，或达到距离/变化阈值退出。它尝试靠近锚点，并不同时解决入口切线、历史横向误差或严格 raw endpoint。
- `O/internal/position_modeler.cc:17-21,64-69` 的加速度为 `(anchor - position) / spring_mass_constant - drag_constant * velocity`。增大终点拉力只能影响随后积分，不能自动改变已经输出的尾段。
- `O/stroke_modeler.cc:160-186`、`O/internal/prediction/input_predictor.h:37-55` 的预测是未来结果，不能作为真实轨迹或尾部观测；`Predict` 用 stylus/mitigation 副本，不改变模型本态。
- `O/internal/prediction/kalman_predictor.cc:101-125` 的 acceleration/jerk 权重属于预测，不反馈到真实 PositionModeler。

**设计推断：** 若目标是让整个未提交弯曲区域一起移动，必须引入有未来终点信息的额外求解阶段。只改 `ModelEndOfStroke` 的弹簧/步长，最多改善新增 terminal 段，仍落在历史失败方案的边界内。

#### 3.2 现有内部 deque 不能替代独立原始尾部历史

- Move 的 `corrected_position` 来自 WobbleSmoother，stylus 与 predictor 消费它；Up 使用真实 `input.position`，不再做 wobble。证据：`O/stroke_modeler.cc:289-315,251-271`。
- WobbleSmoother 只为防抖时间窗口保留 samples，过时点被丢弃。证据：`O/internal/wobble_smoother.cc:37-76`。
- StylusStateModeler 保存的是传入该阶段的 corrected 位置与 stylus 状态；Project 只向前匹配，消费后的段立即 `pop_front`。证据：`O/internal/stylus_state_modeler.cc:29-70,192-232`。不能 Up 后调用现有 Project 回拟已经淘汰的任意窗口。
- 法线投影的“顺序不倒退”思想可以借鉴，但现有实现还根据加速度在左右候选中选择弯曲外侧；它原本为 stylus 属性关联服务，不能直接当“向曲率中心收缩”的几何算法。证据：`O/internal/stylus_state_modeler.cc:107-159`。
- `StrokeModeler` 仅保留 `last_input_`、当前子模型状态和临时 tip buffer；tip buffer 明确没有跨调用历史合同。证据：`H/stroke_modeler.h:117-138`。

**建议：** 第一版使用产品实际消费且进入建模路径的 raw trace，保留真实 Up 以及样本来源；无须为了获取 corrected trace 改老类。若实验显示 raw 噪声过大，再增加可选 corrected 观测/trace hook，但不要在新模块中对已平滑模型结果再无差别套一遍 wobble。缓存 discarded RTS 包、改变 mailbox 采样，是另一项输入保真研究，不随本方案默认实施。

### 4. 推荐的库内边界：独立、无状态的终态求解器

#### 4.1 一个可审核的接口草案

以下仅描述协议，不是已存在或获批的 API；最终命名、类型和 span 实现以项目工具链为准。

```text
ReconstructTerminalTail(request, reusableScratch) -> TerminalTailOutcome

request:
  reference_window: 接纳后的模型派生几何；每点有 caller source id、位置
  raw_trace: 实际输入顺序/位置；按需有物理时间、model时间和来源标记
  fixed_anchor: 真实稳定连接位置（不可改）
  incoming_tangent: 可信时由已稳定前缀给出；附置信度/可缺失
  endpoint: 真实 physical Up 位置（硬约束）
  immutable_boundary / replaceable_range: 由调用方定义的 source范围
  budgets: 最大输入/输出点数、匹配搜索/求解迭代和位移界限

outcome:
  applied / unchanged / insufficient_window / ambiguous_match / invalid / budget
  TailPatch:
    source replacement range（调用方的 id，不是库Result索引）
    replacement positions
    每个输出点的有序来源映射（source id 或相邻id + 插值比例）
    可选 raw分支匹配证据、入口/endpoint误差、最大位移等诊断
```

协议不返回伪造的运动 `Result`，位置补丁不携带“复制旧 velocity/acceleration 后的新中心线”。几何求解器不认识 `InkPoint`、DirectX、DIP/L1、GPU、widthEstimator 或 Stored；这些都由产品适配。

TailPatch 范围应为半开区间、含明确源版本/基准点数或产品側 token。调用方检查范围还属于当前同一个 runtime/baseline，再原子应用；失败不半写。source id 最初可用产品 `realPoints` 的稳定序号，不能混用 `modeledResults` 的下标。如果未来点数改变或范围移动，id 和映射合同仍有效。

连接锚点可以作为输出中的首个共享点，但其坐标必须原值不变；所有真正修改的 source id 都必须在不可改边界之后。已进入 L1 的中心线及稳定侧入射方向作为约束。无 L1 时入口取真实 Down/首个固定参考，不制造新的起笔位置。

第一版优先保持可变段点数/顺序，降低宽度和时间映射难度；Up 补点/异常 terminal 段剔除可作为明确且有映射的后续能力。保持点数并不能解决匹配与短窗口不足，但允许先隔离中心线算法收益。

#### 4.2 优于在 ProcessUpEvent 中直接替换的原因

1. **库不知道真实固化边界。** L1 游标、实际被接纳的 point 和共享连接点都在产品。`Update` 甚至不知道调用方是否保留全部先前 Result。
2. **产品输入与库事件不同。** physical Up 可能暂时作为 kMove 等待续接，新求解器不能只依赖库“已完成”布尔状态。
3. **避免污染优秀前向算法。** 不修改 spring 当前态、predictor 或 stylus 历史；旧 Update/Predict 的输出可保持基线，关掉新求解器就能回退。
4. **避免把几何与物理导数混在一起。** source map 让产品保留基础笔宽；只有最终几何被替换，求解不制造加速度/压力样本。
5. **库内计算和产品决策清楚分工。** 新模块属于自有 modeler fork 或其 extension 源码，但不会用任何 Draw3 私有类型；这仍满足“抬笔算法由库内完成”。

**限定：** “独立/无状态”描述语义而非禁止 reusable scratch。可以复用分配、矩阵工作区与有界临时缓存；它们不是会改变 Move/Predict 的模型历史。

### 5. 与类内自动缓存方案的比较

| 项目 | 独立 terminal 模块（推荐先做） | StrokeModeler 内自动缓存 + 新方法 |
| --- | --- | --- |
| raw 与 corrected 观测 | 产品提供 raw；corrected 可选且第一版不必获取 | 类内最容易在 wobble 前后同时缓存 |
| 被接纳几何与 L1 边界 | 产品直接传入真正的窗口和锚点 | 仍须由产品提供边界和接纳映射，类内 raw Result 不够 |
| 旧 Update 合同 | 不改只追加语义 | 也必须保留旧语义，新方法显式返回 replacement metadata |
| Save/Restore | 求解不改模型；无新增历史要回滚 | 缓存、绝对序号、最后Up、可用窗口及其saved副本都需跟着快照 |
| Reset/Down | 产品清自己的有限缓存；计算本身无跨stroke状态 | Reset与每次Down都需清缓存/generation/saved终态 |
| physical Up/reconnect | 可用产品物理事件，无须kUp状态 | 普通Input无法区分真实Up转成kMove与正常Move，需新增旁路语义 |
| 风险面 | 新几何算法 + 适配；旧类布局不必变 | 老类布局/头文件ABI、每次Update成本、快照一致性、旧生命周期都扩大 |
| 适合阶段 | 第一版证明有界尾段重构的收益 | 证明算法有效后，再按通用调用者需求收拢到高级facade/API |

如果以后增加 `StrokeModeler::ReconstructTerminalTail(...)`，建议它只做高级 facade、返回 TailPatch，内部仍调用独立求解器。不要让旧 Update(kUp) 隐式重写已有输出。可以新增另外的 `UpdateWithTailReplacement` 协议，但那会引入全程 revision 生命周期，第一版没有必要。

自动缓存绝不能只“追加更多 samples”：长 stationary 会生成合成 kMove，raw 运动权重不能被这些同位置样本挤占或累计；Predict 不能写真实缓存；保存需要同时带 absolute output indices，否则 Restore 后替换范围可能指向旧调用方vector。既有 Save 仅保存模型状态，调用方输出和宽度/游标也不会自动恢复。证据：`O/stroke_modeler.cc:321-343` 与 `D3/Draw3.InkPrediction.cppm:436-485`。

### 6. Draw3 适配必须尊重的事实

#### 6.1 realPoints 不是 modeledResults 的一对一副本

- `ActiveStroke` 明确分开 `modeledResults`、`modelScratch`、`realPoints`、`predictedPoints`、`convertedResultCount` 与 `committedIndex`。证据：`D3/Draw3.InkPrediction.cppm:436-449`。
- 普通 Move 的结果可逐个转换至 realPoints，转换游标取 modeledResults 大小；stationary/terminal 则进入 scratch，经接纳门禁后仅一部分形成 realPoints。证据：`D3/Draw3.StrokeGeometry.cpp:1653-1682,1529-1588`。
- pin 精确 endpoint 会替换/新增几何点，未必对应任何原 Result；失败回退同样生成 raw endpoint。证据：`D3/Draw3.StrokeGeometry.cpp:449-463,1434-1476` 与 `D3/Draw3.DrawingController.cpp:9071-9075`。

**建议：** reference 应以“实际可见并有基础半径的接纳后几何”为基线，原 modeled Result 可作为额外先验/诊断，但不将它的索引当替换范围。需标记 modeled、stationary-admitted、pin、fallback 等来源；pin/人工 Up 回接不应被当成高可信真实弯曲先验。raw trace 是另一条数据，不把 realPoints 误称 raw。

#### 6.2 三种时间不能混在一起

- physical time 是 QPC 对应真实 contact 时间；model time 在长停收敛后可减去 `modelTimeOffset`，并被约束至少比上一次大 1us。证据：`D3/Draw3.StrokeGeometry.cpp:1596-1605`。
- ordinary Pen realPoints 的 time 已映射为显示时间并封顶到最后运动；stationary/terminal Result 仍能有未来 model time。证据：`D3/Draw3.StrokeGeometry.cpp:1665-1667,1580-1582` 和 `O/stroke_modeler.cc:255-260`。
- 第一次 physical Up 固定 `terminalDisplayTime` 与 `terminalFirstPoint`，prediction 被清掉；显示年龄不随之后timeout增长。证据：`D3/Draw3.StrokeGeometry.cpp:419-446`。

**建议：** raw trace 以真实样本顺序和物理时间保留轨迹证据，另存实际 model time（如用于诊断/匹配）；各参考点传 source map，不能仅凭 clamped display time寻找 raw。算法几何正则以弧长或明确参数化计算。stationary、reconnect桥接和synthetic终态不是新真实运动观测，不反复增加 raw权重。这个库仍需单位无关，像素门槛由产品配置传入或归一化，不在通用求解器里硬编码 0.05px。

#### 6.3 保护窗口不是随便能选一个“大窗口”

- L1 当前以 live-tip duration + 实际 prediction duration 决定可变后缀，多保留一个连接点；已经提交的 `committedIndex` 只前移。证据：`D3/Draw3.StrokeGeometry.cpp:265-273,1741-1785`。
- 常用 Fps120 的基础live-tail为 55ms；HardPen 当前没有普通笔live-tip保护，prediction关闭时更可能已将几何提交。证据：`D3/Draw3.StrokeGeometry.cpp:247` 与 `D3/Draw3.DrawingController.cpp:13264-13302`。

**建议：** 终态模块只能在调用方提供的窗口内扩展。若实验需要更大的未固化区，必须另设重构保护时间/弧长与点数预算，并在绘制过程中就延后 L1；它与 taper 长度解耦。不能到了 Up 才要求修改已落L1的整段转弯，也不能把 enlarged raw缓存当 enlarged几何可变窗口。该合同更改需先更新原spec并审查；用户当前允许的是未L1尾段重构，未授权改已L1。

#### 6.4 physical Up、deferred kUp、reconnect

源码事实：

1. 开启断触续接时，physical Up 可被 `deferUp` 识别并当 `kMove` 输入；随后冻结候选、记录 `deferredUpSnapshot`、保持模型未结束。证据：`D3/Draw3.DrawingController.cpp:9139-9142,9184-9241,9294-9335`。
2. 候选超时/淘汰才调用 `completeModelUp` 发送真正kUp；使用保存的原物理终点/QPC，不能把等待时刻当新抬笔。证据：`:9004-9094,12869-12887`。
3. 成功续接发送kMove，在成功后 `ClearPenTerminalState` 并清candidate字段。证据：`:8568-8605,8639-8651`。不是已经kUp后再自然kMove。

**第一阶段建议：** 最终不可续接边界完成原Update(kUp)/fallback后，用统一helper运行终态求解，只改独立冻结尾段或完成态scratch。两条终止入口都要经过该helper，且每个logical stroke只冻结一次。等待候选阶段保留现有endpoint-safe几何，成功reconnect不需要撤销任何修正。代价是启用续接时末端改善会等待最终完成；必须明确记录这一延迟，不能声称physical Up立即改善。

若必须在physical Up立即改善，可在等待期生成**可撤销 preview TailPatch**，保持baseline/raw/model不改且暂停新增L1；真正完成时从同一基线重算/冻结，成功reconnect时丢弃preview并从baseline继续。这个方案还需验证“从修正preview回原模型”的可见跳变，不能只说状态可回滚就认为视觉无缝。重构不应改变现有reconnect匹配的终端速度/方向证据，除非作为独立实验明确改其合同。

Cancelled 丢弃patch和缓存、不追加Stored；对象池复用/Reset/newDown清所有产品scratch与版本；多contact拥有独立输入尾部与结果。场景边界合成终止也使用同一完成helper，但需标记不是新的真实device运动，不能污染raw统计。第一版应限定普通Pen，HardPen若纳入需独立保护窗口决策，其他工具保持现状。

#### 6.5 不让补丁再次经过旧单调endpoint门禁

`AppendEndpointBoundedModeledPoints` 在到Up距离不再严格减小、越过approach平面或接近终点时截断并pin；这是防spring回摆的局部停止策略。证据：`D3/Draw3.StrokeGeometry.cpp:1544-1588`。对任意S弯/回环的大窗口重构要求“全程距Up单调减小”会裁坏本来合法的曲线。

建议原 Up scratch 先保留旧门禁作为安全基线/失败回退，新TailPatch则通过专门的窗口检查后直接组合最终几何；不要伪装成新的普通 Result batch 再送旧门禁。新检查包括固定边界不变、严格endpoint、finite、有序同一raw分支、位移/空间走廊、弧长/采样质量及不新增不合理反向/自交。终端小邻域可以做方向/overshoot约束，但不把它推广成整窗口单调距离规则。如何区分真实反向和错误回钩仍是算法风险。

#### 6.6 压力、宽度与导数

- 模拟宽度来源是产品真实inputSpeed，不是spring速度；widthEstimator还按原model时间与相邻model距离限制半径变化。硬件压力模式同样按时间/距离推进状态。证据：`D3/Draw3.StrokeGeometry.cpp:285-337`。
- tail patch若只是改Result.position再保留原velocity/acceleration，其物理字段已不对应修正轨迹。若按修正坐标重新差分，它们又是派生几何导数，不能当真实raw加速度或喂回预测/宽度。
- **建议第一版只输出位置 + 来源映射。** 产品从已确认的未taper基础半径和相应显示time有序转移宽度，不二次调用有状态widthEstimator，不用修正后的视觉速度改变压力。改变点数时依据source interpolation转移半径/time；orientation不能普通线性跨2π，第一版不重估stylus字段。
- 再按最新共享taper/公切线逻辑生成派生轮廓；新的弧长会改变收锋长度，即使基础r相同也会影响最终形状，需要单独验收。证据：`D3/Draw3.StrokeGeometry.cpp:490-501`、`.trellis/spec/native-desktop/input-and-ink.md:645-652`。
- 已固化连接点的中心线位置不变；最终轮廓与稳定侧接缝也要查，不能“中心点未改”便忽略半径/tangent变化。共享半径规则若需要调整已L1附近范围，应明确为独立合同决策，不暗中扩大。

#### 6.7 完成态与Stored同一几何

`BuildCompletedPenTail` 当前从realPoints的committedIndex开始拷贝，`FinalizeStoredStroke` 将稳定前缀和这份tail去重组合，产品先Append再从同一Stored对象绘制。证据：`D3/Draw3.StrokeGeometry.cpp:490-531`、`D3/Draw3.DrawingController.cpp:1833-1849`。

**建议：** 扩展完成tail构造让其可消费已冻结TailPatch，再执行同一taper/tangency；活动收尾预览若存在也消费同一几何来源。不得只移动l0DrawPoints而让Stored重建旧中心线。只需冻结派生中心线/基础r的补丁，不改原modeledResults，不倒退convertedResultCount或committedIndex。旧新bounds共同进入dirty，首次L2与后续重放使用同一Stored。库不负责文档append事务。

### 7. 具体几何算法方向与复用原算法的方式

推荐先验证“平滑参考上的有界修正场”而非全笔重新平滑。窗口模型点为 `m_i`，修正为 `q_i=m_i+d_i`。可选目标包括：保持`d`小、靠近有序raw走廊、惩罚弧长参数上的`d''`、可信时末端切线软约束。固定入口位置/稳定方向，最后点精确等于raw Up。参考先验权重在入口较强，向末端逐渐转为raw约束，但硬endpoint不依赖无限大权重。

这样保留了原算法的局部平滑与多数形状，不要求所有轨迹有圆心，也不直接最小化整条q曲率把真实转弯拉直。基于同一raw分支的横向位移，在单向圆弧外偏时可以自然向内修正；S弯/反向时不能套统一收缩方向。

可以复用 `O/internal/utils.h:91,102,119-126` 的插值、最近点、法线投影几何基本函数；不能直接复用会消费并丢历史的旧StylusStateModeler::Project来做反向重构。求解器仍要自己处理有序对应、短窗口、重复点与边界。

入口切线有两种可实验形式：固定前两个参考点（保持离散入口段方向，损失一些自由度）；或固定首点并约束第一段正向共线/连续曲线导数。后者若要求正向、不出现overshoot，会引入额外约束，不应在文档中把所有情况描述成一次无约束线性解。只有2-3个点或零长度窗口时，硬入口、硬终点和低弯曲要求可能不相容，应返回insufficient，而不是强造曲线。

匹配不能直接用同时间raw点：spring存在相位滞后，display时间又可能封顶。候选应比较时间有界的顺序投影、法线匹配或弧长对应，限制在最近raw分支，保持对应不倒退。位移场正则需按实际空间间距或先等弧长采样，避免“多加几个点”让转角数字变小。初版采用哪种匹配、权重和触发阈值仍需真实轨迹决定。

### 8. 纯终态阶段、spring重写与双向replay的取舍

| 方向 | 优点 | 难点/为何不优先 |
| --- | --- | --- |
| 库内独立纯终态重构 | 前向输出不变；终点/入口合同显式；接口和状态风险可控 | 只能改未固化窗口；Up一次形变可能可见；匹配/触发/调参仍需实验 |
| 改ModelEndOfStroke的力/阻尼/步长 | 最接近旧库Up调用路径 | 无历史窗口，无法修pre-Up外偏；更强终点力仍可能集中拐弯 |
| Up时恢复窗口、同参数重播 | 可复用Save/Restore和前向模型 | 同输入同状态的因果重播不会因为后来知道Up而改变过去；还需回滚产品状态 |
| 修改raw窗口再前向重播 | 原spring仍参与新的候选计算 | raw先变形才有结果，等于先引入新算法；压力投影/速度/预测与基线会一起改变 |
| 双向弹簧/终端边界值求解 | 可以把未来endpoint传回较早尾段 | 阻尼积分不是直接负dt就安全可逆；端点/速度边界、合并两向解、stylus映射和数值稳定性均需新设计，风险大于几何阶段 |
| 全新的实时有修订模型 | 可逐帧减小外偏，降低Up跳动 | 需要永久revision/stability协议和L1保留窗口，手感/性能回归面扩大；作为后续独立阶段 |

由于重构只是终态派生几何，内部PositionModeler仍停在它原本的最终状态即可，不必反写q作为TipState。最终kUp后不再Predict；重连发生在deferred路径而非完成后的模型续接。若之后决定实时应用，新的稳定状态/预测接缝就必须重新设计，不能沿用本报告“无影响”结论。

### 9. 有把握实现的边界与目前没有把握的效果

| 类别 | 当前判断 | 需补的证据 |
| --- | --- | --- |
| 保留旧Update/Predict行为，新模块opt-in | 工程上清楚可实现 | 源码编译基线与新旧轨迹逐字段对照；此处未构建 |
| L1前缀不动、endpoint严格一致、补丁范围原子校验 | 合同清楚可实现 | 固定边界/范围/finite/错误路径测试 |
| 位置与source映射，不重复推进widthEstimator | 工程上可明确实现 | 实际压力模式、点数变化、taper与Stored对照 |
| 抑制真实高速圆弧Up末钩 | 有合理数学方向，尚不能承诺 | 当前真实modeler与真实失败raw样本的同轨迹对照 |
| 无抬笔抽动/缩回 | 无保证 | 最大位移、可见前后变化、deferred-preview/reconnect视觉验收 |
| S弯、自交、真实尖角/折返不误伤 | 无保证 | 有序branch匹配与不改坏样本；不能用统一圆心处理 |
| 稀疏/被mailbox覆盖的raw正确恢复曲率 | 不能保证，可能信息不足 | 完整设备采样/实际消费采样对照；缺失中间路径不能靠算法证明还原 |
| 已L1的外偏与短window问题 | 本边界下不能完全修 | 更早保留窗口或实时机制的独立决策 |
| 运行成本 | 可以做有界预算，实际延迟未定 | 各点数/多contact/坏条件下无窗口实测，预算不能只限制单次Update |

“实现收尾求解模块”与“达到用户期待的笔迹效果”是两个不同里程碑。接口、边界和失败回退可作严格验收；几何效果要先收集同轨迹失败样本、成功样本和真实角点样本，再决定生产算法。没有实测就承诺“只要加权就能消除大曲率问题”不成立。

### 10. 建议验证顺序（规划，未执行）

1. 保持原参数/前向算法，将产品基线的raw、pre-Up接纳几何、terminal scratch、pin/fallback与L1入口输出为无窗口可重放数据。先确认真实问题能复现。
2. 同样输入分别做旧路径、截尾对照、库内终态求解；使用固定宽度隔离中心线。对按同物理弧长重采样后的端部方向/曲率、raw走廊误差、入口/endpoint、最大位移和路径长度比较。
3. 证明关闭新模块与源码编译基线相同；Update/Predict的old Result仍valid，模块scratch失败不改变模型。
4. 压力、taper、完成/Stored、long idle、物理时间锁定、候选超时/成功续接/Cancelled/multi-contact分别做生产helper无窗口验证。明确final-only改善的等待延迟。
5. 根据short-window失败率判断是否另增重构L1保护；根据Up跳动大小判断是否需要preview/运动期。不能通过扩大已提交修改范围或把失败样本移出验收来“通过”。

## Related specs

- `.trellis/workflow.md`：当前planning，本轮不start、不实施。
- `.trellis/spec/native/runtime-and-rendering.md:400-429`：terminalFirstPoint保护、physicalUp时间、旧endpoint门禁与仍未解决的曲率外偏。用户最新允许未L1 pre-Up重构，但实施前需显式修订这部分几何保护合同；时间锁保持。
- `.trellis/spec/native-desktop/input-and-ink.md:635-652`：taper只改半径、真实中心线/L1当前不改；最新方向需明确独立终态修正边界，不能顺带扩大旧taper任务。
- `.trellis/spec/guides/cross-layer-thinking-guide.md:72-101`：L1稳定游标单调、prediction不保存、Stored为初次与重放真值。
- `.trellis/spec/native-desktop/draw3-integration.md:5-34`：当前只链接固定merge.lib、不直编第三方cc。用户本轮明确提出规划fork源码，规划可以比较例外；落地前需维护spec而非无声违背。
- `.trellis/spec/native/quality-and-validation.md:52`：预编译库Release ABI，不能把新源码直编视为CRT/Abseil自动兼容。

## External references

- 本轮接口与状态判断以当前本地代码为依据，没有重新进行上游修复搜索。上轮固定版本/来源核对在本任务 `research/upstream-status-and-diff.md`，当前源码参考对应上游 `f2388813b0b25bc3e33d143d369a8367ab2e30c8`（源码比对结果，不代表merge.lib符号/实现已核实）。
- 官方接口源参考：[StrokeModeler 头文件](https://github.com/google/ink-stroke-modeler/blob/f2388813b0b25bc3e33d143d369a8367ab2e30c8/ink_stroke_modeler/stroke_modeler.h)、[StrokeModeler 实现](https://github.com/google/ink-stroke-modeler/blob/f2388813b0b25bc3e33d143d369a8367ab2e30c8/ink_stroke_modeler/stroke_modeler.cc)。此处链接作为上轮已确认来源的固定版本参考，行号来自本地文件。
- 未引入新外部优化库、学术算法实现或新Google Ink依赖；目标函数仅候选推导，不是上游现成能力或已发表修复。

## Caveats / Not Found

- 没有真实当前binary回放、编译、运行、GUI或设备验证；新增API名、类型、算法与阈值均仅提案。
- 不能保证自动知道圆心、还原被覆盖输入、移除已L1外偏、避免所有Up形变。短窗口和不可信终端方向存在客观约束冲突。
- 第一版独立模块可以只编新cc，未必必须改旧StrokeModeler；若工程决定将现有ISM cc全部移到产品自编，则旧模型版本/ABI/archive符号分拆仍是独立验证门，不能把两者混成算法收益。
- 当前产品头文件与原始工作树cc分处不同目录；本报告读取源码不证明旧merge.lib实现与其完全一致。
- 本报告不修改spec或主设计；供主会话将最新“库内终态模块”方向与`design.md`上一轮“库外首选”协调。
