# E02/F063 启动失败边界：分批设计

日期：2026-09-30。Active task：`.trellis/tasks/09-27-integration-and-release-check`。负责人：`shutdown_failed_arm`。本轮只有此设计文件可写；没有源码、测试、工程/spec/账本、Git或进程操作。E01源码和E03初始化失败身份修补已由主会话冻结；本设计等待独立审查与 `GREEN_DESIGN`。先实施最小 A/B，C 单独确认合同/所有权，不把一份批准扩大成全仓改造。

## 0. 当前事实和分类

- 已读 `startup-boundary-postcommit.md` 全文、E01 design/code-review、新 errors 合同、父 handoff 和 Main/Host/Window/RTS/Bar/持久化实际源码。构建入口仍为完整 InkeysRepo.sln，原生ARM64 MSBuild；所有构建/CLI由root执行。
- **确认的顺序缺口**：D003/D004/D005、较晚D002在确认fatal之后，进入 PublishFatalStartupFailure，而该helper尚未Arm；晚期主循环在ActiveSnapshot.failed后先等待、构造消息、Show、Preview.Stop，最后才SetOffSignal。实际永久挂起未动态复现；“顺序缺口”不等于已证明driver/COM死锁。
- 早D001/早D002也已决定return，却直接ReportFailure+Show，无正式退场；可沿同一helper合同最小处理，保留各自return1。D003/较晚D002既有return0不改。
- 原六个F063分支D101/D201/D301/D401/D202/D102已在提示/清理前Arm；但其fatal日志往往还位于Arm前。IDTLogger在Main使用async_overflow_policy::block，队列8192、64 workers；磁盘/队列卡住时这些enqueue可以等待。这是条件性调用顺序事实，未复现日志背压。不得通过关闭日志来修顺序。
- B001和B002有真实pre-ready发布者：Bar.InitializeWindow失败、RenderLoop.Register失败 → SetBarStartupState → Startup.ReportFailure → Main ActiveSnapshot。B003只有映射、未找到正常producer。
- **B004未受保护来源假设排除**：当前Bar.Initialization的三处StoppedBeforeReady均在offSignal已非0（开头/中段检查、退出循环尾）。正常Main不会以offSignal0在while中处理这些B004。通用snapshot处理会保护该code，但测试不能把人为B004状态注入写成已证实生产漏Arm。
- Host失败Start确实在return false前完成join/drain/detach；main随后的StopProduct通常不进入活动Host最终保存屏障。startupMutex condition.wait会释放锁；Window owner在自身执行Submit不排队等自己。未发现需要作为已确认互锁上报的简单mutex环。
- 无deadline等待仍存在于Host失败join/RTS Shutdown、Window失败rollback/owner join及DComp→ULW重建。stop_token/wake不会抢占已运行的COM/DestroyWindow/driver/callback；C针对**已知失败后的清理**，不设置产品普通冷启动总计时器。

## 1. 单元 A：fatal 意图与提示/日志/清理顺序

### 文件、签名、所有权

唯一写入者：本agent，仅 `Inkeys/IdtMain.cpp` 的明确fatal分支和 `PublishFatalStartupFailure`。root保留其余共享Main调度/版本策略；不碰窗口/Host实现、项目、Bar渲染或设置业务代码。函数签名保持：

```cpp
void PublishFatalStartupFailure(std::uint32_t code, const wchar_t* message) noexcept;
void SetOffSignal(int signal); // E01现有接口/失败接管不改
```

### 最小改变

1. PublishFatalStartupFailure的第一条业务操作是SetOffSignal(1)，随后原ReportFailure、350ms失败帧预算、原Show文本/模态、500ms淡出、Preview.Stop原样。完整失败红帧仍是best-effort，不能承诺scheduler已退出也必呈现。
2. fatal调用者不能依赖这个helper来保护**函数调用前**的logger或wstring构造。在D002晚/D003/D005和原六分支，已确定fatal后先SetOffSignal，再原日志/提示/清理。原六分支已有SetOffSignal与中文说明整体前移到日志前；不复制新退出实现。
3. D005先以Win32本地DWORD捕获activation/load的GetLastError，再Arm，再记录保存的error。其它HRESULT已为local value，不因Arm覆盖错误上下文。
4. D004直接入helper无前置日志；helper统一补保护。早D001/早D002把原ReportFailure+Show最小改为同一helper，消息文本和return1保留。其它失败return0/1、COM owned判断、反向资源释放顺序不改。
5. Main看到startupSnapshot.failed时立即SetOffSignal(1)，再进入原失败帧/字符串构造/Show/Stop流程；不用helper改变该分支原350ms等待语义，删尾部重复SetOffSignal。B001/B002受保护；B004已有退出意图不被改写。
6. helper和caller的重复SetOffSignal是首CAS幂等调用，保留必要的caller前奏以保护日志。不得为去重把它再次挪到潜在等待之后。正常启动、可恢复DComp失败、用户Cancel、白板gate、返回码、版本/通道无改变。

### 验收

- B harness先存在、A旧源码仍保留：实际fatal分支到达prompt/log hold却尚未接受意图；parent上限后只结束自己的child，记FAIL。
- A修补后同binary配置/场景：helper前已正式Arm，holder不放行也约原15秒由产品监督退场，Close无新实例、child HWND不残留。不使用父强杀当绿色。
- 无hold反例：消息确认后原branch按既有返回码/清理自然结束，不因为Arm增加固定等待；user普通取消重启不受影响。
- 静态审调用链确认所有上述fatal前奏、错误保存、E01成功顺序/双失败契约；独立review+最后修改后的完整Debug ARM64/Headless/PptCOM/supervisor，再Release三架构。

## 2. 单元 B：进入真实 wWinMain 的私有故障夹具

### 最小接口与文件

本agent独占 `Inkeys/IdtMain.cpp` 相应probe点、`Inkeys/Inkeys/Helper/ShutdownSupervisor.h/.cpp` 的既有测试框架扩展。不新增Solution/test项目/另一套进程框架。以下为**拟议签名/入口，尚未实现**：

```cpp
namespace Inkeys::Shutdown {
    bool IsAuthorizedStartupFailure(std::uint32_t site) noexcept;
    void ObserveAuthorizedStartupBoundary(std::uint32_t site,
        std::uint32_t boundary) noexcept;
}
// 既有Early函数/标准suite保留；新增精确selector：
// Inkeys.exe --shutdown-supervisor-tests --startup-failure-only
```

新的内部 `--inkeys-internal-startup-failure-child-v1` 由原Early dispatcher识别：失败授权必须返回错误退出，不能落普通GUI；**成功授权只预置私有状态，然后继续实际wWinMain**。不会用RunTestParent复制fatal算法或直接调用PublishHelper冒充初始化分支。

### 授权与数据隔离

- 复用E01的MakeUefTestDirectory/copied bin/精确inherited parent+ack+mapping handle list。父EXE HANDLE image与显式expectedParentImage的volume/file-index完全一致；root/bin/copied EXE非reparse、image恰为该bin/Inkeys.exe；数字/uintptr边界、HANDLE_FLAG_INHERIT、映射magic/version/size均核验。新mapping版本独立固定POD并static_assert布局，不能修改/复用E01旧48B schema或削弱旧UEF argc6/7。
- defaults为None/null；没有env、配置或普通UI故障开关，没有公开setter。只能精确受控parent生成的copied child启用。任何参数未知、身份/句柄/magic错误都在配置、mutex、窗口之前退出。
- 根目录来自当前可信copied image，不改真实globalPath/pluginPath。独立配置/opt/AutoSave/log/嵌入DLL都留在新目录。私有配置关闭SuperTop、网络自动更新及外部PPT绑定；若对应配置不能阻止创建外部业务线程，只在**已经授权的test child**省略该线程启动，并明确记录覆盖限制。正常product不变，更新逻辑/安全验证范围仍保留用户决定。
- 实际Main还有系统外部副作用，不能仅凭复制目录认定安全：FullConfig后无条件QueryStartupState/SetStartupState（2056–2057）、Plugins阶段shortcutAssistant.SetShortcut/StartDesktopDrawpadBlocker（2117–2120）、后期PPTLinkage线程，以及formal宏下AutomaticUpdate。新增只读getter `bool IsAuthorizedStartupFailureChild() noexcept`，仅通过强授权信封的child为true；该fixture跳过上述系统启动项/真实桌面shortcut/外部helper/PPT/update调用。SuperTop分支也显式隔离，不能凭opt默认为false就放行。只保留真实内部config/字体/embedded COM/Host/window/fatal流程，不宣称被跳过的系统集成由本测试覆盖。默认getter false，普通产品逐句行为不变。
- fixture可使用真实配置把Preview开启/关闭以区分早/晚D002；不开放白板/Unsupported/NotReady。实际窗口可移到私有offscreen布局以避免拦截桌面；真实提示反例只操作/观察该child HWND，不影响用户实例/Office，无computer-use。

第一批固定observation提议如下（仅新startup信封，128字节；不扩充/混用E01旧packet）：

```cpp
struct StartupFailureObservation {
    DWORD magic, version, bytes, site;                 // 0..15，parent构造后不改
    volatile LONG authorized, failurePublished;       // 16/20，child授权与Main发布
    volatile LONG armPublished, gatePublished;        // 24/28，Arm owner、gate owner各自发布
    DWORD expectedFailureCode, realInitResult;        // 32/36，parent期望/Main真实结果
    DWORD boundary, armResult, armError, armState;     // 40..55
    LONG acceptedIntent; DWORD reserved;              // 56/60
    ULONGLONG requestTick, failureTick, armStartedTick;// 64/72/80
    ULONGLONG armDeadlineTick, gateTick;               // 88/96
    DWORD windowCountAtGate, reserved2;               // 104/108，parent观察
    ULONGLONG reserved3[2];                           // 112..127
};
```

magic独立于E01，version1/bytes128；static_assert sizeof128、requestTick64、armDeadlineTick88。scope字段不塞入此packet，C如需记录另定义独立版本。site是Dpi/EarlyRender/Com/PptCom/LateRender/Font/BarWindow/BarRegister等有限值；boundary有限为BeforeFatalLog/BeforeFatalPrompt等。未知值拒绝。授权与Main/Arm/gate各自先填自己的字段再Interlocked发布；同一slot只首次发布，parent不复制未经各slot published核准的整包。Show hook只有目标failure确已发布才hold，普通早期提示/同实例提醒不能误触发；自然其它错误导致目标未达必须记FAIL，不当绿色。

### probe与计时

- Main真实初始化结果边界增加auth-only结果/分支注入：D004使用真实CoInitializeEx后强制已声明fatal条件，不能用已允许RPC_E_CHANGED_MODE；D005在实际文件验证/activation/load结果if处强制fatal，不删除DLL；D003在真实font result处强制条件；D002在对应早/晚result处区分。
- 不伪报真实COM/字体自然失败。记录real HRESULT及synthetic site；实际已取得的COM/module资源按test ownership处理，不因改HRESULT丢owned事实。
- 在真实ShowStartupMessage前（或具体fatal日志前）的已授权probe记录gateReached并hold。默认无probe。至少一轮使用真实MessageBox Show、parent观察child自己的对话窗而不响应；不能只用源码grep代替运行。
- mapping在进入Main前建立；阶段/intent/Arm result/error/绝对deadline/到达tick写固定POD+Interlocked发布，无fatal临界文件/logger/堆/等待parent回执。多producer字段按Main、Arm、gate各自独占slot，防止把共享packet当无保护聚合结构。parent保留view直到旧HANDLE死亡，再读published字段。
- 从fault触发、首次有效Arm、gate到达、精确旧HANDLE signaled分别计时，不能用父强杀或回调数换口径。parent在最大启动等待+15秒+调度容差内判定，红灯上限只终止已核对的本轮child HANDLE，保留失败日志。

### 第一批A/B场景与后续扩展

| 场景 | 真生产链/注入点 | 红/绿要求与限制 |
| --- | --- | --- |
| D004 prompt hold | wWinMain真实CoInitializeEx结果if → 原helper → Show前gate | 旧无Arm且一直活，修后15秒退场；不声称真实COM故障 |
| D005 log/prompt hold | 真实PptCOM activation/load结果if，保留真实模块cleanup对象 | 原error捕获，修后log前已Arm；不得删/改真实Office或DLL |
| D003 prompt hold | 真实InitializeFontCollection结果if → 原helper | 原return0保留；资源/失败帧仍在真实模块 |
| 早/晚D002、D001 | Preview配置区分，真实Init/DPI结果if → helper | 最小后续扩展，不把不同site同code掩盖；branch return码不改 |
| B002 state-flow hold | 私有child在本来启动Bar初始化线程的位置替换为真实SetBarStartupState(ClientRegistrationFailed)，让Main仍沿ActiveSnapshot处理 | default的Bar线程不变；fixture期间不产生FirstFrameCommitted以免先封状态；只证明状态传播/Main处理，**不称Register自然失败** |
| B001 state-flow | 同上以WindowMissing | Main通用边界复验；actualproducer证据来自Bar源码 |
| B004 source check | 当前三处setter均after offSignal | “未受保护source”不适用，附理由；可选人工状态注入仅验证通用handler，不升级为新bug |
| 原F063 D101/D201/D301/D202/D102 | Main实际if结果点强制对应失败，包含已启动Host的D301 | 逐case记生产分支覆盖；不替代initializer内部等待。D401仍gate关闭，N/A |

优先四项D004/D005/D003/B002，默认三轮；先旧顺序红，再A最小修补绿。其它site可分批而非一次巨型diff。新增selector和payload均需准确记录实现后才能写“已运行”。无外部Office关联是测试隔离限制，不替代PPT矩阵。

## 3. 单元 C：已知失败清理的可取消截止（另审后实施）

### 3.1 合同与预算

建议第一版 **失败清理grace=15000ms**（保守保留正常错误恢复空间），只从第一处**已知failure**开始计时。到期接受不可撤销Fatal Close，再由现有SetOffSignal的15秒监督退场：最坏约30秒从已知清理失败开始，**不是**从正常冷启动或用户点击15秒。普通Close/Restart仍从已接受意图沿原15秒绝对tick；重复Close不重置现有截止。

scope可在构造前准备一个**Dormant** Win32 monitor，但未发现failure前无deadline/终止动作；它不是general cold-start timer。预准备是为覆盖“failure之后、promise发布之前的Rollback/RTS/GPU清理”，同时避免多个owner在已失败时争资源创建。正常成功后完全取消并join；不永久保留第三个render/设备owner。

Scope准备失败时，在任何可能阻塞的原清理前接受Fatal Close并沿E01保证退场，禁止继续无保护join、假称scope建立或普通false让Main启动ULW。若准备本身的OS调用不返回或OS停止调度，仍属系统边界，不宣称可抵抗所有内核故障。

### 3.2 共享接口提议（需要root冻结）

```cpp
namespace Inkeys::Shutdown {
    struct FailedCleanupCallbacks {
        void* context = nullptr; // 非owning，scope owner必须留到所有startup producer退出
        void (*beginKnownFailure)(void*) noexcept = nullptr;
    };
    class FailedCleanupDeadline { // 只拥有固定Win32状态/事件/monitor，不拥有GPU/HWND/document
    public:
        bool Prepare(DWORD graceMilliseconds,
            void (*acceptFatalClose)() noexcept) noexcept;
        void BeginKnownFailure() noexcept;
        FailedCleanupCallbacks Callbacks() noexcept;
        void CompleteOrFatal() noexcept; // 已expired不可返回正常流程
        ~FailedCleanupDeadline(); // 必须cancel/真实join，不detach
    };
}
```

Main准备scope及process-lifetime callback（调用统一SetOffSignal(1)），以Callbacks传入本次Window.Start和HostStartOptions；failed lease跨Host失败清理、StopProduct与DComp重建旧窗口join保持同一最早deadline，**不能每个子步骤重新加grace**。清理全部成功后CompleteOrFatal取消，才可开始下一代ULW正常attempt。成功Start必须清掉组件内保存的非owningstartup callback，不能让后续Run/正常Destroy读已销毁的scope。

`Window::Service::Start(specs, callbacks={})` 与 `HostStartOptions::failedCleanup` 是候选接口；RTS.Initialize增加startup-only的默认空callback参数，仅在失败分支Release/Shutdown前调用。默认空用于原独立Host/非产品调用兼容，不把全产品与旧demo的生命周期机械合并。具体module/global fragment类型登记需独立compile gate；不增加source工程项、不递归改第三方。

### 3.3 cancellation、expiry与资源寿命

- monitor固定Win32-only状态，Dormant→Armed→Cancelled/Expired；第一个已知failure记录绝对deadline，后续begin不延长。Interlocked/CAS决定唯一终态，不用业务锁、GPU、窗口owner或磁盘。
- Cancel必须在线性化的expiry赢得前成功；Expired赢得后，无论清理刚返回还是scope离开，都不能重启ULW。CompleteOrFatal调用统一Fatal Close（重复幂等）后只等已建立退出，不返回未受保护流程。
- Cancel只设置固定state/event；monitor不再执行fatal callback。Join设**1000ms管理预算**；纯monitor不及时signaled也转Fatal Close并保留context/句柄/业务资源直到进程退场，不detach、TerminateThread或释放仍被线程访问的对象。到期callback自身卡在日志/清理也已先建立进程级15秒保护。
- lease由Main活到失败清理全部结束；monitor只持固定context而不引用Host/Window/document。Host绘制owner仍自己销毁GPU；Window仍由各owner DestroyWindow/activeSpec.destroyed；保存请求仍在原owner排空。到期是进程结束，不能靠提前free/generation reset制造“join已完成”。
- 可recover的DComp失败正常cancel时 **offSignalInterop仍0、Window.BeginShutdown未调用**；新ULW可以真实Start和Present。取消之后至少超过原grace再观察应用存活，以排除迟到watchdog杀新generation。

### 3.4 具体安装点与所有权

| 文件/唯一owner | 必须保护的边界 | 不扩大承诺 |
| --- | --- | --- |
| ShutdownSupervisor.h/.cpp / 本agent | Dormant monitor、已知failure激活、取消/expiry CAS、资源不可建立和cancel join失败的bounded fatal | 不复用一次性的Arm当可取消timer；不改E01/UEF语义 |
| IdtMain.cpp / 本agent | 一attempt scope、DComp失败StopProduct→旧Window.StopAndJoin、完成后取消再开下一代ULW | 正常cold Initialize阶段无deadline；return/message/gates/FLIP不改 |
| Draw3.Host.h/.cpp / root分配的唯一owner | 已知图形/presenter/exception初始化失败**先激活再打印/发布握手/释放**；Main失败join/drain用同lease；RTS失败/首帧失败路径 | Host已经false返回不等于仍活动；正常Host.Stop不加丢请求timeout |
| Window.cppm/.cpp / root分配的唯一owner | 已知失败RollbackCreation（含created抛错、class/create/bind失败）在cleanup前激活；CreateGroup false之后DestroyGroup、Start false的StopUnlocked；Main回退StopAndJoin由同scope包住 | 不能只在future.get之后加scope来漏掉promise前rollback；未返回的正常CreateWindowEx/beforeCreate不冒称已受保护 |
| Draw3.RealtimeStylus.cppm/.cpp / root分配的唯一owner | CoCreate/config/Add/Enable已经返回失败后，日志、plugin.Release、Shutdown/COM Release前激活同startup lease | 正常未返回CoCreate本身不受cold timer；producer quiescence仍需真实证据 |

RTS的Disable/Remove返回失败时，**scope不是静止证明**。不得以timer存在就清coordinator/复用输入池/HWND。C的严格失败清理需要与E03 owner确认：无法确认producer已停时转Fatal Close、保留已借出的Host/input/COM对象至退场；当前void Shutdown不能静默当PASS。若需要结果接口/failed-stop最小保护，作为C的独立输入合同增量审查，不在A/B偷改RTS。

### 3.5 C验证/真实Host和恢复点（不能用sentinel替代）

| 测试 | 实际模块/门 | 必须断言 |
| --- | --- | --- |
| scope cancel/expiry竞争 | 生产deadline class，显式barrier控制CAS前后，不随机SuspendThread | cancel赢不接受Close，expiry赢不返ULW，早/已耗deadline不重置；准备/cancel-join失败不能无保护等 |
| Window failed Start rollback/join | 私有真实windowSpecs beforeCreate false、已创建magnifier.destroyed在原回调后hold；另测created抛错的promise前Rollback | 真Window代码路径、scopeGrace→正式Close→15秒旧HANDLE结束；解除gate能正常false并全清，不假造D101返回 |
| Host graphics失败join | 第一attempt真实style/presenter拒绝，绘制owner公布失败后释放前显式hold | 最早lease已激活；不release共享GPU/HWND或detach；scope到期真实退场 |
| RTS清理失败/hold | actual Initialize结果失败及真实Shutdown点，明确Disable/Remove outcomes | 主动provider停止/回调fence分别证实；COM错误不伪报quiescent，unknown转fatal不重用generation |
| DComp→ULW反例 | 首attempt拒绝style conversion，真实Start失败返回；实际StopProduct/旧Window销毁/第二attempt allowDComp=false | **不接受Close**、唯一新HWND generation、ULW首个成功Present、FLIP不变、两DWM不尝试；wait>原grace仍活且继续输入 |
| 已Armed Close + render owner卡住 | 真产品Host/Controller帧或命令consumer gate（默认null），先确认已消费真实输入/已有durable基线，再SetOffSignal | 真实Host.Stop barrier/join可卡住但15秒退場；所有producer/资源仍留至death，不断言未保存墨迹零丢失 |
| 已Armed Close + Desktop worker卡住 | 先无fault真正Clear/保存取得UInk/index，再设置现有writeDelay60000，确认新请求进入实际Commit | 真实CloseAndDrain的存活/超时路径、15秒退出；parent用生产codec/service重新Load最后committed文件，核workspace/fileGuid/笔迹属性，不只看sentinel |
| PPT UInk已写/index未换 | 原afterUInkCommittedEvent+continueIndexCommitEvent真实worker gate（<=30s） | Close后旧index仍指向最后有效UInk，fresh生产service Load+identity/笔迹对比；孤立新版本不记已提交、不删未知文件；无Office附件 |
| 无fault保存反例 | 同真实Host轨迹和已接受Save，gate放行 | 正常Close自然drain到新durable点，不能把保存改latest-only/丢已接受请求 |

每组默认三轮；raw含scene/fault scope、准确PID/HANDLE、firstFailure/expiry/acceptedClose/original15s deadline、到达gate、成功Present、索引/UInk身份与load结果。软件成功Present和codec Load不等于光学落笔/GUI自动恢复；当前关闭的恢复入口不开放。Restart唯一新实例与UEF仍由原suite/独立GUI门分别确认。

## 4. 顺序、验收状态与后续工作

1. 独立审查本设计，先给A/B的GREEN及明确范围。任何C签名/预算另审；若Root选择分段，不能将“设计已经写出”记C实现完成。
2. 本agent仅加入授权夹具/结果分支probe，保留旧fatal顺序，交HARNESS_READY。root完整Debug ARM64并保留代表性真实wWinMain红；无授权/私有根失败必须拒绝，不能当环境假红。
3. root给GREEN_IMPLEMENT后做A最小顺序修补；root同case绿（三轮）、真实提示/自然清理反例、Headless/PptCOM/E01及独立actualdiff审查。源码冻结后再Release三架构。
4. C只读预算/接口确认→唯一文件owners串行实施→生产primitive/真实failed-cleanup红绿→正常ULW反例→真实Host/存储/codec恢复验证→独立review。最后Root更新spec/HF/ledger与人工清单。
5. 本轮所有新场景均**未实现/未验证**；无GUI、COM/driver故障注入或新构建。没有已确认现场永久死锁根因。Win7 SP1仅KB2670838、Hardware FL11.0/无FL11.0→WARP、FLIP、只DComp/ULW保持；真设备/Office/HC-H2体验的人工门不能由本机fixture升级PASS。
