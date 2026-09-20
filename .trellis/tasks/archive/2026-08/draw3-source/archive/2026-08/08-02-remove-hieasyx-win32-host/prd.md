# 移除 HiEasyX 并替换原生 Win32 窗口宿主

## Goal

完全移除当前程序对 HiEasyX 的源码、头文件、工程项和运行时行为依赖，用一个只承担 HWND 创建、消息泵和生命周期管理的原生 Win32 窗口宿主替换，减少无用 GDI/EasyX 状态并消除 HiEasyX 全局预设与 Release LTCG 的优化敏感边界。

## Background

- 自研源码只有 `draw3/window_control.cpp` 直接包含 `HiEasyX.h`。
- 当前直接使用的能力仅为 `PreSetWindowStyle*`、`PreSetWindowPos`、`PreSetWindowShowState`、`initgraph_win32`、`peekmessage_win32`、`flushmessage_win32` 和 `HIWINDOW_DEFAULT_PROC`。
- D3D11/DXGI、DComp/DWM/ULW、renderer、Shader、RealTimeStylus、prediction、工具和触觉反馈均由 `draw3` 自己实现，不依赖 HiEasyX 绘制或 UI。
- `TryGetMouseMessage` 已无调用者；`FlushMouseMessages` 只清理 HiEasyX 复制的 `ExMessage` 队列，不是 Win32 窗口线程的消息泵。
- 主绘制循环长期占用主线程，因此窗口仍需要独立、可控且可等待退出的消息线程。

## Requirements

- 删除 `inkStrokeModelerTest/HiEasyX/`、`inkStrokeModelerTest/HiEasyX.h` 及 `.vcxproj/.filters` 中全部 HiEasyX 编译和头文件项。
- `WindowController` 直接注册窗口类并使用 `CreateWindowExW` 创建主显示器大小的 `WS_POPUP` 窗口。
- 首选 DComp 时必须把 `WS_EX_NOREDIRECTIONBITMAP` 直接传给 `CreateWindowExW`；Win7 或 DComp API 不可用时保持扩展样式为 `0`。
- 创建时保持隐藏；透明 presenter 提交首张透明画布后仍由现有 `WindowController::Show` 显示。
- 窗口线程必须由 `WindowController` 明确拥有，初始化有同步完成信号，析构时关闭窗口并等待线程结束，不能使用 detached thread、普通全局预设或轮询共享标志。
- 使用 `GWLP_USERDATA` 把 HWND 绑定到对应 `WindowController`，删除单实例 `activeController_` 和 `HIWINDOW_DEFAULT_PROC` 哨兵协议。
- 保留现有窗口消息行为：resize、DWM composition change、鼠标/笔光标、Tablet Pen Service 属性、清屏/工具快捷键、关闭退出及绘制线程唤醒。
- 删除无调用的 `MouseMessage/TryGetMouseMessage` 和 HiEasyX `ExMessage` 清理接口；绘制输入仍以 RTS、Pointer 与现有 Win32 状态采样为准。
- 保留控制台子系统和现有诊断输出，不引入新的窗口框架或第三方依赖。
- 更新现行 Trellis 规范：由 HiEasyX 文件级 `/GL` 例外改为原生 Win32 创建期样式和线程生命周期契约；历史任务记录保留历史事实。

## Acceptance Criteria

- 自研源码和工程文件不再包含 `HiEasyX`、`hiex::`、`initgraph_win32`、`HIWINDOW_DEFAULT_PROC`、`ExMessage` 或 HiEasyX 消息 API；HiEasyX 目录与聚合头文件不存在。
- ARM64 Debug 与 Release 完整解决方案构建通过；不再需要 `HiWindow.cpp` 的文件级 `WholeProgramOptimization=false`。
- ARM64 Debug/Release 自动测试通过。
- Debug 与 Release 多轮启动均首先尝试并激活 `DirectCompositionVisualTree`，窗口创建后包含 `WS_EX_NOREDIRECTIONBITMAP`，不出现错误 87。
- Alt+F4/关闭窗口和快捷键退出都能结束绘制循环、销毁 HWND 并回收窗口线程，不遗留进程或死锁。
- 用户在真实桌面上确认窗口透明、基础绘制、prediction、抬笔烘干和 resize 正常，且无明显延迟回归。
- `git diff --check`、编码/换行和修改范围检查通过；未跟踪的 `Vcpkg/` 不纳入修改。

## Out of Scope

- 改写 D3D renderer、透明 presenter、RTS 或绘制算法。
- 引入 WinUI、WPF、SDL、GLFW 等替代框架。
- 改变窗口为多窗口、可停靠或带系统标题栏。
- 自动化最终桌面 alpha 的视觉判断；该项仍由用户实机确认。
