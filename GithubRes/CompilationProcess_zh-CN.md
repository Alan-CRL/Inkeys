# 编译流程

对于一般的构建需求来说，你只需要构建 `智绘教.vcxproj` 即可，而该项目有一个附属项目 `PptCOM.csproj` 是 智绘教Inkeys 的 PPT 联动模块。  
`智绘教.vcxproj` 依赖于 `PptCOM.csproj` 生成的类库（dll/tlb），但 `PptCOM.csproj` 已经被编译好了，可以直接构建 `智绘教.vcxproj`。这意味着只需要 C++ 桌面环境，而不用准备 C# 环境。

### 编译主项目 `智绘教.vcxproj`
智绘教Inkeys 采用完全开源方式，所有源码和资源全部开源

#### 准备环境
- Visual Studio 2022 (MSVC v143 编译器)
> 勾选 `使用 C++ 的桌面开发` `Windows 应用程序开发` 工作负荷
- Windows 11 SDK 10.26100

#### 代码环境
- Unicode
- C++20

#### 编译步骤
1. 拉取 `main` 仓库
2. 使用 Visual Studio 2022 打开 `智绘教.sln`
3. 选择 `智绘教` 项目
4. 切换为 `Release | Win32` 构建配置（按需调整生成架构，如 `x64`）
5. 点击 `生成->生成解决方案` 或 `生成->Build 智绘教` 即可

---

### 编译附属项目 `PptCOM.csproj`

#### 准备环境
- Visual Studio 2022
> 勾选 `.NET 桌面开发` 工作负荷
- .NET Framework 4.0 SDK 或更高版本

#### 编译步骤
1. 拉取 `main` 仓库
2. 使用 Visual Studio 2022 打开 `智绘教.sln`
3. 选择 `PptCOM` 项目
4. 切换为 `Release | AnyCPU`
5. 点击 `生成->Build PptCOM` 即可