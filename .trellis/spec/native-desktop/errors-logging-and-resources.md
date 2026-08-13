# Errors, Logging, and Resources

本文区分 `【直接确认】`、`【合理推断】`、`【待确认】` 和 `【历史/兼容】`。仓库并存多套错误与所有权模型；现状不等于以后必须复制，也不能在没有运行证据时把静态风险写成缺陷。

## 错误处理边界

| 边界 | `【直接确认】`的当前实现 | 证据 |
| --- | --- | --- |
| D2D/D3D11 初始化 | 检查 `HRESULT`/`FAILED`，记录并 reset 已创建对象 | `IdtD2DPreparation.cpp::D2DStarup` |
| 设置 D3D11 | device/RTV/SRV 创建函数返回 bool；resize、occlusion 与 present 走显式分支 | `Setting.Base.cppm::CreateDeviceD3D/ResizeSwapChain/LoadTextureFromMemory`、`Setting.cpp` |
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
| 设置窗口 D3D11、DXGI、SRV | raw COM pointer + 显式 `Release`；SRV、RTV、swap chain、context、device 按依赖逆序清理 | `Setting.Base.cppm::CleanupSettingTextures/CleanupDeviceD3D` |
| ImGui context/backends | 显式 DX11/Win32 shutdown 与 `DestroyContext` | `Setting.cpp` 的窗口线程退出路径 |
| Office COM（C#） | 事件解绑、`ReleaseComObject`/`FinalReleaseComObject`、置 null | `PptCOM/PptCOM.cs::FullCleanup` 及 release helpers |
| native PPT 服务 | `_com_ptr_t` 包装、`pptComSlotSm` 保护服务槽/快照 | `IdtPlug-in.cpp::Get/Set/ResetPptComSnapshot` |
| `Graphics::DibSurface`/临时画布 | RAII 管理 HDC、DIB bitmap、旧选入对象和像素地址；容器按值拥有 | `Inkeys/Graphics/Surface.*`、`IdtDrawpad.cpp`、`IdtImage.cpp` |
| Win32 HWND/消息 channel | `Inkeys.Window` 所属线程创建、解绑并逆序销毁；外部只持有非 owning HWND | `Inkeys/Window/Window.*`、`IdtMain.cpp` |
| 线程 | 窗口、Setting 和低级 Hook 使用受管 `jthread/stop_token`；遗留业务线程也必须在 Window Service 前 join | `IdtMain.cpp`、`Inkeys/Window`、`Inkeys/Input` |

`【合理推断】` 在局部功能中沿用目标资源的现有所有者和释放点。raw pointer → smart pointer、detached thread → jthread 等会改变生命周期和退出顺序，应作为可验证的独立改动，而不是顺带“清理”。

## 已能追溯的清理路径

仓库没有一条可证明适用于所有子系统的统一“七步清理顺序”。可直接确认的是各自路径：

- `【直接确认】` 设置线程关闭 ImGui DX11/Win32 backend 与 context，再释放用户图片 SRV、RTV、swap chain、device context 和 device；hide/show 与 stop 路径都执行该顺序。证据为 `Setting.cpp`、`Setting.Base.cppm`。
- `【直接确认】` `PptCOM/PptCOM.cs::FullCleanup` 解绑事件并释放 slide-show window、presentation、application；WPS 分支有额外 GC/释放处理。
- `【直接确认】` `IdtMain.cpp` 的退出路径处理 COM apartment、activation context、加载模块和部分进程级 handle。
- `【直接确认】` 多个传统线程观察 `offSignal` 或线程状态；具体等待范围和超时逻辑位于 `IdtMain.cpp` 及各线程入口。

`【合理推断】` 修改某个子系统时，按其实际依赖逆序检查“停止生产者 → 等待仍使用资源的线程 → 释放目标/设备/COM/窗口”。这只是审计方法，不能替代对具体退出代码的追踪。

## 受管线程退出合同

### 1. Scope / Trigger

当线程由主退出路径执行 `join()`，或线程可能无限等待事件、消息、状态值时，必须应用本合同。

### 2. Signatures

- 进程级退出入口：`SetOffSignal(int)`。
- C++20 线程入口：接收并观察 `std::stop_token`，或观察全局 `offSignal`。
- 共享 UI3 调度器：发布退出标志时同步调用 `RenderScheduler::Scheduler::WakeForStop()`。

### 3. Contracts

- 被 `join()` 的线程中，每一层可能持续等待的循环都必须观察同一个退出条件；只在最外层检查不成立。
- `SetOffSignal()` 必须先发布 managed/native 共用退出槽和 C++ `offSignal`，再唤醒所有无限等待对象。
- 窗口服务销毁前，所有仍可能访问 HWND 或 UI3 target 的线程必须已经返回。

### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| 状态值在退出时长期保持不变 | 内层等待在一个轮询周期内观察退出并返回 |
| UI3 调度器处于 `WaitForSingleObject(..., INFINITE)` | `SetOffSignal()` 同步设置 wake event，渲染线程重查退出条件 |
| 退出入口被重复调用 | 标志发布和唤醒保持幂等，不创建第二个调度线程 |

### 5. Good / Base / Bad Cases

- Good：`while (!offSignal && RequestUpdateMagWindow == 0)`，退出不依赖状态先变化。
- Base：`stop_token` 的 stop callback 设置等待事件，线程醒来后重查 `stop_requested()`。
- Bad：外层 `while (!offSignal)` 内嵌 `while (state)`，随后主线程对其执行无限 `join()`。

### 6. Tests Required

- Headless 覆盖退出通知能唤醒 idle 的 UI3 调度器，并在有请求/无请求两种状态下有界返回。
- 手工验证默认放大镜状态、穿透状态和 UI3 完全 idle 状态下退出，进程不得停留超过一个轮询周期。

### 7. Wrong vs Correct

~~~cpp
// Wrong：状态不变化时，退出标志永远没有机会被重新检查。
while (RequestUpdateMagWindow == 0)
    std::this_thread::sleep_for(100ms);

// Correct：所有可能长期等待的层级都观察退出条件。
while (!offSignal && RequestUpdateMagWindow == 0)
    std::this_thread::sleep_for(100ms);
~~~

## 关闭前隐藏用户窗口合同

### 1. Scope / Trigger

关闭或重启由 UI 命令触发时，必须在后台清理前同步移除全部用户可见窗口。

### 2. Signatures

- 窗口服务：`bool Inkeys::Window::Service::HideAllUserWindows()`。
- 进程入口：`CloseProgram()`、`RestartProgram()`。

### 3. Contracts

- Window Service 分别在 Overlay 与 Setting owner thread 批量执行 `SW_HIDE`，不销毁 HWND；`DisplayObserver` 不属于用户界面。
- 关闭/重启入口先调用批量隐藏，再执行 CrashHandler 清理和 `SetOffSignal()`。
- 隐藏失败不得阻断退出信号发布。

### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| 窗口不存在或已经隐藏 | 视为可继续，其他窗口仍被隐藏 |
| 任一 owner thread 隐藏失败 | 关闭/重启仍继续清理并发布退出信号 |
| 调用来自任一窗口 owner thread | 该组直接执行，另一组同步投递，不发生自锁 |

### 5. Good / Base / Bad Cases

- Good：用户点击关闭后所有界面立即消失，后台线程随后有序退出。
- Base：部分窗口尚未创建或本来不可见，调用仍可完成。
- Bad：先执行耗时清理或等待线程，再隐藏窗口。

### 6. Tests Required

- Window Service 测试先显示全部用户窗口，调用批量隐藏后断言 HWND 仍有效且均不可见。
- 完整构建验证关闭入口能够导入 Window 模块，不形成模块依赖环。

### 7. Wrong vs Correct

~~~cpp
// Wrong：清理耗时会让界面看起来卡住。
CrashHandler::Shutdown();
SetOffSignal(1);

// Correct：视觉退出先完成，隐藏结果不改变退出控制流。
(void)Inkeys::Window::GetService().HideAllUserWindows();
CrashHandler::Shutdown();
SetOffSignal(1);
~~~

## 待确认风险（不是已确认缺陷）

- `D2DShutdown`：`IdtD2DPreparation.cpp` 有声明/定义，但全仓静态搜索未找到调用。需确认是否有意依赖进程退出，影响未来 Codex 是否可以复用或调整 D2D 生命周期。
- detached workers：墨迹、Bar、PPT 等处可见 detached thread，且部分配有 `offSignal`/状态等待。需确认官方退出保证，影响涉及捕获对象、全局资源和快速退出的修改边界。
- 主退出等待：`IdtMain.cpp` 对选定线程状态有等待与超时逻辑；本轮没有运行验证，不能称为死锁或遗漏。
- 日志政策：代码已有 7 天/10 MiB 清理行为，但它是否是正式保留要求、是否需要隐私/导出/崩溃上传约束仍待维护者确认。
