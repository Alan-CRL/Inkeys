# E01：双监督建立失败后的截止退场设计

日期：2026-09-30。Active task：`.trellis/tasks/09-27-integration-and-release-check`。负责人：`shutdown_failed_arm`。仅设计；生产与测试尚未修改，等待主会话 `GREEN_DESIGN`。

## 事实、缺口与范围

- 基线 commit `e32a5fc06096c1e4ab88a29866c60ebe323fd722`；当前共享工作区的任务记录修改由其他 agent 所有，保持原样。
- 真实链路是 `SetOffSignal(1/2)` → `offSignalInterop` 首次 CAS → Window `BeginShutdown`（原子显示终态门）→ `ArmShutdownSupervisor` / `ArmCore` → 当前 tick + 15000 → `CreateThread(FallbackDeadlineThread)` → 同 EXE helper 创建/握手。两个方案均失败时 ArmCore 返回 Failed；SetOffSignal 当前继续进入窗口隐藏入队、logger、CrashHandler shutdown、Magnifier stop，之后主线程可能在正常 drain/join 阻塞。
- 首次 CAS 已被占用，后续按钮不会重建监督。此处是已确认的条件性 liveness 缺口，不等于发现每个无内部 timeout 的正常 drain 都是死锁。
- `g_armState` 是 ArmCore 的唯一事实源：0 未 Arm、1 创建中、2 helper已握手、3 两个方案均失败、4 自身deadline已建立。现有低层 CAS 路径在观察到 1 或 3 都会返回 Failed，所以**单看 Failed 枚举不能泛称“没有任何 helper”**。本接管的窄前提是普通 SetOffSignal 首次胜出、使用合法 default 参数、它所发起的 ArmCore 已经结束为 state3，且已有非0原始 deadline。普通 SetOffSignal 1/2 与 UEF 3 先争同一个 offSignalInterop，只有胜者才 Arm；当前所有正式调用链不会在两个合法 owner 间重复 Arm。低层测试的主动并发 Arm 可达性与此不同，不为假设扩大 enum/锁/仲裁。
- 仅修 E01；保持 Armed/FallbackArmed 的正常排空和已接受保存请求语义。Host、Window、Setting、UEF、项目文件和发布宏不变。共享规范/父账本由主会话更新。

## 最小生产修改

1. `ShutdownSupervisor.h/.cpp` 增加 `[[noreturn]] void EnforceFailedShutdownDeadline(Intent intent) noexcept;`。只供上述 state3/合法 deadline 的首次普通 owner 调用，不是可以随意拿去处理 state1 的通用 Arm 恢复 API，不把它描述为已经成功 Arm。函数/调用点写明前提；现有成功 Arm 路径、枚举和 crash arbiter 不更改。
2. 实现只读取已有 `g_fallbackDeadlineTick`、`GetTickCount64`，在**当前调用线程**按剩余时间 `Sleep`，到绝对截止执行 `TerminateProcess(GetCurrentProcess(), exitCode)`；无需新线程、helper、事件、堆分配、文件 I/O、logger、业务锁、COM 或 C++ 析构。已经过截止立即终止；无合法截止（0）立即安全终止，不重新加 15 秒。若自终止意外返回失败，留在 Win32-only 的有限 Sleep/重试循环，绝不返回业务清理。
3. `IdtMain.cpp::SetOffSignal` 只改 E01 调用点：Arm 后保存 Win32 error，发布 `offSignal`；**仅 Failed 分支**先同步 `WakeForStop`（目前仅 SetEvent，无业务锁）并立刻调用上述函数。分支放在隐藏命令入队、异步日志和其他业务清理前。成功路径的原有“offSignal → 隐藏入队 → WakeForStop → 日志/清理”逐句顺序不移动。
4. **上述实际 state3 路径**不给新实例作成功承诺：两个方案均已失败/失败创建的 helper 已被本次 Arm 清理，不能在旧进程存活期间另尝试普通实例。Close 使用专用 `0xE1430018`；Restart 使用 `0xE1430019`，明确表示“强制退出，重启监督未建立”。采用固定退出码提供可复核结果，不在唯一剩余截止线程上依赖可能阻塞的日志。restart_count=0 只断言在本次明确 state3 的 double-failure 隔离测试，不扩展成任意低层 Failed/state1 或 OS无响应情形的保证。

## 时间与资源边界

- 继续使用 ArmCore 在创建任意监督之前保存的同一个绝对 tick；CreateProcess/握手失败已耗费的时间不重复计算。测试不能从 Failed 返回时重新起算 15 秒。
- Thread、process、attribute-list、堆等资源不足导致两个方案返回失败后，接管不再消耗这些资源；`SetOffSignal` 的本地原子发布与 Win32 SetEvent 保留。
- 不能证明 OS 内核自身挂起、当前线程永久不被调度、尚未返回的 CreateProcess 内核调用在“所有监督均无法创建”情况下也有硬实时界限；本修复关闭的是**已返回 Failed 后继续无界业务清理**。这些 OS 边界独立记录，不声称一般进程可抵抗全部系统故障。
- 普通成功 Arm 的正常 drain 仍可完成最后保存；双失败时当前调用线程专职截止，其他已经工作的线程可按退出标志尽力收尾。15 秒届满可能丢未 durable 请求，按用户既有决定执行，不把强退写成保存成功。

## 实际生产入口红→绿测试

测试只在现有 `--shutdown-supervisor-tests` 内增加私有 copied-child 场景；为节约回归时间可允许 `--shutdown-supervisor-tests --failed-arm-only`，其含义仅为运行这些隔离测试，不是产品故障开关。

- 复用现有 `kRealUefTestChildMode` 的 **copied-child 授权信封**与 `MakeUefTestDirectory`：新增 failed-arm 选项由 **argc9** 信封携带明确的 launcher EXE 绝对路径及固定 observation mapping HANDLE（旧 UEF argc6/7 不变）。在既有 inherited parent/ack HANDLE、PID/继承标记、隔离目录、copied EXE `directory\\bin` 校验后，**新选项再验** parent HANDLE 的 QueryFullProcessImageName 与该 launcher 路径用 SameExecutableFile 的 volume/file-index 完全同一；copied root、bin 目录和 copied EXE 各自非 reparse，当前 image 恰为 bin/Inkeys.exe 的实际文件。不得沿用“parent 路径包含 Inkeys.exe”作新选项授权证明。通过后才设置 fault 状态；不新增通用 framework、公共 setter 或普通产品配置项。无 inherited handles 的直接调用须拒绝并在 GUI 初始化前结束。
- 测量 packet 是新场景专用 pagefile-backed mapping，Arm前创建/映射、以显式 handle-list 第三个句柄继承；child 先核整数/uintptr边界、HANDLE_FLAG_INHERIT、MapView成功及 magic/version/size。固定48字节、DWORD/LONG/ULONGLONG，无指针/容器；static_assert大小与字段offset覆盖Win32/x64/ARM64。Arm wrapper只填原始 deadline、Failed tick、result/error/state后InterlockedExchange发布，**没有临界路径文件写入/堆/日志/锁**。parent用Interlocked读取published（release/acquire边界）后读取一次，mapping在child死亡后仍由parent持有，过期接管立即退场不会丢计时。sentinel在Arm前Flush，cleanup-entered只在旧生产入口返回后写；mapping不是产品必需资源。
- 新 fault 只在该已验真的 child 内作用于 `ArmShutdownSupervisor`：同时模拟 fallback CreateThread 失败，并分别模拟 helper CreateProcess 失败 / bad-pid 握手拒绝；普通产品、现有 UEF 和旧场景默认值不变。
- child 从 0 意图调用真正的 **`SetOffSignal(1/2)`**，不是复制 CAS / 清理算法。Failed 返回的老源码会回到 child 的“业务清理已进入”marker 后 `Sleep(INFINITE)`；父进程 20–25 秒上限结束自己持有的精确 child HANDLE并判 FAIL，形成红证据。修补后的真实 SetOffSignal 不返回，该 marker 不出现，旧 HANDLE 约原始 15 秒 signaled，专用结果码正确。
- 父进程记录 ack/请求时刻、published observation 的 Arm result/error/state3/deadline/Failed tick、结束时刻/exitCode、是否进入清理、restart marker count。Close/Restart × create/handshake 共四项；create 场景至少做两项老源码红测，绿测覆盖四项。另加 Close-create 两项授权 child：在 ArmCore 保存原始15秒deadline后、确认双失败前分别延迟 6000ms（已消耗）和 16500ms（已过期），然后真实 SetOffSignal 调用实际 guard。前者总时长仍约15秒且接管剩约9秒；后者 Failed 到结束立即/有界，而不是再次等15秒。后者只证明已过期接管，不伪称无监督的模拟 Arm创建阶段16.5秒也符合15秒界限。
- 重启双失败应 `restart_count=0` 且 `0xE1430019`，不能期待一个无 launcher 的正常新进程。既有 natural/forced Restart 和 duplicate suite 继续证明监督成功时的唯一新进程，不被此修补破坏。
- fault 前在私有目录写入并 Flush 一个 durable sentinel，父进程退出后核内容保持完全一致，并核未进入清理。此处证明不覆盖已提交文件；**sentinel 不是 UInk codec/index 或新 GUI 可见恢复验收**。实际 Host 保存 worker 停滞 / 最后 UInk 点由 E02/E03 独立测试负责。
- 一项 failed-arm 无继承句柄/错误 parent file identity 拒绝测试；不触发实际退出。延迟测试仅由已验真子进程启用，ArmCore参数严格限定在私有 testDirectory 且双重失败选项内；默认0，最多17000ms，真实产品无开关。测试用真实 guard，不复制另一份计时实现。

## 顺序与交付

1. 主会话审本设计并返回 GREEN_DESIGN。
2. 只加隔离 test harness，不修生产 Failed 分支；等 BUILD_SLOT 运行完整 `InkeysRepo.sln Debug|ARM64` 和 targeted red。保存 stdout/stderr、命令/exitcode与精确 child PID。
3. 加最小生产函数/调用点；同 slot 完整 Debug|ARM64、targeted green、既有 supervisor suite、严格 Headless。Release/三架构由主会话串行安排；不得抢共享输出。
4. 交 `failed-arm-deadline-implementation.md`、真实 diff、编码/换行检查、red/green路径、未验证项目和剩余风险；主会话独立 trellis-check 后更新规范和最终账本。不 commit/push/archive/结束任务。

## 已读取上下文

implement.jsonl 的四个实际路径、prd/design/implement、父 handoff/completion清单、post-commit-completion-audit、根 AGENTS、native-desktop build/errors/cpp/index、native quality-and-validation（原派发所写 native-desktop 路径不存在）、guides/reuse/cross-layer。已执行 get_context --mode packages（单仓库，无 package）。旧规范的 Failed 风险是本次待关闭缺口；最后更新由主会话负责。
