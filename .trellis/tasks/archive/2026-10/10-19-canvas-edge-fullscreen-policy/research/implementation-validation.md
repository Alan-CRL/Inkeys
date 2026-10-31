# 双画布实现与验证记录（2026-10-19）

## 已实现

- `Window.CanvasPolicy.h` 提供生产使用的屏幕物理底行到 window-local 的区域差集、NonRude 场景值、HRGN RAII 及双画布提交/逆序回滚流程。Desktop/PPT 共享同一非白板策略，不读取任务栏、UI/DPI 倍率；Whiteboard 恢复创建时的完整/自定义原 region 与原属性。
- `Window.cpp` 在 Drawpad / DrawpadPresentation 首次显示前应用策略（创建时明确去掉 WS_VISIBLE）；每个 WindowRecord 保存本 HWND 的原 region/属性、已应用 key、失败期间显隐，销毁及创建回滚释放资源/恢复本任务添加的属性。
- 白板窗口 mode 包装原 activation-style 事务；两画布显式参加 region/属性事务，不能被旧 style helper 对 DrawpadPresentation 的筛选漏过。两窗共同隐藏、应用、style 完成、恢复原互斥显隐之后才提交白板 mode。任一步失败逆序恢复；回滚未收敛保持双隐藏和原显隐意图。
- SetBounds(Drawpad) 即使批量移动失败也按真实 RECT 重算策略；真实 bounds 变化后旧 region 不再适用于新位置，策略失败时保留双隐藏，避免以旧裁剪重新显示新位置。Show、surface 显隐与白板组恢复核对已应用 HWND/bounds/monitor/scene key，相同 key 不调用 region/属性/隐藏显示。
- EnsureCanvasPolicy 的 owner-thread 重入 guard 防止 SetWindowRgn/ShowWindow/FrameChanged 回调递归提交/缓存；失败通过原 owner 消息循环以 250ms 节拍重试当前已提交场景。正常状态保持无限等待，退出关闭准入后不启动重试或忙循环。白板期望 scene 仍由现有 IdtState phase 循环重发，没有另建 scene authority。
- 保留 Freeze/root 的既有 MarkFullscreenWindow best-effort 机制；没有变更 COM 行为、Draw3 Host、RTS、presenter、文档、撤销/持久化或资源 resize 路径。无需修改 IdtMain/IdtState 或公开 Window API。

## 验证状态

- `git diff --check --ignore-submodules=all`：通过。
- 按原始文件检查格式：Window.cpp、window_tests.cpp 原有 UTF-8 无 BOM + CRLF 保留；animation_tests.cpp 原有 UTF-8 BOM + CRLF 保留；新 header 与相邻 Window.CommandCompletion.h 一致，无 BOM + CRLF。
- 在既有 window_tests.cpp 添加 `RunCanvasPolicyTests()`，由 animation_tests.cpp 的无条件分支调用，因此 `--no-window` 也会执行，不创建 HWND。覆盖负屏坐标、物理 1px、完整 bounds 不变、跨屏交集、原自定义 region 白板恢复、GDI 对象释放、HWND 换代/几何 key、双方显示前设置、每个 capture/hide/property/region/style/show 步骤故障、逆序回滚、未收敛双隐藏、真实 bounds 变更后的不揭开、退出白板恢复。
- 本实现代理未单独启动构建，避免与父任务集成构建竞争。完整 `InkeysRepo.sln Debug|ARM64` 及 `InkeysHeadlessTests --no-window` 由父任务统一执行；这里的测试为已添加，尚未把未执行的结果写作通过。

## 验证边界

- 无窗口测试直接使用生产 region/NonRude 值与生产事务控制 helper，Win32 故障调用以注入 operations 模拟；不证明真实 USER32/Explorer 的消息、DComp/ULW 裁剪或 Shell 非全屏分类已验收。
- 没有启动产品或任何 GUI，没有创建 commit/push。真实 Shell 自动隐藏触发、Win7 与实际笔/触摸输入，继续遵守需显式 GUI 授权的边界。
- API 的 GetWindowRgn 在“无 region”时也返回 ERROR；生产将其作为没有自定义区域处理，创建/复制失败则返回失败。当前产品无其他自定义 region 写入者；仍保存原形状而非假设必须是矩形。
