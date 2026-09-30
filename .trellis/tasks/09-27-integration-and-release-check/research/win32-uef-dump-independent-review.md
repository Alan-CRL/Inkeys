# Win32 真实 UEF dump 降载修补独立复审（2026-09-29）

## 审查结论

只读复核 `Helper.CrashHandler.cpp::GenerateMiniDump` 当前实际 diff、`ShutdownSupervisor.cpp` 真实 UEF 测试入口、红绿进程日志与私有 dump/report。**确认 Win32 本机 x86 仿真中，原先“有报告和重启、无 dump”的失败已由有条件的 `MiniDumpNormal` 重试修复；没有将它外推为 Win7 真机或所有崩溃类型通过。** 未改上述产品/测试代码，也未启动新构建或进程。

## 首因、事务与实物核对

- 红版 `hf-final-rel-win32-shutdown-supervisor.stdout.log` 的真实 UEF 旧 PID 33836：`authorized=1 dump=0 report=1 early_restart=0 restart_count=1`，suite 退出 62。其唯一 `20260929_024541_33836.txt` 报告有 `DumpGenerated: false`、`DumpLastError: 2147942699`、异常码 `0xE143C001`，目录无同 PID `.dmp`。`0x8007012B` 对应 `HRESULT_FROM_WIN32(ERROR_PARTIAL_COPY)`，但原代码在 `FlushFileBuffers`/`CloseHandle` 后才取 `GetLastError`，该旧报告数值的采样时序本身不能单独证明高保真 API 首因。
- 新代码先尝试原高保真 `MINIDUMP_TYPE`，在 `MiniDumpWriteDump` 返回 FALSE 的下一步立即保存 `GetLastError` 到局部值和 `g_dumpPrimaryError`。仅 partial-copy、无效参数的指定 HRESULT/raw partial-copy 能触发一次最小 dump 重试；磁盘/权限失败不会被伪装为降载成功。[微软文档](https://learn.microsoft.com/en-us/windows/win32/api/minidumpapiset/nf-minidumpapiset-minidumpwritedump)确认此 API 的最后错误是 HRESULT。
- 回退复用本函数新建且无共享的精确文件 HANDLE，`SetFilePointerEx(..., FILE_BEGIN)` 后 `SetEndOfFile` 清掉首试残片，再执行 `MiniDumpNormal`。成功还须 `GetFileSizeEx > 0`、`FlushFileBuffers`、`CloseHandle`；各失败分支保存对应 Win32 错误、标记失败，并只尝试删本轮精确失败 `.dmp`。报告区分 `DumpPrimaryError`、`DumpFallbackAttempted` 和最终 `DumpGenerated`。没有删旧 dump、扫用户文件或更改默认目录。
- 绿版完整 `InkeysRepo.sln Release|Win32` Build 日志 `win32-uef-fallback-build-release.log` 末尾有 Inkeys、Headless、PptCOM.Tests 输出；suite `win32-uef-fallback-release.stdout.log` 的父 PID 13008 自然退出 0，真实 UEF 旧 PID 49820 `dump=1 dump_bytes=46440 report=1 report_bytes=1033 early_restart=0 restart_count=1`，约 14,891ms 的强制 Close 子例也 PASS。私有报告 `TestResults/release-hardening/Inkeys Shutdown Test UEF 测试 13008 239682812 0/bin/Inkeys/Crash/20260929_032011_49820.txt` 实含 `DumpGenerated: true`、`DumpFallbackAttempted: true`、`DumpPrimaryError: 2147942699`、精确 PID/异常码。独立读取同名 46,440 字节 `.dmp` 首四字节为 `MDMP`；测试源码还要求大于 32 字节、报告身份、旧 HANDLE 死后唯一重启 marker。**dump 成功与自动重启成功是两个分别验证的结果。**
- `hf-final-release-win32-pe-imports.log` 有 `dbghelp.dll` 的 `MiniDumpWriteDump` 和 Kernel32 的 `SetFilePointerEx`、`GetFileSizeEx`、`InitializeProcThreadAttributeList`、`UpdateProcThreadAttribute`、`QueryFullProcessImageNameW`。官方 [SetFilePointerEx](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfilepointerex) 最低 XP，[InitializeProcThreadAttributeList](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-initializeprocthreadattributelist) 与 [QueryFullProcessImageNameW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-queryfullprocessimagenamew) 最低 Vista；这些新增调用没有静态要求高于 Win7 的 OS 版本。[MINIDUMP_TYPE](https://learn.microsoft.com/en-us/windows/win32/api/minidumpapiset/ne-minidumpapiset-minidump_type) 说明附加 flag 受 DbgHelp 版本影响；本轮没有在 Win7 SP1 仅 KB2670838 的内置 DbgHelp 上运行，不能单靠导入表断言实际生成成功。

## 遗留风险与复验门

1. `MiniDumpNormal` 仅是高保真写入失败的最低诊断保底，不能恢复原 `MiniDumpWithPrivateReadWriteMemory` 的内容。高保真是否在修改后的 ARM64/x64 仍成功，应由主任务串行构建及同一真实 UEF suite 复验；截至本次只读审查，所见绿证仅 Win32。
2. **未复现但值得发布门禁单列：**产品默认 `IdtMain.cpp:1819` 设置 `teachingSafetyMode=0`，传给 `CrashHandler::SetFlag`。模式 0 在 UEF 开始时 `TryClaimCrashRestartIntent(..., false)` 返回 false，外部 15 秒 supervisor 只在 dump/report 完成、用户点击 OK 后才 arm。微软 [MiniDumpWriteDump 文档](https://learn.microsoft.com/en-us/windows/win32/api/minidumpapiset/nf-minidumpapiset-minidumpwritedump)明确提醒崩溃进程内调用可能发生 loader deadlock；如果模式 0 在报告前卡住，提示和重启无法到达。模式 1 真实 UEF 绿证不能覆盖模式 0。当前仅是静态风险假设，建议独立受限故障注入或明确有界机制，未获得证据前不要声称所有崩溃入口有 15 秒界限。
3. Win7 真机 x86/x64、正式 `IDT_RELEASE` 的单实例和 GUI 启动、绘图中异常、Office、FailFast/强杀、可见 UInk 恢复与磁盘满/权限失败路径均未由此 x86 仿真用例验证。Win7 目标仅 SP1 + KB2670838，不应加补丁替换合同；本修补不涉及 FLIP/DComp/ULW 选择。
