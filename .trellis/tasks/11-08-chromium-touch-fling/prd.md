> 本轮实现与无窗口验证已完成，结果和环境问题处理见 [validation.md](validation.md)。用户真机手感及后续屏幕尺度仍待进行；未提交、未归档。

> 当前实施以 [implementation-scope.md](implementation-scope.md) 为准：用户已批准本轮修复鼠标/触摸手感并接通诊断，屏幕尺度延后；以下阶段1及先前调查记录保留为历史。

> 2026-11-08 续接调查：阶段1已提交为 `7b213d90`；用户反馈手感退化。本轮仅调查与规划，新的候选目标、证据、实施顺序及验收见 [repair-plan.md](repair-plan.md)。下文保留阶段1历史合同；原S6及新修复均须先评审，不据 task 的 in_progress 状态自动实施。

# 白板画布触摸平移复刻 Chromium fling

## Goal

把白板"移动（拖动）"模式下的触摸/鼠标平移与惯性，整体替换为 **Chromium（Windows 触摸）** 的滚动与 fling 模型，使手感与 Edge / Chrome 触摸滑动一致。多指平移现在就要支持，并为后续缩放预留数据通道。与 Chrome 的唯一有意差异：笔出现（hover）或落笔绘制时**立即停止**画布惯性。

前置任务：`11-06-whiteboard-canvas-pan-inertia`（已实现摩擦耦合动力学，真机反馈手感与 Chrome 不一致）。本任务取代其中的动力学内核，不改变其输入路由、视口应用、背景跟随等部分。

## Background（已确认事实）

- 2026-11-08 双路调查（Codex Sol High 只读 + 独立复算）结论：现行动力学与 Chrome 的差异集中在
  1. 惯性中按下不立即停住：进入 `Slipping`，约 133–192 ms 才"抓住"，反向时先向旧方向多冲约 95 DIP 再被拽回；
  2. 二次最小二乘估速在折返时过冲（真实 +3000→−3000 时估速依次为 +2011/+462/−1292/−5727）；
  3. 连甩语义为"耦合到手速"，不累加；Chrome 有 fling boost（同向累加起滑速度）；
  4. RTS `Packets` 回调只解码批末包、时间戳取回调到达时刻、`PublishMove` 单槽覆盖，估速器每帧至多 1 个样本；
  5. 释放门限 250 DIP/s 处距离有台阶；`WaitForFrameDeadline` 遇 Down/Up 不提前醒来。
- Chromium 模型的精确常量与行为见 `research/chromium-fling-model.md`（均来自 Chromium main 源码）。Windows 上触摸 fling 曲线默认是 `PhysicsBasedFlingCurve`（`kExperimentalFlingAnimation` 在 `IS_WIN || IS_LINUX` 默认开启；Edge 实际出货配置未验证，故曲线做成单点开关）。
- 现有代码接入点：`Draw3.CanvasNavigation.cppm/.cpp`（`CanvasPanDynamics::Step/Stop/ClearAxis`、`CanvasPanFrameInput/Output`），控制器 `Draw3.DrawingController.cpp` 的 `updateCanvasNavigation`、`panContacts`/`ContactSnapshot` 消费、视口应用块与笔仲裁；测试 `InkeysHeadlessTests/draw3_canvas_pan_tests.cpp`。

## Requirements

**R1 速度估计（Chromium LSQ2）**
- 每触点一个 LSQ2 估速器：100 ms 窗口、环形 20 样本；相邻 Move 间隔 ≥40 ms 清空历史；抬起时距最后样本 ≥80 ms 视为无速度；**方向改变不重置**；速度取最新样本时刻的一阶导；按轴限幅 17000。
- 不再有"≥3 样本且跨度 ≥20 ms""≤2 ms 合并""释放门限 30/70 ms""250 DIP/s 起滑下限""手静止判定"。

**R2 滚动检测（含起滑门限）**
- 触摸滑动距离超过 touch slop（15 DIP，detector 越限判定含 +0.05 余量，平方比较）才开始滚动；首次滚动按 Provider 对少于 3 指的每指按下到当前位移径向扣除 15 DIP 后取平均，避免跳变；当前 ≥3 指不扣。epsilon 不参与扣除，扣除后二维增量全零则不派发。
- 起滑后每轴增量 ≤0.1 不派发（`kScrollEpsilon`）。
- 按下即取消 fling，画布停住；之后在起滑门限内画布不动。

**R3 多指（现在支持）**
- 质心（focus）平移；每触点各自一个估速器。
- 增减触点规则见 `design.md §3` 的规则表：加指/减指不跳变；抬起手指时基线重置为**不含被抬起者**的质心；被抬起者与任一剩余手指速度点积 <0 时清空全部估速器；最后一根抬起时 fling 速度取该触点自己的估速器；曾同时按下 >2 指则跳过门限。
- 单个触点被系统 Cancelled 而其余仍在：当作"不贡献速度的抬起"；若是最后一个触点则不起滑。
- 准入由调用方（控制器）负责，模块对输入设备无感知；支持"带历史样本加入"（阶段 1 仅首指）。

**R4 Fling（Chromium PhysicsBasedFlingCurve）**
- 抬起时任一轴速度 >30 才 fling。曲线：`duration=min(2 s, vmax/2300)`；距离 `v²/(2a)`（`a=ppi×2.7559e-5 inch/ms²`，DIP 单位 ppi 取 96，若核对 Chromium 来源给出更准确结论则以其为准并写入 design）；距离上限为视口尺寸×3×boost 倍数；位移走 (0.2,1)–(0.55,1) 贝塞尔并按初速斜率修正 P1。
- fling 起点时间为最后一次拖动更新的时刻；首个推进帧若已晚于起点 2 帧（1/60 s）则起点回拨 1 帧；每帧位移 = 曲线偏移 − 上次偏移；单轴增量 ≤0.1 不派发（与 Chrome 一致，含其丢弃语义）。曲线返回 inactive 时立即结束，与 Chromium 一致，结束帧不补发终点 delta。
- 曲线选择做成单点开关，可切到旧版 `FlingCurve`（指数曲线）。

**R5 连甩（Chromium FlingBooster）**
- 完整移植 `FlingBooster` 的规则：同向、新旧速度平方均 ≥350²、在 50 ms 窗口内，**累加上次起滑速度**（不是衰减后的当前速度）；反向或窗口内滚动速度 <150 则重置；落指取消 fling 时不重置 booster。
- 起滑速度累加后用于创建曲线，`boost_multiplier` 影响距离上限。

**R6 笔（唯一有意偏离）**
- 笔 hover 出现或落笔绘制时，**立即**取消 fling 并重置 booster（等同 `StopFling`），取代现在"笔 hover 用手摩擦加速制动"。
- 离开移动模式、翻页、切换工作区、加载、最小化、Resize 沿用现有的停止钩子，同样重置 booster。

**R7 视口与限位**
- 视口触及硬限位的轴：该轴 fling 终止并清零（沿用现有语义）。
- 视口模型保持纯平移，本任务不引入缩放。

**R8 缩放预留（不实现缩放）**
- 每帧输出增加 `focus`（质心）、`span`（触点到质心的平均距离）、`pointerCount`；预留 `scaleDelta = 1`。日后缩放叠加在这些字段上，不再改触点管理与增减规则。

**R9 诊断**
- 在现有 RTS 诊断模式下的 `LogCanvasPan` 输出每帧：样本数、各触点估速、focus、booster 状态、fling 速度与偏移，供真机对照；正常模式不输出。

**R10 输入管线保真度（阶段 2，阶段 1 通过构建与测试后再做）**
- RTS `Packets`/`PublishMove` 保留批内全部样本与可信时间戳，控制器按序喂入估速器（需评估包内是否有硬件时间戳）；增加溢出处理与同步。
- `WaitForFrameDeadline` 遇终态/控制唤醒提前返回，缩短按下到停止的延迟。

## Acceptance Criteria

- AC1 惯性中按下，下一帧起画布增量为 0；起滑门限内不位移；首次滚动按 Provider 径向扣除 15 DIP（少于 3 指逐指扣除后取平均），避免跳变。
- AC2 折返不重置估速器，数值与 Chromium LSQ2 一致（以独立复算的黄金值为准）。
- AC3 `PhysicsBasedFlingCurve`、`CubicBezier::Solve`、`FlingBooster` 的单测与 Chromium 源码公式逐项一致（时长、距离、上限、booster 八个条件、50 ms 窗口、150/350 阈值）；controller 结束帧不补发，派发总位移不超过 distance，尾差为最后活跃帧到终点的剩余（另保留 0.1 门限丢弃语义）。
- AC4 多指：加指/减指全部场景画布位移连续无跳变；反向手指抬起清空估速器；>2 指跳过门限；Cancelled 与同帧多事件按 design 规则；模块可带历史样本加入（阶段 1 仅首指）。
- AC5 笔 hover/落笔立即停止，且 booster 被重置；其余停止钩子不遗留速度。
- AC6 缩放预留字段存在且不影响平移结果。
- AC7 `InkeysRepo.sln` `Debug|ARM64` 构建通过，`InkeysHeadlessTests` 全部通过；`git diff` 无无关改动、保持原有编码与换行。
- AC8 spec（`runtime-and-rendering.md` 惯性契约等）与本任务/前置任务文档同步为新模型。

## Out of Scope

- 缩放本身、视口缩放因子、绘制模式下的双指拖动及其仲裁（`CanvasTouchGestureState` 保持绕开）。
- 背景点阵跟随、Freeze 窗口、Draw3 渲染 pass 的改动。
- 鼠标左键拖动沿用与触摸相同路径（含 fling），不按 Chrome 的"鼠标无 fling"改。
- Edge 实际出货配置的验证；真机手感标定与高速摄像核对。
