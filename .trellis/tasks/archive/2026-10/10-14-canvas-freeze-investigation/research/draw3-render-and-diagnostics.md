# Research: Draw3 渲染、窗口同步与卡死取证

- Query: 调查 UI 仍能操作而 Draw3 画布失去响应的渲染/presenter 路径；重点核对 DComp/ULW、选择切回画笔、Resize、窗口显隐及 RTS 锁的等待关系，并整理现有诊断和确定性实验。
- Scope: internal；仅阅读源码、Trellis 规范和本地 Windows SDK/工具，不运行 GUI，不修改产品文件。
- Date: 2026-10-14（本会话客户端日期；研究期间收到 2026-10-15 环境日期更新）

## Findings

### 1. 视频日志的关键语义

主会话转述的约 50 秒日志为 `frame tool=0 owner=2 ... mouse=1/1 mouseLifecycle=0/0` 与 `present success=0 ... dirty=(0,0,0,0)`；本研究未独立读取视频像素，以下结论针对这些字段的生产代码语义。

- `tool=0` 是 `DrawingTool::Pen`（软笔），不是 Selection。枚举见 `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cppm:29`；日志实际输出的是 `cursorTool`，不是直接输出本帧绘制用的 `frameTool`，见 `Draw3.DrawingController.cpp:10546`。
- `owner=2` 是 `DrawingCursorPointerAuthority::Mouse`，见 `Draw3.PenCursor.cppm:42`。
- `mouse=1/1` 是光标样本 `valid/inContact`；`mouseLifecycle=0/0` 是速度橡皮悬停尺寸会话的 `HasPosition/NeedsAnimation`，软笔下不能把它解释为缺失鼠标 Down/Up，见 `Draw3.DrawingController.cpp:10550`。
- `mouse ... accepted=1 reason=accepted-mouse` **只证明 Win32 鼠标光标采样通过过滤**。`WindowControl.cpp:1724` 设置 Mouse owner，`:1737` 发布光标样本，`:1739` 记 accepted，`:1744` 随即 break；该分支没有 ContactInput 的 Down/Move/Up 入队。生产墨迹仍依赖唯一 RTS producer。这条日志不能排除 RTS 回调停滞、Down 准入失败或 contact mailbox 未消费。
- `presentSucceeded` 每轮在 `Draw3.DrawingController.cpp:14286` 初始化为 false，只有 `frameDirty` 非空且没有 raster failure 才在 `:14379` 调用 `PresentFrame`。因此 `:14419` 输出的 `present success=0 + 空 dirty` 常常只是本轮没有 Present，**不是 presenter/GPU 调用失败的证据**。
- `HostRuntimeSnapshot::lastPresentSucceeded` 是另一个字段：只由真实的 `ObservePresented` 回调更新（`Draw3.Host.cpp:638`），空 dirty 帧不会把它改成 false。

若故障期间仍连续出现新的 `frame`/`present` 配对日志，Draw3 绘制 owner 至少在这些时间点完成了循环；此时应优先追踪 RTS contact/准入/消费到 dirty 的链，不能先定性为绘制线程死锁。若之后日志停止，则仍需单独取得停止后的线程证据。

### 2. Files found

| 文件 | 作用 |
| --- | --- |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp` / `.h` | 独立绘制线程、RTS 初始化、桥接消费、RuntimeSnapshot、实际 Present 观察与生命周期 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` | 输出目标切换、contact 消费、dirty 合成、Resize/恢复/Present、空帧等待 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cpp` | DComp 主输出、ULW 辅助输出、CPU 读回、真实 Present/失败/恢复 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp` | Drawpad WndProc、鼠标光标采样、尺寸 mailbox、全量重绘请求 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp` | RTS 同步插件、decoder lifecycle writer、packet reader guard、COM 查询 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Product.cpp` | 产品唯一 Host、recursive 调用闸门、WndProc 转发与停止 drain |
| `Inkeys/Inkeys/Window/Window.cpp` / `Window.CommandCompletion.h` | HWND owner 命令队列、同步 future、显隐/样式/位置事务 |
| `Inkeys/IdtState.cpp` | 工具状态同步、Setting owner 收敛、Draw3 已呈现目标到窗口显隐的握手 |
| `Inkeys/Inkeys/UI/Setting/Setting.cpp` | ImGui recursive mutex、独立业务 FIFO、设置渲染时延后 SyncDraw3State |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Diagnostics.cpp` | CURSOR_TRACE 有界缓冲及批量控制台输出 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.HiddenWindowTest.cpp` | 真实 Host/Window Service 隐藏窗口集成、模式往返、Resize、指标 smoke |
| `InkeysHeadlessTests/window_tests.cpp` / `animation_tests.cpp` | 无 HWND 命令完成等待测试及 `--no-window` 分流 |
| `Inkeys/Inkeys/Helper/Helper.CrashHandler.cpp` | 现有 MiniDumpWriteDump 选项和 ARM64 栈展开参考；当前触发点是异常，不是 hang |

### 3. 已核对的线程和呈现合同

1. Draw3 与 UI3/Setting 使用不同设备和不同渲染线程。Draw3 `Host` 的 `drawingThread` 在 `Draw3.Host.cpp:1716` 创建；Setting/Bar 走共享 RenderPipeline。打开 Setting 并不直接把其 swap chain/RTV 交给 Draw3。相关规范为 `draw3-integration.md:95`、`rendering-and-ui.md:1502`。
2. Window Service 把 overlay 与 Setting 分为两组 owner 命令队列，见 `Window.cpp:1380`、`:909`。Drawpad/辅助画布/Bar 的 HWND 在 overlay 组；Setting 有独立 HWND 线程。调查时应记录实际 `GetWindowThreadProcessId`，不能把 UI RenderPipeline、overlay owner、Setting owner、RTS callback 和 Draw3 owner 混称为“UI 线程”。
3. RTS 在 Host 的调用线程初始化，而不是在 Draw3 绘制线程初始化；见 `Draw3.Host.cpp:1759`、`:1763`。`RealtimeStylus.cpp:2490` 已输出 initialization currentThread/windowThread，`:2498` 调用 `CoInitializeEx(COINIT_MULTITHREADED)`。回调实际线程仍须现场采样，不据此假定与初始化线程相同。
4. 普通 Selection → Pen 只切换输出目标：`DrawingController.cpp:12608` 决定 SelectionUlw/PrimaryDrawpad，`:12618` 调用 `SetOutputTarget`；`TransparentPresentation.cpp:1145` 仅变更 target 和 revision。**这不是 DComp → ULW 后端重建，也不会每次修改主窗样式。** 主 DComp presenter 与辅助 ULW presenter 在 `TransparentPresentation.cpp:927` 同次初始化并长期共存。
5. 模式切换把下一帧设为 full dirty（`DrawingController.cpp:12661`），真实成功全帧由 `Host.cpp:701` 更新 ready target/revision，随后 `IdtState.cpp:523` 检查 `DrawpadPresentationReady`。未就绪时 `:534` 保留旧表面；就绪后 `:572` 才同步提交成对显隐。旧墨迹保留因此也可能是握手尚未就绪，不能直接推断 GPU 已死。
6. 成对显隐通过 overlay owner 的 `Begin/Defer/EndDeferWindowPos`，并读回实际 visible 状态，见 `Window.cpp:1976`；主窗撤下前还会释放自己的 capture。辅助窗固定 layered/click-through，主窗不靠动态 `WS_EX_TRANSPARENT` 切换模式。
7. `WM_SIZE` 只发布 pending 尺寸和 wake（`WindowControl.cpp:1539`）；Draw3 owner 执行 renderer Resize、presenter Resize、最后 CommitSize（`DrawingController.cpp:7682`）。Renderer 的实际 `ResizeBuffers` 在 `Draw3.Renderer.cpp:488`，未发现窗口 owner 在 WndProc 中同步等待它完成的代码。

### 4. 可能的阻塞点与可证伪条件

| 候选 | 已确认代码事实 | 现场应看到的证据 / 可证伪方式 |
| --- | --- | --- |
| Setting ImGui 锁 → HWND future → WndProc 同锁 | 规范已记录 10-13 的这条死锁。当前 `SyncDraw3State` 入口有 defer（`IdtState.cpp:725`），设置 `SettingRenderScope` 为 TLS（`Setting.cpp:372`、`:8681`、`:8766`），业务 FIFO 在锁外执行真实 Sync（`:199`、`:8791`） | 如果当前版本仍中此环，需找到漏标作用域/旁路/持其他锁的同步调用，并同时取得 render 与 Setting owner 的两个栈；仅凭历史规范不能宣布本次根因。Bar 的真实帧提交与动画持续前进不支持“共享 RenderPipeline 永久卡在此环”。 |
| Draw3 owner → 样式 Window 命令 → overlay owner | 产品 style callback 确实同步调用 Service（`IdtMain.cpp:3207`）；跨线程 Service 默认 future.get 无超时（`Window.cpp:1376`，`Window.CommandCompletion.h:16`）；样式事务会 `SetWindowPos(...SWP_FRAMECHANGED)`（`Window.cpp:1851`） | 主要发生在初始化/运行失败重建，而非普通 Selection→Pen。需真实 recovery/style begin 但无 end，以及 owner 等待反向资源的栈。普通切换后 target/revision 和帧持续前进可反证这一解释。 |
| RTS lifecycle writer 持锁执行 COM 查询，与重入 callback/窗口线程互等 | `RtsStateWriterGuard` 在 `RealtimeStylus.cpp:112` 取得非 recursive mutex 后等待 reader 清零；UpdateMapping 在 `:1476` 持该 guard 调用 RebuildCurrentContextDecoders，后者 `:1709` 查询 source；InRange `:1120` 同样持 writer 后查询 decoder | 这是可疑等待边，不是已证明等待环。需记录 writer owner/回调类型/COM begin/end/重入，或 dump 中一线程持 writer 等 COM、另一线程/重入等同一 writer。packet reader 在 `:82` 只尝试一次 CAS，不能误说它也阻塞等待 writer。 |
| GPU/USER32 调用不返回 | ULW owner 在 `TransparentPresentation.cpp:537` 同步 Map 读回，`:591` 调 ULW；DComp 在 `:1038` Present1；Resize 在 `Renderer.cpp:488` | 新 RTS 输入计数增长、Draw3 phase 停在对应 API begin，线程栈落在 d3d/DXGI/driver/USER32，且重复快照不前进。若 frame 日志仍持续走到 `:14419`，至少不是一直卡在这些调用。 |
| 绘制线程已异常退出，窗口/UI 仍活 | `Host.cpp:1684` Run 异常会输出 drawing loop stopped；退出后释放资源并在 `:1709` 置 running=false。Presenter 恢复彻底失败在 `DrawingController.cpp:12675` 打印错误、RequestExit、break | 读取 running=false、失败日志和线程已退出的事实；不能把任何不动的画布都叫死锁。当前 RuntimeSnapshot 的 firstFrameReady 不能代替 running。 |
| 没有真实 contact / contact 被准入或 controller 丢弃 | 鼠标 accepted 只更新 cursor，空 dirty 合法；这与视频转述日志直接兼容 | 同一 Down/Move/Up 的 RTS callback 序号、Coordinator published/recycled、controller consumed/adopted 及拒收原因应连续对应。若该链产生几何和非空 dirty 却仍无像素，再回查呈现。 |
| 诊断输出自身阻塞 | `Diagnostics.cpp:574` 每帧批量 Flush；`:48` 使用同步 WriteConsoleA/WriteFile。写控制台发生在 ring mutex 外，但发生在 Draw3 owner | dump/phase 显示停在 console I/O 才成立；正常产品关诊断的对照有意义。不要因加日志改变症状而直接断言输入问题已消失。 |

没有从普通 Selection→Pen 的静态调用链中证明“Draw3 owner 持 RTS writer mutex 同步等 Window Service”的直接边。RTS writer 属于插件回调路径，Draw3 consumer 不直接取得它；真实线程与重入关系必须测量。

`Product.cpp:13` 已使用 recursive `productCallMutex`，避免 owner-thread SetWindowPos 同步重入 Drawpad WndProc 时自锁；停止在 `:38` 先 drain 再锁外 Host::Stop。不能仅因发现 mutex 就改成 recursive 或 try_lock，也不应把已规避的停止环混作运行中根因。

### 5. 可复用的诊断及其限制

| 诊断 | 能回答的问题 | 边界 |
| --- | --- | --- |
| `[RTS] initialize`（`RealtimeStylus.cpp:2490`） | 初始化线程、HWND owner、digitizer/tablet window flags | 不证明之后每次 callback 在线 |
| `[Draw3Diag][target]`（`TransparentPresentation.cpp:1155`） | 请求输出目标及 revision 是否变化 | 受 StartupEnvironmentDiagnosticsEnabled 控制；不是实际成功 Present |
| `[Draw3Diag][present] event=success/failure/recovery`（`Host.cpp:666`） | 真实 attempt 返回、mode/target/revision/content/dirty/HRESULT | 目标/成功状态变化时输出；不是每空帧输出 |
| `[Draw3Diag][ulw]`（`TransparentPresentation.cpp:481`） | 失败在资源/DIB/Map/ULW 哪一步、Win32/HRESULT | 失败去重；只有退出 API 后才有结果，卡在 API 中须 begin/phase 或栈 |
| Window paired visibility 状态（`Window.cpp:1983`） | 两个 HWND 的实际 visible/owner/style、显隐失败 | 仅状态变化/失败采样；建议同时查 HWND thread、capture、focus |
| RuntimeSnapshot（`Host.h:237`，实现 `Host.cpp:1924`） | running、inputDown/Move/Terminal/Recycled、真实 Present 成功/失败、resize、工具 selection、workspace、content、目标请求/就绪 revision、presentation gate | 不是完全无锁：读取 eraserDiagnosticsMutex、metrics.metadataMutex、presentationTargetMutex。不能把它当成永不阻塞的 hang watchdog；各原子字段也不是一个统一原子事务 |
| Host RuntimeMetrics（`Host.h:224`、`:195`） | 显式开启后 frameSerial/presentAttempts、contactSeen/pending/confirmed、finite drops | 产品默认不开。开关在 HostStartOptions，普通 main 并未默认设置 true；WriteRuntimeMetrics 需结束并真 join 后离线写，不能作为 hang 中即时导出工具 |
| CURSOR_TRACE（`Diagnostics.cpp:544`、`:574`） | 原始线程 ID、tick、sequence、光标过滤和帧信息 | 有界队列会丢旧行，必须保留 dropped；在 render Flush 停止时“控制台无新输入行”也可能只是没被 drain，不能证明 producer 无 callback |
| IDTLogger（`IdtMain.cpp:2335`） | 工具/窗口协调日志、重试和错误 | `<程序目录>/log/idt<timestamp>.log`；普通 logger overflow=block，不适合作为加在锁内的高频取证写口 |

建议调查插桩只发布数值到有界内存/原子阶段：Draw3 heartbeat/frameSerial、当前 phase、最后 begin/end 的 tick/thread/操作 ID、输入 published/consumed 及拒绝计数、bridge applied/requested revision、RTS writer owner 和 reader count。格式化/磁盘导出放独立诊断线程且不调用 Window Service、不取业务锁。不能仅加“每 250 ms 日志”就保证卡死现场可读。

### 6. 本地取证工具与 Win32 方法

已执行只读盘点（没有 attach、没有 GUI、没有开始 ETW 录制）：

- `C:\Windows\System32\wpr.exe` 存在；`wpr -profiles` 成功，版本 `10.0.26100`，列有 GeneralProfile、CPU、GPU、DesktopComposition、Handle 等 profile。
- `C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit\xperf.exe` 存在。
- PATH 没有找到 cdb/windbg/windbgx/procdump。`C:\Program Files (x86)\Windows Kits\10\Debuggers\arm64` 实际只有 dbgcore.dll、dbghelp.dll、srcsrv.dll、symsrv.dll；不能把此目录当作已安装 debugger CLI。`D:\Project\Resource` 未检出这些可执行文件。
- Windows SDK include 版本是 `10.0.26100.0`。`um/Wct.h:111` / `:131` 声明 OpenThreadWaitChainSession / GetThreadWaitChain；`:33` 有 CriticalSection、SendMessage、Mutex、ALPC、COM、ThreadWait 类型，`:67` 最大 16 节点，`:122` 有 out-of-proc/COM/CS flags，`:146` 有 RegisterWaitChainCOMCallback。
- `um/minidumpapiset.h:1474` 声明 MiniDumpWriteDump，`:1368` 开始有 full-memory、handles、unloaded-modules、thread-info 等 flags。

可由主会话在用户已授权的复现实验中使用的取证顺序：

1. 记录目标 PID、主/辅助 Drawpad 和 Setting HWND 的实际 owner thread ID；外部枚举进程线程，采两到三组带时间的 WCT。同步 WCT probe 使用 session flags=0、有效长度的 16 节点数组，并保存 BOOL、GetLastError、IsCycle 和每个节点的类型/状态/线程/对象名。不要把单条 `Blocked` 等同于 cycle。
2. WCT 中如出现 COM/SendMessage 链，结合源码中的 future/Window 命令和 RTS writer 相互映射。COM 状态回调支持可参考本地 Wct.h 的 RegisterWaitChainCOMCallback，并按当前系统导出实测；本研究未实际注册。
3. **WCT 没报 cycle 不能排除死锁**：std::mutex 的底层实现、原子自旋、GPU 等待和部分 COM 细节可能无法被 WCT 完整关联。特别 `RtsStateWriterGuard` reader 等待是 YieldProcessor 自旋，可能体现为 Running/CPU 消耗而非内核 wait。
4. 独立 helper 可使用已存在的 ARM64 dbghelp.dll，通过 MiniDumpWriteDump 对指定 PID 写 hang dump，不需制造异常或调用产品退出。`Helper.CrashHandler.cpp:724` 的 flags 和 `:348` 的 ARM64 栈展开是本地参考；现有 crash handler 本身不会自动抓 hang。helper 应记录 API 结果/错误，保留与运行 EXE 对应的 PDB。
5. 如可获得适用架构的 debugger CLI，取所有线程栈、模块和锁状态，重点找 future.get/condition wait、SetWindowPos/SendMessage、RTSCOM/COM、stateWriterMutex、D3D Map/Present1/ResizeBuffers、WriteConsole。当前机器没有验证可用的 debugger CLI；不把 `rundll32 comsvcs` 等未核对路径写成已验证方案。
6. 对 GPU/API 不返回或自旋，WPR 的 CPU/GPU/DesktopComposition 轨迹可以区分 owner 是否仍被调度、CPU 是否持续自旋、GPU/合成是否还有提交。先核对已有录制状态并使用独立输出路径；ETW 录制/分析尚未执行，不据 profile 存在宣称已拿到栈或证明驱动故障。

### 7. 最小复现实验矩阵（待主会话执行）

每次使用独立 run ID，固定笔画区域，记录 baseline 后完成同一条 Down→Move→Up，并以 contact published/consumed、content revision 和真实成功 Present 共同验收，不能只看鼠标光标或按钮高亮。

1. **Baseline**：启动→选软笔→连续画三笔→等最后 Up 回收；记录初始 mode、两窗 visible/owner、input/present/revision。
2. **Setting-only**：保持软笔→打开设置→不改选项→关闭→原区域画三笔；按“打开/关闭/首个 Down”分别打时间标记，循环多次。另做“设置打开但不关，直接回画布”以分开 focus/capture 与 hide 生命周期。
3. **Selection-only**：baseline→Selection→软笔→画笔/橡皮；对有内容/无内容两种分别测试，请求和就绪 output revision 应成对前进，不应出现后端重建日志。
4. **组合**：baseline→设置开关→通过 Inkeys 打开资源管理器→Selection→软笔/橡皮→画笔。与相同步骤但跳过设置对照，避免把必要条件和相关条件混在一起。
5. **后端对照**：使用现有 Host requiredPresentationMode 的隐藏 harness 对比 DComp 主目标和 ULW 主目标；生产人工复现若需强制后端，应仅加显式诊断启动项，不改变默认发布行为。两个模式都失效且绘制 frame 活跃，降低单后端 GPU 原因的优先级。
6. **Resize 对照**：单独改变 Drawpad bounds（现有隐藏 harness），确认 Resize mailbox→CommitSize→full Present；只有与设置开关相关、而无尺寸变化时，不能把 Resize 当成必经触发器。

故障当下保留进程：先采 WCT/dump/数值，再尝试工具/选择往返等恢复动作，避免先重启清掉等待图。采样应覆盖故障前、首个失效 Down、后续 1–3 秒，而不只故障后的静态画面。

### 8. 无窗口与隐藏 harness 的复用边界

- `InkeysHeadlessTests.exe --no-window` 的入口分流在 `animation_tests.cpp:1644`；即使跳过 HWND，也会运行 `RunWindowCommandCompletionTests`（`:1672`）、UI render scheduler、Draw3 bridge 和 ContactInput（`:1694`）。`window_tests.cpp:124` 已用真实 WaitForCommandResult 测试 deadline 与迟到 promise 安全；普通命令无 deadline 的事实也在 `:151` 断言。
- `draw3_contact_tests.cpp` 可复用生产 Coordinator 对准入/队列/回收做确定性计数对照；它不经过 RTSCOM，也不证明真实窗口互锁。
- `RealtimeStylus.cpp:2393` 的 `RtsStateGateForTesting` 已构造 reader→writer→第二个 reader 的确定性交错：writer bit 立刻阻止新 reader，旧 reader 释放后 writer 完成。对当前仓库的搜索只找到此 helper 定义及声明，**未找到被 HeadlessTests 或 main 调用的证据**，不能把 helper 存在算成现成已跑覆盖。
- `Inkeys.exe --draw3-hidden-test` 的主入口在 `IdtMain.cpp:1360`，真实 RunMode 在 `HiddenWindowTest.cpp:854`。它覆盖选择模式 output revision 往返（`:953`、`:990`）、真实窗口 capture、两窗 bounds/Resize（`:1644`）、主 DComp 和 legacy ULW 两代顺序生命周期（`:3896` 之后）。它创建真实 HWND，不是 `--no-window`，也没有复现真实 Setting/Explorer/RTSCOM 设备交互的证据。
- `--draw3-host-metrics-smoke --output-root <private dir>` 在 `IdtMain.cpp:1258` 进入 `RunHiddenWindowRuntimeMetricsSmoke`，用真实 Host 开/关指标和离线 JSON 导出；同样创建隐藏窗口。
- Renderer pixel test 入口在 `IdtMain.cpp:1210`，在普通产品窗口初始化之前进入自检；WARP/hardware 两入口分开。它验证像素/上传合同，不能验证 RTS 生命周期或 Setting 锁。
- Debug 的 Ui3PresentationHarness 默认启用，Release 默认关闭（`Inkeys.vcxproj:84`）；内部 harness 参数在关闭构建的行为要按现有发布边界核对，不能把普通发布 EXE 的拒绝当产品故障。

### 9. 确定性等待链测试思路（仅方案，未写代码）

1. **真实同步窗口等待合同**：复用 WaitForCommandResult/production queue seam，owner 先停在可释放 barrier；caller 提交后记录 WaitingFuture，owner 记录执行前/后。测试线程证明当前普通等待确实未返回，再释放 owner，验证原 promise 完成。所有失败路径先释放 barrier 再 join，不能靠测试超时杀掉产品线程。
2. **Setting 历史环防回归**：使用真实 Setting 业务 FIFO、SettingRenderScope 和 DeferDraw3SyncIfRendering。render 在 ImGui 等价 mutex 下提交 Sync，模拟 HWND owner 正等同锁；必须在 owner 完成之前让 render 释放锁，worker 再执行真实同步。只用“调用 Sync 次数加一”的桩不足以证明绕开等待环。当前生产代码有 guard，但未在本研究中发现独立的现成生产 FIFO 测试入口。
3. **RTS writer/COM 重入图**：从现有 RtsStateGateForTesting 扩展显式 test-only seam，在 writer 已取锁、即将 COM query 时停住；假 source 将 query 完成依赖于另一个 UpdateMapping/InRange 请求。记录 owner TID、回调 ID、writer acquired/COM begin/reader count、第二次 writer attempted；先证明等待边，再由测试主动放行 source，验证 guard 释放和下一笔恢复。不能让测试真实永久自锁；可在锁尝试前用 barrier 观测，或在独立受监督 child 中运行必死情形并保留证据。
4. **绘制与窗口不互等合同**：把 Present/Resize/Window style 的 begin/end 旁路事件用于可控暂停。主窗 WndProc 发布 resize 后应立即返回，Draw3 owner 后续消费；暂停 Draw3 renderer 不应使纯尺寸 mailbox 发布等待它。仅检查最后宽高相等不能验证这一点。
5. **空帧与失败分母**：用真实 controller/Present 观察 seam 分开验证“empty dirty → no attempt，Host counters 不变”“attempt false → failure/recovery”“input published 但未 adopted → 无 dirty”。这能防止以后把 `CURSOR_TRACE present success=0` 当 GPU 失败重新误诊。

## Related Specs

- `.trellis/spec/native-desktop/draw3-integration.md:55`：窗口所有权；`:95` 设备与线程；`:345` Selection 与内容/output 握手；`:412` presenter；`:427` 有限软件指标证据。
- `.trellis/spec/native-desktop/rendering-and-ui.md:1520`：Setting 渲染与 FIFO；`:1669` Window Service 合同；`:1927` 已知 Setting 锁环与 defer 合同。
- `.trellis/spec/native-desktop/input-and-ink.md:67`：显式光标诊断与不能从日志开关推导已修复；`:423` Contact Closing/拒收回收。
- `.trellis/spec/native-desktop/ui3-render-diagnostics.md:30`：非阻塞、有限计量、真实提交与代理时刻分离；它属于共享 UI3 管线，不是 Draw3 GPU 的统一计时器。
- `.trellis/spec/native-desktop/build-and-compatibility.md:66`：Debug/Release harness 边界。
- `.trellis/spec/native-desktop/errors-logging-and-resources.md:3`：静态风险不冒充已确认缺陷；`:21` 日志；`:61` 等待与退出合同。

## External References / Versions

未访问网络。外部 API 的本地依据为 Windows SDK `10.0.26100.0` 的 `um/Wct.h`、`um/minidumpapiset.h`；工具依据为本机 `wpr -profiles` 版本 `10.0.26100` 及实际目录盘点。WCT/DbgHelp 在目标系统的取证效果尚未运行验证。

## Caveats / Not Found

- 没有复现真实故障、没有故障现场 dump/WCT/ETW，不能据静态等待边认定死锁或驱动根因。
- 没有执行编译或测试；本研究只生成本 Markdown，未改代码/配置/工程、未执行任何 git 操作、未运行 GUI。
- CURSOR_TRACE 证明鼠标 cursor 采样并不证明 RTS 墨迹 contact；空 dirty 的 `present success=0` 不等于失败，必须在最终分析中保留这两个区分。
- 历史规范中既有 Draw2/旧日志错误边界，也有后续修订；以本次逐行读取的生产 Draw3 路径为准，不用旧源码推断 fallback 已启用。
- 未发现现成“live hang dump/等待链”产品接口，也未发现本机可用 debugger CLI；可复用的是 WinSDK API、dbghelp DLL 与现有异常 dump 的参数/架构处理。
