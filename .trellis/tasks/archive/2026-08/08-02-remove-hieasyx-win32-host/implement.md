# Implementation Plan

## Implementation

- [x] 复查 `window_control.cpp/.cppm`、主循环和工程文件中的全部 HiEasyX/EasyX 引用与链接依赖。
- [x] 在 `WindowController` 中实现窗口类注册、`CreateWindowExW`、初始化同步、消息泵、关闭和线程等待。
- [x] 用 `GWLP_USERDATA` 替换 `activeController_` 与 `HIWINDOW_DEFAULT_PROC`，保持现有消息语义。
- [x] 删除 `MouseMessage/TryGetMouseMessage/FlushMouseMessages` 以及绘制控制器中的 HiEasyX 队列清理调用。
- [x] 先构建验证原生宿主，再从 `.vcxproj/.filters` 删除全部 HiEasyX 项和 WPO 例外。
- [x] 删除 `inkStrokeModelerTest/HiEasyX/` 与 `inkStrokeModelerTest/HiEasyX.h`，搜索确认零业务/工程引用。
- [x] 更新 native platform、quality 与 cross-layer 规范，记录原生窗口创建和线程生命周期契约。

## Validation

- [x] 使用 ARM64 MSBuild 完整构建 `Debug|ARM64` 解决方案，确认 Shader 与资源链成功。
- [x] 完整重建 `Release|ARM64` 解决方案，确认不再存在单文件 WPO 例外。
- [x] 运行 ARM64 Debug/Release `inkStrokeModelerTestTests.exe`。
- [x] 用户已完成 Debug/Release 启动验证，DComp active mode、窗口样式和错误日志正常。
- [x] 用户已验证关闭窗口与快捷键退出能够结束进程且无死锁。
- [x] 执行 `rg` 零引用检查、`git diff --check`、编码/换行、范围和未跟踪文件检查。
- [x] 用户已人工验证真实透明、基础绘制、prediction、抬笔烘干、resize 和延迟通过。

## Risk And Rollback Points

- 窗口线程未正确同步会导致初始化读取空 HWND；在删除 HiEasyX 前先验证原生创建链。
- 默认消息处理差异可能影响关闭、激活、光标或 resize；逐项保持现有 `HandleWindowMessage` 分支并把其余消息交给 `DefWindowProcW`。
- 物理删除 HiEasyX 前先确认工程不再编译或包含任何文件；`Vcpkg/` 始终保持不动。
