# E04 UI3：有界原始采样设计

日期：2026-09-30。Active task：`.trellis/tasks/09-27-integration-and-release-check`。负责人：`contact_failure_identity`，复用完成 E03 的 implement worker。**本文件仅为设计，尚未改 UI3 源码/测试；等待 root `GREEN_DESIGN`。** Controller 所有权已交还 root，不再写 Draw3 文件。

## 事实与最小行为缺口

- 当前生产 `RenderPipeline.cpp` 的 Scheduler 只在 `diagnosticsSink` 存在时创建 `FrameDiagnostics`、启用 TLS、计 callback 和阶段时钟（544–614）。没有 sink 的实际 callback 中 `CurrentFrameDiagnostics()` 为 null。
- `RenderPipeline.Diagnostics.h` 的实际 `DiagnosticsAccumulator` 保留 counts/sum/max/latest/slowest/lastFailure 和健康静默/异常限频/恢复链；并不保留逐帧分布，不能把 max 或每秒汇总解析成 median/P95。
- 同一 Scheduler 串行按 DispatchOrder 调 Bar、StartupPreview、四 PageControl、Settings、Freeze；使用既有 frameTime 与 16,666,667ns pacing。不同客户端调用与 Idle/Continue/Retry/DeviceLost/Stop 的合同保持。
- Bar 当前 `presentCommitted` 由完整 GetDC/ULW/ReleaseDC/EndDraw 与 `BarPresentDecision::CompleteAttempt` 共同决定（Bar.RenderLoop.cpp:12623–12702）。Scheduler 仅有 callback end；在本单元无 Bar 改动时，成功间隔的终点必须叫 **commit-callback-end proxy**。

本单元补充旁挂 numeric recorder，不把异常 sink 改成逐帧日志，不增加任意 raw sink 回调、线程、文件 I/O 或新的渲染请求。不能据加入采样宣称性能提升、完整 Bar 阶段计时已完成，或指定 Canary/Inkeys2 已胜出。

## 所有权与分单元

| 单元 | 唯一写入范围 | 出口 |
| --- | --- | --- |
| U04-R，当前设计 | UI/RenderPipeline/RenderPipeline.cppm、.cpp、Diagnostics.h；InkeysHeadlessTests/render_scheduler_tests.cpp | 固定容量 raw callback/batch 记录、真实 Scheduler/TLS/生命周期测试、Debug/Release 构建与独立 review |
| U04-B，后续另冻结 | Bar.RenderLoop.cpp，必要时 Bar.Rendering.cpp | 真提交时间/Bar 分段/动画完成与 SVG/path 实际边界；未批准前仅只读 |
| U04-F，root 集成 | CLI、工程/filters、独立真实 Bar fixture、离线导出/场景报告 | 显式激活、隔离 HWND/数据、实际三轮 Release raw 采样；renderer 与 GPU 真机范围另验 |

禁止本 worker 修改 Main、Window、Draw3、Settings、工程/spec/设备/功能 gate。root 独占构建/CLI/性能采样和规范账本；其它 writer 的现有改动保留。实现、检查、三轮采样分阶段，采样期间所有编译/扫描/其它基准停止。

## R1：最小公开 API（拟议，当前不存在）

在现有 module 中导出纯数值 DTO，以及 Scheduler 和全局 scheduler 的两个对应方法：

```cpp
bool Scheduler::ConfigureRawCapture(std::size_t capacity = 32768);
std::optional<RawCaptureReport> Scheduler::TakeRawCapture() noexcept;
bool ConfigureRawCapture(std::size_t capacity = 32768);
std::optional<RawCaptureReport> TakeRawCapture() noexcept;
```

- 只允许在该 Scheduler 完全停止且上一线程已 join 的所有者阶段配置，容量 0 显式关闭下一轮；默认构造不配置、没有 recorder/数组分配。
- 不支持运行中重新配置或边写边导出。`TakeRawCapture` 对 running / stopping / 尚未 seal / 已取走返回空，立即拒绝，不等 callback、GPU 或日志。它只在 stopped seal 后移动所有权，不复制半成品，不暴露 live recorder 指针/span。
- 使用既有 callbackMutex 的 **try_lock** 检查与短发布，不增加 API 等待。配置先短锁核 stopped/无既有buffer并预约一次配置，再锁外在调用线程分配候选，最后 try-lock复核并发布；配置预约用atomic状态，第二个Configure立即false，不允许多个候选同时耗预算。并发Start照常无raw启动，使尚未发布候选被拒绝/在调用线程释放；Start不等配置完成，也不因该诊断竞态失败。任何失败均撤销预约。分配/容量失败不改变正常 product 初始化或原异常 sink。
- 初次短锁要读取单调 `lifecycleEpoch`（每次 Start、Stop 完结都推进）并分配唯一 reservation token。第二次短锁必须同时核对 token 和 epoch；即使分配期间完成 Start→Stop、再看到 stopped/joined，也要拒绝旧候选。`try_lock` 失败时，先在调用线程释放本地候选，再以仅属于自己的 token 清理预约；不等待 Scheduler 锁或 callback。
- 已 seal 的未取走报告不被下一次非零配置静默替换：先 Take；配置失败需 runner 记录 NO_CAPTURE/未验证。容量 0 只禁用未来激活，未取走的 sealed 报告仍可取。
- 已有尚未运行的Prepared buffer也不在后台叠加另一个非零候选；需先容量0解除，再配置。分配预约尚在进行时，容量0立即返回false，不能提前清掉其 token 让第二个64MiB候选同时分配。取消已有Prepared buffer时先短锁移出，发布 `Releasing`，再由调用线程锁外释放；释放完成前其它Configure立即false，Start可继续但不启用旧buffer。释放者最后只解除自己的Releasing token。这样Scheduler拥有、正在构造或尚在释放的数组至多一份；Take后保留旧报告是runner显式拥有的离线内存，另行释放并记资源。
- 每次需要采样的 Start 都需停止后显式配置；Stop/restart 不能把旧会话默默接着写。未取走的旧报告保留只读，下一次普通 Start 可无采样运行。
- 全局 Configure 必须由 root fixture 在 `RenderPipeline::Initialize()` 调用 Scheduler::Start 之前使用；当前 Initialize 不先重置 scheduler，可沿原 Start 入口激活。它只测从 Scheduler 启动/客户端注册之后的 UI3，不含更早共享 D3D/D2D/DWrite 初始化成本。

这些生命周期操作的 Start/Stop 继续由现有单一 owner 串行，不能在自己的 render callback 调 Stop/Unregister 自己。本 API 不承诺修复现有非法并发 Start/Stop；新 Configure/Take 可以与 Stop 请求并行安全地返回空/false，无新增 unbounded wait。

### 预约与报告的状态表

`nextRecorder` 的内存所有权单独取 `Empty→Reserved→Prepared→Running→Sealed`；`Sealed` 通过Stop后的同锁发布移至 `sealedReport`，`Take` 再把它移给runner。`Releasing` 是容量0释放旧Prepared或分配失败后的短暂状态，只有原reservation owner释放候选后能转回Empty。一个已seal未Take的报告可和下一次**无采样**的普通Start并存；下一次Stop不得删除旧报告。由于sealedReport存在时非零Configure被拒，不能静默叠加另一采样会话。cap0在Reserved/Releasing返回false，在Prepared仅按上述释放；在Empty或仅有sealedReport时不删除报告。

Start的running预标志发生在callbackMutex之前；该短窗口不创建render thread。Configure第二次锁中若已看到running=true则拒绝；若先发布Prepared，Start随后取得同一锁并消费它。成功Start在原callbackMutex阶段推进lifecycleEpoch，使任何尚在锁外分配的旧预约失效；不释放该预约自己的候选。Stop只在原join之后同锁推进epoch并封口；重复Stop不清sealedReport或重新激活旧Prepared。分配抛异常、第二次try_lock失败或epoch/token不符时，本调用先在锁外释放自己的候选，再以自己的token撤销预约；不能清除已由其它所有者替换的状态。析构只在既有Stop/join边界之后释放Scheduler仍拥有的buffers；已交给runner的报告不被反向引用。

### 已 join 的 publication 证明

当前 `running=false` 会由 renderThread 在 final controlTasks 执行前发布，**单看 running=false 不能说明已经 join**。新增 recorder 交接状态（如 rawThreadJoined / sealed），在 callbackMutex 下于 Start 启动前置为 false，只有原 Stop 的 join 完成后才置 true并 seal；Configure/Take 同 mutex 的 try-lock 核它，不读取与 join 并发变动的 `std::jthread::joinable()`。`lifecycleEpoch` 还要覆盖停止后的空转周期；完成 Start→Stop 必改变 epoch，不能只比较当前 running 值。

- Start 在原 callbackMutex 设置 provider 的短段里发布 Prepared→Running 和稳定 recorder；之后才创建原渲染线程，thread 创建同步发布这些先行写入。
- 写入口只由唯一 Scheduler thread 调用，整个 run 内 recorder 地址不变；Stop 请求时不释放它。
- Stop 继续原 stop flag→event→join，随后原清理短锁内仅 seal 数值；不加导出/sort/日志尾部等待。Take 移出 buffers 后，runner 拥有所有字节；即使 Scheduler 析构或新 run 开始，报告无悬空。
- Unregister 的既有同步 drain 不变。callback 样本在原 activeCallbacks-- 前保存，callbackGeneration 在原获取 callback 的同一短锁取值；注销/re注册形成新代次，不把新槽连到旧活动链。

## R2：资源边界与数值 payload

- 第一版 capacity 同时限定 callback 与 batch 两个预分配数组，默认开启值32768，最大65536；总硬预算 **64 MiB**。按 `capacity <= maxBytes / (sizeof(CallbackRecord)+sizeof(BatchRecord))` 与各元素上限先校验，避免乘法溢出。分配与初始化只在 Configure，失败不半发布。
- 可以用预 resize 的 vector 或 unique_ptr 数组；render thread 只按 retained index 赋值，不 push_back、grow/reserve、format、sort、file I/O、GPU query、用户函数或资源 ownership copy。
- 填满后保留前缀不覆写，并继续独立 seen/retained/dropped。callback 与 batch 各有分母；`seen=retained+dropped`，尾部容量丢样使报告显式 truncated，不能把前缀分位数称全程分布。每个 client 另有 seen/advance/attempt/commit/failure/Idle/Retry 数量以保留分母。
- 一个 fixed 数值 record 不含 COM 引用、地址、HWND、字符串、容器、user callback或业务对象。只复制现有 `FrameDiagnostics` 与 numeric metadata；FrameContext 不整体复制到报告，避免 COM AddRef。
- 报告带 schema/version、runSerial、capacity、allocatedBytes、clock period、origin、sealed/stop reason、各 overflow/invalid 计数。runSerial 为该 Scheduler 生命周期单调递增；不同实例/进程由 root 报告的外部 scenario/run ID 区分，不导出指针。

### CallbackRecord

至少含：runSerial、batchSerial、callbackSerial、client、callbackGeneration、contextEpoch/backend/featureLevel、schedulerIdleEpoch、clientActivitySegment、result、requested/continued/retried flags、frameTime/start/end ticks、前一 callback/commit 活动状态与相关前一 tick、完整 `FrameDiagnostics`。

- 用同一个 `steady_clock` 域，保留原始整数 ticks 与 clock period；离线转换 milliseconds，不把这些叫直接 QueryPerformanceCounter 的原始计数。导出须保存 int64 精度，不能经 double 先截断。
- result 与 sampled `animationAdvanced/presentAttempted/ulwAttempted/ulwSucceeded/presentCommitted/presentDeferred/backoffSkipped` 独立保存。Settings 的 Continue、合法 Retry 和 Bar 回调本身均不能冒充成功 Present。
- `FrameDiagnostics` 对无对应阶段的 client 保持未设置值；feature level来自实际 context.epoch，不从CPU架构或 Client 名称猜设备。
- 本单元成功时间固定标注 `commit-callback-end proxy`，记录点是 callback 实际返回后的第一读钟，先 raw 保留再异常格式化/投递。只有 `presentCommitted` 才建立该 commit 链，Idle 并不代表失败已恢复。未来真 Bar commit tick 单独字段/口径，不静默改代理旧字段含义。
- end<start、逆序 tick 或冲突阶段标记不 clamp 成0来美化；记录原值/invalid 标志并从有效时间分布排除，保留 invalid 分母。FrameDiagnostics中的 API 数值本身保留，不驱动调度决策。

### BatchRecord

至少含：runSerial、batchSerial、frameTime/begin/end ticks、work/requested/continued/retried/registered mask、actual executed mask、recoveryAttempted/result/start/end、device context有效标志与epoch、stop/deviceLost flags。

- 每次实际批次都保存，包括 recovery 失败后没有任何 callback 的批次。该批次不获取/复制旧失败设备 context，epoch可明确 unknown；恢复耗时和等待缺口各自可见。
- 回调之外存在真实 control task、contextProvider、dispatch 和异常 sink 成本时，不把 batch总 wall 减各stage后的余量叫纯CPU或GPU。mask/time可以解释归属，GPU duration 本单元 unavailable。

## R3：复用 Scheduler/TLS，日志行为保持

拆开两个观察布尔：`logDiagnosticsActive=bool(diagnosticsSink)`，`frameSampleActive=logDiagnosticsActive || rawRecorderActive`。

- 仅后者控制 TLS/FrameDiagnostics、callbackStart/end读钟和callbackGeneration读取；raw-only 时阶段 timer 正常填真实回调样本，无 sink 时仍不调用 accumulator 的日志格式化/EmitDiagnostics。
- 原 DiagnosticsAccumulator BeginBatch/AddClient/AddRecovery/EndBatch/MarkIdle、HealthySummariesEnabled=false、1秒限频、失败恢复与 sink 拒绝保留继续由原 logDiagnosticsActive 控制。raw 开关不安装/卸载或清空异常 sink。
- Recorder 的 RecordCallback/RecordBatch 在既有边界复制数值，不改 results 或 dispatchComplete 参数。失败判断若需汇总，提取现 Diagnostics.h 的同一纯数值 classifier供 accumulator/recorder共用，保持布尔表达式，不另造 Retry=失败的规则。
- 只有 capture/log开启才加必要读钟；两者都关时维持当前 TLS=null 和不读stage时钟，不为数值采样改变 Tick/frameTime、sleepUntil、frameInterval、Request、设备恢复或control队列。
- Recorder MarkSchedulerIdle 放在当前确实 pending=0、二次检查无新 work 且即将 wait 的既有位置，只在真正从 active 转 idle 时推进 epoch。诊断到期再次醒来但仍无 work 不反复增加 idle transition，不改变 INFINITE/diagnosticDelay 等待。
- 客户端 Idle/Stop、真正 scheduler idle、重新注册 generation、device epoch切换均切断其活动间隔；raw成功间隔可另列但不能混成active gap。只用固定 Client::Count数组，复杂 registry不必要。
- pending retry/长批次等异常完全保留；合法 Retry可没有presentAttempt，不独立计失败。实际API失败、callbackException与DeviceLost仍分别可读。

## R4：真实实现测试（不复制算法）

沿现 `render_scheduler_tests.cpp` 直接使用生产 Scheduler 和 Diagnostics.h 的生产 recorder/accumulator。新的 DTO/API需先实际实现才能运行；下表目前均 **未验证**。

| ID | 夹具与断言 |
| --- | --- |
| R01 默认关闭 | 真 Scheduler callback中TLS=null、Take无报告；不开数组；原 no-sink/restart测试保留。静态核 recorder默认nullptr，不用全局allocator替换制造无关行为。 |
| R02 raw-only | Configure后无任何sink；callback取得真实TLS、填写advance/attempt/commit/阶段，Stop/Take后值原样；callback外与另一线程TLS=null。原代码的sink-only TLS门可保留到harness red再改OR，证明原观察门不足，不强求所有新增case红。 |
| R03 overflow | 实际 recorder小capacity2、真实5次callback，seen5/retained2/dropped3、预分配地址/容量不增长；Stop不补渲染请求。batch分母独立且 report truncated，停止后才读arrays。 |
| R04 advance/attempt/commit/Retry | 真 Scheduler连续返回合法Retry、真实失败Retry、成功Continue、Idle；每个mask/result/flag分别保留，attempt/success/failure分母正确。callbackCount不冒充帧率，成功代理只对真实commit记录。 |
| R05 idle分段 | 生产 recorder可注入确定time_point测试：clientIdle而SettingsContinue、全SchedulerIdle、下一次首帧；不跨Idle活动gap但保留raw间隔。实际 Scheduler fixture保证 idle不增callback。 |
| R06 generation/epoch | 同slot注销/重注册 generation改变、contextProvider从epoch1→2，切断链；旧样本保留其旧meta。Unregister返回前所有旧callback与raw copy已完成。 |
| R07 停止/封口/重启 | callbacks停止前Take返回空，renderThread提前发布running=false但finalTasks仍hold时也返回空；放行/join/Stop后可移出一次；旧报告在scheduler析构/新run仍可读，新run不写旧数组/不串会话。Stop重复不清已seal报告。 |
| R08 sink拒绝/抛错 | 与实际已有异常sink同步启用，false/throw不丢raw、不重新分类Retry、不追加请求；保留原1秒限频/恢复/idle到期callback不增测试。慢sink耗时可影响既有批次，raw写入口不新增任意sink。 |
| R09 实例隔离 | 两个真实Scheduler分别raw-only、off或sink-only；样本只归各实例，无悬空TLS/互串资源。 |
| R10 慢Settings | 真 Scheduler，假Settings callback在受控gate里暂停后由fixture释放；检查本批Bar→Settings顺序、Settings wall与下一Bar延后因果tick，不把假客户端当真实UI性能。所有失败出口放行/join，不改产品回调。 |
| R11 recovery/Stop | 真 Scheduler DeviceLost→recovery失败→成功，no-callback recovery batch保留实际result/time；Stop不因raw导出新增等待，不让旧epoch callback执行。 |
| R12 非法/容量 | 对真实recorder确定逆序tick/非法sample标记，原值保留并记invalid；容量0按上面的状态表关闭或在Reserved/Releasing期间拒绝，超过65536/64MiB拒绝；不能溢出或影响sink。没有样本的离线分位数unknown/null，不从max造P95。 |
| R13 配置预约 ABA/容量 | 在真Scheduler的Configure锁外分配阶段用默认空指针的私有暂停hook冻结调用线程，另一线程完成Start→Stop，再放行：旧候选按epoch拒绝，下一正常Configure才可发布。暂停期间第二Configure与容量0都不得形成第二个64MiB候选；失败出口先释放候选再释放自己的预约token。测试hook不从callback调用，也不在正常产品开启。 |
| R14 已封口报告与释放状态 | Stop后旧sealed未Take，再普通Start→Stop，最终仍可Take旧报告一次；两个运行不能写同一buffer。容量0移出Prepared后在释放暂停点尝试第二Configure，必须立即拒绝且总占用不超过一次候选；放行后才可重新配置。 |

测试里的假 callback是production Scheduler/recorder合同测试，不称真 Bar GetDC/ULW 或主观流畅度。不会复制一套正确Recorder/percentile再对照；离线统计/JSON由root runner负责，若以后增加生产统计helper需独立已知数组测试。内存失败不通过全局注入干扰产品/其它worker。

root在代码冻结后串行完整 `InkeysRepo.sln Debug|ARM64`（原生 MSBuild、同 invocation PATH规范、>=5分钟）、严格Headless、Release及适用三架构；worker不占输出目录。原日志聚合/时间/pacing tests继续运行。独立 checker看实际diff、生命周期锁/状态、raw-only门、bounds与原始结果。

## B/F：需 root 另冻结的实际 Bar 与激活合同

- U04-R不修改Bar。既有Draw/GetDC/ULW/ReleaseDC/EndDraw/PresentLockWait与mask实际hit/miss/create/failure/slices可复制；未提供layout/lighting/SVG/path细段就标 absent，不因缓存存在就称已量。
- 后续U04-B在原 WakeAndSnapshot、SubmitTargetsAndLayout、AdvanceAnimationsAndDeriveLayout、PrepareLightingAndDemand、CalculateDirtyAndDrawPresent调用边界补 opt-in阶段；不得为计时移动函数或变条件。原FrameStage枚举/formatter镜像需同一owner串行同步。
- 真Bar提交time应在原CompleteAttempt确认commit之后紧邻锁存，标软件完整事务确认时间（同steady-clock域或经明确QPC换算），不能以callback end悄称API真实返回。ULW是事务中的一段，不把ULW早成功当EndDraw已成功。
- 动画完成需要 root fixture的目标/交互revision、已消费target、适用动画全部收敛与最终成功commit；不能用首次Idle或鼠标/动态主光是否活动替代，不增加全动画registry。快速反向标旧scenario cancelled，不调duration/dt。
- SVG/path独立 parse/build/raster/upload/draw要在实际缓存miss/创建/draw owner边界计；evict未有计数标 unavailable，不改key/量化/纹理分辨率/失效。非Scheduler初始化期间的资源时间不能冒称已被TLS抓到。
- root选择明确opt-in隔离程序副本/自有HWND消息的真实Bar runner。只跑现offscreen Shape/eraser fixture不能标完整主栏ULW。Configure在Initialize前，Shutdown/Stop后Take，由owner对唯一新报告路径写盘；无真实用户配置/UInk目录，无computer-use/globalSendInput或用户PID终止。
- 只在所有编译/扫描/其它基准结束后，至少三轮相同Release/设备/效果/轨迹，≥16次预热，cold/first/warm/idle分开；每population count、median/P95、长帧比和drop/invalid明确，小P99不可靠。原门槛median>噪声且>5%、P95>噪声且>10%；新增观测不是收益证据。

## 未验证范围与交付

本轮只读 actual jsonl/PRD/design/implement、父UI3任务/性能与handoff、FULL performance-sampling-postcommit-contract、相关diagnostics/render/errors/conventions/build/quality/guides，以及实际Scheduler、aggregator、headless和Bar提交/阶段源码。继承的旧native demo入口不覆盖主产品完整Solution。

当前无新构建/运行/性能/GUI/Git；只写本设计与已授权E03实施报告。后续报告记录实际更改symbols/文件、测试命令/配置/退出码/原始路径、容量和drop、真实与proxy字段、独立review与未验证。默认视觉/动画/时钟/缓存/dirty/设备/功能入口保持；Win7 SP1仅KB2670838、FLIP、DComp/ULW及UI3/Draw3设备所有权继续有效。没有Win7/真笔/GPU光学/HC/H2 PASS，任务不结束。
