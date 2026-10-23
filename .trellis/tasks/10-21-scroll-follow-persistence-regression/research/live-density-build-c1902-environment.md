# Research: density-floor3 首次完整构建 C1902 环境核对

- Query: 只读核对首次完整ARM64构建的首个C1902、实际CL/PDB路径与direct-cl工具选择；不给源码或缓存做试错。
- Scope: mixed（原日志/工程/脚本/安装文件版本及Microsoft官方错误文档；不执行编译、CLI测试、进程或服务操作）
- Date: 2026-10-23

## Findings

### 首错与实际编译器

`Build/ScrollFollow/motion-tracker-build-density-floor3-production-1023.log:45` 首错：主项目 `VisualMotionTracker.cpp(1,1) C1902 Program database manager mismatch`；`:94–95` Headless的tests源与同一Tracker源也相同失败。三处发生于两个独立编译PDB目标，尚未到本次新算法的语法/类型诊断，不能作为密度权重源码不正确的证据。

日志`:25/:43/:81/:90–91` 已给实际绝对工具路径：

`C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.44.35207/bin/HostArm64/arm64/CL.exe`

两工程均 `<PlatformToolset>v143</PlatformToolset>`，SDK10.0.26100.0；`Directory.Build.props`的ARM64 vcpkg也固定`arm64-windows-static-v143`，未发现把项目升级成v145。当前安装的 `Microsoft.VCToolsVersion.v143.default.txt` 为14.44.35207，与日志一致。

| 目标 | 实际Debug形式 | 实际编译PDB路径 |
| --- | --- | --- |
| Inkeys | `/Zi /FS` | `D:/Project/Inkeys/Repo/Inkeys/Inkeys/Cache/ARM64/Debug/vc143.pdb` |
| InkeysHeadlessTests | `/ZI /FS` | `D:/Project/Inkeys/Repo/Inkeys/InkeysHeadlessTests/Cache/ARM64/Debug/vc143.pdb` |

这是编译器的 `/Fd` PDB，不是Build目录的最终链接Inkeys.pdb；不能照搬先前链接PDB/ILK问题的处理步骤。本次两PDB都存在，仅读取记录到文件长度约68.4MB/15.5MB；不能仅凭存在或长度认定它们损坏或被新版写入。

### direct-cl 与项目工具选择确实不同，但不是已证明的污染

`recorded-motion-replay/match-frame11-diagnostic/compile.cmd` 只调用同安装 `vcvarsall.bat arm64`，没有 `-vcvars_ver`，然后用裸`cl`。本机 `Microsoft.VCToolsVersion.default.txt` 当前为 **14.51.36231**，不同于项目v143的14.44.35207。因此其默认选择机制与完整solution不同；不过该次compile.log只打印Developer Prompt18.11/架构初始化及源文件，没有保存绝对CL路径或`cl /Bv`，不能把“当次实际CL已证实为14.51”追认成事实。

direct-cl使用 `/Z7`，调试信息嵌入obj，且`/Fo /Fe`均指向独立ignored诊断目录，没有 `/Fd` 指向上述vc143.pdb。它通过不能证明项目`/Zi /ZI`的PDB管理通路正常，也没有现有证据说明它改写了两项目编译PDB。其子cmd环境不会通过vcvarsall回写父PowerShell环境；不因默认版本不同就判定是它污染本次build。

仅读安装文件版本，两个HostArm64/arm64目录均齐全且各自内部一致：

- 14.44.35207目录：cl19.44.35229.0；mspdb140.dll/mspdbcore.dll/mspdbsrv.exe均14.44.35229.0。
- 14.51.36231目录：cl19.51.36260.0；上述PDB组件均14.51.36260.0。

这排除了“已查看目录中的组件缺失/明显混搭”这一简单情况，但不能证明运行时DLL实际从哪个位置加载，或PDB管理进程能在当前sandbox里启动/通信。

### 最小安全处理顺序

主会话补充：首次失败build在默认sandbox中运行，当前只读Get-Process未见mspdbsrv；旧direct-cl为/Z7。结合两独立PDB同时C1902，**优先区分runner隔离环境与PDB运行通路**，而非先动缓存。没有mspdbsrv本身不是唯一根因证明，也不需要人为启动或杀死它。

1. 主会话计划保持源码/工程/依赖/缓存/参数不变，在正常宿主 `require_escalated` 用原完整 `InkeysRepo.sln Debug|ARM64` 命令串行再构建一次，属于最小有区分力实验。保留同invocation PATH规范化、node reuse关闭和≥5分钟预算；不把新构建结果写成旧二进制的测试证明。
2. 若同配置正常宿主通过，记录“sandbox编译PDB管理环境失败、正常宿主通过”；无需改代码、toolset或删除PDB，也不由此绝对断定具体受限系统调用。
3. 若仍失败，下一步仅收集实际进程环境/已加载PDB DLL路径版本/明确异常，确认是否有外部新旧工具混用；可能需要用户侧工具链检查。不要直接升级SDK/VS、改/Zi成/Z7、强制换toolset、设置PDB替代位置或全量Clean来掩盖。
4. 不杀共享mspdbsrv、不全局改PATH/VS配置、不清目录或挪走vc143.pdb。本研究没有实施任何上述操作，也未替主会话启动第二次build。

## Official reference

[Microsoft Fatal Error C1902](https://learn.microsoft.com/en-us/cpp/error-messages/compiler-errors-1/fatal-error-c1902?view=msvc-170) 说明该错误涉及PDB版本/管理组件不匹配，建议核对mspdbsrv、mspdbcore和mspdbXXX版本。该文档不能独自证明本例安装损坏或必须清缓存；实际隔离因素要用正常宿主对照验证。

## Related specs / files

- 根AGENTS.md：Codex PATH规范化、原生ARM64完整solution、环境错误不得污染源码、合理等待。
- `Build/ScrollFollow/build-motion-tracker.ps1`：vswhere→ARM64 MSBuild、原完整参数、10分钟进程等待。
- `Inkeys/Inkeys.vcxproj`、`InkeysHeadlessTests/InkeysHeadlessTests.vcxproj`、`Directory.Build.props`：实际v143/IntDir/triplet。

## Caveats / Not Found

本研究没有运行编译器、vswhere、MSBuild、测试或进程查询/操作；没有新采集实际加载模块证据。root提供的sandbox/进程现状与本研究直接读取的日志/文件分别列出。direct-cl当次绝对CL路径未记，运行时PDB组件来源未明。只写research；不改源码/项目/依赖/配置/缓存或Git。
