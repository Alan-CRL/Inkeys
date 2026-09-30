# F-046 / SR-02：崩溃重启交由精确旧进程监督器

日期：2026-09-28。此单元只改 `ShutdownSupervisor.h/.cpp` 与 `Helper.CrashHandler.cpp`；未改 `IdtMain.cpp`、工程文件或 UI/Draw3。F-043 退出/主动重启监督器与用户允许的 15 秒硬截止是前置。

## 现象与合同

旧 UEF 在生成 dump/报告后直接 `ShellExecuteW(-CrashTry)`，此时旧进程仍在异常处理器中；新实例可绕过当前单实例门，存在并存和数据/窗口交接风险。崩溃与受控退出已有同一 `offSignalInterop` 首意图 CAS，F-046 让 UEF 抢到 0→3 的单一 owner 才可能发出崩溃重启；重复异常/受控退出先到者不能另建 launcher。

`Intent::CrashRestart` 在与 Close/Restart 相同的真实继承父进程 HANDLE、PID/同 EXE 文件身份、ack 与绝对 deadline 下编码为 helper X 模式。helper 等精确旧 HANDLE signaled；15 秒到期仅强制终止它并待 signaled；然后唯一一次 `CreateProcessW` 用绝对 `lpApplicationName`、正确引号、`bInheritHandles=FALSE` 启动 `-CrashTry`。旧 UEF 不再直接 ShellExecute、也不调用 SetOffSignal 或业务清理。`teachingSafetyMode=1` 的自动选择在 dump 前建立监督，`=0` 仅用户 OK 后建立；`=2/3` 不重启。Arm 失败只记低层错误，不在旧进程仍活时补一个直接启动。已有五分钟启动崩溃抑制保持。

## 红→绿自动化证据

- 红版仅加 `CrashRestart` 枚举和两个显式无 GUI 隔离子进程用例，生产 ArmCore 仍拒绝该意图；完整 `InkeysRepo.sln Debug|ARM64` build exit 0，`--shutdown-supervisor-tests` pid 40140 exit 62：既有八例 PASS，`crash-restart-natural`/`crash-restart-forced` 两例 FAIL，旧进程未获监督、新 marker 0。原始日志 `TestResults/release-hardening/f046-crashrestart-red-build-debug-arm64.log` 与 `f046-crashrestart-red-debug-arm64.{stdout,stderr}.log`。
- 绿版接 X 模式、UEF owner 与唯一 launcher；完整 Debug|ARM64 Solution build exit 0，显式无 GUI `--shutdown-supervisor-tests` pid 59852 exit 0。自然模拟旧 pid 58220 exit 77 / restartCount 1；强制模拟旧 pid 11924 exit 3779264533，观察间隔 2953 ms / restartCount 1；原八例也 PASS，其中 15 秒 Close 观察为 14890 ms。测试子进程 marker 校验其收到 `-CrashTry`，Restart 原标志保持。原始日志 `f046-crashrestart-green-build-debug-arm64.log` 与 `f046-crashrestart-green-debug-arm64.{stdout,stderr}.log`。

上述测试调用的是同一生产 `ArmCore`/helper，但用显式早期测试模式模拟旧进程自然/强制结束，**没有触发真实 UEF 异常**，也没有启动产品 GUI、验证新实例 ready、保存或 Win7 运行。真实 UEF、Office/窗口交接与 Win7 SP1+仅 KB2670838 仍为人工门禁；FLIP_SEQUENTIAL、仅 DComp/ULW 与 HARDWARE/WARP 选路未触碰。下一单元 SR-03 独立检查 helper 创建/握手失败后的自身截止兜底，不把 F-046 绿灯扩大为所有失败路径已通过。
