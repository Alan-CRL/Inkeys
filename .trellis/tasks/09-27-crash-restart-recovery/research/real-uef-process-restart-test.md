# 真实未处理异常 → 自动重启的隔离进程验证

设计开始：2026-09-28；红绿验证：2026-09-29。F-046 的 `CrashRestart` 已用显式子进程直接调用生产 `ArmCore` 验证，但那不等于操作系统把真实未处理异常交给 `CrashHandler::UnhandledExceptionHandler`。本单元补一条明确分开的真实 UEF 无 GUI 测试，不改变产品默认 dump 根、恢复功能或 UI 入口。

## 隔离与门禁

最外层 `--shutdown-supervisor-tests` 必须从当前仓库根执行，检查 `InkeysRepo.sln` 与 `.trellis` 后，仅在已忽略的 `TestResults/release-hardening/` 创建唯一 `Inkeys Shutdown Test UEF .../bin/`，复制本次已构建的 `Inkeys.exe` 到 `bin/Inkeys.exe`。UEF 当前 `GetExeDirectory()` 实际返回完整 EXE 路径，取 `.parent_path()` 后在该 `bin/Inkeys/Crash/` 生成 dump/report；每轮均属于唯一测试目录，不接触真实安装、配置、文档或 Office。测试结束只尝试删除自己复制的 EXE，保留本轮 dump/report/marker 供核验，不扫描或清理未知文件。

顶层进程以 `DuplicateHandle` 制作只含 `SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION` 的真实自身进程 HANDLE 和 ack event，再用 `STARTUPINFOEX HANDLE_LIST` 只继承这两项。复制的 child 只在内部 `--inkeys-internal-real-uef-test-child` 模式下核可继承 flag、父 PID、同一仓库 `TestResults` 唯一目录、当前 EXE 位于其 `bin/` 后，才设置本进程私有 testDirectory；缺任一条件在配置、单实例、HWND 前直接返回，不 RaiseException。正式 `ArmShutdownSupervisor` 只有这个已授权 child 才把 testDirectory 传给同一生产 `ArmCore`，令 helper 将 `-CrashTry` 映射到无 GUI `--inkeys-internal-shutdown-test-restarted` marker；普通产品没有环境变量测试开关，默认目录始终为空。

child 用 `SetErrorMode` 抑制 OS 故障对话框，确认 ack 后调用 `RaiseException(..., EXCEPTION_NONCONTINUABLE)`，调用位于 `TryRunShutdownSupervisorEarly` 的 C++ catch 外。绿版由真实 `CrashHandler::SetFlag(1)` 与 `Initialize()` 安装 UEF；handler 使用现有 dump/report 与生产 `ArmShutdownSupervisor(CrashRestart)`。最外层只等待自己创建的 child HANDLE：旧进程必须 signaled、dump 与报告需匹配旧 PID，新 restart marker 恰好一个且不能在旧进程 signaled 前出现；记录旧退出码、文件状态、marker 数和测试根。若 child 卡住，宿主只对自己持有的该 HANDLE 做失败清理。

## 红→绿安排和证据状态

红版已写私有根、继承句柄/ack 授权、显式 RaiseException 与观测，但故意只安装测试抑制过滤器，**不安装生产 CrashHandler**；因此期望真实异常发生而产品 dump/后置 marker 缺失，suite 退出失败。这保证红测不会意外拉起正式 `-CrashTry` GUI。完整 `InkeysRepo.sln Debug|ARM64` build exit 0；显式隐藏 `Start-Process`/`WaitForExit` suite pid 32904 exit 62。旧 child pid 64160 已验授权，真实未处理 `RaiseException` 的退出码 3779313665 (`0xE143C001`)，dump=0/report=0/early_restart=0/restartCount=0，新例 FAIL，原有子例含 15 秒 Close PASS。原始 `TestResults/release-hardening/f054-exit-green-uef-red-build-debug-arm64.log` 与 `real-uef-red-debug-arm64.{stdout,stderr}.log` 保留。

绿版只在同一已授权 copied child 安装生产模块 `CrashHandler::SetFlag(1)`、`IsSecond(false)`、`Initialize()`，其余异常点/私有根/marker 与红版相同。完整 Debug|ARM64 Solution build exit 0；显式隐藏 suite pid 41648 exit 0。旧 copied child pid 58840 的真实 `RaiseException(0xE143C001)` 未经正常 catch，旧退出码仍为 3779313665；authorized=1、dump=1、report=1、early_restart=0、restartCount=1。实际测试重启 marker `restart-56696.txt` 内容为 `56696 58840 2`（新 PID、旧 PID、CrashTry 类型），旧 HANDLE signaled 后才出现；其它子例含 15 秒 Close 14984 ms 均 PASS。原始日志 `real-uef-green-build-debug-arm64.log` 与 `real-uef-green-debug-arm64.{stdout,stderr}.log`。

本轮唯一原始目录为 `TestResults/release-hardening/Inkeys Shutdown Test UEF 测试 41648 228043156 0/`。本轮复制的 `bin/Inkeys.exe` 已被测试宿主删除；保留 `bin/Inkeys/Crash/20260929_000612_58840.dmp`（52,901,322 字节）、同名 `.txt` 报告（522 字节）及上述 marker。报告明确 `DumpGenerated: true`、`ExceptionCode: 0xE143C001`、`RaiseException` 栈帧。dump 只留在忽略目录本地，不加入源码/发布包或上传。文件身份与 marker 的核对只读，不递归清理任何未知文件。

即使绿灯，也只证明本机指定 Win11 ARM64、无 GUI 子进程下的 UEF→dump/report→旧进程死亡→helper 唯一测试 marker。新**正式产品** `-CrashTry` 实际初始化/单实例/HWND、Office、绘图中故障、已提交 UInk 恢复、Win7 SP1+仅 KB2670838 x86/x64 与 FailFast/硬杀仍独立验收；FailFast/外部强杀不属于普通 UEF 合同。FLIP_SEQUENTIAL、仅 DComp/ULW、HARDWARE/WARP 没有由该进程测试改动或证明。
