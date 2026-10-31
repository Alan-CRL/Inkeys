# 实际构建验证记录

## 已完成

- 独立 `PptCOM/PptCOM.csproj /t:Rebuild /p:Configuration=Release /p:Platform=AnyCPU`：ARM64 原生 MSBuild，退出 0，DLL/TLB 均导出。日志 `Build/verification/pptcom-release.log`。
- Win32 Release 默认关闭 Harness：实际属性为 `EnableUi3PresentationHarness=false`、宏 `0`、仓库根 `Build/Win32/Release/` 输出；显式 `PreferredToolArchitecture=x64`，日志确认 `HostX64/x86` CL 和 LINK。限制本机为两个编译进程后完整构建退出 0，370 个原有第三方/产品 warning，0 error，无 C1002。耗时 24:24。日志 `Build/verification/win32-release-default-limited.log`。
- 对新生成的 Win32 EXE 仅作资源数据映射：内嵌 PptCOM.dll SHA-256 为 `AB34C31F9510BE311439B798A35D6980D5DA609D55622C8DFAD339E97733C105`，与维护者随附 DLL 相同；没有执行 EXE。关闭 Harness EXE 副本保存在 `Build/verification/win32-release-noharness/Inkeys.exe`。
- 本地 `Inkeys/PptCOM.dll`、TLB 全程继续跟踪且内容哈希未变；PptCOM 独立构建未自动覆盖它们。

## 执行环境诊断（未修改源码或工程绕过）

1. 默认 Codex 沙箱在标准库编译报 C1902。仅一个函数的 `/Zi` 编译探针在沙箱内也失败，同一安装编译器在 require_escalated 环境成功。后续实际构建使用允许 PDB 服务的执行环境。
2. 首次默认多核编译在多个无关文件报 C1060，其他进程创建同时报 1455/分页文件资源不足，MSBuild logger 也报资源不足。以本地验证命令 `CL_MPCount=2, MultiProcMaxCount=2, /m:1` 控制资源后通过；不修改仓库并发配置。
3. 每次 MSBuild 在同一 PowerShell invocation 先规范化 PATH 并设置 MSBUILDDISABLENODEREUSE=1。后续命令保留原搜索路径到唯一的 Env:Path，避免子工具丢失查找路径。

## 显式启用 Harness

`Inkeys/Inkeys.vcxproj /p:Configuration=Release /p:Platform=Win32 /p:EnableUi3PresentationHarness=true` 完整构建退出 0，374 warnings / 0 errors，耗时 11:45。属性宏 1，ARM64 本机默认 `PreferredToolArchitecture=arm64`；日志确认 `HostArm64/x86` CL 和 LINK。原故障文件重型 runner 及授权实现实际编译，链接生成代码通过，无 C1002。新 EXE 内嵌 DLL SHA-256 同上。日志 `Build/verification/win32-release-harness-enabled.log`。

## ARM64 Debug solution 与无窗口测试

动态定位 `MSBuild/Current/Bin/arm64/MSBuild.exe`，执行 `InkeysRepo.sln /t:Build /p:Configuration=Debug /p:Platform=ARM64 /m:1 /p:CL_MPCount=2 /p:MultiProcMaxCount=2`：退出 0，365 warnings / 0 errors，耗时 8:03。主程序、PptCOM 和两个测试项目全部构建，日志 `Build/verification/arm64-debug-solution.log`。

运行新生成的 `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window`：允许测试临时文件操作的执行环境下退出 0、`PASS animation correctness`，本次新增的 8 个内部参数 / 4 个普通参数 / null 回归也执行通过。日志 `Build/verification/headless-arm64-no-window-unsandboxed.log`。首次沙箱运行 11 个失败仅位于临时文件写入、原子替换与配置回读；相同二进制/参数在允许这些文件操作后全部通过，未改源码或工程绕过。

新 ARM64 Debug EXE 内嵌 DLL SHA-256 与选定 DLL 相同；现有 x64 Release EXE 也作为跨架构资源读取样本校验通过，但本次没有重新构建 x64 Release 或 ARM64 Release。上述资源校验均不执行主程序。

构建 warnings 属于既有第三方、转换/对齐和模块代码；本次没有为零 warning 扩大改动范围。

真实 Windows 2022 三架构发布留待 Actions 验收；本次不触发 workflow。

## 最终收尾 Rebuild 与资源同步

用户授权完整重编并提交。动态定位 ARM64 原生 MSBuild，再次执行 PptCOM Release/AnyCPU `/t:Rebuild`，退出 0，0 warnings / 0 errors。日志确认 CoreClean 删除旧 DLL、CoreCompile 实际编译全部 C# 源文件、ExportTlb 重新导出；输出修改时间均晚于本次构建开始。

将本次 `PptCOM/bin/Release/PptCOM.dll` 与配套 TLB 手动同步到 `Inkeys/`，RC 中 `IDR_DLL1 DLL "PptCOM.dll"` 路径保持。两对 SHA-256 均一致：DLL 为 `AB34C31F9510BE311439B798A35D6980D5DA609D55622C8DFAD339E97733C105`；TLB 为 `C652B5DD0603576033B2E0D15869BE103F23FFDCAA2A1984E53F3B6A14DBA606`。确定性构建使本次新文件与此前随附文件字节相同，二进制无 Git diff。日志 `Build/verification/pptcom-final-rebuild.log`。
