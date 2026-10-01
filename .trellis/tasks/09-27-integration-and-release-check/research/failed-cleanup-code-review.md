# E02 C-P1 FailedCleanupDeadline 实际代码独立复审

日期：2026-09-30。Active task：`.trellis/tasks/09-27-integration-and-release-check`。本 reviewer只读冻结合同/implementation、两helper完整293行、Main静态publisher/getter、96B caller/auth和root红绿raw/status；未改产品/工程/spec/账本，未构建或运行EXE/GUI，不递归派发。只写本报告并同步自有harness安全增量。

## 结论

**C-P1实际代码GREEN；十类已定义primitive及本机Debug ARM64门通过。** 无静态阻断。可以据此继续另行批准的C-P2接线；C-P1不证明真实Host/Window/RTS/presenter/保存worker停滞或UInk恢复。

| 文件 | 本次核对SHA-256 |
| --- | --- |
| `FailedCleanupDeadline.h` | `1D8DB0010A388CC21522F1D0F9CC7858CB8D59DFCB928431C6C3112A94AAD0A0` |
| `FailedCleanupDeadline.cpp` | `95932CE0ECBCEE120F884FD834A9A72E3A85479A296420CD6D87BF9239F37F4C` |
| `ShutdownSupervisor.cpp`（含新C00 caller） | `D7E6B60E494339FCC31C58B3B45E1044604F0F2CD9F7B21159913108E6CB11D9` |
| `IdtMain.cpp`（publisher/D005观察） | `FA9282CE30B381696001F5A4CDAEA8752D1C16C317491D72775C6DFD921F3BF8` |

两helper符合PATCH_READY身份，当前C-P2调用点未接。源内容直接核对；本报告不以implementation的表述代替代码。

## Findings (fixed)

本 reviewer未改产品；没有机械修补。先前harness的grace/原绝对tick、重复Begin同tick、ordinary掩盖min、旧Signal无nextScope/竞争门等问题已由root实码补齐，本次按真实路径与输出核对；见下面C00安全和测试证据。

## 实际寿命/时钟路径

- **wake最后引用与Signal保活**：Detail::FailedCleanupState持不可变publisher/test gates、control/fatalDeadline和wake；析构才CloseHandle(wake)。scope、Signal、MonitorEnvelope及MonitorThread移动取得的shared_ptr各自拥有引用，State不存Main/Host/业务栈地址。BeginKnownFailure首句复制自己的局部强引用，覆盖CAS→测试门→SetEvent。Complete即使先cancel/真join/reset scope，正在Begin及迟到旧Signal仍保活旧wake，不触及下一scope。
- **Dormant与唯一仲裁**：control初值0 Dormant、1 Cancelled，其余合法低高位值是绝对grace tick，Expired把同一tick OR高位。64位atomic is_always_lock_free编译断言已通过当前ARM64。Begin首次CAS一次发布完整tick，继承tick只能取更早；非法tag/溢出fatal，重复Armed返回原tick。monitor Dormant无限event等待，激活后按原剩余时间wait，没有cold-start deadline或正常取消轮询。
- **Complete不取消已观察过期**：Complete持局部强引用，control已有Expired直接FatalExpired；观察GetTickCount64>=Armed后只ClaimExpired，竞争失败重读control。未到期/Dormant才同字CAS Cancelled。没有分开发布tick的Arming阶段，也没有另一个可改写终态的flag。小的clock读→CAS调度间隔按同字赢家判定，不声称内核硬实时。
- **真join失败不释放**：Cancelled后SetEvent(wake)，WaitForSingleObject(monitor,1000)必须WAIT_OBJECT_0，CloseHandle成功才将monitor置null、completed=true、释放scope State。timeout/WAIT_FAILED/唤醒/管理handle错误先FatalNow，未close活monitor、未reset State/返回caller。没有detach/TerminateThread。scope禁止copy/move，Prepare/Complete只供单管理owner串行；跨owner仅传Signal。
- **OOM/准备失败无新依赖**：null publisher、合成/实际make_shared异常直接FatalNow(nullptr,publisher)。State存在后的CreateEvent失败、nothrow envelope为空、CreateThread失败也在当前线程noreturn。失败envelope/wake/State保留到死亡，不为收尾再分配/建监督；无State路径仅存栈tick和已保存函数指针。MonitorEnvelope只在成功monitor入口移动strong ref后删除。
- **所有wait/event错误**：Begin/Complete的SetEvent失败进入001C；monitor wake wait仅接受OBJECT0/TIMEOUT，其余fatal。WaitTestGate SetEvent/Wait错误同样fatal，默认gate为空不等待。真实Cancelled monitor在返回前才进入cancel-exit测试门；before-expiry门只影响monitor，Complete不受门阻塞。
- **fatal tick不可延长**：FatalAt先SaveEarlierDeadline CAS own tick，再调用publisher，再只缩短共同tick；每个fatal owner按共同最早值守截止，50ms重读只发生在fatal阶段。FatalExpired从control内原grace算+15000，迟到不从publisher重新起时。Prepare/join/producer-unproven从首次接管now+15000；共同状态重复调用取min。无State也先保存own tick，再取publisher更早值。
- **最后动作与边界**：ForceCurrentProcessAt仅GetTickCount64/Sleep/TerminateProcess(GetCurrentProcess)，异常返回Sleep1重试而不返回析构/业务。001A/B/C/D分别expiry/prepare/管理失败/producer-stop-unproven。未返回OS调用、publisher违约、进程永久不被调度不由普通用户进程保证；当前实际publisher符合无等待合同。
- **publisher/getter与nullptr**：Main299只Interlocked意图CAS、既有processService原子BeginShutdown、普通1/2 offSignal发布、WakeForStop和既有deadline原子read，无Arm/日志/业务锁/隐藏入队。UEF3和既有Restart不被抢写。empty Signal.Begin返回0、Publisher返回null；但FailUnprovenProducerStop(empty)仍以001D当前线程noreturn自守，不返回清理。有效旧Cancelled Signal的HasPublisher仍true，可供后续停止失败使用进程寿命函数；scope(nullptr).Prepare视非法准备，以001B自守。C-P2调用者必须仅在停止证明失败时使用该显式noreturn接口，不能把空Signal默认为产品已保护。

## C00新版caller运行前安全增量

**CLEAR_SAFETY**，含新有限begin-cancel/expiry-cancel和实际独立nextScope，身份为上表D7E6而非旧8EE。仍是96B purpose/继承三个exact HANDLE/私有文件身份和永不落普通Main的路径；fault/事件只在鉴权后建立，没有GUI/Office/配置启动。

begin-cancel将Signal强引用放进只写一次的静态对象，不传scope栈给CreateThread。producer停在实际Begin CAS后；owner先Complete真join monitor，再放continue门，真实wait producer HANDLE signaled后才读取见证并清空静态Signal。producer未join直接自Terminate87且不析构局部handles/State。事件声明在scope之前，保持到全部join/死亡；未进入暂停或失败都不是绿色。

expiry-cancel的monitor停在到期CAS前，owner等到真实reached事件后调用Complete。当前helper独立赢Expired并noreturn，保留Gate/State/stack；原grace+15000和afterComplete0验收，不以释放门后的线程退出假称旧scope安全。cancel则实际建立第二个State/scope并激活，再核旧Signal仍0、下一tick不变及两个scope真join后超过16秒不被旧timer结束。

## 原始结果核对（root执行，reviewer读取）

`TestResults/release-hardening/c-p1-ui3-b1-green-debug-arm64-build.status.txt` exit0；log 0Error/7Warning、39.65s。strict `ui3-b1-green-debug-arm64-headless.status.txt` 为 **exit0 pid24596**，stderr空、216layouts failures0/PASS animation。没有采用口头旧pid23160。

十组 `c-p1-<case>-green-debug-arm64.{stdout.log,status.txt}` 均父自然exit0，目标child dead1，授权负例各84/83且零activated/published/durable；未把parent强杀计绿。

| case / child PID | 实际证据 |
| --- | --- |
| expiry / 28024 | grace-activate15000ms；从原grace到死亡15047ms，总30047ms；001A，witness1/afterComplete0。 |
| allocation / 31096 | 无State合成失败、published1/afterComplete0；publish→death15031ms，001B。 |
| monitor / 31084 | 合成CreateThread失败、published1/afterComplete0；15031ms，001B。 |
| join / 27440 | 真实monitorPaused1；childStart→publish1016ms含prepare/1000ms join管理；publish→death15094ms；001C，afterComplete0。 |
| ordinary / 31320 | 原deadline11153656，death11153671；0016/原tick+15ms。这是原监督集成反例，未冒称helper独立取min。 |
| cancel / 12592 | 两scope nextDeadline11169296、lateSignal witness1、afterComplete1、普通0，超原activate16s存活。 |
| dormant / 32204 | activated0/published0、afterComplete1、普通0，未失败等待16047ms。 |
| earlier / 27108 | auth-only合成existing11194000；死亡11194031，6s+31ms，001B；未Arm普通监督。 |
| begin-cancel / 21868 | 暂停1/activated1/afterComplete1/witness1、普通0，16047ms后退出；覆盖CAS后旧wake保活。 |
| expiry-cancel / 29444 | 暂停1/afterComplete0；原grace11226578→death11241593即15015ms；001A，证明Complete独立接管。 |

动态证明限受控测试门和当前机器；无自然驱动/RTS卡死复现。sentinel durable1只是私有已Flush文件不变，不是UInk/index fresh Load。

## Findings (not fixed)

- **P2，调用点未接**：C-P2的Host失败日志/GPU释放/RTS清理、Window promise前rollback/旧owner join、presenter局部ReleaseAttempt和正常ULW成功反例仍未实施。本primitive不能宣布产品清理已全部有界，尤其RTS Disable/Remove失败目前的真实释放顺序仍待接线。
- **P2，其它故障分支运行缺口**：目前有allocation/monitor/join合成与竞争门，未逐项注入CreateEvent/envelope/SetEvent/wake WAIT_FAILED/null FailStop等路径；实码均noreturn保护。后续触碰对应资源路径时补针对性验证，当前没有发现源码错误，不为枚举所有OS失败扩写框架。
- **P2，平台/完整门**：本机Debug ARM64证明atomic/link，Release/Win32/x64和Win7实机仍需root后续门；不把无GUI primitive当真实RTS callback排空、Host render/save/存储或恢复证据。

## D005观察字段补充

已读 `e02-D005-real-result-natural-debug-arm64` status exit0/pid26972，child7096 actual_result S_OK、普通1、Arm→death875ms；hold status exit0/pid21984，child27736 S_OK、0015、15032ms；各授权负例PASS，stderr空。这关闭“合成ERROR_GEN_FAILURE冒称真实初始化结果”的观察缺口；Main仍先锁存actual result，仅用于授权包，正式fatal Arm/error日志/提示/清理/return未变。不是自然DLL失败或UInk恢复。

## Verification

- Lint：未运行独立linter；本reviewer只读源码/记录，无产品修补。
- TypeCheck/Build：root完整Debug|ARM64 **PASS**，上述status/输出已核；本reviewer未重建。
- Tests：十类C primitive及strict Headless由root自然 **PASS**，如表；本reviewer未运行EXE/GUI。
- B1同构建的默认off合同另有修补待复验，不以本C结论替代其GREEN。C-P2/spec/最终HF仍由root另行完成。
