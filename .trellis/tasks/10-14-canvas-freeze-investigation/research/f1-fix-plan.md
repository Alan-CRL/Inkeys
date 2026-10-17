# Research: F1 标准流背压修复实施前设计

- Query: 为已证实的 Draw3 同步标准流阻塞提出可实施、可验证的最小修复；覆盖旁路、输出兼容和退出取消竞态。
- Scope: mixed；生产源码只读，Microsoft Win32 官方文档核对。仅规划，不实施、不构建、不启动 GUI。
- Date: 2026-10-15
- Task: `.trellis/tasks/10-14-canvas-freeze-investigation`；本研究只拥有此文件，不修改其他工作者的产品代码或任务主文档。

## Findings

### 结论先行

建议新增一个 **Draw3 专用、固定容量、允许丢弃的标准流输出器**，所有生产可达的 Draw3 / RTS `stdout`、`stderr` 诊断只尝试入队；单独的输出线程负责 console / pipe / file 写入。保留现有诊断开关、主要前缀、关键字段及目的流，不接管全进程 CRT，不建立通用日志框架，不把诊断混入 UInk 持久化。

退出必须同时具备：停止接纳、唤醒、反复取消专用线程的同步 I/O、有限总等待、超时后的独立状态隔离。**不能使用没有截止时间的 `jthread` 析构 / `join`；不能认为一次 `CancelSynchronousIo` 成功就代表线程已结束。** 正常停止回收资源；极端取消不完成时，保留最多一份独立输出上下文到进程结束，既不杀线程，也不释放仍在 I/O 中的缓冲。这是诊断隔离兜底，不改变 Host / RTS / 持久化线程的既有停止合同。

本轮 F1 范围经主会话明确收窄为 **标准流背压**：包含同路径的 Cursor、帧计时、TouchArea、RTS、窗口 / 设备恢复、保存错误等旁路；**不改 `INKEYS_DRAW3_TAIL_TRACE` 专用轨迹文件协议**，不承诺任意 Draw3 磁盘诊断已异步。该默认关闭的独立同步 I/O 记录在下文，不阻塞本方案。

### 1. 已知证据及可复用设施

- 既有 [expanded-investigation.md](expanded-investigation.md) 的 F1 在未经修改的完整 HEAD GUI 两次复现：Cursor 开启，stdout 管道读端存活但停读；画布线程停在 `WriteFile`，Settings 可交互，读端恢复后同进程恢复绘制。这里没有新增运行证据，也不能推出原用户事故均由 F1 导致。
- `Draw3.Diagnostics.cpp:48–55` 的 `WriteFastConsoleLine` 在调用线程先 `WriteConsoleA`、再同步 `WriteFile`；`:574–617` 的 Cursor flush 虽然锁外输出，仍由 `DrawingController.cpp:12607,14454,14592` 调用，因此有界队列只限制内存，没有隔离输出等待。
- `Diagnostics.cpp:655–671` 的 Debug 帧计时独立走同一同步 sink；只修改 Cursor flush 不足以关闭该旁路。`:146–150` 的 RTS trace 用 `cout.write/flush`，`:681–705` 的 CanvasPan 还直接镜像 `OutputDebugStringA`。
- `IdtMain.cpp:295–325` 已明确支持继承 stdout / stderr 普通文件及管道；两个流都重定向时不分配控制台。不能用关闭重定向、强制打开 `CONOUT$` 或改变启动器句柄来规避。
- `IdtMain.cpp:2337–2360` 中主 `IDTLogger` 的异步队列策略是 `block`；`UI3Diagnostics` 是 `discard_new`，但与它共用同一文件 sink、同一 `spdlog::thread_pool()`（8192 / 64）。这是可借鉴的丢弃语义，不是本问题可直接复用的标准流输出器。复用会改变输出目的地，且未自动解决线程池析构 / sink 阻塞的退出问题。`RuntimeMetrics.cpp` 是内存度量与报告，不存在可直接承担本任务的有界标准流 worker。
- `IdtMain.cpp:339–350` 已有独立退出监督；spec 规定首次关闭意图先武装 15 秒绝对期限。该监督保留为进程最终保护，不能把它作为正常日志停止的实现或测试成功依据。

### 2. 最小接口与队列合同

建议新增 `Draw3.DiagnosticOutput.h/.cpp`，使用普通 TU 的窄 API，使已有模块实现单元、普通 `Host.cpp` 和被多个非模块 TU 引用的 `PptTiming.h` 都可调用；不要在公共头直接导入模块或全局替换 `std::cout.rdbuf()`。`Inkeys.vcxproj` / `.filters` 只增加这两个源文件的清单项，工具链、SDK、依赖、编译宏不改。

接口只需表达四件事：初始化当前标准输出端、尝试提交定长文本、读取有界统计、按截止时间停止。记录保存 `stdout / stderr` 目标及可选既有 debugger 镜像标记；保留字节长度，不依赖调用方文本生命周期。接口名在实施时可按仓库习惯确定，不额外设计 levels、logger registry、插件 sink、动态通道配置。

建议固定上限：**1024 条 × 每条最多 4096 字节文本**，另有固定元数据和一个 worker 私有输出缓冲，常驻 payload 上界约 4 MiB。4096 对齐现有最长普通记录（RTS contact summary 缓冲 4096、TouchArea 3072）；Cursor 原 320 字节消息与 seq / tick / producer TID 格式不变。该数值是规划参数，不是性能实测结果。

具体要求：

1. 初始化阶段一次分配；生产者不扩容、不分配每条日志、不等待 I/O。先做原有开关检查，再格式化到有界缓冲，尝试提交。队满、门闩竞争、输出器关闭、初始化失败都丢弃诊断并返回；不得回退到同步 `fprintf`、`cout` 或 `OutputDebugString`。
2. 队列互斥采用 **try-only**；不能把原 Cursor `lock_guard` 直接搬成输出层的阻塞生产者。可用 Win7 支持的 `TryAcquireSRWLockExclusive`；锁内仅检查 / 复制 / 移动索引，所有外部输出在锁外。worker 只取一个固定记录到自己缓冲再释放锁，不持锁等待设备。用 auto-reset event，成功发布后 SetEvent；worker 每次先检查 / drain 队列，再等待事件，不使用“解锁后 ResetEvent”造成丢唤醒。
3. 入口的 `GetLastError()` 要保存 / 恢复，迁移前就捕获故障 HRESULT / Win32 error，避免诊断调用污染后续错误判断。保持现有原始错误字段，不拿输出器的错误覆盖业务错误。
4. 记录按成功入队顺序输出。拥塞时允许丢弃新记录；`droppedFull`、`droppedContention`、`droppedClosing`、`truncated`、`sinkError` 保持原子有界统计，饱和或明确溢出语义。恢复读取后可输出一次聚合丢弃说明，不能每丢一条再制造一条日志。Cursor 自己的 sequence 仍是生产者序列；丢弃意味着可见 gap，不声称连续。
5. `RecordCursorDiagnostic` 直接生成 / 提交原 Cursor 记录，去掉“必须由绘制线程 flush 才能输出”的依赖。可以保留 `FlushCursorDiagnostics` 名称作为无等待的唤醒 / 有界转交兼容入口；不可保留任何 sink I/O。优先合并为一个队列，避免不必要的二级队列与多份 dropped 解释。
6. `std::cout << ...` 的多片输出改成一条有界记录，保留前缀、关键数值 / 十六进制含义；不能通过先写全局 ostream 再转发来规避显式迁移。超长字符串有截断标记；不随手修改业务控制流。已有大型结构查询及限频条件不扩展。
7. 这不是硬实时调度保证：有界复制与 try-lock 消除的是“诊断生产者等待输出端 / 队列互斥”的路径，不承诺 Windows 调度或任意格式化成本恒定。测试应区分 CPU 用时与 OS 调度暂停。

### 3. 初始化、默认关闭和标准句柄兼容

生产生命周期放在 `wWinMain` 的局部 RAII owner 中，覆盖正常返回和启动失败返回；在 `IdtMain.cpp:2635` 完成可选 `InitializeDebugConsole()` 之后、`:2637–2639` Win7 诊断及 Cursor 启用之前真正启动输出器。此前仅建立空 owner。它是进程级 Draw3 输出资源，不在每次 Host 的 DComp / ULW 回退或设备重建时重复创建。

- 所有相关开关关闭、没有可写继承标准句柄、也没有需要保留的 debugger 镜像时，不分配大队列、不创建 event / worker、不打开控制台；关闭入口应在格式化前快速返回。仍有有效继承 stdout / stderr 时，即使 Cursor 开关关闭，也要允许现有普通错误 / 初始化日志异步输出；不能把“Cursor off”当作所有诊断 off。
- 对可用的 stdout / stderr 分别 `DuplicateHandle` 到本进程，关闭继承标志；worker 只使用自己拥有的副本。保持原流目的地，不关闭 / 改写启动器原句柄。失败的一路禁用，另一路仍可使用；不通过同步错误日志报告失败。
- 正常 console 用与当前实现一致的窄字节控制台输出语义；可在 worker 中通过 `GetConsoleMode` 确认 console，再用 `WriteConsoleA`。普通文件 / pipe / 非 console 字符设备用同步 `WriteFile`，但只在专用 worker 上执行。不要借 F1 修改 console code page、换成 UTF-16 重定向文件、合并 stderr 到 stdout。
- 部分写入继续处理剩余字节，每次前检查 stop；成功但写入 0、无效句柄、broken pipe、operation aborted 要退出当前记录，不能零进度自旋或立即无限重试。关闭坏端只影响诊断；禁用后仍保留其它流的接纳 / 统计语义。
- 继承的匿名管道不能靠“给同步句柄补一个 OVERLAPPED 参数”变成可靠可取消异步 pipe；不要求启动器改用命名管道。单 worker 某一路停读会阻塞该 worker 的其它诊断，这是允许的诊断损失边界，画布 / RTS / 持久化完成通知仍前进。
- 同一记录原有的 debugger 镜像与标准流写入一起放到 worker；镜像不得留在绘制 / RTS / Host 回调中。没有标准流的独立 debugger-only 诊断不自动纳入本次范围，见下表边界。迟附加 debugger / 运行中替换标准句柄并非既有 `WriteFastConsoleLine`（缓存句柄）所保证的能力，本次不扩展。

Microsoft 的 [WriteConsole](https://learn.microsoft.com/en-us/windows/console/writeconsole) 区分 console 与重定向文件；[匿名管道](https://learn.microsoft.com/en-us/windows/win32/ipc/anonymous-pipe-operations) 的同步满缓冲写会等待且不支持普通 overlapped 用法。[WriteFile](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefile) 要求 I/O 完成前缓冲始终有效，Win7 仍传有效的 `lpNumberOfBytesWritten`。

### 4. 可靠停止：顺序、竞态、所有权

建议 `Stop` 总预算先定为 **1000 ms**，其中至多 **100 ms** 尝试正常 drain，之后进入取消。所有等待共用首次停止计算出的绝对 deadline，不能每阶段各加 1000 ms。实测后只能基于证据调整；不能声称目前已满足该耗时。

正常入口在 Host / RTS 及现有持久化生产者清理之后停止输出器。所有原标准流旁路已迁移，所以这些生产者不会再因诊断输出而卡住自身 `Stop` / `CloseAndDrain`。已有 window / draw / persistence 停止次序不因日志改变；最终 RAII owner 兜住提前返回。晚到的记录只被丢弃。

可直接实施的状态与生命周期合同：

1. 使用一个专用 native thread handle（例如 `_beginthreadex`），不要借用线程池线程；句柄保留到停止处理结束。它提供 `SYNCHRONIZE` / `THREAD_TERMINATE` 权限供 wait / `CancelSynchronousIo` 使用。这里的权限名不代表调用 `TerminateThread`。
2. 在进程常驻的极小路由门闩下发布上下文指针。生产者只 `TryAcquireSRWLockShared`，读取 active 指针，确认接纳，再 try-lock 队列并复制；在完成 `SetEvent` 前保持路由读保护。这样停止取得路由独占后可证明没有生产者还在使用 event / queue。不能仅 atomic-load raw pointer 后无保护地递增引用计数，这存在释放竞态。
3. 停止先发布 `accepting=false`；在同一截止期内尝试取得路由独占、解除发布并唤醒 worker。正常 drain 阶段仅处理已接受记录；到取消阶段发布 `cancelRequested=true`，放弃剩余队列。worker 在每个新 `WriteConsole / WriteFile / debugger mirror` 之前和之后检查该标志，不继续写下一条。
4. 在剩余期限内循环：对专用线程 `CancelSynchronousIo`，再有限 `WaitForSingleObject`（例如最多 10 ms，且受剩余预算限制）；如果线程已 signaled 则立即退出。记录 API 的结果，但 **ERROR_NOT_FOUND 不是成功停止**。
5. 原因：worker 可以先读到 `cancel=false`，停止线程随即取消得到 `ERROR_NOT_FOUND`，worker 再提交 `WriteFile` 并阻塞。单次取消会漏掉这个窗口；反复取消配合 worker 检查才能处理正常支持取消的 I/O。`CancelSynchronousIo` 返回 true 也仅表示请求取消，不代表 I/O 或线程已经完成。不得在 true 后立即释放缓冲 / 关 writer。
6. 正常确认 worker signaled，且路由已解除发布 / 所有生产者退出后，才释放上下文、队列、event 和线程句柄；stdout / stderr 副本由 worker 在最后一次 I/O 返回后关闭，避免主线程在未结束 I/O 上强行关句柄。不要在 worker 中使用全局 `cout`、FILE 锁或 `IDTLogger`。
7. 截止期耗尽，或路由独占无法在期限内取得：返回明确的 `IsolatedPending` 结果，将接纳保持关闭，保留整份独立上下文与仍需存活的句柄 / 缓冲至进程结束；同进程不再新建第二个输出器。即使 worker 稍后返回，也只能访问自己的 stop 状态、固定队列 / buffer 和自有句柄；不得调用 Host、窗口、COM、上层日志、引用已析构对象或在全局析构中重新 join。若仍有路由读者，整个状态保留，不做部分释放。有限的一份状态是输出线程隔离兜底，不是泄漏随重试增长。
8. 不调用 `TerminateThread`，不以日志错误终止整个产品；不把异常输出 worker 放入普通 `jthread` 容器，让其析构再无限等待。已有 15 秒关闭监督是原系统最终保护，F1 正常成功必须在它触发前完成。

该设计不声称“所有设备驱动都能被取消”。Microsoft [CancelSynchronousIo](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelsynchronousio) 定义请求取消、ERROR_NOT_FOUND 与完成状态的区别，最低客户端 Vista；[取消挂起 I/O](https://learn.microsoft.com/en-us/windows/win32/fileio/canceling-pending-i-o-operations) 明确指出 I/O 间隙需要额外取消检查、底层驱动并不总能完成取消。隔离上下文使这一系统边界不再变成画布或关闭线程的无限等待。

### 5. 必须迁移的生产标准流清单

路径以下均相对于 `Inkeys/Inkeys/Drawing/Draw3/`；行号为本轮只读快照。不是把所有 `WriteFile` 搜索结果机械替换。实施后应再次按“生产路径 / 测试输出 / 持久化 I/O”审查剩余命中。

| 文件与入口证据 | 必须迁移的输出及原因 |
| --- | --- |
| `Draw3.Diagnostics.cpp:48,146,574,655,681,711–841` | Cursor flush、RTS 初始化 / 停止 trace、FrameTiming、CanvasPan（含 mirror）、HRESULT / Win32 / DWM / swapchain / adapter，均可同步 stdout。更新 `.cppm:88` 的旧“由绘制线程统一输出”注释与 flush 合同。 |
| `Draw3.DrawingController.cpp:377–1589,7698–14592` | 初始化 / 快照 / load gate / Undo / Redo / page / reconnect / GPU recovery / Cursor sources / 正常和异常退出的 cout、fprintf、fputs；这些错误路径仍能走到唯一画布消费者。中间内嵌 `Run*Probe/Test` 的测试结果输出单独识别。 |
| `Draw3.Host.cpp:111,129,666,895–1202,1364,1477–1697,1864–1868` | Build/window/present、Display / EDID、TouchArea / EraserEntry / EraserInput / Follow / FineBand / Curve、启动 / worker / 退出错误。`:1021,1036,1044,1086–1089,1202` 的 stdout/stderr 配对 debugger mirror 必须一起迁移。不能只改 TouchArea 总结行。 |
| `Draw3.RealtimeStylus.cpp:257,2490,2628,2752–2804` | RTS 初始化 / MultiTouch / reconnect simulation、TouchAreaDevice metadata；`Host.cpp:973` 从实际绘制诊断入口调用 `TraceTouchAreaDiagnostics`。RTS callback 的固定 trace 记录继续只复制；其停止 flush 经 Diagnostics 新输出器。 |
| `Draw3.WindowControl.cpp:577,1070,1097,1224` | cursor appearance / cursor system trace；目前若干输出位于自己的 cursor trace mutex 内，迁移后锁内只有有界记录提交，不能再持锁碰外部 sink。 |
| `Draw3.PenCursor.cpp:116–118` | app cursor trace 的 `cout.write/flush`，目前在 `drawingCursorTraceMutex` 内。 |
| `Draw3.GraphicsInitialization.cpp:57,104–126` | device 创建及 Hardware → WARP 回退，亦会被运行期恢复调用；不能只认为启动日志无关。 |
| `Draw3.TransparentPresentation.cpp:283–486,859–1020,1115,1141,1155,1286,1308` | presenter / swapchain / 回退 / resize / target / present 错误；包含设备恢复。`:1363` 之后 `RunUlwDirtyCopyBenchmark` 结果不混入产品迁移。 |
| `Draw3.Renderer.cpp:87–88`、`Draw3.RendererPrimitives.cpp:321` | shader 选择 stdout + mirror、cursor GPU diagnostic cout；后者不经过 Cursor ring。 |
| `Draw3.HapticFeedback.cpp:341,409–584` | 实际输入 / render 活动可调用的初始化、附着、continuous / discrete、Stop 和 unavailable 日志；包括当前 compile-time debug off 的分支，避免开诊断后重新引入同步 sink。 |
| `Draw3.AutoSave.cpp:510,805,1176,1203` | 保存 worker 的 test-I/O 诊断、失败、结果输出；`:1203` 在 `PushCompletion` 前，可由 stdout 背压挡住业务完成通知。迁移文字诊断，保留真实保存写文件。 |
| `Draw3.PresentationAutoSave.cpp:626,748,931,1152,1710,1773–1788` | 回收 / track / 保存 / load 结果、异常与测试开关下的 I/O 诊断；`:1773–1782` 输出位于 `PushCompletion` 前。迁移对 F2 状态活性尤其重要，但不改其业务分类。 |
| `Draw3.Product.cpp:211,215` | Drawpad WndProc 的异常兜底 stderr，不能让处理异常反而等待外部 sink。 |
| `Draw3.PptTiming.h:31–32` | 环境开关下的 stderr + debugger mirror，被 Controller / Host / `IdtPlug-in.cpp` / PageControl 共用；改这个窄 helper 即可，不重构 PPT 同步。 |
| `Inkeys/IdtMain.cpp:2637–2639` | Win7 专用启用提示在输出器启动后提交；主文件另加生产生命周期和早期无窗口测试分发。其它非 Draw3 业务日志不借本任务迁移。 |

额外审阅边界：

- `DRAW3_RTS_DIAGNOSTICS` 当前不在项目默认定义中；需静态确保启用时的源码也走新 sink，并在验证可行时用既有 harness 构建覆盖。不能把默认关闭当作可留下同步旁路的理由。
- `Draw3.FailedCleanupFixture.cpp`、`Draw3.HiddenWindowTest.cpp`、`Draw3.HistoryProbe.cpp`、`Draw3.PerformanceProbe.cpp` 和 Controller / Geometry 的 `Run*` 测试报告 stdout / stderr 不是普通产品画布入口，保留测试专用输出。测试父进程必须持续排空这些输出，不能让测试结果通道本身反向阻塞。
- `Draw3.AutoSave.cpp:349` 等真正的 UInk / 索引 / durable 文件写入不是日志，**绝不送入允许丢弃的队列**。`RuntimeMetrics` 的持久化报告不改。
- `StrokeGeometry.cpp:54–216,950` 的 `INKEYS_DRAW3_TAIL_TRACE`：默认关闭，独立文件，最多 128 笔 / 2 MiB；其 CreateFile / WriteFile / CloseHandle 与 ODS 都保留。这是 **deferred 的已知独立同步诊断 I/O**，目前没有同等完整产品故障实测；F1 标准流验收不能覆盖它，亦不能宣称该分支已修复。
- `InkHistoryGpu.cpp:162`、`RendererSnapshot.cpp:29`、`Renderer.cpp:96` 这类独立 debugger-only 提示，不属于已证 stdout/stderr 背压；不借本任务扩大迁移。带标准流的原配对 mirror 已在表中列为必须迁移。

### 6. 测试真实生产代码的入口与验收

利用现有 `IdtMain.cpp:1197–1366` 的启动前自检模式增加单一 `--draw3-diagnostic-output-test` 分发，严格 argv 校验，先于配置、单实例和任何 HWND。测试调用 **实际 `Draw3.DiagnosticOutput.cpp`** 的生产队列、writer、Stop；不复制实现成独立模型。测试函数可在同 TU 的 `INKEYS_ENABLE_UI3_PRESENTATION_HARNESS` 区域内以最小接缝访问真实内部状态；Release harness-off 该参数明确拒绝，不落入 GUI。无需新构建系统。

| 验收 | 真实行为要求 / 证据边界 |
| --- | --- |
| 无诊断 / 无输出端 | 初始化为空，线程 / event / 大缓冲均未创建；重复日志调用返回，无窗口 / 控制台、无新后台工作。 |
| 普通文件、正常 pipe | 真实 OS 句柄，stdout / stderr 目的地保持，前缀字段 / 字节有效；正常停止尽量 drain，未达预算时不丢。普通文件定位 / 追加保持 inherited handle 语义；不得重新 CREATE_ALWAYS。 |
| stdout 停读与 stderr 停读 | 各自使用真实匿名管道，读端保持打开，先确认 worker 已实际进入未完成写 / 缓冲已积压，再持续多生产者提交超过容量；队列 / 内存有界，生产者返回且 dropped 增长。不要用只暂停 worker 或 stub 写入代替背压主验收。 |
| 读端恢复 / 断开 | 继续读后 worker 前进、新记录可见且有 drop 统计；关闭读端得到真实 broken pipe，不自旋、不影响另一端或业务。不可只验证 WriteFile 被调用。 |
| 取消提交竞态 | 在**真实 worker**“最后 stop 检查后、真实 WriteFile 前”置仅 harness 的事件屏障；让第一轮 Cancel 返回 ERROR_NOT_FOUND，再放行真实 WriteFile 到仍满管道。反复取消必须令实际线程完成；该屏障只验证取消竞态，不用于声称原产品自然必现。 |
| Stop 上限与隔离 | 真实停读 pipe 正常取消后 worker signaled，资源回收，Stop 用时在总预算 + 明确调度容差内；另用仅 harness 的不可取消等待接缝验证 deadline 分支和上下文保留，Stop 返回后再放行验证无 UAF。后者是生命周期故障注入，不是产品自然复现。 |
| 生产者 vs Stop 竞争 | 多生产者同时提交 / 格式化 / 通知 event，Stop 解除路由；无 event use-after-close、上下文 UAF、无限锁等待。超时保留分支不得启动第二 worker。重复 Stop 幂等。 |
| Cursor / TouchArea / Debug / RTS / 错误旁路 | 对实际 helper 分别触发并收集目标流，确认不再触碰生产者线程的 sink；代码扫描结合动态 worker TID 观察，不能仅凭某一 helper 通过就覆盖所有路径。 |
| 初始化失败 | 句柄复制、内存、event、thread 创建失败有确定关闭状态；实际产品继续运行，未启动的 thread 不 join，不同步补打一条错误。 |
| Console | 后续获准执行阶段实测实际 console 正常中文 / 数字输出、关闭与可取消边界；QuickEdit 暂停需另有现场证据，不以 pipe 测试冒充 QuickEdit 已修复验证。 |
| 完整产品回归 | 复用已有隔离配置的 `pipe_backpressure_probe.py` 条件：相同 HEAD-derived 产品开启 Cursor，pause stdout 保持 reader 活着并确认满缓冲；实际新笔、Clear、设置交互仍前进；停读时关闭 / 重启不靠 15 秒强杀才通过。再覆盖 TouchArea / stderr 及诊断关闭。运行身份、配置、输入路径和截图 / 线程证据明确记录。 |

仅 harness 自检通过证明生产输出器的行为，不等于完整产品已验证；完成后的 GUI 回归必须真正看到新内容 / Clear 成效。当前环境的完整产品合成 Touch 尚无阳性出墨基线，因此 F1 GUI 可先用已有阳性 Mouse 路径，触摸不能用“没有复现”算通过。触摸后续需要先建立完整产品输入基线，不能把简化 RTS 模型当替代。

### 7. 工具链、Win7 与预计变更文件

相关 spec：`.trellis/spec/native-desktop/errors-logging-and-resources.md`（日志 / owned worker / 退出监督），`build-and-compatibility.md`（v143、C++20、兼容与 harness），`draw3-integration.md`（产品/测试边界、输入与渲染合同）。本方案没有重写日志系统，也没有改持久化 / 输入线程的所有权。

当前工程 `Inkeys.vcxproj:37,43–78,190` 为 Windows SDK 10.0.26100.0、v143、C++20；`:84–87` 和各配置已有 `INKEYS_ENABLE_UI3_PRESENTATION_HARNESS`。使用 Win7 现有 API（SRW try-lock、CreateEvent、DuplicateHandle、WaitForSingleObject、GetTickCount64、CancelSynchronousIo），不引入 WaitOnAddress、CancelIoEx-only overlapped 假设、Win8+ 新导入或新依赖。Console API 是否每次能取消仍按上文隔离处理，不作超出官方合同的保证。

后续实施的最小预期改动：

- 新增 `Draw3.DiagnosticOutput.h/.cpp`；项目 / filters 只登记二文件。
- 修改 `Draw3.Diagnostics.cpp/.cppm` 和 `IdtMain.cpp` 的窄接入 / 生命周期 / 测试入口。
- 显式迁移上表中的 `DrawingController.cpp`、`Host.cpp`、`RealtimeStylus.cpp`、`WindowControl.cpp`、`PenCursor.cpp`、`GraphicsInitialization.cpp`、`TransparentPresentation.cpp`、`Renderer.cpp`、`RendererPrimitives.cpp`、`HapticFeedback.cpp`、`AutoSave.cpp`、`PresentationAutoSave.cpp`、`Product.cpp`、`PptTiming.h` 的日志调用；不格式化其它代码，保持原编码和换行。
- 不改 `StrokeGeometry.cpp` 的专用尾轨迹文件、UInk 保存协议、所有者窗口、日志文件框架或第三方源码。若新研究发现另一个生产标准流旁路，只迁移对应调用并更新清单，不追加无关重构。

实施后按用户当前 AGENTS 使用本机原生 ARM64 MSBuild、`InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64`，至少 5 分钟；同一 PowerShell invocation 先规范化 `PATH` / `Path` 并禁用 node reuse。优先定位 Visual Studio 安装，不硬编码版本。当前 spec 对 PptCOM 依赖描述与用户会话规则不同，但二者均不支持本任务偷换成默认只构建 vcxproj；仍执行用户要求的 solution。Win7 导入审查与实际 Win7 运行证据分开记录，ARM64 本机通过不代表 Win7 运行通过。本轮没有执行编译或测试。

## Caveats / Not Found

- F1 缺陷已证实；本文件只提出修复计划，不能写成修复已完成。原用户未开控制台的反馈仍不能单凭 F1 解释。
- 不存在“任意同步输出端均可在 1 秒内被取消”的官方保证。可靠停止的保证来自有限等待 + 独立上下文隔离；正常取消成功仍需后续真实 pipe / console 实测。1000 / 100 ms 是建议预算，不是测量结论。
- 满队列、门闩竞争、异常输出端、退出超时都可能丢诊断；这是明确的取舍，不能对诊断承诺 durable / 全量。真实墨迹、保存结果和业务命令不使用此队列。
- 没发现可无需调整生命周期、直接复用并同时满足标准流目的地和可靠停止的已有非阻塞 logger。现有 UI3 discard-new 可借鉴，但共享 spdlog pool 不等于本方案。
- 专用尾轨迹文件及独立 debugger-only 路径已明确 deferred；其他模块的全进程 stdout / stderr 也未在 F1 中全部迁移。最终表述应为“消除已审计 Draw3 / RTS 生产标准流诊断对画布 / 完成通知的背压”，不能扩大成“所有日志 / 磁盘 I/O 永不阻塞”。
- 未决实现细节仅剩预算 / 容量的测量校准及原日志内容兼容测试；没有等待用户决策的架构分叉。全局 owner 的准确声明位置与 early-return 清理在实施时按实际 diff 复核，必须满足上文退出和生命周期验收。
