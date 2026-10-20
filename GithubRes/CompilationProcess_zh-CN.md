# 编译流程

对于一般的构建需求来说，你只需要构建 `Inkeys.vcxproj` 即可，而该项目有一个附属项目 `PptCOM.csproj` 是 智绘教Inkeys 的 PPT 联动模块。
仓库在 `Inkeys/` 中维护已编译的 `PptCOM.dll` 和 `PptCOM.tlb`，两个工程之间不再有项目依赖，可以直接构建 `Inkeys.vcxproj`。这意味着只需要 C++ 桌面环境，而不用准备 C# 环境。PptCOM 源码或接口变更后，由维护者按下文步骤手动同步同次生成的 DLL/TLB。

### 编译主项目 `Inkeys.vcxproj`
智绘教Inkeys 采用完全开源方式，所有源码和资源全部开源

#### 准备环境
- Visual Studio 2022/2026  
> 勾选 `使用 C++ 的桌面开发` `Windows 应用程序开发` 工作负荷  
- MSVC v143 - VS 2022 C++ 生成工具(v14.44 或更新版本)  
- Windows 11 SDK (10.0.26100)  

#### 代码环境
- Unicode
- C++20

#### 特别注意
**仓库目录应放置于纯英文路径下，且路径中不能包含空格。**  

#### 编译步骤
1. 拉取仓库

```cmd
git clone "https://github.com/Alan-CRL/Inkeys.git"
cd "Inkeys"
```

2. 初始化子模块

```cmd
git submodule update --init --recursive
```

3. Bootstrap vcpkg

```cmd
.\vcpkg\bootstrap-vcpkg.bat
```
**不需要** `vcpkg integrate install`  

4. 使用 Visual Studio 2022/2026 打开 `InkeysRepo.sln`

> 在 Visual Studio 2026 中编译 Inkeys，务必保持 MSVC v143 编译配置集，而不是升级到 MSVC v145。

5. 选择 `Inkeys` 项目
6. 切换为 `Release | Win32` 构建配置（按需调整生成架构，如 `x64` 和 `Arm64`）
7. 点击 `生成->Build Inkeys` 即可

> 首次切到某个平台构建时，vcpkg 会自动下载/编译依赖，第一次可能较久；之后会复用缓存。

命令行也可单独构建，输出仍位于仓库根 `Build/<Platform>/<Configuration>`：

```cmd
msbuild Inkeys\Inkeys.vcxproj /m /p:Configuration=Release /p:Platform=Win32 /p:PreferredToolArchitecture=x64
```

`PreferredToolArchitecture` 只选择宿主工具，不改变目标架构；ARM64 本机默认使用原生宿主。Release 默认关闭内嵌 UI3 Presentation Harness，Debug 默认开启；测试版本可显式传入 `/p:EnableUi3PresentationHarness=true`。两个独立测试项目保留在 solution，但三个 Release 配置不再默认构建它们。

---

### FAQ
某些特殊环境可能会导致无法编译。  
- **error LNK2001: 无法解析的外部符号 `std_search_1`**  
在 VS 2022 17.8.x（MSVC v143.35 ～ v143.41）中会导致无法编译。  

推荐使用 **17.14.x**，与项目开发者所安装的版本保持一致。（非必须）  

---

### 编译附属项目 `PptCOM.csproj`

#### 准备环境
- Visual Studio 2022/2026

> 勾选 `.NET 桌面开发` 工作负荷

- .NET Framework 4.0 SDK 或更高版本(4.x)

#### 编译步骤
1. 选择 `PptCOM` 项目
2. 切换为 `Release | AnyCPU`
3. 点击 `生成->Build PptCOM`，或运行 `msbuild PptCOM\PptCOM.csproj /p:Configuration=Release /p:Platform=AnyCPU`。仅构建 PptCOM 不需要子模块或 vcpkg。
4. DLL 与同次生成的 TLB 位于 `PptCOM/bin/Release/`。构建不再自动复制；修改 PptCOM 源码/接口后，由维护者手动同步这两个文件到 `Inkeys/`，再构建 native 项目。两个随附文件继续由 Git 跟踪，RC 和 `#import` 路径不变。
