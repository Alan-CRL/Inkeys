# Chromium 触摸滚动 / fling 模型（参考事实）

来源：Chromium main（2026-11-08 抓取）。本地副本在仓库外 `%LOCALAPPDATA%\Temp\inkeys-codex\chromium-src\`（文件名为路径把 `/` 换成 `_`），不进仓库。权威以源码为准，本文是整理。

## 源码文件

| 角色 | 路径（chromium/src） |
|---|---|
| 手势检测（slop、focus、POINTER_UP、UP→fling 判定） | `ui/events/gesture_detection/gesture_detector.cc/.h` |
| 手势 → ScrollBegin/Update/FlingStart 映射 | `ui/events/gesture_detection/gesture_provider.cc/.h` |
| 默认配置（min/max fling、slop） | `ui/events/gesture_detection/gesture_configuration.h` |
| 速度估计（LSQ2） | `ui/events/velocity_tracker/velocity_tracker.cc/.h` |
| fling boost | `ui/events/blink/fling_booster.cc/.h` |
| fling 启停与逐帧推进 | `components/input/fling_controller.cc/.h` |
| 曲线选择 | `ui/events/gestures/blink/web_gesture_curve_impl.cc` |
| 曲线（Windows 默认） | `ui/events/gestures/physics_based_fling_curve.cc/.h` |
| 曲线（旧版指数） | `ui/events/gestures/fling_curve.cc/.h` |
| 贝塞尔求解 | `ui/gfx/geometry/cubic_bezier.cc/.h` |

## 配置默认值（gesture_configuration.h）

- `min_fling_velocity = 30`、`max_fling_velocity = 17000`（px/s，按轴）
- `max_touch_move_in_pixels_for_click = 15`、stylus 20；`GestureDetector::Init` 在 slop 上加 `kSlopEpsilon = 0.05` 后取平方
- 无 native touch major 时 slop 平方 ×4（`kExpandSlopFactorForZeroTouchMajor`），有则 ×1
- `kScrollEpsilon = 0.1`

## 曲线选择

`kExperimentalFlingAnimation`：`IS_WIN || IS_LINUX` 默认开启，其余关闭 → Windows 触摸使用 `PhysicsBasedFlingCurve`；关闭时用 `FlingCurve`。

## 速度估计（velocity_tracker.cc）

- LSQ2（`STRATEGY_DEFAULT = LSQ2`），`kHorizonMS=100`、`kHistorySize=20`。
- `AddMovement`：与上次事件间隔 ≥`kAssumePointerMoveStoppedTimeMs=40` 且新旧 id 集有交集时清策略历史。UP/POINTER_UP 不加样本；若距最后样本 ≥`kAssumePointerUpStoppedTimeMs=80` 清历史。DOWN 清全部；POINTER_DOWN 清新指。
- 估计：取窗口内样本（时间以最新样本为 0，向前为负），度数 = min(2, m−1)；m=1 时仅位置（无速度，`GetVelocity` 返回 false、速度 0）；速度 = 线性项系数。权重 NONE（LSQ2 不加权）。QR（Gram-Schmidt）求解，范数 <1e-6 时退化为位置。
- **方向改变不重置历史**。无速度限幅（限幅在 `ComputeCurrentVelocity(1000, max)` 调用方）。

## 手势检测（gesture_detector.cc）

- focus：所有触点平均位置；POINTER_UP 时**排除被抬起者**（`skip_index`）。
- DOWN：`down_focus = last_focus = focus`，`all_pointers_within_slop_regions = true`，`maximum_pointer_count = 1`。
- POINTER_DOWN：`down_focus = last_focus = focus`（含新指）；更新 `maximum_pointer_count`；恰好 2 指时记录 `secondary_pointer_down_event`（用于第二指的 slop 起点）。
- POINTER_UP：`down_focus = last_focus = focus`（不含被抬起者）；`ComputeCurrentVelocity`；被抬起者速度与任一剩余手指速度点积 <0 → `velocity_tracker.Clear()`。
- MOVE：`scroll = last_focus − focus`。若仍在 slop 内：`IsWithinSlopForTap` 为假时派发第一次 `OnScroll(scroll)`（**此处仅为 detector 原始 delta；最终 Provider 首次事件还要扣 slop，见下文**），随后 `last_focus = focus`、`all_pointers_within_slop_regions = false`。已起滑：仅当 |scroll_x| 或 |scroll_y| >0.1 派发，并更新 `last_focus`。
- `IsWithinSlopForTap`：曾同时按下 >2 指直接返回 false（不判门限）；否则每个触点用**各自的按下事件**（第一指用 current_down，第二指用 secondary_pointer_down）算位移平方，任一 >（slop²×expand）即 false；找不到来源按下事件也返回 false。
- UP（已起滑）：`pointer_id = ev.GetPointerId(0)`（即最后一根），`ComputeCurrentVelocity(1000, max_fling_velocity)`，任一轴 |v| >`min_fling_velocity` 才 `OnFling`；之后 `velocity_tracker.Clear()`。
- CANCEL：整个手势取消，无 fling。

## FlingBooster（fling_booster.cc）

常量：`kMinBoostFlingSpeedSquare = 350²`、`kMinBoostTouchScrollSpeedSquare = 150²`、`kFlingBoostTimeoutDelay = 50 ms`。无余弦阈值，仅用点积 ≤0 拒绝。

状态：`previous_fling_starting_velocity`（**累计的起滑速度**）、`current_fling_velocity`、`cutoff_time_for_boost`、`previous_boosting_scroll_timestamp`、`source_device`、`modifiers`。

- `GetVelocityForFlingStart`：若 `ShouldBoostFling` → 新速度 += `previous_fling_starting_velocity`（累加的是上次**起滑**速度，不是衰减后的当前速度）；`Reset()`；再把结果写入 previous 与 current；返回。
- `ObserveGestureEvent` 顺序：无活动 fling（previous 为零）返回 → 有 cutoff 且事件时间晚于 cutoff：Reset 返回 → 设备不同：Reset 返回 → 分派：
  - ScrollBegin：cutoff = t+50 ms。
  - ScrollUpdate：惯性相位更新直接返回；无 cutoff 返回；previous 与本次 delta 点积 ≤0 → Reset；若有上次 boosting 时间戳，间隔 ≥0.001 s 时算滚动速度，平方 <150² → Reset；否则记录时间戳并 cutoff = t+50 ms。
  - ScrollEnd：仅清 boosting 时间戳。
  - FlingCancel：`prevent_boosting` → Reset；否则清时间戳并 cutoff = t+50 ms。
- `ShouldBoostFling` 依次：无活动 fling、设备不同、modifiers 不同、无 cutoff、事件时间晚于 cutoff、点积 ≤0、当前 fling 速度平方 <350²、新 fling 速度平方 <350² → false；全过 → true。
- `ObserveProgressFling`：把 `current_fling_velocity` 更新为曲线当前速度。
- 超时只在事件到来时判定，无独立定时器。

## FlingController（fling_controller.cc）

- 落指产生 FlingCancel，是唯一停止 fling 的触发；`ProcessGestureFlingCancel` 结束当前 fling，**不重置 booster**。`StopFling`（程序性）重置 booster 并结束 fling。
- `UpdateCurrentFlingState`：速度来自 booster；`start_time` = 最后一次 scroll update 的时间（无则 fling start 时间）；速度为零/视口为空 → 重置 booster 并结束；`boost_multiplier` = 加速后最大绝对分量 / 原始最大绝对分量；创建曲线（参数：设备、加速后速度、零初始偏移、是否主线程、mobile 曲线标志、屏幕 ppi、boost_multiplier、视口大小）。
- `ProgressFling`（每帧）：无曲线返回；首次更新前：`start_time` 为空则设为 now；若 now ≥ `start_time + 2 帧(2/60 s)`，`start_time = now − 1 帧(1/60 s)`；now < `last_progress_time` 或 ≤ `start_time` 则等待；`Advance(now − start_time)` 得 delta 与是否活跃；不活跃则重置 booster 并结束；任一轴 delta 绝对值 >`kMinInertialScrollDelta = 0.1` 才派发，并更新 `last_progress_time`；每次派发后 `ObserveProgressFling(当前速度)`。
- `WebGestureCurve::Advance`：offset 为曲线绝对偏移，`delta = offset − cumulative`，`cumulative = offset`（无论 delta 是否被 0.1 阈值丢弃，cumulative 都已前进）。

## PhysicsBasedFlingCurve

常量：P1=(0.2,1.0)、P2=(0.55,1.0)、`kDefaultPixelDeceleration=2300`、`kMaxCurveDurationForFling=2.0 s`、`kDefaultPhysicalDeceleration=2.7559e-5 inch/ms²`、`kDefaultBoundsMultiplier=3`。

- 速度 px/s → px/ms（÷1000）；减速度 a = ppi × 2.7559e-5 px/ms²（按轴 ppi）；时间 t=|v|/a（ms）；偏移 = sign(v)·(|v| − a·t/2)·t（即 v²/2a）；每轴限制为 ±(视口 × boost_multiplier × 3) 的取整值。
- `duration = min(2 s, max(|vx|,|vy|)/2300)`（用原始 px/s 速度）。
- 斜率 slope = 主导轴 `|v|·duration/distance`；若 `slope·p1.x < 1` 则 `p1.y = p1.x·slope`，否则 `p1.x = p1.y/slope`。
- 进度 x=elapsed/duration；活跃当 x<1；`progress=bezier.Solve(x)`；offset = distance·progress；速度为相邻两次偏移的有限差分（px/s）；结束时 offset=distance，速度 0。
- 距离在主导轴为 0 时 slope 无穷：移植时须定义保护（速度来自 >30 的判定，实际不会为 0，仍需防御 NaN/Inf）。

## 旧版 FlingCurve（备用开关）

α=−5.70762e3、β=1.72e2、γ=3.7；位置 p(t)=α·e^(−γt) − β·t − α；速度 v(t)=−αγ·e^(−γt) − β；时长为 v=0 的时刻；初速按最大分量限幅到 v(0)，`time_offset=t(v_start)`，`position_offset=p(time_offset)`；方向比例 `displacement_ratio = v/max_start_velocity`。

## 未核实 / 注意

- Edge 实际出货的 fling 曲线与 feature 开关未验证。
- ppi 来源与默认96的解释已在下文“S1 逐层复核修正”及“Windows ppi 下游补查”核对：Windows可取硬件物理PPI，失败回退96；本任务固定96是DIP适配，不能等同所有设备。原“待核对”状态由该复核取代。
- `gesture_provider.cc` 的派发顺序与delta hint已在下文“S1 逐层复核修正”展开，并修正了首帧slop整理遗漏；原“未逐行展开”状态由该复核取代。

## S1 逐层复核修正（2026-11-08）

- **首帧 slop 整理遗漏**：以上 detector OnScroll 包含 slop 的结论仅描述 detector 回调。`gesture_provider.cc::OnScroll` 在首次 scroll 且当前指数 <3 时调用 `ComputeFirstScrollDelta`，对每个仍按住触点相对来源按下事件的位移调用 `SubtractSlopRegion`（径向减 `touch_slop`，不是加 epsilon 后的 slop），再除以当前指数。最终 ScrollBegin hint 与 ScrollUpdate delta 均为该调整后 detector distance 的负值。第二轮裁决 A 已采纳该 Provider 语义：`d × max(0,1−15/|d|)`（SubtractSlopRegion 在 |d|≤1e−3 时原样返回 d，含零向量），逐指求和除以当前触点数；当前 ≥3 指不扣，扣除后二维全零则不派发。epsilon=0.05 仅用于 detector 越限判定。后续 scroll 仍为 last_focus−focus。PRD R2 / design §3 #7 与测试已同步。
- **派发序列**：首个有效 MOVE 为 ScrollBegin→ScrollUpdate；UP 产生 FlingStart 会清 scroll_event_sent_，因此该 UP 不再发 ScrollEnd；没有 fling 的 UP/CANCEL 才尾部发 ScrollEnd。
- **曲线终点与 controller 不同**：PhysicsBasedFlingCurve 在 x≥1 输出最终 distance 并返回 false；FlingController 在 Advance 返回 false 时先 EndCurrentFling/return，终点 delta 不派发。第二轮裁决 B 已采纳 controller 不补发：总派发位移 ≤ distance；无 0.1 丢弃时尾差等于最后活跃帧到终点的剩余，纯曲线终点黄金值保持不变。
- **时间比较修正**：ProgressFling 是 now < last_progress_time（非 ≤）或 now ≤ start_time；首次派发前才执行两帧回拨。
- **ppi / viewport 来源**：FlingController 调用 scheduler_client_->GetPixelsPerInch(global_point) 和 event_sender_client_->GetRootWidgetViewportSize()；GestureProvider 无 ppi 字段。本地副本不足以确定平台实现，随后 raw 补查结论见下。96 是本任务 DIP 适配的明确默认值，不是 Chromium Windows 的固定运行期常量。
- **slop 展开**：GestureDetector::IsWithinSlopForTap 读来源 Down 的 HasNativeTouchMajor，有则平方乘1，无则乘4。阶段1按任务指令固定1，未把 RTS 已有面积数据误当成已贯通此标志的证明。
- **多指 tracker**：40 ms 规则须有新旧 id 交集；POINTER_DOWN 先 ClearPointers(new_id)，再新增含全部当前 id 的 movement。ClearPointers 只修改最新 movement 的 id_bits，回溯遇到缺少该 id 的 movement 立即中止。

- **0.1 阈值是整条二维事件的 OR 门**：GestureDetector 与 FlingController 均在任一轴绝对值 >0.1 时派发完整 delta，不对另一轴 ≤0.1 的分量单独清零；两轴都不过门限才丢弃。PRD 的“每轴/单轴”措辞应按 design §3 #8 的 OR 条件理解，不能引入逐轴抹零。

- 本地副本未包含 feature 定义文件，额外核对 [Chromium ui/base/ui_base_features.cc](https://raw.githubusercontent.com/chromium/chromium/main/ui/base/ui_base_features.cc) 第179–187行：`kExperimentalFlingAnimation` 在 `IS_WIN || IS_LINUX` 确为默认开启，其余默认关闭；与上述曲线选择结论一致，不证明 Edge 实际出货配置。

- **Windows ppi 下游补查**（本地缺文件，读取 raw main）：[FlingScheduler](https://raw.githubusercontent.com/chromium/chromium/main/content/browser/renderer_host/input/fling_scheduler.cc) 在 Windows 转交 ScreenWin；[ScreenWin](https://raw.githubusercontent.com/chromium/chromium/main/ui/display/win/screen_win.cc) 从 Touch pointer device 的 `GetPointerDeviceRects` 取得 HIMETRIC deviceRect 与像素 screenRect，算 `2540*screen.width/device.width` 与Y轴对应值；失败回退96，按 global_point 最近的DIP显示器选取。[ScreenWinDisplay](https://raw.githubusercontent.com/chromium/chromium/main/ui/display/win/screen_win_display.cc) 原样保存DisplayInfo PPI，没有除device_scale_factor。故本模块固定96是任务约定的适配，不等于所有Windows设备上的Chromium硬件PPI。不得由此擅改其余DIP约定。
