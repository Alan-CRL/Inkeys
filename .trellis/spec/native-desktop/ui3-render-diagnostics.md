# UI3 Render Diagnostics

## 1. Scope / Trigger

修改 UI3 的阶段计时、光影计数、共享 Scheduler 汇总或 `[UI3Diag]` 日志时适用。诊断覆盖现有渲染路径，不改变动画时间、缓存命中条件、设备恢复或成功呈现事务。

## 2. Signatures

`Inkeys.UI.RenderPipeline` 导出轻量帧样本与注册接口：

```cpp
FrameDiagnostics* CurrentFrameDiagnostics() noexcept;
using DiagnosticsSink = std::function<bool(std::string_view)>;
bool Scheduler::SetDiagnosticsSink(DiagnosticsSink sink);
bool SetDiagnosticsSink(DiagnosticsSink sink); // 仅全局 Scheduler

FrameStageTimer(FrameDiagnostics*, FrameStage) noexcept;
void FrameStageTimer::Stop() noexcept;
```

`FrameDiagnostics` 只由当前客户端回调填写；包含 raw/animation dt、推进/尝试/成功/延后/退避标记、各阶段毫秒、实际 API 结果、资源几何和光影计数。`FrameAnimationClock::LastRawElapsedSeconds()` 是只读观测，不改变 `Tick()` 的 50 ms 上限或 `Rebase()` 接线。

`RenderPipeline.Diagnostics.h` 为内部纯数值聚合，置于 module/import 之后；生产 Scheduler 与无窗口测试共用同一实现，不作为新的通用遥测层。

## 3. Contracts

- TLS 样本只在所属 Scheduler 的当前回调期间有效；没有 sink 或不在回调内返回 nullptr。禁止缓存指针到下帧、跨线程或在回调后写入。多个 Scheduler 的 sink、聚合和恢复链相互独立。
- 正常热路径只更新固定大小的数值与单调时钟；不做日志格式化、文件写入、每 packet 日志或逐分片时钟采样。`FrameStageTimer` 必须允许提前 Stop 且只计一次，nullptr 时不读时钟。
- `animationDtSeconds` 只在 `animationAdvanced` 时计入实际推进总量；退避已领取但未推进的 dt 不得被记成动画已使用。回调数、动画推进数、GetDC 尝试数、ULW 尝试数、成功数分别计数。
- `FrameResult::Retry` 可以是合法布局交接，不独立触发错误；由实际 API 失败、显式失败标记、回调异常或 DeviceLost 建立错误链。实际呈现失败须等到后续成功提交才记恢复，隐藏后的 Idle 不代表呈现恢复；纯回调/设备异常的恢复按回调结果另行判断，不修改重试决策。
- 活动批次/客户端间隔、单回调或整批耗时达到 50 ms 时触发诊断。真正 Scheduler idle、客户端上次 Idle 后的首次唤醒排除在活动间隔外；原始成功提交间隔仍可单列输出，不能混成活动长帧。
- 默认健康汇总关闭。异常或恢复聚合最多每秒尝试输出一次；sink 返回 false、拒绝入队或抛异常也消耗本次限频额度，并保留未被接收的聚合。
- 异常后进入 idle 时只为待输出诊断设置到期等待；到期可以发日志，不得制造任何客户端渲染请求。没有待输出诊断时沿用原来的无限 idle 等待。
- 不为诊断增加退出等待；Stop 仅尝试已到期的记录。因此立刻终止进程时，仍处冷却时间内的尾部记录不保证落盘。
- sink 在调度器内部锁外执行。产品日志桥复用现有 file sink/thread pool，诊断专用 async logger 使用 `discard_new` 避免阻塞渲染；不改变普通 IDTLogger 的溢出策略或共享格式，不新增日志线程。
- mask 的 hit/miss/create/failure 描述实际查询/创建边界；创建耗时只在 miss 时采样。`slices` 是实际 FillOpacityMask 提交次数（含 exact 烘焙和整图单次绘制），不是 Gaussian 创建数；空片/零透明度跳过不计。
- exact fallback 原因只用于观察；拆分原因不能改变条件求值结果、缓存预算、失败 latch 或绘制质量。

## 4. Validation & Error Matrix

| 场景 | 必须行为 |
| --- | --- |
| 正常帧或合法 Retry、无其他异常 | 不输出异常日志 |
| 阶段超过 50 ms / 真实失败 | 保存阶段、计数与资源快照，按限频输出 |
| 多次失败后成功、期间被限频 | 错误与恢复证据均保留；不能只剩最后正常快照 |
| sink 拒绝或抛异常 | 不改变渲染结果；保留聚合，至少 1 秒后再尝试 |
| 长时间 idle 后首次请求 | 不把静置时长判为活动卡顿 |
| 异常后全部客户端 Idle | 到期输出日志，客户端回调数不增加 |
| 没有 sink、多个独立 Scheduler、Stop/restart | 无悬空 TLS、无实例之间串样或遗留 sink |
| 同一计时器 Stop 两次 / Stop 后析构 | 只累计一次 |

## 5. Good / Base / Bad Cases

- Good：Bar 绘制快、Settings 回调慢，日志保留各客户端耗时，能够解释主栏下一帧为何延后。
- Base：GetDC 长耗时与绘制、ULW 分开；GetDC 会 flush，因此其耗时仍可能包含此前绘制等待。
- Bad：把 Retry 都写成失败、把闲置后首帧当长帧、每个鼠标消息写一条日志，或者让日志队列阻塞渲染。

## 6. Tests Required

- 复用实际聚合器的确定时间测试：健康静默、50 ms 阈值、1 s 限频、拒绝保留、恢复保留、idle 排除、推进与回调分离。
- 真实 Scheduler 与假客户端：TLS 生命周期、多个客户端/多个实例隔离、动态 sink 安装、sink 异常、Stop/restart、idle 诊断到期不增回调。
- 帧计时测试：原始 dt 可见，但 clamp/负值回退/Rebase 原行为不变。
- 绘制计数使用实际函数体探针或真实 renderer 验证正常分片、exact 单次和跳过分支；不能把计数验证称为完整 GPU/ULW 性能复现。
- 完整 `InkeysRepo.sln Debug|ARM64` 与 `InkeysHeadlessTests --no-window`；主观流畅度和故障现场仍需独立运行证据。

## 7. Wrong vs Correct

```cpp
// Wrong：回调领到的 dt 不一定进入了动画，合法 Retry 也不一定是错误。
advancedSeconds += sample.animationDtSeconds;
failed = result == FrameResult::Retry;

// Correct：以实际阶段标记统计；错误来自真实 API 结果与显式失败。
if (sample.animationAdvanced) advancedSeconds += sample.animationDtSeconds;
failed = sample.presentFailed || result == FrameResult::DeviceLost;
```

完整失败判定还包含资源/DC/EndDraw HRESULT、ULW 错误和回调异常；上例仅说明统计语义。
