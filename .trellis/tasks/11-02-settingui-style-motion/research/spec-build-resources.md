# 构建与资源所有权规范摘录

补充约束以仓库AGENTS.md为准：本机ARM64原生MSBuild、完整InkeysRepo.sln Debug|ARM64、同一PowerShell invocation规范PATH、至少5分钟；无明确授权不打开GUI，不继承此前任务commit授权。

## .trellis/spec/native-desktop/build-and-compatibility.md:1-24

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


## .trellis/spec/native-desktop/errors-logging-and-resources.md:27-53

- 启动清理会处理匹配日志：时间差达到 7 天或出现负时间差时删除；目录总大小超过 10 MiB 时还会在遍历中继续删除旧条目。

因此，先前“仓库没有日志保留/轮转行为”的说法不准确。这里能确认的是当前启动清理算法，不代表它已形成稳定的产品政策，也不等同于 spdlog 的滚动文件 sink。

`【直接确认】` 现有日志常使用中文“线程/函数”上下文。`【合理推断】` 新日志宜包含子系统、操作对象和 HRESULT/Win32 error，并避免在每帧/每 packet 无节制写入；是否需要统一字段、隐私脱敏、用户导出或崩溃上传规则仍为 `【待确认】`，不能在无项目依据时写成已批准规范。

Draw3 输入/橡皮控制台诊断仅在显式开关启用时发布：显示/EDID 信息走显示快照发布边界，输入记录复用约250ms帧级门及同一选中 contact 的坐标、身份与尺寸。它是稀疏快照，不在 RTS packet 回调中同步写盘或控制台。

## 资源所有权映射

| 资源 | `【直接确认】`的当前所有权 | 证据/边界 |
| --- | --- | --- |
| D3D11、D2D、DWrite、DXGI | `Microsoft::WRL::ComPtr` | `IdtD2DPreparation.cpp`、`Bar.Main.cpp` |
| 设置窗口 D3D11、DXGI、SRV | raw COM pointer + 显式 `Release`；SRV、RTV、swap chain、context、device 按依赖逆序清理 | `Setting.Base.cppm::CleanupSettingTextures/CleanupDeviceD3D` |
| ImGui context/backends | 显式 DX11/Win32 shutdown 与 `DestroyContext` | `Setting.cpp` 的窗口线程退出路径 |
| Office COM（C#） | owner 按每次取得配平 `ReleaseComObject`；raw 事件连接显式解绑，不以 GC/FinalRelease 补漏 | `PptCOM/PptCOM.cs::FullCleanup` 及 release helpers |
| native PPT 服务 | `_com_ptr_t` 包装、`pptComSlotSm` 保护服务槽/快照 | `IdtPlug-in.cpp::Get/Set/ResetPptComSnapshot` |
| `Graphics::DibSurface`/临时画布 | RAII 管理 HDC、DIB bitmap、旧选入对象和像素地址；容器按值拥有 | `Inkeys/Graphics/Surface.*`、`IdtDrawpad.cpp`、`IdtImage.cpp` |
| Win32 HWND/消息 channel | `Inkeys.Window` 所属线程创建、解绑并逆序销毁；外部只持有非 owning HWND | `Inkeys/Window/Window.*`、`IdtMain.cpp` |
| 线程 | 窗口、Setting 和低级 Hook 使用受管 `jthread/stop_token`；遗留业务线程也必须在 Window Service 前 join | `IdtMain.cpp`、`Inkeys/Window`、`Inkeys/Input` |

`【合理推断】` 在局部功能中沿用目标资源的现有所有者和释放点。raw pointer → smart pointer、detached thread → jthread 等会改变生命周期和退出顺序，应作为可验证的独立改动，而不是顺带“清理”。

## 已能追溯的清理路径

仓库没有一条可证明适用于所有子系统的统一“七步清理顺序”。可直接确认的是各自路径：
