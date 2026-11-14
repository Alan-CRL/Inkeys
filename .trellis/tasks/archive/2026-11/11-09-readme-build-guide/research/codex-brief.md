# Task: Audit how Inkeys is actually built on `dev`, to rewrite the build guide

Read-only investigation. Do NOT modify any file. You cannot run git or subprocesses; read files directly.

## Repository
`D:\Project\Inkeys\Repo\Inkeys`, branch `dev` (HEAD 571b49ec). Solution `InkeysRepo.sln`: native C++ `Inkeys/Inkeys.vcxproj`, C# COM add-in `PptCOM/PptCOM.csproj`, plus test/tool projects.

## Owner's statements (verify each against the files, do not assume)
1. Inkeys and PptCOM no longer have a project dependency. To ship the latest PptCOM.dll, it must be copied manually.
2. PptCOM targets .NET Framework 4.0. Building it needs the .NET Framework 4.0 targeting environment; if the dev machine only has e.g. 4.7.2, the developer must "upgrade" (retarget). `PptCOM/PptCOM.cs` header comments (lines ~1-40) describe a retarget procedure that may be outdated.

## Current docs to check against
- `GithubRes/CompilationProcess_zh-CN.md` (current Chinese build guide) and `README.md` section "## 编译说明".

## Answer, with file:line evidence
A. Project graph: does `InkeysRepo.sln` / `Inkeys.vcxproj` contain any ProjectReference / ProjectDependencies / build-order link to PptCOM? Any pre/post-build event or `Directory.Build.targets` step that copies PptCOM output? How does Inkeys consume PptCOM (RC resource embedding of `Inkeys/PptCOM.dll`? `#import` of a `.tlb`? exact paths)? Which files exactly must be manually copied (DLL, TLB, others?), from where to where?
B. Solution configurations: list every solution config|platform and which projects build in each (esp. whether PptCOM and tests are built in Release/Debug of each platform). Output dirs (`Build/<Platform>/<Configuration>`?).
C. PptCOM build requirements: TargetFrameworkVersion, references (Office PIA? where from: NuGet / bundled DLL / GAC?), manifest (`PptCOM.manifest` runtimeVersion), signing key, COM visibility, platform target. What VS workload / targeting pack is required? Note: .NET Framework 4.0 targeting pack is not offered by modern VS installers — check whether the project/ repo provides a workaround (e.g. Microsoft.NETFramework.ReferenceAssemblies NuGet package in csproj or NuGet.config / packages), and state concretely what a developer with only 4.7.2/4.8 must do.
D. Evaluate the retarget guide in `PptCOM/PptCOM.cs` header step by step: which steps are still accurate, which reference files/settings that no longer exist or changed (check manifest, csproj, app.config, RC, Inkeys-side code that checks CLR version, e.g. grep for `v4.0.30319`, `runtimeVersion`, `supportedRuntime`, `CLRCreateInstance`, `ICLRMetaHost`).
E. Native build: required VS workloads/components, toolset (v143?), Windows SDK version, C++ standard, vcpkg manifest mode settings (`Directory.Build.props`, `vcpkg-configuration.json`, `VcpkgTriplets/`, installed dir `VcpkgInstalled/`), whether `vcpkg integrate install` is needed, submodules (`.gitmodules`), the real casing of the vcpkg dir, any properties like `EnableUi3PresentationHarness`, `PreferredToolArchitecture`. Is the "pure ASCII path without spaces" requirement still real (look for reasons in props/targets/scripts)?
F. Test projects in the solution (`PptCOM.Tests`, `InkeysHeadlessTests`, others): what they need, whether they're built by default.
G. Any errors/outdated statements in `GithubRes/CompilationProcess_zh-CN.md`, listed concretely.

## Output
Simplified Chinese Markdown report, ≤ 2500 Chinese characters, sections A–G, each claim with evidence path:line. End with a section "事实 / 推测 / 待确认" separating proven facts from inferences.
