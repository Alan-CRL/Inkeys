# 主栏反馈修复（2026-10-15）

- `Bar.A2.h`：未进入白板时也使用标准 2×2 投影，功能关闭路径保持原行为。UI 子代理同步分页测试中的 A2 断言。
- `Bar.MainLayoutPolicy.h` / `Bar.RenderLoop.cpp`：现有换向计算已只输入显示器左右边界，且 Main.cpp 已将白板/PPT 的控件排斥签名清空。实际问题是居中态无条件锁方向；白板在空间不足策略下取消稳定期的居中锁，仍保留入场/位置转场锁与中线策略的居中锁，避免中线反复翻转。
- `Bar.Main.cpp` / `Bar.BottomDock.h`：使用 Window Service 的 `WhiteboardWindowGroupMinimized()`；最小化仅将屏底 5 DIP 停靠线退回工作区，不改变工作区身份。
- `Bar.RenderLoop.cpp`：最小化边沿将已有展开底栏居中；恢复边沿还原先前居中选择。进入桌面前快照不重写，关闭最小化白板时桌面恢复优先。浮动和折叠保持其原形态。
- 新增 `bar_bottom_dock_tests.cpp` 和 `ppt_session_tests.cpp` 纯策略回归，覆盖空间不足方向锁、最小化停靠线、白板/PPT/折叠区别。

验证：`git diff --check` 通过（另有父任务 PRD 的 LF 自动转换提示）；保留各源文件原字节编码和 CRLF。未运行 GUI 或独立 solution build；由主会话在各子代理交付后统一构建、运行 headless。真实最小化/恢复、边缘换向动画仍需人工验收。
