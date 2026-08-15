# Platform and Resources

## Compatibility Target Versus Verified Behavior

Windows 7 SP1 + KB2670838 是 Inkeys 的正式项目级兼容目标。面向平台、设备、交换链、DWM、输入或窗口样式的变更必须考虑该目标，不能仅以 Windows 11 ARM64 开发机结果作为兼容结论。

当前测试程序包含若干兼容候选路径，包括 D3D feature level 重试、DirectComposition 运行时探测、DWM alpha 重试、presenter fallback 和 WARP fallback。

> **待验证**：这些路径尚未在本次 Bootstrap 中于 Windows 7 SP1 + KB2670838 环境执行。DirectComposition、DWM extended frame、UpdateLayeredWindow、premultiplied alpha、dirty rect 和 resize 的具体行为都不能写成已验证或保证能力。

兼容性报告必须区分：

- 项目级目标：Windows 7 SP1 + KB2670838。
- 当前代码路径：源码中存在的兼容处理。
- 实测能力：指定操作系统、补丁、GPU/驱动和 presenter 下实际通过的场景。

### 未来欢迎页透明度测试备忘

- 未来实现欢迎页时，Windows 7 环境或检测到 Qualcomm GPU 的设备必须额外显示一页透明度测试，让用户在真实桌面背景上确认窗口是否正确透明。
- 测试结果用于选择或建议兼容呈现路径，不能仅凭 OS、GPU 厂商或 API 初始化成功自动判定透明正确。
- 该页面尚未实现；实现时应另立任务，定义可回退选择、结果保存方式和无法判断时的处理。

## Scenario: Windows Compatibility Claim

### 1. Scope / Trigger

当任务修改设备初始化、feature level、窗口样式、交换链、DWM/DComp/ULW、dirty rect、resize 或对外兼容说明时，必须应用本契约。

### 2. Signatures

当前兼容链的主要入口：

```cpp
bool InitializeGraphicsDevice(GraphicsDeviceResources& resources);
bool TransparentPresentationController::Initialize(
    HWND window, GraphicsDeviceResources& graphics,
    InkRenderer& renderer, UINT width, UINT height);
bool TransparentPresentationController::Resize(UINT width, UINT height);
bool TransparentPresentationController::Present(RECT dirty, bool presentFull);
```

本规则不新增 API；修改上述签名或返回语义需要专门设计并检查全部调用者。

`WindowController` 直接使用原生 Win32 API 创建窗口，并拥有独立消息线程；工程不需要窗口源文件的 Release 优化例外。主绘图窗口当前以 `WS_EX_TOPMOST`（按 presenter 需要再含 `WS_EX_NOREDIRECTIONBITMAP`）创建，并以 `SW_SHOWNORMAL` 显示，允许独立测试宿主取得键盘焦点；接入 Inkeys 后再恢复 NOACTIVATE。

### 3. Contracts

- `Windows 7 SP1 + KB2670838` 是项目目标字段，不是测试结果字段。
- 实测记录必须包含：OS/补丁、GPU/驱动、feature level、active presenter、是否 WARP、场景与结果。
- 没有环境记录时，只能声明代码路径存在并标记“待验证”。
- 首选 DComp 时，`WS_EX_NOREDIRECTIONBITMAP` 必须随 `CreateWindowEx` 的 `dwExStyle` 传入；不能依赖创建后调用 `SetWindowLongPtr` 补设。
- 主绘图窗口矩形取主显示器 `rcMonitor`，宽度不变，高度为 `max(1, monitorHeight - 1)`；创建期保持 TOPMOST，当前测试宿主允许激活以接收方向键。
- 窗口线程使用 `_beginthreadex` 启动，通过手动复位事件发布创建结果；禁止 detached thread、普通全局预设或轮询非原子完成标志。
- `WindowController::window_` 是跨线程句柄，必须以 acquire/release 原子语义发布和清空；调用 Win32 API 前先读取到局部 `HWND`，避免关闭期间重复读取失效句柄。
- `WM_NCCREATE` 必须从 `CREATESTRUCTW::lpCreateParams` 取得控制器并写入 `GWLP_USERDATA`；其余未处理消息交给 `DefWindowProcW`。
- 析构时向仍有效的 HWND 投递 `WM_CLOSE` 并等待线程句柄；`WM_DESTROY` 必须 `PostQuitMessage`；消息泵无论因 `WM_QUIT` 还是 `GetMessageW` 错误结束，都要由窗口线程销毁仍有效的 HWND 后再清空原子句柄。

### 4. Validation & Error Matrix

| Condition | Required behavior/report |
|---|---|
| feature level 11_1 列表返回 `E_INVALIDARG` | 使用 11_0 列表重试并记录最终 feature level |
| hardware device 创建失败 | 尝试 WARP；两者都失败则初始化失败 |
| DirectComposition API/初始化不可用 | 进入下一 presenter，不宣称 DComp 可用 |
| DComp 窗口在创建后缺少 `WS_EX_NOREDIRECTIONBITMAP` | 输出包含实际 `GWL_EXSTYLE` 的低频诊断并进入下一 presenter；不要尝试创建后补设 |
| 主显示器有效高度大于 1 | 主窗口从顶部覆盖到倒数第二个物理像素，最底部保留 1px |
| 显示窗口或鼠标按下 | 保持 TOPMOST；当前独立测试宿主可正常取得前台激活与键盘焦点 |
| 窗口类注册、线程或 `CreateWindowExW` 失败 | 输出对应 Win32/CRT 上下文，发布失败结果并回收线程与事件句柄 |
| `WM_CLOSE` 投递失败 | 向已记录的窗口线程投递 `WM_QUIT`，析构仍等待线程结束 |
| `GetMessageW` 返回 `-1` 或线程收到兜底 `WM_QUIT` | 记录错误（如有），销毁仍有效的 HWND，原子清空句柄后结束线程 |
| 当前 presenter 初始化失败 | 清理本次资源后尝试下一 presenter |
| presenter `Present` 失败 | 返回失败并请求后续全量呈现 |
| 未在目标系统执行 | 结果标记“待验证”，不得写“已支持/已保证” |

### 5. Good / Base / Bad Cases

- Good：记录 Windows 7 补丁、GPU 驱动、实际 presenter 和四项人工场景结果。
- Base：仅从源码确认 fallback 分支存在，并明确写“待验证”。
- Bad：因为存在 DWM/DComp 分支就直接写“Windows 7 透明呈现已保证”。
- Good：Debug 与 Release 都确认窗口创建后包含 `WS_EX_NOREDIRECTIONBITMAP`，并多轮启动进入 DComp。
- Good：窗口创建后包含 TOPMOST，主显示器底部保留 1px，独立测试时点击画布后可使用方向键。
- Bad：只在 Debug 验证窗口预设，或在 DComp 配置阶段再补设创建期样式。

### 6. Tests Required

- 目标系统启动与 device/presenter 日志。
- ARM64 Debug/Release 多轮启动，确认 active presenter、创建期 TOPMOST、底部 1px 留边与方向键焦点；涉及窗口预设或工程优化时，两种配置都必须验证。
- 检查 Release 编译命令：窗口宿主与其他自研源码都沿用项目级 `/O2 /GL`，没有文件级 WPO 例外。
- 基础绘制、prediction、抬笔烘干、窗口 resize。
- 关闭窗口、快捷键退出和 `WM_QUIT` 兜底均应断言 HWND 被销毁、窗口线程可等待结束且进程无死锁。
- 能触发时验证 presenter fallback；不能触发时记录环境限制。
- Debug Layer 检查方式可用后，记录无明显 D3D error。

### 7. Wrong vs Correct

Wrong：`Windows 7 上 DWM 透明和 resize 均受支持。`

Correct：`Windows 7 SP1 + KB2670838 是项目兼容目标；当前测试程序包含 DWM/ULW 候选路径，但该环境下的透明和 resize 行为待验证。`

Wrong：`窗口创建后发现缺少 WS_EX_NOREDIRECTIONBITMAP，再用 SetWindowLongPtr 补上。`

Correct：`首选 DComp 时把 WS_EX_NOREDIRECTIONBITMAP 作为 CreateWindowEx 创建参数；创建后缺失则记录并安全回退。`

当前独立测试宿主：`CreateWindowExW` 直接带 `WS_EX_TOPMOST`，高度为 `monitorHeight - 1`，并用 `SW_SHOWNORMAL` 显示以接收方向键。接入 Inkeys 后恢复 NOACTIVATE 时，必须同时恢复创建样式、显示方式与鼠标激活处理。

## D3D Device Initialization

`InitializeGraphicsDevice` 使用 `D3D11_CREATE_DEVICE_BGRA_SUPPORT`：

1. 尝试 hardware device。
2. `E_INVALIDARG` 时用仅含 feature level 11_0 的列表重试。
3. hardware 失败时回退 WARP。
4. 通过 `ComPtr::As` 获取 `IDXGIDevice1`，再取得 adapter 和同源 `IDXGIFactory2`。
5. 最大帧延迟设置为 1。

失败时记录设备或 HRESULT 上下文并返回 `false`；不要在该函数中创建窗口或交换链。

## COM And D3D Ownership

- COM 接口使用 `Microsoft::WRL::ComPtr`。
- 覆盖现有对象时使用 `ReleaseAndGetAddressOf`；查询接口优先使用 `ComPtr::As`。
- renderer 从外部设备资源保存 `ComPtr` 引用，`ReleaseResources` 明确释放所有固定和尺寸相关资源。
- 纹理即将从 SRV 变成 RTV 或被释放时，先解除 VS/PS/OM 绑定。
- 部分初始化失败必须允许调用统一 cleanup，不能依赖进程退出回收。
- `InkRenderer` 的公开资源字段属于实现暴露，不承诺稳定 API。没有专门架构任务时，禁止新增跨模块直接依赖；需要新访问点时优先评估窄方法，而不是继续传播裸资源访问。

依据：`InkRenderer::ReleaseSizeDependentResources`、`ReleaseResources`、`TransparentPresentationController::Impl::ReleaseAttempt`。

## Size-Dependent Resources

以下资源随窗口尺寸重建：

- swapchain backbuffer 与 RTV
- L2 texture/RTV
- L1/L0 的 Add texture/RTV/SRV
- L1/L0 的 Retain texture/RTV/SRV
- ULW staging texture 与 DIB

resize 顺序：

1. 临时持有旧 L2、L1 Add、L1 Retain。
2. 解绑并释放尺寸资源。
3. `ResizeBuffers`。
4. 创建并清空新资源。
5. 复制左上角交集。
6. presenter 更新成功后提交 `WindowController` 逻辑尺寸。

L0 可由当前笔状态重建，不在 resize 中保留。

## Transparent Presentation

当前源码中的初始化回退顺序：

```text
DirectCompositionVisualTree
  -> DwmBlurBehind2 (DwmExtendFrameIntoClientArea)
  -> UlwDirtyRect
```

窗口由 `WindowController` 直接以不含 `WS_VISIBLE` 的 `WS_POPUP` 样式创建，且窗口类背景画刷为空。设备、presenter、RTS 和 renderer 初始化完成后，必须先提交一张透明画布，再显示窗口，避免首帧前出现实色背景。

每种模式先尝试 waitable swapchain，失败后使用普通 swapchain。GPU 透明模式使用 BGRA8、flip sequential、双缓冲和 premultiplied alpha；DWM HWND 路径失败时会尝试 unspecified alpha 兼容模式。

- DComp 运行时加载 `dcomp.dll`，不把 API 可用性当成编译期保证。
- DComp 依赖窗口创建时包含 `WS_EX_NOREDIRECTIONBITMAP`。配置 presenter 时若样式缺失，记录实际扩展样式并回退；创建后补设不属于恢复路径。
- GPU 模式通过 `Present1` 和 dirty rect 呈现。
- ULW 把 backbuffer dirty rect 读回 staging/DIB，并只在 CPU 输出副本中加入 `1/255` alpha 命中测试底层。
- 内部 L0/L1/L2/backbuffer 始终保持真透明背景。
- 厂商、架构或 OS 标签本身不改变上述候选顺序；专用兼容路径必须有完整设备/驱动元组和真实透明度测试结果作为证据。

不要把 ULW 命中测试底层写回 GPU 画布，也不要在回退尝试之间复用上一模式的 swapchain/presenter 状态。

该顺序和实现细节是当前代码事实，不等同于所有兼容目标系统上已经验证成功。无法触发或没有环境证据的模式必须标记“待验证”。

## Error And Diagnostic Pattern

- 顶层初始化失败：输出清晰消息并返回 `-1`。
- 可恢复兼容失败：记录原因并尝试明确的 fallback。
- HRESULT：使用 `LogHResult(step, result)`。
- Win32 `GetLastError`：使用 `LogWin32Error(step, error)`。
- 高频帧日志使用缓存 console handle 与固定缓冲区，避免每帧构造 iostream 格式状态。
- `Present` 只记录连续失败的第一条，成功后重置抑制状态。

当前代码主要以 `bool`/`HRESULT`/状态对象返回失败，没有异常处理体系。不要在单个模块中引入新的异常边界，除非任务明确设计并覆盖所有调用者。
