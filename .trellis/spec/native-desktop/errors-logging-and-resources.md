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

Draw3 输入/橡皮控制台诊断仅在显式开关启用时发布：显示/EDID 信息走显示快照发布边界，输入记录复用约250ms帧级门及同一选中 contact 的坐标、身份与尺寸。它是稀疏快照，不在 RTS packet 回调中同步写盘或控制台。

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
- 共享 UI3 调度器：发布退出标志时同步调用 `Inkeys::UI::RenderPipeline::WakeForStop()`。

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

正常 Close/Restart 意图（Bar、设置页直接按钮、窗口服务）进入本合同；UEF 的 `offSignalInterop=3` 使用独立报告/确认/重启仲裁，只共享监督器原语，不把报告阶段当作正常 `SetOffSignal(1/2)`。带确认框的动作只有用户 OK 后才算已确认；取消不能启动监督器。

### 2. Signatures

- `SetOffSignal(int signal)`：`1=Close`、`2=Restart`；只接受首次 `offSignalInterop` CAS。
- `CloseProgram()`、`RestartProgram()`：统一发布退场意图，再以 `Window::Service::RequestHideAllUserWindows()` 请求 owner thread 隐藏。
- `Shutdown::ArmShutdownSupervisor(Intent, deadlineMilliseconds=15000)`：外部精确旧进程 HANDLE 与进程内 Win32 截止兜底；`Window::Service::BeginShutdown()` 是显示请求的单调门。
- 设置页 `QueueClose()`/`QueueRestart()`：直接调用全局入口；确认式 `ConfirmRestart` 仍在用户 OK 后执行。

### 3. Contracts

- 首次 `SetOffSignal` 先 CAS 决定 Close/Restart，再关 Window Service 的新显示门，并在任何业务清理、磁盘 I/O、jthread join 或 COM 等待前调用 `ArmShutdownSupervisor`。仅 `Armed/FallbackArmed` 证明独立 15 秒截止已建立；外部 helper 与进程内 `CreateThread` 均失败时返回 `Failed` 并记录错误，不能保证强退。随后发布 `offSignal`、排入隐藏命令并唤醒 UI3 scheduler。迟到显示/恢复请求不得重新显出画布；隐藏 owner 不参与已建立监督器的运行。
- 隐藏与清理尽力按原顺序完成，正常退出仍排空已接受保存请求；`Armed/FallbackArmed` 的截止到达时按用户决定无条件结束旧进程。已 durable 的 UInk/索引保留最后有效点，未 durable 请求可丢失，不能把强退写作保存成功。
- 重启 helper 只针对握手验证的当前可信 EXE 和旧进程 HANDLE；旧进程真正 signaled 后才尝试拉起一次。外部 helper 未建立而 `FallbackArmed` 时，本进程截止兜底仍会结束旧进程但不保证新进程启动；双重建立失败时没有 15 秒保证，应保留 `Failed` 日志与发布风险。重复退场请求不能多发 helper；确认框取消不占意图槽。
- 设置页直接关闭/重启按钮先执行本地 `Setting::Hide()` 的短状态锁与隐藏命令入队，再在同一渲染回调直接调用全局退场入口；不能排在配置写盘、ShellExecute 或其它业务 FIFO 后才尝试监督。`Hide()` 本身若停住，尚未进入 `SetOffSignal`，不是已受 15 秒保护的阶段。先前排队的写入继续由正常退出排空，已建立监督且超过 15 秒时按上述 durability 边界处理。

### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| owner thread / Draw3 / 设置业务 worker 卡住 | 首次正式请求在同步业务等待前尝试 Arm；成功时独立 15 秒退场，不能等隐藏回执后才 Arm |
| helper 创建或握手失败，但自身线程已建立 | `FallbackArmed` 截止强退旧进程；Restart 不能假报新进程成功 |
| helper 与自身截止线程均创建失败 | `Failed` 记录错误，无可证明的 15 秒保证，仍尝试正常退出；列发布风险 |
| 正常提前结束 | 监督器只观察旧进程退出，不误拉起 Close；Restart 最多一个新实例 |
| 旧进程死亡延迟超过 5 秒 | Restart helper 继续等精确旧 HANDLE，不并行新旧实例 |
| 用户取消确认 / 重复点击 | 取消零退场；首次 CAS 决定意图，后续请求不改写 |
| 持久化 I/O 卡住超过 15 秒且监督已建立 | 强制结束，旧 durable 恢复点仍有效，待保存请求明确未验证/可能丢失 |

### 5. Good / Base / Bad Cases

- Good：设置页直接 Exit 的业务 worker 已被旧写盘卡住，`Hide()` 正常返回且监督建立后旧 PID 至迟约 15 秒退场，双 Drawpad 不在退场期重新显示。
- Base：正常保存/线程关闭较快，完整排空后自然退出；确认式 Restart 取消后继续运行。
- Bad：先把 Close/Restart 投递到同一可能被 I/O 卡住的 FIFO，或先同步等 Window owner 隐藏，再启动 15 秒倒计时；用 `TerminateProcess` 代替未处理异常捕获测试。

### 6. Tests Required

- 独立进程故障注入：正常 Close、Restart、helper 创建/握手失败、旧死亡延迟、重复请求、真实 `RaiseException` 的自动/手动报告、报告卡住与磁盘满，分别核旧 PID、dump/report、唯一新 PID、15 秒边界。`TerminateProcess` 只测监督，不冒充 UEF。
- 自建隔离 GUI/config 测设置页直接关闭/重启在旧 worker 卡住时的点击到退场；双画布 HWND/capture、设置窗口和单实例交接分别验收。无可靠 GUI 命中或 Win7 设备时记需要人工，不以无窗口套件代替。
- 最终完整 `InkeysRepo.sln Debug|ARM64` 与 Release 可得架构、相关 Headless/PptCOM；本机编译不推导 Win7 SP1+仅 KB2670838 运行通过。

### 7. Wrong vs Correct

~~~cpp
// Wrong：已确认的关闭按钮仍排在可能永久阻塞的设置业务 I/O 后面。
void QueueClose() { QueueBusiness({ SettingBusinessKind::Close }); }

// Correct：不等业务 worker 即尝试建立监督；既有命令在正常清理中排空。
void QueueClose() { ::CloseProgram(); }
~~~

## D2D/GDI present 借用资源事务合同

### 1. Scope / Trigger

当 Bar、Whiteboard 或其他 RenderPipeline 客户端从 renderer/scene 取得 raw `ID2D1DeviceContext*`、`ID2D1GdiInteropRenderTarget*` 并跨越 `BeginDraw`、GDI interop 或 `EndDraw` 使用时适用。ARM64 上资源重建更容易暴露借用指针在事务中失效的问题。

### 2. Signatures

~~~cpp
BarSurfaceRenderResult BarSurfaceScene::Render(
    ID2D1DeviceContext* deviceContext, ...);
HRESULT ID2D1GdiInteropRenderTarget::GetDC(D2D1_DC_INITIALIZE_MODE, HDC*);
HRESULT ID2D1GdiInteropRenderTarget::ReleaseDC(const RECT* update);
HRESULT ID2D1DeviceContext::EndDraw();
~~~

### 3. Contracts

- Scene/renderer 返回的 raw COM 指针是借用引用，不转移所有权。调用方必须立即用本地 `Microsoft::WRL::ComPtr` 建立本帧 lease，并持有到对应 `EndDraw` 返回。
- `BeginDraw -> Render -> GetDC -> ULW -> ReleaseDC -> EndDraw` 是一个提交事务。不得在 `GetDC` 或 ULW 成功后提前释放 context/interop，也不得因中途失败跳过 `ReleaseDC`/`EndDraw` 结算。
- 只有所有阶段成功后才推进业务 damage、viewport 与 debug 快照。红框记录业务 dirty；绿框记录实际 present union，上一帧绿框只参与下一帧擦除，不得写回业务 dirty。
- 任一阶段失败时保留请求并强制下一帧全脏；target/device 分类继续沿用 RenderPipeline 的局部重建与 device epoch 恢复规则。

### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| renderer 在调用期间替换内部 COM owner | 本地 lease 保证旧对象存活到 `EndDraw` |
| `GetDC` 失败 | 不调用 ULW；仍结算 `EndDraw`，快照不推进 |
| ULW 或 `ReleaseDC` 失败 | 完成可执行的 release/end，标记整笔事务失败 |
| `EndDraw` 返回 recreate target | 丢弃该窗口资源并全脏重试，不发布成功快照 |
| debug 绿色 present 框扩大提交范围 | 只更新 presented-frame 快照，不污染业务 dirty |

### 5. Good / Base / Bad Cases

- Good：ARM64/x64 都以局部 `ComPtr` 持有 context 与 GDI interop，所有退出分支在 lease 析构前完成 `EndDraw`。
- Base：无 debug overlay 时 present damage 等于本帧业务 damage，成功后推进两类快照。
- Bad：只保存 Scene 返回的 raw pointer，或把 `previousDebugPresentedFrames` 写成业务 `drawResult.damage`；前者可能 use-after-release，后者无法可靠擦除上一帧绿框。

### 6. Tests Required

- Headless 对纯 damage transaction 断言失败不推进、成功才推进，以及红/绿框 union 的下一帧擦除范围。
- 完整 `Debug|ARM64` 与 `Debug|x64` Solution 构建及两架构 `--no-window` 测试。
- D2D Debug Layer 与重复进入/退出需要在允许 GUI 的独立阶段验证；静态 COM lease 审计不能替代运行时 layer 输出。

### 7. Wrong vs Correct

~~~cpp
// Wrong：raw pointer 的所有者可能在 EndDraw 前重建。
auto* context = renderer.DeviceContext();
context->BeginDraw();
RenderAndPresent(context);
context->EndDraw();

// Correct：本帧持有借用对象，事务结算后再释放 lease。
Microsoft::WRL::ComPtr<ID2D1DeviceContext> context = renderer.DeviceContext();
Microsoft::WRL::ComPtr<ID2D1GdiInteropRenderTarget> interop = renderer.GdiInterop();
context->BeginDraw();
RenderAndPresent(context.Get(), interop.Get());
const HRESULT endDrawResult = context->EndDraw();
~~~

## 待确认风险（不是已确认缺陷）

- `D2DShutdown`：`IdtD2DPreparation.cpp` 有声明/定义，但全仓静态搜索未找到调用。需确认是否有意依赖进程退出，影响未来 Codex 是否可以复用或调整 D2D 生命周期。
- detached workers：墨迹、Bar、PPT 等处可见 detached thread，且部分配有 `offSignal`/状态等待。需确认官方退出保证，影响涉及捕获对象、全局资源和快速退出的修改边界。
- 主退出等待：`IdtMain.cpp` 对选定线程状态有等待与超时逻辑；本轮没有运行验证，不能称为死锁或遗漏。
- 日志政策：代码已有 7 天/10 MiB 清理行为，但它是否是正式保留要求、是否需要隐私/导出/崩溃上传约束仍待维护者确认。

## 首发更新与崩溃处理边界（2026-09-27）

- 首次发布沿用用户确认的 HTTP/HTTPS 更新策略：先尝试 HTTPS，失败时允许同源 HTTP 回退，合法显式端口（1..65535）保留；HTTPS 链仍须验证证书与主机名。每一跳验证 scheme、host、路径与跳转次数，远端 `representation`、安装器相对路径、哈希格式和 JSON/ZIP 大小在文件操作前校验。HTTP 与同源哈希不提供发布者身份认证，现阶段作为用户明确接受的剩余风险记录，不把它误记为已修复的安全边界。ZIP 只能向本次创建的 staging 目标提取指定 EXE，不能将归档 entry 名直接交给落盘 API，也不能递归清理未知用户文件。
- 从同一远端 JSON 取得的 MD5/SHA-256 只证明下载结果与该 JSON 一致，不构成发布者身份认证。当前源侧签名/发布公钥和 CDN host allowlist 仍未定；实现运输与路径修补不自动升级为完整供应链验收。启动 update.json 的两阶段解析和旧程序替换还须验证磁盘满/权限错误下旧 EXE 保持可启动。
- `SetUnhandledExceptionFilter` 的返回值是**前一个**过滤器，可合法为 `nullptr`；是否已安装必须另记。`CloseProgram`/`RestartProgram` 正常清理前无条件恢复前一个值（包括空值）。崩溃路径只保证尽力写 dump/报告与尝试拉起，不在受损进程里同步走业务保存或无限等待；进程内第二次异常不得重复创建子进程，`-CrashTry` 的启动循环要有有限抑制窗口。
- Desktop/PPT 已提交 UInk 与索引的可读性、自动重启创建新进程、新进程完成初始化、画面恢复分别记录。当前跨进程可见墨迹恢复仍受功能 gate 约束，不因存在自动保存就宣称已支持。真实未处理异常与外部强杀分别验收，Win7 SP1+仅 KB2670838 的结果不得由 Win11 ARM64 编译推导。
