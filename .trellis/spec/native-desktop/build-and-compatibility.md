# Build and Compatibility

本文沿用根 Spec 的证据分级：`【直接确认】`、`【合理推断】`、`【待确认】`、`【历史/兼容】`。工程中存在某项配置，只能证明该构建入口被声明，不能证明产物已在对应系统或设备上运行验证。

## 主构建入口

`【直接确认】` 仓库根 `AGENTS.md` 对自动化代理明确要求：

- 构建 `InkeysRepo.sln`，不要单独构建 `Inkeys/Inkeys.vcxproj`；
- 使用 `Debug | ARM64`；
- 使用 ARM64 host 的 `MSBuild.exe`；
- 构建超时至少 5 分钟；
- 原因是 `Inkeys` 依赖 `PptCOM`，完整 Solution 构建用于生成并带入 DLL/TLB。

等价参数形式为：

~~~text
MSBuild.exe InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64
~~~

`MSBuild.exe` 的绝对路径取决于本机 Visual Studio 安装，不在 Spec 中硬编码。本次 Bootstrap 及本轮证据审计均未执行构建，因此不把上述配置写成“已经构建通过”。

`【历史/兼容】` `GithubRes/CompilationProcess_zh-CN.md` 另有面向一般贡献者的路径：使用仓库内预编译 `PptCOM.dll`/`PptCOM.tlb` 时，可取消项目依赖并单独构建 `Inkeys`。它与当前 `AGENTS.md` 的代理规则适用对象不同；Codex 应遵守 `AGENTS.md`，普通开发者的正式首选流程仍为 `【待确认】`。

## Solution 与工程配置事实

| 证据 | `【直接确认】`的内容 | 不能据此推出 |
| --- | --- | --- |
| `InkeysRepo.sln` | 声明 `Debug/Release × Win32/x64/ARM64`；包含 `Inkeys`、`PptCOM` 和 `I18n` Solution Folder | 六种组合都已成功构建或运行 |
| `InkeysRepo.sln` 的 `ProjectDependencies` | `Inkeys` 依赖 GUID `{A7B02228-179F-4B8E-BA8F-82D50066FB66}`，与 `PptCOM/PptCOM.csproj` 的 `ProjectGuid` 一致 | `Inkeys.vcxproj` 内的重复引用一定无害 |
| `Inkeys/Inkeys.vcxproj` | Windows/Unicode 可执行程序，v143、SDK `10.0.26100.0`、C++20、`/utf-8`、module dependency scan、静态 CRT | 这些设置构成完整运行兼容矩阵 |
| `Inkeys/Inkeys.vcxproj` | 输出目录是 `Build/$(Platform)/$(Configuration)`；中间目录是 `Inkeys/Cache/...`；ARM64 定义 `IMGUI_DISABLE_SSE` | ARM64 已覆盖所有第三方库和 Office 组合 |
| `InkeysRepo.sln` 的项目配置映射 | 主 Solution 的各项配置把 `PptCOM` 映射到 `Release | Any CPU` | AnyCPU 与任意 Office 位数组合都已验证 |
| `.github/workflows/build-windows.yml` | CI 被配置为用完整 Solution 构建 `Release` 的 Win32、x64、ARM64 | 当前或历次 CI 实际成功；本轮未查询运行记录 |

## Vcpkg 集成

`【直接确认】` `Directory.Build.props`、`Directory.Build.targets`、`vcpkg.json` 和 `VcpkgTriplets/` 共同表明：

- 使用 manifest 模式，主 manifest 是 `vcpkg.json`；
- x86、x64、ARM64 使用仓库自定义静态 triplet；
- targets 在 C++ module 扫描前触发 manifest 安装目标；
- 构建说明不要求执行 `vcpkg integrate install`。

`vcpkg.json` 当前直接依赖并通过 overrides 固定：

| 包 | override 版本 |
| --- | --- |
| openssl | 3.0.8#2 |
| cpp-httplib | 0.22.0 |
| jsoncpp | 1.9.6 |
| libcuckoo | 0.3.1 |
| lunasvg | 3.5.0 |
| magic-enum | 0.9.7#1 |
| concurrentqueue | 1.0.4#1 |
| unordered-dense | 4.7.0 |
| spdlog | 1.17.0 |
| opencv4 | 4.10.0#3 |

`【直接确认】` `Vcpkg` 是第三方依赖子模块。本项目代码任务不得递归修改它；依赖变更应同时审查 manifest、triplet、许可和随附第三方产物。

## Scenario: OpenCV manifest 静态接入

### 1. Scope / Trigger

当任务引入或调整 OpenCV 时，使用根 manifest 和现有 x86/x64/ARM64 v143 静态 triplet；不直接修改 `Vcpkg/` port。

### 2. Signatures

~~~json
{
  "name": "opencv4",
  "default-features": false,
  "features": ["dshow", "msmf", "intrinsics", "thread"]
}
~~~

override 必须使用 `{"name":"opencv4","version":"4.10.0","port-version":3}`；该 port 在当前版本库采用 `version` 方案，不使用 `version-string`。

### 3. Contracts

- `builtin-baseline` 保持 `99a97de2cb371449d4fb9dc970f2ac562d689ec2`。
- 直接 feature 集合只有 `dshow`、`msmf`、`intrinsics`、`thread`；不启用 default features、world、FFmpeg、DNN、GUI、TBB 或 OpenMP。
- `VCPKG_CRT_LINKAGE` 和 `VCPKG_LIBRARY_LINKAGE` 均为 `static`，工具集为 v143。
- 对外许可声明复用 `ThirdpartyLicenses/Apache License 2.0`。

### 4. Validation & Error Matrix

| 条件 | 处理 |
| --- | --- |
| manifest 解析为其他 OpenCV 版本 | 停止，核对 baseline 和 override，不自动升级 |
| 安装 feature 出现额外项 | 停止，检查 `default-features:false` 和依赖图 |
| 产物出现 OpenCV/FFmpeg DLL | 停止，检查 triplet 和 OpenCV features |
| ARM64 完整 Solution 构建失败 | 保留 vcpkg/buildtrees 日志；不用动态链接或默认 features 规避 |

### 5. Good / Base / Bad Cases

- Good：`opencv4 4.10.0#3` 在 `arm64-windows-static-v143` 安装，只列出四个指定 feature，完整 Solution 通过。
- Base：官方 port 生成额外静态模块库；未被符号引用的对象不进入 EXE，不因此引入 overlay-port。
- Bad：使用 `opencv`、开启默认 features、新建重复 triplet 或在第一阶段修改 `BUILD_LIST`。

### 6. Tests Required

- JSON 解析并断言 baseline、dependency features 和 override。
- 使用 ARM64 host MSBuild 构建 `InkeysRepo.sln` 的 `Debug|ARM64`。
- 检查 `VcpkgInstalled/Arm64/vcpkg/status` 中版本/四个 feature，并用 PE 导入表确认最终 EXE 无 OpenCV/FFmpeg 动态依赖。

### 7. Wrong vs Correct

Wrong：`"opencv"` 或缺省 `default-features:false`。

Correct：使用上述 `opencv4` 对象和四个显式 feature，版本通过 `version` + `port-version` 锁定。

## PptCOM 构建链

`【直接确认】` `PptCOM/PptCOM.csproj` 是 .NET Framework 4.0、AnyCPU class library，引用 `Package/` 下的 Office/PowerPoint Interop。其 `AfterBuild` 调用 SDK 4.0 的 `TlbExp.exe` 生成 `PptCOM.tlb`，再将 DLL/TLB 复制到 `Inkeys/`。

`【待确认；风险观察，不是已确认缺陷】` `Inkeys/Inkeys.vcxproj` 尾部有两个都指向 `..\PptCOM\PptCOM.csproj` 的 `ProjectReference`，分别记录 `{66F84848-5C28-4398-80D7-78F6189FC442}` 与 `{404F6E99-A0CA-45B2-8CAA-A2D819DA8EFF}`；两者都不同于 csproj/Solution 使用的 `{A7B02228-179F-4B8E-BA8F-82D50066FB66}`。本轮未运行 MSBuild，无法判断 Visual Studio/MSBuild 的实际解析结果，也不能据此断言重复引用导致构建错误。修改前应由维护者确认它们是历史兼容项还是应清理的工程异常。

## 随附依赖与第三方边界

`【直接确认】` `Inkeys/additional/`、`Package/`、`ThirdpartyLicenses/` 和工程链接项可见 Dear ImGui、stb、WinToast、zip/hash、HiMsg、Office Interop 等随附代码或库。HiMsg 以 `Inkeys/additional/HiMsg` Git submodule 固定版本，并复用主仓 vcpkg 依赖；HiEasyX/EasyX 源码、头文件和静态库已删除。主项目还使用 Windows SDK/COM、D2D、DWrite、D3D11、DXGI、GDI+ 和 RealTimeStylus。

`【历史/兼容】` `Timeout/InkeysTimeout/json/` 内嵌一份 JsonCpp 源码；它不是主项目 vcpkg JsonCpp 使用方式的证据。除非任务明确覆盖 Timeout，不要把两套依赖路径合并。

## Windows 与架构：声明、配置与验证分开记录

| 级别 | 证据 | 结论 |
| --- | --- | --- |
| 项目声明 | `README.md` | 声明最低 Windows 7 RTM/SP0，并列出 32 位、64 位、ARM64 |
| 工程配置 | `InkeysRepo.sln` | 存在 Win32、x64、ARM64 的 Debug/Release 配置 |
| 工程配置 | `Inkeys/Inkeys.vcxproj` | 仅 `Release|Win32`、`Release|x64` 显式写 `MinimumRequiredVersion=6.01`；`Release|ARM64` 写 `6.02`；三个 Debug 配置未显式写该属性 |
| 工程配置 | `Inkeys/Inkeys.vcxproj` | ARM64 配置定义 `IMGUI_DISABLE_SSE` |
| 构建自动化配置 | `.github/workflows/build-windows.yml` | 被配置为构建 Release Win32/x64/ARM64；不是运行验证证据 |
| 兼容实现 | `Inkeys/IdtStart.cpp::GetWindowsVersion`、`Inkeys/IdtMain.cpp::wWinMain` | 启动时读取系统版本；部分 DPI API 通过动态加载并带回退 |
| 图形实现 | `Inkeys/IdtD2DPreparation.cpp::D2DStarup` | WARP 设备请求 D3D feature level 11.1/11.0，并创建 D2D device |
| 独立项目配置 | `Timeout/InkeysTimeout.sln` | 仅有 Win32/x64 的 Debug/Release；没有 ARM64 配置 |

`【待确认】` README 的支持声明没有在仓库内附带逐功能测试矩阵。Windows 7 是否要求 Platform Update、ARM64 的真实最低系统、各架构的 D2D/输入/Office/WPS 组合，以及 CI 最近是否成功，都不能由静态工程配置替代。

`【待确认】` `Timeout` 未被 `InkeysRepo.sln`、主项目、README 或当前 Windows CI 引用；目前只能确认它是仓库内独立 Win32 计时器工程，不能确认仍属于主产品发布范围。

## 后续构建变更的审查要求

以下除第一项外是根据当前构建结构形成的 `【合理推断】` 审查清单，不是声称仓库已有完整发布政策：

1. `【直接确认；AGENTS.md】` Codex 使用完整 Solution、`Debug | ARM64`、ARM64 MSBuild host，超时至少 5 分钟。
2. 新增源码、module、资源或 manifest 时，核对 `Inkeys/Inkeys.vcxproj` 的 Item 类型和条件配置。
3. 涉及平台宏、SIMD、指针宽度或 Win32 API 时，分别检查 Win32、x64、ARM64，而不是从单一配置外推。
4. 兼容性结论必须记录实际运行过的 Windows、架构和 Office/WPS 组合；未运行的只写“配置存在”或“项目声明”。
5. 不提交 `Build/`、`Inkeys/Cache/`、`VcpkgInstalled/`、`PptCOM/obj/` 等生成输出。
