# Research: ink_stroke_modeler 停笔、过冲与抬笔终态语义

- Query: 分析 `ink_stroke_modeler` 的弹簧位置模型、Kalman/StrokeEnd prediction、同点 `kMove` 与 `kUp` 行为，解释停笔过冲折返和快速抬笔越过真实终点，并比较可行修复策略。
- Scope: internal（仓库 Draw3 集成 + provenance 指向的本地模型库源码）
- Date: 2026-09-20

## Files Found

- `Inkeys/additional/ink_stroke_modeler/README.md`：产品所链接模型库的来源、源 revision 与静态库集成方式。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.InkPrediction.cppm`：当前启用 Kalman prediction 和 120 FPS timing profile。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.InkPrediction.cpp`：spring/drag、Kalman、采样与 prediction 参数。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp`：真实输入、stationary `kMove`、`kUp`、prediction 和最终持久化的控制流。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.StrokeGeometry.cpp`：model result 到真实点/预测点/L0/L1/Stored Stroke 的转换，以及当前收敛判定。
- `inkStrokeModelerTestTests/contact_input_tests.cpp`：上一轮低速停笔回归测试及其尚未覆盖的过冲/折返边界。
- `D:/Project/Inkeys/inkStrokeModelerTest/Inkeys3-Draw3/inkStrokeModelerTest/additional/ink_stroke_modeler/stroke_modeler.cc`：模型库 `Update`、`Predict`、Move 和 Up 的实现。
- `D:/Project/Inkeys/inkStrokeModelerTest/Inkeys3-Draw3/inkStrokeModelerTest/additional/ink_stroke_modeler/internal/position_modeler.{h,cc}`：弹簧积分与仅供笔画结束使用的防过冲收敛。
- `D:/Project/Inkeys/inkStrokeModelerTest/Inkeys3-Draw3/inkStrokeModelerTest/additional/ink_stroke_modeler/internal/prediction/{kalman_predictor,stroke_end_predictor}.cc`：两种 prediction 的目标和边界。
- `D:/Project/Inkeys/inkStrokeModelerTest/Inkeys3-Draw3/inkStrokeModelerTest/additional/ink_stroke_modeler/internal/{wobble_smoother,stylus_state_modeler}.cc`：重复同点输入对 corrected anchor、Kalman 样本和 stylus 投影历史的影响。

## Findings

### 1. 已确认的模型事实

1. 产品使用固定版本静态库，当前 provenance 指向本地源 revision `8d04529`；模型 `.cc` 不参与产品工程现场编译。见 `Inkeys/additional/ink_stroke_modeler/README.md:5-9`。因此修改库算法意味着重新产出并同步 x86/x64/ARM64 静态库，不是普通 Draw3 源码小改。
2. 当前产品启用 `InkPredictionMode::Kalman` 和 `Fps120` profile，见 `Draw3.InkPrediction.cppm:75-78`。120 FPS profile 使用 `min_output_rate=360Hz`、`prediction_interval=1/60s`、wobble window `2.5/120s`、live tip `55ms`，见 `Draw3.StrokeGeometry.cpp:193-204`。
3. 位置模型是显式质量-弹簧-阻尼积分：`a=(anchor-position)/spring_mass_constant-drag*velocity`，随后依次积分 velocity 和 position，见外部源码 `position_modeler.cc:17-21,64-70`。Draw3 配置 `spring_mass_constant=11/32400`、`drag_constant=72`，见 `Draw3.InkPrediction.cpp:236`。
4. 由上述参数可推导自然频率约 `54.27 s^-1`，阻尼比约 `0.663`，小于 1，属于欠阻尼系统。**这是依据参数和方程的数学推导，不是库注释直接声明。** 在锚点突然停止、tip 仍带正速度时，越过锚点再回摆是该动力学的正常结果；残余速度越大，过冲可显著大于只按静态阶跃估计的幅度。
5. 普通 `kMove` 走 `ProcessMoveEvent`：先经 wobble smoother 得到 corrected position，再用 `UpdateAlongLinearPath` 推进 position modeler，并把 corrected position 送入 predictor，见外部 `stroke_modeler.cc:282-315`。`UpdateAlongLinearPath` 本身只是逐步调用普通 `Update`，没有过锚点检查，见外部 `position_modeler.h:54-78`。
6. 当前新增 stationary advance 正是每帧向普通 Move 路径发送最后 raw endpoint 的同点 `kMove`，见 `Draw3.DrawingController.cpp:6521-6554`。因此它解决了“模型不推进”，但会如实推进欠阻尼回摆。
7. `StrokeModeler::Predict` 使用 position modeler 当前状态的副本构建结果，不修改真实 position/stylus/loop 状态，见外部 `stroke_modeler.cc:160-186`。上一轮用同点 `kMove` 推进真实模型是必要的；仅重复 Predict 仍不能消除旧欠账。
8. Kalman predictor 有两个阶段：先用三次曲线把 modeled tip 接到 Kalman estimated state，再按 velocity/acceleration/jerk 向未来外推，见外部 `kalman_predictor.cc:109-124,147-228`。它的设计目的本来就允许越过最后 raw input；预测长度最多由 `prediction_interval` 和 confidence 控制，见 `kalman_predictor.cc:230-282`。所以“运动时前伸”不是异常，异常是已判断停笔或 Up 后仍把前伸/回摆纳入最终几何。
9. `StrokeEndPredictor` 不做未来延伸，而是在 position modeler 副本上调用 `ModelEndOfStroke` 快速追最后 input，见外部 `stroke_end_predictor.cc:28-46`。`ModelEndOfStroke` 会检测候选段是否越过锚点；越过时丢弃候选、时间步减半并重试，见外部 `position_modeler.h:82-123`。
10. 真正的 `kUp` 不是只调用上述安全收敛。`ProcessUpEvent` 先用普通 `UpdateAlongLinearPath(last corrected input -> raw Up)` 处理 Up 时间差，再调用 `ModelEndOfStroke`，见外部 `stroke_modeler.cc:233-260`。前半段没有防过冲，因此高速度/较大时间差时可以先输出越过 raw Up 的点；后半段只能保证自己追加的结束收敛候选不继续穿过锚点，不能撤销已经输出的过冲点。
11. Draw3 在 Up 后会清空 prediction，见 `Draw3.DrawingController.cpp:6882-6890`，但成功 `kUp` 的全部 modeled results 已由 `AppendRuntimeModeledPoints` 进入 `realPoints`，见 `Draw3.DrawingController.cpp:3105-3142` 或直接终态路径 `:3245-3260`。`AppendNewModeledPoints` 对普通 Pen 不做空间过滤，逐个把尚未转换的 Result 加入 `realPoints`，见 `Draw3.StrokeGeometry.cpp:864-915`。
12. 完成态 Pen 又直接从 `realPoints` 构造尾段并写入 Stored Stroke，见 `Draw3.StrokeGeometry.cpp:374-415`。因此快速 Up 后保留下来的“超过抬起点的大笔锋”在当前代码中主要是**真实 model output 被持久化**，不是旧 prediction 没清掉。
13. stationary 同点 Move 同时更新 wobble smoother、stylus raw polyline 和 Kalman predictor。wobble window 淘汰旧运动样本后 corrected anchor 才完全等于 raw endpoint；Kalman 也需要若干静止样本才衰减速度。这个过程有助于内部状态收敛，但不意味着这些内部过渡点都应成为可见/持久几何。

### 2. 两个用户现象的直接解释

#### 2.1 瞬间刹停：终点前冲后大折返

当前 stationary `kMove` 把 raw endpoint 固定为弹簧锚点，同时保留 tip 原有正速度。普通 Move 积分允许 tip 越过锚点；下一帧同点 Move 又让弹簧把已经越过的 tip 拉回。Draw3 无条件把两段 Result 依次追加为 `realPoints`，于是可见和最终几何成为“前冲 -> 回到停止点”的折线路径。

这不是 Kalman 单独造成的：Kalman prediction 可能让前伸更明显，但即使 prediction disabled，真实 position modeler 的同点 Move 仍能产生过冲和回摆。判断方法是看折返是否最终进入 Stored Stroke；按当前数据流，它会。

#### 2.2 快速抬笔：笔锋超过真实 Up

快速 Up 时，`ProcessUpEvent` 的第一段普通 `UpdateAlongLinearPath` 继承高速度并推进到 Up 时间。它没有 `ModelEndOfStroke` 的 crossing guard，所以能先生成 raw Up 前方的真实 Result。Draw3 随后清 prediction，却仍将这些 Result 保存为 real/stored points。因此只“Up 时清 prediction”不足以解决。

### 3. 现有测试为什么没有捕获

`TestLowSpeedStopConvergence` 只断言 stationary advance 最终满足 position/velocity gate、输出数量有界，见 `contact_input_tests.cpp:1913-1936`；它没有断言到 raw endpoint 的距离单调下降、没有越过 endpoint plane，也没有断言路径方向不折返。

同一测试只在**先完全停稳之后**发送同坐标 Up，并检查最终点距 endpoint `<=0.05px`、半径变化 `<=0.02px`，见 `contact_input_tests.cpp:1997-2022`。这绕开了“高速 Move 后立即 Up”的初速度场景，也不会发现中间已经出现并被持久化的越界点。

### 4. 可选修复策略比较

| 策略 | 能否解决停笔折返 | 能否解决快速 Up 越界 | 延迟/连续性 | 点数与几何副作用 | 结论 |
|---|---|---|---|---|---|
| 全局关闭 prediction | 否；真实 spring 仍回摆 | 否；Up 普通积分仍可越界 | 明显增加活动绘制延迟 | 点少，但问题仍在 | 不采用 |
| 全局改用 StrokeEnd predictor | 只抑制预测前伸，真实 stationary Move 仍回摆 | Up real output 仍可能越界 | 活动时不再未来预测，跟手性下降 | 中等 | 不采用 |
| 调大 drag/改 spring 为临界阻尼 | 可降低但不形成严格边界 | 可降低但不保证 Up 不越界 | 改变整条笔迹动态，可能增加 lag/依赖 prediction | 所有设备/速度回归面很大 | 不作为首选 |
| stationary 输入使用“反向虚拟锚点”主动制动 | 理论可做临界制动 | Up 仍需另修 | 参数敏感；虚拟位置会污染 wobble/stylus/Kalman 输入 | 容易产生新的弯折或状态偏移 | 不采用 |
| 只在 L0/renderer 裁剪 | 暂时可隐藏 | 完成态仍越界 | 活动视觉可能较好 | L1/Stored Stroke 仍保留坏几何，重放后复现 | 不采用 |
| Up 时只强制最后一个点等于 raw Up | 停笔折返不修 | 只能修最终坐标，前面的越界点仍在 | 尾部仍可能“出去再回来” | 持久路径仍有 loop | 不足 |
| 停笔时提前 `kUp`，恢复移动时 Reset/新建 modeler | 能利用库内 end guard | 可一起处理 | predictor、压感和笔宽历史断裂，恢复移动易有接缝 | 生命周期复杂 | 不采用 |
| 修改/重编模型库，为普通 Move/Up 增加全局 crossing clamp | 可彻底改变底层行为 | 可修 | 影响所有调用方；三架构静态库发布与兼容风险高 | 需要上游式验证 | 只作长期库能力方案 |
| **Draw3 双通道：内部继续收敛，几何按 endpoint 单调门禁接纳** | **是** | **是** | 运动期 Kalman 保留；停止期最多一帧切换到收敛视觉 | 模型 scratch 有界，shader/Stored 不接纳回摆 | **推荐** |

### 5. 推荐的体验契约

核心原则是把“推进模型内部状态”和“允许该输出成为几何”分开，不能再把 stationary/terminal Update 的所有 Result 自动视作真实路径。

#### 5.1 活动运动期

- 有真实 Move 时继续使用现有 Kalman prediction，保留低延迟手感；不全局调 spring/drag，不全局关闭 prediction。
- 最新 raw endpoint 仍是输入真值，prediction 只留在 L0，不进入 Stored Stroke。

#### 5.2 停笔收敛期

- 第一帧没有真实 model input 时进入 `EndpointSettling`，冻结一个 `stopAnchor=lastModelSnapshot.position` 和最后一条非退化真实输入方向。
- 继续按帧向 `StrokeModeler` 发送同点 `kMove`，但把输出写入小型 scratch；这样 wobble、position state 和 Kalman state 继续衰减，解决原始“没追上”的问题。
- stationary scratch 通过“端点单调门禁”后才可成为几何：
  1. 到 `stopAnchor` 的距离不得比上一接纳点增加超过 `0.05px`；
  2. 沿最后真实方向不得越过 anchor plane 超过 `0.05px`；
  3. 一旦候选进入 `0.05px`、越过 anchor plane 或首次不再接近，追加**恰好一个** raw anchor（半径沿用最后有效宽度状态），进入 `visualPinned`；本次及后续回摆 Result 只更新内部状态，不再追加 geometry。
- 进入 settling 后停止展示 Kalman 的未来延伸；可以展示门禁后的 modeled catch-up，或者直接把 prediction 限制为同一 raw anchor。否则 internal geometry 虽被过滤，Kalman cubic 仍可能画出前伸再回缩。
- `visualPinned` 后 shader/L0/L1/Stored 点数保持不变，但后台同点 Update 继续到当前 position+velocity 收敛门槛；之后再沿用三帧视觉稳定并 `idleFrozen`。这样不会为了内部阻尼过程生成重复 shader 点。
- 若 pinned 期间很快恢复真实 Move，不 Reset modeler；首批恢复输出继续通过“从 pinned anchor 朝新 raw endpoint 单调前进”的门禁，直到重新进入正常运动走廊，避免把尚未完全衰减的内部回摆重新显露出来。

#### 5.3 物理 Up

- 仍向 modeler 发送 `kUp` 以维持库生命周期和错误语义，但 terminal output 应进入 scratch，不再直接全量追加 `realPoints`。
- 对 `preUpAcceptedTip -> rawUp` 应用相同的单调/stop-plane 门禁；任何越过 raw Up 的候选及其后续返回段全部拒绝。
- 最终几何必须以 raw Up 为权威：若最后接纳点不在 `0.05px` 内，追加一个 raw Up 点；若已经同位则不新增重复点。这样完成态最后一点和整段 terminal tail 都不会超过抬起位置。
- Up 后继续清空 prediction；Stored Stroke 只能使用经过门禁的 real geometry。Cancelled 保持既有“不持久化”语义。

#### 5.4 为什么这比“直接裁掉最后几个点”好

- 只裁最后一点不能删除“先越界、再折返”的中间 loop。
- 只按离 raw endpoint 的圆形半径裁剪可能允许候选绕着 endpoint 横向摆动；stop plane + 距离单调同时约束前向越界和回摆。
- 内部 modeler 仍真实收敛，后续 Move 不会恢复到上一轮完全未推进的旧状态；可见 geometry 又不必忠实呈现弹簧内部的非物理回摆。
- 不改模型参数和静态库，风险集中在 Draw3 的 idle/terminal Result 接纳边界，可在测试宿主与产品同构实现。

### 6. 建议的可测阈值与验收矩阵

建议复用现有视觉容差，不新增难以解释的时间魔数：

- endpoint position epsilon：`0.05px`。
- endpoint plane overshoot allowance：`0.05px`（只作 float/离散采样容差）。
- distance monotonic allowance：相邻接纳点距 anchor 最多增加 `0.05px`。
- internal settle：继续使用 `endpointError <=0.05px && |velocity|/target_fps <=0.05px`。
- visual settle：继续使用 position `0.05px`、radius `0.02px`、连续 3 帧。
- point budget：`visualPinned` 后 geometry/shader/Stored 候选点数严格不增长；每次 Up 最多额外追加 1 个 raw endpoint。
- 时间安全阈值只用于测试/诊断，不作为冻结条件：120 FPS 默认参数下应在 `<=200ms` 内进入 internal settled；超出时记录/失败测试，而不是继续无界产点。

必须新增以下模型级测试：

1. 直线低速/中速/高速 Move 后无新输入：接纳点到 raw endpoint 的距离单调不增，任何点不越过 endpoint plane `0.05px`，路径没有前进后反向的 terminal segment。
2. 高速 Move 后立即同位 Up：完整 terminal accepted points 均不超过 raw Up plane，最后一点距 raw Up `<=0.05px`，Stored Stroke 同样满足，而不是只检查 `back()`。
3. 高速 Move 后 Up 坐标再前进少量：允许补齐到新的 raw Up，但仍不越界、不折返。
4. 停笔后 pinned、内部仍在衰减时立即恢复 Move：第一批可见点不得先向旧方向折返；最终正常恢复 Kalman prediction。
5. prediction Kalman/StrokeEnd/Disabled 三模式：运动期只有 Kalman 可前伸；settling/Up 三者都服从相同 raw endpoint 边界。
6. 30/60/120/240 FPS、MouseLeft/MouseRight、Pen/HardPen；硬件压感还要确认 raw Up 压力变化只影响 endpoint radius，不改变位置边界。
7. pinned 后持续 10 秒：model scratch、realPoints、L0/L1/shader input 和 Stored candidate 均有界；内部 settled 后停止 model Update。
8. 曲线/急转后停：以最后非退化真实方向构造 stop plane，同时检查 anchor distance monotonic，避免仅适配水平直线。

## Code Patterns

- **不应继续复用的模式**：`stationary Update -> AppendRuntimeModeledPoints(all results)`；当前位置见 `Draw3.DrawingController.cpp:6547-6554`，无条件转换见 `Draw3.StrokeGeometry.cpp:864-915`。
- **可复用的库模式**：`ModelEndOfStroke` 的“越过锚点则丢弃候选并减半时间步”，见外部 `position_modeler.h:82-123`。Draw3 不必复制其时间积分，但应复用其“不把 crossing candidate 当几何”的边界思想。
- **应保留的现有模式**：prediction 只进 L0，Up 后清 prediction，见 `Draw3.StrokeGeometry.cpp:918-969` 与 `Draw3.DrawingController.cpp:6882-6890`。
- **应扩展的现有模式**：`IsModeledTipSettled` 的 position+velocity gate，见 `Draw3.StrokeGeometry.cpp:807-845`；它继续决定何时停止内部 advance，但不再直接决定哪些 Result 可见。

## External References

- 本次未使用网络资料。模型实现依据项目 provenance 指向的本地源仓库：`D:/Project/Inkeys/inkStrokeModelerTest/Inkeys3-Draw3`，声明 revision 为 `8d04529`。
- 数学上的“欠阻尼”判断由仓库参数和 spring 方程推导；不是对 Google 上游其他版本行为的假设。

## Related Specs

- `.trellis/spec/native/runtime-and-rendering.md:394-455`：现有“普通笔停笔模型收敛与冻结”合同；需要在后续规划中补充“内部收敛与几何接纳分离”、endpoint 单调门禁和 raw Up 权威终点。
- `.trellis/spec/native/runtime-and-rendering.md:127-142`：Shape 已采用 raw Up 强制覆盖终点，可作为“输入终态是真值”的同层先例，但普通 Pen 不能简单复制 Shape 的双点实现。
- `.trellis/tasks/09-19-draw3-low-speed-stop-convergence/prd.md`：上一轮要求 stationary Update 全量追加的表述需调整为“内部必须推进，几何只接纳无过冲的单调前缀”。

## Caveats / Not Found

- 产品链接的是固定静态库；本次读取的是 provenance 声明对应的本地源码，没有对 `.lib` 做反汇编或逐符号二进制一致性验证。
- 本次是源码语义调查，没有运行可见 GUI、真实鼠标/笔或 D3D Debug Layer；过冲幅度取决于实际输入速度、QPC 间隔和设备采样率，需在实现前先用模型级轨迹输出量化。
- 本文件没有执行 Git 历史比较；历史回归点由同任务的另一份 Git 调查负责合并判断。
- 推荐方案要求新增一个明确的 settling/visual-pinned 几何门禁状态；具体字段布局与 helper 命名留给设计阶段，不在本研究中修改产品或测试代码。
