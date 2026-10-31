# Win32 Release 真实 UEF dump 降载回退

设计与红证：2026-09-29。仅修改第一方 `Helper.CrashHandler.cpp` 的 dump 生成/诊断和 `ShutdownSupervisor.cpp` 的显式无 GUI 真实 UEF 测试断言；不改产品 UI、Draw3、Storage、工程文件或发布矩阵。

## 失败证据与首因

最终矩阵的完整 `InkeysRepo.sln Release|Win32` 构建已通过，但沙箱内显式隐藏 `--shutdown-supervisor-tests` 的真实 UEF 子例失败，suite exit 62。唯一旧 child PID 33836 的报告位于忽略目录 `TestResults/release-hardening/Inkeys Shutdown Test UEF 测试 52752 237612468 0/bin/Inkeys/Crash/20260929_024541_33836.txt`，记录 `ExceptionCode: 0xE143C001`、`DumpGenerated: false`、`DumpLastError: 2147942699 (0x8007012B)`；没有 `.dmp`。测试 marker 仍表明旧进程死亡后恰好一次测试重启，说明重启链与 dump 成败是两项不同结论。原始 `hf-final-rel-win32-shutdown-supervisor.{stdout,stderr}.log` 和上述私有报告保留。同期 Release|x64/ARM64 的相同 suite exit 0。

`0x8007012B` 是 `HRESULT_FROM_WIN32(ERROR_PARTIAL_COPY)`；[Microsoft MiniDumpWriteDump 文档](https://learn.microsoft.com/en-us/windows/win32/api/minidumpapiset/nf-minidumpapiset-minidumpwritedump)明确该 API 失败时 `GetLastError` 为 HRESULT。当前 `GenerateMiniDump` 却先 `FlushFileBuffers`/`CloseHandle`，然后读取 `GetLastError`，故现有报告值的**采集时序不可靠**。高保真类型含 `MiniDumpWithPrivateReadWriteMemory`，需扫描可读写私有页；[MINIDUMP_TYPE 文档](https://learn.microsoft.com/en-us/windows/win32/api/minidumpapiset/ne-minidumpapiset-minidump_type)说明 `MiniDumpNormal` 只保留形成线程栈回溯所需的基本信息，附加 flag 对 DbgHelp 版本也有依赖。Win32 进程在 ARM64 主机仿真下某页无法完整读取，是与现象相符的**假设**，不能仅凭这次 HRESULT 断言仿真是唯一根因。

## 修补合同

先用现有高保真 flags 尝试一次，失败后**立即**捕获 DbgHelp HRESULT。仅对 `ERROR_PARTIAL_COPY` 或无效参数这类读页/类型兼容错误，且仅在本次创建的精确 `.dmp` 文件上回退：把已失败的部分内容定位到开头并截断，再用 `MiniDumpNormal` 重试一次。磁盘满、权限、路径等错误不降载重试。成功须检查非零文件长度、Flush 和 Close；任一失败均报告 `DumpGenerated:false` 并尝试只删除本函数新建的失败 `.dmp`，不能留下“成功”诊断。报告额外保存首轮 `DumpPrimaryError` 与 `DumpFallbackAttempted`，失败的 `DumpLastError` 为最终错误；正常可写高保真 dump 的 x64/ARM64 路径不降画质/不改采集范围。最小 dump 是异常情况下的保底诊断，不能宣称等价于原包含私有读写内存的高保真转储。

显式测试门在真实未处理 `RaiseException` 后，要求旧 PID 对应的 `.dmp` 大于基本头长度、首四字节为 `MDMP`；报告非零且精确包含旧 PID、`ExceptionCode: 0xE143C001`、`DumpGenerated: true` 和首因/回退字段；新测试 marker 仍须旧 HANDLE signaled 后恰好一次。红版 `dump=0/report=1/restart=1` 不会因文件名存在或编译通过而变绿。

## 当前状态与边界

修补后，使用 VS 安装中原生 ARM64 MSBuild 的完整 `InkeysRepo.sln Release|Win32 /t:Build /p:VcpkgManifestInstall=false /m:1` 退出 0；项目正式支持的 `VcpkgManifestInstall=false` 使用已安装固定 x86 依赖，未宣称本轮重新下载。原始 `TestResults/release-hardening/win32-uef-fallback-build-release.log`。随后显式隐藏、精确 PID/`WaitForExit` 的 `Build/Win32/Release/Inkeys.exe --shutdown-supervisor-tests` pid 13008 exit 0；原始 `win32-uef-fallback-release.{stdout,stderr}.log`。真实 UEF old pid 49820 的异常码 `0xE143C001`，`dump=1/dump_bytes=46440`、`report=1/report_bytes=1033`、`early_restart=0/restart_count=1`；其它用例包括真 15 秒 Close 约 14891 ms 均 PASS。

本轮唯一私有目录 `TestResults/release-hardening/Inkeys Shutdown Test UEF 测试 13008 239682812 0/` 内，保留 `bin/Inkeys/Crash/20260929_032011_49820.dmp`（46,440 字节，测试已核首四字节 `MDMP`）、同名 `.txt`（1,033 字节）与 `restart-12048.txt`。报告明确 `DumpGenerated: true`、`DumpFallbackAttempted: true`、`DumpPrimaryError: 2147942699`，证明高保真先失败后 `MiniDumpNormal` 成功、且即时捕获的首因与旧报告一致。测试检查报告旧 PID/异常码/成功字段和 dump 非零/签名，不能用 report 或 marker 代替 dump。文件大小从 ARM64 控制样本约 52.9 MiB 降到 46 KiB，仅是故障时诊断降级，并非普遍质量降低或性能收益。本轮复制 EXE 已按确切自建路径删除，dump/report 保存在本机忽略目录，不提交/上传。

**状态：Win32 Release 在本机 Windows 11 ARM64 的 x86 仿真中，真实 UEF→低负载有效 dump→旧进程死亡→测试 marker 已验证通过。** 尚需本轮修改后的 ARM64/x64 Release 构建与同 suite 回归，确认高保真成功路径及新报告字段；主 agent 正协调串行槽。沙箱/仿真与真正 Windows 7 SP1+仅 KB2670838 x86/x64 环境不同；Win7、正式 GUI/Office/绘图中异常、FailFast/外部强杀、崩溃后可见 UInk 恢复仍未由本测试通过。若真机高保真/降载都失败，仍应保留发布门禁，不以本机仿真结果外推。
