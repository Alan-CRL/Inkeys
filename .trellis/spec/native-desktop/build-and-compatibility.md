# Build and Compatibility

本文沿用根 Spec 的证据分级：`【直接确认】`、`【合理推断】`、`【待确认】`、`【历史/兼容】`。工程中存在某项配置，只能证明该构建入口被声明，不能证明产物已在对应系统或设备上运行验证。

## 主构建入口

`【直接确认】` 仓库根 `AGENTS.md` 对自动化代理明确要求：

- 构建 `InkeysRepo.sln`，不要单独构建 `Inkeys/Inkeys.vcxproj`；
- 使用 `Debug | 当前设备原生架构`；
- 使用当前设备原生架构对应的 host `MSBuild.exe`（例如 x64 设备用 x64 host，ARM64 设备用 ARM64 host）；
- 构建超时至少 5 分钟；
- 这是本机整体开发验证入口；本次发布流水线任务已明确授权独立构建。`PptCOM` 与 native 工程不再有项目依赖或自动复制，接口变更时必须另行同步同次生成的 DLL/TLB 后再验证 native。

等价参数形式为：

~~~text
MSBuild.exe InkeysRepo.sln /p:Configuration=Debug /p:Platform=<NativePlatform>
~~~

`MSBuild.exe` 的绝对路径取决于本机 Visual Studio 安装，不在 Spec 中硬编码。Release 或非当前原生架构（Win32/x64/ARM64 中未覆盖者）可以作为任务或风险要求下的额外平台证据，但不能替代当前 AGENTS 要求的 `Debug | 当前设备原生架构` 完整 Solution 验证。构建是否通过必须引用具体任务验证记录，不能由本规范的入口说明推出。

`【直接确认】` 发布 CI 独立构建 `PptCOM/PptCOM.csproj` 和 `Inkeys/Inkeys.vcxproj`，以本次 artifact 交接 DLL/TLB。本机日常代理验证仍遵守上述 Solution 入口；独立 native 构建的 `SolutionDir` 默认回到仓库根，产物仍在 `Build/`。

## Solution 与工程配置事实

| 证据 | `【直接确认】`的内容 | 不能据此推出 |
| --- | --- | --- |
| `InkeysRepo.sln` | 声明 `Debug/Release × Win32/x64/ARM64`；包含 `Inkeys`、`PptCOM` 和 `I18n` Solution Folder | 六种组合都已成功构建或运行 |
| `InkeysRepo.sln` / `Inkeys/Inkeys.vcxproj` | Inkeys 对 PptCOM 的 solution 依赖及两处 ProjectReference 已移除；测试工程保留，但三个 Release 配置均不默认构建测试 | native 构建会自动获得最新 DLL/TLB |
| `Inkeys/Inkeys.vcxproj` | Windows/Unicode 可执行程序，v143、SDK `10.0.26100.0`、C++20、`/utf-8`、module dependency scan、静态 CRT | 这些设置构成完整运行兼容矩阵 |
| `Inkeys/Inkeys.vcxproj` | 输出目录是 `Build/$(Platform)/$(Configuration)`；中间目录是 `Inkeys/Cache/...`；ARM64 定义 `IMGUI_DISABLE_SSE` | ARM64 已覆盖所有第三方库和 Office 组合 |
| `InkeysRepo.sln` 的项目配置映射 | 主 Solution 的各项配置把 `PptCOM` 映射到 `Release | Any CPU` | AnyCPU 与任意 Office 位数组合都已验证 |
| `.github/workflows/build-windows.yml` | PptCOM 单次 AnyCPU 构建后，Windows 2022 分别构建 native Release Win32/x64/ARM64 | 当前 CI 已实际完成构建或运行验证 |

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

`【直接确认】` `Vcpkg` 是第三方依赖子模块。本项目代码任务不得递归修改它；依赖变更应同时审查 manifest、triplet、许可和随附第三方产物。

## PptCOM 构建链

`【直接确认】` `PptCOM/PptCOM.csproj` 是 .NET Framework 4.0、AnyCPU class library，引用 `Package/` 下的 Office/PowerPoint Interop。其 `ExportTlb` 构建后目标调用 SDK 4.0 的 `TlbExp.exe`，仅在自身输出目录生成 TLB，不复制 DLL/TLB。`Inkeys/PptCOM.dll` 与 TLB 均继续 Git 跟踪，维护者在 PptCOM 源码变化后手动同步；CI 必须下载同次构建的两者并覆盖现有资源与 `#import` 位置。

## Scenario: Harness 发布边界与独立构建

### 1. Scope / Trigger

修改 native Harness 编译条件、Windows 发布 job或 artifact 交接时应用本合同。Win32 的 `Bar.Presentation.Test.cpp` 曾在 32 位宿主工具链报 C1002；独立 headless 测试成功不能代替主项目验证。

### 2. Signatures

~~~text
MSBuild PptCOM/PptCOM.csproj /p:Configuration=Release /p:Platform=AnyCPU
MSBuild Inkeys/Inkeys.vcxproj /p:Configuration=Release /p:Platform=Win32 /p:PreferredToolArchitecture=x64 /p:EnableUi3PresentationHarness=false
# Debug 默认 true；Release 默认 false；显式 true 保留测试入口。
~~~

`pptcom` job 输出 DLL 与同次 TLB 的 artifact ID，供 native 构建按 ID 下载。

### 3. Contracts

- `EnableUi3PresentationHarness` 使用统一 0/1 编译宏。关闭后移除 runner、授权测试实现及固定测试轨迹，保留无 fixture 时的产品输入/渲染连接；内部 Harness 参数必须在窗口初始化前非零退出，共用诊断与关闭监督不受影响。
- CI 显式 x64 宿主与目标架构分离；工程默认保留 ARM64 本机原生宿主，其余 64 位宿主选择相应工具。不得把 PreferredToolArchitecture 当作目标 Platform。
- `check → pptcom → build matrix → package → upload`。PptCOM 仅初始化 MSBuild、NuGet、.NET 4.0 引用程序集；native 仅初始化子模块/vcpkg，不做 .NET 安装或 solution NuGet restore。
- DLL 与同次 TLB 独立传递。下载以本次 job 的 artifact ID 为准，不按同名跨运行搜索。
- 每个 EXE 的 IDR_DLL1 字节 SHA-256 必须等于选定 DLL；检查不能执行 EXE。

### 4. Validation & Error Matrix

| 条件 | 处理 |
| --- | --- |
| 初始化、构建、artifact 上传/下载失败 | job 失败 |
| DLL/TLB 下载内容或 EXE 内嵌 DLL 不一致 | 阻止后续处理 |
| Release 收到内部 Harness 参数 | 初始化 GUI 前非零退出 |

### 5. Good / Base / Bad Cases

- Good：三架构都嵌入同一个选定 DLL。
- Base：本地随附 DLL/TLB 继续跟踪并由维护者手动同步。
- Bad：按名称消费旧 artifact，或仅禁用独立测试工程以处理主项目 C1002。

### 6. Tests Required

静态检查 YAML/PowerShell/工程 XML、job 依赖、Release 项目映射、工程属性 off/on 与实际宿主路径；独立 PptCOM DLL/TLB 生成且不覆盖本地资源；Win32 Release Harness off/on 构建；ARM64 Debug Solution 与 headless `--no-window`。产物模拟覆盖缺失输入、rerun 精确产物选择和清理。真实三架构 artifact 留待 Actions 验收。

### 7. Wrong vs Correct

~~~text
Wrong: msbuild InkeysRepo.sln Release，再按名称下载任意同名 DLL。
Correct: 独立构建 PptCOM → 选择本次 DLL/TLB ID → native 下载 ID 并构建 → Package 收集本次产物。
~~~

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

1. `【直接确认；AGENTS.md】` Codex 使用完整 Solution、`Debug | 当前设备原生架构`、对应架构 MSBuild host，超时至少 5 分钟。
2. 新增源码、module、资源或 manifest 时，核对 `Inkeys/Inkeys.vcxproj` 的 Item 类型和条件配置。
3. 涉及平台宏、SIMD、指针宽度或 Win32 API 时，分别检查 Win32、x64、ARM64，而不是从单一配置外推。
4. 兼容性结论必须记录实际运行过的 Windows、架构和 Office/WPS 组合；未运行的只写“配置存在”或“项目声明”。
5. 不提交 `Build/`、`Inkeys/Cache/`、`VcpkgInstalled/`、`PptCOM/obj/` 等生成输出。

## Scenario: 首发完整压缩包许可文件

### 1. Scope / Trigger

修改 `.github/workflows/build-windows.yml` 的 Package 完整发布 ZIP 时核此合同；自动更新 ZIP 的内容合同单独维持。

### 2. Signatures

CI `Prepare license notices for release packages` 将仓库根 `LICENSE`、`NOTICE`、`ThirdpartyLicenses/` 放入 `signedUpload/{Inkeys,Inkeys64,InkeysArm64}/`，然后现有三条 `Compress-Archive` 生成完整发布 ZIP。

### 3. Contracts

每个完整发布 ZIP 同一顶层架构目录含 `Inkeys.exe`、`Tips.txt`、`LICENSE`、`NOTICE`、`ThirdpartyLicenses/`。`NOTICE` 指向的本项目与第三方许可文件应随包可读。`InkeysUpdate*.zip` 仍由较早步骤仅封装待替换 EXE，不改变既有更新解析、命名或包内路径。

### 4. Validation & Error Matrix

| 条件 | 处理 |
| --- | --- |
| 源 LICENSE/NOTICE/ThirdpartyLicenses 缺失或复制失败 | `Copy-Item -ErrorAction Stop` 使 CI Package 失败，不静默发缺许可完整包 |
| 完整 ZIP 漏任一许可文件 | 阻止发布并核实际 ZIP entry；不能只凭仓库里有 NOTICE 判通过 |
| 更新 ZIP 仍仅有 EXE | 保持原更新合同，不把完整包误送给 updater |

### 5. Good / Base / Bad Cases

- Good：Win32/x64/ARM64 三个完整 ZIP 都含上述许可目录，更新 ZIP 仍为 EXE。
- Base：仓库根许可文件未改，仅调整打包随附；不修改产品运行或依赖版本。
- Bad：只在源码仓库保留 NOTICE、发布 ZIP 仅含 EXE/Tips 却标注许可已随附。

### 6. Tests Required

对三架构完整 ZIP 逐项枚举 entry；本地同 `Copy-Item`/`Compress-Archive` 命令的隔离 dry-run 可检查打包语义，真正 CI 产物仍需发布前复核。静态 YAML diff、命令退出码与实际 ZIP 内容分开记。

### 7. Wrong vs Correct

~~~text
Wrong: InkeysArm64/Inkeys.exe + InkeysArm64/Tips.txt
Correct: 上述文件 + InkeysArm64/LICENSE + NOTICE + ThirdpartyLicenses/*
~~~
