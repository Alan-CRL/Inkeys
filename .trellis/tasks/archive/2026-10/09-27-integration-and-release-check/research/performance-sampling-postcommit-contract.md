# Research: 提交后 UI3 / Draw3 生产采样合同

- Query: E04 如何复用当前生产计时组件，补齐有界、默认关闭、可重复的成功帧采样；区分指标错误、工程覆盖缺口和真实设备验收。
- Scope: internal；只读源码、任务与规范。唯一写入为本研究文件。
- Date: 2026-09-30
- Active task: `.trellis/tasks/09-27-integration-and-release-check`。
- 状态: 设计建议 / 待实施和独立检查；本次没有编译、运行、性能采样、GUI、Git 操作或代码改动。

## Findings

### 1. 结论与最小实施顺序

集成 Draw3 已编译真实 `RuntimeMetricsSession`，Controller 中亦有几何到成功 Present 的接入点，但 `Host` 不构造会话。该会话不能原样启用并宣称 E04 完成：失败帧计数、完成 contact 首帧失败后的样本保留、终帧统计和身份关联存在下列确定缺口。UI3 当前 TLS 帧诊断已经能正确区分 callback / advance / attempt / commit / Retry；异常聚合器不是逐帧分布，不应从它的 maxima 或汇总日志反推 median/P95。

建议分四个可独立检查的单元：

1. 修正并测试 `RuntimeMetricsSession` 的记录合同，保留缺失、失败、容量溢出等分母；不改变输入消费、时钟和呈现策略。
2. Host 显式采样选项接入会话，在实际 Controller 待提交帧锁存 contact 身份与模型/最终版本，成功 Presenter 返回后确认；以隐藏 Host 做三轮生产基准。
3. UI3 Scheduler 增加有界原始样本保留，复用现有 `FrameDiagnostics` 与计时边界；Bar 补缺少的阶段/真实提交时间与动画完成标记。异常日志桥继续保留。
4. 工程自动验证和可比 HF 采样完成后，再执行 HC/H2 同机视觉、真笔、Win7/其它设备验收；这些设备结论不能由软件测试外推。

### 2. 已找到的文件与真实调用链

| 文件 | 当前职责与证据 |
| --- | --- |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RuntimeMetrics.cppm:23`、`.cpp:138` | 生产指标类；32768 默认容量、最多 1048576，landing / 活动间隔 / work / Present 数组与 idle 计数。 |
| `Draw3.DrawingController.cppm:84`、`.cpp:4143` | 构造可选 `RuntimeMetricsSession*`，默认 nullptr；设备、文档和 renderer 仍在唯一绘制线程。 |
| `Draw3.Host.cpp:112`、`:1166` | Host 拥有真实输入/renderer/presenter/controller；构造 Controller 只传到 observer，默认 metrics=nullptr。`:1072` 目前仅隐藏注入时启用输入诊断。 |
| `Draw3.Host.h:80` | `HostStartOptions` 没有性能采样选项；`:101` 后的 runtime snapshot 有尝试/成功计数，却没有帧到 contact 的因果关联。 |
| `Draw3.Host.cpp:1490` | 明确 gate 的隐藏消息注入；`:1507` 在 owner 收到消息后读 QPC；`:1539` 发布 Down 到真实 `ContactInputCoordinator`。 |
| `Draw3.ContactInput.cppm:56`、`:149`、`:165` | 实际 snapshot.qpc / sequence / admissionRevision；ContactHandle=(record,generation)；占用槽和发布/回收累计值。Move 是现有 latest snapshot 合同，不能把所有已发布 sequence 自动当成均被模型消费。 |
| `Draw3.DrawingController.cpp:5772`、`:6081` | `metricEligibleQpc=down.qpc`；消费 latest snapshot，并记录 `lastConsumedSequence` / `lastInputSnapshot`。不是物理触笔时刻或保证每个发布 Move 可见。 |
| `Draw3.DrawingController.cpp:10345`、`:10508` | 完成 contact 在回收前、活动 contact 在合成前分别 `StageLanding`；仅具有可见几何且未 cancel 的 runtime 纳入。 |
| `Draw3.DrawingController.cpp:4334`、`:10594`、`:10657` | 实际 `presentation_.Present`，随后 observer；帧末 `CommitStagedLandings(presentSucceeded,QPC)`。 |
| `Draw3.HiddenWindowTest.cpp:83`、`:94`、`:846`、`:2491` | 真实 Window Service 的不可见 HWND 与 Host/RTS/modeler/D3D/presenter 集成；`WaitUntil` 每 10ms 是功能等待，不能作为亚毫秒延迟测量终点。 |
| `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cppm:112`、`.cpp:544` | `FrameDiagnostics`；Scheduler 当前只在 diagnosticsSink 存在时开启 TLS 和阶段时钟。 |
| `RenderPipeline.cpp:589`、`:612`、`:630` | callback 样本由本回调填写，随后加入数值聚合；批次/实际 callback 起止与请求/继续/重试掩码都有现成位置。 |
| `RenderPipeline.Diagnostics.h:48`、`:92` | 真生产聚合器，保存数量、sum/max、latest/slowest/lastFailure；没有逐样本数组。健康窗口按一秒清零，日志仅限频异常/恢复。 |
| `Bar.RenderLoop.cpp:13001`、`:13234` | 真实主栏 coordinator：WakeAndSnapshot → target/layout → 动画 → lighting/demand → dirty/draw/present。 |
| `Bar.RenderLoop.cpp:12623`、`:12636`、`:12653`、`:12695` | GetDC / ULW / ReleaseDC / EndDraw 和 `BarPresentDecision::CompleteAttempt`；只有完整事务成功才 `presentCommitted`。 |
| `InkeysHeadlessTests/render_scheduler_tests.cpp:15`、`:705`、`:812` | 直接使用生产聚合器、Scheduler、TLS 的确定性与生命周期测试；新增 recorder 应沿此入口。 |
| `Inkeys/Inkeys.vcxproj:1068`、`:1084` | 已登记 RuntimeMetrics 与 HiddenWindowTest，优先复用；共享工程文件只能由主 agent 或指定唯一 owner 编辑。 |

旧 standalone `inkStrokeModelerTest/main.cpp:42` 支持 `--metrics-output` / `--strict-metrics` 并构造旧源会话；当前产品 `Inkeys/IdtMain.cpp:667–737` 没有这些参数。`inkStrokeModelerTestTests/runtime_benchmark.cpp:118` 自动操作 standalone 可见窗和全局 SendInput，不能冒充已集成主产品基准，也不应直接拿来操纵用户桌面。

### 3. 确认的指标问题与覆盖缺口

| ID | 分类 / 精确证据 | 影响与最小合同 |
| --- | --- | --- |
| PM-01 | 已确认指标口径：`PresentFrame:4341` 不论成功失败都调用 `RecordPresent`；`RuntimeMetrics.cpp:255` 无条件加 `totalPresents`。 | JSON `presentCount` 是尝试数。保留尝试、成功、失败三个独立计数；不能把当前数叫有效帧率。当前产品 profiling 关闭，不能由此推断用户画布故障。 |
| PM-02 | 已确认漏样条件：Session `BeginFrame:197` 和失败 `CommitStagedLandings:218` 清候选；Controller `10345–10392` 已将 ended runtime 回收并清 metric 字段。 | Down+Up 在首次 Present 前完成、该帧失败、之后同页恢复成功时，原 contact 没有 runtime 再 Stage，漏掉最慢 landing。保留纯值的待呈现 contact，到同页/同 raster 内容成功帧才确认；Clear/换页/Cancel 等必须显式标未呈现/被取代，不能把其它页成功错配。单纯删除 BeginFrame.clear 不足以处理换页和失效。 |
| PM-03 | 已确认分布覆盖不足：`RecordActiveFrame:10694` 只在帧后仍有 physical contact 且非 eraserIdle 分支记录。 | Up/Cancel 最终帧、同帧单点完成、无 contact 的恢复/页面/粒子帧未进入 work/Present 分布。保留每次真实 render attempt 的帧记录，再按 reason flags 选物理书写活动子集，终态另列；保持现有 pacing。 |
| PM-04 | 已确认计时口径：传入 `workMs:10693` 包含 Present 墙钟；landing QPC 在 `10659` 才读取，晚于 Present、ready/其它帧末工作。 | `workDurationsMs` 不能称线程 CPU 或排除 Present 的绘制时间。真实成功返回 QPC 在 `PresentFrame` 紧邻调用结果锁存，CPU 与 wall 分开。帧末迟读是软件延迟的较松上界，非光学时间。 |
| PM-05 | 已确认报告不足：Session `MeetsStrictThresholds:282` 只要求全部工具混合 landing≥200、frame intervals 非空；WriteJson 只有 P99，没有 median/P95、分类/完整性标记。 | 不保证每个工具/场景足量，也不保证 frame P99 有样本量。保留 legacy 门槛含义，发布判断使用每个 population 的 count、median/P95 和证据等级；小 P99 仅探索性或 null，不能直接升级为 PASS。8.33/9.5ms 的旧 demo 阈值不能直接套 UI3 60FPS。 |
| PM-06 | 已确认保留边界不足：`:205` / `:225` 到容量静默停止；stage 上限64；`:145` dedup 初始 reserve512，`:156` 对全部既往 key 线性查找，`:227` 热路径继续增长。 | 内存有逻辑上限，尚非固定热路径分配，长期采样可能自身带入成本；没有 dropped/truncated 分母。会话开始一次预分配，固定容量写入，容量满显式 retained/seen/dropped，不再悄悄筛选头部为全程。dedup 至少保留有界身份映射；复杂度改动只能称观测成本控制，未实测不称产品性能收益。 |
| PM-07 | 已确认覆盖缺口：Host 不创建 Session，runtime snapshot 的 present count/pen mutex/多原子值不是同一帧载荷。 | 新增明确 opt-in 激活，不读取快照拼接同笔延迟。目标是 Controller 构建 frame observation，再由实际成功 Present 确认。 |
| PM-08 | 已确认覆盖缺口：现有 FrameDiagnostics 没有 raw retained callback、成功提交 serial/time、动画 target/completion tuple、SVG/path 分段；RenderPipeline 聚合仅 maxima。 | 用旁挂 recorder 复制数值帧和元数据，不改 HealthySummariesEnabled，不解析每秒日志当逐帧数据；保留 opt-in 异常日志。 |
| PM-09 | 已确认覆盖缺口：`totalFrames` 只加于 RecordActiveFrame；idle 测试只对该计数检查增长。 | 该值可证明已定义的活动记录不增长，不能证明 idle 期间所有 loop/wake/raster 回调为零。另列 loop/wake/renderAttempt/success，区分合法保存 completion、Hold 到期、Retry 的唤醒。 |

这些是指标 correctness / 可观测性事实，尚无本轮动态复现。不要将它们作为新的已确认崩溃、输入丢失或性能退化 finding。

### 4. Draw3 生产观察合同（建议，尚非已有 API）

#### 4.1 启用、所有权与导出

- `HostStartOptions` 可加显式采样布尔和容量；默认 false。Host 每次 Start 形成新的 session/run serial，绘制线程持有并调用 Session；Controller 借用指针的寿命覆盖 Run，绝不暴露 GPU/document 所有权给 runner。
- 指标会话与输入数值诊断只在显式启用时构造；构造/容量失败关闭该诊断并报告一次，不因采样把正常产品变成启动失败。不得在热路径写 stdout、日志字符串、文件或 GPU query 等待。
- 导出在采样封口和绘制线程 join 后进行，写 runner 指定的本任务独立根。正常 Stop/保存 drain 不为导出增加新无限等待；强退无法导出时记录 incomplete run，不能捏造尾样本。
- 导出路径属于测试/诊断，不复用真实 UInk/config 目录；不用 CreateAlways 覆盖用户文件。显式 CLI 由 root 解析、控制隔离目录；Session 只写一个正式完成的离线报告。

#### 4.2 因果身份及可见语义

最低 frame observation 为固定数值结构：`runSerial/frameSerial/presentAttemptSerial/presentSuccessSerial`、输入源种类、真实 `driverType/featureLevel/presenter`、`device/raster/output generation`、workspace/page/session 运行时身份、dirty、fullPresent、实际 HRESULT/result、frame start/consume/present start/present return QPC、reason flags 与 physical contact 前后数量。

contact observation 要携带：`ContactHandle.record` 的会话内匿名 ordinal + `generation`（不导出地址）、admissionRevision、Down QPC、实际 consumed sequence/qpc/phase、model 输出采纳版本、已栅格化版本、最终 Stored history item/raster token。生产代码已经有 `runtime.lastConsumedSequence`、`lastInputSnapshot`、owner workspace/page、history revision / rasterState，复用它们。record+generation 只可在当前 run 内判等，不能跨 Reset 将同址同代当旧 contact；pointer 不得被导出或之后解引用。

规则：

1. Down 的起点为真实 `down.qpc`。RTS 是软件采样/回调时刻；隐藏注入是在消息 owner 收到时读取 QPC，须命名 `synthetic-owner-Down-to-PresentReturn`。可另记 runner send QPC/消息接收 QPC，但不混在同一分布。
2. **候选 frame 在绘制线程实际构建时锁存 contact/token**；只有候选对应几何/合成完整成功且该 Presenter 成功返回时确认。不要在成功 observer 中重新读取“最新 pen diagnostic”倒填旧帧。
3. 同时多 contact 使用固定容量表；overflow 明示，不任取第一支作整帧代表。当前 Stage 上限64与真实并发能力不能未经检查等同。
4. 首次 landing 只确认一次；失败后 completed 候选仍保留纯值，待同页权威恢复帧确认。页面切换/clear/cancel/停机/不可恢复设备故障需结束并标 status，不能默默删除或等新页面一帧即报成功。
5. Move 指标是 **真正被模型消费且进入该候选几何的 sequence** 到成功返回。现有 mailbox 覆盖导致未消费的源 sequence 必须分类计数，不能声称每个 PublishMove 都呈现；不为指标增加 latest-only 策略、不减少既有必要样本。
6. Up→稳定至少同时绑定 terminal sequence、对应 Stored history/raster token 和成功帧；后续无新输入/工具变更时短安静窗口不得再修改该笔最终 token。Laser 不进入普通 Stored history，其终态、Hold/Fade/粒子过程另列，不能借同一“final L2”语义伪造最终稳定。
7. `contentRevision` 主要服务内容真值/页边界，并非每个 Down/Move/同非空页新增笔的身份。`:4524` 的常规发布仍以 hasContent 是否变化决定；页切换另有强制发布。不修改该业务合同来塞每包计数。

#### 4.3 成本字段和资源

- 每帧至少有总 wall、命令/输入消费、modeler/prediction、几何/dirty、upload/raster、合成、Present、wait 的计时；计时包围已有批次，不能每个片段/像素读时钟。几何与上传现在交错，若不拆 owner 调用，诚实报告 `geometry+rasterSubmit` 合并段，不人为相加产生重复耗时。
- `Present` 有 CPU/GPU 同步/ULW readback 等包含成本，软件计时不可拆称纯 GPU。真正 GPU duration 没有异步 timestamp/disjoint 合同时标 unavailable；不能同步 flush/wait 来取得“漂亮”数据。
- `GraphicsDeviceResources` 已有 driverType/featureLevel，Host 可在绘制线程启用后一次抄入元数据，设备恢复时推进 epoch。不从 CPU 架构猜 HARDWARE/WARP。
- 输入 diagnostics 是累计发布/槽容量占用；queue `size_approx()` 是近似 pending command/Down 指针数量，绝不是 Move 样本总深度。最低字段可先报 `occupiedSlots`、已发布/已消费序号差和未消费计数各自语义；若需要 queue 分布，由 ContactInput owner 增加只读近似字段，禁止 published-recycled 反推队列。
- `InkHistoryGpu` 的 capture/restore result 可观测 path/tile/status，当前没有全套 hit/miss/create/evict 导出。第一轮可复用真实 result 计数；GPU allocation/evict 若没有内部 hook 即明确 unavailable，不能以 command 数冒充 cache hit。
- 进程 private bytes / working set / handles / GDI/USER 在 warm-up 前后和长场景周期采；线程 CPU 用真实 thread-time delta，不能用 workMs。可得显存指标需要平台能力探测，Win7 没有的可选 API 不得静态导入。

### 5. UI3 bounded raw capture（建议，尚非已有 API）

- 放在现有 RenderPipeline 诊断层，旁挂数值 recorder；不增加第三套 telemetry 或日志线程。`diagnosticActive = anomalySink || sampleCapture`，因此只启用 raw capture 时 TLS/FrameStageTimer 也有效；关闭 raw capture 不卸载生产异常 sink。
- Recorder 在开启前预分配固定容量，可采用填满停止保留而继续总 seen/dropped 计数。容量和总预算有明确上限；写入口只由 Scheduler thread 调用 `noexcept` 数值复制，导出和 sort 在结束后；不允许 raw sink 每帧做任意用户 callback/I/O。
- 每个 retained callback 复制现有 `FrameDiagnostics`，加 `batchSerial/client/callbackGeneration/requested|continued|retried/start/end/frameTime/idleEpoch`。批次另有 start/end/recovery result 与 cost。只在 capture 下加必须的 QPC；不能改变用于动画 `Tick()` 的 frame time。
- 现有 client activity/reset/idle 判据可以供 recorder 共用，以 `(client,callbackGeneration,epoch,idleEpoch)` 切段。callback duration、活动 callback gap、成功 commit gap 分开；barSampled=false 的 Settings/PageControl 不能因 result=Continue 自动成为 Bar 成功呈现。
- 保持 `presentCommitted` 的完整 GetDC+ULW+ReleaseDC+EndDraw 事务。Bar 在 `CompleteAttempt` 成功附近锁存提交 QPC；若暂只保存 callback end，报告必须名为 `commit-callback-end proxy`，不能称 API 精确成功时间或像素光学可见时间。
- Bar 增加现有职责阶段的 opt-in timer：WakeAndSnapshot、SubmitTargetsAndLayout、AdvanceAnimationsAndDeriveLayout、PrepareLightingAndDemand、资源准备/dirty，与当前 Draw/GetDC/ULW/EndDraw 合起来归因。为了“计时拆段”移动业务调用或改变布局/dirty/cache 策略是不必要风险。
- 动画实际完成时间需在 scenario start 记录目标/交互 revision，在 coordinator 已消费该目标且所有适用活动值收敛、最终候选完整成功 commit 时标完成；快速反向令旧场景 cancelled，不能把第一次 Idle 当最终帧或把主光/鼠标活动当布局动画永不结束。无需为了测量引入全动画 registry。
- 光影已有 rounded/geometry mask hit/miss/create/failure/time、exact hit/fallback 与 slices。没有 evict/SVG/path 独立时间时标 absent。若补 SVG/PNG 的 parse/build/raster/upload/draw，owner 改 `Bar.Rendering.cpp` / cache 实际边界，计数必须指真实发生；不调 cache key、量化/画质或关闭动态光影。

### 6. 最小文件所有权与集成边界

| 写入者 / 单元 | 允许修改文件 | 不应借此改动 |
| --- | --- | --- |
| 唯一 Draw3 metrics 实施者 | `Draw3.RuntimeMetrics.cppm/.cpp`、`Draw3.Host.h/.cpp`、`Draw3.DrawingController.cppm/.cpp`；必要时明确扩展 `Draw3.ContactInput` 只读 diagnostics | 输入算法/采样率、modeler 参数、画质、设备合并、呈现/失败事务、持久化接受合同。 |
| 唯一 Draw3 harness 实施者（在合同冻结后） | `Draw3.HiddenWindowTest.cpp/.h`；直接 import/use 实际 Session 的 no-window probe 或 headless fixture | 不读用户配置/文档，不从 10ms wait 推算延迟，不复用独立 demo 结果。 |
| 唯一 UI3 diagnostics 实施者 | `RenderPipeline.cppm/.cpp/Diagnostics.h`、`Bar.RenderLoop.cpp`、必要时 `Bar.Rendering.cpp` | 不更改时钟、duration、frameInterval、dirty/present/retry、cache invalidation、动画功能。 |
| UI3 验证者（在接口冻结后） | `InkeysHeadlessTests/render_scheduler_tests.cpp`、现有 Bar offscreen fixture；真实 Bar runner 可放测试归属文件 | 假客户端仅验证 Scheduler/Recorder 合同，不能报告主栏真实 ULW 性能。 |
| root 串行集成 | `IdtMain.cpp` CLI、项目/filters 登记、父任务/规范、构建输出目录 | 与 E01 的 SetOffSignal/startup 改动冲突；必须等 worker 交回再编辑。 |

UI3 / Draw3 两个模块可以并行静态实施，但性能采样必须等所有编译/检查/扫描停止，且 runner 独占。已有 Host 与 Window Service 新的退出边界由 E01/E02 owner 协调，不在指标改动中重新设计。

### 7. 确定性验证：调用真实生产类

| 测试 | 使用真实实现与所需断言 |
| --- | --- |
| 成功/失败分离 | 实际 Session 或生产 Present observer probe；失败两次再成功，attempt=3/success=1/failure=2，不会记录失败 landing。 |
| 单点/Up 帧失败 red→green | 真实 ContactInput 身份，Stage 后完成回收，首次 Commit(false)，同页权威帧恢复 Commit(true)，恰一条 landing且包含失败等待；切页/clear 的反例不得确认。 |
| 同址新 generation / 新 run | 相同 record 的新 generation 不抑制；旧身份不能计入新 frame；Reset/start 新会话隔离。 |
| Cancel / 快速反向 / 换页 | 未呈现、取消、被取代、超时各有 count/status，成功分位数分母不偷偷缩小。 |
| 已呈现 contact / 多 contact | 同一 contact 多帧只一次 landing；同时多 contact 均归属该帧，overflow计数明确。 |
| 终态与无接触帧 | Up final、恢复、页面、Laser fade 帧都有原始记录，但 physical-writing interval 只连合法连续区间；idle首帧不跨时段。 |
| 容量 / 异常值 | 真实 recorder/session 小容量触发截断；seen/retained/dropped 一致。无样本的 median/P95/P99为 null，invalid QPC/负间隔不被 clamp 成漂亮0ms。 |
| 统计与样本量 | 直接调用生产报告函数，对小已知数组断言固定预期 median/P95；不足 population 不可靠 P99，严格标志不能掩盖未覆盖工具。不要在测试复制实现一套 percentile来相互对照。 |
| UI3 raw-only/off | 真 Scheduler：无任何诊断时TLS=null；raw-only非空且不调用日志 sink；callback后TLS失效；Stop/restart无保留悬空。 |
| UI3 legal Retry / failure | 真 DiagnosticsAccumulator/Recorder，同一次 callback 中 Retry但没有API失败不会变failure；失败后只有真实commit可成为呈现恢复。 |
| UI3 idle / generation / sink背压 | idle后首帧不算活动gap；重新注册client切断；异常sink拒绝/抛错不改变raw保留、不阻塞render；overflow不增加渲染请求。 |
| UI3 慢 Settings | 真 Scheduler + 假客户端验证批次与Bar等待归因；假回调故意慢仅用于确定性合同，不作为产品性能结果。 |
| 生产隐藏 Host | 沿当前 Window Service + StartProduct + mailbox + Controller/modeler/D3D/Presenter，验证每条候选对应成功帧，源事件顺序与完成笔数不变。 |

每个修补保存旧实现失败、新实现通过的最小证据。之后根代理独占输出执行完整 `InkeysRepo.sln Debug|ARM64`（原生MSBuild，同PowerShell规范Path，至少5分钟），Release|ARM64，相关三架构/CLIs/headless；新 RuntimeMetrics 若补进测试工程由共享工程 owner 登记。真实 HWND 测试不是严格 no-window，必须另记。

### 8. 可执行 runner 形状、环境与三轮方案

以下是**待 root 批准/实施的命令形状，当前不存在，不得现在运行或记录PASS**：

```text
Inkeys.exe --draw3-presentation-benchmark --metrics-output <isolated-absolute-report> --round <1|2|3> --presenter <dcomp|ulw>
Inkeys.exe --ui3-presentation-benchmark --metrics-output <isolated-absolute-report> --round <1|2|3>
```

root 应用 `CommandLineToArgvW` 严格解析、参数完整校验，在配置/单实例/正常启动之前转入专用 fixture。最小第一版可以只接受固定场景集与独立输出目录，不必新增任意生产配置。输出记录实际CLI版本与代码/EXE散列，失败退出码非0；缺少必需硬件/backend则 SKIP/NOT VERIFIED，不回退后继续标指定backend PASS。

Draw3 复用隐藏 Window Service 四窗链、真实 StartProduct，明确 force DComp/ULW。UI3完整主栏可复用 `Bar::Initialize` / `InitializeWindow` / `InitializeUI` 和 `BarUISetClass::Rendering`，以独立窗口/测试配置和实际 Bar coordinator 操作；仅用现有 `--bar-eraser-offscreen-test` 或 Scene 画图不能升级主栏 ULW / scheduler完成时间结论。若普通生产窗口 profiling 更小，亦可在隔离程序副本上 opt-in recorder，用定向自有 HWND 消息做固定轨迹，然后正常结束导出。

runner 仅向自己的 HWND 用有界 `SendMessageTimeoutW`/PostMessage发布消息；窗口消息处理、输入/绘制各自owner不变。不使用 computer-use、不用全局 SendInput 抢用户焦点、不终止用户进程。SendMessage返回只证明接收，真正采样终点由绘制线程QPC/成功frame生成，不能在 runner Poll结束时打延迟终点。

冻结方法：

- 每 renderer/backend/工具/轨迹三次独立进程，不重用跨轮缓存。每轮≥16笔/动画完整预热，≥200 Down 条目及足量活动帧；1000有效事件/群体才将 P99 用作稳定尾指标的候选，仍说明轮间噪声。少量场景只给median/P95和count，P99探索性/null。
- 冷启动/首交互单独保留，预热不覆盖冷路径。UI3含展开/收起/快速反向/拖动吸附/属性颜色粗细/FineDial/SVG/光影/设置竞争/idle；动画最终完成需关联最后成功commit。
- Draw3普通软硬笔、荧光、固定/速度橡皮、Laser及已开放形状单列；轨迹含单点、慢、快、折返、停住再动、Up、Cancel、快速换工具、多contact。不要为缩短报告混合成一种工具。
- 每源事件固定QPC轨迹/坐标/压感及必要 Down/Move/Up/Cancel 数，报告发送/发布/实际消费/成模/栅格/成功帧数。模型消费与发布速率不同不称输入样本无丢失；对遗漏和失败可回溯。
- 测量期间无编译、静态扫描、其它基准/测试；记录OS/补丁、CPU/GPU驱动、供电、DPI/刷新率/多屏、Release配置、实际UI3和Draw3设备/presenter、主题/效果、QPC频率、容量/掉样、raw文件。clean测量不等于禁用正式效果。
- 每轮单列median/P95/count/longframe比例、activegap、Down/Move/Up软件延迟、失败/超时、内存/句柄/可得显存、cache实际计数；汇总显示三轮并给噪声，不挑最好轮。不将块均值分布叫逐帧P95。
- 完成正常短场景后才跑长文档（约1000笔、undo/redo/页面/clear/save）与60–120分钟趋势。保存fixture必须唯一根并保留所有接受请求；指标不更改durable保存或正常退出屏障。

发布性能判据沿父任务预先冻结：同可比环境 median 超轮间噪声且>5%、P95超噪声且>10%是实质退化；Down/Up尾延迟、画质和输入语义各自独立。只是加入测量时不可先承诺修复收益，需由数据驱动下一项最小修补。

### 9. H0 / HC / H2 / HF 和能力边界

- H0在父 performance.md锁定 `8b156fca59f0337a6afc6d722941666fcf143080`；已提交阶段HF为 `e32a5fc0`，后续收尾须加实际工作区指纹/EXE散列。研究代理本轮没有使用 Git 命令重新证明这些身份。
- HC候选为 run31487748238、`82f7b7c0`、ARM64内层EXE SHA256 `81a3dbb26a845308ea2aafe7869844b389968e31f3798aee2e0987f947a6d07e`；仍未证明是用户安装过的“上一个Canary”。H2公开 Release20260713a，EXE SHA256 `2300b276aac3402e87b5c6a11ca39a81f2b06f7643f14f8ab85acd830f2635a5`。
- 现有H0/HC/H2并无新逐帧观察合同。HF软件新raw只可描述HF或同一生产采样补丁下的可比源码构建。若给H0同样观测补丁，必须标明为H0+instrumentation、有最小仅诊断diff和同Release/effects，不能把修改过的基线悄称原H0。
- 两个旧二进制已经被隔离启动不代表完成相同轨迹/呈现采样。跨版本比较只能用双方都有的定义/外部观察（例如一致捕获或光学），不能用新HF内部QPC和旧版本主观描述证明胜出。
- 隐藏窗口成功DComp/ULW调用可能受到OS隐藏/遮挡策略影响；只能叫该fixture的软件PresentReturn，透明桌面正确性、真实屏幕光子、笔硬件、混合GPU、刷新率手感仍需真机。
- Win7 SP1仅KB2670838合同保持；用户实测FLIP_SEQUENTIAL可用是当前约束，不因微软通用文档冲突回退bitblt。Hardware FL11.0可用/不可用→WARP FL11.0分别复验；只DComp/ULW，两个DWM方案禁用。此研究没有Win7运行证据。

### 10. Related specs / 既有研究的适用边界

- `.trellis/spec/native-desktop/ui3-render-diagnostics.md`：TLS寿命、数值热路径、advance/attempt/commit区分、idle排除、1秒异常日志与非阻塞sink；raw capture应扩展而非删除此合同。
- `.trellis/spec/native-desktop/rendering-and-ui.md`：实际Bar/Setting/共享Scheduler、FrameAnimationClock与呈现事务；保持不同窗口实现职责。
- `.trellis/spec/native-desktop/draw3-integration.md`：集成Host与HWND、独立设备/线程、保存worker、selection完成帧、Win7/FLIP门禁。
- `.trellis/spec/native-desktop/input-and-ink.md`：同一个输入/Closing/页代次、默认关闭有界诊断、不可删除必要样本。
- `.trellis/spec/native-desktop/errors-logging-and-resources.md`：资源/退出/报告事务；不要为了日志扩大关闭等待。
- `.trellis/spec/native/{runtime-and-rendering,quality-and-validation,platform-and-resources}.md`：旧Draw3模块事实与ABI/三架构/Win7约束；其中旧standalone `--metrics-output` 命令和窗口路径不能机械套集成产品。
- `.trellis/spec/guides/code-reuse-thinking-guide.md`：复用生产逻辑、CPU/GPU字段和调用合同，不在测试复制正确算法。
- 父任务 `performance.md` / `research/baseline-sources.md` / `completion-and-manual-acceptance.md`：已有数值仅CPU子段、冻结门槛与E04欠账。
- 既有 `research/draw3-hidden-end-to-end-benchmark-design.md` 所列“Host snapshot不能关联指定笔画”的结论仍有效；本研究进一步找到已编译 Session/Stage接口，建议复用并修正，而非另起一个独立指标系统。

## External References

本题以内仓生产代码和已冻结用户约束为依据；没有新增网络查询。未拿旧demo、公开DXGI泛化文字或未取得设备数据作为本产品已测证据。任何未来API/Win7/GPU支持声称应独立核验官方接口与真机，当前只提出可选测量且不得静态引入目标外API。

## Caveats / Not Found

- PM-01–09是静态核实的指标/覆盖事项，本轮无动态red→green，不标行为PASS或确定现场根因。
- 当前没有生产 benchmark CLI、逐Move/Up成功token、完整UI3原始帧导出、全部缓存evict或GPU timestamp合同；表中的新增字段/命令均为待实现设计。
- 只启用原Session能取得一部分Down样本，不能闭合UI3/Draw3全链路、所有工具、Up尾延迟、PPT切换、资源趋势或HC/H2对照。
- 本次只写research，不改spec/project/代码/测试；实现前由主代理冻结接口/唯一owner和输出目录，再由独立checker看实际diff与测试。
