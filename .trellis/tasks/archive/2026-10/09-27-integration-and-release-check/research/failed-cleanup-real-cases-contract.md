# Research: C03–C11 真实模块最小隔离夹具合同

- Query: 将 C-P2 已审接线变成可执行的真实 Window/Host/存储故障验收，首批冻结 C03/C05/C07/C08/C09/C10，并明确 C04/C06/C11 的范围。
- Scope: internal；只研究当前集成源码、指定任务材料与父最新 handoff。唯一写入本文件；未修改源码、spec、项目或其他任务材料，未执行 Git、构建、EXE 或 GUI。
- Date: 2026-09-30（Asia/Shanghai）。
- Active task: .trellis/tasks/09-27-integration-and-release-check。
- Status: DESIGN PROPOSAL / NOT IMPLEMENTED / NOT RUN。下列新 purpose、selector、类型及插点均需独立设计审查和运行前安全审查；本文没有新增动态 PASS。

## Findings

### 1. 采用的冻结依据及当前准确状态

已读 AGENTS.md、当前任务 prd/design/implement、父 handoff 顶部 C-P2/B06 检查点，以及 failed-cleanup-lifetime-contract、lifetime-design-review、code-review、module-implementation、module-code-review。寿命合同冻结 SHA-256 为 4A7EDE9C5C44AEF068F892BDC489B3203D270D1A10A8288CCEA577D16CCBF683；设计 GREEN、P1 实码 GREEN、P2 STATIC GREEN 的含义分别保留。父 handoff.md:3–9 和 module-code-review.md:7–9 明确新真实用例未运行。

现有 --shutdown-supervisor-tests --failed-cleanup-only 及 96B --inkeys-internal-failed-cleanup-child-v1 只覆盖十个 primitive，实际入口在 ShutdownSupervisor.cpp:1689–2031,2247–2253。它永不进入正常 wWinMain，不能填写 C03–C11 的结果。当前 Host false、Window rollback、RTS fail-closed 接线是实码，下面的持久化读者与事件增量还不是实码。

首批只选一个真实 Host presenter 拒绝实现 C03；不再模拟 InitializeGraphicsDevice 返回 false，不把强制 DWM 的入口早拒绝算 C03。C04 的合成 HRESULT、真实 provider 停止成功静止和 C06 的旧 Window owner 单独停滞属于下一组。C11 由首批各 release/no-fault 对照承担，须有独立结果标签。

### 2. 两个先决修正：Begin 唤醒顺序和 Desktop fresh 读者

**Begin 门当前是 pre-wake。** FailedCleanupDeadline.cpp:187–205 在 control CAS 后先 WaitTestGate，再 SetEvent(state->wake)。永久停在现 afterBeginClaimedEvent/continueBeginEvent，monitor 可能仍按 Dormant 在 INFINITE 等待；仅看到 CAS 成功不能证明它已经守截止。不得由夹具额外 SetOffSignal 或到期 Complete 来制造 C03/C05 的绿色。

root 已在本次研究消息中确认如下最小测试增量：FailedCleanupTestGates 在现两个 bool 后新增 plain bool beginGateAfterWake（所有调用继续 gates{} 聚合零初始化，不加成员默认 initializer），State 仍按值保存整份 gates，复用原事件对。false 保留原 CAS→门→SetEvent；true 改为 CAS→SetEvent→同一门。SetEvent 失败继续原 fatal 管理失败，不能进入假门。只新 exact-auth integration child 的 scope 设 true，产品默认和旧 C00 全部 false。没有新增模块 callback 框架、正常读钟或普通 cold-start timer。

~~~cpp
// 位于 BeginKnownFailure 成功 CAS 后；原 false 分支顺序不变。
if (!state->testGates.beginGateAfterWake)
    WaitTestGate(*state, afterBeginClaimedEvent, continueBeginEvent);
if (!SetEvent(state->wake)) FatalNow(/* 原参数与退出码 */);
if (state->testGates.beginGateAfterWake)
    WaitTestGate(*state, afterBeginClaimedEvent, continueBeginEvent);
~~~

新增 bool 使用现 padding：当前是两个bool和六个HANDLE，按默认布局，Win32 gates 为 28B、首 HANDLE offset 4；x64/ARM64 为 56B、首 HANDLE offset 8，新增第三个bool不改变这些size/offset。三个架构均需 sizeof/offsetof 编译断言，并重跑原 begin-cancel、expiry-cancel、cancel、dormant 和十 primitive 总组；不能只由 ARM64 外推。原 pre-wake C00 用例仍覆盖 CAS 后、SetEvent 前的旧 wake 保活交错。

**Desktop fresh Start + SubmitLoad(fileGuid) 当前不可用。** AutoSave.cpp:1073 清空 records；1123–1145 的 SubmitLoad 只查本实例 Committed records，不扫描磁盘。不能创建 fresh service 后伪造 Committed record、先重写同一文件，或把 Invalid 当恢复成功。

按 root 的本次澄清，本任务只新增 AutoSave 现 module 内的隔离 fixture 专用严格读者：ReadLastCommittedDesktopAutoSaveFixture(ownedRoot, localDate)。普通业务不增加 indexed-load/cold-recovery 入口，不开放 NotReady 或放宽首次门禁。读者复用本 module 的 ReadIndex/ValidateIndex（AutoSave.cpp:373–454）、既有 primary/backup 选择规则和 worker 的生产 UInk 读入/导入/interval 投影（926–959）；不在新 fixture.cpp 复制索引算法。最小提取一个 private ReadCommittedDesktopPath，由原 WorkerMain 和这个窄读者共同调用，原 SubmitLoad 的行为保持原样。

读者仅返回 owned value：date、dailySequence、sessionId、sequenceInSession、fileGuid、实际相对路径、严格加载后的 Draw3UInkExportSnapshot 和 sourceRevision。没有 Document/Controller/HWND/worker/借用指针，没有写盘、records 注入或业务 ready 发布。接口注释限定显式隔离 CLI；唯一新增调用来自已鉴权 fixture。主 module 不反向依赖 Main/ShutdownSupervisor；module 内 receipt 类型由 fixture 转为下面普通 POD，不把 named-module 类型复制进普通头。

选择索引必须在同一 root/date NamedMutexGuard 下进行：有效 primary 优先，否则仅用有效 backup；两者缺失返回 NotFound，损坏无有效 backup 返回失败，绝不 NewIndex。用 ValidateIndex 后最高 dailySequence 的唯一 entry 得到路径；只读索引引用的文件，完整解码、不接受 contentSequenceRecovered/invalidCompleteBlocks、核 GUID 后作现 interval 投影。C09 预期 primary 仍有效且精确未变；使用 backup 的读者能力不是本例 primary 成功的替代证据。

### 3. 新独立 purpose 和有限 case 集

提案新增 --inkeys-internal-failed-cleanup-real-child-v1；公开父 selector 提案为 --shutdown-supervisor-tests --failed-cleanup-real-only <case>。现源码尚不支持它们，不能先运行或写成已支持。旧 48B E01、128B startup/UEF、96B C primitive magic/version/bytes/解析器完全保持。

新有限 enum CleanupRealCase : DWORD，0 无效；只接收以下 1–17，不接收任意故障码、环境开关、脚本或外部路径：

| 值 | selector | 目的 |
| --- | --- | --- |
| 1/2 | C03-presenter-hold / C03-presenter-release | 真实 ULW presenter style 拒绝，outer Begin 后停住/放行 |
| 3/4 | C05-before-hold / C05-before-release | required Drawpad.beforeCreate=false |
| 5/6 | C05-created-hold / C05-created-release | required Drawpad.created 抛出，真实 rollback |
| 7 | C07-main-ulw | 同一 Main 生产回退 span，首 StartProduct=false 后新链 ULW 成功 |
| 8/9 | C08-render-hold / C08-render-release | 真非首成功 Present 后 render owner 停住/放行 |
| 10/11 | C09-desktop-hold / C09-desktop-release | 第二 Desktop Save 到真实 writeDelay / 短 delay 对照 |
| 12/13 | C10-ppt-hold / C10-ppt-release | 新 UInk durable、index 未发布 / 放行完整提交 |
| 14 | C11-natural-close | 无 fault 的真实 Host、两次 Desktop Clear Save、正式 Close |
| 15 | C09-read-committed | 新 child 中调用上述 Desktop 严格 fixture 读者 |
| 16/17 | C10-read-committed / C10-read-foreign-session | 新 service 严格 Load / foreign session 拒绝对照 |

C04/C06 没有本版本枚举值；下一组不能借任意数值进入，须另审有限扩展。省略 selector 的新父 CLI 是否逐项跑由 root 实施时决定；首批验收命令固定逐 case，避免误跑未冻结场景。

### 4. 1024B POD、句柄信封与文件根

root 新普通 shared header 建议为 Inkeys/Inkeys/Helper/FailedCleanupRealCases.h，包含有限 enum、POD、VerifiedCleanupRealLaunch 和普通 fixture entry 声明；只 root 写。新包 magic=0x1430FA04、version=1、bytes=1024，与现 0x1430FA03/96B 分开。全部零初始化，alignas(8)，没有 HANDLE、指针、size_t、bool、std::atomic、std::string、容器或 module 类型。

| 区块 | offset / bytes | 精确布局 |
| --- | --- | --- |
| Header | 0 / 64 | DWORD magic/version/bytes/case；DWORD issuerParentPid/originChildPid/expectedIntent/reservedFlags；BYTE nonce[16]；volatile LONG authorized/result/stage/error |
| Trace | 64 / 256 | 下述 24 个 ULONGLONG=192B；ULONGLONG oldGeneration/newGeneration；DWORD ownerThreadIds[4]；volatile LONG faultReached/startReturned/stopReturned/oldSignalCancelled/oldChainDestroyed/intentObserved/armState/readerSucceeded |
| expected | 320 / 352 | 仅 parent 填的 immutable Receipt；生产 child 全零，reader child 从已死亡生产 child 的完整 observed 复制 |
| observed | 672 / 352 | child 输出 Receipt；按case先完整填值再 Interlocked 发布 BaselineReady/Finished/ReaderDone |

Trace 的 24 个 ULONGLONG 按顺序为 childStartTick、failureTick、gateTick、graceDeadline、closeRequestTick、ordinaryDeadline、fatalPublishTick、firstStartFalseTick、oldJoinTick、firstSuccessTick、postGraceStrokeTick、stopEnterTick、stopReturnTick、oldDrawpad、oldPresentation、newDrawpad、newPresentation、baselineSuccessPresents、gateSuccessPresents、afterGraceSuccessPresents、saveAccepted、saveCommitted、saveFailed、savePending。数值 HWND 仅是观测，不授予对旧窗操作的权利。generation 是 created/destroyed 生命周期计数；HWND 数值可被重用，不能用 oldHandle!=newHandle 代替销毁和新创建证据。

Receipt 的固定顺序：BYTE fileGuid/workspaceGuid/pageGuid/presentationKey 各[16]（64B）；8 个 ULONGLONG mutationRevision/bindingRevision/targetRevision/sessionRevision/sequenceInSession/dailySequence/intervalOrdinal/uinkLength（64B）；8 个 DWORD workspaceType/pageIndex/totalPages/bindingMode/pageKind/processLocalIdentity/strokeCount/pointCount（32B）；LONG slideIds[4]（16B）；char storageSession[40]、localDate[16]（56B）；BYTE indexSha256/uinkSha256/geometrySha256 各[32]（96B）；DWORD deviceCount/activeCanvasCount/retainedCanvasCount/desktopTrigger（16B）；BYTE reserved[8]。合计 352B，8 对齐。未用字段必须零，字符串必须在容量内 canonical ASCII、NUL 终止、尾部零；不得截断。首批只用两张 stable slide 和一个独立 EndScreen，拓扑放得下，未表示的 Canvas 身份也进入 geometry digest。Desktop trigger按现Clear/Exit枚举编码，PPT该字段零；所有extra进入下述digest。

geometrySha256固定为一个fixture value编码的SHA256：按snapshot原顺序编码workspace/file GUID、workspaceType/currentPageIndex/dpiScale/viewport、devices和active/retained/canvases，所有Canvas GUID/index/number/SlideID/intervalOrdinal/retained/extra，以及完整operations/strokes的kind/RGB/opacity/texture/undoId/renderOnlyWhenLatest和全部x/y/width。数值固定little-endian，浮点bit_cast为IEEE位，optional先1B存在tag，字符串/vector先DWORD长度；不排序、不取样、不只算端点，NaN/超限/转换溢出直接失败。UInkExtra 使用实际递归 UInkMessagePackValue::Map；visitor 每值带确定 variant tag，覆盖 monostate/bool/int64/uint64/float/double/string/bytes/Array/Map/Extension，Map 按原顺序编码 key/value，Extension 编码 type+payload。深度上限32，长度/计数溢出与非finite拒绝；不排序、不JSON化、不把extra当普通字符串。codec投影后baseline与reader使用同一纯值编码函数；原index按完整字节hash，UInk摘要优先取实际ReadUInkFile.sourceRevision.sha256/length。实际bindingToken/sourceIdentity从verified run、origin PID/旧数值HWND和固定descriptor重构并逐值比较，不能用摘要代替该target匹配。

Receipt的PPT pageIndex/totalPages/bindingMode/pageKind来自实际Load completion.target，而snapshot自己的currentPageIndex保留在geometry编码；Desktop这组分别是当前单页/1和未用零值。PPT各revision取真实completion/target/index，不从期望值回填。所有case的expectedIntent固定普通Close值1，仅作为验收期望，不授权reader发布Close。

三架构 static_assert：sizeof(Receipt)==352、sizeof(Trace)==256、sizeof(Packet)==1024、上述四 offset 和 offsetof(Header,authorized)==48；以 SDK 定义的 DWORD/LONG 和 ULONGLONG 为准。禁止 #pragma pack(1) 规避对齐。普通文件中的 POD 不得附着到两个不同 named module。

新 purpose 仍 argc==9：purpose、父 PID、父 HANDLE、ack HANDLE、私有 run 绝对目录、finite case、expected parent image 绝对路径、mapping HANDLE。复用 RunCleanupPrimitiveChild:1725–1753 的强度：三值非零且 <=UINTPTR_MAX、三个 HANDLE_FLAG_INHERIT、GetProcessId(parent)==argv PID 且非自身；CurrentImagePath/ProcessImagePath；HasExactFailedArmChildIdentity:1031–1056 的父镜像文件 volume/file index、ownImage==私有非 reparse bin/Inkeys.exe 文件 identity；MapViewOfFile 只能新 1024B，精确 magic/version/bytes/case、issuerParentPid 与父 HANDLE PID 一致、所有 reserved/status/trace/output 初始零。reader 才允许 expected 非零和 originChildPid 非零，生产 case 这两个区域必须零。

父进程仍 PROC_THREAD_ATTRIBUTE_HANDLE_LIST 恰好三个继承对象：父进程查询/同步 HANDLE、ack 的 EVENT_MODIFY_STATE HANDLE、mapping 的 FILE_MAP_WRITE HANDLE（现 1912–1927）。不新增 named events、继承 gate、任意 process/window handles 或按名称寻进程。reader 的 originChildPid 是 receipt 数据，父已持有并确认原 child HANDLE signaled；reader 不 OpenProcess(originPid)。不能把 PID 字段升级为权限。

仍复用 MakeUefTestDirectory:917–954，在仓库 TestResults/release-hardening 创建唯一 private run 和 bin/copied EXE。新增文件根仅为 <run>/artifacts/C09/AutoSave 与 <run>/artifacts/C10/AutoSave；逐层创建新目录并拒绝 reparse。C10 sourceIdentity 由 <run>/artifacts/C10/fixture-source.pptx 这一固定绝对名字经现 ResolvePresentationTarget 计算；不打开这个文稿、不接受 packet 中外部 path。所有输入/输出文件路径均由验真 run + 常量/生产已验证 relativePath 推导。父保留 copied EXE 到两阶段 reader 完成，不照抄 primitive 在生产 child 死后立即 DeleteFile 的时机。保存证据及孤儿版本保留；不递归删目录、未知文件或真实配置。

Supervisor early 识别新 purpose 和新 selector；CommandLineToArgvW 失败但包含该 purpose 仍 fail closed，未知/坏参数返回错误，永不落普通 Main。完整鉴权前不得创建 HWND、fault setter、gate、COM/RTS/设备或 artifact 文件。允许 parent 先创建自己的 run/bin/mapping。nonce必须非零；issuerParentPid/finite intent匹配，reader输入receipt另验GUID、日期、字符串尾零、字段范围及全部reserved零。ack 仅表示 authorization，不能作为 fault reached。

stage为单调有限DWORD语义、以LONG存储：0 None、1 Authorized、2 WindowReady、3 HostReady、4 BaselineReady、5 FaultReached、6 ClosePublished、7 StopReturned、8 ReaderDone、9 Finished、10 PrerequisiteFailed；reader走1→8→9，case允许跳过不适用的阶段。result为0 Pending、1 Passed、2 Failed。error0表示无夹具错误；坏argc/case=81、数值=82、HANDLE/mapping/POD=83、exact image=84、ack失败=85沿用拒绝原因分类，新fixture前提/线程/文件/receipt失败=90，C07缺实际DComp前提=91。接口拒绝码不称产品退出码。held child不能发布Passed；父根据partial receipt与精确自然deadline死亡独立判定。stage由coordinator汇总单writer，producer只写自己字段并用Interlocked发布reached；observed Receipt 按 case 只封口一次：hold 的 A 在 BaselineReady 发布；release/C11 的 A 只存在 fixture 私有纯值中，最终 B 只在 Finished 发布；reader 在 ReaderDone 发布。已封口 observed 不再覆盖，parent 仅在对应 acquire stage 或死亡之后读取。新负例至少wrong-parent/no-inherit/旧96B-or-bad-magic/非法case，必须authorized=0、无窗口/fault/artifact/退场意图。

### 5. 借用寿命和通用执行骨架

每个生产 child 建立 heap FixtureChildState，持有 mapping view 的使用期、事件、原/新 HWND 记录、style/生命周期计数、线程 HANDLE 和 immutable capability。WindowSpec lambda 按值捕获该 state 的强引用；HostStyleCallbacks/startupContext 指向它的成员，runner 和每个 Win32 thread envelope 持有强引用。State 不反过来拥有 product Host，不形成 owner 环。publisher 使用鉴权后一次绑定的进程寿命 observation 指针，只调用真实 PublishFatalFailedCleanupNoWait，再原子发布 tick；无 logger、堆、文件、业务锁或捕获栈。

root在Supervisor新增有限的RunAuthorizedCleanupClose(verified-launch)接线供child close thread调用：case必须是7–14（7仅C07最终普通Close，原8–14 render/save不变），记录request tick，调用真正SetOffSignal(1)，然后只读原PublishedShutdownDeadlineTick与本cpp的g_armState填Trace并发出close-ready。它不直接ArmCore、不注入ordinary失败、不改变deadline或调用另一套退出函数。若SetOff内部Failed-only接管noreturn，缺少armState2/4的receipt仍使本普通Armed用例FAIL。Fatal publisher和ordinary close见证使用不同字段，不把后者当grace接管证据。

 scope 只由一个 management owner Prepare/Complete；跨 owner 只传 owning Signal。事件先建立再 Prepare scope。进入 hold 后 runner 不返回，不析构任何 Host、callback context、mapping 或 gate；产品强退由 OS 回收。release/no-fault 必须先真实 Start/Stop 返回、Window.StopAndJoin 和 scope Complete 真 join，再等所有 controller/close threads HANDLE signaled，最后才能关事件/视图/清静态 observation。join 失败以 fixture error 退出，不能关闭仍活句柄后继续下一 case；也不把 fixture 自终止码算产品截止成功。

每个 child 仅绑定一个 GetService()/ProductHost() 和自有屏幕外窗口链。参考 HiddenWindowTest.cpp:95–115,2491–2619，最小四角色为 dummy MagnifierHost、Freeze、DrawpadPresentation、Drawpad；x/y=-32000、visible=false、WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW，辅助固定 layered/transparent，主窗不透明穿透。dummy MagnifierHost 用 DefWindowProcW 和有限纯值生命周期回调，不调用真实 magnifier 初始化。没有 Setting、Bar、PageControl、Office、配置、显示/全局键鼠脚本。所有输入只向本代 GetWindowThreadProcessId 验证 PID==本 child 的主 Drawpad 发 kDraw3HiddenTestContactMessage。

trajectory 固定 Tool::Pen、selectionMode=false、声明的 RGB/width，真实 Down→每个 Move consumed→Up→Stored 内容/成功呈现。复用 Host.h:115–129 的 private message 和 enableHiddenTestContactInjection，显式 hidden contact 走真实 WindowController/coordinator/controller。不得用 SendInput/全局 capture/ComputerUse；消息发布成功只算 ingress，不等于 stored 或成功 Present。验收使用 inputDown/Move/TerminalPublished 的本轮差分、pen inputSequence/strokeId、completedStrokeKind、contentRevision 与成功呈现版本；不从旧非零计数推本轮成功。

为 C09/C10 基线加一个只在 hidden injection 已启用时可读的 HostHiddenPersistenceSnapshot()/普通 POD getter，包含 Desktop accepted/committed/failed/pending 和 Presentation accepted/committed/loaded/failed。实现只在显式 getter 调用时读现两个 service.Diagnostics；普通 RuntimeSnapshot/帧无新锁或读钟。getter的许可读取该generation不可变的startOptions.enableHiddenTestContactInjection，fixture只在Start返回之后且不并行下一Start时调用，不能新增对Stop会写的普通bool的无锁读。PPT accepted 包含 Load（PresentationAutoSave.cpp:1504），不能把 accepted 差分精确称 Save 数。getter 无效时返回 nullopt，不能返回伪零 baseline。该 getter只给新 fixture 调用，不提供业务 Save/Load/ready 命令。

### 6. C03：真实 presenter 拒绝及 Host 失败 owner

先真实 Window.Start 并 Complete Dormant Window scope，验证四窗仍隐藏和属于本 child。创建 outer Host scope，gates.beginGateAfterWake=true，afterBeginClaimed/continueBegin 为 child 私有事件对。Host options.requiredPresentationMode=UlwDirtyRect、allowDirectComposition=false；style callback 在这次调用中实际收到 set/clear mask 后记录 graphics/presenter 前提并 return false。先前 Window style本身仍为可合法 ULW 的样式；不能用无效 HWND、两个 DWM gate 或 fake bool 早拒绝代替。

插点原样使用 Host.cpp:1097–1125 → TransparentPresentation.cpp:695–750 的 required-mode ConfigureWindow 失败 → local Begin + terminalOuter Begin（继承 local deadline）→ Host 内部失败握手/释放。outer 首 Begin 后 owner 停在现 helper 门，monitor 已醒；此时不得已有 FirstFrameCommitted、不得 StartProduct 已返回、不得 detach/GPU cleanup 完成或出现新 HWND generation。startupMilestone 已确认 GraphicsReady，style rejection count>=1、exact masks、failure event、gateReached 和 Signal 已有同一 graceDeadline 都是必需前提。gate 之后调用方/日志仍未返回的意义需如实记录，不称自然 GPU 驱动永久卡住。

hold 不放 continue，不调用额外 Close；应真实 cleanup-expired 0xE143001A、graceDeadline+15000 左右自然 signaled，允许 local/outer fatal publisher 幂等多调用，仅首个真实意图 CAS 和首 tick 见证单发布、startReturned/stopReturned 均0。这里 local presenter monitor也可能赢 Expired，记录真正 fatal owner，不能伪称一定是 outer monitor。

release 在 reached 后 250ms 放行；StartProduct 必须真实 false，Host worker/RTS（本例尚未初始化）/窗口附着收尾完成，StopProduct、Window.StopAndJoin 返回，再 outer Complete。旧 Signal.Begin==0（Trace.oldSignalCancelled=1）、start/stop 返回和全部管理线程真 join；不新增 afterComplete 字段；等待到原 grace+1000ms 后仍活，最后自然0。这个反例证明失败 cleanup 可取消，不假称产品启动成功。

### 7. C05：两个真实 Window rollback

使用原 Service::Start(specs, signal)，不加 Window module test framework。required Drawpad.beforeCreate 返回 false 的 callback先记录 invoked；原 Window.cpp:958–968 → RollbackCreation:1144–1174 自己 Begin，true-after-wake 门停在 readyPromise 前。本分支根本没有创建 Drawpad HWND，Trace.oldDrawpad（failedDrawpad 的准确映射）必须0，已经建好的前三窗保持 owned/hidden。

created variant 的 Drawpad.beforeCreate=true，created(HWND) 记录真实 HWND 与 owner thread 后抛固定 std::runtime_error；原 1085–1095 catch进入真实 RollbackCreation，Begin 门停在 DestroyWindow 前。gate 时 Trace.oldDrawpad 必须 IsWindow、PID正确、隐藏，startReturned=0；不能只模拟 callback 抛错但不创建窗口。lifecycleActive 由 beforeCreate=true建立，destroyed计数必须在放行后出现。

hold 两案均原 grace→fatal，001A 自然死、不得有第二代窗或假 return false。release 在250ms放行，真实 Rollback/DestroyGroup/Start失败 StopUnlocked owner join（706–768）完成，Start返回false、所有 created HWND在新建任何窗前失效、destroyed计数匹配、scope真 Complete/旧Signal0；超过旧grace仍活再自然0。Release 的这条完整链同时承担 C11-window，不只测回调返回。beforeCreate false 不会调用该 Drawpad destroyed（lifecycle尚未active），不能错误要求该计数为1；前三个已成功角色仍须清理。

### 8. C07：执行同一个 Main 回退 span

只在新 fixture.cpp 手抄 StartProduct/Stop/新Start 不能覆盖 IdtMain.cpp:2630–2673 的真实 scope。root 在 Main 将这段最小必要 span 提取为同一个 private StartDraw3ProductWithFallback，由正常 Main 和 root 新 RunMainFailedCleanupUlwCounterexample 两个调用点使用。root 的 C07 entry 经新 purpose early 直接调用，结束即return；它不运行完整 wWinMain 的配置/Office/Setting/Bar。new shared header只声明这个 entry的 verified-launch普通类型，不向普通头 forward-declare named-module Window类型。

span保留真实第一 scope Prepare/Start，first false→幂等Begin→原日志→StopProduct→Window.StopAndJoin→第一Complete，关闭意图为0才新Window.Start，新 independent Window/ULW Host scope 和 allowDirectComposition=false。只提取这个必要 span，原日志正常路径保持；early fixture无logger的分支只发布bounded receipt。Main 原 StartWindowService/RefreshWindowHandles closure 同步传入 span、仅在调用内使用；其捕获对象留到 span 返回或进程死亡，不存入后台 owner。C07隐藏 spec builder由root在Main持有，避免 worker重复生产 span。

第一链使用实际 ShouldPreconfigureNoRedirectionBitmap() 的结果；本机若不满足 preferDComp 前提，C07输出 NEEDS_DCOMP_PREREQUISITE，绝不PASS。firstHost style callback 在整个第一次 StartProduct 的所有实际模式请求上拒绝，直到真实 old-chain StopAndJoin/第一Complete 结束才放开。不能只拒绝 DComp，让同一 Host 内 ULW 成功后跳过 Main fallback。必须取得 graphics-ready/真实style rejection、first StartProduct=false 和 Main fallback entered三个见证；故障前提不成立即FAIL。

oldJoin 时记录 Host.Running=false、旧四窗销毁边沿/IsWindow=false、ownerThread终止、旧 Signal.Begin==0、意图仍0。再建立唯一新四窗 generation；仅主 Drawpad 绑定 Host/RTS，辅助没有第二 runtime。Window 生命周期计数按 creation/destroyed边沿判定，不因 USER32 重用旧 HWND数值误判。ULW首次成功 Present 必须 mode==UlwDirtyRect、successfulPresentCount>=1、firstFrameReady、lastPresentSucceeded，主样式无NOREDIRECTIONBITMAP和透明穿透，辅助固定ULW，FLIP_SEQUENTIAL/两个DWM禁用保持源码合同。

等待到旧实际 graceDeadline+1000ms（非从新启动重新加15秒），核还活且意图0/没有BeginShutdown，然后新代写一笔，核本轮 ingress/consumed/stored/contentRevision 和新的成功 Present，仍 mode ULW。old stale Signal对新scope仍0；首/次scope真join之后无旧timer。最后用真实 SetOffSignal(1) 和 StopProduct/Window.StopAndJoin 自然清理，最终自然0。这个结果证明受控Main重建与取消，不能外推 Win7或正常硬件DComp一定可用。

### 9. C08：实际 ObservePresented 发布后停 render owner

HostStartOptions仅增加默认null的 HANDLE successfulPresentReachedEvent / continueSuccessfulPresentEvent。有效条件固定为 enableHiddenTestContactInjection=true、事件成对、succeeded=true、firstFrameReady已true、currentPageHasContent=true、observation.presentedContentRevision==当前contentRevision。新事件代码位于 Host.cpp:337–397 ObservePresented现快照更新、ULW统计及PublishRuntimeRevision之后；first ClearCanvas的回调先于1185设 firstFrameReady，因此不触发。第一笔未stored也不触发。条件不满足不SetEvent/等待；未授权普通产品不会传事件。没有通用callback/context、没有逐帧clock。此用途的一个content frame门足够，不扩大frame选择器。

实际 ULW Start成功并 Complete Dormant startup scope；一笔真实 Down/Move/Up 存入history，有content且成功呈现，门进入。controller thread取得当前快照核 successfulPresentCount>首帧基线、实际primary output/content版本一致、terminal已发布、Stored内容/完成种类；如果 diagnostics发布时序不足以取得所需receipt，按前提失败处理，不能继续凭event名PASS。

另一受控Win32 close thread记录 request tick后真正 SetOffSignal(1)，记录原 PublishedShutdownDeadlineTick、实际armState2/4，再通知管理线程进入 StopProduct（stopEnterTick）。render owner仍停在ObservePresented，Host.Stop原 exit屏障/wait/join链1361–1400停住。hold必须按原ordinary deadline自然退出0015或0016，stopReturned0；不能启用startup cleanup timer或给普通Close再加grace。release对照先在门停250ms后SetEvent(continue)，再走同一真实Close/Stop，须自然0、stopReturned1、所有join完成。两案都不使用GPU Present失败冒充render暂停。

### 10. C09：Host Desktop worker delay、真实 drain 和新读者

worker唯一写 AutoSave.cppm/.cpp：DesktopAutoSaveTestFaultInjection新增默认null void* enteringWriteDelayEvent；在 AutoSave.cpp:654–656取得fault快照、writeDelay!=0时，Sleep前SetEvent。事件失败使该测试事务明确失败，不静默宣称到达；delay==0或event空完全走旧语义。只该auth child设置，事件保留到worker真join/进程死亡，不在重设全局fault时关闭旧快照仍借用的事件。

真实Host options.autoSaveRoot=上述private根，hidden注入启用；PublishProductState使 autoSaveEnabled=true、Desktop/Pen/nonselection。无fault先写A（RGB_A与B不同）并Clear；Clear command必须Accepted、真实commandCount推进，Desktop capture/worker回到committed1/pending0/failed0。源码CaptureDesktopAutoSaveForScene:1031–1120在Clear前捕获当前可见一页并生成fresh fileGuid，不能用写prepared sentinel代替。窄严格读者读取这个实际index/UInk，产生baseline observed Receipt；仍为一笔A且page/workspace/file身份正确。baseline阶段没有强制关闭Host，不替换成standalone worker。

再set writeDelayMilliseconds=60000和enteringWriteDelayEvent，写B并第二次Clear。需 command Accepted、accepted==baseline+1、committed仍baseline、pending>=1，且真正进入delay事件；accepted/pending本身不证明到Sleep。SetOffSignal(1)在独立close thread发生于gate后至多2秒，记录原ordinary deadline，然后管理线程真正 StopProduct。render持续可处理最终PrepareExitAutoSave，但 autoSave.CloseAndDrain仍等待60秒Sleep中的同一worker；普通15秒先到，0015/0016自然死、stopReturned0，不能调短产品drain或称第二请求Saved。

父精确确认旧HANDLE自然signaled后，以同run/bin另建新1024B mapping和同三继承HANDLE启动 C09-read-committed；expected从baseline receipt复制。该新child无Host/Window/GPU，仅调用AutoSave module窄严格读者，重读最后index引用的A：dailySequence/session/sequence/trigger、fileGuid/workspace/page GUID、workspace type、one-page/interval、所有stored点和style的digest、UInk bytes/sourceRevision摘要、index摘要与baseline一致。第二Clear B未进index；不把 orphan/文件存在当Saved。reader必须自然0+readerSucceeded1，生产child截止和恢复分别记录，任一失败都使C09总结果FAIL。

release使用同trajectory，真实writeDelay=400ms且仍有reached事件，待第二Committed/pending0后正式Close/Stop，全部accepted请求进入终态，自然0；新的receipt是最后B，并用新reader验证。C11-natural-close同A/B而fault全空，独立证明默认零delay正常排空。

这个读者只证明最后durable点的完整可读性；当前产品自动Desktop恢复UI、未保存零丢失、重新安装Controller/可见笔迹恢复均不在本批授权或证据中。

### 11. C10：真实 PPT worker UInk 完成、index 未换

复用 PresentationAutoSave.cppm:143–152 的现 afterUInkCommittedEvent/continueIndexCommitEvent；实际停点在 cpp:1061–1067，新version已经durable，CommitIndex尚未调用。原等待30000ms保持；Close必须在gate后2秒内开始，原15秒先于该30秒返回。本例不设置 failIndexCommit=true、不绕过worker或NamedMutex，不连接Office、不开放closed白板/PPT业务入口。

建立真实同一ProductHost/private根。仅auth夹具在Host.Start前设sessionIdOverride=本run nonce的canonical GUID；root/正常产品保持空。descriptor固定provider、private绝对source名字、applicationProcessId=本child、slideShowHwnd=自有Drawpad、bindingRevision=1，stable slideIds={601,602}，通过实际ResolvePresentationTarget和PublishProductPresentationTarget取得key/targetRevision，requirePresentationUiReady=true并为matching ready identity发布真实UI-ready值（无PageControl GUI）。所有页/输出ready须核完整identity和成功content版本。

page601写A并切到602，真实controllerwhole-presentation保存；无faultawait presentation committed1/failed0（accepted含Load，不强行断言==1）。使用一个fresh PresentationAutoSaveService在本child同session、无pending缓存下真实SubmitLoad取得baselineCommitted的mutationRevision/fileGuid/workspace/Canvas GUID/stable IDs/独立EndScreen、stored笔迹receipt和index字节摘要。baseline物理version由productionReadUInk/strict importer核，不能先stub出snapshot。此fresh service CloseAndDrain完成后才设置两个gate。

返回601待严格ready，增加一笔B（A+B与baseline A可区分），切602触发新Save；afterUInk event到达且gate未放，snapshot新revision已有完整UInk，而index字节仍精确baseline。gate发生后才给另一线程真正SetOffSignal(1)，随后Host.Stop原presentationAutoSave.CloseAndDrain卡住。hold要求原15秒0015/0016自然死、stopReturned0；新物理version不是Committed completion，不称saved。

父确认旧死后启动 C10-read-committed，重构同一target：由private source绝对路径重新Resolve，比较key、sourceIdentity、旧origin PID/观测HWND生成的bindingToken、bindingMode/SlideID topology/bindingRevision/sessionRevision，targetRevision取已验真receipt值；数值旧HWND只用于identity，绝不发消息。reader只设同一auth sessionIdOverride，创建全新service（pendingIndexEntries为空），Start→真实SubmitLoad(Current,target)→CloseAndDrain→TryTakeCompletion。要求 Loaded、storageTrack Base、fileGuid/loadedSnapshot/target回显、旧mutationRevision、oldA而非A+B、Canvas/page身份和indexSHA都与baseline一致；productionLoad同时检查sourceRevision与严格应用extra（1182–1204）。

现Tests的正确模式见 presentation_autosave_tests.cpp:516–573 TestInterruptedSecondCommitKeepsPreviousVersion：seed Committed、same-session event窗、fresh service读旧点；跨process本批由auth reader继承原session，不能用parent默认ProcessSessionId冒报可恢复。额外 C10-read-foreign-session用从nonce确定的不同canonical session，在相同root/target fresh Load必须CrossProcessConflictDeferred并不返回loadedSnapshot；root不删除或重写文件。既有生产session/binding/SlideID校验全部保留。

release在gate后250ms放continue，真实index提交、controllercompletion/诊断committed推进，再同正式Close自然drain，reader读取final A+B的新mutation。保留旧和孤儿version；不扫files找“最新”替代index。此处恢复结论同样限持久化严格readability；普通新产品进程如何授权恢复旧session另有产品门禁，不因测试override开放。

### 12. 下一组职责、判定与串行实施顺序

C04下一组由唯一RTS writer拥有 RealtimeStylus.cppm/.cpp 和新的有限fixture分支；root拥有新枚举/信封/调度、helper及Main，check只写独立报告。必须拆开：真实初始化失败清理（现2397–2625）中的明确 synthetic终止HRESULT；Disable/Remove FAILED后的不Release/不Reset/noreturn（2651–2664）；真实provider返回S_OK时callback停止/排空。synthetic结果必须同时记录原providerHRESULT与注入HRESULT，不能用header里Shutdown声明、hidden mailbox笔画、同步插件method存在或纯helper PASS证明provider callback quiescence。若实际provider callback来源/in-flight证据不足，成功静止项保持未验证；不为首批添加callback registry或重写同步插件。关键强引用还包括removedPlugin的raw临时引用和coordinator/Host/HWND，必须留到死亡。具体gate/POD扩展需另冻结，首批未声明运行许可。

C06下一组直接复用root已提取Main span，第一StartProduct真实false清Host后，仅旧dummy MagnifierHost.destroyed停住真实Window.StopAndJoin；outer原失败scope仍armed，不出现新generation。它与C03的Host内部暂停、C05的readyPromise前rollback是三个不同证据，不相互替代。

root唯一writer：Main正常/早期C07 entry及共享span；ShutdownSupervisor新authorizer/父runner/finite enum/POD/capability；FailedCleanupDeadline两源的beginGateAfterWake；新shared header；主项目登记及filters；父账本/spec/所有build/run/output。既有helper writer须先释放所有权，root再开始，不双写。worker real-fixture唯一writer：新 Drawing/Draw3/Draw3.FailedCleanupFixture.cpp、Host.h/.cpp两event和hidden persistence getter、AutoSave.cppm/.cpp的delay事件和窄读者。不改Window、Presenter、RTS、DrawingController/RuntimeMetrics、HiddenWindowTest或PPT存储算法；后续U2触Host时串行交接。普通module global fragment只include所需normal头，不把完整Supervisor拉进Headless。

新fixture.cpp只负责六场景的有限准备/trajectory/receipt与reader；C07转到root Main entry。文件登记只在主Inkeys.vcxproj(.filters)，不创建Solution或新构建系统，不把真实窗口/产品监督拉入InkeysHeadlessTests。保留现文件UTF-8/BOM与CRLF，关键门与异常寿命用适量中文注释。

执行顺序固定：独立设计review→root helper测试接缝/新信封和worker夹具源码冻结→独立增量源码+safety（准确SHA、POD三架构布局、参数负例、文件根、借用寿命）→root唯一构建槽完整Debug|ARM64 solution（ARM64原生MSBuild，同PowerShell先Remove-Item Env:PATH并MSBUILDDISABLENODEREUSE=1，至少5分钟预算）→严格Headless/PptCOM与十primitive回归→各release/no-fault先验前提→各hold/C07→fresh reader→独立实际结果复核。红候选和首个有意义失败输出保留；不随机改源码/工具链绕过环境失败。

父运行上限：授权负例3s；C03/C05 hold在ack之后45s；C07/release含超过grace等待在ack之后45s；C08/C09/C10在真实closeRequest/已发布普通deadline后再25s，前提准备独立最多20s；reader20s。阶段限额失效就FAIL并仅清父持有的本轮child HANDLE；不杀进程名/PID邻居、不把父kill记产品自然截止。runner自身用分段wait保持每次<=60s，构建/采样/其他真实GUI CLI全不并行。

产品自然死亡：必须WaitForSingleObject(exact child HANDLE)==WAIT_OBJECT_0后GetExitCodeProcess；hold的期望码、gate/前提/error0、start/stop未返回、原绝对deadline和死亡时间一起满足才通过。grace hold原tick+15000至死亡的调度容差上限2s；ordinary hold要求death在原ordinaryDeadline至+2s，绝不重新加15秒。提早以assert/fixture错误码退出、窗口/RTS/GPU/保存前提失败、缺gate、file缺失、任何字段不一致、父cleanup都FAIL；没有“前提失败但进程已死所以PASS”。原fatal publisher可幂等并发调用；只有首个意图/首 tick 见证一次发布，普通15秒armState2/4须有实际见证，不从SetOff调用成功文本推断。

恢复：C09/C10先单独判旧产品截止，再单独判fresh reader自然0/严格receipt，通过总合取；reader失败不能覆盖旧死结果，也不能被旧死PASS覆盖。重启：本批均Close，不启动新产品实例，C07只同进程重建Host/Window；新产品Restart/UEF必须另核旧death后唯一新实例、single-instance交接和恢复，不由本批继承PASS。

代表重复固定三轮串行：C03-hold、两个C05-hold、C07（每轮超过旧grace继续一笔）、C08-hold、C09-hold+reader、C10-hold+same/foreign reader；C11-natural-close三轮。每个release边界至少一轮；出现波动增加同一case的定位，不以另两绿抹去红。Release三架构编译/适用回归留root最终门，尤其helper/POD的Win32/x64/ARM64 sizeof和link，不能用配置存在替代运行。每轮记录candidate SHA、case、父/child/reader PID与exactHANDLE死、所有阶段tick/原deadline、窗口生命周期、成功Present与content版本、service计数、receipt/UInk/index身份、exitCode及是否parent清理；三个阶段各自有status/raw。

## Files Found

- Inkeys/Inkeys/Helper/ShutdownSupervisor.cpp:94–140,1031–1056,1723–2031,2221–2309：现96B primitive、exact父/镜像鉴权、三继承HANDLE、父超时FAIL与early dispatch。
- Inkeys/IdtMain.cpp:259–309,671–677,2524–2570,2630–2673：真实普通/失败publisher、early入口、Window scope、Main DComp→ULW scope。
- Inkeys/Inkeys/Helper/FailedCleanupDeadline.h:14–27、.cpp:144–205：默认测试门、Dormant monitor、CAS后pre-wake测试门。
- Inkeys/Inkeys/Window/Window.cppm:87–124；Window.cpp:706–768,951–1095,1144–1187：现beforeCreate/created/destroyed、promise前rollback、owner真join。
- Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.h:115–185,190–280；.cpp:337–397,524–581,996–1028,1030–1329,1344–1400：private input、诊断/observer、真实握手/释放、最终save barrier/drain。
- Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cpp:695–867：真实style拒绝和local/outer共享deadline，required最后mode先Begin。
- Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp:2397–2700：实际provider初始化、Disable/Remove先fail-closed和未验证成功quiescence。
- Inkeys/Inkeys/Drawing/Draw3/Draw3.HiddenWindowTest.cpp:95–115,467–673,2491–2619：既有屏幕外owned链、真实presentation保存与顺序重建模式；本批只参考。
- Inkeys/Inkeys/Drawing/Draw3/Draw3.AutoSave.cppm:99–137；.cpp:373–454,492–580,654–656,926–959,1052–1145：delay缺到达事件、严格index/codec、fresh records缺口。
- Inkeys/Inkeys/Drawing/Draw3/Draw3.PresentationAutoSave.cppm:143–175；.cpp:1061–1204,1411–1541：现UInk/index gate、same-session strictLoad、sourceRevision、真实drain。
- Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:1031–1120：真实Desktop当前区间capture与fresh fileGUID；不需要修改。
- Inkeys/Inkeys/Drawing/Draw3/Draw3.Presentation.cpp:151–183；Draw3.Bridge.h:77–137,192–201：path/key/bindingToken resolver和target/ready完整身份。
- inkStrokeModelerTest/draw3/uink_draw3_export.cppm:32–94；uink_model.cppm:462–469；uink_file.cppm:22–24：stored x/y/width/style模型、sourceRevision SHA256及生产ReadUInkFile。
- inkStrokeModelerTestTests/presentation_autosave_tests.cpp:516–573,2431–2483：同session/fresh interrupted读取范例与已有atomic child；它不是新Host Stop场景。

## Related Specs

- .trellis/spec/native-desktop/errors-logging-and-resources.md：先建立普通退场监督、原15秒、先停producer/join再释放、强退保留最后durable点。
- .trellis/spec/native-desktop/draw3-integration.md：唯一Host/RTS、owned双surface、Presenter两DWM禁用/FLIP不变、成功Present/content/target版本、PPT strict storage身份。
- .trellis/spec/native-desktop/input-and-ink.md：真实输入汇合、hidden合成与真实硬件证据分开。
- .trellis/spec/native-desktop/cpp-conventions.md：module边界、共享状态按线程owner传递、最小中文注释和本源格式。
- .trellis/spec/native-desktop/build-and-compatibility.md 与根用户AGENTS：完整Solution/PptCOM依赖、ARM64原生MSBuild、合理超时、禁止GUI/remote/Git副作用的本research边界。
- .trellis/spec/ppt-interop/native-session-ui3.md：完整target/ready、版本、EndScreen独立Canvas和closed功能边界。

## External References

本题只确定当前仓库的可执行合同，未新增外部查询或工具版本判断。Win32/COM/RTS/DirectX语义以当前Windows SDK/C++20调用和已审材料为证据；本报告没有从API声明推出provider quiescence或Win7运行成功。版本/系统运行验收仍由root记录真实构建与机器。

## Caveats / Not Found

- 新CLI、1024B信封、beginGateAfterWake、Host事件/getter、Desktop到达事件/严格fixture读者和Main共享span全为本报告提案；没有编译/运行结果。原P1/P2 GREEN不批准本提案自行开跑。
- 没有自然GPU/RTS/COM永久停滞现场dump。受控event/delay/HRESULT证据不得写成用户现场故障唯一根因。
- Desktop SubmitLoad(fileGuid)仍只支持本service已Committed记录；fresh fixture读者不会替产品开放冷恢复UI或NotReady入口。PPT跨process同session测试override也不等于普通新产品自动恢复已获准。
- UInk Draw3 snapshot点只有x/y/width，style有kind/opacity/RGB/texture；没有原始pressure字段。恢复核全部stored几何宽度和style/identity，不伪称原始压力样本逐值恢复；输入压力到宽度模型另有输入测试证据。
- C04 successful provider callback排空、C06旧owner独立hold、真实Restart/UEF、Office、Win7 KB2670838/GPU组合、真笔/Touch与用户体验尚未在本批验证。Header声明、purehelper、sameprocess Host重建、父强杀或durable sentinel均不能替代。

## C3-B 实码来源增量：PPT reader 的旧 HWND 纯值

2026-09-30 冻结缺口修订：352B Receipt 无 HWND，reader Trace 原全零，仅 receipt+originPID 无法重构真实 bindingToken。保持1024B/POD/argc不变：parent 在 exact producer HANDLE 已 signaled 后取 trace.oldDrawpad；只 ReadPptCommitted/ReadPptForeignSession 新 mapping 初始化 trace.oldDrawpad。authorizer 只这两case允许该纯值非零且<=UINTPTR_MAX，暂存副本将它清零后核其余Trace仍全零；Desktop/production cases完全维持全零。reader 使用 originPID+此数值与固定descriptor重构身份，不OpenProcess、不对旧HWND发消息，不将 PID/HWND升级为权限。expected仍是死亡后完整封口Receipt。此新reader输入语义须新实码增量safety及构建/真freshLoad，旧C3-A九selector CLEAR不能直接当C3-B授权。

## C3-B C10 初次合法 NotFound 计数口径

2026-09-30 实码核对：Controller 初次601 target真实SubmitCurrentPresentationLoad，PresentationAutoSave::PushCompletion对NotFound也累加diagnostics.failed。保留生产结果/计数：先核真实Current Load NotFound和合法empty-ready，再锁存初始failed基线；后续Save/Load要求failed无新增，Trace仍输出actual absolute值，不Reset/伪0/预seed。Host仅hidden injection时在原真实completion看到operation Load/status NotFound累加单个计数，explicit HiddenPersistenceSnapshot getter读；不改普通service/SharedPOD或默认热路clock/lock。committed和fresh严格Loaded/完整receipt分别证明save/readability，不拿accepted总计冒Save数。本新增来源须独立实码复审，当前C3-B仍未验证。
