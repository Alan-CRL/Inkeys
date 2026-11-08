> 本轮实现与无窗口验证已完成，结果和环境问题处理见 [validation.md](validation.md)。用户真机手感及后续屏幕尺度仍待进行；未提交、未归档。

> 当前实施以 [implementation-scope.md](implementation-scope.md) 为准：用户已批准本轮修复鼠标/触摸手感并接通诊断，屏幕尺度延后；以下阶段1及先前调查记录保留为历史。

> 2026-11-08 续接调查：阶段1已提交为 `7b213d90`；用户反馈手感退化。本轮仅调查与规划，新的候选目标、证据、实施顺序及验收见 [repair-plan.md](repair-plan.md)。下文保留阶段1历史合同；原S6及新修复均须先评审，不据 task 的 in_progress 状态自动实施。

# Technical Design

## 总览与边界

- **只替换动力学内核**：`Draw3.CanvasNavigation.cppm/.cpp` 内的 `CanvasPanDynamics` 及其依赖类型。输入路由、触点快照消费、视口应用块、背景跟随、tile 预测、笔仲裁骨架保持不变；`CanvasPanFrameInput/Output`、`Step/Stop/ClearAxis/Phase/Active/ContentVelocity` 接口尽量保持，以最小化 `Draw3.DrawingController.cpp` 的改动。
- 事实依据：`research/chromium-fling-model.md`（常量与行为）、前置任务 `11-06-whiteboard-canvas-pan-inertia` 的 design/research、2026-11-08 调查结论（见 prd Background）。
- 单位：全程 **DIP**（控制器已用 `SpeedEraser::DisplayScale.dipPerPixelX/Y` 在入口/出口换算）。Chromium 在 DIP 空间运行，阈值（15 / 30 / 150 / 350 / 17000 / 2300）直接沿用。ppi 固定取 96（本任务 DIP 适配）。源码核对：`FlingController::UpdateCurrentFlingState` 从 `scheduler_client_->GetPixelsPerInch(global_point)` 取得 ppi，从 `event_sender_client_->GetRootWidgetViewportSize()` 取得根 widget 视口，再原样传入曲线；`gesture_provider.cc` 不提供 ppi。本地副本未含平台实现，后续补查 raw 源码确认 Windows scheduler 委托 ScreenWin，触摸设备可用时从 HIMETRIC/屏幕矩形计算物理 PPI，失败回退96（详情与原始链接见下）；因此 Chromium Windows 的该参数并非恒96。阶段1遵从任务默认继续固定96，未接入硬件PPI，也不据此改其它单位结论。
- 符号约定：本模块沿用现有"内容跟随手指"约定——`contentDelta` 与手指位移同向；Chromium 内部 `scroll = last_focus − focus` 与 fling 速度符号相反/同向的细节，移植时在边界统一转换并加测试，避免符号错误。

## 1. 模块划分（均在 canvas_navigation 模块内，纯 CPU、可无窗口测试）

| 组件 | 对应 Chromium | 职责 |
|---|---|---|
| `CanvasPanVelocityTracker` | `VelocityTracker`（LSQ2） | **单个多指对象**，内含每指历史；移植 `AddMovement`/`GetEstimator`（100 ms 窗口、20 环、40 ms 清窗、UP 80 ms 清窗、度数=min(2,m−1)、QR 求解）。不能拆成每触点一个独立对象：Chromium 的 `Clear()`、40 ms 断口与 id_bits 回溯语义是跨指的。 |
| `CanvasPanGestureDetector` | `GestureDetector` + `GestureProvider` 的滚动/fling 部分 | focus、slop、POINTER_DOWN/UP 基线、scroll 派发、UP 的 fling 判定；产出 ScrollBegin/Update/FlingStart/ScrollEnd 事件序列供 booster 与 controller 使用。 |
| `CanvasPanFlingBooster` | `FlingBooster` | 逐条移植，时间用 QPC 换算秒。 |
| `CanvasPanFlingCurve` | `PhysicsBasedFlingCurve` + `CubicBezier` + 旧 `FlingCurve` | 曲线开关 `kCanvasPanFlingCurveKind`（默认 PhysicsBased，可切 Legacy）；`CubicBezier::Solve` 须与 Chromium 一致（Newton + 二分，含 epsilon）。 |
| `CanvasPanFlingController` | `FlingController` 的 Start/Cancel/Progress | `start_time`、两帧回拨规则、delta=offset−cumulative、0.1 阈值、`ObserveProgressFling`、结束重置。 |
| `CanvasPanDynamics`（保留名） | — | 对外门面：把每帧的触点样本合成为 MotionEvent 序列，驱动上述组件，输出 `CanvasPanFrameOutput`。 |

删除：`Slipping`、手/自由摩擦与 `StepCanvasPanFriction`/`CanvasPanGlideDistance`、`CanvasPanReleaseGate`、手静止判定、旧 tracker 的 3 样本/20 ms/2 ms 合并规则及相关常量；对应旧测试重写。

## 2. 输入合成（每帧快照 → MotionEvent）

- 输入仍是 `CanvasPanFrameInput.contacts`（每触点 Down/Move/Up/Cancelled + QPC）。门面按 QPC 升序处理，同 QPC 以 Down < Move < Up 稳定排序。
- Down：第一指 = DOWN，其后 = POINTER_DOWN。Up：最后一指 = UP，其余 = POINTER_UP。Cancelled：见 §3。
- Move：严格 QPC 排序优先，以拓扑事件（Down/Up/Cancelled）为边界；边界之间的连续 Move 样本合并为一个 MOVE（时间戳取该段最新样本，所有按住触点取各自最新位置），在处理边界前先 flush。Move→Down→Move、Move→Up→Move 因而各有两个 MOVE，不跨拓扑合并。Chromium 的 LSQ 回溯在遇到不含该 id 的 movement 时会中断，单触点样本不能各自成一个 movement。阶段 2 有批内多样本时，按样本时间聚类，每簇一个 MOVE，需实测"其他触点位置沿用"带来的偏差并记录。
- 倒退/重复 QPC、非有限坐标：丢弃并保持状态（沿用现有测试契约）。
- fling 推进在门面 `Step` 的帧末按 `frameQpc` 执行，与手势事件处理在同一次调用内，按"先事件、后推进"的顺序。

## 3. 触点增减规则（重点）

focus = 所有"按住"触点的平均位置；POINTER_UP 时排除被抬起者。`last_focus/down_focus` 的重置时机严格按 `gesture_detector.cc`。

| # | 场景 | 行为 |
|---|---|---|
| 1 | 空闲 → 第一指 Down | `down_focus=last_focus=focus`；取消 fling（见 §4）；`all_pointers_within_slop=true`；`maximum_pointer_count=1`；tracker.Clear 后加入首样本。 |
| 2 | 拖动前（门限内）第二指 Down | POINTER_DOWN：`down_focus=last_focus=focus(含新指)`；`maximum_pointer_count` 更新；恰 2 指时记录 secondary_down 作为第二指的 slop 起点；新指在 tracker 中从空开始，第一指历史保留。 |
| 3 | 已起滑（拖动中）第二指/更多指 Down | 同 #2 的基线重置（画布不跳）；已起滑后不再做门限判定。 |
| 4 | 已起滑，抬起一指但仍有剩余 | POINTER_UP：基线 = **不含被抬起者**的 focus（被抬起者最后一次位移不计入）；计算被抬起者与每个剩余指的速度点积，任一 <0 → `tracker.Clear()`（全部清空，剩余手指需重新积累样本后才可能起滑）。 |
| 5 | 最后一指 Up | UP：fling 速度取**该触点自己**的速度（`GetPointerId(0)`），按轴限幅 17000，任一轴 |v|>30 才 fling；随后 tracker.Clear。 |
| 6 | 曾同时按下 >2 指 | `IsWithinSlopForTap` 恒为 false：不判门限，下一次 MOVE 即起滑。 |
| 7 | 起滑门限判定（≤2 指） | 每个触点用**各自的按下位置**（第一指用首次 Down，第二指用 secondary_down），平方位移 >（(15+0.05)²×expand）即越限；任一越限则起滑。本阶段 `expand` 固定为 1；依据是 Chromium `HasNativeTouchMajor(source_index) ? 1 : 4` 的有原生接触面积分支。当前门面未传 native touch-major 标志，按任务指定模拟有面积；若设备实际上无原生面积，其 Chromium 门限会扩大到约 30 DIP，此处存在手感差异风险，后续由输入元数据验证。detector 原始 `OnScroll` 为 `last_focus − focus`；Provider 首次 scroll 且当前触点数 <3 时，对每指按下到当前位移 `d` 计算 `d × max(0, 1−15/norm(d))`（沿用 SubtractSlopRegion：norm(d)≤1e−3 时直接保留 d，含零向量）后除以当前触点数；扣除的是 touch_slop=15，不含 detector 的0.05 epsilon。当前 ≥3 指不扣。二维 delta 全零时不派 ScrollBegin/Update；之后仍用 `last_focus − focus` 增量，在 provider 边界转为内容跟手方向。 |
| 8 | 起滑后 MOVE | 仅当 |scroll_x| 或 |scroll_y| >0.1 派发并更新 `last_focus`。 |
| 9 | 同帧多事件 | 严格按 QPC 排序逐个处理，每个 Down/Up 各触发一次基线重置。同帧"全部抬起后又有新 Down"视为新手势起点（先完成上一手势的 UP/fling 判定，再 DOWN）。 |
| 10 | 单触点 Cancelled，其余仍在 | 当作"不贡献速度的抬起"：从 focus 与 tracker 中移除该指，基线重置为不含它的 focus（不跳）；不触发反向清空。 |
| 11 | 最后一个触点 Cancelled | 不起滑（等同整手势取消），tracker.Clear，booster 保留（与触点取消不同于 StopFling）。 |
| 12 | 惯性中落指 | 第一指 Down 即取消 fling（FlingCancel）；booster 不重置；第二、三指 Down 不重复取消。 |
| 13 | 静止时增减触点 | `contentDelta=0` 且视口不变；focus 随当前触点质心重算。速度断言用绝对容差 norm(v)<0.01 DIP/s（LSQ float 舍入），不加归零阈值。 |
| 14 | 超过 `kCanvasPanMaximumContacts`(16) | 忽略新触点，已有手势不受影响。 |
| 15 | 未知 key 的 Up/Cancelled | 忽略。已抬起触点的迟到 Move 不得复活（沿用现有契约）。 |

准入与"带历史样本加入"：模块对设备无感知，调用方通过 `contacts` 决定谁参与。为日后"绘制中的笔画转平移"，提供入口让调用方在加入新触点前预置该触点已有样本（按 QPC 升序传入；这些样本只用于估速与基线，不产生 scroll 事件）。仅暴露接口与测试，本任务不接线。阶段 1 仅支持首指带历史加入；活动多指加入历史等待阶段 2 的同步样本格式，当前返回 false。

## 4. fling 生命周期

- **起滑**（UP 且速度过门限）：`velocity = booster.GetVelocityForFlingStart(fling_start_velocity)`；`boost_multiplier = max(|v_boosted|)/max(|v_raw|)`；`start_time = 最后一次 ScrollUpdate 的时间`（无则 fling start 时间）；用曲线创建 fling。非 boost 情形等同 raw。
- **推进**：门面每帧调用；规则全部来自 `fling_controller.cc::ProgressFling`（两帧回拨 `kFrameDelta=1/60`、`kMaxMicrosecondsFromFlingTimestampToFirstProgress=2/60`、delta=offset−cumulative、cumulative 总是前进、任一轴 >0.1 才派发、派发后 `ObserveProgressFling`）。
- **结束**：曲线不活跃（x≥1）→ booster.Reset、Idle；与 Chromium 一致，结束帧不补发。纯曲线虽返回 `distance`，controller 立即结束并丢弃该帧 delta；派发总位移 ≤ distance，未触发 0.1 丢弃时尾差恰为最后活跃帧到终点的剩余。视口触限位的轴：该轴清零（沿用 `ClearAxis` 语义；双轴都清零则结束）。
- **取消**：落指 = FlingCancel，不重置 booster；`StopFling`（程序性）重置 booster 并结束。
- `contentVelocity`（供 tile 预测）：拖动中取最新样本的 tracker 速度（按轴限幅）；fling 中取曲线当前速度；Idle 为 0。

## 5. 笔与停止钩子

- 笔 hover 出现：**仅当处于 fling（Coasting）时**立即 `StopFling`（取消 fling + 重置 booster）；有手按住时沿用现有仲裁，不改变（现有 `ShouldBeginSuppressingPenContactDuringTouchPan` 等保持）。移除"hover 用手摩擦加速制动"。
- 笔落笔（`penContact`）：沿用现行 `Stop()`（清空一切）。
- 离开移动模式、翻页、切工作区、白板加载、最小化、Resize、落笔：`Stop()`，重置 booster（以 `stopCanvasNavigation(reason)` 为准）。

## 6. 缩放预留

- `CanvasPanFrameOutput` 增加 `focus`、`span`（触点到 focus 的平均距离，单指为 0）、`pointerCount`、`scaleDelta=1`。`scaleDelta` 本任务恒为 1，不参与位移；日后缩放以围绕 focus 缩放叠加，不改触点管理与增减规则。
- 视口模型保持纯平移，不引入缩放因子。

## 7. 控制器接入（阶段 1 的最小改动）

- `updateCanvasNavigation` 继续消费 `panContacts` 快照并调用 `Step`；删除对已移除常量/phase 的引用；笔 hover/落笔按 §5。
- 诊断：`LogCanvasPan`（仅 RTS 诊断模式）输出每帧样本数、各触点估速、focus/span、booster 状态（cutoff、累计起滑速度）、fling 速度与累计偏移。
- 不改：背景跟随、渲染 pass、tile 规划、`TranslateViewport`、模式门禁、设备互斥。

## 8. 阶段 2（阶段 1 构建通过、测试通过、审查后再做）

- RTS：`Packets` 保留批内全部样本，评估包内是否有硬件时间戳（否则用到达时间并记录局限）；`PublishMove` 由单槽覆盖改为有界队列（溢出策略、与终态的顺序保证、写锁竞争不丢 Move 的方案需在 design 补充并评审后再改）。
- `WaitForFrameDeadline`：遇终态/控制唤醒提前返回（避免所有 Move 都触发）。
- 阶段 2 单独起子步骤与测试，不与阶段 1 混合提交。

## 9. 兼容与回退

- 回退点：曲线开关切回旧 `FlingCurve`；阶段 1 与阶段 2 分别提交，可独立回退。
- 不改 UInk 格式、渲染与背景；不改鼠标/触摸设备互斥与 Win32 鼠标兜底。
- 鼠标左键拖动沿用触摸路径（含 fling）。

## 10. 测试策略

- 纯 CPU 单测（`draw3_canvas_pan_tests.cpp`，旧动力学契约整体重写）：
  - 黄金值：由 Python 独立复算 `PhysicsBasedFlingCurve`（时长/距离/贝塞尔/P1 修正）、LSQ2 估速（含折返序列）、booster 八个拒绝条件与累加；脚本放 `research/sim/`，结果入断言。
  - 手势：按下即停、门限内不位移、首次滚动逐指径向扣 15 后平均（≥3 指不扣、二维全零不派发）、0.1 阈值、>2 指跳过门限、每触点各自 slop 起点。
  - 多指：§3 每一行至少一个用例，重点断言"画布位移连续、无跳变"和"起滑速度正确"（含反向手指抬起清空 tracker 后不起滑）；跨拓扑同帧 Move→Down→Move / Move→Up→Move；静止增减指断言 contentDelta=0、视口不变，速度用绝对容差 <0.01 DIP/s（LSQ float 舍入）。
  - fling：两帧回拨、cumulative 前进、dt 抖动下按实际活跃帧核对位移、结束帧不补发（总位移 ≤ distance；尾差核对最后活跃帧剩余）、限位清轴。
  - booster：窗口 50 ms、150/350 阈值、反向重置、落指不重置、StopFling 重置。
  - 笔：hover 仅在 fling 时停、落笔清空；缩放预留字段不影响位移。
- 生产探针/既有测试：`draw3_mouse_input_tests.cpp`、`draw3_bridge_tests.cpp`、`animation_tests.cpp` 等受影响处同步；不得删除无关测试。
- 未覆盖须如实记录：真机手感、RTS 实际样本分布、Edge 出货配置。

## S1 源码核对与 A–E 裁决（2026-11-08 第二轮）

- **A（已裁决）**：采纳 `gesture_provider.cc:428–439, 807–856` 的 `OnScroll` / `ComputeFirstScrollDelta` / `SubtractSlopRegion`，首次 scroll 当前少于 3 指逐指径向扣 15 后平均，≥3 指不扣，全零不派发。PRD R2 / AC1 与本文 §3 #7 已同步，不再把 detector 原始 delta 当作最终 ScrollUpdate。
- **B（已裁决）**：采纳 `fling_controller.cc:247–257`；曲线 `ComputeScrollOffset` 在 x≥1 输出 distance 且返回 false，controller 立即结束并 return，不补发最后一段 delta。纯曲线黄金值仍校验终点 distance，controller 测试另断言总位移与尾差。
- GestureProvider 派发顺序：首个有效 OnScroll 先 ScrollBegin（hint=-distance），再 ScrollUpdate（delta=-distance）；二者已由 detector 的 `last_focus-focus` 转为内容跟手方向。UP 生成 FlingStart 后立即把 `scroll_event_sent_` 置 false，因此不紧接 ScrollEnd；只有未生成 fling 的 UP/CANCEL 才在尾部补 ScrollEnd。ScrollBegin 的坐标采用首次 Down，持续 scroll focus 按 raw_distance 推进。
- `ProgressFling` 时间门禁实际为 `now < last_progress_time` 或 `now <= start_time`，不是整理文此前的两处 ≤。首帧回拨判据依赖“已派发首个更新”，被 0.1 门限丢弃的 Advance 不会标记已派发；cumulative 则照常前进。
- Tracker 是单个多指 movement 环：40 ms 清窗仅在新旧 id 集有交集时触发；DOWN/POINTER_DOWN 会新增完整 movement，UP/POINTER_UP 不新增 movement。ClearPointers 仅从最新 movement 去掉 id，使下一次该 id 回溯遇断口终止。
- `research/sim/chromium_fling.py` 使用 Python 双精度作独立源码公式参考；生产 Chromium 的 float 舍入由主 Agent 的 C++ 黄金值容差核对。A/B 按本轮裁决采用 Provider 扣 slop、controller 不补发，不改曲线终点黄金值。

- **C（已裁决）**：严格 QPC 排序优先，以 Down/Up/Cancelled 为边界 flush 连续 Move 段；不将边界前的 Move 推迟到新指加入/旧指移除后，不跨拓扑补点。§2 与跨边界同帧用例同步，保留当前实现。
- **D（已裁决）**：阶段 1 仅实现无歧义的首指 history seed；单个多指 tracker 的 missing-id 回溯中断语义要求历史 movement 含所有对应时刻的活动 id，活动多指 seed 等待阶段 2 同步样本格式。
- **E（已裁决）**：静止增减指的不变量为 contentDelta=0 且视口不变，focus 按当前质心重算；速度按绝对容差 <0.01 DIP/s 验证 LSQ float 舍入，不增加归零阈值。

- E 的数值说明：Chromium LSQ 的 float QR 在非零绝对坐标存在舍入残差；独立静态复算示例为同一 `(10,20)` 坐标、间隔 1 ms 两点得到约 `(-0.0026974,-0.0053948) DIP/s`，原点样本可精确为0。阶段1不额外加归零阈值或平滑；此量级低于源码每轴 >30 DIP/s 的 fling 门限，不能据此宣称真实 fling。测试使用上述绝对容差，不承诺任意坐标下速度位级不变。
- GestureProvider 零 delta：detector 越过门限时仍更新 last_focus/within_slop，但 provider 若最终二维 unconstrained distance 全为0则不发 ScrollBegin/Update（不能让对向两指的零 focus 位移制造 scroll 序列）。

- **ppi 平台来源补查**：本地未含下游文件，已读取 [fling_scheduler.cc](https://raw.githubusercontent.com/chromium/chromium/main/content/browser/renderer_host/input/fling_scheduler.cc)、[screen_win.cc](https://raw.githubusercontent.com/chromium/chromium/main/ui/display/win/screen_win.cc) 与 [screen_win_display.cc](https://raw.githubusercontent.com/chromium/chromium/main/ui/display/win/screen_win_display.cc)。Windows scheduler 调 `ScreenWin::GetPixelsPerInch(global_point)`；ScreenWin 按最近 DIP 显示器取存储PPI，该值由对应 Touch pointer device 的 `GetPointerDeviceRects` 计算：`ppiX=2540*screenRect.width/deviceRect.width`（Y同理），查询失败用96。ScreenWinDisplay 原样保存 DisplayInfo 的PPI，未再除显示缩放。**本任务固定96是明确适配差异，不能宣称精确复刻有硬件PPI的Windows分支**；将来若接入，需先澄清与全程DIP数值空间的换算，不在本阶段擅改。
