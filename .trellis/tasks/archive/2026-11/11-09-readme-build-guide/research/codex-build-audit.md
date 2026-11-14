只读核查对象为 `dev / 571b49ec`；未修改文件、运行 git、子进程或构建。下列路径均相对仓库根目录。

## A. 项目关系与手工交接

**说法①成立，但手工交接应包含 DLL、TLB 两个文件。** Inkeys 无指向 PptCOM 的项目引用或构建顺序依赖；solution 唯一显式依赖是 HeadlessTests→Inkeys。（`InkeysRepo.sln:6–16`；`Inkeys/Inkeys.vcxproj:1–1496`）

无生成前后复制步骤；PptCOM 构建后仅导出 TLB，native 的 `CleanPptCOM` 仅删除输出目录 DLL。（`PptCOM/PptCOM.csproj:62–65`；`Inkeys/Inkeys.vcxproj:1459–1461`；`Directory.Build.targets:1–19`）

交接：`PptCOM/bin/Release/PptCOM.{dll,tlb}` → `Inkeys/PptCOM.{dll,tlb}`，随后重建 native。DLL 被 RC 嵌入，TLB 被 `#import "PptCOM.tlb"` 使用；manifest 直接读取 `PptCOM/PptCOM.manifest`，无需另复制；PDB/config/Office DLL 未列为交接必需品。（`PptCOM/PptCOM.csproj:23,47–64`；`Inkeys/Inkeys.rc:166–174`；`Inkeys/IdtPlug-in.cpp:317`）

CI 则自动下载匹配 DLL/TLB 到 `Inkeys/`，不是项目依赖。（`.github/workflows/build-windows.yml:438–450`）

## B. solution 配置与输出

“✓”表示存在 `Build.0`；PptCOM 和其测试均映射为 **Release|Any CPU**。

| 配置 | Inkeys | PptCOM | PptCOM.Tests | HeadlessTests |
|---|---|---|---|---|
| Debug\|ARM64 | ✓ | ✓ | ✓ | ✓ |
| Debug\|Win32 | ✓ | ✓ | ✓ | ✓ |
| Debug\|x64 | ✓ | ✓ | ✓ | ✓ |
| Release\|ARM64 | ✓ | ✓ | — | — |
| Release\|Win32 | ✓ | ✓ | — | — |
| Release\|x64 | ✓ | ✓ | — | — |

证据：`InkeysRepo.sln:44–94`。

native 两项目输出 `Build/<Platform>/<Configuration>/`；两个 C# 项目各自输出 `bin/Release/`。（`Inkeys/Inkeys.vcxproj:127–173`；`InkeysHeadlessTests/InkeysHeadlessTests.vcxproj:62`；两个 `.csproj:23、17`）

## C. PptCOM 构建要求

目标确为 **net40**；Release 为 AnyCPU、`Prefer32Bit=false`、允许 unsafe、禁用自动 COM 注册。COM 可见；项目无强名称签名键配置。（`PptCOM/PptCOM.csproj:4–30`；`PptCOM/Properties/AssemblyInfo.cs:20–23`）

Office PIA 来自 NuGet：PowerPoint `15.0.4420.1018/lib/net20`、Office Core `15.0.0/lib/net35`，存于根目录 `Package/`，均启用嵌入互操作类型；不是声明依赖本机 Office/GAC。（`PptCOM/packages.config:3–4`；`PptCOM/PptCOM.csproj:47–53`；`NuGet.config:4`）

需 C#/.NET 桌面构建工具、net40 参考程序集及 `$(SDK40ToolsPath)TlbExp.exe`。（`GithubRes/CompilationProcess_zh-CN.md:77`；`PptCOM/PptCOM.csproj:11,61–64`）

现代 VS 不提供 net40 targeting pack，较新 Developer Pack 也不包含旧目标程序集。（[微软版本说明](https://learn.microsoft.com/dotnet/framework/install/versions-and-dependencies)、[开发包说明](https://learn.microsoft.com/dotnet/framework/install/guide-for-developers)）

**但说法②的“必须升级”不成立：**仓库虽无 `Microsoft.NETFramework.ReferenceAssemblies` 包，却提供 `ActionsRes/v4.0.zip`；CI 将其 `v4.0/` 放到 `C:\Program Files (x86)\Reference Assemblies\Microsoft\Framework\.NETFramework\`。（`PptCOM/packages.config:1–5`；`.github/workflows/build-windows.yml:163–181`）

只有 4.7.2/4.8 的开发者，可沿用该参考程序集补救、恢复 PIA 并确保 TlbExp 可用；或主动 retarget 到已安装的 targeting pack。只有运行时不足以编译。

## D. 文件头 retarget 指南

对应 `PptCOM/PptCOM.cs:14–22`：

1. “目标默认为 4.0”准确；“本机版本不同就切换”混淆运行时与目标程序集。
2. 属性页修改目标仍有效；测试项目也需同步目标，因其引用 PptCOM。（`PptCOM.Tests/PptCOM.Tests.csproj:11,29–32`）
3. manifest 文件仍存在，但 `runtimeVersion` 表示 CLR，不能改成 `v4.7.2/v4.8`；4.x 共用 CLR4，现值 `v4.0.30319` 合理。（`PptCOM/PptCOM.manifest:12`；[微软说明](https://learn.microsoft.com/dotnet/framework/install/versions-and-dependencies)）
4. 清理重建仍可用，但遗漏 DLL/TLB 手工交接。

`App.config` 仍含 net40 SKU，且写成非标准大小写 `<StartUp>`；retarget 应检查它。RC 仍嵌入原 manifest；native 通过 activation context 加载，所查源码未见 `CLRCreateInstance`、`ICLRMetaHost` 或显式 CLR 版本门禁。（`PptCOM/App.config:3–5`；`Inkeys/Inkeys.rc:166`；`Inkeys/IdtMain.cpp:3660–3688`）

## E. native 构建

使用 MSBuild VC 工程：v143、SDK `10.0.26100.0`、Unicode、C++20，包含 HLSL/FXC；应准备 C++ 桌面开发及对应目标架构工具。“Windows 应用程序开发”整个工作负荷的必要性未获工程证明。（`Inkeys/Inkeys.vcxproj:37–80,190,1320–1428`）

Manifest 模式自动安装依赖，无需 `vcpkg integrate install`；overlay 位于 `VcpkgTriplets/`，静态 CRT/库、v143；安装根分别为 `VcpkgInstalled/{Win32,Win64,Arm64}/`。（`Directory.Build.props:3–26`；`Directory.Build.targets:7–18`；`vcpkg-configuration.json:2–4`；`VcpkgTriplets/*:1–4`）

真实目录为 **`Vcpkg`**；子模块还有 `Inkeys/additional/HiMsg`。（`.gitmodules:1–7`）

宿主仅在 AMD64 条件下默认 x64，ARM64 留给工具链默认；Harness 为 Debug 开、Release 关，可覆盖。（`Inkeys/Inkeys.vcxproj:34–36,83–87`）

未发现强制纯 ASCII/无空格的项目检查；关键外部命令已有路径引号，因此只能列作保守建议，不能证明任意路径均可用。（`PptCOM/PptCOM.csproj:64`；`Vcpkg/scripts/buildsystems/msbuild/support/InstallImplementation.targets:9–15`）

## F. 测试与其他工程

PptCOM.Tests 是 net40 控制台程序，依赖 PptCOM；HeadlessTests 使用 v143/C++20/同版 SDK，关闭自身 vcpkg 安装，依赖 Inkeys 先准备库，并链接 freetype/jsoncpp 等。默认构建范围见 B，构建不等于运行测试。（`PptCOM.Tests/PptCOM.Tests.csproj:8–37`；`PptCOM.Tests/Program.cs:349–370`；`InkeysHeadlessTests/InkeysHeadlessTests.vcxproj:34–90`；`InkeysRepo.sln:13–15`）

Modeler 工具及测试属于另一 solution，不在主 solution。（`inkStrokeModelerTest.sln:6–9`）

## G. 当前指南应修正之处

`GithubRes/CompilationProcess_zh-CN.md`：

- **79行**：“4.0 SDK 或更高”不能保证满足 net40；应补 C 中参考程序集、ZIP 补救及 TlbExp。
- **82–85行**：缺少 PIA NuGet 恢复步骤；CI 实际执行了恢复。
- **20行**：绝对路径禁令缺少证据。
- **11行**：第二工作负荷必要性未证实；**39行**目录大小写宜统一为 `Vcpkg`。
- **59行**仅说明 Release 测试排除，宜补完整 B 表，避免误以为 solution build 仅需 C++。
- **12、65–68行**的最低小版本、故障范围及开发者环境版本，不能仅由工程配置验证。

**4、41、53、59、85行的解耦、集成方式、输出、Harness、双文件交接基本准确。** README 编译说明只是链接，无独立旧步骤。（`README.md:89–90`）

## 事实 / 推测 / 待确认

- **事实**：上述工程、源码、CI 的声明及路径。
- **推测**：C++ 桌面组件足够，无需完整第二工作负荷；含空格路径可能可用。
- **待确认**：ZIP 补救在各版现代 VS/ARM64 上的实测、TlbExp 工具定位、编译器最低小版本。按要求未执行构建或测试。