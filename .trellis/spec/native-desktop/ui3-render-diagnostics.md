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

## 公共 UI3 多目标无窗口基准

`Inkeys.exe --ui3-multitarget-benchmark` 调用真实主栏 coordinator 与四个分页生产回调，使用同一 `Scheduler`、共享 WARP epoch、真实 D2D/GetDC/ReleaseDC/EndDraw；只替换最终 HWND 提交 sink。主栏单独、PPT 四目标、白板两个可见目标分别覆盖 100%/150%/200%，每组预热 120 帧、采样 600 活动帧、三轮，默认光效开启。隐藏目标不得持续回调，白板最后一轮额外观测收起后的 Idle。活动负载使用真实标准按钮的连续按压动画（与输入反馈使用不同按钮）和每帧变化的页码内容；每个可见目标必须实际成功提交全部 600 个采样帧，不能以强制 Continue 的空回调充当活动帧。生产首次启动放置可能改变fold；无窗口fixture应在该批完成后按正常状态展开，并逐采样帧核对MainBar与真实Draw/Geometry图标、文字尺寸和可见alpha，main_visible_frames必须600；注册身份或成功提交数不能单独证明展开主栏负载。

- 基准输入经过生产 mailbox/dispatcher 和公共 `ButtonPointerState` 反馈；主栏输入使用无窗口标准按钮反馈适配器，分页使用真实已呈现命中与 `BarPageSurface::ApplyPointer`。这不是完整原生 WndProc、触摸驱动或主栏复杂属性手势的端到端延迟测量；相关取消/续接由独立生产 Pump 回归覆盖，原生链留人工验收。
- 原始 `*-metadata.json`、`*-batches.csv`、`*-callbacks.csv` 和 `summary.csv` 写入忽略目录 `TestResults/ui3-shared-runtime/`。检查 sealed、零掉样、同 batch frameTime、成功提交及预期倍率/活动帧数；计算、Draw、GetDC 与 batch 分别报告，不能把包含性阶段重复相加。main_culled_draws 在真实主栏 coordinator 提交后采样，需为正以证明公共脏区剔除进入生产路径；它不替代像素正确性。
- 热态目标为 batch P95≤16.67 ms、P99≤33.4 ms；同时报告主栏成功提交间隔、超过 50 ms 的异常来源和正常静置是否仍唤醒。资源计数按几何稳定的重复帧检查；回弹/改变几何产生合法新遮罩不能冒充缓存缺陷。
- `--ui3-input-worker-test` 直接验证生产注册、mailbox、dispatcher/续接、跨目标公平、超限取消和卸载排空；`InkeysHeadlessTests --no-window` 验证纯策略/状态合同。绘制/像素/命中/设备重建/失败提交回归使用 `--bar-eraser-offscreen-test` 中的真实公共实现。
- 基准通过也只证明此软件渲染事务和指定输入负载；不证明原生 ULW/DWM、真实 Office/WPS、触摸驱动或视觉帧率。未启动 GUI 时必须将原生转场与主观手感列为人工验收，不能由无窗口结果推断通过。

## 三态转场离屏采样与像素对照（2026-10-28）

### 1. Scope / Trigger

修改主栏三态动画、frame pacing、容量预约或转场性能fixture时适用。复用现有workspace-centering真实主栏coordinator、公共按钮/renderer及PageControl；固定16ms正确性Step不能冒充真实调度性能。

### 2. Signatures

- `Inkeys.exe --ui3-workspace-centering-test`：不设置以下性能变量时仍执行原几何/反向/失败正确性测试。
- `INKEYS_UI3_TRANSITION_PERF=pilot`：100%、一轮、每方向两次；`full`运行三倍率三轮，默认每方向30次。
- `INKEYS_UI3_TRANSITION_CYCLES=1..30`：完整模式的有界重复数，实际值必须随结果记录；不能将5次写成30次。
- `INKEYS_UI3_TRANSITION_PIXELS`：在原确定时间几何模式下额外保存选定帧的实际ULW source/viewport BGRA，用于前后对照；还须核对该attempt的完整呈现事务成功；不能同时当性能采样。

- `INKEYS_UI3_TRANSITION_SVG_TIMING`：仅转场性能fixture显式启用独立SVG数值采样；`INKEYS_UI3_TRANSITION_PERF=svg-pilot`选择100%/200%、一轮、每方向两次，不替代三倍率三轮验收。
- `Ui3SvgStageTimings`：`calls[Ui3SvgStageCount]`、`elapsedMs[Ui3SvgStageCount]`、`clockReads`，`Ui3SvgStageCount=Draw+1`，目前为Parse/Raster/Upload/Draw四项。
- `SvgStageTimingScope(Ui3SvgStageTimings*, Ui3SvgTimingClock=nullptr)`：独立owner线程TLS；注入时钟返回`steady_clock::duration`的tick计数，默认使用steady_clock；RAII恢复前一数值sink/时钟。

### 3. Contracts

- 性能模式走真实Scheduler和实际steady_clock；所有fixture状态/分页快照变更在同一render owner上，runner只等待完成，不并发裸写动画对象。保留默认动画/光效/字体路径、真实展开主栏和完整呈现事务。
- 六向分别报告batch、Bar、计算、Draw、GetDC、真实成功commit间隔及每transition内的target尺寸变化。成功样本使用生产stamp validator的Valid状态并核epoch/client generation/success serial；idle/失败/身份断链不串联间隔。空Continue不算成功帧，完整结束、sealed及零掉样必须核验。
- SVG数值scope不设置`CurrentUi3SvgScope`，不调用proof观察、NoteOperation或改变缓存/hidden/绘制决策；无数值sink且原probe关闭时，既有StageTimer不读钟。数值与原probe同时开启时共享同一对原probe读钟，原clockReads只增加2；null/嵌套scope按LIFO屏蔽及恢复。timer内只写固定数值，不分配、不做I/O、不持有COM资源。
- SVG分段复用实际Parse/Raster/Upload/Draw钩子，calls表示真实执行次数；Draw仅为CPU提交，Raster包含lunasvg内部工作，UTF转换/换色等前置CPU工作不在四段内，不将四段之和称为完整SVG函数或全部WARP耗时。
- 转场数值sidecar预先分配32768个固定帧槽，按Bar callback ordinal/frame_ticks/phase关联；Stop/join后写`*-svg.csv`，缺配、溢出或验真scope变化使fixture失败。RawCaptureReport原schema不变，metadata另记numericSvgTiming/svgStageCount/svgTimingDropped；新旧二进制证据分别保存。
- RawCaptureReport仍用既有schema。原始结果在忽略目录 `TestResults/ui3-workspace-transition/`，每个版本另存目录、EXE哈希、执行环境/倍率/光效/实际cycles；采样之间不并行构建或运行其它重负载。
- 当前fixture直接发布测试workspace，未覆盖真实Draw3/原生窗口协调、WhiteboardFreeze全屏背景或fresh容量；metadata必须明确这些边界，不能把场景直接切换称为完整业务请求到ACK的总延迟。首次新峰值扩容须另查实际尺寸序列，warm无resize不能外推cold。
- 像素读回/文件写出仅在测试标志启用且指定帧发生，不进入默认产品帧；同一确定时间轨迹按实际source/viewport比较BGRA，不能只比较bitmap尺寸或裁剪中心。读回发生在ULW sink内、EndDraw之前，失败attempt的图片不能冒充成功帧。差异必须定位原因，不能以平均差很低自动宣称等价。

### 4. Validation & Error Matrix

| 条件 | 要求 |
| --- | --- |
| raw掉样/未封口、失败/无效stamp或没有有效活动提交 | 该组不得宣称通过，保留失败和分母 |
| fixture使用700ms场景间隔/空Continue | 仅实际成功提交纳入活动帧；不计作光学帧率 |
| cycles超出1..30、初始化/注册/执行超时 | 非零退出；先Stop/join再释放测试资源 |
| 固定时间Step或像素读回模式 | 只用于几何/像素正确性，不纳入真实Scheduler性能分位 |

### 5. Good / Base / Bad Cases

Good：同机同环境三轮A/B，batch与真实成功间隔分别报告，独立timer-only和容量候选归因。Base：原geometry入口保持原断言。Bad：拿callback-end当commit、拿固定16ms序列当60FPS、只选择最快一轮，或把未覆盖Freeze/native标成已测。

### 6. Tests Required

SVG计量接缝须确定性红绿覆盖default/null零读钟、四段数值、nested/null恢复、proof共存只读同一对时钟、真实cache复用与整幅BGRA透明性；同一callback的数值行与raw逐一关联、sealed且零掉样。其短采样不替代完整性能矩阵。

完整Debug|ARM64 Solution、frame pacing与Scheduler的headless --no-window、workspace-centering几何/失败/快速反向、适用真实公共renderer离屏回归。容量/映射变化还需相同确定轨迹的完整viewport BGRA对照，展开属性/拖动/倍率与设备失败恢复单独覆盖；高分timer兼容路径不代表真实Win7已测。性能沿用热态batch P95≤16.67ms/P99≤33.4ms，并保留未达标组、成功间隔和>50ms来源；不以性能改善自动替代原生视觉验收。

### 7. Wrong vs Correct

Wrong：前一batch只画8ms，下一次开始31ms，仍因绘制小于16.67ms宣称流畅。Correct：同时测batch起点截止的超时和真实commit间隔，先确认等待/调度与绘制各自成本，再独立A/B；保持最大60Hz和真正idle不唤醒。

## 屏外原生窗口候选采样（2026-10-28）

`INKEYS_PAGE_CONTROL_TRANSITION_PERF=1 Inkeys.exe --page-control-hidden-test` 使用生产 RenderSurface、屏外真实四 HWND、ULW 与 Window Service；保留原 `frames.csv/summary.csv/metadata.json` 字段。新增 native/pixel commit 计数、owner 完成时刻、candidate→receipt 延迟、队列深度及 coalesced/stale 计数必须区分回调工作量与成功提交，不将快速 Retry 或空 Idle 算作呈现。

候选计时仅在 `SetCandidateDiagnosticsEnabled(true)` 时采集；默认关闭不为诊断格式化/写盘。提交期限使用的生产时钟与诊断读钟分开。`completedTicks` 为 owner 实际完成的软件时刻，不是 UI 消费时刻、GPU完成或 DWM 光学显示。队列深度必须为实际固定邮箱占用，短锁争用不能伪造峰值。A/B 保存双方 EXE SHA-256、metadata、原始 CSV 和真实进程退出码；原基线缺少新增时刻列时明确缺口，不事后把 callback proxy 当作旧原生成功间隔。

P3 候选回执的 error 保存首个原生失败，rollbackError 独立保存恢复失败；不能在回滚后才用 GetLastError 推测原始阶段。lease 延后是正常 pending，不计为真实 HWND 失败。P4 progress notification 不等于窗口成功或 UI-ready，失败预算只按原有时间尺度和实际进展推进。
