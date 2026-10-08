# Design

## Architecture

`WindowController` 继续作为唯一窗口边界，但直接拥有原生 HWND 与消息线程：

```text
main/draw thread
  -> WindowController::Initialize
  -> start owned window thread
  -> RegisterClassExW
  -> CreateWindowExW(WS_POPUP, optional WS_EX_NOREDIRECTIONBITMAP)
  -> signal initialization result
  -> GetMessageW / TranslateMessage / DispatchMessageW
  -> WindowController::WindowProcedure via GWLP_USERDATA
```

主线程等待“窗口创建完成”事件后才继续初始化 D3D/presenter。窗口线程在收到 `WM_DESTROY` 后 `PostQuitMessage`；析构函数在窗口仍存在时投递 `WM_CLOSE`，随后等待线程退出并关闭同步句柄。

## Window Creation Contract

- 窗口类不设置背景画刷，避免 GDI 擦出白底。
- 样式固定为 `WS_POPUP`，位置与尺寸来自主显示器矩形。
- `ShouldPreconfigureNoRedirectionBitmap()` 的结果只转换为 `CreateWindowExW` 的 `dwExStyle`，不再经过全局预设。
- 窗口以隐藏状态创建，不使用 `WS_VISIBLE`；现有首帧透明画布完成后调用 `ShowWindow`。
- 控制台由项目的 Console 子系统自然保留，不再由 HiEasyX 隐藏/恢复。

## Message Routing

`WindowProcedure` 在 `WM_NCCREATE` 从 `CREATESTRUCTW::lpCreateParams` 取得控制器并写入 `GWLP_USERDATA`。之后直接调用实例的 `HandleWindowMessage`；没有自定义处理的消息返回 `DefWindowProcW`。

`WM_DESTROY` 除现有退出请求和绘制线程唤醒外，还必须调用 `PostQuitMessage(0)`。`WM_NCDESTROY` 清除 `GWLP_USERDATA`，防止 HWND 生命周期结束后继续保留实例指针。

## Thread And Lifetime Contract

- 禁止 detached thread。
- 初始化同步必须使用事件或等价同步原语，不能轮询普通 `int/bool/HWND`。
- `WindowController` 析构前，后续声明的 D3D、presenter、RTS 和 drawing 对象已经按 C++ 逆序完成释放；窗口最后关闭。
- 如果窗口注册、线程创建或 `CreateWindowExW` 失败，记录 Win32 上下文，唤醒初始化等待者并回收已创建的线程/句柄。
- 窗口被用户提前关闭时，消息线程自然退出；析构只需要等待，不重复销毁无效 HWND。

## Dependency Removal

- 删除 HiEasyX 源码和聚合头文件。
- 从 `.vcxproj` 和 `.vcxproj.filters` 删除全部 HiEasyX `ClCompile/ClInclude` 项以及 `HiWindow.cpp` 的 Release WPO 例外。
- 删除 `MouseMessage`、`TryGetMouseMessage`、`FlushMouseMessages` 及绘制控制器中的队列清理调用。窗口线程持续分发 Win32 消息，因此不再存在 HiEasyX 的第二份消息积压。
- 保留项目仍实际需要的系统库和 ink stroke modeler 库；只有经引用/链接检查证明专供 HiEasyX 的依赖才移除。

## Compatibility And Rollback

- 所用核心 API 均为 Win7 可用的 User32 API；Win7 路径不会设置 `WS_EX_NOREDIRECTIONBITMAP`。
- DComp/DWM/ULW 回退顺序和交换链实现不变。
- 风险集中在窗口线程初始化、默认消息处理和退出顺序。实现分为“原生宿主可构建运行”与“物理删除依赖”两个检查点；若原生宿主不稳定，可在删除目录前回退窗口控制文件和工程项。
