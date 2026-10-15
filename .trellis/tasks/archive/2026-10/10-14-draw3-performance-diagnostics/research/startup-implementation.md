# Draw3 性能诊断启动与配置实施说明

实施范围：`startup_implement`。与 telemetry / package 共享工作树，未回滚其他代理修改；未构建、运行 GUI/隐藏 HWND、Computer Use、commit 或 push。

## 修改文件

- `Inkeys/IdtMain.cpp`：最早 argv 块分发 `--draw3-performance-test` 与 `--draw3-performance-launch-test`；错误参数在配置、互斥体与 HWND 前退出 2。显式会话仅在越过既有 SuperTop 后建立最终进程握手。设置性能字段经启动 Mini 读取，固定本次会话；传 `HostStartOptions.performanceOutputDirectory`，不打开 Console / StartupEnvironmentDiagnosticsEnabled / Cursor / TouchArea。
- 新增 `Inkeys/Launch/IdtPerformanceLaunch.h/.cpp`：只在启用分支读取进程创建时间/令牌、创建目录或写文件；空状态不采集时钟、不启动线程。保存绝对唯一目录、final owner 与 create-new JSON，提供纯 CPU 启动合同自检。
- `Inkeys/Inkeys/Other/Other.Config.cppm`：`Experimental.Inkeys3.ConsoleOutput.Performance : IdtAtomic<bool> = false`、`NoUpload`，未迁移其他配置。
- `Inkeys/Inkeys/UI/Setting/Setting.cpp`、三语言 JSONC、生成 `IdtI18nKeys.g.h` 与 `Scripts/i18n.zh-CN.snapshot.jsonc`：新增 Draw3 性能数据卡片，说明重启与退出后输出到 `log/Draw3Performance`；仅新卡片按 90 DIP 换行，原卡片保留 70 DIP。
- `Inkeys/Inkeys.vcxproj/.filters`：登记上述 launcher 与 telemetry 新增 `Draw3.Performance.h/.cpp`。既有 `Draw3.PerformanceProbe.h/.cpp` 原已登记，不重复添加。

## 参数与启动边界

正式入口 `--draw3-performance <绝对已存在唯一会话目录>`；仅接受附加既有内部 `-SuperTopComplete/-WarnTry/-CrashTry/-Restart` 标志。拒绝重复或拼接形式、其他参数、相对目录、末尾分隔符、非法路径字符、长度达到 MAX_PATH、裸 drive/UNC share、已有 owner/launch/completion/host-* 或 launch-failure 文件的目录。参数名大小写不正确同样在 GUI 前拒绝。

保留产品真实 `Inkeys::Split::Run(..., '*') → LaunchState::commandLine → SuperTop '-CmdLine=?...?'` 透传，不修改权限、UIAccess 或 SuperTop 行为。只 final 进程取得 `.draw3-performance-owner.lock`，不会把 initial / helper 的 exit(0) 当完成。旧重启消费者 `ShutdownSupervisor.cpp` 固定以 `-Restart/-CrashTry` 启动，不消费 `LaunchState::commandLine`；本次 CLI 目录不会继承到业务重启。持久化性能选项开启时重启自然创建新的唯一目录。

显式目录优先，不赋值到持久化 bool。实验选项创建 `log/Draw3Performance/<UTC时间-PID-GUID>`。实验采集目录/握手失败时，仅放弃本次输出，cold stderr 及 logger ready 后 warn，继续普通业务启动，不改用户偏好；显式 CLI 请求失败则非零早退。

## 跨代理输出合同（schema_version = 1）

`launch.json` 只 final 越过 SuperTop 后创建，完整临时文件 Flush 后同目录 Move 发布，不替换已有目标。必需字段：

```text
schema_version, session_id(目录叶名), final_pid, process_start_time(UTC ISO8601 毫秒),
process_start_filetime_utc(完整 FILETIME 十进制字符串), status=started,
output_directory, exe_path, software_version, edition_date, process_architecture,
source=command_line|experimental_option, ui_access=bool|null
```

采集器按完整 FILETIME / exe_path 验证 final PID，握手默认 180 秒且可配置，超时不杀软件。initial/helper 不写 completion；前置单实例/路径权限等明确拒绝以 `launch-failure-PID.json` 保存 `status=failed_before_final / pid / reason / win32_error`，不冒称 final。

`completion.json` 仅正常返回时在幂等 `StopProduct()` 后读取 `GetDraw3PerformanceExportStatus()`，所有 Host 封口/导出完才发布。错误早返回同样 StopProduct，但保留原实际 return code。C++ 未处理异常不写无法证明的 completion；崩溃/强退/失去最终握手由采集器判 incomplete。

顶层除共同身份外含 `status=completed|incomplete|failed / reason / exit_code / startup_failure_code / normal_shutdown / export_succeeded / capture_complete / samples_dropped / log_file / export_status`。`export_status` 字段：

```text
enabled, host_runs, exported_host_runs, failed_host_runs,
startup_failed_host_runs, run_failed_host_runs, unavailable_host_runs,
frames_seen, frames_retained, frames_dropped,
inputs_seen, inputs_retained, inputs_dropped,
receipts_seen, receipts_retained, receipts_dropped, proof_overflow, payload_budget_bytes
```

`failed_host_runs` 是导出失败，startup/run 异常单独保留。DComp 首代失败但 ULW 回退正常、所有代导出完整，可 completed；正常结束但帧/输入/回执掉样或 proof 溢出为 incomplete（`capacity_exhausted`），不把导出成功冒称全程覆盖。业务 run/startup 失败为 failed，仍可同时 `export_succeeded=true`。GPU 稀疏 pending/disjoint/unsupported 在 telemetry 的独立能力字段报告，不冒称输入掉样。重启结束本次采集时 reason 为 `restart_requested`。

## 已执行验证

- `Scripts/i18n.ps1 sync` 与 `check`：en-US / zh-TW 362/362（100%）通过，生成 diff 仅新键。
- `git -c core.fsmonitor=false -c core.untrackedCache=false diff --check`：通过。
- vcxproj / filters 用 .NET XML 实际解析：通过，新 h/cpp 两份工程镜像登记一致。
- 所有本角色修改文件 byte 检查：零裸 LF；IdtMain 保留 UTF-8 BOM+CRLF、原本无 EOF newline；其他既有文件保留 UTF-8 无 BOM+CRLF。
- 静态核对正常模式：launcher 空字段，无新增时钟/令牌查询/文件/线程；运行热路径与 Draw3 功能由 telemetry 独占，本角色未编辑。

## 主会话待执行

- 完整 `InkeysRepo.sln Debug|ARM64`（原生 ARM64 MSBuild、同一 invocation PATH workaround），以及 `InkeysHeadlessTests --no-window`。
- `Inkeys.exe --draw3-performance-test`（telemetry 的真实计量/输入/proof 自检）。
- `Inkeys.exe --draw3-performance-launch-test`：真实 legacy Split 双层透传、中文空格目录、exact argv、关闭状态、配置路径失败可放弃、CLI 目录优先、独占身份/拒绝复用、create-new atomic handshake、业务 failure 与帧/输入/receipt/proof/export 容量的 9 场景。该自检只创建/精确清理自身临时文件和空目录，不创建 HWND。
- `Inkeys.exe --config-boundary-test`：本次扩展真实缺失默认 false、Write 与 ReadMini 的 true/false 往返，沿原隔离 TestResults 目录。

真实 UIAccess/UAC、Win7、兆芯/低端设备、RTS 输入及卡片视觉未运行，不能用静态检查或 CLI 替代现场验证。15 秒退出监督保持；慢盘/崩溃可能中断导出，缺 completion 必须如实判不完整。
