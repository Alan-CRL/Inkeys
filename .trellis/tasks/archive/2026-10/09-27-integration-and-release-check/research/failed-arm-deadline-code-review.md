# E01 双监督失败截止：独立实际代码复审

日期：2026-09-30（Asia/Shanghai）。Active task：`.trellis/tasks/09-27-integration-and-release-check`。只读复审真实 `git diff HEAD`、调用者、check.jsonl引用/context、PRD/design/implement、E01设计/设计审查/实施记录及既有red日志；本reviewer不构建、不运行测试/GUI、不改产品/设计/实施者报告/共享账本。仅写本报告，构建槽归主会话，任务保持in_progress。

## 审查结论

**当前实际源码的E01静态复审GREEN：未发现本补丁新引入、需要阻断实施的正确性或安全问题。** Failed-only分支、原绝对截止、强退原语、正常成功顺序和隔离故障入口符合已审设计。主会话Debug ARM64完整Solution与full supervisor suite均自然exit0，red/green日志已实际读取；本reviewer没有独立复跑，结论限定于该源码/本机隔离路径。

仅关闭“有效普通Arm已返回双失败后，还进入可能无限业务清理”的条件性缺口。Host/窗口启动失败、真实输入、UEF资源耗尽、Win7、最后UInk/index恢复等其它门禁没有由本补丁自动关闭。

## 固定源码身份

基线HEAD：`e32a5fc06096c1e4ab88a29866c60ebe323fd722`。

| 文件 | 当前工作树Git blob |
| --- | --- |
| Inkeys/IdtMain.cpp | a8d4c2dc83839e98ceb319f0b5b2f8a59c912e99 |
| Inkeys/Inkeys/Helper/ShutdownSupervisor.cpp | 41c9cb562a6b39b04381399ac9d0f852339d47fc |
| Inkeys/Inkeys/Helper/ShutdownSupervisor.h | cabba9936d92fa816fabe7a8d1423bef15dde959 |

## Findings (fixed in candidate)

### Failed后不再进入业务清理

- `IdtMain.cpp::SetOffSignal:260–276` 保留首次offSignalInterop CAS、Window单调关门、Arm及offSignal发布；只有 `supervisor == ArmResult::Failed` 才在隐藏入队、logger、CrashHandler shutdown及Magnifier stop之前调用低层 `WakeForStop` 和noreturn接管。
- 成功Armed/FallbackArmed路径的 **offSignal→RequestHideAllUserWindows→WakeForStop→日志/清理** 原顺序没有移动。没有把 `result != Armed` 泛化成强退分支。
- GetService返回全局已构造processService；BeginShutdown只原子store；WakeForStop只SetEvent。Failed分支新增部分没有业务锁、堆分配、文件I/O、logger、新线程/进程或COM。

### 复用原绝对截止，零/已过期均立即接管

- `ShutdownSupervisor.cpp::EnforceFailedShutdownDeadline:1421–1438` 只读已有 `g_fallbackDeadlineTick`，Sleep剩余时间；`now >= deadline` 即尝试精确自进程TerminateProcess。零tick同样立即到期，不重复增加15秒。
- Close为 `0xE1430018`，Restart为 `0xE1430019`。自终止异常返回时只Sleep(1)后重试，不回到可能无限等待的业务清理，不调用DLL析构链。
- 该函数的前置条件在header和实现明确：首次普通owner有效Arm已state3失败。正式Close/Restart和UEF3先竞争同一offSignalInterop，只有赢家能Arm；本次真实调用者没有“合法UEF helper仍创建而普通SetOffSignal也赢得Failed分支”的双winner。
- 低层ArmCore的state1重复调用仍返回Failed；本函数不是公开通用恢复入口，不能被未来调用者用于任意低层Failed。当前只有SetOffSignal调用noreturn函数，未改变UEF仲裁和报告阶段。

### 故障与观察仅作用于显式验真的child

- `ArmShutdownSupervisor:1394–1418` 的新fault枚举默认为None。普通及旧UEF的ArmCore故障参数继续全部false/0；仅已授权Failed child设置该状态，没有产品配置/env setter。
- 新测试在原tick已发布后分别耗6秒、16.5秒（`ArmCore:425–436`），结果mapping记录真正的deadline/failed tick/result/error/state；测试能区分“剩约9秒后退场”“已过期立即退场”与错误的重新加15秒。
- `FailedArmTestObservation:54–69` 为48字节固定字段布局，deadline偏移16、published偏移12分别static_assert；没有跨进程指针、std::string或std::atomic对象。child先写字段再InterlockedExchange发布，parent在精确进程死亡/超时后InterlockedCompareExchange读取。mapping、view和继承HANDLE寿命都涵盖child；生产接管前没有新增磁盘marker/printf/等待测试回执。
- durable sentinel在Arm之前Write+Flush，旧真实SetOffSignal返回后才写cleanup-entered。green不应写该marker。sentinel只证测试文件未被覆盖，不冒充UInk/index或GUI可见恢复。

### 新child身份与parser

- `RunRealUefTestChild:995–1069` 按argc6/7原选项或argc9已识别Failed选项处理；拒绝短argc和未知选项的短路发生在访问argv[5]之前，未见新增越界。
- parent/ack均要求真实继承HANDLE，精确parent PID且非self；Failed选项在额外 `HasExactFailedArmChildIdentity:964–992` 检查后才设fault。它通过 `SameExecutableFile(parentImage, expectedParentImage)`核实际文件identity，要求expected路径绝对；root/bin/复制EXE拒绝reparse，当前EXE需与root/bin/Inkeys.exe实际文件身份一致，已超出旧parent文件名子串检查。
- observation HANDLE也要求继承属性，MapView成功后核magic/version/size及尚未published，再开放fault；坏对象/权限会拒绝，未见可以仅凭普通CLI开启产品故障的路径。
- `MakeUefTestDirectory:852–888` 使用新建唯一根，已有TestResults/release-hardening分量拒绝reparse，bin必须新建，复制不覆盖既有文件。测试只结束持有的child HANDLE并删除自己copiedImage，没有按名称终止用户进程或清理未知文档。
- 旧UEF argc6/7分支保持原验证及行为；默认测试launcher仍只传原2个HANDLE，新Failed场景才加第3个mapping HANDLE。新的 `--failed-arm-only` 是已有显式测试CLI的精确参数，未知参数返回71，不落正常GUI。

## Findings (not fixed / limits)

未发现本次diff需要阻断的新增问题。以下保留为窄口径限制，不能写成已全验证：

- ArmCore原始tick晚于正式意图CAS/BeginShutdown；OS创建调用尚未返回、OS不调度/内核无法自终止时不能证明硬实时15秒。人工16.5秒延迟用例证明的是返回Failed后不延长截止，不能称从请求起在15秒内退出。
- Failed分支不入可能卡住的hide队列，已有窗口可直到进程结束才消失；BeginShutdown只阻止迟到显示，不代表即时hide成功。
- 双失败Restart没有已握手launcher，旧进程强退不保证新GUI。未新增launcher/重启循环，成功Arm的唯一新实例行为需原suite复验。
- state1/非法输入的低层Failed不属于该正式入口前提；新函数的zero防护有源码证据，但未增加独立zero-tick自动场景。未来若增加其它调用者，必须再次核意图owner/原tick。
- 新child身份检查覆盖本次新建路径和父文件identity，不提供安装目录对抗并发恶意rename/reparse/ACL的完整证明；它是显式测试隔离信封，不等于生产进程权限认证。
- 本次尚无Win32/x64/Release结果，POD布局仍需三架构编译门；本reviewer未复跑旧UEF/full suite/Headless。真实Host保存/绘制停滞、启动失败分支、Win7 SP1仅KB2670838和真实GUI/恢复仍各自验收。
- 实施记录当前的HARNESS_READY段是红测时历史状态，尚写Header/Main未改和green待执行；应由owner追加当前源码冻结与red/green事实。本reviewer没有改实施者报告。

## Verification

### 本reviewer执行的只读检查

- `git diff --check HEAD --` 三份E01源码：exit0。
- 读取实际diff、生产普通/UEF调用链、parser/身份/mapping/计时断言、上述Git blob。
- Lint/TypeCheck/Build/Tests/GUI：按分工未独立运行，不冒充PASS。

### 已实际核对red日志

主会话完整Debug ARM64红build exit0（主会话交接结果）；`e01-failed-arm-red-debug-arm64.{stdout,stderr}.log`已逐行读取，targeted自然exit62由主会话提供。

- 六个有效child均authorized=1、ArmResult Failed=2、state3、durable=1、cleanup_entered=1、25秒后old_dead=0/exit259。create剩15秒，handshake剩约14.9秒，consumed剩9秒，expired剩0；然后父测试只清理自己的精确child，不把强杀计作生产成功。
- 错parent身份child exit84、未授权/未发布/无sentinel/无cleanup；无继承句柄child exit83，两项PASS。

### green状态

主会话提供 `e01-failed-arm-green-debug-arm64-build.log` exit0，本reviewer读取末段0 Error/11 Warning及实际源码格式；`e01-failed-arm-green-debug-arm64-full-supervisor.{stdout,stderr}.log` 已逐行读取，主会话确切suite parentPID26448自然exit0。日志有旧22项PASS、新6个双失败场景+2个授权负例PASS及1个composite PASS；不把末尾PASS代替主会话进程退出码。

| 真实SetOffSignal child场景 | 原截止剩余 / Failed到死亡 | ack后观测总时间 | 结果 |
| --- | --- | --- | --- |
| create Close | 15000ms / 15031ms | 15031ms | 专用Close码3779264536，cleanup0/durable1/restart0 |
| create Restart | 15000ms / 15032ms | 15032ms | 专用Restart码3779264537，cleanup0/durable1/restart0 |
| handshake Close | 14891ms / 14922ms | 15031ms | 同Close结果 |
| handshake Restart | 14875ms / 14922ms | 15047ms | 同Restart结果 |
| 已耗6秒Close | 8985ms / 9047ms | 15062ms | 未重加15秒，同Close结果 |
| Arm已延迟16.5秒Close | 0ms / 62ms | 16562ms | Failed返回后立即接管；不宣称整次Arm在15秒内完成 |

六项均authorized1、state3、result2、精确old HANDLE死亡，未进入业务cleanup，sentinel保持完整；错parent身份exit84、无继承句柄exit83均未启用fault。时间为父测试的20ms轮询/Win32调度观测，不是光学点击计时或硬实时容差承诺。

旧trueUEF自动场景dump/report有效且唯一restart1；manual/no-restart/CAS输家报告停滞仅pending，磁盘满保留dump，旧Close15秒、自然/强制/重复/迟死重启均PASS。该结果支持默认/旧UEF路径此次没有动态回归；不外推Win7、真实GUI或UInk可见恢复。

格式核对：Shutdown.cpp无BOM、1499行CRLF；Header无BOM、35行CRLF；IdtMain.cpp保留BOM、2840行CRLF；三者裸LF0。严格Headless/PptCOM及Release三架构由root后续安排。若其后再改产品blob，以上静态GREEN与动态结果需按影响增量复核。
