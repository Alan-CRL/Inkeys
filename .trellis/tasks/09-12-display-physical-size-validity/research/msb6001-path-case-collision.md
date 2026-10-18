# Research: Debug|ARM64 MSB6001 Path/PATH collision

- Query: 调查 ToolTask 启动 `link.exe` 时因继承环境同时包含 `Path` 与 `PATH` 而触发的 MSB6001，并判断仓库/MSBuild 工程配置是否引入或能安全归一化这些变量。
- Scope: mixed
- Date: 2026-09-12

## Findings

### 结论

- 仓库原有 Solution、vcxproj、csproj、顶层 props 中没有发现创建 `Path`/`PATH` 双键的配置。`Directory.Build.props` 只为 vcxproj 配置 vcpkg manifest、triplet 和安装目录（`Directory.Build.props:3-26`）；主工程出现的是 MSBuild 的 `IncludePath`/`LibraryPath` 属性继承，并非进程环境变量赋值（`Inkeys/Inkeys.vcxproj:111-112`、`Inkeys/Inkeys.vcxproj:121-122`）。
- 当前 Codex/PowerShell 子进程的实际继承环境同时含 `Path` 和 `PATH`，且两者值不同；因此问题源头位于启动 MSBuild 之前的宿主环境注入链，不是本功能对显示模块或 `Setupapi.lib` 的工程改动。
- Visual C++ 目标本来就在 `SetBuildDefaultEnvironmentVariables` 中执行 `<SetEnv Name="PATH" Value="$(ExecutablePath)" Prefix="false">`，并把输出写入 MSBuild 属性 `Path`（本机 VS 18 v170：`Microsoft.Cpp.Current.targets:89-102`）。既然失败仍发生在 Link ToolTask，这个内置重设动作本身不能消除大小写重复的原始环境条目。
- 工作区当前并发加入的 `Directory.Build.targets` 定义了 `NormalizeInheritedPathCase`，在内置目标前执行 `<SetEnv Name="Path" Value="" Prefix="false" />`（`Directory.Build.targets:2-5`）。Microsoft 文档说明空值会删除变量，但 Windows/.NET API 按不区分大小写查找已有变量，无法指定删除环境块中的哪一个大小写拼写。
- 隔离子进程实测与上述限制一致：初始键为 `Path,PATH`；调用 `Environment.SetEnvironmentVariable("Path", "", Process)` 后仍保留两个键，其中一个变为空；随后写入 `PATH` 后仍为两个键。`cmd.exe` 的 `set Path=` / `set Path=sentinel` 同样不能消除另一拼写。因此当前 `Directory.Build.targets` 方案不能安全归一化，仍可让 `ProcessStartInfo`/ToolTask 在构造大小写不敏感字典时抛重复键异常，并可能令某个 PATH 版本暂时为空。
- 在 Link 项上设置 `EnvironmentVariables` 也不是可靠补救：ToolTask 必须先从继承环境创建 `ProcessStartInfo.Environment`，然后才应用任务级 override；重复键异常发生在 override 能覆盖 `PATH` 之前。当前仓库的 Link 任务调用没有传该属性（VS v170 `Microsoft.CppCommon.targets:1156-1193`）。
- 可安全处理的边界是 MSBuild 进程外：由创建 MSBuild 进程的宿主生成一个已按 Windows 规则去重、仅含单个 `PATH` 的新环境块，再启动 ARM64 `MSBuild.exe`。这应修在 Codex/runner/launcher 层，或用能直接构造原生环境块的专用启动器；普通 `SetEnv`、PowerShell Env provider、`cmd set` 和项目内 target 均不能可靠修复已经含双键的父环境。
- 不建议把宿主缺陷永久固化为产品仓库的通用 `Directory.Build.targets` workaround。该文件会被根目录下多项目自动导入，而 `SetEnv` 又只适用于 C++ 构建系统；方案既不能完成去重，也扩大了与显示物理尺寸功能无关的构建行为范围。

### Files found

- `Directory.Build.targets` - 当前并发存在的 Path 删除 workaround；实测不能消除大小写双键。
- `Directory.Build.props` - 仅配置 C++ vcpkg 集成，不创建 Path/PATH。
- `Inkeys/Inkeys.vcxproj` - 主 C++ 工程；继承 IncludePath/LibraryPath，无进程环境变量定义。
- `InkeysHeadlessTests/InkeysHeadlessTests.vcxproj` - headless 工程；配置输出、模块与链接依赖，无 Path/PATH 定义（`InkeysHeadlessTests/InkeysHeadlessTests.vcxproj:58-88`）。
- `PptCOM/PptCOM.csproj` - Solution 依赖的托管工程；AfterBuild 仅运行 TlbExp 和复制产物（`PptCOM/PptCOM.csproj:62-65`），与 Link 环境双键无关。
- `InkeysRepo.sln` - Debug|ARM64 的完整 Solution 构建入口；相关构建合同见 `.trellis/spec/native-desktop/build-and-compatibility.md:5-21`。
- `C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Microsoft/VC/v170/Microsoft.Cpp.Current.targets` - C++ 默认环境目标，已有 PATH 重设动作（行 89-102）。
- `C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Microsoft/VC/v170/Microsoft.CppCommon.targets` - Link ToolTask 调用位置（行 1156-1193）。

### Code patterns

- 项目级公共属性按 `$(MSBuildProjectExtension)=='.vcxproj'` 限定 vcpkg 配置：`Directory.Build.props:3-26`。
- 主工程由 `Microsoft.Cpp.Default.props`、`Microsoft.Cpp.props` 和 `Microsoft.Cpp.targets` 提供标准 C++ 构建链：`Inkeys/Inkeys.vcxproj:36`、`Inkeys/Inkeys.vcxproj:79`、`Inkeys/Inkeys.vcxproj:1212`。
- C++ 默认目标在 PrepareForBuild 前段统一重建 PATH；附加一个标准 SetEnv 删除步骤无法处理父环境中的大小写重复键：VS v170 `Microsoft.Cpp.Current.targets:89-102`。
- 当前 workaround 是无条件根级 target，且未限定 vcxproj、ARM64 或检测到双键：`Directory.Build.targets:3-5`。

### External references

- Microsoft Learn, [SetEnv task](https://learn.microsoft.com/en-us/visualstudio/msbuild/setenv-task?view=visualstudio): `SetEnv` 仅适用于 C++；空 Value 删除变量，`Prefix=false` 直接赋值。本机安装的 ARM64 MSBuild 版本为 `18.10.1.42706`。
- Microsoft Learn, [MSB6001 diagnostic code](https://learn.microsoft.com/en-us/visualstudio/msbuild/errors/msb6001?view=visualstudio): MSB6001 的附加异常文本才是具体原因；本次附加信息指向环境字典重复键，而非 linker 参数拼写。
- dotnet/msbuild, [ToolTask.cs](https://github.com/dotnet/msbuild/blob/main/src/Utilities/ToolTask.cs): ToolTask 先取得 `ProcessStartInfo`，再把 `EnvironmentVariables` override 写入 `startInfo.Environment`，所以 task override 不是继承环境字典构造失败的前置修复点。
- dotnet/runtime, [ProcessStartInfo.cs](https://github.com/dotnet/runtime/blob/main/src/libraries/System.Diagnostics.Process/src/System/Diagnostics/ProcessStartInfo.cs): Windows 上 `ProcessStartInfo.Environment` 使用 `StringComparer.OrdinalIgnoreCase` 建字典；输入同时含 `Path`/`PATH` 时存在重复键冲突机制。

### Related specs

- `.trellis/spec/index.md:25-41` - 平台/构建结论必须区分配置事实与实际验证。
- `.trellis/spec/native-desktop/index.md:22-34` - 主程序构建必须走完整 Solution 和当前设备原生架构。
- `.trellis/spec/native-desktop/build-and-compatibility.md:5-21` - ARM64 host MSBuild、Debug|ARM64、完整 Solution 与超时要求。
- `.trellis/spec/native-desktop/build-and-compatibility.md:25-65` - Solution、工程、vcpkg 与 PptCOM 构建链边界。

## Caveats / Not Found

- 研究角色按隔离规则不得读取 `implement.jsonl` 或 `check.jsonl`；任务目录没有其他 JSONL。已先读取用户指定的 `prd.md`、`design.md`、`implement.md` 和 `task.json`。
- 未运行完整 `InkeysRepo.sln` 构建，也未修改或验证当前并发加入的 `Directory.Build.targets`；结论来自静态目标链检查和隔离子进程环境语义实验。
- 未确认是 Codex desktop、sandbox wrapper 还是更外层 runner 首次产生双键；只确认双键已存在于本会话启动的 PowerShell/MSBuild 父环境中。
- 不同大小写的两个 PATH 值当前并不完全相同，因此外部 launcher 去重时必须明确合并/保留策略，不能任意选择其中一个。最稳妥的是由宿主在注入附加路径时维护一个规范化的单一 `PATH`，而不是事后猜测优先级。
