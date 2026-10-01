# E01 双监督失败截止退场：实施与验证记录

Active task：`.trellis/tasks/09-27-integration-and-release-check`。负责人：`shutdown_failed_arm`。设计：`failed-arm-deadline-design.md`。主会话 GREEN_DESIGN 后实施；不 commit、push、archive 或标任务完成。共享账本、spec、构建/运行由主会话所有。

## 2026-09-30 HARNESS_READY（生产红测检查点）

- 改动仅 `Inkeys/Inkeys/Helper/ShutdownSupervisor.cpp`，337行新增/14行删除；Header、IdtMain.cpp 未改，生产 Failed 的旧清理路径保留，尚无 EnforceFailedShutdownDeadline 实现。
- 测试复用原 copied UEF child 的 launcher/handle授权信封；新 failed-arm为argc9并在普通 UEF argc6/7外附加精确parent文件identity、root/bin/EXE非reparse、48字节pagefile observation mapping（magic/version/size/inherited handle）核验。只有全部校验通过后才打开双失败/延迟选项。
- 测试调用真实 `SetOffSignal(1/2)`，有create/handshake × Close/Restart四项，加原deadline已耗6秒/已过期16.5秒两项；错误parent identity、无继承句柄两项拒绝检查。所有fault均是私有child状态，默认关闭。标准suite包含本组；单组入口仅为既有CLI下 `--shutdown-supervisor-tests --failed-arm-only`。
- Arm后观测仅写预映射固定packet并Interlocked发布，无文件、logger、堆、锁或新线程。durable sentinel在Arm前Write+Flush，cleanup-entered只在旧SetOffSignal返回后产生。sentinel不冒充真实UInk/index或可见恢复证据。
- 生产正常 Arm wrapper新增内部默认None的测试状态读取，但所有旧调用的fault参数仍false/0；普通 SetOffSignal的offSignal→hide→Wake/日志/清理顺序完全未动。原UEF信封/选项行为保留。

## 2026-09-30 实际红证据与PATCH_READY

- 主会话执行ARM64原生完整 `InkeysRepo.sln Debug|ARM64` 红构建，exit0；日志 `TestResults/release-hardening/e01-failed-arm-red-debug-arm64-build.log`。此时 Header/Main/guard仍未修补，只有测试夹具。
- 主会话在仓库根运行上述targeted CLI，父PID20252自然exit62。六个生产SetOffSignal fault用例**全部FAIL**，两个授权拒绝用例PASS。六个实际child均authorized=1、resultFailed(2)/state3、已过25秒仍old_dead=0、cleanup_entered=1、durable=1、restart_count=0；父suite随后只结束各自精确child HANDLE。consumed观测remaining9000ms，expired观测remaining0，不是环境/授权造成的假红。原始stdout/stderr `e01-failed-arm-red-debug-arm64.{stdout,stderr}.log`。
- 主会话以真实红证据授予GREEN_IMPLEMENT后，新增 `EnforceFailedShutdownDeadline`：Win32-only当前线程原始绝对截止；已过期直接终止；意外自终止失败仍不回业务清理。Close0xE1430018、Restart0xE1430019，无新线程/helper/heap/业务锁/日志/文件或DLL退出；失败Restart不宣称新实例成功。
- Header仅4行，SetOffSignal仅8行Failed分支（offSignal原子发布后、隐藏/日志/清理前），成功的offSignal→hide→Wake逐句顺序保留。implementation约22行。三文件CRLF/BOM保持，git diff --check exit0。
- PATCH_READY源码已冻结供主会话green构建/运行及独立review：Shutdown.cpp SHA256 `6be69e7f5a3505eafd3fb48ed0f36a9e57b3f35647fe71750e0027ad1412170d`，Shutdown.h `937355898081950820fad8e7d1b7473eea75e52d12caf8d7037a6b8c89ea09ac`，IdtMain.cpp `2bf0957859818074f21a28d5f1ebd3e30551614fb06b2193254b209a02157919`。
- 上述PATCH_READY时仍是**已修复待验证**；下节保留之后返回的实际green，不以旧PASS替代。

## 2026-09-30 Debug ARM64 green与交付

- 主会话完整 `InkeysRepo.sln Debug|ARM64` green构建exit0，日志 `TestResults/release-hardening/e01-failed-arm-green-debug-arm64-build.log`。源码三个SHA256与PATCH_READY完全一致。
- 主会话运行完整 `Build/ARM64/Debug/Inkeys.exe --shutdown-supervisor-tests`，自然exit0。原22项全部PASS，新增六项deadline与两项auth拒绝全部PASS（另一个组合PASS标签）；raw stdout共31 PASS标签/0FAIL。这不是31项独立全产品验收。
- raw stdout/stderr：`TestResults/release-hardening/e01-failed-arm-green-debug-arm64-full-supervisor.{stdout,stderr}.log`。本implementer读取并逐项核对，未自行执行构建或CLI。

| 新用例 | 实际child PID | 观测 | 结果 |
| --- | --- | --- | --- |
| create Close | 32692 | old_dead1，15031ms，0xE1430018，cleanup0，durable1，restart0 | 已验证通过 |
| create Restart | 25852 | old_dead1，15032ms，0xE1430019，cleanup0，durable1，restart0 | 已验证通过；新实例未建立是明确失败边界 |
| handshake Close | 21528 | 15031ms，原剩14891ms，0xE1430018，cleanup0 | 已验证通过 |
| handshake Restart | 8152 | 15047ms，原剩14875ms，0xE1430019，cleanup0 | 已验证通过；无虚假重启成功 |
| consumed Close | 18200 | Arm已耗约6s、剩8985ms；总15062ms，Failed→死亡9047ms | 已验证通过，未重加15s |
| expired Close | 31724 | 模拟Arm已16.5s、原剩0；Failed→死亡62ms，cleanup0 | 已验证通过，仅证明已过期接管，不宣称Arm16.5s阶段符合15s |
| wrong parent identity | 5036 | auth0、exit84、无fault/cleanup/sentinel/restart | 已验证通过 |
| no inherited handles | 7836 | exit83，无正式Arm | 已验证通过 |

六个合法child均resultFailed/state3、durable1、restart0，保留scope：只有普通首次owner真实双失败才执行本接管；不把任意低层Failed/state1解释为无helper。数值为软件观测，15.03–15.06s含调度/测试轮询；不是硬实时/光学计时。

原suite继续证实成功监督的正常/强制Close/Restart、重复请求、手动pending、迟死亡、fallback/helper失败、引用同EXE/引号、真实UEF dump/report/有限重启及report截止，未因本补丁破坏；不替代GUI真实按钮/真实Host保存和绘图停滞、Win7设备/墨迹可见恢复。

**工程子项交付：核心实现与Debug ARM64生产红→绿已验证。** 主会话正在运行严格Headless/PptCOM与Release/三架构，并派独立review读实际diff/raw；这些后续结果尚未纳入本报告。源码保持冻结，worker可回收。整体任务仍in_progress；无新commit/push/archive/产品发布结论。

## 已执行的检查

| 项目 | 执行者/结果 | 证据范围 |
| --- | --- | --- |
| Git差异/代码静态阅读 | implementer，已检查实际diff与调用点 | 无Header/Main修补，保持红路径；不代表构建通过 |
| `git diff --check -- Inkeys/Inkeys/Helper/ShutdownSupervisor.cpp` | implementer，exit0 | 差异空白检查 |
| 编码/换行 | implementer，Shutdown.cpp UTF8无BOM，全1479行CRLF、裸LF0；Main仍BOM、Header无BOM | 原格式保持，无全文件换行diff |
| Debug ARM64完整Solution红build | 主会话，exit0 | ARM64原生MSBuild、同调用Path规范、>=5min；尚非最终绿构建 |
| Targeted真实入口红测 | 主会话，exit62，六FAIL+两授权PASS | 实际生产SetOffSignal进入旧清理后25秒仍活；见上述raw日志 |
| 生产最小修补 | implementer，PATCH_READY | 代码已修、静态diff检查0；动态待验证 |
| green完整Solution/全suite | 主会话，exit0；本implementer核raw | 八个新case及原22case均PASS，31标签含一组合标签 |
| 严格Headless/PptCOM、Release/三架构、独立review | 主会话继续，**未纳入此处PASS** | 后续账本记录，不以本机ARM64 Debug外推 |

## 主会话红测运行合同

从仓库根目录（MakeUefTestDirectory要求存在InkeysRepo.sln和.trellis）完成主Solution Debug|ARM64后，运行：

```text
Build/ARM64/Debug/Inkeys.exe --shutdown-supervisor-tests --failed-arm-only
```

测试仅创建自身私有copied进程/目录。每个合法红child最长25秒；失败后用仍持有的准确HANDLE结束该child，绝不按名称清理用户进程。单组约155秒，外层至少5分钟。stdout为case的old_pid、old_dead、exit_code、authorized、elapsed_ms、Arm result/state/error、原deadline/Failed tick、remaining_ms、failure_to_death_ms、cleanup_entered、durable、restart_count、wrong_identity；每项PASS/FAIL另行打印。

红版本应state3、resultFailed、authorized=1、durable=1、cleanup_entered=1、25秒后仍未终止（exit_code=STILL_ACTIVE 259），最后父测试结束该精确child并返回FAIL。这是监督liveness红证据，不是UEF/崩溃捕获测试。Restart双失败应无新实例；监督建立成功的唯一重启由原suite独立复验。

raw输出/命令/exitcode路径由主会话提供后追加。测试/构建槽始终归主会话，本implementer不运行CLI/MSBuild。

## 仍需验证的边界

- 独立review及其修正；生产guard调用点、已耗/已过期与Close/Restart区别已有上述动态证据。
- 严格Headless/PptCOM、Release与Win32/x64/ARM64 ABI/binary由主会话继续；最新Debug完整Solution与原suite已通过。
- 真实Host保存/绘制线程停滞、最后UInk/index与GUI可见恢复属于E02/E03；Win7 SP1仅KB2670838、真实按钮及真笔是单独验收。
- 所有监督均无法创建时尚未返回的OS调用/内核整体停顿、线程永久不被调度无法由当前线程强退合同证明；已返Failed的业务清理无界等待是本项修补目标。
