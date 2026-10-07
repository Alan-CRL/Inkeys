# UI3 U04-R 原始采样实际代码独立复审

日期：2026-09-30。Active task：`.trellis/tasks/09-27-integration-and-release-check`。审查当前 `git diff HEAD` 中唯一 UI3 owner 的 `RenderPipeline.cppm/.cpp`、`RenderPipeline.Diagnostics.h`、`render_scheduler_tests.cpp`，对照 `ui3-raw-sampling-design.md` 和前置设计审查。E02 Main/ShutdownSupervisor 并行改动不在本报告范围。本 reviewer 不改源码、不构建、不运行测试。

## 结论

**本批实际代码静态 GREEN，最终冻结源码的 Debug ARM64 完整 Solution 与严格 Headless 已由 root 验证通过。** RED stub 有且仅有六个新增 Scheduler 断言失败；同一批真实实现转绿，未见旧测试回归。此单元只记录 Scheduler callback/batch 数值与结束代理，不包含 Bar 真 GetDC/ULW/EndDraw 提交时间、完整主栏 fixture、SVG/path 分段或可比 HC/H2 性能。

## 实际源码核对

- **默认关闭与既有时钟。** `Impl` 的 `rawPrepared/rawActive/sealedReport` 默认空，未配置时没有数组分配。RawMarkIdle/RawBeginBatch/RawRecordCallback/RawEndBatch 均在 null rawActive 时短路；`frameSampleActive = diagnosticActive || rawActive`，两者都关时仍不建 `FrameDiagnostics`/TLS，不加阶段读钟。原 `frameTime`、`nextDeadline = frameTime + FrameInterval`、`Request/Complete/Retry`、设备恢复和客户端结果未被更改。旧 `DiagnosticsAccumulator` 仅在原 sink 启用时运行，raw-only 不格式化/投递异常日志。
- **分类与真实样本。** Diagnostics.h 抽出原样的 `PresentFailed`/`FrameFailed` 布尔表达式；合法 `Retry` 本身不等于失败。raw callback 在生产 callback 返回后、`activeCallbacks--` 前复制实际 `FrameDiagnostics` 与客户端/代次/设备 epoch、result、时间；同一 `steady_clock` 域的整数 tick 与 period 输出，不称 QPC 或光学像素可见。成功标志沿客户端原 `presentCommitted`，callback end 仅为 `commit-callback-end proxy`。batch 在设备恢复失败无 callback 时仍保存 recovery 时间、失败结果和 contextValid=false；成功后才写真实 epoch/executed mask。一个帧的执行标志和各计数来自真实 Scheduler，不是测试复制的绘制算法。
- **固定预算和掉样。** Configure 容量限 65536 且先用除法验证 `capacity <= 64MiB/(sizeof callback+sizeof batch)`，锁外一次 `resize` 两数组。render thread 仅固定索引覆盖预分配槽；满后继续 seen/retained/dropped/invalid 与各客户端计数，不覆盖旧样本。`Take` 在 join/seal 后缩到 retained 的前缀；后续分析须看掉样分母，不能把前缀分位数叫全程尾延迟。Raw record 为纯数值、enum、内嵌诊断，不保留 COM/HWND/用户路径/文稿内容或原 callback。
- **生命周期与 ABA。** Configure 初次 try-lock 捕获单调 lifecycle epoch 与 reservation token，锁外构造；复核时同时查 running、joined、epoch、token、Reserved、无旧 sealedReport。Start 的锁前 `running.exchange(true)` 先阻并发配置；成功 Start 在同锁推进 epoch、消费 Prepared，若外部分配尚在途则下一轮不会接收旧候选。Stop 在 jthread join 后同锁封口并推进 epoch；render thread 提前置 running=false 不能让 Take 越过 joined 门。cap0 的 Prepared→Releasing 在锁中移所有权，释放完成后才 CAS Empty；Reserved/Releasing 时其它 Configure/cap0 均拒绝，避免两份 64MiB 候选重叠。失败候选先析构再把自己的 Reserved 退回 Empty。旧 sealedReport 可跨下一次**无采样**的 Start/Stop 存留，Take 一次转交；Stop 不清掉它。
- **hook、测试与 no-window。** 暂停 hook 默认 null，只由 `SetRawCaptureTestHookForTests` 的显式测试设置；读取与指针一起在 callbackMutex 内，调用在锁外。R01 现在等到一次真实 callback 再判断 TLS 为空；R02/03/04 用真 callback、阶段 timer 和五次调用验证 raw-only/overflow/数值保留。R07 finalTasks gate 验证 running=false 早于 join 时 Take 不可见；R13/R14 暂停分配/释放、尝试竞争，等待超时仍先 Resume+join 后断言；R05/06/08–11 覆盖 idle、重注册/epoch、异常 sink/实例隔离、慢 Settings、无 callback 恢复；R12 调生产 time validator 检逆时值。测试已由现有 `RunRenderSchedulerTests` 路由到 `--no-window`，未新增 HWND/全局输入。Module DTO/API 与实现签名对应，未见 ODR 双定义。
- **文件格式。** 本批源码保持原无 BOM/CRLF；scoped `git diff --check HEAD` 退出 0。未做全文件格式化。

## 不阻断本批的边界

- `RawCaptureReport` 目前用 `sealed`、seen/retained/dropped 与每项 result 表达封口/截断；设计提到的独立 stop reason、显式 truncated、长帧/分位数、完整 Bar commit tick 仍待后续离线 runner/Bar 单元。不能只凭 `sealed=true` 给录制场景完整性 PASS；入口调用者须核 Start 成功、容量无掉样及预期场景结束。
- `Scheduler::Start/Stop` 仍沿既有单 owner 生命周期；Configure/Take 可与停止请求并行失败，但本单元不证明可并发析构 Scheduler 或从自身 callback 调 Stop。异常创建 thread 时 `Start` catch 调 Stop 封口空样本，调用者应把失败 Start 标诊断 unavailable，不输出正常 run 结果。
- 固定容量报告仍可能改变 opt-in 测量的 CPU/内存成本；收益/噪声需要 Release 同轨迹对照。当前测试的假 Settings 与 synthetic present flag 验证计量合同，不构成真实主栏体验、Win7 或用户设备性能通过。

## Verification

本 reviewer 只读核对 scoped diff、生产 Scheduler 调度/诊断/生命周期调用链、测试登记与 no-window 路由、编码/换行和 scoped diff-check。UI3 四个最终文件 SHA-256：module `0dd6373ca6c6d8569f9c2240a5a043043043f59bcd78391728c1475faf93f470`；cpp `d9ecfc48ae3c0d26339da94db2d5c77b8b2bb2a589bcfb081911fb53b2807d57`；Diagnostics.h `be0fa42465a0159d4384506d27a5a957288bca94d4be7f06c724a2eba190893b`；测试 `6d00ed01acac7d2e579b6a50408f23d80caa90c91c151af907683145647ddfd7`。

root 的最终 `TestResults/release-hardening/e02-ui3-final-source-debug-arm64-build.log` 明确 `InkeysRepo.sln`、Build succeeded、0 Error/4 Warning；主会话另核构建进程退出码 0。`e04-ui3-raw-red-debug-arm64-headless.log` 可直接读到 R02/R03/R04/R13/R12/R14 六项失败，旧布局/动画 `216 layouts failures=0`；主会话记录 RED 进程自然 exit1。`e04-ui3-raw-green-debug-arm64-headless.log` 不再有 `[RenderScheduler] failed`，仍以 `216 layouts failures=0` 和 `PASS animation correctness` 收尾；主会话记录严格 `--no-window` 进程自然 exit0。测试为生产 Scheduler 的合同证据，并非真实 Bar ULW 帧或设备上的性能分位数。本 reviewer 没有独立运行 Build/Tests；Lint/TypeCheck 工具不适用于此 C++ Solution，MSBuild 编译是实际类型/链接门。
