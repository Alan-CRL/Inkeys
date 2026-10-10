# E01 双监督失败截止设计：独立实施前复审

日期：2026-09-30（Asia/Shanghai）。Active task：`.trellis/tasks/09-27-integration-and-release-check`。本 reviewer 已重读真实 check.jsonl、其引用文件、PRD/design/implement、E01 设计及相关源码。用户后来授予窗口测试与这次 commit 权限，原“不提交/不启动 GUI”条款按该授权覆盖；本次只读设计复审，不构建、不测试、不启动进程、不改产品或实施者设计，仅写本报告。

## 结论与 GREEN 条件

**设计方向 GREEN；当前文案还需三项明确调整后才发 `GREEN_DESIGN`。** Failed 后让当前调用线程复用原始绝对截止并只用低层 Win32 强退，能关闭“所有监督建立失败后又进入无界业务清理”缺口，无需另建线程、调整正常 drain 或开放产品测试入口。最新设计已修正成功分支的原有顺序，本项不再阻塞。

剩余三项是：明确新函数只用于首次普通意图 owner 的有效 Arm 失败而非任意低层 `Failed`；为新增 Failed 测试选项补足精确父/复制EXE身份校验；至少增加一个确定消耗/过期原截止的测试，使“不能重新加15秒”成为能被测出的门。三项可以在本次最小范围内完成，不需要新产品决策。

## 已核对的真实调用链

### 截止接管之前没有新增业务等待

- `SetOffSignal` 首次 CAS 成功后调用 `Window::GetService().BeginShutdown()`；`GetService`（`Window.cpp:2132–2135`）直接返回在 `:140` 预先构造的全局 `processService`，不是本调用中的 lazy singleton。`BeginShutdown:211–215` 仅原子 store，未拿 owner/lifecycle lock。
- `RenderPipeline::WakeForStop`（`RenderPipeline.cpp:896`）转 `Scheduler::WakeForStop:742–745`，只有检查已有 impl/event 并 `SetEvent`，未创建事件、分配堆、进业务锁或等待渲染线程。
- `offSignal.store` 与上述唤醒可以放在 Failed-only 分支接管前。`RequestHideAllUserWindows`、IDTLogger、`CrashHandler::Shutdown`、Magnifier stop 均应在接管分支之后，Failed 时不会执行。新 `EnforceFailedShutdownDeadline` 内也不得调用这些函数、CRT日志、路径解析、I/O、COM、`ExitProcess` 的DLL析构链或新线程/进程。
- 成功路径必须继续逐句保持 **offSignal → RequestHideAllUserWindows → WakeForStop → 日志/清理**。设计最新版第3项已明确这一点，不能按较早版本把全部 Wake 提前。

### 时间原点与边界

- 当前 `ArmCore`（`ShutdownSupervisor.cpp:296–301`）在创建 fallback/helper 前保存 `GetTickCount64()+deadlineMilliseconds`，握手/创建返回 Failed 时仍保留该 tick。接管读取同一 tick，不能从调用接管函数、Failed返回或日志时间再加15秒；已经超过截止立即尝试强退。
- 原始 tick 在 SetOffSignal CAS 与 BeginShutdown之后取得，因此是**本次有效Arm开始的截止**，不是用户按钮的光学点击时刻。前序当前只有有界CAS/原子访问；设置页 `Hide()` 前奏、确认框等待，以及尚未返回的OS内核调用不在本次修补的硬实时证明内。最终报告和计时容差应写清，不把ack到退出误称光学点击到退出。
- `GetTickCount64`、`Sleep`、`GetCurrentProcess`、`TerminateProcess` 已在当前 fallback 使用，没有引入需要额外Win7补丁的API。绝对tick读取使用既有原子；最终三架构编译要确认所用原子实现不增加业务锁/堆依赖。
- 若 `TerminateProcess` 自终止异常失败，低层有限间隔重试且不返回清理是可接受的极限保护；OS永久不调度线程、内核挂起/无法完成自终止属于不可由普通用户进程消除的资源/系统边界。不得宣称对这些也有硬实时保证，亦不应因此无限扩写新看门狗层。

## 必须调整一：明确 state1/state3 与普通/UEF 仲裁

| 当前低层状态 | ArmCore重复调用结果 | 正确解释 |
| --- | --- | --- |
| 1 创建中 | Failed | 另一个低层调用还在建立，不证明两方案已失败 |
| 2 已握手 | AlreadyArmed | 原helper合法存在，不能另起接管launcher |
| 3 全失败 | Failed | 第一有效调用已返回，没有已建立监督 |
| 4 自身兜底 | FallbackArmed | 原始截止线程合法存在 |

当前 `SetOffSignal(1/2)` 在 `IdtMain.cpp:260–261` 先赢 `offSignalInterop` 的0→1/2，重复普通请求在Arm之前返回。UEF自动/手动在 `Helper.CrashHandler.cpp:479–480,660–662` 经 `TryClaimCrashRestartIntent`（`ShutdownSupervisor.cpp:1103–1108`）争同一个slot的0→3，只有赢家进入 `ArmShutdownSupervisor(CrashRestart)`。

所以在目前正式调用者中，**不存在已证的“普通SetOffSignal进入Failed时，另一个合法UEF helper仍在建立”的双winner竞争**。`state1→Failed` 是导出的低层API在不遵守共同意图仲裁时可达；内部旧测试也有直接 `ArmCore` 调用，不能把它们混作正式普通入口。无需为该不存在的正式竞争重写仲裁。

设计应明确：新noreturn函数只供已接受普通Close/Restart的首次owner、有效Arm已完成并失败的调用点；不得把任意 `result != Armed` 当双失败，也不得把它作为UEF/任意重复Arm的通用处理器。补充面对不符合该前置条件的 `Failed`/零tick 的fail-closed解释，不重新起截止或承诺没有任何并发launcher。现有UEF报告、确认、mode1 CAS输家和CrashRestart分流保持原样。本次不以普通E01绿灯宣布UEF所有资源耗尽边界关闭。

## 必须调整二：新增故障选项授权不能只复用旧弱子串检查

现有 `RunRealUefTestChild:896–914` 已核 inherited parent/ack HANDLE标志、精确PID、ownImage目录等，但父路径条件只是 `parentImage.find(L"Inkeys.exe") != npos`。`IsTestDirectory:233–265` 只检查最终目录非reparse及目录标记，并非所有路径分量的身份证明。不能将原样复用称为新的“严格父文件身份/owned copied child”认证。

对新增四个Failed选项，在启用fault之前至少补齐：

- 精确有效继承process HANDLE/PID/存活身份与ack event；句柄错误、非继承、self PID、未知选项立即拒绝，在GUI初始化前退出。
- 父EXE完整路径/精确basename与声明的源EXE身份相符，使用现有 `SameExecutableFile` 等真实文件身份检查，不能接受含 `Inkeys.exe` 的任意子串。复制源与child必须来自本次允许的测试安装/仓库边界，父路径核对不靠未验证工作目录。
- child当前EXE确为规范化私有根的 `bin\Inkeys.exe`，根和bin不是reparse，检查必要的路径边界；不把任意同前缀目录当自有数据，也不清理未知文件。
- 新fault状态只在通过这些检查的Failed child分支设置，无产品env/config setter；旧UEF默认选项可以保持原验证/行为，额外严格检查限定新增fault选项，不为E01静默扩展其它行为。

负例至少包含无继承句柄拒绝；建议同时覆盖错误PID/parent身份和错误copied路径。所有核验/复制/测试sentinel准备在fault与实际 `SetOffSignal` 之前完成，不污染Failed后的低层接管。

## 必须调整三：计时测试必须能发现deadline重置

当前四格 `create/handshake × Close/Restart` 能测真实入口不返回清理、退出码正确、无launcher时restart_count=0，但helper创建被直接禁止或bad-pid很快退出时，Failed前只耗极短时间。即使错误实现“Failed后再Sleep15秒”，这些格子也可能落入宽计时容差，无法证明复用原tick。

因此把“确定耗费原始截止”的一项变成必选：在已授权child内、原tick已发布后、无fallback/helper的故障路径固定耗费足够时间，或让同一次真实deadline已过后再进入接管；父进程必须观察**原tick/原请求时间**与退出时间，容差窄到重新加15秒必败。不要在测试里复制正确睡眠算法，接管仍调用真实生产函数。

测试中的Arm result/error/tick观察应使用明确测试范围的低成本状态，不能在Failed生产分支接管前插logger/磁盘marker再称“无阻塞”；清理进入marker只在旧版真实 `SetOffSignal` 返回后产生，绿色noreturn不得触达。父测试红灯超时的外部强杀只是清理自己的child HANDLE，不能计成生产强退成功。

## 可接受的行为与明确限制

- Failed-only同步接管会占住当前调用线程直到原截止；其他线程只能按已发布offSignal尽力收尾。用户已经选择到期无条件强退并接受丢未durable请求，不需要重新请求该产品选择。成功Arm路径仍完整正常drain。
- Failed路径在接管前不入业务hide队列，因此窗口可能直到进程结束才消失；BeginShutdown阻止迟到显示，不能把它写成即时Hide成功。不得为了隐藏回执再增加可能卡住的业务等待。
- 无已握手launcher的双失败Restart只能保证旧进程退出，不能凭当前线程把新GUI启动成功。专用 `0xE1430019` 和restart_count=0属于诚实降级；已成功监督的自然/强制Restart仍由原suite保证唯一新实例，不改变其合同。
- durable sentinel不变只证明测试没有覆盖该文件，不能冒充最后UInk/index或GUI可见恢复；真实Host render/保存worker卡住与启动故障注入分别由E02/E03负责。
- 本设计不解除Host Start未Arm清理、Controller页交错、真实Win7/笔/Touch等其它门禁，也不打开发布宏/功能gate。

## GREEN 发放条件

实施者把以上三项写入设计后，可直接 `GREEN_DESIGN` 进入仅test harness的红测阶段。无需重新设计成功路径或扩展成第三套监督框架。正式修补之后仍须独立代码复审，核查实际diff与真实入口红→绿、既有suite/严格Headless、三架构完整Solution、编码/换行及更新后的spec；本设计GREEN不等于实现/自动化通过或可以发布。

本次验证：只读源码/设计/context核对。Lint/TypeCheck/Build/Tests：未运行。仅本报告被写入，未改产品、设计或共享账本。
