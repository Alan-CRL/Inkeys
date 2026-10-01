# Research: 已知启动失败清理的可取消截止与寿命合同

- Query: 修正 E02 C 尚未获准的设计，冻结不会释放仍在使用的上下文、不会误杀成功 ULW 的最小失败清理合同，以及真实 Host/存储故障验证。
- Scope: internal；当前集成产品和真实 Window/RTS/Host/保存实现。只写本文件，不改源码、已有报告、spec、工程、Git，不构建、运行、GUI或递归派发。
- Date: 2026-09-30（Asia/Shanghai）。
- Active task: `.trellis/tasks/09-27-integration-and-release-check`。
- Status: **DESIGN PROPOSAL / NOT IMPLEMENTED / NOT VERIFIED**。需要独立 design review；原 C 的拒绝结论没有被本报告自行覆盖。

## Findings

### 1. 现状与本次设计边界

已完整读取 `startup-boundary-postcommit.md`、`startup-failure-boundary-design.md`、`startup-failure-boundary-design-review.md`、`input-closing-quiescence-postcommit.md`，并核对当前 Main、ShutdownSupervisor、Window、Host、RTS、透明 presenter、保存 worker 的实际调用点。上述报告中的旧 A/E01 待办是历史状态：当前 `SetOffSignal` 已有双失败 noreturn 接管，`PublishFatalStartupFailure` 已先发布正式退场；本研究不重新修改这些冻结单元。

当前可直接确认的范围：

- `Draw3.Host.cpp:1234–1250,1278–1297` 在 `Start(false)` 返回前已经 join/drain/detach。后面的 `StopProduct` 通常走 Host 已停止早退，不能把它描述成必定等待活动文档的最终屏障。
- 失败的绘制 owner 在 `Host.cpp:1221–1227` 释放 Controller/presenter/renderer/device；RTS 的失败 `Initialize` 已在 `RealtimeStylus.cpp:2451,2550,2560,2570–2571,2590,2605` 清理；在 Main 收到 false 后才启动时钟会漏掉这些实际清理。
- `Window.cpp:907,938,991,1004,1026` 的 Rollback 可发生在 `readyPromise` 发布前；`RunGroup:747–757` 的失败 DestroyGroup 和 `Start:191–195` 的 StopUnlocked 同样需要前置激活。`StopUnlocked:690–701` 的 owner join 不能抢占已在 DestroyWindow/用户 callback/COM 内的 owner。
- Main 的 DComp→ULW 重建位于 `IdtMain.cpp:2611–2630`。其失败日志、StopProduct、旧 Window owner join 都发生在正常第二次 Start 之前。可恢复失败不能预先占用全局 Close 或调用单调 `BeginShutdown`。
- `TransparentPresentation.cpp:801,809` 在返回 false 前已 ReleaseAttempt；自动路径还在同一 Initialize 中尝试下一 presenter。只保护 Host 返回后的释放会漏掉此范围；在第一次模式失败就让整个下一次普通初始化受旧 timer 管辖也不成立。
- 普通已 `Armed/FallbackArmed` 的 Close/Restart 继续保留现有 15 秒进程监督和正常保存排空。`Host.Stop` 内部无限等待本身不是已证实的死锁；本设计不给正常 worker 添加会丢请求的短 timeout。

没有用户现场 dump 或自然 COM/驱动永久停滞复现。确认的是调用边界和上版设计的寿命漏洞，不能把本设计写成现场画布卡死根因已确认。

### 2. 选定的最小形状：一个独立 helper，按 scope 拥有固定共享状态

建议新增一对普通文件：`Inkeys/Inkeys/Helper/FailedCleanupDeadline.h/.cpp`。它不 import CrashHandler、不引用 IdtMain/Window/Draw3 全局、不读配置/日志/磁盘、不创建设备，也不提供一般任务调度框架。只有 prepare、激活、取消、真实 join 和进程自终止。

**不继续使用 Main 栈地址作 callback context。** 每次 scope prepare 预先建立一个小的 `shared_ptr<State>`；State 不包含业务对象或指向 Main 栈的指针。Signal 按值持有同一强引用。thread 参数使用 heap 上的强引用 envelope，monitor 入口取得该强引用并释放 envelope。即使调用方 Signal 比 scope 活得久，它访问的也是旧 scope 的 Cancelled State，不能改变下一个 scope。没有可复用 singleton slot 的“先核旧 generation，后 CAS 到新 slot”的窗口。

State只包含不可变grace/静态函数指针/诊断attempt ID、几个固定原子字段、wake HANDLE和显式测试门；thread HANDLE只由scope管理，真实join后关闭。**wake HANDLE由State最后一个强引用关闭**，不在scope cancel/join时提前关闭：并发Begin可能已赢CAS但尚未SetEvent，其owning Signal仍须保证HANDLE有效。monitor持强引用，最后一次State释放必在monitor退出后；迟到旧Signal只触及旧Cancelled State。scope没有业务锁或GPU/HWND/文档所有权。每个进程各自链接一个helper实现，不靠跨module header内联全局变量合并状态。

建议精确接口如下（均为拟议接口，不可作为已存在 CLI/API 使用）：

```cpp
namespace Inkeys::Shutdown {
    // 只发布原子退场前奏，返回已发布的普通/UEF监督绝对截止，0 表示没有可复用的 tick。
    // 函数本身必须是进程寿命的普通函数；没有 void* context，没有业务等待。
    using FatalCleanupPublisher = ULONGLONG (*)() noexcept;
    namespace Detail { struct FailedCleanupState; }

    class FailedCleanupSignal {
    public:
        FailedCleanupSignal() noexcept = default;
        // 返回真正采用的绝对grace截止；empty/已Cancelled为0。
        // inheritedDeadline仅供嵌套清理复用先前截止，不得延后now+grace。
        ULONGLONG BeginKnownFailure(ULONGLONG inheritedDeadline = 0) const noexcept;
        [[nodiscard]] bool HasPublisher() const noexcept;
        [[nodiscard]] FatalCleanupPublisher Publisher() const noexcept;
        [[noreturn]] void FailUnprovenProducerStop() const noexcept;
    private:
        friend class FailedCleanupDeadline;
        std::shared_ptr<Detail::FailedCleanupState> state_;
    };

    class FailedCleanupDeadline {
    public:
        explicit FailedCleanupDeadline(FatalCleanupPublisher publisher) noexcept;
        FailedCleanupDeadline(const FailedCleanupDeadline&) = delete;
        FailedCleanupDeadline& operator=(const FailedCleanupDeadline&) = delete;
        void PrepareOrFatal() noexcept;
        [[nodiscard]] FailedCleanupSignal Signal() const noexcept;
        void CompleteOrFatal() noexcept;
        ~FailedCleanupDeadline() noexcept;
    private:
        // State 的强引用、唯一 monitor HANDLE、prepared/completed 状态。
    };
}
```

`PrepareOrFatal` **不能返回 bool 让调用方忽略失败**；`CompleteOrFatal` 也不返回“超时但可以继续”的值。destructor 对尚未完成的 prepared scope 调用同一 CompleteOrFatal；未 prepared/已正常 complete 的 destructor 才是 no-op。class 不可 move，避免正在运行的 scope 被挪走后丢掉唯一 join owner。

### 3. 唯一取消/过期仲裁与无返回不变量

建议把State的一个 `atomic<ULONGLONG> control` 作为唯一仲裁字：`0=Dormant`、`1=Cancelled`、其它高位未置位的合法值为Armed绝对grace deadline；`kExpiredBit=(1ULL<<63)`，Expired编码为 `deadline|kExpiredBit`。这样终态CAS仍唯一，Complete看到Expired也能取得原共同deadline，不依赖另一线程随后才写的字段。当前tick+grace不与0/1混淆；加法及高位范围均校验，异常值转fatal。三架构编译须静态断言此原子lock-free，不能把CRT锁偷偷带入截止路径。

1. Prepare 在普通初始化前分配 State/strong envelope、建立一个auto-reset wake event并创建一个持HANDLE的Win32 monitor。control初始Dormant；Dormant **没有cold-start deadline**。不等另外一个业务线程的ready/ack。OOM、CreateEvent或CreateThread失败立即进入本线程的fatal noreturn，不能返回去join/COM/下一次ULW。若OS创建调用本身不返回，属于尚未返回的系统调用边界，不能承诺内核级硬实时。
2. 第一个 `BeginKnownFailure` 计算tick；若传入非零inheritedDeadline，取与now+grace的较小值，过去的截止立即到期，非法保留tag/expiry高位转fatal。CAS `Dormant→absoluteDeadline`，获胜后SetEvent并返回真正采用的deadline；后续begin返回已有Armed deadline，不修改它。Expired则直接fatal noreturn，不把expiry编码当新tick返回。CAS同时发布完整时钟，避免`Arming`状态里发布时钟的线程停住后另一个线程永远等它。empty/Cancelled为0/no-op；旧Signal不复活监视者。SetEvent失败进入当前线程的fatal noreturn，不能依赖仍在Dormant等待的monitor。
3. Monitor Dormant在wake event上等待；Armed以剩余绝对时间为timeout等待同一event，再读control。激活与取消都显式SetEvent。WAIT_FAILED转fatal noreturn，不永远重试坏HANDLE。**不采用100ms轮询取消**，避免在每个成功初始化/每次presenter恢复清理后平白增加最多100ms固定等待；实际线程创建/唤醒/join成本仍须记录，不能声称零成本。
4. 到期monitor CAS `absoluteDeadline→(absoluteDeadline|kExpiredBit)`；Complete以同一control CAS `Dormant/未到期Armed→Cancelled`。Complete若已观察到clock到期，则竞争Expired，不再尝试Cancelled。取消和过期只由一个终态CAS决定。clock检查与CAS的细小调度间隔不是硬实时保证，测试按终态赢家判定。
5. Cancelled赢：Complete先SetEvent（失败直接fatal noreturn），monitor不调用publisher而正常结束；Complete用HANDLE等最多1000ms管理join。只在 `WAIT_OBJECT_0` 后关闭thread HANDLE、释放scope引用并正常返回。不能用“flag已Cancelled”“thread应该醒了”替代thread-signaled。wake HANDLE继续由State所有强引用保活；旧Signal看到Cancelled先返回，但并发Begin已进入SetEvent的反例也不碰已关闭或复用的HANDLE。
6. Expired 赢：monitor 直接转 fatal noreturn。Complete 若也看到 Expired，同样转 fatal noreturn，不能返回到 ULW/Reset。即使两个线程都进入 fatal，它们共享 State 中首次 CAS 保存的进程强退 deadline，重复调用不会延长截止或创建多实例。
7. Cancel-join timeout/WAIT_FAILED：调用方直接转 fatal noreturn，保留 HANDLE、State、仍存活业务对象及其栈。**即便取消赢了，也不能因此释放仍未退出的 monitor。** 不能 detach、TerminateThread、关闭 HANDLE 后假称已 join，或回到 caller 继续 Start。
8. Prepare 失败：即使没有 State/thread，当前调用线程仍使用函数参数中的进程静态 publisher 和固定 Win32 自终止 loop；不用新线程、heap、日志或业务锁来“补建监督”。State/envelope 的准备失败清理不是继续进入业务析构的理由。

每个 fatal 路径的末端都是同一个 helper 内部 `[[noreturn]] ForceCurrentProcessAt(absoluteTick, exitCode)`：Sleep 原剩余时间，反复 `TerminateProcess(GetCurrentProcess(), code)`，意外返回时 Sleep(1) 重试。不得 ExitProcess/DLL析构或返回。该 helper 不是 `EnforceFailedShutdownDeadline`；后者继续仅供有效普通 Arm 已 state3 的唯一 owner 使用，不被用于 scope、state1、UEF 或任意 Arm 失败。

### 4. Fatal 前奏、计时和既有退出的关系

建议清理 grace 固定 15000ms，维持上版 C 提议的保守恢复空间。它只从**首次已知失败**起算，所有连续清理步骤共用此绝对截止。若 grace 到期，再接受不可撤销 fatal Close，由当前 monitor/调用线程守新的 15000ms 进程截止。单个挂起 episode 约 30 秒；不是点击 Close 后 30 秒，也不是正常冷启动 30 秒上限。该预算仍属待独立审查的技术提议。

在 Main 新增一个局部普通函数 `PublishFatalFailedCleanupNoWait() noexcept -> ULONGLONG`，位置邻近 `SetOffSignal`，由 root 唯一修改：

- 与原意图槽 CAS `0→1`；若已有 1/2/3，保留该意图，不抢 Restart 或 UEF。
- `WindowService.BeginShutdown()` 仍只是已构造 processService 的原子关门；普通 1/2 按既有意图发布 `offSignal`；`WakeForStop` 只 SetEvent。
- 无 RequestHide 回执、日志、磁盘、堆、COM、Magnifier stop、Arm/CreateThread/CreateProcess。它不是 UI 新入口，只由已经不可恢复的失败清理接管使用。
- 新增只读 `ShutdownSupervisor.h/.cpp::PublishedShutdownDeadlineTick() noexcept` 返回已有原子 `g_fallbackDeadlineTick`，不 Arm、不判定某个 Failed 可接管。读取非零 tick 只用于取更早截止，不能冒称 helper 已正常握手。

helper 在调用 publisher 前，先保存自己的绝对强退 tick。grace Expired 使用 `graceDeadline+15000`；Prepare/Cancel-join/producer-unproven 使用首次接管 tick+15000。publisher 返回的非零现有截止更早时取 min。这样普通已经 Armed 的 Close/Restart 保留原 15 秒，错误清理不能给它重新加 15 秒。state1/UEF 也不被强行送入 E01 Failed-only API。

新 fatal scope **不靠再创建监督线程/helper**：此时原 monitor 或接管线程本身就是唯一不会返回业务清理的截止 owner。因此 publisher CAS 取得新的 Close 后，可以保持 `g_armState=0`，由本 scope 的 noreturn owner 直接守 deadline；后续正式按钮因相同意图 CAS 不会另造监督。若已有 Restart helper，它仍按精确旧 HANDLE 在死亡后最多启动一次；本 scope 新接受的 fatal Close 不启动新的实例。

建议独立退出码 `0xE143001A=cleanup-expired`、`0xE143001B=prepare-failed`、`0xE143001C=cancel-join-failed`、`0xE143001D=producer-stop-unproven`；root 在实际实现前复核未使用并固定。成功提前完成不新增强退码或固定等待。

### 5. exact callpoints / 文件所有权

| 文件 | 最小接口/触点 | 寿命与范围 |
| --- | --- | --- |
| `FailedCleanupDeadline.h/.cpp`（新） | 上述唯一 helper；独立 normal header | 实施者只负责此 helper 和冻结调用点；不 import 产品模块。 |
| `IdtMain.cpp` / `ShutdownSupervisor.h/.cpp` | Main 的静态无等待 publisher；只读 tick getter；私有 child 信封扩展 | root 唯一写入者，不重写 E01/E02 A/B，不复用旧 48B/128B packet。 |
| `Window.cppm/.cpp` | `Service::Start(vector<WindowSpec>, FailedCleanupSignal={})`；token 按值传入 RunGroup→CreateGroup→CreateWindowFor→Rollback | 普通 runtime Create 默认 empty；RunGroup startup signal 不在普通消息 loop 激活。Rollback 可改非 static 或显式传 signal，不靠全局“当前 task”。 |
| `Draw3.Host.h/.cpp` | `HostStartOptions.failedCleanup`；绘制 lambda 捕获自己的 Signal 副本 | 在已知 graphics/首帧/捕获异常失败后，**先 Begin，再日志、startupCompleted 发布和 EndDrawing/GPU 释放**；主线程在 RTS false/catch 后先 Begin 再发布 stylusDecision，失败 join/drain/detach继续同 signal。正常 Run/device-lost不受 startup scope 时钟。 |
| `Draw3.RealtimeStylus.cppm/.cpp` | `Initialize(HWND, Coordinator&, CursorSink*=nullptr, FailedCleanupSignal={})`；Impl 留安全的 owning Signal | 每个终止初始化的已知失败先 Begin 再 Log/Release/Shutdown；成功可保留已 Cancelled Signal 的静态 publisher供 shutdown fail-closed，没有 Main 借用对象。 |
| `Draw3.TransparentPresentation.cppm/.cpp` | `TransparentPresentationOptions.failedCleanup`；仅 startup Initialize 的已失败 `ReleaseAttempt` episode | 见下段；不把 Recover/Resize/普通 Present 变成 cold-start timeout，不改 FLIP/DWM/画质。 |
| `Inkeys.vcxproj(.filters)`、`InkeysHeadlessTests.vcxproj(.filters)` | 登记 helper 一次；普通 header 在 module global fragment include | root 唯一工程写入者；不拉入 ShutdownSupervisor/CrashHandler/Main，不新增测试框架或 Solution。 |

Main 顺序建议：

1. Window scope Prepare→实际 Window.Start(signal)→Complete；Start false 内部的 rollback、DestroyGroup、StopUnlocked 先 Begin；这些 cleanup 完成且 Complete 正常返回后，才入原 D101 fatal 分支。Window Start true 时 Dormant cancel，不给实际创建耗时设上限。
2. 第一 Host scope Prepare→实际 StartProduct(options.signal)。失败分支在其内部早激活；**Host Start false 后 scope 尚不 cancel**，继续原 DComp 失败日志→StopProduct→旧 Window.StopAndJoin，保持第一次失败截止。在接触日志和旧 owner join 前 Main 再幂等 Begin，覆盖返回 false 没有经过 Host owner 的早拒绝。
3. 旧 HWND 链完全销毁、Host producer/consumer 清理结束后 CompleteOrFatal。只有它正常返回且全局退出意图仍为空，才开始第二次正常 Window.Start 与 ULW Host.Start；两者各自创建新的 Dormant scope，下一代不继承旧 clock。
4. 若第一 Host 成功，立即 cancel/join其 Dormant scope，然后继续正常 Setting/Bar初始化。若已过期，Complete不会返回；不能只检查 draw3Started=true 就继续。

Window最早激活：已知beforeCreate false/throw、class/CreateWindow失败、Bind失败、created throw时，先Begin再诊断/Rollback。CreateGroup因缺owner或非optional失败返回false，以及RunGroup catch，必须在ready promise/DestroyGroup前Begin；Main Start的failed StopUnlocked仍幂等Begin。当前Main的MagnifierHost/Child是optional（2395/2410），因此明确采用与presenter相同的小episode：产品CreateGroup进入每个optional CreateWindowFor前，Prepare一个local Dormant scope并传local Signal；rollback已知失败先Begin，返回后Complete，才继续下一role。required角色继续传外层Window Signal。optional成功也cancel/join Dormant scope。missing optional owner且从未进入CreateWindowFor不需要清理或timer。这样保留optional原语义，避免一次可恢复放大镜失败给后面的普通窗口创建设置15秒上限。

Presenter明确是**必要的小增量**：只有拥有有效publisher的产品startup Initialize在每次TryInitialize前准备一个Dormant release scope（publisher从owning signal读取，无context）；默认empty的独立调用保留原行为。私有 `TryInitialize(mode, releaseSignal={}, terminalOuterSignal={})` 接收local Signal；ConfigureWindow/CreateSwapChain/renderer.Init/InitializePresenter等终止该mode的false分支，**先Begin再失败cout/return**（当前703–727），不能在TryInitialize已返回后才覆盖可能背压的失败日志。requireMode或最后mode传入outer；同处执行 `terminalOuterSignal.BeginKnownFailure(releaseSignal.BeginKnownFailure())`，内外共享一个绝对grace tick。调用者在对应ReleaseAttempt前再幂等Begin，Release完成后Complete，才进入下一种模式的普通TryInitialize。成功模式也cancel/join Dormant scope。中间模式失败而下一种可恢复时传empty outer，避免给正常ULW构造设15秒cap；最终失败的后续Host清理保持共同原tick。Recover/Resize的原调用使用empty默认参数，普通运行时恢复不被偷偷纳入startup监督。此接口在实现前由reviewer确认；不能只在Host bool返回后加钩子就声称覆盖内部Release。

**诚实的剩余范围：** TryInitialize/Renderer::Init/COM/API尚未返回的调用、其内部 local COM析构/驱动回滚、异常栈展开在外层catch之前、普通 cold启动都不在这个最小 helper 的保证内。GraphicsInitialization只在最终bool失败后由Host接管，不新增“每个HRESULT都装timer”的大改造。主承诺限定为上表显式可观察的已知失败清理；需要现场栈或具体故障证据才能扩展更低层边界。

### 6. RTS停止失败的不可复用边界

`RealtimeStylus.cpp:2624–2649` 当前 Disable/Remove失败仅日志后继续 CloseAll/释放/清指针。已有 reader/writer gate不等于所有 callback in-flight fence。这是条件性静止证明缺口，未证明现场确有 callback在return后继续。

产品带有效 owning Signal 时的最小 fail-closed 提议：

- 已加入plugin的Shutdown若Disable或Remove返回FAILED，**在可能阻塞的Log和任何CloseAllProducerContacts、plugin/stylus Release、coordinator清空之前**直接 `FailUnprovenProducerStop()`；保留原对象/引用/Host/input/HWND，当前线程不返回直到自进程结束。removedPlugin的临时强引用也留在该noreturn栈，不通过提前Release假装停止。
- 未加入plugin时允许原内部初始化失败清理，只需已激活scope保护COM Release。plugin加入、Enable失败后仍必须按上述规则检查disable/remove，不因`initialized=false`就假设没有 callback。
- 正常已Arm Close也可以到同一fail-closed，只取已发布较早截止，不新增15秒。成功Disable/Remove保持原顺序，所有实际callback成功排空的动态证据仍由E03/真机补；本设计不在每个packet加registry或重写同步插件。
- 不带Signal的旧独立调用兼容保持原接口默认值；报告限定为主产品。若未来扩大所有调用者，需要独立冻结publisher/ownership，不把默认空类型默认为产品已经受保护。

Watchdog存在不是静止证明；一旦停止结果无法确认，绝不能允许Host false返回后下一轮Reset。新测试必须证实“未确认→不返回/不Reset”，不能只断言最后进程死了就忽略提前free窗口。

### 7. 冻结的最小自动验收组（root独占build/output）

全部使用现有复制EXE、精确父HANDLE/文件身份、继承HANDLE固定POD鉴权，只启动自己的child，不使用computer-use。现有 `--shutdown-supervisor-tests --startup-failure-only <D004...>` 不包含C，不能伪写其已经支持下列场景。新 selector/信封若实现，独立版本并保留旧48B/128B语义；无授权、坏mapping/父身份/句柄必须在任何配置/窗口前拒绝。root复用现有startup child系统副作用隔离；不得有普通UI/config/env崩溃按钮。

| ID / 场景 | 实际入口与最小门 | 必须断言 |
| --- | --- | --- |
| C00 dormant/cancel/expired primitive | 同一生产helper，显式event barrier控制expiry CAS前后；owned child验证fatal | Dormant等超过15秒仍无Close；cancel胜不publisher，expiry胜不返回；重复Begin不重置deadline。旧Signal对新scope无影响。 |
| C01 prepare失败 | 仅验真child的一次性CreateEvent/CreateThread失败；OOM在同一prepare接缝注入 | 不进入业务清理/ULW，当前线程noreturn，固定tick退场；不依赖另建线程。 |
| C02 cancel join失败 | cancel胜后真实monitor在测试退出门停住；保留其HANDLE，Complete真实wait超时 | publisher后旧context/State/线程仍有效，无scope析构完成/no ULW，约接管15秒死；不能只假返回WAIT_TIMEOUT后放掉线程。 |
| C03 Host graphics失败 cleanup | 实际style/presenter拒绝产生失败；绘制owner在Begin后、startupCompleted或GPU释放前用明确gate hold | 实际Host Start等待join/handshake；grace→fatal→旧HANDLE自然死；没有detach/free/新generation。不是真实驱动挂起的证明。 |
| C04 RTS失败 cleanup | 真实RTS Initialize资源建立/结果分支，synthetic失败明确记录；Shutdown前gate，另合成Disable/Remove FAILED结果 | 前者guard在COM cleanup前；后者不Release/Reset/返回。合成HRESULT不冒充provider自然失败或真实callback残留。 |
| C05 Window failed rollback | 私有Drawpad.beforeCreate=false；另created throw；已创建magnifier.destroyed在原callback后hold | 覆盖ready promise前Rollback和failed StopUnlocked；guard先于门。release gate反例自然完整清理，永久hold真实进程截止。 |
| C06 DComp旧owner join | 首attempt实际失败已清Host，旧Window.destroyed gate单独hold | scope横跨失败日志/StopProduct/旧ownerjoin，不先占Close；停住才grace fatal，禁止第二代HWND。 |
| C07 成功DComp→ULW反例 | 首attempt所有实际style切换都被auth-only callback拒绝，确保内部DComp/ULW都失败；返回Main后完整旧Host/Window清理，再解除首次拒绝、新legacy-compatible HWND、真实ULW Start | 断言真实首StartProduct=false且到达Main重建分支，不能只令DComp失败却在同Host内ULW成功。没有Close/BeginShutdown；旧HWND无效再有唯一新generation；首个成功Present、保持FLIP/两个DWM禁用；等待超过原grace+join容差仍活并继续一笔。必须有该反例才能说scope不误伤兼容回退。 |
| C08 真实Host render owner停住后Close | 无fault先绘一笔并确认accepted/consumed/successPresent；Host ObservePresented默认空测试门在原快照发布后hold；另一受控线程正式SetOffSignal | 正常Host.Stop等待最终捕获/绘制owner，但普通原15秒退场；noreturn保留对象。不需要改metrics owner的Controller文件，也不拿GPU Present失败当render pause。 |
| C09 真实Desktop写worker停住后Close | 无fault真实Clear保存取得Committed baseline；设置现有writeDelay=60000并确认第二请求已到实际delay | 原15秒结束真实Host drain；fresh parent生产service/codec读取最后committed UInk/index，identity/笔迹一致；pending可丢，不能算Saved。 |
| C10 PPT UInk已提交/index未换 | 原afterUInkCommittedEvent + continueIndexCommitEvent，真实worker gate；无外部Office | 原15秒结束；旧index仍指最后有效version，fresh生产Load严格匹配key/binding/SlideID/revision/笔迹；孤儿新版不是durable恢复点、不删未知文件。 |
| C11 无fault/放行反例 | 同一真实Host trajectory、Save请求，gate在grace前放行；普通Close自然drain | 全部已接受请求进入Committed/Failed，无新增保存丢弃/固定等待；failed-start顺利cancel不误杀后续ULW。 |

root已有hidden pulse形态在 `HiddenWindowTest.cpp:217–240,521–540`：PostMessage真product mailbox，逐个Move等`pen.inputSequence`推进；successPresent使用Host成功计数/内容revision，不用纯逻辑提交代替可见成功。复用该轨迹，不复制modeler或强开白板/Canvas navigation等gate。

C09需要准确证明worker到了delay。当前Desktop fault只有writeDelay，Diagnostics.accepted/pending不能证明进入了`ProcessRequest:654–656`。若无现成可核对pulse，最小增量是在 `DesktopAutoSaveTestFaultInjection` 增加默认null的 `void* enteringWriteDelayEvent`，仅已有delay启用时在Sleep前SetEvent；这是worker测试证据，不改保存语义。事件由验真child生命周期拥有至worker结束或进程死亡。PPT已有两个event，直接复用，不另加索引mock。

新fresh-reader用实际 `DesktopAutoSaveService::{Start,SubmitLoad,TryTakeCompletion}` 与 `PresentationAutoSaveService::{Start,SubmitLoad,TryTakeCompletion}`/生产UInk decoder：先记录真实Committed completion的fileGuid、session/sequence、target/binding/SlideID、stroke数/端点/压力/颜色宽度/画布identity；强退后另一新service实例Load对应文件并逐项核对索引引用。PPT基线可先停页/切离场景促成真实Committed，再把已编辑的第二版本停在afterUInk事件。不要用硬编码sentinel、仅文件存在或MDMP正确替代恢复。

每类代表场景默认三轮；正常counterexample等超过grace也至少三轮。root记录failure/activation/cancel-or-expiry CAS、原Close tick、gate tick、精确child PID/HANDLE死亡、exitcode、旧/新HWND、successPresent、恢复identity及生产Load结果。parent上限后只清自己的child，记FAIL，不把父强杀计绿。Win7/真笔/Office/HC-H2体验仍独立人工范围。

### 8. 实施门与最小分批

1. 独立review本文件，先冻结helper强引用/无返回、publisher无等待、计时与module归属。当前C仍未获准，不让旧A/B的GREEN扩大到C。
2. 一个实施者独占helper、Window/Host/RTS/presenter必要触点；root继续独占Main/ShutdownSupervisor/工程/公共测试登记。metrics实施者的Controller/RuntimeMetrics和已冻结E01/E03/E02A/UI3 source保持各自所有权；本组可用Host ObservePresented门避免冲突。
3. prepare/CAS/cancel-join failure先有真实生产helper红→绿；已失败Host/Window内部门和成功ULW反例成对验证。对于新建立的保护没有自然旧版故障复现时，明确“受控门旧版超时→新guard退场”，不伪称自然driver卡死修复。
4. 加原Armed Close的render/Desktop/PPT真实停滞及fresh UInk恢复；这些case不要求重写正常drain。最后actualdiff/callchain独立review、完整Debug|ARM64、Headless/PptCOM/相关supervisor和可得Release三架构由root串行执行。
5. root按最后source身份更新spec/ledger/HF与人工清单；不自动commit/finish/archive，不把设计写出或单个helper测试通过升级为整个任务完成。

## Files Found

- `Inkeys/IdtMain.cpp:264–295,2538–2641`：普通退出与DComp重建唯一产品协调入口；GetService/BeginShutdown基础已确认是全局对象/原子关门。
- `Inkeys/Inkeys/Helper/ShutdownSupervisor.cpp:45–46,325–454,1723–1741`：一次性Arm state、已发布绝对tick及仅state3使用的Failed noreturn。
- `Inkeys/Inkeys/Window/Window.cpp:153–198,690–809,892–1118` 和 `.cppm:118`：startup promises、双owner join、promise前rollback与生命周期callback。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp:1029–1374` 和 `.h:169–184`：graphics/RTS握手、GPU owner释放、默认options及最终保存屏障。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp:2394–2650` 和 `.cppm:40–43`：Initialize内部清理、Disable/Remove/Release失败边界。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cpp:544–549,689–731,778–817`：同Host内部模式尝试和失败ReleaseAttempt。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.AutoSave.cpp:627–656`、`.cppm:98–103,118–139`：真实写延迟、diagnostics、fresh Load；现无delay到达event。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.PresentationAutoSave.cpp:1061–1066`、`.cppm:143–172`：真实UInk已提交、索引前门与保存/Load API。
- `InkeysHeadlessTests/InkeysHeadlessTests.vcxproj:137–138`：独立编译Window，未登记ShutdownSupervisor/Main；新helper必须独立链接。

## Related Specs

- `.trellis/workflow.md`、当前子任务PRD/design/implement：研究持久化、实现前规划和独立审查。
- `.trellis/spec/native-desktop/errors-logging-and-resources.md`：正式退出先Arm、Failed-only原截止接管、正常drain/最后durable点、资源逆序与借用寿命。
- `.trellis/spec/native-desktop/draw3-integration.md`：唯一Host/RTS、独立device、显示前DComp重建、两窗、FIFO保存屏障、功能gate。
- `.trellis/spec/native-desktop/input-and-ink.md`：Reset前全部producer/consumer静止，ClosingDiscarded单释放，不能以watchdog代替callback quiescence。
- `.trellis/spec/native/quality-and-validation.md`：真实生产模块、故障/成功反例、不同架构与Win7证据分开。

## External References

本轮没有新增外部资料或依赖/API。只选当前代码已使用、目标Win7可用的CreateThread/WaitForSingleObject/GetTickCount64/Sleep/TerminateProcess与固定原子；不引入新SDK/KB/API。用户确认Win7 SP1仅KB2670838下FLIP_SEQUENTIAL可用，保持此合同；Hardware/WARP、两个DWM禁用、仅DComp/ULW不因清理监督变更。

## Caveats / Not Found

- 全部C实现/测试为待做；本研究没有写代码、构建、进程操作或GPU/COM/IO故障注入，没有当前HF或PASS输出。
- 失败清理grace=15s和约30s总范围是技术提议，尚待独立design review；用户已批准的接受Close后15s不被改成30s。
- 低层调用尚未返回、驱动/内核不调度、自进程TerminateProcess异常不能当本helper可保证的硬实时范围。已知失败scope也不是一般冷启动总时限。
- 成功Disable/Remove的实际callback排空与真设备故障仍需证明；条件性failed-stop fail-closed不能冒称已找到现场Reset race或完整callback计数。
- Recoverable presenter/optional-window episode必须完全cancel/join后才进入下一普通初始化。上述拆分是本提案的一部分；若实施范围缩小，需重新审设计并明确下层未覆盖，不能偷偷给整个fallback设cap。
- 文中必要签名/退出码/新测试模式未存在于当前产品；只有独立审查和root冻结后可实施。行号为本次源码读取位置，后续修改可能移位。
- research角色隔离未加载implement/check JSONL；输出只在约定的当前任务research文件。
