# Research: ARM64 C1902 构建环境诊断

- Query: 只读诊断 `Build/ScrollFollow/freeze-reuse-build.log` 中的 C1902；核对实际 ARM64 MSBuild、v143 编译器/PDB 组件、PATH/VC 环境及 PDB 服务，区分环境问题与旧构建产物。
- Scope: mixed；本机工具链与仓库历史证据为主，Microsoft 官方文档为辅。不构建、不运行 GUI、不终止进程、不清理输出、不改源码/工程/依赖/全局配置，不执行 Git 操作。
- Date: 2026-10-21

## Findings

### 结论与证据强度

- **直接确认：当前 v143 安装文件版本匹配，没有找到工具链混用的直接证据。** ARM64 `cl /Bv` 实际加载的编译器 passes 和 `mspdb140.dll` 均来自同一个 v143 `Hostarm64/arm64` 目录。
- **直接确认：当前仓库曾在相同 v143 版本下出现由执行边界触发的 C1902。** 10/18 的全新 `/Zi /FS` 无头文件探针对照，在普通 sandbox 中失败，在允许 PDB 服务的环境中成功，随后同一完整 solution 成功。见下方原始日志与历史结论；这比单凭 C1902 错误文字推断安装损坏更有辨别力。
- **合理推断：本轮先按 PDB 服务/IPC 执行环境调查最合适；尚不能唯一归因。** 18:44 的失败日志没有保存原始 `exec.sandbox_permissions` 元数据，也没有 compiler command line，因此不能确认它确实运行在 normal host，更不能据此断言安装损坏。
- **未确认旧 PDB 损坏或来自较新工具链。当前不建议清理 PDB。** 主项目和 headless 的 compiler PDB 均仍是当天先前成功构建时的时间戳；时间戳不能证明内部结构完好，但没有支持“本次失败写坏 PDB”或“由新版工具链写入”的证据。
- 主会话已安排 implement worker 用 `require_escalated` 的 normal host、原生 ARM64 MSBuild、完整 solution 进行下一次构建；本研究代理没有执行该构建，其结果必须由主会话另行记录。

### 失败与已有成功证据

| 文件 | 直接观察 |
| --- | --- |
| `Build/ScrollFollow/freeze-reuse-build.log:10` | 主项目 `IdtMagnification.cpp`、`MagCaptureService.cpp`、`Window.cpp` 均在 `(1,1)` 报 C1902；同文件 `:29` 起 headless 的四个 TU 同样失败。未看到业务源码语法或类型错误。日志更新时间 2026-10-21 18:44:21 +08:00。 |
| `Build/ScrollFollow/capture-build-arm64-final.log:25` | 先前实际 CL command line 使用 `.../VC/Tools/MSVC/14.44.35207/bin/HostArm64/arm64/CL.exe`，带 `/Zi /FS`，compiler PDB 为 `Cache/ARM64/Debug/vc143.pdb`。同文件 `:26`、`:27` 也覆盖后来失败的 capture/Idt TU。 |
| `Build/ScrollFollow/capture-check-build-arm64-final.log:99` | 同日最后审查构建的 headless 仍使用相同 ARM64 v143 CL。`:233` 起为 `Build succeeded`、0 warning、0 error；`:239` 为 8.68 秒。日志时间 14:43:40 +08:00。 |
| `.trellis/tasks/archive/2026-10/10-18-pptcom-dynamic-compatibility/research/verification.md:41` | 历史完整 solution 的 sandbox C1902；记录 cl 19.44.35229 与 PDB 三组件 14.44.35229 匹配。 |
| 同历史研究 `:42`、`:43` | 全新无头文件单函数 `/Zi /FS` 探针，普通 sandbox exit 2/C1902，完全相同命令及环境参数在 sandbox 外 exit 0；随后完整 solution exit 0/114.98 秒，后续增量 exit 0/24.27 秒。没有改源码、SDK、依赖、工程或关闭 PDB 检查。 |
| `Build/Validation/pptcom-dynamic-20261018/pdb-probe-sandbox.log:1` | 原始日志含 `pdb-probe.cpp` 与 C1902。对应源为 `pdb-probe.cpp` 的一个返回 42 的函数；`pdb-probe-unrestricted.log:1` 仅正常源文件名，没有 C1902。探针退出码由上述历史研究保存，不能仅从日志无错误文字推出退出码。 |
| `Build/Validation/pptcom-dynamic-20261018/solution-debug-arm64-unrestricted.stdout.log` | 历史 normal host 完整构建输出包含生成两个 native EXE。 |

### 当前安装与版本（本轮实际读取）

通过 `${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath` 定位，未硬编码 Visual Studio 版本作为发现入口。当前仅发现一个完整、无需重启的安装：

- 安装根：`C:/Program Files/Microsoft Visual Studio/18/Community`。
- VS installation version：`18.11.12306.390`，product display version `18.11.0`，updateDate `2026-10-19T04:24:51.9618685Z`。
- 原生 MSBuild：`MSBuild/Current/Bin/arm64/MSBuild.exe`，file version `18.11.0.47511`。
- `VC/Auxiliary/Build/Microsoft.VCToolsVersion.v143.default.txt:1` 为 **14.44.35207**。
- `Microsoft.VCToolsVersion.default.txt:1` 为 **14.51.36231**；这是默认新工具集，不代表本项目 v143 会使用它。不能把 VS/MSBuild 的 18.x 或安装默认 14.51 直接当成项目实际编译器版本。

| MSVC 目录 | cl.exe / c1xx.dll / c2.dll file version | mspdb140.dll / mspdbcore.dll / mspdbsrv.exe file version |
| --- | --- | --- |
| `14.44.35207/bin/Hostarm64/arm64` | 三者均 `19.44.35229.0`；product version 均 `14.44.35229.0` | 三者均 `14.44.35229.0` |
| `14.51.36231/bin/Hostarm64/arm64` | 三者均 `19.51.36260.0`；product version 均 `14.51.36260.0` | 三者均 `14.51.36260.0` |

`cl.exe /Bv` 在没有传源码的情况下列出 v143 所有 compiler passes、link.exe 与 mspdb140.dll 的实际路径和版本；它预期以 D8003（missing source filename）/exit 2 结束，**这是只读版本查询，不是编译失败证据，也没有测试 PDB 服务通信**。

### 项目配置与只读属性求值

- `Inkeys/Inkeys.vcxproj:75` 的 Debug|ARM64 配置使用 v143（`:78`）；相应中间目录在 `:173`。
- `InkeysHeadlessTests/InkeysHeadlessTests.vcxproj:40` 声明 Debug 使用 v143，中间目录在 `:62`。
- `Inkeys/Inkeys.vcxproj:36` 只有 AMD64 host 的条件默认；本次查询及建议命令用参数明确指定 `PreferredToolArchitecture=arm64`，不编辑工程。
- 本轮对两个 vcxproj 均执行了 **`-getProperty`、未传 `-target`** 的 MSBuild 属性求值，exit 0；没有构建目标。执行前在同一 PowerShell invocation 内先规范化 PATH 并设 `MSBUILDDISABLENODEREUSE=1`。
- 两者实际求值一致：`PlatformToolset=v143`、`VCToolsVersion=14.44.35207`、`VCToolsInstallDir=<当前安装>/VC/Tools/MSVC/14.44.35207/`、`PreferredToolArchitecture=arm64`。`ExecutablePath` 第一项为该目录的 `bin/HostArm64/arm64`，`CLToolPath` 与 `CLToolExe` 均无额外覆盖。
- 仅查询属性不能证明下一次 CL/PDB 服务启动成功；已有 compile command line 与 `/Bv` 分别补充了实际工具路径证据。

### 环境与 PDB 服务盘点

- 默认 runner 的 `PROCESSOR_ARCHITECTURE=ARM64`。未设 `CL`、`_CL_`、`VCToolsVersion`、`VCToolsInstallDir`、`VCINSTALLDIR`、`VSINSTALLDIR`、`VisualStudioVersion`、`PreferredToolArchitecture`、`_PDB` 或 `_PDBSRV_ENDPOINT_` 的环境覆盖。
- 当前目录与 PATH 各目录顶层没有发现 `cl.exe`、`mspdb140.dll`、`mspdbcore.dll` 或 `mspdbsrv.exe` 的 shadow copy。项目求值后正确 VC 工具目录位于 `ExecutablePath` 最前。
- 默认 runner 的 TEMP/TMP 是用户 `AppData/Local/Packages/sandbox.{...}/AC/Temp`。这说明本轮只读默认调用仍使用 sandbox 临时目录，不能反推 18:44 构建的实际 token 或权限。之前成功审查记录使用仓库 `Build/ScrollFollow/TestTemp`，见本任务 `research/check.md:36`。
- `Get-Process -Name mspdbsrv` 未发现进程。默认 runner 的 CIM 查询返回 `Access to a CIM resource was not available to the client`，**不能把该受限查询的空结果当作全机无进程的证据**。
- 因默认 CIM 被拒，使用经自动审批的 **`require_escalated` 只读** `Win32_Process WHERE Name='mspdbsrv.exe'` 查询 ProcessId、ExecutablePath、CreationDate。退出 0，确认为 **0 个运行中的 mspdbsrv**。未终止或启动任何 PDB 服务；因此本轮没有可供核对的活跃 server 映像版本。
- `Inkeys/Cache/ARM64/Debug/vc143.pdb`：65,556,480 bytes，mtime 14:00:23 +08:00；`InkeysHeadlessTests/Cache/ARM64/Debug/vc143.pdb`：14,946,304 bytes，mtime 14:43:37 +08:00。均早于 18:44 失败。

### 给主会话的最小一致工具链调用

以下是下一次由实现/主会话执行的 **完整 solution 增量构建建议**，本研究没有执行它。必须在 `require_escalated` 的 normal host 运行，给完整构建至少 5 分钟，不与其他同配置构建并发。保存 exec 的权限元数据、退出码和完整日志；仅打印“normalhost”文字不能证明权限状态。

```powershell
$ErrorActionPreference = 'Stop'
$taskSavedPath = $env:Path
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:Path = $taskSavedPath
$env:MSBUILDDISABLENODEREUSE = '1'

$taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$taskNativeMsbuild = @(& $taskVswhere -latest -products '*' `
    -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\arm64\MSBuild.exe')
if ($taskNativeMsbuild.Count -ne 1 -or !(Test-Path -LiteralPath $taskNativeMsbuild[0])) {
    throw 'Native ARM64 MSBuild not uniquely found'
}
& $taskNativeMsbuild[0] InkeysRepo.sln /nologo /nr:false /m:1 `
    /p:BuildInParallel=false /p:Configuration=Debug /p:Platform=ARM64 `
    /p:PreferredToolArchitecture=arm64 /v:minimal
exit $LASTEXITCODE
```

现有工程已经选择正确 v143，不需要强制 `VCToolsVersion`，也不需要额外加载可能选到默认 14.51 的 dev shell。当前不要清理 PDB、复制 DLL、关闭 `/Zi` 或 `/FS`、切换工程工具集、升级/修复安装或修改全局 PATH。

如果明确的 normal host 同工具链构建仍复现，则先使用**新的输出位置**做最小 `/Zi /FS` PDB 服务对照，由主会话另行授权/安排，不复用或覆盖当前项目 PDB。只有“新 PDB 探针成功、项目仍失败”并且进一步定位到具体旧产物时，才考虑一次生成物清理/完整重建。届时优先复用完整 solution 的既有 Clean/Rebuild 目标；不能只删 compiler PDB 后保留依赖旧 type-server 信息的 obj/ifc 继续增量链接。若采用目录级清理，必须先核对解析后的绝对目标仅为仓库内 `Inkeys/Cache/ARM64/Debug` 与 `InkeysHeadlessTests/Cache/ARM64/Debug`，保留日志和证据。**本轮尚未满足清理条件，未执行清理。**

### Files found

- `AGENTS.md`：完整 solution、Debug|ARM64、ARM64 原生 MSBuild、至少五分钟、PATH 规范化、环境错误不得污染源码等约束。
- `.trellis/workflow.md`：研究必须落盘，研究代理限定写入任务 research 目录。
- `.trellis/spec/native-desktop/build-and-compatibility.md`：主产品构建、实际 host/target 证据、环境与工程边界。
- `.trellis/spec/native-desktop/index.md`、`.trellis/spec/native/index.md`、`.trellis/spec/native/quality-and-validation.md`：主产品与独立 demo 的构建验证入口边界。
- `Inkeys/Inkeys.vcxproj`、`InkeysHeadlessTests/InkeysHeadlessTests.vcxproj`：v143、ARM64 配置、两份独立中间目录。
- `Build/Validation/ppt-controls-20261018/build-debug-arm64.ps1:1`：已有规范化 PATH、vswhere、ARM64 host MSBuild 的调用模式；本研究未执行该脚本。
- 本任务 `research/check.md:36`：此前构建命令、TEMP/TMP 与退出码记录。
- 上述失败/成功日志与 10/18 历史 research：当前诊断及 runner 对照的证据。

### External references

- [Microsoft Learn: Fatal Error C1902](https://learn.microsoft.com/en-us/cpp/error-messages/compiler-errors-1/fatal-error-c1902?view=msvc-170)：通常核查 mspdbsrv/mspdbcore/mspdb DLL 的缺失或版本不匹配。文档没有声明“任何 C1902 都是安装损坏”；本机执行边界结论来自上述对照。
- [Microsoft Learn: /FS](https://learn.microsoft.com/en-us/cpp/build/reference/fs-force-synchronous-pdb-writes?view=msvc-170)：`/Zi` PDB 写入经 MSPDBSRV.EXE 序列化；因此服务/执行环境值得检查，而单纯降低 MSBuild 并发不能证明服务可用。
- [Microsoft Learn: Evaluate items and properties](https://learn.microsoft.com/en-us/visualstudio/msbuild/evaluate-items-and-properties?view=vs-2022)：MSBuild 17.8+ `-getProperty` 未同时指定 target 时只求值、不构建；本次使用 MSBuild 18.11。

## Caveats / Not Found

- 没有获得 18:44 失败那次 exec 的原始权限记录或完整 compiler command line；不能把它确认成 normal host 安装错误，也不能凭历史把本次根因记作已确认 sandbox IPC 问题。
- 未执行新 `/Zi /FS` 探针、构建、Clean/Rebuild、PDB 内部结构解析或真实软件测试。`/Bv` 和 `-getProperty` 是只读诊断，不是本轮功能编译验证。
- 当前无运行中 PDB server，只能核对磁盘组件，无法核对故障发生时 server 的映像版本/endpoint。
- 主会话的后续 normal host 构建结果将决定是否还有环境阻塞；这份研究不提前宣称新 FreezeMag/PrintWindow 实现构建通过。
