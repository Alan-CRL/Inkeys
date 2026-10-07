# E02 C-P2 已知失败清理模块接线实际代码独立复审

日期：2026-09-30（Asia/Shanghai）。Active task：.trellis/tasks/09-27-integration-and-release-check。HEAD：e32a5fc06096c1e4ab88a29866c60ebe323fd722。本 reviewer 已读保存的完整 hook、真实 AGENTS/check.jsonl 及其引用、当前 PRD/design/implement、父最新 handoff/validation、C 冻结寿命合同和设计/primitive 实码 GREEN；独立核对八模块 git diff HEAD、实际调用链和 root Main span。只写本报告，不改产品/spec/工程/共享账本/Git，不构建、运行 EXE/GUI或递归派发。

## 结论

**STATIC GREEN。** 本批八源和 Main 接线符合冻结合同，未发现当前可观察的终止失败分支先日志/清理后 Begin、未 join 活 monitor 就继续下一次普通初始化、或向 owner 借用 Main scope/context 的静态阻断。八源身份与 failed-cleanup-module-implementation.md 的 PATCH_READY 表完全一致。

root 已在本批八源、Main 和 B06 都冻结后完成完整 Debug|ARM64 Solution Build，以及严格 Headless、生产 parked U1/M16、PptCOM.Tests，实际退出码均 0；本 reviewer 读取了真实 status/raw，没有自行运行。此结论只覆盖源码合同、当前 ARM64 编译/链接和既有相关回归。**C03–C11 的真实 Host/Window/RTS/presenter 停滞、完整 Main DComp→ULW 成功反例、正常 Armed render/save 停滞和 fresh UInk/index 恢复尚未运行**；成功 RTS callback quiescence、Win7 和 Release 三架构也没有由此变为 PASS。C-P1 十绿不升级为这些模块动态证据。

## Findings (fixed)

- 本 reviewer 没有产品机械修补；没有发现需要本次修改的 lint/type/import/局部逻辑问题。下面列的是实施者已经写出的真实保护点，均按实际源码核对，未以交付文案代替代码。

## 实际代码与寿命核对

### 普通头、module 与默认空调用

- FailedCleanupDeadline.h 只声明普通类型/方法，不定义 Main/业务全局或 monitor slot；实现由独立 cpp 提供。六个 module 单元的 include 都在 global module fragment、module 声明之前：Window.cppm:5 / Window.cpp:7、RealtimeStylus.cppm:8 / cpp:20、TransparentPresentation.cppm:12 / cpp:25。Host.h:6 通过普通头包含。没有把 helper 类型分别附着到三个 named module，也没有新增反向 import Main/ShutdownSupervisor 的环。
- Window 的 Start(vector<WindowSpec>, FailedCleanupSignal = {}) 与 cpp:2068/Impl:155 一致；RTS 的 Initialize(HWND, coordinator, cursorSink = nullptr, FailedCleanupSignal = {}) 与 cpp:2397 一致；HostStartOptions.failedCleanup 和 TransparentPresentationOptions.failedCleanup 都按值拥有 Signal。Draw3.Product.h/.cpp:21 仍原样把 options 传到唯一 productHost.Start，Main 使用的 API 是真实产品链。
- 普通动态 Window CreateWindowFor 的默认 Signal 为空；RecoverFromRuntimeFailure:924/940 调 TryInitialize 时仍使用空默认参数，Resize/Present 未接 startup timer。Presenter 无有效 Publisher 不 Prepare；Window optional 无有效 Publisher 直接用原创建入口。旧独立 Host/RTS 调用没有默认 monitor/timer。RTS 旧空 Signal 的停止行为保持原有路径，产品 fail-closed 的声明限于 Main 传入有效 Signal 的调用。
- 主项目及 Headless 各登记 FailedCleanupDeadline.cpp 一次；Headless 只需 helper，不拉入 Main/ShutdownSupervisor。Headless 原来没有 vcxproj.filters 文件，不据此要求新增。当前完整 Solution 的 module compile/link 已通过；其它架构仍需独立编译门。

### State/Signal 跨 owner 和迟到调用

- 新 helper 的两份 SHA 与 C-P1 实码 GREEN 相同。Signal、scope、monitor envelope/入口都持同一固定 State 强引用，State 不存业务对象、Main 栈地址、Window/Host 指针或借用 callback context。BeginKnownFailure 首句复制局部强引用，覆盖 CAS 到 SetEvent 的间隔。
- Window 两个 jthread 的 lambda、Host drawing lambda [this, failedCleanup]、RTS Impl 和 Presenter options 都按值保活 Signal；scope 仍由 Main或局部 episode 的管理 owner 串行 Prepare/Complete。模块没有擅自 close wake/monitor HANDLE。
- Cancelled State 的旧 Signal.Begin 返回 0；wake 由最后引用关闭，monitor HANDLE 只在真 WAIT_OBJECT_0 后关闭。旧 Signal.HasPublisher 仍为 true 是有意保留进程寿命 publisher，供后来明确 RTS 停止证明失败使用；它不复活已取消的 startup timer。expiry/join/prepare/producer-stop 接管均 noreturn，调用栈与仍活资源保留至进程死亡。取消/过期的唯一 CAS 与更早普通截止取 min 仍是已审 helper 实现，没有被模块接线绕过。

### Window 的 promise 前失败与 optional episode

- Window.cpp:164–210 把 ResetState、promise/future 和两次 jthread 构造放入现有 Start 的窄 try。部分启动异常及 false ready 都先 Begin，再 StopUnlocked 的 request_stop、owner join、pending settlement 和 event 关闭。StopUnlocked:706–708 同时检查两个 event，promise 创建异常时不会因尚未 running/无 thread 而漏掉已建立的 event。
- CreateWindowFor:963 的 beforeCreate throw 立即 Begin；beforeCreate false、RegisterClass 失败、Bind false、created throw 都先进入 RollbackCreation:1148 的 Begin，之后才 Unbind/DestroyWindow/channel/lifecycle 清理。CreateWindowEx false:1044 先锁存原错误，再 Begin、诊断与 rollback。required 缺 owner/创建 false、RunGroup group false/catch 都在 promise/DestroyGroup/CoUninitialize 前 Begin；catch 置 created=false，避免误进成功消息循环。
- CreateStartupWindowFor:929–947 只给确实进入创建的 optional role 准备局部 Dormant scope。失败 rollback 已先激活 local；成功或可恢复 false 都在继续下一 role 前 Complete 真 join。missing optional owner 无资源创建，不 prepare timer。未处理异常会把 local 原 tick 交给 outer，再重抛；局部析构完成 monitor，RunGroup 的 DestroyGroup 继续受 outer 同 tick 保护。
- CreateEvent 返回空时仍是既有 100ms 消息轮询路径（RunGroup:790–815），没有把它判为终止初始化失败；不将此兼容 fallback 误列为“已知终止失败漏 Begin”。普通成功消息 loop 的退出/正常 join 不新激活 startup Signal。

### Host false 返回前的 owner 清理

- Host.cpp:1057 在 AttachExternal false 后、复位附着字段前 Begin。graphics bool false:1100、Presenter false:1125、首帧结果 false:1184、两个初始化 catch:1194/1201 和共同 !initialized:1206 均先激活，再失败日志、握手发布和 EndDrawingActivity/controller/presenter/renderer/device 释放。
- drawing jthread 构造失败:1248–1263 在排空两个保存 worker、解除输入/窗口附着之前 Begin；没有 detach 活 owner。graphics-failed:1270 与 startup-failed:1315 在各自 join/drain/detach 前继续同 Signal。RTS false:1291/catch:1298 早于 stylusDecision/notify；不会只等 Main 收到 false 才开始保护内部清理。
- Host 初始 DWM/运行中/无效 HWND 等拒绝没有新 owner 清理；Main false 分支幂等 Begin 覆盖随后动作。AutoSave Start 的可选失败原本允许继续 Host 初始化，不升级为终止启动失败。正常 Run、运行期 catch 和 Stop 的保存屏障/drain/join 顺序保留；不因内部无限 wait 一律报告死锁，也没有新增丢保存请求的短超时。

### RTS 已知停止失败不释放

- RTS.cpp:2405 保存 owning Signal；CoInitialize/CoCreate、HWND/all-tablets、最终 packet description、多点配置、plugin allocation/marshaler/Add/Enable 的终止失败都先 Begin，再 Log/trace/Release/Shutdown/清 coordinator。
- extended packet description 的一次失败仍允许 required description 重试；flicks/query/验证诊断沿原语义，不给后续正常初始化加旧 timer。这些可恢复诊断不是本次终止失败 hook。
- Shutdown:2653–2654 在 pluginAdded=true、有效 Publisher、Disable FAILED 时先 FailUnprovenProducerStop，早于失败 Log 和 Remove。Remove FAILED:2661–2662 同样先 noreturn，早于 Log、removedPlugin->Release、pluginAdded=false、CloseAllProducerContacts:2668、plugin/stylus Reset 和 coordinator 清空。即使 Enable 失败、initialized=false，pluginAdded 已为 true，仍走该检查。
- removedPlugin 是 noreturn 栈上的 raw 临时强引用，没有在停止失败接管前释放；原 plugin/stylus/coordinator/Host/HWND 也没有由该失败分支继续拆除。Shutdown 最开始仅关闭 diagnosticsPlugin 的非 owning 诊断借用，不等于释放 producer 或清 coordinator。

### Presenter 局部模式失败和后续普通初始化

- 每个 startup mode 的 TryInitialize 前 Prepare 一个独立 Dormant releaseCleanup；有效 Signal 才创建 monitor。TryInitialize:695–750 的 disabled/API unavailable、ConfigureWindow/CreateSwapChain/renderer.Init/InitializePresenter false 都先 local Begin，再本层失败 cout/return；API 错误快照在 Begin 前保存。
- requireMode 和最后 mode 的 outerSignal 使用同一个 local 原绝对 tick；中间失败 outer 为空。ReleaseAttempt:837 及 fallback cout:841–842 保持在 local Armed 期间；Complete:854 真 join 后才循环到下一普通 mode。成功 mode 也 Complete Dormant。意外异常:849 先让 outer 继承 local tick 再传播，异常栈离开局部 scope 时仍执行 Complete。
- “All modes failed”cout:867 前 outer 幂等 Begin；下一层 Host 清理继续同 episode。Recover/Resize/Present 不继承这个 startup timer。DComp→ULW 顺序、FLIP_SEQUENTIAL:647、Hardware→WARP/11.1→11.0 和两个 DWM 禁用 gate 保留，未引入 bitblt 或设备/渲染线程合并。

### Main 首失败 span 与下一代

- Main.cpp:2557–2567 的 StartWindowService 局部 scope 在实际 Start 前 Prepare，在模块 owner 清理及 monitor 真 join 后返回；每次调用都新建 Dormant scope。false 后的 D101 提示/Window 清理仍先走既有 SetOffSignal。
- firstHostCleanup:2630–2647 在实际 StartProduct 前 Prepare；Host false 后没有提前取消。Main 幂等 Begin 在 warn logger 前，scope 继续跨 StopProduct 和旧 Window.StopAndJoin，直到二者完成才 Complete。
- Complete 正常返回后，2654 先查 offSignalInterop 仍为空，再调用新的 StartWindowService；新 ULW Host scope:2656–2666 单独 Prepare，不继承第一次 tick。首 Host 成功、非 DComp false 和 ULW false 也都有相应 Complete；fatal 提示前正式 SetOffSignal 保留。旧 Signal 可仍被 Host/RTS/options 持有，但只指旧 Cancelled State。
- PublishFatalFailedCleanupNoWait:300–309 仍只有意图 CAS、已构造 Window Service 的原子 BeginShutdown、普通意图 offSignal、WakeForStop、published deadline read。没有 Arm、日志、隐藏队列、业务锁、COM、新进程/线程；不夺已有 Restart/UEF 意图。其函数和 getter 的实际签名与 helper 一致。

## Findings (not fixed)

1. **冻结范围内明确保留的低层未覆盖。** CreateSwapChain:653–657 在 helper bool 返回前仍先 LogHResult，InitializeGraphicsDevice 的 Hardware/WARP 最终失败日志也早于 Host:1100，首次 ClearCanvas 内的 Present 失败 cout:1005–1008 也早于 Host:1184。选定 catch 前的异常栈展开、内层 COM 释放/驱动回滚、尚未返回的 OS/COM/renderer/API 和普通 cold start 同样不受新 grace 保证。这里已直接核到真实调用顺序，但冻结合同明确以外层可观察 bool/catch 为最小触点，不是本批所承诺的本层终止分支先日志漏 Begin；未扩写每个 HRESULT 的 timer。最终报告不得写成“所有 API 首个错误起均有 30 秒上限”或内核硬实时保证。没有现场栈，不称它们已复现为用户卡死。
2. **RTS 成功停止的证明缺口。** FAILED 分支已 fail-closed；Disable/Remove 的 SUCCEEDED 仍沿原 CloseAll/Release 时序，没有新增完整 callback in-flight fence。现有 reader/writer gate 和 watchdog 都不能证明所有 callback 静止，也不保证其它窗口 producer 已排空。真实 provider/callback 交错需 E03/C04 等独立证据，不能仅 HRESULT、进程最终死亡或 Build0 就宣布 Reset 安全。没有凭此改 COM/packet 同步体系。
3. **真实模块验收尚缺。** 当前 --failed-cleanup-only 仍只有 C-P1 十种 primitive；没有 C03–C11 模块夹具/结果。需成对运行 Host/Window/presenter failure hold 与放行、完整 Main DComp→ULW（旧 HWND 失效、唯一新 generation、真实成功 Present、超过旧 grace 后还能继续一笔且未占 Close），以及原 Armed render/Desktop delay/PPT UInk-index 窗口停滞和 fresh 生产 Load。新测试门须先冻结 capability/鉴权/借用 HANDLE 寿命并独立 safety。B002 Natural/Hold 若复验通过，只补当前 Window/Host 成功启动后普通/fatal 清理，不替代失败 hold、ULW 回退或 C04 静止证明。
4. **平台与性能门尚缺。** 本批仅当前 Debug ARM64 完整编译/链接。Release ARM64/Win32/x64、x86/x64 导入与真 Win7 SP1 仅 KB2670838 的 HARDWARE/WARP、ULW FLIP/透明/resize/device-lost/输入仍独立待验。成功启动新增 monitor 创建/唤醒/join 成本存在，尚未有冷启动成本或完整 UI3/Draw3 采样；未声明零开销、性能改善、输入/sample correctness 或用户现场根因闭环。
5. **Spec 同步由 root 所有。** errors-logging-and-resources.md 尚把内部 failed-start cleanup 写作未批准/待验证独立合同；接线后应同步 Dormant/已知失败、local optional/mode episode、真 join/noreturn、更早普通 tick、RTS FAILED fail-closed 和上述剩余边界。draw3-integration/input-and-ink 应保持成功 callback 静止未证实及新失败顺序的区别。未因 STATIC GREEN 覆盖旧失败证据、改任务完成度/HF或发布门禁。

以上没有需要本次修补的已确认 C-P2 源码阻断；不固定这些项的原因分别是既定最小范围、独立动态证据、尚未实施测试、平台/性能验证与 root 文档所有权。

## 源码身份

八源总 diff 为 273 insertions / 77 deletions；scoped git diff --check HEAD 真实 exit0。全部严格 UTF-8、原 BOM 状态、纯 CRLF（lone CR/LF=0），没有全文件格式改写。

| 文件（Inkeys/Inkeys/ 下） | Bytes / BOM / CRLF | SHA-256 |
| --- | --- | --- |
| Window/Window.cppm | 7236 / 无 / 213 | AA08A3555FCECBC6EC24BE40D41ACD539444A94F71465202A180B727B4CE2101 |
| Window/Window.cpp | 74609 / 无 / 2239 | 43A242A00B37B98F1AFE29B91DE1A76A40B7F2CA652E255E5C7245A838E94FD3 |
| Drawing/Draw3/Draw3.Host.h | 9824 / 无 / 285 | C2EE47AF7198B26BA4BA8BFCC782300E46D1539E0180F1031083674F8CEF9B2E |
| Drawing/Draw3/Draw3.Host.cpp | 83125 / 无 / 1716 | 60E84B339CB16B8A17AA72A6A62CC83E02A15CAE5513E3292ADFE9C0617FC595 |
| Drawing/Draw3/Draw3.RealtimeStylus.cppm | 4558 / 有 / 120 | 567C222DAAB18B05A1F43DB420464F2CD67AFC0624C82C1D3EB20A0775EB055E |
| Drawing/Draw3/Draw3.RealtimeStylus.cpp | 109871 / 有 / 2752 | 895F98BDAA19847C608B859BA57A3F1D7F6C12ED0F2F2196F70C9ADF7CFFAC07 |
| Drawing/Draw3/Draw3.TransparentPresentation.cppm | 4802 / 有 / 125 | AC6EEFFF0770E5E86CAB02A4A7B664F39256833DC750201EA9F355FE6BD085C7 |
| Drawing/Draw3/Draw3.TransparentPresentation.cpp | 46204 / 有 / 1197 | 091C68ABA549A98F9CB0913CCA93DAE4CAA162ACA87813FF3AADA47C7B75867B |

Main 当前 SHA-256：9C02C10B3A114C3AC187F9BF1B8921D768561CFEA040E806F0FB664F1B3DCE67（113607 B、UTF-8 BOM、2978 CRLF）。ShutdownSupervisor.cpp 仍 D7E6B60E494339FCC31C58B3B45E1044604F0F2CD9F7B21159913108E6CB11D9；helper h/cpp 仍 1D8DB0010A388CC21522F1D0F9CC7858CB8D59DFCB928431C6C3112A94AAD0A0 / 95932CE0ECBCEE120F884FD834A9A72E3A85479A296420CD6D87BF9239F37F4C。合同仍 4A7EDE9C5C44AEF068F892BDC489B3203D270D1A10A8288CCEA577D16CCBF683。

## Verification

- **Lint/静态格式：** scoped diff-check PASS（exit0）及 UTF-8/BOM/CRLF 检查 PASS。本 C++ Solution 没有本轮单独执行的 linter；按只读分工不新增运行工具。
- **TypeCheck/Build：** root 的完整 InkeysRepo.sln Debug|ARM64 native MSBuild /m:1 PASS。c-p2-b06-green-debug-arm64-build.status.txt 为 exit0；log 明示 Debug|ARM64、PptCOM→Inkeys 依赖、Build succeeded、0 Error / 136 Warning、71.00s。没有以 EXE 存在代替构建退出码。日志的既有转换/第三方与 Headless LNK4075 warning 不作为本次无关清理任务。
- **既有 Tests：** root 顺序执行 c-p2-b06-green-debug-arm64-headless（严格 --no-window，status exit0 pid32328、216 layouts failures0/PASS animation）、parked（--draw3-parked-desktop-exit-test，exit0 pid15540；生产 Exit/Closing/ActiveInk/Laser/InitRejection/Session PASS）、c-p2-b06-green-pptcom（PptCOM.Tests，exit0 pid12112、ownership/session PASS）。Headless/PptCOM stderr 空；parked stderr 为预期 AutoSave capture 诊断，没有按空 stderr 假报它。
- **新模块动态 Tests / EXE / GUI：** 本 reviewer未运行；C03–C11/完整成功 ULW/RTS callback/Armed save/UInk fresh Load 未验证。B002 当前 span 回归在 root 后续计划中，本报告不预记 PASS。
- **写入边界：** 仅本研究报告；不改变源码、Spec、项目、共享账本、Git 索引/refs或任务状态。STATIC GREEN 不是整个 G/E02/发布门结束。