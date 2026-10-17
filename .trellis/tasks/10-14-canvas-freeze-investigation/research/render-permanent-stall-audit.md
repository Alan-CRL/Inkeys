# Research: Draw3 渲染与宿主永久停滞审计

- Query: 扩展检查 Draw3 renderer/controller/Host 的 mutex、CV、future、join、D3D/DXGI、工作线程异常和循环，寻找“画布持续失效而 Bar/Setting 仍可操作”的可达链，并提供不修改产品的确定性实验。
- Scope: internal；只读产品、规格与现有测试。仅在本任务 research 下写研究文档和独立实验源码；本研究者不构建、不运行实验、不启动 GUI、不执行 Git。
- Date: 2026-10-15（Asia/Shanghai）

## Findings

### 1. 结论与证据等级

找到并通过无窗口原码实验确认一条不依赖 OOM 的逻辑停滞链：**Presentation 当前槽恢复时，合法 UInk 被短暂独占 → 真实读取失败被统一归为 SourceChanged → Controller 一次进入 manualOnly → 释放占用后，同一 target 没有自动重提，输入和破坏性画布命令持续被保护闸门抑制。** 这条链不要求工作线程死锁或异常退出，适用于有同进程持久化内容、需要从磁盘恢复的冷槽；工具 UI 可以继续同步。主会话已编译执行真实 worker/UInk/Bridge 与原码闸门片段实验，退出 0，`Build/CanvasFreezeInvestigation/presentation-load-probe/run.log:45` 为 `RESULT failures=0`；完整产品冷槽/画面仍未在本实验运行。

该链是有明确前提的另一类画布无响应，**不是用户视频的已确认根因**。视频叙述为启动后设置/Explorer/选择往返，现有证据没有 Presentation 冷槽或文件占用。这里的“持续”只表示在同 target 与该失败状态持续期间不会自行恢复；翻页、新场景 revision、切出再进入或明确新 load 请求可能解除，不能写成所有工作区永久失效或只能重启。

另外确认 PPT completion 分配失败会丢终态，留下 pending 输入闸门的静态缺陷；该项依赖内存分配失败，优先级低于上述普通 I/O 对照。设备重建失败、Run 异常返回本身有受控退出监督，不能直接解释长期 UI 存活。仍存在真实外部同步调用可能一直不返回的边界，但当前没有证明 DWM/驱动/API 在用户操作序列下必然形成等待环。

### 2. 已检查的主要文件

| 文件 | 作用 |
| --- | --- |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` | 唯一 render loop、GPU/history 失败处理、输入抑制、Current load retry、工作区切换、idle pacing、生产 probe |
| `Draw3.Host.cpp`、`Draw3.Host.h`（同目录） | render jthread、bridge ingress、窗口样式回调、mutex/CV、生命周期、runtime snapshot 与退出收敛 |
| `Draw3.TransparentPresentation.cpp` | DComp/ULW 初始化与恢复、Present1、ULW staging Map、DComp detach/wait |
| `Draw3.Renderer.cpp`、`Draw3.RendererPrimitives.cpp`、`Draw3.RendererLaser.cpp` | ResizeBuffers、资源释放、Map、分批绘制与循环进展 |
| `Draw3.InkHistory.cpp`、`Draw3.InkHistoryGpu.cpp` | CPU 范围树/淘汰、GPU slice 分配/重排、tile replay |
| `Draw3.AutoSave.cpp`、`Draw3.PresentationAutoSave.cpp` 及对应 cppm | 保存/加载 owned worker、命名 mutex、终态队列与输入闸门耦合 |
| `Draw3.Bridge.cpp`、`Draw3.Bridge.h` | 同 target 去重、目标 revision、输入/命令抑制谓词 |
| `Draw3.Product.cpp` | productCallMutex、Start/Stop/forward 包装；Stop drain 与 Host.Stop 边界 |
| `Draw3.HapticFeedback.cpp` | render owner 上的 WinRT 同步触觉调用 |
| `Inkeys/Inkeys/Window/Window.cpp`、`Window.CommandCompletion.h` | owner-thread 命令、promise/future、可选 deadline 和窗口显隐/样式事务 |
| `Inkeys/IdtMain.cpp`、`Inkeys/IdtState.cpp` | 产品 callbacks、Draw3 running 监督、UI 与 surface reconciliation |
| `inkStrokeModelerTest/draw3/uink_file.cpp` | 产品直接编译的真实 UInk 读取、sharing retry、路径事务 mutex |
| `inkStrokeModelerTestTests/presentation_autosave_tests.cpp` | 复用的合法最小 PPT 快照和真实 service 保存/恢复测试形状 |

除列出的完整路径外，本文件中 `Draw3.*` 引用均位于 `Inkeys/Inkeys/Drawing/Draw3/`。读取了 `.trellis/workflow.md`、native-desktop index、并发/错误/渲染相关规格；没有加载 role-isolated 的 implement/check JSONL。

### 3. 非 OOM 的强可达链：短暂 UInk 占用变成无自动恢复状态

#### 3.1 到达恢复请求的生产入口

1. 有成功保存的 Presentation 槽成为 inactive 后，`ShouldEvictPresentationSlot` 要求 committed 文件、mutation/queued/committed 相等且非 pending（`Draw3.PresentationAutoSave.cppm:36`）；Controller 的 `canEvictPresentationSlot` 还核对 continuation/redo，`Draw3.DrawingController.cpp:10927`。真实切出分支在 `:12251` 擦除满足条件的旧 Presentation lane；parked completion 也可在 `:11951`、`:12125` 触发淘汰。因此“冷槽恢复”不是虚构入口。
2. 回到该 target 后，Controller `SetPresentationTarget` 路径 `:12130` 进行 CPU slot 切换，`!persistenceInitialized && !loadPending && mutationRevision==0 && queuedRevision==0` 时在 `:12198`–`:12215` 调用真实 `presentationLoadRequested`。只有成功恢复/验证空白才发 workspace ready（`:12217`–`:12223`）。
3. 若是仍有完整内容的 warm/active 槽，该前提不成立，不会因为相同文件被占用就影响正在绘制。测试必须区分这两种状态。

#### 3.2 普通短暂文件占用如何成为 SourceChanged

- worker `LoadPresentation` 先获得 Presentation 索引 mutex，严格选出当前 session 的 index entry，然后调用 `ReadUInkFile(path)`，`Draw3.PresentationAutoSave.cpp:1468`、`:1482`、`:1567`。
- 共享 UInk 实现 `OpenWithRetry` 对 `ERROR_SHARING_VIOLATION/ERROR_LOCK_VIOLATION` 做有限次数重试，`inkStrokeModelerTest/draw3/uink_file.cpp:195`–`:218`；默认是 3 次、间隔 100 ms，`uink_file.cppm:17`。一直保留 FileShare.None 句柄到 completion 即可确定覆盖该窗口，不需要损坏文件或内存分配失败。
- `ReadUInkFile` 无法打开/取得 snapshot 时返回 `UInkReadStatus::IoError`，`uink_file.cpp:1046`–`:1057`。
- 但 Presentation load 对 `read.status != Complete`、无 document、invalid/recovered block、revision 缺失/不等使用同一条件，然后统一返回 **SourceChanged**，`Draw3.PresentationAutoSave.cpp:1568`–`:1575`。暂时无法读取因此没有作为可重试 IoError 传到 controller。

#### 3.3 终态到达后为何持续拒绝新笔迹

- 匹配 active completion 后先清 `activePresentationLoadPending=false`，但非 Loaded/verified NotFound 会保持 `activePresentationPersistenceInitialized=false`，`Draw3.DrawingController.cpp:12059`–`:12064`。
- 只有 `completion.status==IoError` 才调用 `OnIoError`；SourceChanged 走 `OnTerminalFailure()` 并记录 `action=current_load result=manual_required`，`:12065`–`:12077`。`OnTerminalFailure` 设置 `scheduled_=false; manualOnly_=true`，`:870`–`:874`。
- `PresentationLoadUnresolved(workspace, initialized, mutation)` 在 Presentation、未 initialized、mutation=0 时保持 true，`:786`–`:791`。真实 Down 在 `:9850`–`:9856` 调用 `DiscardUntilTerminal`；Clear/Undo/Redo 等在 `:12262`–`:12268` 仅回执命令后跳过处理。主线程和 UI 线程不必卡住，renderer 也仍可执行控制帧。
- 正常 retry safe point `:13171`–`:13185` 不能解除 manualOnly，因为 `TrySubmit` 在 `:893` 直接返回 false；`RemainingWaitMilliseconds` 在 `:880` 返回 nullopt。随着时间过去、文件占用消失均不改变该状态。
- 初始恢复期间没有 identity-ready；Host `RefreshPresentationInputGateLocked` 对 ready identity 不匹配关闭 admission，`Draw3.Host.cpp:558`–`:564`。因此某些真实输入可能已在 ingress 准入前被拒绝，也可能在 Controller 的 unresolved 谓词处被拒绝；诊断应同时记录两层。

#### 3.4 工具点击为何不是可靠的手动重试；哪些动作能够恢复

- Controller 确有“外部明确重发相同 target”的重读分支，`:12135`–`:12155`，会 `currentLoadRetry.Cancel()` 再 `SubmitCurrentPresentationLoad`。这证明不是必须重启才能修复内存状态。
- 但实际 `StateBridge::PublishPresentationTarget` 遇到 `SamePresentationTargetIgnoringRevision` 直接返回旧 targetRevision，`Draw3.Bridge.cpp:75`–`:78`；Host 只在 targetRevision 改变或 restoreLatestScene 时排 `SetPresentationTarget`，`Draw3.Host.cpp:1294`–`:1304`。工具快照虽会推进 state.revision，不能因此重新排同 target load。
- 未发现产品 UI 对 `manual_required` 的提示/显式 retry 入口；全源码搜索此日志只命中 Controller。该“未发现”不等于证明任何 UI 路径都不能间接重发场景。
- 正常翻页、binding/session/target 身份变化或切出后重新进入，可以产生新 target command；Controller 目标变化会 `Cancel()` 旧计时请求，并在新目标仍 unresolved 时重新请求，`:12193`–`:12215`。是否每一种 UI 操作都触发该分支，仍需完整 Host/产品动态验证。

#### 3.5 与已存在的四次自动 retry 合同区分

`OnIoError` 的 250/500/1000/2000 ms 是最多 **4 次自动重提**，加初次请求最多 5 次失败；耗尽后同样 manualOnly，`:808`、`:848`–`:857`。已有 `RunPresentationCurrentLoadRetryProbe` 明确断言该上限，`:4659`–`:4684`，产品参数在 `IdtMain.cpp:1342`。本次发现更窄：可恢复的读取占用在 worker 被转为 SourceChanged，因此一次就跳过现有 IoError 重试。不能把上限本身随意称为新根因，也不能为“恢复绘制”跳过文件身份核对或把未读取内容当作已验证空白。

### 4. 已提供的真实无窗口实验

文件均在本任务 research：

- `presentation-load-sharing-probe.cpp`：导入真实产品 PresentationAutoSave/UInk module，链接已有产品 obj，构造合法最小快照；实际 durable save 与 baseline Loaded 后，持有 FileShare.None 句柄，执行真实 ReadUInkFile 与真实 SubmitLoad/worker/completion。随后执行当前源码提取的失败分支和 retry/gate，释放句柄，核对源 revision/SHA 不变、文件可读、24 小时模拟时间仍无自动重提，最后明确新请求可 Loaded。
- `make-presentation-load-probe.py`：仅摘取 `Draw3.DrawingController.cpp:753` 的 lane/retry helpers、`:12061` 的当前完成失败分支、`Draw3.Host.cpp:1294` 的目标排队条件，写 generated header 和 SHA256/行号 provenance；没有编译/执行产品。
- `presentation-load-gate.generated.hpp`、`presentation-load-probe-source.json`：已生成。失败分支放在 do/while(false) 中只为保留原来的 continue 语法；片段内容不改写。
- `run-presentation-load-probe.ps1`：给主会话使用。要求已初始化 ARM64 C++ SDK 环境，传入与当前产品 IFC 相同 MSVC 的 cl 和完整静态 vcpkg provider。编译新 main 并链接当前 `Inkeys/Cache/ARM64/Debug` 的 service、Presentation、Bridge、UInk obj/IFC；不改 solution/project，不重新编译产品。

主会话调用示意（路径参数使用现有工具链实际值）：

```powershell
& '.trellis/tasks/10-14-canvas-freeze-investigation/research/run-presentation-load-probe.ps1' `
  -ClPath '<matching ARM64 cl.exe>' `
  -ProviderRoot 'D:/Project/Inkeys/Repo/Inkeys/VcpkgInstalled/Arm64/arm64-windows-static-v143'
```

运行脚本的默认产物在 `Build/CanvasFreezeInvestigation/presentation-load-probe/`：build/run 日志、退出码、随机 GUID fixture 根目录；脚本检查解析后的输出绝对路径仅在此目录及其子目录内，不自动删除，以保留索引/UInk 证据。research 只保存源码、generated header 和 provenance，不放二进制或临时 session 文件。真实 worker 的 drain 无人为终态超时，主会话应给整个进程合理外部超时；不得把外部终止等同产品成功排空。

**运行验证（主会话执行，本研究者只读核验）：** 生成器退出 0，原码片段与哈希已核对；C++ probe 已编译运行，`build.exitcode` 与 `run.exitcode` 均为 0。实际日志为 `Build/CanvasFreezeInvestigation/presentation-load-probe/run.log`：

| 实际断言 | 日志位置 | 证据范围 |
| --- | --- | --- |
| durable UInk/index 成功、baseline 同 session load 为 Loaded | `run.log:9`、`:18` | 合法 fixture 与真实 worker 基线通过 |
| FileShare.None 成功，真实 UInk 返回 IoError（枚举值6） | `:19`–`:21` | 是实际分享冲突，没有改文件或注入 OOM |
| 相同 Current load 被接受，真实 worker 返回 SourceChanged（枚举值4） | `:23`–`:28` | 失败身份/generation 保留，分类缺口实际可执行 |
| 原码 failure branch 维持 unresolved/input suppression | `:29`–`:30` | Controller 原码片段，非完整 Run/RTS |
| 释放占用后文件立刻 Complete，source revision/SHA256 不变 | `:31`–`:32` | 没有实际内容改变；SourceChanged 来源是不可读 |
| 原码 retry 没期限，模拟前进24h仍不自动提交；真实 Bridge 同target去重 | `:33`–`:37` | 24h 是输入时钟前移，不是实等24h；原码状态不自愈 |
| 明确发新 load 后真实 worker 为 Loaded；新scene可排新目标 | `:42`–`:44` | 证明可恢复，不是全产品只能重启 |
| 所有断言通过 | `:45`、`run.exitcode:1` | `failures=0`、退出0、no_hwnd=1 |

首次编译使用匹配的 MSVC 14.44.35207，但 probe 未显式设置产品 IFC 的 `/fp:precise`，复编前审读记录中有6条 C5050 `_M_FP_PRECISE` 环境不一致 warning。研究脚本已补 `/fp:precise`，没有修改产品；主会话随后完成复编/重跑。本研究者核对更新后的 `build.log`（UTC 2026-10-15 00:55:41）仅一行源码文件名，没有 C5050 或其它 warning/error，`build.exitcode=0`；`run.log`（00:55:47）仍全部 PASS、末行 failures=0，`run.exitcode=0`。因此匹配参数后的重测也已通过。

证明边界：本实验真实执行 service、文件系统、编码/严格读取及桥接；Controller/Host 使用原码摘录，提供故障分类和状态转移的确定性证明。没有运行完整 DrawingController::Run、RTS、Bar、HWND、DComp；不能因此宣称完整用户画面已复现。若 baseline save/Loaded 失败，应先报告 fixture/链接/环境问题，不在后续失败上强推结论。

### 5. 设备重建、Present pending、Map 与 pipeline fence

| 审计项 | 实际行为及 source | 对“长期 UI 存活”的结论 |
| --- | --- | --- |
| 设备重建失败 | Controller `:12675` false → capture fatal → RequestExit → break；history rebuild failure 同样 `:12697`。presenter Recover `:1195` 设备创建失败返回 false，模式尝试有限 `:1219` | 失败返回不是永远卡 render loop。必须另证明 cleanup 卡住、主监督卡住或历史版本差异 |
| 正常 Present pending | `TransparentPresentation.cpp:1038` 是同步 `Present1(0,0,...)`。生产没有保存 pending 状态后不断轮询的应用层机制 | API 若一直不返回确实冻结 render owner，但未发现普通设置操作必然导致它不返回的代码证明 |
| frame latency fence | swap chain 创建时启用 FRAME_LATENCY_WAITABLE_OBJECT、SetMaximumFrameLatency(1)，`:855` 附近 | 全产品 Draw3 未发现 GetFrameLatencyWaitableObject、keyed mutex 或 D3D11 fence 等待；不能凭创建 flag 推出错误 wait 链 |
| DComp 释放 | Reset `:625` SetRoot(nullptr)，Commit 成功后 `WaitForCommitCompletion():633` 无取消/超时；Recover/Shutdown 都会走 ReleaseAttempt | 是实际无界外部同步边界；正常 mode/tool 往返只是换输出目标，不每次执行 DComp Reset。需故障 dump/阶段取证证明停在该 API |
| ULW readback | CopySubresourceRegion 后 staging `Map(READ, flags=0)`，`:534`–`:537`；UpdateLayeredWindowIndirect `:591` | 均可能是外部调用边界，无应用层重试死循环；Map 返回失败后会报告失败并进入恢复，不能把空 dirty 日志当作失败证据 |
| 普通 GPU Map 失败且 device 仍 S_OK | rasterSubmissionFailed 设置 activeLayerRebuildPending，最多只把日志计数截到3；只有 GetDeviceRemovedReason 失败才触发 device recovery，Controller `:14384`–`:14411` | **若**某资源/配置永久使 Map/提交失败而 device 仍 S_OK，产品会永远重放失败；没找到普通短操作造成这种永久错误的确定入口。需记录实际失败站点/HRESULT，不能仅注入永远失败就认定用户根因 |
| Resize | `WM_SIZE` 只置 pending 原子值；render owner `ProcessPendingResize:7682` 执行 Renderer.Resize，再 presenter.Resize，成功后 CommitSize；Renderer `:485` 释放尺寸资源再 ResizeBuffers `:488` | 未见基于旧 size 的无限内层 retry。外部 ResizeBuffers 不返回仍需现场；返回失败则转 recovery |

`present success=0` 字段仍受早期报告限制：Controller `:14286` 初始 false，空 dirty、不调用 Present 也会记录 `:14419`。不能拿它证明 GPU 在一直失败。源日志在某时刻继续只限制那个时刻；不排除下一帧另一路阻塞。

### 6. 循环、异常退出、线程清理

- 普通笔/laser 分批 `batchCount=min(remaining, 200000)`，`Draw3.Renderer.cppm:230`；笔至少剩2点，`RendererPrimitives.cpp:60` 每轮 `startIndex += batchCount-1`（`:106`），laser 对应 `RendererLaser.cpp:159`/`:205`。Highlighter/shape/dots 每轮按正 batch 数推进。未发现正常容量为0导致内部循环永不前进。
- InkHistory 范围树 invalidate `Draw3.InkHistory.cpp:643` 每轮 blockCount 加倍至 root；evict `:1275`/`:1283` 删除 entry 或退出。GPU undo/composition repack `Draw3.InkHistoryGpu.cpp:387`、`:875` 的 slice 为正或提前失败，hot-depth `:1334` 有 policy 上界。未发现正常四笔/设置往返可进入的永久 CPU 内循环。极大坐标、损坏数据和长期队列流量不因此全部排除。
- `DrawingController::Run` 除非函数调用/等待不返回，会在异常时由 Host catch 收敛；`Draw3.Host.cpp:1686`/`:1694` catch → RequestExit。随后 EndDrawingActivity、metadata、destroy controller、presentation.Shutdown、renderer.Release，完成后才 `running=false`（`:1702`–`:1709`）。
- 主线程 `IdtMain.cpp:3395`–`:3425` 每100ms 监督 ProductRunning，非运行时 SetOff、隐藏表面/UI、记录受控退出。因此“工作线程静默死了但 main 无限正常”不符合当前这条已完成的控制流。**清理卡在 DComp wait/COM/haptic Stop/窗口同步 callback 之前时 running 仍 true**，不能借此监督排除。
- Haptic GetFromPointerId、Send/StopFeedback 同步 WinRT 调用在 render owner（`Draw3.HapticFeedback.cpp:393`、`:432`、`:470`）；没有应用层循环/反向锁边，系统服务调用不返回仍是待证前提。鼠标视频没有证明进入 pointer haptic 路径。

### 7. Mutex、CV、future、join 的等待图

| 同步点 | 持锁/等待合同 | 审计结果 |
| --- | --- | --- |
| Host metadata / eraser diagnostics | `Host.cpp:194`/`:207`；短 copy/read，少量启动/收尾 adapter query | 未发现业务 callback 在这些锁中反向获取窗口 owner；不是绝对排除系统调用阻塞 |
| Host displayMutex | 发布 `:1116` 保存快照，pump `:1132` 复制后释放，再调 USER32 和 penWidthContextChanged | display listener 不直接持 displayMutex 等 Window future，未成立该简易锁环 |
| Host presentationTargetMutex → Bridge mutex → input admission / runtime revision | `Host.cpp:549`、`:2174`；ObserveWorkspace `:847` | 本范围没有找到 Bridge 持锁回调 Host 导致反向边；RTS writer-gate 由输入研究另审 |
| Runtime revision CV | `Host.h:26`；Publish 自己的 mutex 增 revision，unlock notify；WaitForChange wait_for 只查 revision/running | 没有业务回调，读取快照时拿锁意味着故障 watchdog 不能依赖完整 RuntimeSnapshot 永远返回 |
| startup CV/join | graphics/stylus handshake `Host.cpp:1581`、`:1739`、`:1783` | 正常已经画过的 session 不再等待这些 startup CV。启动异常另有 notify/join 处理 |
| content CV | observer 更新 under contentMutex 后 unlock notify；WaitForContentRevision `Host.cpp:2012` finite timeout | 非 render owner 同步等待 UI ACK 的必经链；本范围未见反向等待 |
| exitAutoSave CV / worker join | Host.Stop `:1858` 等最终捕获，service CloseAndDrain 后 render join `:1877`；只在停止入口 | 可因后台 I/O/命名锁慢阻塞退出；不能直接解释仅开关设置后正常运行的 Desktop canvas |
| Window future | Window.cpp `:1376` 外线程 enqueue promise，owner 同线程直接 Execute；无 deadline 时 `result.get()`（CommandCompletion.h:22） | 外线程无界等待是真实边界；必须找 owner 在等谁。仅指出 future.get 不是死锁证明 |
| Window deadline | 全源码实际赋值是 RequestTopmostRefresh `Window.cpp:558`；Execute `:1626` 拒绝已经过期的 command | deadline 不是对执行中的 USER32 事务取消。未找到常规 Selection/Setting 的超时后“半提交继续运行”确定链，不能泛化为所有窗口命令都有 timeout |
| Product locks | Product.cpp 的 recursive productCallMutex 序列化 forward/publish；Stop 在调用 Host.Stop 前结束 drain lock scope | 当前实现已避免 Stop 持 productCallMutex 等 render owner 的简单环；仍需现场确认其它外部 owner 同步边 |

Window/UI 的 stdout 背压与 Settings/ImGui/DXGI 等待研究由主会话和窗口研究者继续处理，本文件不重复运行。同步日志可在锁外但仍占住唯一 render owner；“锁外”不意味着“不阻塞渲染”。

### 8. History / PPT I/O 命名锁与 worker 生命周期

- Desktop 命名索引 mutex 是 normalized root + date，`Draw3.AutoSave.cpp:253`–`:269` INFINITE；Presentation 是 normalized baseRoot hash，`Draw3.PresentationAutoSave.cpp:135`–`:149` INFINITE，两者 namespace/name 不同，不能仅因同属 AutoSave 宣称互锁。
- PPT Save/Load 在单 owned worker 中串行，真正文件 I/O 之前已释放 service 的队列 mutex（`:1723`–`:1728`）。拿到 Presentation named mutex 后执行同步读/写/Flush/Replace，`:1157`、`:1482`。另一活线程/进程一直持同名 mutex，或持锁期间文件系统不返回，会让 Current load 永久 pending；死亡持有者可返回 WAIT_ABANDONED，被实现视为取得锁，`:148`。
- Desktop worker 把 Save I/O 放在队列 mutex 外（`AutoSave.cpp:1137`–`:1180`）；保存慢通常不阻止 Desktop 新笔迹，只影响保存终态/退出 drain。不要把“AutoSave worker 有 INFINITE”直接等同 Desktop render owner 被锁住。
- 共享 UInk `NamedTransactionGuard` 的等待是 **5000ms**（`uink_file.cpp:175`），不同于上述索引 INFINITE；分享冲突是有限 OpenWithRetry。未发现代码内部的 LockFileEx 等待环。把文件 share 冲突描述成永久 OS file lock 等待不准确；本次强链的持续状态来自失败分类后的逻辑闸门。
- 两个 service 的 CloseAndDrain 都在释放 service mutex 后 join（`AutoSave.cpp:1374`、`PresentationAutoSave.cpp:1919`），未见持队列锁 join 所有者本身的普通自锁。

### 9. 低优先静态缺陷：accepted 工作项失去 completion

Presentation `PushCompletion` `:1692` 在 `completions.push_back` `:1704` 分配/搬移异常时只日志并返回（`:1708`）；外层 worker 构造 completion target/payload 的 catch `:1785` 也只日志。若 accepted Current load 的唯一终态因此丢失，Controller 的 pending 不能被 `:12061` 清除；retry 在 loadPending=true 时不再提交，`:899`，Down 会持续被丢弃。

该项与普通 throwWorkerOperation 注入不同：后者位于 inner try，正确转换 IoError 并 PushCompletion，`:1755`–`:1769`；现有异常测试通过不能覆盖 enqueue 分配失败。若日后验证，须精确一次失败 completion queue 分配，记录 accepted/worker done/terminal delivered，而不是让全局 new 一直失败造成其它噪声。本轮未编写或执行该 OOM 实验，也不将其列为视频首要归因。

### 10. 条件化修复方向与可证伪标准

本轮不实现行为修复。真实 worker 分享冲突实验已验证失败分类缺口；对应方案是将“不能读取”与“已读取并证实 revision/内容改变”分别返回结构化终态，保留现有 fail-closed，走既有 IoError 有界 retry。还需决定 retry 耗尽的可见状态/显式恢复入口；不能直接把未知旧内容当作空白或盲目解除 admission。

针对用户视频仍优先取得故障同版本、同操作前后的 input/RTS、bridge/admission、最后 render phase 和窗口 owner 事实。可证伪指标如下：

| 假说 | 支持所需事实 | 明确反证 / 限制 |
| --- | --- | --- |
| PPT load 逻辑保护停滞 | workspace=Presentation；冷槽 load accepted；SourceChanged/manual_required；initialized=false/mutation=0；恢复后 same target 无新 request | Desktop/Whiteboard、始终 warm initialized、未发 load，均排除此链在该时段的解释 |
| GPU API 未返回 | 外部 dump/采样连续显示 render owner 在同一个 Present/Map/Resize/DComp wait，窗口 UI owner 继续处理；入口序号无相应出口 | 同一调用已退出且后续 render phase 前进排除该次调用，不能排除后续新调用 |
| 普通 raster 永远失败 | 精确失败站点和HRESULT连续出现，deviceReason=S_OK，CPU input/command/model持续前进但没有成功新权威帧 | 仅 present success=0/dirty空白不支持；一次失败后真实 success 排除“从该次起永远失败” |
| owner/future 等待环 | dump 有调用线程 future.get + owner 反向等待来源；至少两个连续现场证明稳定链 | 单次正常 WCT无环、仅 UI还能动均不足以排除/确认所有线程间环 |
| 已停止 render worker | Run exit/catch + cleanup phase、running=false 与主监督状态一致 | 当前 main 应退出；若 UI 长活，要进一步定位 cleanup未到runningfalse、主监督阻塞或不同版本 |

## External References

本轮没有新增外部检索或依赖外部网页结论；API 行为仅按实际调用/flags/返回处理陈述。既有诊断报告包含 Windows SDK `Wct.h`/minidump 本地工具盘点，可用于现场取证；WCT 不覆盖所有 GPU/驱动内部等待，不能以无 cycle 提供全局证明。

## Related Specs

- `.trellis/spec/native-desktop/draw3-integration.md:219`–`:230`：slot/target、保护闸门、same-session UInk 和结构化失败；`:226` 的“直到失败 completion 收敛”与当前失败后 unresolved 持续状态值得在后续决策明确解释，不应擅改恢复安全合同。
- 同文 `:417`–`:419`：DComp→ULW、每次失败资源释放、辅助 target 失败恢复。
- `.trellis/spec/native-desktop/cpp-conventions.md`、`errors-logging-and-resources.md`、`rendering-and-ui.md`：共享状态所有权、失败传播和 render/UI 边界。
- `.trellis/spec/native-desktop/build-and-compatibility.md`：既有 solution/ABI、headless 与 GUI 区别。本实验只重编研究 main，复用同工具链现有产品对象；若 IFC/obj 不兼容，不修改产品源码或依赖绕过。

## Caveats / Not Found

- 无窗口实验及 `/fp:precise` 参数对齐后的复编/重跑均已由主会话完成，退出0，本研究者直接核对日志；无产品行为修改。
- 目前最强非 OOM 源码链限于 PPT 冷恢复及 I/O占用，不对应已知 Desktop 视频；文件短暂被独占在实机发生的频率未测。
- “永久”必须附状态前提。场景/页面 revision、显式重读、workspace 往返可能恢复；该实验不是完整 Host/RTS/GUI 集成验证。
- 首轮用户操作实验使用已存在 Debug edition 20260811a，不能等同当前 HEAD edition 20261014a；本研究源码/对象哈希也须与最终主会话运行产物对应。
- 已完成静态等待/循环盘点仍不能证明没有其它 bug，尤其第三方/驱动系统调用及输入研究覆盖的 RTS writer/state 锁；没有用视频某一时刻的继续日志排除后续全部渲染路径。
- 不建议因当前发现而本轮修复：用户明确本轮仅调查定位、修复方案，允许诊断代码；研究实验符合该边界。
