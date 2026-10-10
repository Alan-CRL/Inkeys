# UI3 Render Diagnostics

## 1. Scope / Trigger

修改UI3阶段计时、光影计数、共享Scheduler汇总、原始采样或[UI3Diag]日志时适用。观测复用现有渲染路径与成功呈现事务，默认不配置原始数组；实际Bar提交时刻/阶段另按真实producer接入。

## 2. Signatures

`Inkeys.UI.RenderPipeline` 导出轻量帧样本与注册接口：

```cpp
FrameDiagnostics* CurrentFrameDiagnostics() noexcept;
using DiagnosticsSink = std::function<bool(std::string_view)>;
bool Scheduler::SetDiagnosticsSink(DiagnosticsSink sink);
bool SetDiagnosticsSink(DiagnosticsSink sink); // 仅全局 Scheduler

FrameStageTimer(FrameDiagnostics*, FrameStage) noexcept;
void FrameStageTimer::Stop() noexcept;

bool Scheduler::ConfigureRawCapture(std::size_t capacity = 32768);
std::optional<RawCaptureReport> Scheduler::TakeRawCapture() noexcept;
bool ConfigureRawCapture(std::size_t capacity = 32768);
std::optional<RawCaptureReport> TakeRawCapture() noexcept;
```

`FrameDiagnostics` 只由当前客户端回调填写；包含 raw/animation dt、推进/尝试/成功/延后/退避标记、各阶段毫秒、实际 API 结果、资源几何和光影计数。`FrameAnimationClock::LastRawElapsedSeconds()` 是只读观测；Idle 后首次回调可报告完整 raw 间隔，同时动画 dt 为 0，后续活动帧仍受 `Tick()` 的 50 ms 上限约束。

`RenderPipeline.Diagnostics.h` 为内部纯数值聚合，置于 module/import 之后；生产 Scheduler 与无窗口测试共用同一实现，不作为新的通用遥测层。

## 3. Contracts

- TLS只在所属Scheduler的当前callback有效；仅当原异常sink或rawActive启用才提供FrameDiagnostics，raw-only也可非null。没有两者或callback外为nullptr，禁止跨帧/跨线程保留。多个Scheduler的sink/采样/恢复互不混用。
- Configure只允许stopped+真正join后的owner阶段，try_lock预约token+生命周期epoch，锁外一次分配两个数组；最大65536元素且总payload<=64MiB，乘法用除法先验。并发Start→Stop使旧预约失效；Reserved/Releasing时容量0/另一Configure拒绝。失败候选先释放再解除自己的预约，取消Prepared先Releasing再释放，不能两份预算叠加。
- render owner只写固定numeric槽，seen/retained/dropped分别保存；满后不扩容/覆盖前缀，不做文件I/O、格式化或任意raw回调。所有report payload无COM/HWND/地址/用户路径。原health sink及健康静默策略不因raw改变。
- Stop原jthread真实join后才seal。Take拒绝running/stopping/未join，仅移动一次封口report；早发running=false不等于完成。旧sealed可跨下一无采样Start保留，不能被新run写入。默认Start/Stop单owner前提仍成立，raw不增加导出等待。
- RawCaptureReport schemaVersion=2，保留steady_clock整数tick和period、run/batch/callback序号、client generation/设备epoch/idle segment、实际FrameDiagnostics与结果，callback/batch掉样及invalid分母。callback-end proxy 与 Bar 真实 CompleteAttempt 成功事务后、快照发布前的 barCommitTicks 分开；后者必须 GetDC/ULW/ReleaseDC/EndDraw 全成功且与 attempt/epoch/successSerial 相符。Absent/Unverified/Valid/Invalid 和 Idle/epoch/generation 断链分别保留，失败或代理时刻不能冒充成功事务，软件事务也不是光学可见或 GPU duration。sealed 不自动表示场景完整，runner 仍须核预期终态与零掉样。
- 新详细七阶段（WakeAndSnapshot、DisplayTransition、SubmitTargetsAndLayout、AdvanceAnimationsAndDeriveLayout、PrepareLightingAndDemand、DirtyAndPrepare、Resources）以及 StampBarCommit 必须同时受本帧 detailedCaptureEnabled 门控。Scheduler 仅在 rawActive 时赋 true；正常产品安装旧 DiagnosticsSink 形成 nonnull TLS 不授予新详细计时。nullptr 或门关闭不读新时钟、不写新 commit 字段；测试 clock 参数也不能绕门。旧六阶段/异常 sink 仍按原合同工作，Resources 是包含子阶段的 CPU 软件耗时，不将包含关系重复求和或归因到 GPU。
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
| raw-only无日志sink | callback内TLS/数值阶段有效，原异常logger不新增格式化 |
| 配置预约期间Start→Stop、容量0或释放 | 旧epoch候选拒绝，不能偷预约/并发超预算；默认调度仍可运行 |
| 数组满/逆时tick | seen=retained+dropped，保留invalid分母，不能当全程P95或将坏值夹为0 |
| running=false但尾controlTasks仍运行 | Take拒绝直到真正join/seal；重复Take返回空 |

## 5. Good / Base / Bad Cases

- Good：Bar 绘制快、Settings 回调慢，日志保留各客户端耗时，能够解释主栏下一帧为何延后。
- Base：GetDC 长耗时与绘制、ULW 分开；GetDC 会 flush，因此其耗时仍可能包含此前绘制等待。
- Bad：把Retry当失败、闲置首帧当活动长帧、回调数冒充成功帧；每帧写盘/日志阻塞；仅看running=false移出未完成数组，或把callback-end代理说成像素可见。

## 6. Tests Required

- 复用实际聚合器的确定时间测试：健康静默、50 ms 阈值、1 s 限频、拒绝保留、恢复保留、idle 排除、推进与回调分离。
- 真实Scheduler与假客户端：TLS、多client/多实例、动态sink/异常、Stop/restart、idle到期不增callback。R01–R14对raw-only/默认off、容量2溢出、真实结果/阶段、idle/gen/epoch、sink抛错、慢Settings、恢复无callback、尾tasks未join、配置ABA/Releasing做确定性断言；所有暂停超时先放行再join。当前桩六新断言红1→真实实现严格no-window绿0，仅证明计量合同，不证明真实主栏/GPU性能。
- B01–B06 复用真实 Scheduler/Stamp/validator：缺失或失败 stamp 不完成、serial/epoch/逆时拒绝、Idle/generation 断链；B06 用真正非空旧 sink 且 raw 容量0，旧 Draw timer 有值而新增七 timer/stamp/clock-read 仍为0。原四红与 B06 独立红保留，Debug 严格无窗口绿只证明这些软件计量合同。实际 Bar/SVG 完整目标、owned fixture 与三轮整帧采样仍另行验收。
- 帧计时测试：原始 dt 可见，Idle 首次回调动画 dt 为 0，后续活动帧的 clamp/负值回退与显式 Rebase 行为保持可测。
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
