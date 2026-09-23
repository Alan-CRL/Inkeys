# S4 调度与日志诊断实施记录

日期：2026-09-23。产品修改已接到真实 `Scheduler`，完整 solution 编译与 headless 执行由主代理统一进行。

## 文件与所有权

- `RenderPipeline.cppm`：每 callback 的 `FrameDiagnostics` / `LightingDiagnostics` POD、线程本地访问器、可空 `FrameStageTimer`、返回 bool 的 `DiagnosticsSink`；保留现有调度返回语义。
- `RenderPipeline.Diagnostics.h`：仅内部聚合策略，可使用显式 monotonic time point 直接验证生产限频与事件留存边界，不依赖 logger / HWND。
- `RenderPipeline.cpp`：每 Scheduler 独立的累计、callback TLS、每客户端计时与结果/请求来源、活动 batch 周期、晚注册 sink 控制点、锁外格式化/投递。
- `IdtMain.cpp`：日志就绪之后注册专用非阻塞异步桥，共用原 file sink 和 thread pool，不创建新线程，不改变原 logger overflow / pattern。
- `render_scheduler_tests.cpp`：确定时钟边界和真实 Scheduler fake callbacks 两类验证。

## 输出合同

正常 callback 仅在 sink 已安装时构造 POD、记 monotonic 时间和数值累计；无 sink 的独立 Scheduler 返回空 TLS。未启用健康汇总时，只在长 callback / 活动 batch 周期（50 ms）、真实 API/资源/回调错误、失败恢复 / 退避清除时申请输出。母版创建失败与 exact 创建失败单列 `lightFailureFrames`，不能因正常 present 而宣称缓存恢复。

所有输出尝试（含 sink 返回 false 或抛异常）至少相隔 1 秒。限频期间保留错误快照与计数，后续健康帧不覆盖 `lastFailure`。异常后转 idle 时，以诊断剩余期限代替无限等待；超时只投递聚合，不请求或调用客户端。健康且没有 pending 时保留无限 idle wait。

每条 `[UI3Diag]` 记录：batch 总计/max、活动周期max、requested/continued/retry masks、共享设备恢复次数/时间、各 client 调用与结果分布、回调时间/活动间隔、raw与active成功提交间隔、动画推进/尝试/提交/合法延期/退避次数、原始/实际推进dt、6个阶段total/max、光影命中/缺失/创建/失败/耗时/整图回退原因/实际mask调用数、latest/slowest/lastFailure资源与返回值快照。`previousFormatMs` 与 `previousSinkMs` 是前一次诊断自身的耗时，避免与绘制阶段混淆。

`Retry` 不单独触发错误，合法交接只计 `deferred` / result 分布。真正 scheduler idle、某 client 上一返回 Idle、同一 client 槽重新注册会断开活动间隔链；raw成功提交间隔仅用于观察，不独立触发长帧。

## 异步桥与失败边界

`DiagnosticsSink` 返回 true 表示已接收，false或异常表示聚合继续保留。IdtMain桥使用 `async_overflow_policy::discard_new`，前后比较共享 pool 的 `discard_counter()` 检测投递丢弃；静态搜索未发现项目内其他 discard_new 生产者。它避免沿用主logger满队列阻塞策略。未来若新增其他 discard_new 生产者，计数比较可能保守地重投同一聚合（重复而非丢失）。写磁盘/flush由原日志池执行，sink接收不等价于持久化保证。

Stop在drain渲染线程后释放sink。按照主代理确认的退出边界，关闭只尝试已经到期记录，不等待满1秒、不突破限频；若进程恰在最后一秒内关闭，尾部未到期聚合可能未输出。普通运行/idle的pending不受此边界影响。

## 验证状态

已执行 `git diff --check`；目标文件保持CRLF，IdtMain原UTF-8 BOM保留，其余原无BOM文件保持无BOM。完整编译/测试尚待主代理统一执行，不能把静态通过作为运行通过。

新增测试覆盖健康静默、精确1秒限频、fast Bar/slow Settings归因、晚到失败/exception与恢复留存、被拒sink不每帧重试、idle与client Idle排除、注册代次断链、合法Retry不误报、光影创建失败不冒充present失败、运行中注册sink、锁外sink重入PostControl、idle限期投递不增加callback、sink异常不改变callback结果和独立Scheduler实例隔离。
