# F1 实施交接

日期：2026-10-15。用户已批准实施 F1–F4；本子任务负责 F1。产品基线 HEAD 仍为 5d319bbf5c520dec97dcb37ad2fcc7e26d029bbb，未创建 commit/push。

## 已实施

- 新增普通头/实现 `Draw3.DiagnosticOutput.h/.cpp`。1024 × 4096 字节定长文本记录、单个独立 native worker；生产者只有 try-only 路由和队列锁，满/争用/关闭允许丢诊断，没有同步 sink fallback。
- 保留 stdout/stderr 目的流，启动时 DuplicateHandle；worker 在队列锁外处理 console/pipe/file 和原配对 debugger mirror。部分写继续，0进度/断管/坏句柄禁用该端，其他端可继续。统计含 accepted/written/full/contention/closing/truncated/sinkErrors/writeCalls/cancelAttempts/cancelNotFound/queuePeak/workerTid；64 位计数明确采用模数语义。
- Cursor 删除原二级 mutex/ring/绘制线程 sink。保留 seq/tick/producer thread/schema/文本字段；Flush 仅通知 worker。共享队列的拥塞统计统一为 `[Draw3DiagnosticOutput] dropped=...`，Cursor seq 是生产者原子序号，多生产者可在取得序号后交错入队，不能据日志输出顺序推断全局原子事件顺序。
- `Line` 使用定长缓冲及 to_chars 拼接，兼容本次已审计调用所用的 hex/dec/endl；不实例化 ostream、不分配逐条动态缓冲、不修改全局 cout。长记录有 `[truncated]`，Haptics/Undo 原跨语句片段合并成单记录。
- 进程级 Session 在主入口早期创建，普通启动在可选 Debug console 建立后 Start；Host/RTS/窗口等正常清理后 Stop，RAII 兜住早期返回。仅 PptCOM 延迟建立 console 且此前无输出端时，同一个 Session 可在该既有边界首次启动；已运行状态不会创建第二 worker。
- Stop 使用首次计算的同一个绝对 tick+1000ms，最多100ms drain，再循环 CancelSynchronousIo+至多10ms wait。只有 worker signaled 且路由解除后释放 event/state，句柄副本由 worker 关闭。超期返回 IsolatedPending、关闭接纳并保留最多一份独立状态，禁止后续第二 worker；不 TerminateThread、不无限 join。

## 变更文件

新增：`Inkeys/Inkeys/Drawing/Draw3/Draw3.DiagnosticOutput.h/.cpp`。

接线：`Inkeys/IdtMain.cpp`，`Inkeys/Inkeys.vcxproj`，`Inkeys/Inkeys.vcxproj.filters`；跨消费者最小登记 `inkStrokeModelerTestTests/inkStrokeModelerTestTests.vcxproj/.filters`、`InkeysHeadlessTests/InkeysHeadlessTests.vcxproj`。

诊断迁移：`Draw3.Diagnostics.cpp/.cppm`、`DrawingController.cpp`、`Host.cpp`、`RealtimeStylus.cpp`、`WindowControl.cpp`、`PenCursor.cpp`、`GraphicsInitialization.cpp`、`TransparentPresentation.cpp`、`Renderer.cpp`、`RendererPrimitives.cpp`、`HapticFeedback.cpp`、`AutoSave.cpp`、`PresentationAutoSave.cpp`、`Product.cpp`、`PptTiming.h`（上述短名均在 Draw3 目录）。

F2/F3 代理明确释放 Controller/PresentationAutoSave 后才重读并修改；Controller Run* 报告块原样保留，IoError 分支和 F3 行为未改动。没有修改专用 tail trace、真正 UInk/索引持久化、RuntimeMetrics 报告、独立 debugger-only 分支或第三方代码。

## 自检与待运行命令

`Inkeys.exe --draw3-diagnostic-output-test` 在配置/单实例/HWND前通过 argv 严格早退；大小写、引号和尾随空白由 CommandLineToArgvW/CompareStringOrdinal 处理，额外参数返回2；Release harness-off 参数同样返回2，不启动普通GUI。

真实生产自检覆盖：无输出端（无 debugger）不建 worker、四阶段初始化失败、正常 stdout/stderr pipe 与中文/数字、实际7字节分批 WriteFile、普通文件原有 prefix/position、长记录截断、真实 Cursor/HRESULT/Win32/PptTiming helper、stdout/stderr 各自真实满匿名pipe与读端存活、4生产者饱和、读端恢复后新记录与drop摘要、broken pipe及其他端继续、0进度故障注入、生产者vsStop、最后stop检查后/WriteFile前屏障引起首轮ERROR_NOT_FOUND并重复取消，以及不可取消等待注入的1000ms隔离/保活/拒绝新worker。最后隔离测试有意保留单份 state 到测试进程结束。

父会话须用修复后完整 solution 输出运行该入口（建议外部超时30秒且持续排空测试报告 stderr）。成功预期：exit0，最终 `[Draw3DiagnosticOutput] result=PASS failures=0 production_queue=1 real_pipe=1 hwnd=0`。输出 worker 的真实 pipe 取消必须为 Stopped，不能把有意隔离测试的 IsolatedPending 当作正常pipe通过。

## 已执行的静态检查

- VS 动态定位的 ARM64 原生 cl，`/Zs /std:c++20 /EHsc /utf-8 /W4 /DUNICODE /D_UNICODE`，分别 harness=1/0 检查新实现 TU：两次退出0，无warning。这里只检查语法，没有链接和执行，更不是完整产品验收。
- 已修改19个原计划既有F1文件对HEAD核对 BOM 与 CRLF：全部保持。随后新增的三个消费者工程清单也保持原格式；涉及五份项目/filters XML parse 均通过。
- `git diff --check` 无输出；两份新文件 `git diff --no-index --check NUL <path>` 无格式错误输出（no-index检测有差异返回1属于预期）。
- 标准流扫描：迁移文件的残余 CRT 输出仅为 Controller Run* 与 ULW benchmark 的测试报告；AutoSave 的 WriteFile 是真实保存，Renderer 的独立 OutputDebugStringW 及尾轨迹文件按规划保留。stdout/stderr 配对 mirror 已迁入 worker。

未运行：完整solution、生产自检、headless、GUI/console、物理输入、Win7。按分工由父会话串行构建/运行，不能将上述静态通过记作运行通过。

## 消费者登记补充

全量解析 git 跟踪的所有 vcxproj 的 ClCompile 输入；新sink实际消费者为主Inkeys、InkeysHeadlessTests（直接编译产品PenCursor）和inkStrokeModelerTestTests（直接编译产品AutoSave/PresentationAutoSave）。三者均已登记同一生产DiagnosticOutput.cpp，没有复制实现或创建新构建系统。Headless没有既有filters，未新建。独立工程构建/测试由父会话接管。

## 后续复核提示

- 发布/出队按成功入队顺序；诊断丢失/截断必须由统计和标记识别，不能声称事件链完整。Cursor 序号跨线程交错需在采集解释中保留限定。
- 延迟 console 的首次 Start 保留仅PptCOM原启动时机，不扩展运行中替换已运行输出器的标准句柄；普通继承file/pipe在首次Start复制。
- 仍需完整产品相同管道停读条件下的新线、Clear、设置和正常退出；不得用15秒退出监督或父强杀记为通过。TouchArea/Debug/RTS宏路径的实际覆盖需分别列清。
- 正常console与QuickEdit、物理触摸/笔、Win7和多屏均不能从匿名pipe/无窗口自检外推。
