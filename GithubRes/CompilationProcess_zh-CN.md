# 编译流程

对于一般的构建需求来说，你只需要构建 `智绘教.vcxproj` 即可，而该项目有一个附属项目 `PptCOM.csproj` 是 智绘教Inkeys 的 PPT 联动模块。  
`智绘教.vcxproj` 依赖于 `PptCOM.csproj` 生成的类库（dll/tlb），但 `PptCOM.csproj` 已经被编译好了，可以直接构建 `智绘教.vcxproj`。这意味着只需要 C++ 桌面环境，而不用准备 C# 环境。
**注意：如果只需要编译 `智绘教.vcxproj` 你需要在 `解决方案配置->项目依赖项->智绘教` 中取勾 `PptCOM`！**

### 编译主项目 `智绘教.vcxproj`
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

4. 使用 Visual Studio 2022/2026 打开 `智绘教.sln`

> 在 Visual Studio 2026 中编译 Inkeys，务必保持 MSVC v143 编译配置集，而不是升级到 MSVC v145。

5. 选择 `智绘教` 项目
6. 切换为 `Release | Win32` 构建配置（按需调整生成架构，如 `x64` 和 `Arm64`）
7. 点击 `生成->Build 智绘教` 即可

> 首次切到某个平台构建时，vcpkg 会自动下载/编译依赖，第一次可能较久；之后会复用缓存。

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
3. 点击 `生成->Build PptCOM` 即可