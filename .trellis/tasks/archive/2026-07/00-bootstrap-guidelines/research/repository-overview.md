# Inkeys 仓库调研总览（第二轮证据审计）

## 1. 范围、方法与证据等级

- 调研日期：2026-07-14。
- 调研对象：本地仓库中的 `README.md`、`AGENTS.md`、Solution、`.vcxproj`、`.csproj`、构建属性/目标、manifest、CI、第一方 C++/C#、资源清单与 Trellis 文档。
- 本轮只修改 Trellis 文档；未修改 C++、C#、工程文件、依赖或构建行为，未安装依赖，未编译、运行、测试或提交 Git。
- `Vcpkg/` 仅作为第三方 submodule 确认边界，没有递归分析源码；`Inkeys/additional/`、`Inkeys/HiEasyX/`、`Package/` 和 `Timeout/InkeysTimeout/json/` 只用于识别依赖/兼容边界，不将其内部风格提升为第一方规范。
- 排除 `.git`、`.vs`、`Build`、`Inkeys/Cache`、`VcpkgInstalled`、`PptCOM/obj`，以及 Debug、Release、x64、ARM64、buildtrees、packages、downloads、node_modules 等输出或缓存。

本文统一使用四种证据等级：

- `【直接确认】`：能从实际源码、Solution、项目文件或配置直接定位。
- `【合理推断】`：由多个实现事实推导出的修改建议；不是维护者已宣布的长期政策。
- `【待确认】`：需要构建、运行、发布材料或开发者意图才能确定。
- `【历史/兼容】`：仍存在的旧路径、备用实现、第三方随附代码或兼容分支；不代表未来推荐路线。

重要原则：代码当前这样写，不自动意味着以后必须继续这样写；静态异常或生命周期疑点也不自动构成已确认缺陷。

## 2. 项目用途与主要功能

`【直接确认】` `README.md` 将 Inkeys（“智绘教Inkeys”）描述为 Windows 上可在任意界面书写的 C++20 屏幕批注程序。第一方源码显示它由多窗口 UI、输入服务、墨迹合成、演示文稿联动、设置、更新和启动辅助共同组成，不是单一画布窗口。

| 功能 | 直接证据 |
| --- | --- |
| 桌面/全屏墨迹、普通笔、荧光笔、橡皮和形状 | `Inkeys/IdtDrawpad.cpp`、`IdtDraw.cpp`、`IdtState.cpp/.h` |
| 鼠标、压感笔、touch 和 inverted pen | `IdtRts.cpp/.h::CSyncEventHandlerRTS`、`DrawpadMsgCallback` |
| 清屏、撤销、恢复与历史快照 | `IdtHistoricalDrawpad.cpp/.h`、`IdtDrawpad.cpp` |
| 冻结帧和放大镜 | `IdtFreezeFrame.cpp/.h`、`IdtMagnification.cpp/.h` |
| 两套可选择悬浮栏 | `IdtMain.cpp::wWinMain`、`IdtFloating.cpp`、`Inkeys/Inkeys/UI/Bar/` |
| 设置窗口 | `Inkeys/Inkeys/UI/Setting/`、`Inkeys/src/setting/` |
| PowerPoint/WPS 状态、翻页、结束放映和窗口联动 | `PptCOM/PptCOM.cs`、`Inkeys/IdtPlug-in.cpp/.h` |
| 按幻灯片页保存/恢复墨迹 | `IdtDrawpad.cpp` 对 `PptImg` 的读写 |
| 国际化 | `IdtI18n.cpp/.h`、`Inkeys/src/i18n/*.jsonc`、`Scripts/i18n.ps1` |
| 更新发现/下载 | `Inkeys/Inkeys/Net/Net.Update*` |
| 崩溃、单实例、启动检查、UIAccess/置顶辅助 | `IdtMain.cpp`、`Helper.CrashHandler.*`、`Launch/`、`SuperTop/` |

`【直接确认】` `Timeout/InkeysTimeout/TimeoutMain.cpp` 还实现倒计时/正计时、暂停、重置、停止、声音与 i18n 的独立 Win32 工具。`【待确认】` 仓库主 Solution、README、主 CI 和主项目均未引用它，不能确认它仍随 Inkeys 主产品发布。

## 3. 仓库目录结构

~~~text
Inkeys/
├─ InkeysRepo.sln                  主 Solution
├─ Directory.Build.props/targets   vcpkg manifest 与构建目标集成
├─ vcpkg.json / vcpkg-configuration.json
├─ Vcpkg/                          第三方 submodule（本次不递归）
├─ VcpkgTriplets/                  x86/x64/ARM64 静态 triplet
├─ Inkeys/                         主 native 应用
│  ├─ Idt*.cpp/.h                  传统核心与全局状态子系统
│  ├─ Inkeys/                      C++20 module
│  │  ├─ Conv/  Helper/  Load/  Net/  Other/  Text/
│  │  └─ UI/
│  │     ├─ Bar/                   Inkeys3 悬浮栏
│  │     └─ Setting/               ImGui 设置窗口
│  ├─ Launch/  SuperTop/           启动、UIAccess/置顶辅助
│  ├─ src/                         i18n、PPT、设置、皮肤、字体、UI 资源
│  ├─ exe/  binarypackage/         随附/解包二进制（来源部分待确认）
│  ├─ additional/                  随附第三方代码
│  └─ HiEasyX/                     HiEasyX/EasyX 代码和库
├─ PptCOM/                         C# COM 桥接、manifest、项目文件
├─ Timeout/                        独立计时器 Solution
├─ Scripts/                        i18n 等维护脚本
├─ Package/                        Office/PowerPoint Interop 本地包
├─ ThirdpartyLicenses/  TOS/       许可与条款
├─ GithubRes/  ActionsRes/         对外构建说明和 CI/打包资源
├─ .github/                        GitHub Actions
└─ .trellis/  .agents/  .codex/    Trellis 与代理配置
~~~

`【直接确认】` 主 native 应用是“平铺 `Idt*` 文件 + 领域 C++20 module”的混合结构；一些 module 仍通过 global module fragment 包含 `IdtMain.h` 并访问传统全局状态。

`【合理推断】` 这表现出逐步拆分的趋势，但仓库没有“所有未来代码必须迁到 module”的政策；不能把目录年代描述成正式迁移路线。

`【直接确认】` `Timeout` 有自己的 `.sln/.vcxproj` 并内嵌 JsonCpp；它不是 `InkeysRepo.sln` 的子项目。`Build/`、`Inkeys/Cache/`、`VcpkgInstalled/`、`PptCOM/obj/` 是输出，不是架构层。

## 4. Solution 与项目职责

### 4.1 `InkeysRepo.sln`

| 项目/节点 | `【直接确认】`的职责 | 声明配置 |
| --- | --- | --- |
| `Inkeys` | `Inkeys/Inkeys.vcxproj`；Win32 GUI 主可执行程序 | Debug/Release × Win32/x64/ARM64 |
| `PptCOM` | `PptCOM/PptCOM.csproj`；PowerPoint/WPS COM bridge DLL/TLB | 主 Solution 各配置映射到 Release/Any CPU |
| `I18n` | Solution Folder；组织 JSONC、脚本和生成 header | 不生成独立二进制 |

`【直接确认】` Solution 的 `ProjectDependencies` 声明 `Inkeys` 依赖 `PptCOM`，使用的 GUID 与 `PptCOM.csproj::ProjectGuid` 一致。

### 4.2 `Inkeys/Inkeys.vcxproj`

`【直接确认】`：

- Windows subsystem、Unicode、`v143`、Windows SDK `10.0.26100.0`；
- C++20、`/utf-8`、module dependency scanning；
- Debug 为 `MultiThreadedDebug`、Release 为 `MultiThreaded`，即静态 CRT；
- `PerMonitorHighDPIAware`、UAC `AsInvoker`；
- 输出到 `Build/$(Platform)/$(Configuration)`，中间输出到 `Inkeys/Cache/...`；
- ARM64 定义 `IMGUI_DISABLE_SSE`；
- 登记 C++、`.cppm`、`.rc`、manifest、字体、图像、shader 和 PptCOM 产物；
- Dear ImGui 项目项编译 `imgui_impl_win32.cpp` 和 `imgui_impl_dx9.cpp`，没有编译随附的 `imgui_impl_dx11.cpp`。

`【待确认；风险观察】` vcxproj 尾部存在两个都指向 `..\PptCOM\PptCOM.csproj` 的 `ProjectReference`，分别记录 `{66F84848-5C28-4398-80D7-78F6189FC442}` 与 `{404F6E99-A0CA-45B2-8CAA-A2D819DA8EFF}`；实际 csproj/Solution GUID 是 `{A7B02228-179F-4B8E-BA8F-82D50066FB66}`。Solution 依赖正确；本轮未构建，不能判断这两项当前是否被忽略、合并或造成问题，也不能称为已确认构建缺陷。

### 4.3 `PptCOM/PptCOM.csproj`

`【直接确认】` .NET Framework 4.0、AnyCPU class library，引用 `Package/` 下 Office/PowerPoint Interop；`AfterBuild` 使用 SDK 4.0 `TlbExp.exe` 生成 `PptCOM.tlb`，并把 DLL/TLB 复制到 `Inkeys/`。它是 native 桌面应用的 COM 桥接层，不是 Web/backend 服务。

### 4.4 `Timeout/InkeysTimeout.vcxproj`

`【直接确认】` `Timeout/InkeysTimeout.sln` 只含 `InkeysTimeout`，声明 Debug/Release × Win32/x64，没有 ARM64；Release Win32 可见 C++17 与静态 CRT。`【待确认】` 是否发布、打包、维护及是否计划 ARM64，仓库内没有主产品引用证据。

## 5. 程序入口与启动编排

`【直接确认】` 主入口是 `Inkeys/IdtMain.cpp::wWinMain`。源码中可追溯的主要工作包括：

~~~text
参数/路径/单实例/崩溃与系统检查
  → 日志、COM、DPI/显示器、配置、i18n
  → 准备 PptCOM DLL/TLB/manifest 与 activation context
  → D2DStarup：D2D/DWrite + D3D11 WARP/DXGI/D2D device
  → 创建画板、冻结帧、设置、PPT 等窗口/状态
  → RealTimeStylus 初始化；失败走 mouse fallback
  → 根据 useInkeys3UI 启动 Bar 或 IdtFloating
  → 启动画板、状态、放大镜、PPT 等工作线程
  → 观察 offSignal/部分线程状态并执行进程级清理
~~~

关键证据：

- 路径、系统/架构和启动辅助：`IdtStart.cpp/.h`、`IdtMain.cpp`；
- DPI/显示器：`IdtDisplayManagement.cpp/.h`、`IdtMain.cpp`；
- 传统配置：`IdtConfiguration.cpp/.h`；新配置：`Inkeys/Inkeys/Other/Other.Config.*`；
- D2D 初始化：`IdtD2DPreparation.cpp/.h::D2DStarup`；
- input fallback：`IdtRts.cpp::SafeRTSInit`、`useMouseInput`；
- UI 分支：`IdtMain.cpp::useInkeys3UI` 与 `wWinMain`；
- PPT 加载：`IdtMain.cpp` 的 `CoInitializeEx`、activation-context 和 `LoadLibrary` 路径。

`【直接确认】` `IdtMain.h` 是传统汇聚头，包含大量 Windows/图形依赖与共享声明。`【合理推断】` 它是高影响依赖，新独立 module 不宜无理由增加包含；现有 module 已经包含它，因此这不是全仓已遵守的硬规则。

## 6. 核心模块地图

| 模块 | 主要文件 | 当前职责与状态 |
| --- | --- | --- |
| 启动/进程 | `IdtMain.cpp`、`IdtStart.cpp`、`Launch/`、`SuperTop/` | 入口、单实例、系统检查、重启/UIAccess、启动状态 |
| 配置 | `IdtConfiguration.*`、`Other.Config.*` | 两套持久化系统并存；正式迁移策略待确认 |
| i18n | `IdtI18n.*`、`IdtI18nKeys.g.h`、`src/i18n/`、`Scripts/i18n.ps1` | 运行加载、key 生成和语言同步 |
| 显示/窗口 | `IdtDisplayManagement.*`、`IdtWindow.*` | 多显示器、DPI、Win32 窗口 style/置顶维护 |
| 图形初始化 | `IdtD2DPreparation.*` | D2D factory、DWrite、D3D11 WARP、DXGI、D2D device |
| Inkeys3 Bar | `Inkeys/Inkeys/UI/Bar/` | `UI3=true` 的 D2D 悬浮栏；是否正式默认待确认 |
| 传统悬浮栏 | `IdtFloating.*` | `UI3=false` 的可执行 EasyX/GDI+ 分支；不能称为已淘汰 |
| 设置 | `Inkeys/Inkeys/UI/Setting/`、`src/setting/` | Win32 + Dear ImGui Win32/DX9 |
| 输入 | `IdtRts.*`、`Other.Inputs.cppm`、`Other.Gesture.cppm` | RTS packet、mouse fallback、键鼠/手势辅助 |
| 墨迹/画布 | `IdtDrawpad.*`、`IdtDraw.*`、`IdtImage.*`、`IdtDrawpadList.cpp` | 多点笔画、临时层、基础层、软件合成和 layered window |
| 状态/历史 | `IdtState.*`、`IdtHistoricalDrawpad.*` | 工具状态、撤销/恢复和历史图像 |
| 辅助视觉 | `IdtFreezeFrame.*`、`IdtMagnification.*` | 冻结帧与放大镜 |
| PPT native | `IdtPlug-in.*` | TLB 导入、服务槽/快照、状态/UI/交互线程、PPT 控件 |
| PPT managed | `PptCOM/PptCOM.cs` | Office/WPS 绑定、事件/轮询、HWND 和命令 |
| 更新 | `Inkeys/Inkeys/Net/Net.Update*` | 更新发现和下载 |
| 通用 module | `Helper/`、`Conv/`、`Text/`、`Load/` | 崩溃/线程、转换、文本和加载辅助 |
| Timeout | `Timeout/InkeysTimeout/` | 独立计时器；主产品发布范围待确认 |

`【合理推断】` `IdtConfiguration.cpp`、`IdtFloating.cpp`、`IdtPlug-in.cpp`、`Bar.Main.cpp` 和 `Setting.cpp` 都连接多个状态/生命周期；修改时应先缩小到目标函数和生产者/消费者，不能因文件大就做机械重整。

## 7. Win32、D3D11、D2D、ImGui、EasyX 与 GDI

### 7.1 多窗口 Win32 模型

`【直接确认】` 画板、冻结帧、PPT 控件、悬浮栏、设置及辅助工具分别拥有 Win32/HiEasyX 窗口、线程或消息循环；多处使用 layered、transparent、no-activate/topmost。`IdtWindow.cpp` 还持续维护可见性、style 与置顶状态。

`【合理推断】` 修改窗口不能只看创建点，还需检查消息循环、销毁者、触摸注册、TopWindow/状态维护、多显示器与 DPI 坐标。

### 7.2 后端按窗口分流

| 链路 | `【直接确认】`的当前角色 | 证据 |
| --- | --- | --- |
| D3D11 WARP → DXGI → D2D device | 为共享 D2D 初始化提供软件设备；`d2dDevice_WARP` 由 Bar 创建 device context | `IdtD2DPreparation.cpp::D2DStarup`、`Bar.Main.cpp::BarUISetClass::Rendering` |
| D2D/DWrite factory | Bar 使用；PPT 控件也用它创建 D2D DC target/文字资源 | `IdtD2DPreparation.cpp`、`IdtPlug-in.cpp::PptUI` |
| D2D device context + GDI interop | `Inkeys.UI.Bar` 的 BGRA premultiplied target、脏区与 layered-window 提交 | `Bar.Main.cpp` |
| ImGui Win32 + Direct3D 9 | 当前设置窗口；device lost/reset 与显式 Release | `Setting.Base.cppm`、`Setting.cpp`、`Inkeys.vcxproj` |
| EasyX/HiEasyX + GDI/GDI+ | 主画板和 `IdtFloating`，以及若干传统工具 | `IdtDrawpad.cpp`、`IdtFloating.cpp`、`IdtImage.cpp` |
| D2D DC target + GDI surface | PPT 控件特定窗口 | `IdtPlug-in.cpp::PptUI` |

`【直接确认】` `D2DStarup()` 在新旧悬浮栏分支之前无条件调用；这不仅服务 Bar，因为 PPT 控件还共享 D2D/DWrite factory。

`【历史/兼容；非当前设置路径】` `Inkeys/additional/imgui/` 随附 `imgui_impl_dx11.cpp/.h`，但 `Inkeys.vcxproj` 不编译它，第一方代码也没有 `ImGui_ImplDX11_*` 调用。正确结论是“存在多个 backend 源码，但产品设置窗口当前接入 DX9”，而不是“仓库没有 DX11”或“设置窗口使用 DX11”。

`【待确认；风险观察】` `IdtD2DPreparation.cpp` 定义 `D2DShutdown()`，全仓静态搜索未找到调用。本轮没有运行退出流程，不能称为泄漏或已确认清理缺陷。

### 7.3 两套悬浮栏的真实关系

`【直接确认】` `IdtConfiguration.h::SetListStruct` 把 `Experimental.Inkeys3.UI3` 默认设为 false；`IdtMain.cpp` 读入 `useInkeys3UI`，true 时 detached 启动 `Inkeys::UI::Bar::Initialization`，false 时启动 `floating_main`。该开关还影响画板、设置、PPT 与 zoom 的部分行为。

`【待确认】` 运行时旧配置 `opt/deploy.json` 可以覆盖源码默认；仓库静态默认不能证明发布包默认。维护者需确认当前正式 UI、回退和淘汰计划。

## 8. 输入处理与墨迹渲染

### 8.1 RTS 与 mouse fallback

`【直接确认】` `IdtRts.cpp/.h::CSyncEventHandlerRTS` 实现 `IStylusSyncPlugin`，读取 X/Y、normal pressure、contact width/height、cursor 类型，并区分 touch、pen、mouse 与 inverted pen。不同 RTS 路径包含关闭 flicks 或启用多点触摸。

packet 被写入每个接触点的模式数据、`TouchPos`、`TouchList`、`TouchTemp` 等共享结构；`TouchMode` 不是一个独立的全局输入模式变量。`SafeRTSInit` 用 SEH 防护初始化，失败设置 `useMouseInput`；`DrawpadMsgCallback` 把 Win32 mouse 消息接入相同接触链。

### 8.2 笔画、画布与历史

`【直接确认】` `IdtDrawpad.cpp::MultiFingerDrawing` 处理活动接触点的临时 `StrokeImageClass/IMAGE`：普通笔/荧光笔使用 GDI+ 曲线，橡皮直接改基础 `drawpad`，inverted pen/部分右键走橡皮语义。`IdtDrawpad.h` 注释 `endMode=1` 合并到画布，`endMode=2` 不合并。

`DrawpadDrawing` 合成基础画布和 `StrokeImageList` 活动层到 `window_background`，再更新分层窗口；完成态还与 `RecallImage` 历史及 PPT `PptImg` 页级图像交互。

### 8.3 并发观察

`【直接确认】` `MultiFingerDrawing` 当前使用 detached thread；`TouchList`、`TouchTemp`、`StrokeImageList`、画布、历史和 PPT 页图由多个线程/函数访问，代码并存 mutex/shared_mutex/IdtAtomic 与显式锁。

`【待确认；风险观察】` 静态扫描不能证明快速退出一定安全，也不能证明已经存在数据竞争。未来 Codex 修改接触点、临时图像、清屏/撤销/换页或退出时，必须重新追踪全部读写和锁，不能把“detached”本身写成已确认缺陷。

## 9. 新旧配置、i18n 与资源

### 9.1 两套配置的实际消费者

| 系统 | 磁盘文件/入口 | `【直接确认】`的消费者 |
| --- | --- | --- |
| 传统 `SetListStruct` | `IdtConfiguration.cpp::ReadSetting/WriteSetting`；`opt/deploy.json` | `IdtMain`、Draw/Drawpad、Floating、FreezeFrame、History、Magnification、Plug-in、RTS、Window、Update、Bar.Main、Setting 等 |
| `Inkeys.Other.Config` | `Inkeys::Config` class、`Inkeys::config/configOnce`；`Config::ReadAll/ReadMini/Write`；`Inkeys/Config/main.json` | `IdtConfiguration`、`IdtMain`、`IdtPlug-in`、`Net.Update*`、`Bar.Zoom*`、`Setting*` 等 |

`【直接确认】` `INKEYS_CONFIG_SCHEMA` 包含 `Config.AutoClean`、`Info`、`UI.Bar.Zoom`、`Experimental.Inkeys3.UI3.Animation`、`PlugIn.PPTHelper` 等。UI3 总开关仍是旧 `setlist.Experimental.Inkeys3.UI3`；新 schema 的 `...Animation` 不是同一开关。

两套系统写不同 JSON，没有发现通用双向同步。`Config.AutoClean` 会影响传统 `WriteSetting` 并清理旧 `setlistVal`，这是已确认的局部交点，不代表完整迁移。

`【待确认】` “所有新配置是否必须进入新 module”、旧配置迁移/兼容期限与发布默认文件均无明确政策。Codex 不应自行迁移字段。

### 9.2 i18n

`【直接确认】` `Scripts/i18n.ps1` 以 `Inkeys/src/i18n/zh-CN.jsonc` 为基准，`sync` 更新其他语言、`Scripts/i18n.zh-CN.snapshot.jsonc` 和 `Inkeys/IdtI18nKeys.g.h`；生成 header 标注不要手改。`check` 只检查，`sync` 会写文件。

### 9.3 资源与二进制

`【直接确认】` `Inkeys/src/ppt|quick|setting|skin|ttf|UI`、`Inkeys.rc`、`resource.h` 和 `Inkeys.vcxproj` 登记产品资源；加载方式包括 Win32 resource、磁盘/解包、GDI+/D2D bitmap、ImGui DX9 texture 和 SVG/lunasvg。

`Inkeys/exe/` 当前可见 `DesktopDrawpadBlocker.exe`，`Inkeys/binarypackage/` 可见 EasyX/HiEasyX 相关库。其正式生成、更新、签名与发布流程为 `【待确认】`。

## 10. PowerPoint/WPS 联动与真实数据流

### 10.1 构建与加载

`【直接确认】` `PptCOM.cs` 的 `IPptCOMServer` 为 COM-visible IUnknown 接口。`PptCOM.csproj` 生成 DLL/TLB；`IdtMain.cpp` 初始化 COM、准备 manifest/activation context 并加载 DLL；`IdtPlug-in.cpp` 通过 `#import "PptCOM.tlb"` 创建 `IPptCOMServerPtr`。

### 10.2 managed → native → 画板 → UI

~~~text
CheckPptCom
  → Initialization(&PptInfoState.TotalPage, &CurrentPage, &offSignal)
  → PptCOM.PptComService 绑定 PowerPoint/WPS并写 native int
  → IdtDrawpad 检测页码变化
  → 保存旧页/恢复新页 PptImg.Image[page]
  → 画布加载完成后更新 PptInfoStateBuffer
  → PptDraw/PPT UI 显示缓冲页码；PptInteract 发出 COM 命令
~~~

`【直接确认】` `PptImgStruct` 的 `map<int, IMAGE>` 是每页 native 墨迹，不是 Office 幻灯片截图。`PptInfoStateBuffer` 也不是 managed 直接写入；`IdtPlug-in.cpp` 注释明确它应在 `DrawpadDrawing` 加载 PPT 画布后同步。

### 10.3 unsafe 指针与同步边界

`【直接确认】` native 把 `PptInfoState.TotalPage/CurrentPage/offSignal` 地址 reinterpret 为 `long*` 传入 C# `int*` 并长期保存。`pptComSlotSm` 与 `Get/Set/ResetPptComSnapshot` 保护 COM 服务指针槽；未发现同一锁保护三个普通 int，也未发现它们为 atomic。

`【待确认；风险观察】` managed/native 线程间的内存可见性与退出解引用契约需要维护者说明。不能据此直接断言数据竞争缺陷，也不能误称服务指针 mutex 已覆盖页码同步。

### 10.4 PowerPoint/WPS 与版本标识

`【直接确认】` `PptCOM.cs` 有 PowerPoint/WPS ROT/COM/进程、事件/轮询、HWND 和释放分支，busy HRESULT 识别及约 10 秒重试。

`【直接确认】` `CheckCOM()` 返回常量 `20260627a`；native `CheckPptCom` 调用/保存它，但未找到比较或拒绝不匹配版本的逻辑。它目前是组件标识，不是已实施的版本安全门。

`【历史/兼容】` PowerPoint 2007、WPS 2013+ 等代码/文案只表示兼容意图。`【待确认】` 实际 Office/WPS 版本、位数、安装类型和 Windows/架构测试矩阵。

## 11. 构建方式、依赖和 Windows 兼容

### 11.1 Codex 的构建规则

`【直接确认；AGENTS.md】` 使用完整 `InkeysRepo.sln`、`Debug | ARM64`、ARM64 host `MSBuild.exe`，超时至少 5 分钟，不单独构建 `Inkeys.vcxproj`。本轮按用户要求没有构建。

`【历史/兼容】` `GithubRes/CompilationProcess_zh-CN.md` 另述预编译 PptCOM 产物并取消依赖的贡献者路径；普通开发者正式首选流程为 `【待确认】`，Codex 仍以 `AGENTS.md` 为准。

### 11.2 vcpkg 与随附依赖

`【直接确认】` `Directory.Build.props/targets` 启用 manifest 和自定义静态 triplet，并让安装目标先于 module scan。`vcpkg.json` 直接固定 openssl、cpp-httplib、jsoncpp、libcuckoo、lunasvg、magic-enum、concurrentqueue、unordered-dense、spdlog。无需 `vcpkg integrate install`。

`Inkeys/additional/`、`HiEasyX/`、`Package/` 等还随附 ImGui、HiEasyX/EasyX、stb、WinToast、zip/hash、Office Interop 等；第三方代码风格不是第一方规则。

### 11.3 “声明 / 配置 / 已验证”三层

| 层级 | 证据 | 可得结论 |
| --- | --- | --- |
| 项目声明 | `README.md` | 声明最低 Windows 7 RTM/SP0，列出 32 位、64 位、ARM64 |
| Solution 配置 | `InkeysRepo.sln` | 有 Debug/Release × Win32/x64/ARM64 |
| vcxproj 配置 | `Inkeys.vcxproj` | 仅 Release Win32/x64 显式 `MinimumRequiredVersion=6.01`；Release ARM64 为 `6.02`；三个 Debug 未显式写 |
| vcxproj 平台兼容项 | `Inkeys.vcxproj` | ARM64 定义 `IMGUI_DISABLE_SSE` |
| CI 配置 | `.github/workflows/build-windows.yml` | 被配置为构建 Release Win32/x64/ARM64；本轮未审查运行结果 |
| 运行兼容实现 | `IdtStart.cpp::GetWindowsVersion`、`IdtMain.cpp` | 读取系统版本，部分 DPI API 动态加载/回退 |
| Timeout 配置 | `Timeout/InkeysTimeout.sln` | 只有 Win32/x64，没有 ARM64 |

`【待确认】` Windows 7 Platform Update 需求、各架构真实最低系统、ARM64 运行要求、D2D/RTS/Office/WPS 组合，以及 CI 最近是否成功。工程配置存在不等于已验证支持。

## 12. 命名、目录、错误处理、日志与资源管理

### 12.1 代码世代与命名

`【直接确认】` 传统区以 `Idt*`、PascalCase、全局状态和 `Class/Struct` 后缀为常见形式；module 区多用 `Inkeys.领域.功能` 与 `Inkeys` 命名空间，但仍可能依赖旧全局。`Bar.Buttom.cpp`、`ActivateSildeShowWindow`、`D2DStarup` 等历史拼写仍在调用/ABI 中。

`【历史/兼容】` 这些拼写和混合风格不是新代码模板。`【直接确认；AGENTS.md】` 只做必要改动、关键步骤写简短中文注释、保持原编码和 EOL。

### 12.2 错误处理

`【直接确认】` 当前按子系统并存：

- D2D/D3D/COM 的 HRESULT/FAILED；
- RTS 的 HRESULT + SEH + mouse fallback；
- native PPT 的 `_com_error`；
- managed PPT 的 `COMException`/busy HRESULT；
- 文件/配置的返回值、`error_code`、局部 catch 和默认回退；
- 致命启动路径的日志、MessageBox、退出或重启。

`【合理推断】` 新代码在目标边界保留操作和错误码上下文；不要把某一子系统的异常模型强行推广到全仓。历史空 catch/只返回 false 不是推荐范例。

### 12.3 日志

`【直接确认】` `IdtMain.cpp::wWinMain` 配置 spdlog async file logger，文件位于 `log/`、名称为 `idt` 加时间戳、level 为 info。启动清理会删除时间差达到 7 天或为负的匹配日志，并在目录超过 10 MiB 时继续清理。

这证明存在当前保留/容量行为；它是否是正式产品政策、隐私/导出/上传要求仍为 `【待确认】`。

### 12.4 资源管理

`【直接确认】`：D3D11/D2D/DWrite/DXGI 使用 `ComPtr`；设置 D3D9 使用 raw pointer + `Release`；PptCOM C# 使用 `ReleaseComObject/FinalReleaseComObject`；画布使用 `IMAGE` 指针/容器/回收；Win32 路径显式销毁窗口、DC、handle、module、activation context；线程同时存在 detach/offSignal/status 与局部 jthread/stop_token/StatusGuard。

`【合理推断】` 局部改动延续目标资源的现有所有者；替换所有权或线程模型是生命周期变更，需要单独验证。

## 13. 静态风险与验证现状

以下均是待确认风险，不是已确认缺陷：

- `D2DShutdown` 有定义但未找到调用；可能有意依赖进程退出，也可能需要显式契约。
- 墨迹、Bar、PPT 等存在 detached thread；部分有 `offSignal`/状态等待，快速退出安全性未运行验证。
- `Inkeys.vcxproj` 两个重复且 GUID 不匹配的 PptCOM `ProjectReference`；实际 MSBuild 解析未验证。
- managed 写 native 普通 int 的 unsafe 指针共享未发现显式同步；实际契约待说明。
- `CheckCOM` 没有 native 比较；它是否应成为版本门待决定。

`【直接确认】` 本次未在两个 Solution 和第一方路径中发现自动化测试项目；`.github/workflows/build-windows.yml` 提供构建配置。不能据此断言项目没有外部测试，也不能把 workflow 文件本身当作成功证据。

## 14. 仍需开发者回答的问题及对未来 Codex 的影响

| 待确认问题 | 当前证据 | 对未来 Codex 开发的影响 |
| --- | --- | --- |
| 发布包默认使用 `IdtFloating` 还是 `Inkeys.UI.Bar`？旧栏何时可停止维护？ | 源码默认 UI3=false，但 `deploy.json` 可覆盖 | 决定 UI 功能要改一条还是两条路径、回归范围以及能否删除旧代码 |
| 新字段应进入 `SetListStruct` 还是 `Inkeys.Other.Config`？有无迁移计划？ | 两套文件和消费者同时活跃，无通用迁移器 | 决定持久化位置、兼容旧配置、设置 UI 写回及默认值策略 |
| 重复且 GUID 不匹配的两个 `ProjectReference` 是否有意？ | Solution 依赖正确，vcxproj 两项异常；未构建 | 决定以后能否清理工程项，避免 Codex误诊或破坏特定 VS/MSBuild 行为 |
| 普通开发者/发布构建的正式入口是什么？ | `AGENTS.md` 与历史贡献者说明用途不同 | 决定文档、CI 复现和非代理环境的验证命令；Codex 当前仍遵守 AGENTS |
| Windows 7、Win32、x64、ARM64 的真实最低系统和已验证矩阵？ | README 声明、工程配置、CI 配置三者层级不同 | 决定可用 Win32/D2D API、回退代码、平台宏与最低系统承诺 |
| `D2DShutdown` 是否应在退出调用？ | 有定义，无静态调用点 | 决定图形资源生命周期修改是复用进程退出还是补显式 teardown；未答前不能“修复” |
| detached thread 的正式退出保证是什么？ | 多处 detach，部分有 offSignal/status 等待 | 决定捕获对象、全局资源释放、快速退出和并发重构的安全边界 |
| 实际支持哪些 PowerPoint/WPS 版本、位数和安装方式？ | 有多套兼容分支，未运行 Office/WPS | 决定能否调整/删除兼容分支，以及 COM/HWND 改动的测试矩阵 |
| managed/native 页码 int 的同步契约是什么？ | 服务槽有 mutex；共享 int 未见 atomic/同锁 | 决定跨线程可见性修正是否必要，以及 ABI 变更不能只改一侧 |
| `CheckCOM` 是信息标识还是必须强制匹配的版本门？ | 返回常量，native 调用但不比较 | 决定接口演进、旧 DLL/TLB 错配检测和失败 UX |
| `Timeout` 是否仍属于主产品发布，是否需要 ARM64？ | 独立 Solution；主解决方案/README/CI 无引用 | 决定未来 Codex 是否应同步维护它、把它纳入兼容 Spec 和发布验证 |
| 正式发布冒烟清单和性能门槛在哪里？ | 未发现测试项目，只有 build workflow 配置 | 决定 UI、墨迹、PPT、多显示器改动的完成标准，避免只凭编译通过 |
| 日志的正式保留、隐私、导出/上传政策是什么？ | 代码有 7 天/10 MiB 清理，未见产品政策 | 决定新增诊断字段、敏感内容边界和故障支持流程 |
| `exe/`、`binarypackage/`、预编译 PptCOM/EasyX 产物如何生成和更新？ | 文件存在，只有 PptCOM 构建步骤较明确 | 决定 Codex 能否更新二进制、如何审查架构/签名/许可与可重建性 |

## 15. Spec 映射、空层处理与人工复核顺序

本轮有效项目层为：

- `.trellis/spec/native-desktop/`：目录/工程、构建兼容、C++ 边界、图形/UI、输入墨迹、错误/日志/资源、配置/i18n/assets；
- `.trellis/spec/ppt-interop/`：COM ABI、加载/生命周期、Office/WPS、页码和 `PptImg` 数据流；
- `.trellis/spec/guides/`：Trellis 通用思考指南，不冒充 Inkeys 源码规范。

`【开发者确认；2026-07-15 最终收尾】` backend/frontend 只有初始化模板的“不适用”说明，不构成 Inkeys 有效 Spec。Bootstrap PRD、task.json 和 context manifests 已改为引用 native-desktop、ppt-interop、guides 与本 research；根索引不再链接空层，两个空目录已删除。`python ./.trellis/scripts/get_context.py --mode packages` 已验证只暴露 `native-desktop` 与 `ppt-interop` 两个项目 Code-Spec 层；`guides` 继续作为共享思考指南入口。

建议开发者优先人工检查：

1. `ppt-interop/com-contract.md`：确认 unsafe 指针同步、CheckCOM 和 Office/WPS 支持范围；
2. `native-desktop/rendering-and-ui.md` 与 `input-and-ink.md`：确认两套悬浮栏、后端分流、detached worker 和画布生命周期；
3. `native-desktop/build-and-compatibility.md`：确认正式构建/系统矩阵及重复 ProjectReference；
4. `native-desktop/configuration-i18n-and-assets.md`：确定新旧配置归属与迁移政策；
5. `native-desktop/errors-logging-and-resources.md`：确认 D2D teardown、退出等待和日志产品政策。
