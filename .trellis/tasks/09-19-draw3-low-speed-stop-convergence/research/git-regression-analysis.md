# Research: Draw3 停笔折返与快速抬笔突出的 Git 回归分析

- Query: 找出 Draw3 独立阶段、RTS 多 contact 迁移、Inkeys3 合并与当前实现之间的 prediction / idle freeze / pointer Up / pen-tip geometry 差异，判断“瞬停后先过冲再折返”和“快速抬笔超出抬起点”的引入风险。
- Scope: internal
- Date: 2026-09-20

## Findings

### 1. 来源快照与模型库没有在 Inkeys3 合并时被替换

**事实**

- Draw3 产品集成规范记录的来源是 `Alan-CRL/Inkeys3-Draw3@8d045298eaaac76f752b4f8b5f3303b3520e50b7`，产品使用该快照的固定模型库（`.trellis/spec/native-desktop/draw3-integration.md`）。
- 当前仓库与本机来源仓库 `D:\Project\Inkeys\inkStrokeModelerTest\Inkeys3-Draw3` 的三个 `ink_stroke_modeler_merge.lib` SHA-256 完全一致：
  - ARM64: `30C04541061EBBA69F8A347FEEB92B812DE72F964481B77A9087289C5C6F6F0D`
  - x64: `7C12BC222729589E3040E3B5EA21605AE2D49C5EA15A28F0D969DE3DDCB84819`
  - Win32: `58679FE5C95A3108A6BA24539D15FCE9208F4C09C3A27FD4781F02C59F799D3A`
- `8d045298` 的 `ink_prediction.cpp` / `stroke_geometry.cpp` 与 `fc9a8b86` 首次产品集成版相比，除 module/namespace 改名和 pragma 移动外没有算法差异。`drawing_controller.cpp` 的差异主要是产品颜色/宽度快照、observer 和删除诊断 HUD；针对 modeler / kUp / completed-tail 的 diff 没有普通笔语义变化。
- 当前模型参数仍由初始 Draw3 提交 `47f3b4c2` 引入：`spring_mass_constant = 11/32400`、`drag_constant = 72`、`end_of_stroke_stopping_distance = 0.001`、`end_of_stroke_max_iterations = 20`（`inkStrokeModelerTest/draw3/ink_prediction.cpp:226-239`）。Git blame 未显示合并后更改这些值。

**结论**

`fc9a8b86 feat(draw3): integrate Draw3 drawpad runtime` 不是这两个现象的模型库版本回归点。如果产品手感在合并前后有差异，更可能来自输入/停止/完成态如何驱动同一个模型，而不是固定库被替换。

### 2. 独立 Draw3 早期鼠标循环与 RTS 路径的关键差异

**事实**

- `f9c9ee70^` 的旧 `DrawMouseStroke` 在鼠标按住期间，只要没有 `idleFrozen`，每帧都会以当前鼠标坐标和增长时间调用 `modeler.Update(kMove)`，即使 raw 坐标未变（`f9c9ee70^:inkStrokeModelerTest/draw3/drawing_controller.cpp:148-210`）。
- 该旧路径在检测到鼠标键抬起后直接退出循环，没有再向 modeler 发送 `kUp`；完成态直接把最后可见 `l0DrawPoints` 烘入 L1/L2（同文件 `:268-328`）。
- `f9c9ee70 Add RTS multi-contact input and rendering` 删除了这个单笔鼠标循环，改为只在 mailbox sequence 变化时调用 modeler，并将 RTS 终态坐标作为 `Input::EventType::kUp`。这次迁移同时引入了两个后续风险：
  1. 无新 snapshot 时不再推进模型，是上一轮“笔锋追不上”的直接根因。
  2. 完成态开始接受 `kUp` 生成的 modeled tail，不再与旧鼠标路径的“烘干抬笔前最后可见 L0”等价。

**结论**

如果用户所说的“Draw3 还没合并进 Inkeys3”指早期单鼠标循环，那么它的确有两个同时有利于当前体验的性质：按住停止时每帧追踪，而物理 Up 时不再追加一段 `kUp` 模型几何。RTS 迁移只保留了后者的输入精度，没有保留前者的可见终点策略。

### 3. Git 历史已经曾明确识别“kUp 尾段重连”会产生折返/双束

**事实**

- `a40801a5 Freeze last visible L0 on Up; avoid kUp reconnection` 的提交说明直接写明了“forbid reconnecting tails using kUp smoothing”和“preventing foldback/double-tail artifacts”。其实现在 Pen 完成时优先烘干 `previousL0DrawPoints`，不使用 `kUp` 的平滑尾段（`a40801a5:inkStrokeModelerTest/draw3/drawing_controller.cpp:121-180`）。
- `3d52b33d Add retainPredictionOnUp option for pen tail` 随后把这一策略变成开关，且默认 `false`：
  - `false` 默认改回使用 model `kUp` 真实尾段并清理 prediction。
  - `true` 才保留抬笔前最后可见 L0。
  - 实现位于 `3d52b33d:inkStrokeModelerTest/draw3/ink_prediction.cpp:552-570`。
- `b7af67fd fix: restore pen live tip and freeze tip on up` 又在默认 real tail 上应用 live-tip taper 和胶囊公切线约束（`b7af67fd:inkStrokeModelerTest/draw3/ink_prediction.cpp:1203-1226`）。这改善了抬笔时笔锋消失，但没有限制 completed centerline 相对 raw Up endpoint 的轴向突出。
- `1cc746e7` 引入 Stored Stroke 后，完成态被固定为“仅从 `realPoints` 生成，prediction 永远不进持久 Stroke”。当前 `BuildCompletedPenTail` / `FinalizeStoredStroke` 仍是这个契约（`Inkeys/Inkeys/Drawing/Draw3/Draw3.StrokeGeometry.cpp:374-438`）。

**结论**

“快速抬笔后出现大笔锋/突出”不应归因于 prediction 被持久；当前完成态确实会清除 prediction。更准确的风险是 `kUp` 生成的 modeled results 被追加进 `realPoints`，然后未经 raw-endpoint 轨迹约束就进入 completed taper 和 Stored Stroke。`a40801a5` 是已有的历史证据，证明同类终态尾段曾真实引发折返/双束；`3d52b33d` 把风险路径恢复为默认。

### 4. 当前两个现象的具体数据路径

#### 4.1 瞬停后先前冲再折返

**事实**

- `23dcb728` 在无新真实输入时，按帧以 raw endpoint 作为固定 anchor 发送合成 `kMove`，并把每次输出追加为可见 `realPoints`（`Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:6521-6557`）。
- 模型的 `PositionModeler::Update` 是显式弹簧-阻尼积分：先以 `(anchor-position)/mass - drag*velocity` 更新速度，再用新速度更新位置（`13e81fa0^:inkStrokeModelerTest/additional/ink_stroke_modeler/internal/position_modeler.cc:12-56`）。
- 普通 `kMove` 经 `ProcessMoveEvent -> UpdateAlongLinearPath`，不检查是否穿过 anchor（`13e81fa0^:.../stroke_modeler.cc:282-318`；`.../position_modeler.h:70-79`）。
- 当前停笔收敛判定只检查“最后 modeled position 距 raw endpoint <= 0.05px”和“速度折算下一帧位移 <= 0.05px”（`Inkeys/Inkeys/Drawing/Draw3/Draw3.StrokeGeometry.cpp:807-825`）。它能保证最后稳定，但不能保证收敛路径不穿过 raw endpoint。

**推断（高置信）**

`23dcb728` 没有创造弹簧模型的欠阻尼特性，但它把每次 stationary `kMove` 的全部输出变成了可见中心线；因此它会暴露“穿过固定 anchor -> 向回加速 -> 折返”。上一轮的提前冻结恰好遮蔽了这段轨迹；修复“追不上”后，需要额外的 endpoint monotonicity 契约，不能只加最终 settled gate。

#### 4.2 快速运动时 Up 后突出抬起点

**事实**

- `completeModelUp` 把 raw Up 坐标传入 `kUp`，成功后立即 `AppendRuntimeModeledPoints`（`Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:3105-3162`）。该路径没有保留“本次 Up 追加前的 realPoints 边界”，也没有对新点做 raw endpoint 投影或折返检查。
- modeler 的 `ProcessUpEvent` 分两段：
  1. 先从上一个 corrected input 到 raw Up 做 `UpdateAlongLinearPath`。
  2. 再调用 `ModelEndOfStroke`反复以最终 anchor 追赶（`13e81fa0^:.../stroke_modeler.cc:233-279`）。
- `ModelEndOfStroke` 的确会检测 candidate segment 是否穿过 anchor，若穿过则回退状态并把步长减半（`inkStrokeModelerTest/additional/ink_stroke_modeler/internal/position_modeler.h:82-126`）。
- 但该保护只覆盖第 2 段，不会删除第 1 段 `UpdateAlongLinearPath` 已经生成的 tip states。而 completed stroke 会把所有 `realPoints` 重新组装成 Stored Stroke（`Draw3.StrokeGeometry.cpp:374-438`）。
- `23dcb728` 新增的模型测试只断言 `kUp` **最后一点**距 raw endpoint <= 0.05px，没有检查 Up 新增点列的最大前向投影、折返或完成几何的可见 bounds（`inkStrokeModelerTestTests/contact_input_tests.cpp:2012-2022`）。

**推断（中高置信）**

快速 Up 问题的风险源不是 Kalman prediction 被保存，而是 `kUp` 第一段常规弹簧更新产生的中间 modeled points，以及最后 taper/capsule 的可见半径。现有“最后一点到位”测试不能排除中间先突出、再回到 endpoint 的大笔锋。

### 5. 哪些提交是回归风险点

| Commit | 已证明的变化 | 与当前现象的关系 |
|---|---|---|
| `47f3b4c2` | 引入现有弹簧/阻尼参数和 Kalman 配置 | 建立欠阻尼运动的基础，但不是近期回归 |
| `f9c9ee70` | 单鼠标每帧 `kMove` 改为 RTS snapshot 消费；引入 raw `kUp` | 同时引入“停笔不推进”与 `kUp` modeled-tail 风险 |
| `a40801a5` | 禁止用 `kUp` smoothing 重连尾段，烘干最后可见 L0 | 历史上已直接修复 foldback/double-tail |
| `3d52b33d` | 默认改回 model `kUp` tail，保留 L0 变为非默认开关 | **快速 Up 突出的主要历史风险点** |
| `b7af67fd` | completed real tail 加 taper 和 capsule tangency | 不创造 centerline 过冲，但会影响突出的视觉大小 |
| `1cc746e7` | completion 改为 Stored Stroke，只保存 real points | 排除“prediction 直接持久”，但会持久 `kUp` modeled points |
| `fc9a8b86` | 把 `8d045298` 快照整合进 Inkeys3 | 核心笔模型/几何无算法差异，**不支持它是直接回归点** |
| `23dcb728` | 恢复静止 `kMove` 追赶，直到 position+velocity settled | **停笔折返的暴露点**；修复旧欠账同时缺少 anti-overshoot 可见轨迹约束 |

### 6. 推荐的修复方向（本文不修改代码）

#### 不建议作为首选：全局调弹簧/阻尼或缩短预测时间

- 现有参数从 `47f3b4c2` 延续至今，不是 Inkeys3 合并变化；改参数会同时改变整段运动延迟、转弯手感、全部设备和 prediction，回归面过大。
- 只降低 prediction 不会消除 stationary `kMove` 自身的弹簧穿越，也不会约束 `kUp` 输出的 real points。

#### 建议方案：把 raw endpoint 升级为停笔/完成态的几何不变量

1. **Idle 收敛轨迹要“单调接近”，不只要求最后 settled。**
   - 以最后真实 raw endpoint 和 stationary 开始时的 modeled tip 定义 approach axis。
   - 新的 idle-only modeled point 一旦投影越过 raw endpoint，或在已达到最近距离后又离 endpoint 更远，不得继续作为可见 centerline 追加。
   - 在最近点与 raw endpoint 之间追加/替换一个精确 endpoint 点，使可见笔迹不会先越过再折返。
   - 不能只在 shader 前隐藏过冲点：否则 modeler 仍保留反向速度，下一次 Move 可能再把旧动量甩出。当 endpoint 被接管为静止锚点时，应同步把模型重锚定为该点的零速状态（例如在同一 ActiveStroke 内 reset + synthetic Down），同时保留已生成的 Stroke 几何与宽度估计状态。这样停稳后的新移动自然从静止开始，不背旧 prediction/弹簧债务。

2. **Up 完成几何必须以 raw Up 点为 centerline 终点。**
   - 在调用 `kUp` 前记录 real-point 边界，只对该次 Up 新增的 modeled tail 做终点约束。
   - 截断首个超过 raw Up 投影或开始远离 raw Up 的候选；最后强制追加/替换精确 raw Up center point。
   - 宽度仍可从最后有效 modeled/stylus 状态继承，然后使用现有 completed taper + tangency；不保留 prediction，也不把整段最后可见 prediction L0 盲目烘干。
   - 对 SoftPen，可见圆头会自然超出 center point 一个小半径；验收应分开“centerline 不超出 raw Up”和“最终 taper 半径不形成肉眼可见大包”两个门槛。HardPen 无 taper，则允许半笔宽的正常圆 cap，但 centerline 仍必须以 raw Up 结束。

3. **保留 `23dcb728` 的“真实追上后再冻结”，但把 settled 定义扩展为状态+轨迹契约。**
   - position error + velocity 门槛仍有价值，用于确认到位。
   - 新增“未穿过 endpoint / 未出现反向折返”的轨迹不变量。
   - endpoint 重锚定后立即停止 stationary Update，再用现有 3 帧 L0 稳定门槛确认笔宽/taper，仍能满足重复点有界要求。

### 7. 必须补的回归验证

- 使用真实 `StrokeModeler`在30/60/120/240 FPS 和 prediction 开/关下构造高速直线 -> 同坐标 stationary moves；断言新增可见 center points 对 approach axis 的投影不超过 raw endpoint，距 endpoint 不在进入最近点后增大，且终点精确到位。
- 与上述轨迹相同的快速 `kUp`：记录 Up 前点数，遍历 Up 新增点列，断言最大前向投影、最大 endpoint distance-after-closest 和折返角度均在可见容差内；Stored Stroke 最后 center 必须是 raw Up。
- 对 SoftPen 单独断言 completed taper 的最终半径，对 HardPen 断言只存在正常圆 cap 而无 centerline 外延。
- 停稳后继续按住数百帧，modeled/real/L0/shader 输入点数恒定；再移动时首帧不释放旧速度或旧等待距离。
- 增加一个与 `a40801a5` 问题同构的反折返测试：稳定前缀 + live tail + Up tail 不能出现双束或回接。

## Files Found

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` — 产品 stationary advance、raw snapshot、`completeModelUp` 和 Stored Stroke 提交。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.StrokeGeometry.cpp` — `BuildCompletedPenTail`、`FinalizeStoredStroke`、L0 taper、idle settled/freeze。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.InkPrediction.cpp` — 当前 spring/drag、sampling 和 Kalman 参数。
- `inkStrokeModelerTest/draw3/*` — 与产品同构的确定性宿主，可用于增加轨迹回归。
- `inkStrokeModelerTest/additional/ink_stroke_modeler/internal/position_modeler.h` — 弹簧 position modeler 与 `ModelEndOfStroke` 过冲回退契约。
- `inkStrokeModelerTestTests/contact_input_tests.cpp` — 当前只检查 settled 和 Up 最后点，缺少整段轨迹约束。
- `.trellis/tasks/archive/2026-08/draw3-source/active/07-18-rts-multicontact-rendering/*` — RTS 迁移设计与已知 Up foldback/double-tail 修复历史。
- `.trellis/tasks/archive/2026-08/draw3-source/archive/2026-08/08-02-pen-live-tip-policy/*` — completed tip/taper 及抬笔定住的原始设计意图。

## External References

- 未使用外部网页。预测库是仓库内锁定快照/静态库，本次以同版本源码、头文件、Git 历史和二进制 SHA-256 为证据。

## Related Specs

- `.trellis/spec/native/runtime-and-rendering.md` — 活动 contact 停笔预测、L0/L1/L2、完成态与重复点边界。
- `.trellis/spec/native-desktop/draw3-integration.md` — Draw3 来源快照、固定模型库 ABI 和产品迁移边界。
- `.trellis/tasks/09-19-draw3-low-speed-stop-convergence/prd.md` — 上一轮“追上后再冻结”要求。
- `.trellis/tasks/09-19-draw3-low-speed-stop-convergence/design.md` — `RawMoved -> SettlingModel -> StabilizingVisuals -> Frozen` 状态机；需增补 endpoint-monotonic/re-anchor 约束。

## Caveats / Not Found

- 未运行可见 GUI 或真实鼠标手感测试；折返的“大”和笔锋视觉大小尚无像素量化录像。
- “瞬停折返由 stationary `kMove` 穿过 raw anchor 造成”是基于确定数据路径和弹簧代码的高置信推断；还需上述轨迹回归测试输出每点投影才能定量确认。
- “快速 Up 突出”可能同时包含 centerline modeled excursion 和最终 capsule/taper radius 两部分；未有用户截图/轨迹日志时不能断定两者的占比。
- `ModelEndOfStroke` 对它自己的反复追赶阶段有过冲回退；不应笼统描述为“整个 kUp 都没防过冲”。未受保护的是 `ProcessUpEvent` 前置 `UpdateAlongLinearPath` 已输出的 states，以及应用层对完整 Up tail 缺少 raw endpoint 不变量。
