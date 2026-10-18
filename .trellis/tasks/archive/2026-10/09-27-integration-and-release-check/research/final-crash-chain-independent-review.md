# 最终崩溃报告链独立只读复审

最终增量审查截止：2026-09-28 21:59:53 UTC，H0/当前 `HEAD=8b156fca59f0337a6afc6d722941666fcf143080`。三文件 **SHA-256**：`Helper.CrashHandler.cpp=919f6a1d5234b9f57de9d0464f0bdcbac1409e0fa02fd1c93b65a4fc1633c573`、`.cppm=4042010e1e0e3c6a4be65ffa85f191444c797cdf768f755bb9451399d2c96a03`、`ShutdownSupervisor.cpp=17edd325eba2cb7603ea0f5b3f24ecc30756bd96c8f4a9deb215115d69890e3d`；后者仍为未跟踪新文件。初轮指纹下的 mode1 CAS 败风险已由私有真实 UEF child 红→绿修补，下文记录两轮证据。本报告只读生产链、任务记录、日志和唯一私有目录实物；未运行构建、GUI、进程或性能采样。主 agent 的最终 Release 矩阵仍另计，不能把此次 Debug 绿码自动升级为所有架构/Win7 通过。

## Findings (fixed / evidence at this fingerprint)

- `Inkeys/Inkeys/Helper/Helper.CrashHandler.cpp:42-101,477-510,611-669`：mode0/2/3 在进入可能无界的 dump/report 前，用绝对 `GetTickCount64` 截止、独立 Win32 `CreateThread`、ready event（最多等待 500ms）建立 15 秒报告 watchdog。线程/ready 建立失败便不走重型报告；正常完成或早退通过 guard 取消，mode0 的用户确认框发生在取消之后。mode1 CAS 赢者沿 `ShutdownSupervisor.cpp:265-383` 的单一 CrashRestart helper 与自身 deadline，未再建第二套报告线程。代码静态可证；真实 loader lock、CreateThread/SetEvent 失败与正式 GUI 仍未运行。
- `Helper.CrashHandler.cpp:417-459,682-793`：`.txt` 先 `CREATE_NEW` 写同 basename `.tx_`，逐次检查 BOM/正文写入、Flush、Close，再以同目录、无替换的 `MoveFileExW(...WRITE_THROUGH)` 发布；dump 同样先写等长 `.dm_`，非零大小/Flush/Close 后才发布 `.dmp`。失败只尝试删本轮 pending，旧正式文件不会被本次覆盖。报告 watchdog 强退可留下私有 `.dm_/.tx_`，不会把半写文件放在正式扩展名。等长扩展名不增加原最终路径长度，但 Win7 深路径和断电持久性仍需目标环境证明。
- `Helper.CrashHandler.cpp:672-793` 的 x86 回退在高保真 `MiniDumpWriteDump` 失败后立即抓取 HRESULT，只有 partial-copy/invalid-argument 类错误才清空当前 pending HANDLE 并重试 `MiniDumpNormal`；磁盘/权限错误不降载伪装。旧 `hf-final-rel-win32-shutdown-supervisor.stdout.log` 的旧 PID33836 是 dump0/report1/restart1；修补后 `win32-uef-fallback-release.stdout.log` 旧 PID49820 为 dump1（46,440 B）/report1/restart1。独立读取其私有 `.dmp` 前四字节 `MDMP`，`.txt` 为同 PID、`DumpFallbackAttempted:true`、`DumpPrimaryError:2147942699`。这是 Win11 ARM64 上 Win32 Release 仿真证据，且早于本轮 mode0/磁盘满最终修改；不能直接作为当前指纹的 Win32 或 Win7 PASS。
- `Helper.CrashHandler.cpp:171-233,593-600`：报告第一次模拟 `ERROR_DISK_FULL` 后清理旧 pair 时，以大小写不敏感 basename 排除本轮刚发布的 dump；其余旧 pair 清理照原排序/数量处理。红日志 `manual-uef-diskfull-red-debug-arm64.stdout.log` 的旧 PID44144 显示 report1/restart1/dump0，私有 573 B 报告却写 `DumpGenerated:true`。绿日志的旧 PID48420 为 dump1（52,897,226 B）/report1（572 B）/restart1；唯一私有根 `Inkeys Shutdown Test UEF 测试 43872 246881578 0` 同时有同 PID `.dmp`（`MDMP`）与 `.txt`，报告为 `ExceptionCode:0xE143C001`、`DumpGenerated:true`。这是受控一次性错误注入，不是真磁盘满、ACL 或并发目录改名验证。
- `Helper.CrashHandler.cpp:477-513`：初轮指出 mode1 输掉意图 CAS 时 `automaticDeadlineReady=true` 会让重型报告无自己的 15 秒 owner。冻结版改为 `needsLocalReportDeadline = crashMode != 1 || !earlyCrashIntent`：CAS 赢者仍只用原 CrashRestart helper/自身 fallback；CAS 败者只尝试可取消的本地**报告** watchdog，不占第二个重启意图、不启动新 helper；建立失败跳过重型报告。`ShutdownSupervisor.cpp:881-938,941-1050` 的私有 copied child 在验明继承 parent/ack HANDLE 后预置 `offSignalInterop=1`、不 Arm 正式 helper，放大 CAS→Arm 窗口。红 suite pid45640 exit62，旧 PID45684 挂住 25 秒后由测试宿主精确 HANDLE 清理；绿 suite pid18676 exit0，旧 PID24648 约 15 秒以 `0xE1430017` 终止，正式 dump/report0、7 B `.dm_` pending1、restart0。绿私有根 `Inkeys Shutdown Test UEF 测试 18676 249146218 0` 已独立核 `.dm_` 仅 `PARTIAL`，无同 PID `.dmp/.txt`；原 mode1 CAS 赢 PID56132 仍 dump/report/restart1，mode0/2/磁盘满/15 秒 Close 子例均 PASS。测试放大的是短窗口，并未执行真实多线程 SetOffSignal→helper 交接。

## Findings (not fixed / not proven)

### P2 条件性：真实受控退出与 UEF 并发交接仍未运行

- 以上 mode1 CAS 败私有 child **预置** `offSignalInterop=1` 而不执行完整 `SetOffSignal`。它证明报告时钟在「意图已占、外部 helper 尚无」时独立结束旧进程，并证明自己不重复重启；没有覆盖真正另一线程从 `Inkeys/IdtMain.cpp:256-278` 继续 `BeginShutdown`、`ArmShutdownSupervisor`、窗口隐藏、15 秒自身 fallback 的先后交错。该真实链若发生 COM/Window owner/内核调度卡住，仍只能按各自已建立的 deadline 能力声称，不能由本用例直接宣布完整 GUI 退出/重启 PASS。
- 建议在获准的隔离 GUI 或受控多线程进程里暂停/释放正式 SetOffSignal 的 CAS→Arm，检查本地报告时钟与正式 helper 同时存在时旧 HANDLE 死亡、最多一次新实例、窗口 gate 和已提交 UInk；Win7 单列。

### P2 条件性文件身份与测试覆盖界限

- 当前磁盘满保护按 `protectedDump.filename()` 文本跳过（`CrashHandler.cpp:202-204`），正式 dump 已在 `:776-781` 关闭 HANDLE。若 Crash 目录可被并发改名/替换，清理和报告看到的路径身份可能变化；本轮故障注入只证明私有目录**静止**时保留本轮文件，没有对抗重命名/ACL，也不能把 basename 比较称为句柄级身份保证。该前提需结合安装目录权限与真实故障验收，不据此擅改清理/模块边界。
- `ShutdownSupervisor.cpp:878-929,933-1079` 的真实 UEF 测试在 `RunRealUefTestChild` 先核唯一 `TestResults/release-hardening` 私有 root、复制 EXE 所在 `bin`、两个继承且可用的 parent/ack HANDLE 与 PID，再设置 `SetIsolatedUefTestMode`，最后在普通 C++ catch 外 `RaiseException`。产品默认测试模式为 Disabled，代码搜索只见这一授权 child 调 setter；普通用户仅传内部参数而无继承句柄会在异常前返回。测试没有覆盖任意本地进程伪造祖先文件/句柄的对抗能力，也没有正式 GUI。mode0 挂起、mode2 挂起、mode0 报告后 16 秒无 GUI 等待、mode1 自动均有本机 Debug ARM64 日志；mode3 的 `EXCEPTION_CONTINUE_SEARCH` 正常报告后 OS 行为和 ready/thread 创建失败尚无动态用例。

## 已读证据与发布边界

- `manual-uef-report-red-debug-arm64.stdout.log`：mode0 私有 child PID66424 在 25 秒上限仍活、正式 `.dmp` 留 7 B `PARTIAL`、suite 子例 FAIL；测试仅结束自己持有 HANDLE 的 child。`manual-uef-report-final-debug-arm64.stdout.log`：mode0/2 挂起旧 PID24152/10080 均以 `0xE1430017` 死亡，只有 7 B `.dm_`，无同 PID 正式 `.dmp/.txt` 或 restart；mode0 正常报告后的 16 秒等待 PID66548 有完整 dump/report、restart0；mode1 PID36096 有完整报告与 restart1。已在各唯一私有根按文件名/字节检查，不把红版打印的 `exit_code=0` 占位误认正常退出。
- `manual-uef-diskfull-green-debug-arm64.stdout.log` 与私有 root 的受控磁盘满重试、原 15 秒 Close 子例均 PASS；实施记录注明完整 Debug|ARM64 Solution 构建 exit0，本 reviewer 只读日志关键输出，未独立重跑。`manual-uef-auto-lost-red-debug-arm64.stdout.log` 的旧 PID45684 在25秒上限未死、私有 `.dm_` 7 B 且子例 FAIL；同名 green 日志旧 PID24648 以专用截止码退出、pending1/final0/restart0，实物为 7 B `PARTIAL`。红绿均从显式 no-GUI copied child 触发。主 agent 此前报告 ARM64 Release 真实 UEF suite exit0，但它早于最终 mode1 CAS败修补；不能替代本指纹的 Release/Win32/x64/Win7 复验。
- 新 watchdog 的可见调用是 Win32 event/thread/clock/process API；没有从这三文件静态识别出必须 Win8+ 的新调用。**这只是源码筛查**：真 Win7 SP1+仅 KB2670838 的导入装载、目标机器 DbgHelp 支持高保真 flag/`MiniDumpNormal`、深目录、报告线程调度与 UI/Office/绘图中崩溃均未运行。`MoveFileExW WRITE_THROUGH` 与 `FlushFileBuffers` 也不证明断电后目录元数据一定 durable。mode0 真用户 OK/Cancel、mode3 WER、真正坏盘/权限错误、FailFast/外部强杀与新进程可见 UInk 恢复必须分别保持未验或能力边界；不能把 dump、restart、数据恢复合并为一个 PASS。

## Verification

- 本 reviewer 对已跟踪两文件执行 `git diff --check H0 --`：exit0；`ShutdownSupervisor.cpp` 是未跟踪新文件，未用该命令覆盖。最终三文件 SHA-256 已独立用 `Get-FileHash` 复算，和实施者冻结值一致；仅读取红绿日志、私有 `.dm_/.dmp/.txt` 的长度/文件头/身份字段，没有运行测试、构建、GUI 或采样。此报告只对开头三文件指纹成立，后续 Release/源码修改须按变化范围重验。
