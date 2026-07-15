# Errors, Logging, and Resources

本文区分 `【直接确认】`、`【合理推断】`、`【待确认】` 和 `【历史/兼容】`。仓库并存多套错误与所有权模型；现状不等于以后必须复制，也不能在没有运行证据时把静态风险写成缺陷。

## 错误处理边界

| 边界 | `【直接确认】`的当前实现 | 证据 |
| --- | --- | --- |
| D2D/D3D11 初始化 | 检查 `HRESULT`/`FAILED`，记录并 reset 已创建对象 | `IdtD2DPreparation.cpp::D2DStarup` |
| 设置 D3D9 | 创建函数返回 bool；Present/device-lost/reset 走显式分支 | `Setting.Base.cppm::CreateDeviceD3D/ResetDevice`、`Setting.cpp` |
| RealTimeStylus | HRESULT 加 `SafeRTSInit` 的 SEH 防护，失败切换 mouse fallback | `IdtRts.cpp::SafeRTSInit`、`IdtMain.cpp` |
| PPT native | 调用处可见 `_com_error`，服务通过快照取得并判空 | `IdtPlug-in.cpp::GetPptComSnapshot` 及 PPT 命令包装 |
| PptCOM managed | `COMException`/HRESULT 分类，识别 Office busy 并限时重试 | `PptCOM/PptCOM.cs::IsBusyComException`、`HandleBusyException` |
| 文件/配置/更新 | 返回值、`error_code`、局部 catch 与默认回退并存 | `IdtConfiguration.cpp`、`Other.Config.cpp`、`Net.Update*.cpp` |
| 不可继续的启动路径 | 日志、MessageBox、退出或重启按场景并存 | `IdtMain.cpp::wWinMain`、`IdtStart.cpp` |

`【历史/兼容】` 空 catch、只返回 false 或混合异常模型在部分旧代码中存在，不是新代码的推荐范例。

`【合理推断】` 局部修改应在目标子系统已有边界转换错误，并保留能定位操作与错误码的上下文；更换整个异常模型属于独立架构任务。是否需要用户提示、回退或终止，必须按实际调用方决定，不能套用一条全局规则。

## 日志与清理策略

`【直接确认】` `Inkeys/IdtMain.cpp::wWinMain` 初始化 spdlog 异步文件 logger：

- 日志目录是 `log/`，名称使用 `idt` 加时间戳；
- logger 使用异步 thread pool，level 为 info，pattern 包含 level 和时间；
- 启动清理会处理匹配日志：时间差达到 7 天或出现负时间差时删除；目录总大小超过 10 MiB 时还会在遍历中继续删除旧条目。

因此，先前“仓库没有日志保留/轮转行为”的说法不准确。这里能确认的是当前启动清理算法，不代表它已形成稳定的产品政策，也不等同于 spdlog 的滚动文件 sink。

`【直接确认】` 现有日志常使用中文“线程/函数”上下文。`【合理推断】` 新日志宜包含子系统、操作对象和 HRESULT/Win32 error，并避免在每帧/每 packet 无节制写入；是否需要统一字段、隐私脱敏、用户导出或崩溃上传规则仍为 `【待确认】`，不能在无项目依据时写成已批准规范。

## 资源所有权映射

| 资源 | `【直接确认】`的当前所有权 | 证据/边界 |
| --- | --- | --- |
| D3D11、D2D、DWrite、DXGI | `Microsoft::WRL::ComPtr` | `IdtD2DPreparation.cpp`、`Bar.Main.cpp` |
| 设置窗口 D3D9 | raw COM pointer + 显式 `Release` | `Setting.Base.cppm::CleanupDeviceD3D` |
| ImGui context/backends | 显式 Win32/DX9 shutdown 与 `DestroyContext` | `Setting.cpp` 的窗口线程退出路径 |
| Office COM（C#） | 事件解绑、`ReleaseComObject`/`FinalReleaseComObject`、置 null | `PptCOM/PptCOM.cs::FullCleanup` 及 release helpers |
| native PPT 服务 | `_com_ptr_t` 包装、`pptComSlotSm` 保护服务槽/快照 | `IdtPlug-in.cpp::Get/Set/ResetPptComSnapshot` |
| HiEasyX `IMAGE`/临时画布 | 指针、容器、显式 delete 或回收队列 | `IdtDrawpad.cpp`、`IdtImage.cpp` |
| Win32 handles/modules | 按路径调用 `DestroyWindow`、`ReleaseDC`、`CloseHandle`、`FreeLibrary`、activation-context API | `IdtMain.cpp` 与各窗口实现 |
| 线程 | `thread/detach/offSignal`、线程状态，或局部 `jthread/stop_token/StatusGuard` | 传统 Idt 与部分 module 并存 |

`【合理推断】` 在局部功能中沿用目标资源的现有所有者和释放点。raw pointer → smart pointer、detached thread → jthread 等会改变生命周期和退出顺序，应作为可验证的独立改动，而不是顺带“清理”。

## 已能追溯的清理路径

仓库没有一条可证明适用于所有子系统的统一“七步清理顺序”。可直接确认的是各自路径：

- `【直接确认】` 设置线程关闭 ImGui DX9/Win32 backend、context，再调用 `CleanupDeviceD3D`；证据为 `Setting.cpp`、`Setting.Base.cppm`。
- `【直接确认】` `PptCOM/PptCOM.cs::FullCleanup` 解绑事件并释放 slide-show window、presentation、application；WPS 分支有额外 GC/释放处理。
- `【直接确认】` `IdtMain.cpp` 的退出路径处理 COM apartment、activation context、加载模块和部分进程级 handle。
- `【直接确认】` 多个传统线程观察 `offSignal` 或线程状态；具体等待范围和超时逻辑位于 `IdtMain.cpp` 及各线程入口。

`【合理推断】` 修改某个子系统时，按其实际依赖逆序检查“停止生产者 → 等待仍使用资源的线程 → 释放目标/设备/COM/窗口”。这只是审计方法，不能替代对具体退出代码的追踪。

## 待确认风险（不是已确认缺陷）

- `D2DShutdown`：`IdtD2DPreparation.cpp` 有声明/定义，但全仓静态搜索未找到调用。需确认是否有意依赖进程退出，影响未来 Codex 是否可以复用或调整 D2D 生命周期。
- detached workers：墨迹、Bar、PPT 等处可见 detached thread，且部分配有 `offSignal`/状态等待。需确认官方退出保证，影响涉及捕获对象、全局资源和快速退出的修改边界。
- 主退出等待：`IdtMain.cpp` 对选定线程状态有等待与超时逻辑；本轮没有运行验证，不能称为死锁或遗漏。
- 日志政策：代码已有 7 天/10 MiB 清理行为，但它是否是正式保留要求、是否需要隐私/导出/崩溃上传约束仍待维护者确认。
