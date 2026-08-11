# Directory Structure

本文只把项目/文件归属作为**直接确认**内容；“新增文件放哪里”若没有 AGENTS.md 或重复模式支撑，会明确标为**合理推断**。

## Solution 边界

| 路径 | 角色 | 依据 |
| --- | --- | --- |
| InkeysRepo.sln | 主 Solution，包含 Inkeys、PptCOM 和仅用于组织文件的 I18n Solution Folder | InkeysRepo.sln |
| Inkeys/Inkeys.vcxproj | 主 Windows 桌面可执行程序；包含 C++、module、资源、manifest、字体、图像和 shader 项 | Inkeys/Inkeys.vcxproj |
| PptCOM/PptCOM.csproj | 生成 PptCOM.dll 和类型库的 .NET Framework COM 桥接库 | PptCOM/PptCOM.csproj |
| Timeout/InkeysTimeout.sln | 独立 Solution，不属于 InkeysRepo.sln | Timeout/InkeysTimeout.sln 的 Project 节点；InkeysRepo.sln 无该项目 |
| Timeout/InkeysTimeout/InkeysTimeout.vcxproj | 独立 Win32 计时器应用；是否随主产品发布待确认 | TimeoutMain.cpp 的 WinMain、对应 vcxproj |

**【直接确认】** Inkeys.vcxproj 显式列出 ClCompile、ClInclude、ResourceCompile、Manifest、Font 等项目项。新增或移动源码时应同步检查项目文件；这是对当前显式项目清单的保守维护要求。

## 仓库一级目录

| 目录 | 当前职责 |
| --- | --- |
| Inkeys/ | 主程序源码、C++20 module、资源、内置二进制和随附第三方代码 |
| PptCOM/ | PowerPoint/WPS 托管 COM 服务、manifest、程序集属性 |
| Timeout/ | 独立计时器源码及其内嵌 JsonCpp；主产品发布归属待确认 |
| Scripts/ | i18n 检查、同步、快照等仓库脚本 |
| Vcpkg/ | microsoft/vcpkg Git submodule；视为第三方，不递归分析或修改 |
| VcpkgTriplets/ | x86、x64、ARM64 的项目自定义静态 triplet |
| VcpkgInstalled/ | manifest 安装输出，不作为源码阅读或规范样本 |
| Package/ | PptCOM 使用的本地 NuGet/Interop 包 |
| ThirdpartyLicenses/、TOS/ | 第三方许可和项目条款 |
| GithubRes/ | 对外说明和构建指南，包含 CompilationProcess_*.md |
| .github/ | CI、Issue/PR 等 GitHub 配置 |
| ActionsRes/、Build/ | CI/打包资源与构建输出；Build 不属于源码 |
| .trellis/、.agents/、.codex/ | AI 工作流、Spec 和平台配置 |

.git、.vs、Inkeys/Cache、Build，以及 Debug、Release、x64、ARM64、buildtrees、packages、downloads、node_modules 等输出或缓存目录不作为代码架构证据。

## Inkeys 主程序内部

| 路径 | 当前职责与放置规则 |
| --- | --- |
| Inkeys/Idt*.cpp、Inkeys/Idt*.h | 传统主程序子系统。修改现有 Idt 功能时优先留在其现有文件对中，不为风格统一做无关迁移 |
| Inkeys/IdtMain.cpp、IdtMain.h | wWinMain、启动编排、共享声明和大量传统依赖的中心 |
| Inkeys/Inkeys/ | C++20 module 根目录，按 Conv、Helper、Load、Net、Other、Text、UI 分区 |
| Inkeys/Inkeys/UI/Bar/ | Inkeys3 悬浮栏的状态、主题、布局、动画、渲染属性和入口；是否为发布默认待确认 |
| Inkeys/Inkeys/UI/Setting/ | Dear ImGui 设置界面、DX11 后端和自定义控件 |
| Inkeys/Launch/ | 启动状态相关代码 |
| Inkeys/SuperTop/ | UIAccess/置顶辅助相关代码和 token 声明 |
| Inkeys/src/i18n/ | zh-CN、zh-TW、en-US JSONC 文案 |
| Inkeys/src/ppt/ | PPT 控件相关图像资源 |
| Inkeys/src/quick/、setting/、skin/、ttf/、UI/ | 快捷操作、设置、皮肤、字体和 UI 资源 |
| Inkeys/exe/、binarypackage/ | 已存在 DesktopDrawpadBlocker.exe 和分架构 EasyX 库；生成来源、更新方式与打包链待确认 |
| Inkeys/additional/ | Dear ImGui 后端、stb、WinToast、zip、hash 等随附第三方代码 |
| Inkeys/HiEasyX/ | HiEasyX/EasyX 封装与相关库，视为外部/随附实现 |

## C++20 module 的已观察模式

当前 module 名与目录相呼应，例如：

- Inkeys.Other.Config：Inkeys/Inkeys/Other/Other.Config.cppm 和 .cpp；
- Inkeys.Net.Update：Inkeys/Inkeys/Net/Net.Update.cppm 和实现文件；
- Inkeys.UI.Bar 及分区：Inkeys/Inkeys/UI/Bar/Bar*.cppm、Bar*.cpp；
- Inkeys.UI.Setting：Inkeys/Inkeys/UI/Setting/Setting*.cppm、Setting*.cpp。

**【合理推断】** 若任务在既有 module 体系内新增相邻模块，Inkeys.领域.功能 与对应领域目录是最保守的默认形式。它来自上述重复模式，不是维护者已确认的全仓迁移规则；跨领域或替代 Idt* 的设计需另行确认。新增文件仍需按 Inkeys.vcxproj 的实际 module 项写法登记。

## 第三方边界

以下目录是**直接确认的第三方/随附边界**，不应自动作为第一方命名、错误处理或资源管理的默认范例：

- Vcpkg 及 VcpkgInstalled；
- Inkeys/additional；
- Inkeys/HiEasyX；
- Timeout/InkeysTimeout/json；
- Package 中的 Office/Interop 包；
- Inkeys 中随附的旧 DirectX/EasyX 二进制或头文件。

**【合理推断】** 只有任务明确涉及依赖升级、补丁或许可时才修改这些区域，并把第三方改动与产品代码改动分开说明；Vcpkg 不递归修改还受到本 Bootstrap 用户约束。
