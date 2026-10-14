# 当前产品启动时序证据

日期：2026-10-12；只读调查，无 GUI 复现。

## 确定事实

- IdtMain.cpp:2832/2852/2884：overlayExStyle = WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW；根 MagnifierHost 与覆盖层创建样式不含 WS_EX_TOPMOST。
- Window.cpp:718/2131：overlayTopmost_ 初始为 true，表示刷新目标策略，不代表 HWND 已完成置顶。
- IdtMain.cpp:3009：StartWindowService 完成创建；3078 调用 StartDraw3ProductWithFallback。
- IdtMain.cpp:752：首次 StartProduct；794：DComp 失败 StopProduct/StopAndJoin 后重建窗口再 StartProduct。两次之前都未请求 topmost。
- Draw3.Host.cpp:1741：stylus.Initialize 在 Host Start 中发生；1745 发布 RtsReady。
- Draw3.RealtimeStylus.cpp:2531 绑定 put_HWND，2687 put_Enabled(TRUE)。后置刷新发生时这些步骤已经完成。
- IdtMain.cpp:3126 首帧检查，3138 ReconcileDraw3Presentation，3148 才首次显式 RequestTopmostRefresh。这甚至可能在首帧显示画布以后。

## 修复边界建议

在产品启动协调层为每一代 HWND 增加置顶前置准备，再调用 StartProduct。不可只在主流程 3078 之前增加一次，否则遗漏 794 回退后的新 HWND。窗口调用仍经 Window Service 根刷新，不在 Draw3 RTS 内直接操作 HWND_TOPMOST。

前置失败应归类窗口层级准备失败并终止该启动，不伪装为 DComp 初始化失败；不得因前置失败转入 ULW 来掩盖问题。保留既有失败清理 deadline、shutdown supervisor、旧窗口销毁/新窗口创建顺序。首帧后的刷新可继续承担 Preview/启动 milestone，不能继续充当 RTS 前提。

## API 与经验边界

微软 SetWindowPos 文档：非零表示该次操作成功；将 owner 置顶会传播给 owned popup，但非置顶 owner 可以拥有置顶 popup。因此主栏 topmost 不能反推画布 topmost。文档未保证未来状态或 RTS 后续行为。
https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos

IRealTimeStylus::put_HWND 文档仅说明选择采墨窗口，未说明 topmost 前提，也未说明违反顺序将不可修复。因此历史措施应记录为产品经验合同，不宣传为微软 API 强制要求或已证明的系统内部机制。
https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-irealtimestylus-put_hwnd

## 仍需验证

同一代 HWND 在前置刷新前/后、RTS 初始化后、首帧后及故障现场的 root/owner/可见/WS_EX_TOPMOST/Z 序。成功返回、实际属性一致和视觉位于普通应用上方是不同证据。当前仅确定代码时序缺口。
