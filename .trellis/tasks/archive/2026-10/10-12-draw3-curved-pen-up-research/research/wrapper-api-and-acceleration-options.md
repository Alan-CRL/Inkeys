# Research: StrokeModeler 状态接口、加速度参数与可变几何尾段

- Query: 用户提出直接丢弃末钩，或在较大窗口内提高真实终点权重、向局部曲率中心收缩。核查当前公开 API 能否改旧结果/动态调 spring 与 drag、Kalman acceleration/jerk 权重的实际作用、Save/Restore 重播语义及库外尾段重建的集成边界。
- Scope: internal；读取主产品头文件、生产代码和已确认来源的本地原始源码当前工作树；不重复上游历史调查。
- Date: 2026-10-12（Asia/Shanghai）

## Findings

### 1. 关键结论与证据等级

1. **源码事实：库不能通过后来的普通 Update 自动修订旧 Result，但这不禁止产品修订自己的派生几何。** `Update` 只追加新 Result，不检查调用方已有 vector；产品本来就有独立的 modeledResults、realPoints、l0DrawPoints。库保证旧 Result 仍有效，不代表结果值在调用方必须只读，更不代表几何层只能原样绘制。为避免破坏 baseline 和转换游标，修正应保留未经改写的模型输出作为参考，而不是回写库的累积 vector。证据：`Inkeys/additional/ink_stroke_modeler/stroke_modeler.h:62-80`；`Draw3.InkPrediction.cppm:436-449`；`Draw3.StrokeGeometry.cpp:1653-1682`。
2. **源码事实：Kalman 的 acceleration_weight / jerk_weight 不修正真实中心线。** 两者只缩放预测状态，再生成预测 cubic；实际 Move 和 Up 中心线由 PositionModeler 产生，没有 predictor 到 position model 的反馈。把它们调高、调低可改变抬笔前可见预测，但不能消除已经进入确认中心线及 Stored 的外偏。证据：`params.h:231-235`；`O/internal/prediction/kalman_predictor.cc:101-125`；`O/stroke_modeler.cc:251-277,306-317`。
3. **源码事实：公开 StrokeModeler 没有保持笔画状态的动态参数 setter。** `Reset(params)` 复制参数、清 last_input_ 和顶层 Save 有效位，重新创建 predictor；下次必须 Down，直接 Move 会报没有进行中笔画。改变传给 Reset 的原始配置对象也无效，因为参数已复制。证据：`stroke_modeler.h:52-60,108-138`；`O/stroke_modeler.cc:79-126,189-218,282-286`。
4. **源码事实：Save/Restore 可以重播缓存窗口，但只提供一个库内状态快照，不提供参数切换或产品几何回滚。** 它保存模型各阶段、predictor 和 last_input_，不保存结果 vector、realPoints、widthEstimator、convertedResultCount、committedIndex、模型/显示时钟等。Reset 会使 Restore 无效。证据：`O/stroke_modeler.cc:321-343`；`Draw3.InkPrediction.cppm:436-485`。
5. **受限推断：本题更易控制的方案是库外有界尾段重建。** 保持模型输出不改，从尚未提交的 baseline 尾段及近期 raw 轨迹产生修正几何；真实 Up 用端点约束，入口用稳定连接点/切线约束。它绕开 wrapper 的参数限制，并可统一完成与存储。但现有实现尚无该中心线修正，需要后续批准后实施和验证，不是现有功能。

本报告中的 `Draw3.*` 均表示 `Inkeys/Inkeys/Drawing/Draw3/` 下同名文件；未加目录的库头文件表示 `Inkeys/additional/ink_stroke_modeler/`。定义 `O = D:/Project/Inkeys/inkStrokeModelerTest/Inkeys3-Draw3/inkStrokeModelerTest/additional/ink_stroke_modeler`。`O/...:行号` 是此次实际读取的当前工作树源码，不是产品编译进去的 `.cc`。

### 2. Files found

| 文件 | 一行说明 |
| --- | --- |
| `.trellis/tasks/10-12-draw3-curved-pen-up-research/prd.md` | 当前 planning/research 范围，不实施。 |
| `.trellis/spec/native/runtime-and-rendering.md:400-429` | 真实终点、stationary/terminal 门禁、物理 Up 时钟及尚未解决的曲率问题。 |
| `.trellis/spec/native-desktop/input-and-ink.md:645-652` | 最新 taper 只改变派生半径；L1、完成与 Stored 一致合同。 |
| `.trellis/spec/native-desktop/draw3-integration.md:5-34` | 头文件和固定架构静态库来源；产品不构建第三方 `.cc`。 |
| `.trellis/spec/guides/cross-layer-thinking-guide.md:72-101` | 稳定前缀单调、prediction 不保存、Stored 真值及共享 L0。 |
| `Inkeys/additional/ink_stroke_modeler/stroke_modeler.h` | 公开 Reset/Update/Predict/Save/Restore 及私有模型所有权。 |
| `Inkeys/additional/ink_stroke_modeler/params.h` | 真实 position spring/drag、位置融合与预测权重定义。 |
| `Inkeys/additional/ink_stroke_modeler/types.h:140-169` | Input 只有事件/位置/时间/压力/倾角/方位，无输入置信权重或加速度字段。 |
| `Inkeys/additional/ink_stroke_modeler/internal/position_modeler.h` | 位置状态、参数访问及单一 TipState 保存。 |
| `O/stroke_modeler.cc` | Reset/真实运动/预测/快照的完整实际调用顺序。 |
| `O/internal/position_modeler.cc` | 真实加速度方程和时间积分。 |
| `O/internal/prediction/kalman_predictor.{h,cc}` | 预测状态、权重、cubic、MakeCopy 动态状态克隆。 |
| `O/internal/wobble_smoother.{h,cc}` | 防抖历史 deque 和其保存/恢复。 |
| `O/internal/stylus_state_modeler.{h,cc}` | 输入/压力 deque、投影状态及其保存/恢复。 |
| `O/internal/loop_contraction_mitigation_modeler.{h,cc}` | 速度融合 deque 和其保存/恢复。 |
| `Draw3.InkPrediction.{cppm,cpp}` | 产品模型配置、独立几何容器、半径/时钟/终点状态。 |
| `Draw3.StrokeGeometry.cpp` | Result 转换、L0、L1、完成尾段、Stored 拼接。 |
| `Draw3.DrawingController.cpp` | 真实模型调用、预测生成、工具保护窗口及文档提交。 |

### 3. Reset 与动态参数：哪些操作实际可行

#### 3.1 Reset(params) 不是在线调参

- `O/stroke_modeler.cc:81-89` 先校验并复制参数；参数非法时直接返回，不到清状态步骤。成功才 `ResetInternal()`，将 `last_input_` 清空并令 `save_active_=false`（`:90,123-126`）。
- `Reset(params)` 随后按 PredictionParams 重新构建 predictor（`:92-107`）。零参数 `Reset()` 保留原参数，但同样结束进行中笔画（`:114-126`）。
- 清 stroke 后内部子模型并非所有成员立即归零；它们在下一 Down 按真实起点重置（`:196-218`），包括 position/velocity/acceleration、wobble、stylus、mitigation、predictor 历史。因此“当前笔画清状态”的语义不能用“残留内部 state 可能还在”来绕过。
- 继续送 Move 会因 `last_input_` 为空报错（`:282-286`）；新 Down 使 position model 从 `{position,time}` 默认零速度/零加速度开始（`:201-202`）。这会丢失旧惯性/压感映射/预测历史，不是无缝修改 drag。
- 私有 `position_modeler_` 不对外暴露（`stroke_modeler.h:108-138`）。内部 PositionModeler 虽有 `Reset(TipState, params)`，而 `Params()` 只返回 const 引用（`position_modeler.h:43-54`），调用方无法通过正常 StrokeModeler API 取出其状态后单独改参。

**推断的可选路线：** 若将来明确选择在线调真实加速度，可 fork 库增加有契约的 setter，或自有 position-stage；需处理积分稳定性、采样角度估算、predictor/投影一致性及三架构 binary/头文件同步。当前固定库不是改一份产品头文件就能取得这个能力。此方向改动面大于独立尾段几何层，本轮不建议优先。

#### 3.2 真正影响中心线的加速度和“权重”

真实 position-stage 使用：

```text
a = (anchor - position) / spring_mass_constant - drag_constant * velocity
v_next = v + dt * a
position_next = position + dt * v_next
```

源码：`O/internal/position_modeler.cc:17-21,64-69`。spring_mass_constant 是 mass/spring，不是终点权重。减小它会增强位置追踪；drag 影响阻尼，但全程手感和滞后也会改变，不能等同单独把终点拉强。

另有真实输出的投影融合：`result = (1-alpha)*projected + alpha*spring`（`O/stroke_modeler.cc:62-72`）；其公开参数在 `params.h:58-108`。这是真实位置层权重，区别于 Kalman 权重；它根据速度历史变化，非“Up 点在大窗口里有更高拟合权重”，也不重写已输出的旧点。本任务上游报告已详述该能力，本报告不重复调查其修复历史。

Input 没有 `weight` 或 `acceleration` 参数（`types.h:140-169`）。在当前 public API 上不能直接给最后一个点附高置信度，也不能直接送“向曲率中心的加速度”。额外同位 Move 若增加时间，会推进未来 spring 收敛；它不会使已有点随终点调整。完全相同输入还会触发 duplicate input（`O/stroke_modeler.cc:139-145`）。合成大量同位点也违反现有有界收敛合同，不能用来替代窗口拟合。

#### 3.3 Kalman 权重的精确边界

`KalmanPredictor::GetEstimatedState()` 将估计的 a、jerk 分别乘 acceleration_weight、jerk_weight（`O/internal/prediction/kalman_predictor.cc:101-106`），`ConstructPrediction` 再构造与最后 modeled state 连接的 cubic 和向未来延伸的 cubic（`:109-125`）。真实 Move 先推进 position model，再更新 predictor 观察（`O/stroke_modeler.cc:306-317`），没有取 predictor 估计来改真实 position state。Up 不读取 predictor 的估计（`:233-277`）。

产品配置两值为 `0.5/0.1`（`Draw3.InkPrediction.cpp:217-218`）；prediction 独立存放，Up 清除（`Draw3.DrawingController.cpp:13278-13293,13319-13323`）。**可以验证调权是否减少上一帧预测消失造成的跳变，但不能把它作为最终确认轨迹末钩的根治。**

### 4. Save/Restore：允许缓存 raw 重播，但不是完整笔画快照

#### 4.1 实际快照内容

`O/stroke_modeler.cc:321-343` 保存/恢复：

| 状态 | 实际内容 | 源码 |
| --- | --- | --- |
| Wobble | samples deque、weighted_position_sum、distance_sum、duration_sum | `O/internal/wobble_smoother.h:58-74`；`.cc:79-85` |
| Position | 当前 TipState，包含 position/velocity/acceleration/time | 产品 `position_modeler.h:132-138` |
| Stylus | 已收到 unknown 字段状态、输入/压感 deque、projection | `O/internal/stylus_state_modeler.h:84-106`；`.cc:235-243` |
| Mitigation | speed_samples deque | `O/internal/loop_contraction_mitigation_modeler.cc:48-54` |
| Predictor | MakeCopy 克隆所有动态状态；Kalman 按对象复制，包括滤波器和时间 deque | `O/stroke_modeler.cc:327-328,341-342`；`O/internal/prediction/kalman_predictor.h:60-62,95-103` |
| Last input | 原始 Input 及 corrected_position | `O/stroke_modeler.cc:326,340`；产品 `stroke_modeler.h:130-138` |

**不包含** StrokeModelParams 保存副本，也不包含调用方输出 vector 或产品 ActiveStroke 状态。Position Save 也只存 TipState，不存 params。只有一个内部槽位；下一次 Save 覆盖旧槽，没有可导出的 Snapshot handle 或快照栈（`stroke_modeler.h:95-106,136-138`）。Restore 可重复调用，不消费快照；kUp 结束 `last_input_` 后也可 Restore 回 pre-Up Save，因为 Up 未清 save_active_（`O/stroke_modeler.cc:276-277,333-343`）。

#### 4.2 缓存窗口重播的可行流程（概念，未实施）

1. 在仍有笔画状态的安全窗口起点 Save，同时由产品记录对应输出数量/转换状态/半径状态/时钟状态及稳定锚点。
2. 缓存从该点起实际送给库的 Input 序列，包含真实样本及必要的 stationary Move、模型时间和 stylus 字段；只有 raw x/y 不足以精确回放。
3. Restore，并由产品截断尚未提交的输出与派生几何，恢复半径估算/endpoint admission/时钟等相关状态；然后按合法时间顺序重新 Update 缓存窗口。
4. 若要向前移动 Save 锚点，可以 Restore，先重播旧锚到新锚的前段，Save 覆盖为新锚，再重播剩余窗口。公开 API 没有直接随机访问多个历史快照的能力。

**必须区分两种用途：**

- **同参数、同输入重播：** 可撤销某段模拟并再算，适合作对照或投机输入回滚。但按这些因果状态转移函数，旧 Move 输出只依赖此前输入；重播相同序列，后来 Up 并不会反向改变旧 Move 结果。确定性一致是源码推断，本轮未运行 binary 比较。
- **新参数重播：** 不能通过 `Reset(newParams); Restore()` 接回旧状态，Reset 禁用原 Save（`O/stroke_modeler.cc:123-126`）。若坚持换参数，只能另一模型从 Down 全程重播，或 fork 扩充状态/参数 API。只用窗口首点作为新 Down 会将速度、a、wobble、预测和stylus历史归零，与全笔状态不等价。
- **修改窗口输入后重播：** Save/Restore 允许这个概念，但需要外部预处理先以终点/轨迹信息改写窗口输入；权重仍是自有算法给出的，库没有端点加权回溯。改写 input 路径还会改变 stylus 投影和模型半径来源，集成复杂度比仅修派生中心线更高。

#### 4.3 产品回滚和代价限制

- 当前主产品 Draw3 搜索未发现 `StrokeModeler::Save/Restore` 调用；仅隐藏测试内不相关 `matrixSnapshot.Restore()`。因此窗口回放不是现成生产路径。
- StrokeWidthEstimator 保存 currentDiameter/lastTime/lastPosition/hasSample/hasInputSpeed（`Draw3.InkPrediction.cppm:388-407`）；转换会推进它（`Draw3.StrokeGeometry.cpp:1404-1429,1663-1682`）。只回退 modeler 并重新 Convert 会重复推进半径状态，结果不可比。
- convertedResultCount、committedIndex、modelScratch 接纳、latestModeledResult、modelTimeOffset、terminalDisplayTime/terminalFirstPoint、宽度/真实速度采样状态都在产品层（`Draw3.InkPrediction.cppm:436-485`；`Draw3.DrawingController.cpp:9179-9244`）。回放只能覆盖未提交区间；Restore 不撤销 L1 或文档/历史事务。
- Save/Restore 会复制 deque 和 predictor；成本与各队列实际长度相关，非只拷贝一个 TipState。重播另增加窗口内每个 Input 的积分/upsampling 与投影成本。`max_outputs_per_call` 限制单次 Update，不自动限制一帧内多个重播 Update 的总量（`O/internal/position_modeler.cc:56-59`）。本轮未测性能或最大队列长度，不给出耗时承诺。

### 5. 库外可变 L0 尾段：不需要库改旧输出，需统一几何真值

#### 5.1 已有可复用的层次

`ActiveStroke` 已区分基准 modeledResults、确认 realPoints、prediction 以及派生 l0DrawPoints（`Draw3.InkPrediction.cppm:436-449`）。realPoints 是被接纳的模型点，不是原始设备轨迹。活动 L0 从 `committedIndex` 复制尾段再附 prediction（`Draw3.StrokeGeometry.cpp:1718-1738`）；完成尾段也从相同位置复制 realPoints，做 taper/tangency（`:490-504`）；Stored 拼接稳定前缀与完成尾段（`:520-531`）。

**设计推断：** 可让 modeler/原始 Result 不回写，另有有界修正尾段；用 baseline 位置、近期 raw 折线、稳定入口约束和真实 Up 生成新中心线。模型仅作为其中一个观测/先验，而不是最终几何的唯一权威。这完全不要求 public StrokeModeler 能改旧输出。

#### 5.2 不可只在 shader 或 l0DrawPoints 上动坐标

- 只改活动 l0DrawPoints 会在 `BuildCompletedPenTail` 重新读取未经修正 realPoints 时消失；Saved Stored 回到外偏路径（`:499,522-531`）。
- 若修正实时持续进行，稳定点离开修正窗口时 `CommitStablePrefixToL1` 目前仍从 realPoints 取点（`:1750-1753`）。必须让它提交同一份已确定修正几何；否则可见尾点跨到 L1 时跳回 baseline。
- 若选择只在 Up 修正未提交尾段，尚未修正的 L1 前缀可以保持，但修正入口必须接上真实已提交几何，尾段同时进入 completed 和 Stored。Up 一次形变有多大仍需验收，不能承诺不会抽动。
- 最终 Stored 必须先追加 Canvas/history，初次 L2 和后续重放共用这一对象（`Draw3.DrawingController.cpp:1833-1849`）；这使修正成为真正保存的中心线，而非显示遮盖。

#### 5.3 窗口与现有合同

- L1 `committedIndex` 只前移，连接点共享。不能改小于或等于已提交锚点的中心坐标；连接位置及进入方向应来自已提交前缀（`Draw3.StrokeGeometry.cpp:1741-1753,1784-1785`）。
- 当前选择 Normal/Fps120（`Draw3.InkPrediction.cppm:76-78`），配置 live-tail 为 55ms、Kalman beyond-input hint 为 1/60s（`Draw3.StrokeGeometry.cpp:247,255-262`）。实际 L1 保护时间为 live-tip + `GetPredictionDurationSeconds`（`:1746-1747`），后者是生成出的 prediction 时间范围，不是固定 1/60s。还应保留一个连接点（`:265-273`）。不能假定任何速度下都有“整段转弯”可重构。
- HardPen 保护没有 live-tip，仅可能有生成 prediction 的时间；endpoint admission 激活时暂停新增 L1（`Draw3.DrawingController.cpp:13264-13302`）。大窗口设计应定义自己的修正保护边界并覆盖各工具，不能默认把现有 taper 窗口加大就完成。
- 当前 terminalFirstPoint 禁止 Up 替换 pre-Up 确认点（`Draw3.InkPrediction.cppm:469-471`；`Draw3.StrokeGeometry.cpp:1454-1462`）。较大尾段方案会改变这条策略，应明确改为“允许重拟合未提交的 pre-Up 后缀，已提交锚点固定”，并同步相应规范/验收。研究阶段没有这项实施批准。
- 时间、硬件压力半径仍应独立保留；拟合中心线改变弧长，需要再从未经 taper 的半径生成笔锋，并重新检查胶囊公切线/dirty。只对 x/y 平滑也不能忽略最新弧长收锋的行为。

### 6. 对用户两个方向的具体取舍

| 方向 | API 结论 | 取舍（推断，未验收） |
| --- | --- | --- |
| 直接截掉弯折 | 库外几何即可，不需要 Reset 或改库 | 可以作为用户接受端点/长度误差后的可选策略或受限 fallback。但若截后又强制连接 raw Up，可能重新产生同一短回接；若不连接则修改精确终点合同。若外偏已发生在 pre-Up 或 L1，丢弃少数 Up 新点不足。 |
| 在大窗口提高终点权重 | 当前 Input 不支持权重；Save/Restore 也不提供回溯拟合 | 应将权重放在库外拟合目标。若精确 Up 是要求，末点用硬约束更清楚；高但有限权重只保证趋近，不保证到位。入口、切线、raw 走廊和位移上限应同时约束，不能只增强末点。 |
| 整体向圆周中心收缩 | 当前 wrapper 没有此动态力接口 | 对单向圆弧可作实验描述，但泛用实现宜依据局部 raw 走廊/法向误差，不必强行求唯一圆心；直线、S 弯、反向及自交没有稳定统一圆心，盲目收缩会改坏本来正确的段。 |
| 调 Kalman a/jerk 权重 | 仅影响 prediction | 可用于拆分预测跳变与真实外偏贡献，不应作为真实中心线修复主案。 |
| 调 spring/drag | 真实运动位置，但只能从 Reset/Down 配置生效 | 可作固定参数全笔 A/B；在线自适应需要 fork 或自有 stage，且只改变未来点，不能消除已提交外偏。 |
| 库外重建完整未提交后缀 | 不受“新输入不改旧 Result”的限制 | 本题优先考虑的集成方向：baseline 不变，修正输出统一送显示、完成、稳定提交和 Stored；以 raw-aware 约束减少运动外偏，Up 再对同一尾段收敛。需要有界近期 raw 缓存和明确可变窗口，收益仍待同轨迹对照。 |

这不等同重上历史撤回的仅 Up 新增 cubic：区别是修正对象包括尚未提交的 pre-Up 尾段，raw 信息参与约束，并在运动期管理窗口/连续性；完整算法方案由后续设计决定。

### 7. Related specs and external references

- 已读取 `.trellis/workflow.md`、当前 PRD、native/native-desktop 索引和上述相关规范；没有读取 implement/check jsonl。
- `.trellis/spec/native/runtime-and-rendering.md:417-429` 是停笔/Up 有界、模型与显示时间、未解决曲率问题的现行合同；`:424` 明确局部终态 cubic/末点到位不是完整修复证据。
- `.trellis/spec/native-desktop/input-and-ink.md:645-652` 是当前半径派生与一致性合同；新中心线算法将是另外的明确设计变更，不能包装为只调 taper。
- `.trellis/spec/native-desktop/draw3-integration.md:5-34` 及产品 `Inkeys/additional/ink_stroke_modeler/README.md:5-9` 说明 source repo revision `8d04529`、固定静态库与头文件边界。
- 本次无联网检索、无新上游版本事实；当前库公开状态/固定 HEAD 参见同任务 [upstream-status-and-diff.md](upstream-status-and-diff.md)，Google Ink 新算法参见 [google-ink-new-smoothing.md](google-ink-new-smoothing.md)。本报告 API 机制直接由本地代码确认，不推定 merge.lib 的实际来源完全一致。

## Caveats / Not Found

- 仅新增本任务 `research/wrapper-api-and-acceleration-options.md`。未改产品、测试、第三方、规范、既有任务、配置或其他研究者文档；未运行任何 Git 操作、构建、GUI/Computer Use 或 binary 轨迹测试。
- 本地原始 `.cc` 当前工作树可读取；产品链接固定 `merge.lib`，本轮没有验证其动态行为、符号/版本或重编 provenance。API 和实现事实分开列明，不以源码阅读等同产品 binary 验收。
- 没有完整设备曲率轨迹。末钩截断触发阈值、重拟合窗口长短、终点/切线权重、变形上限、速度预算都尚未选择和验证。
- Save/Restore 重播确定性、每帧总成本和三架构表现未执行；建议先用无窗口轨迹回放验证，不能用“接口可回退”推定现有产品已支持窗口回滚。
- 有界尾段方案受实际稳定锚点位置限制；若已把大段外偏提交到 L1，有限窗口无法修改那些点。必须先观测需要纠偏的区间是否仍在窗口内，再决定扩大保护范围或从运动期降低偏差。
