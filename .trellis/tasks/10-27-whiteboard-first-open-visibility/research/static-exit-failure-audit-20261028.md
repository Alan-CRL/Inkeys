# Research: 静态定位白板退出失败

- Query: GUI 与桌面操控暂不可用时，能否从当前代码静态确认白板退出失败的根因。
- Scope: internal / 静态源码与 headless 测试审查；未启动 GUI、未改产品代码、未运行构建。
- Date: 2026-10-28

## Findings

生产协调循环把 `PrepareWorkspaceWindowPolicy` 返回空和 `CompleteWorkspaceWindowPolicy` 返回 false 都映射为同一个 `nativeFailed` 路径（`Inkeys/IdtState.cpp:3398-3406,3422-3423,3445`）。该路径仅检查最近命令回执是否为 Cancelled/Expired，再把其他结果计入同一请求的失败预算；没有保留失败位于 Prepare/Complete、窗口角色、Win32 API 结果或回滚是否完成。因此静态代码能确认提示的直接机制和 UI 部分状态，却不能从提示本身反推某个确定 API 根因。

白板退出先发布背景 inactive 并等待 ACK，之后才完成窗口策略；主栏的 `SetWhiteboardActive(false)` 只在成功提交后执行（`Inkeys/IdtState.cpp:3407-3411,3449-3463`）。所以“白板和分页已消失、主栏仍在白板态”与现有分阶段提交一致，是 native Complete 或后续事务长期失败时可观察到的状态；提前清主栏会伪造工作区已提交。

Complete 策略按 `ValidatePrepared → ApplyInput → ApplyVisibility → PublishFullscreen` 顺序推进（`Inkeys/Inkeys/Window/Window.CanvasPolicy.h:148-152`）。其中 Complete 的输入与显示操作均可能失败；真实执行路径包在 `ExecuteTracked` 中，失败后尝试恢复捕获的窗口快照，若恢复未完成则 owner 保留恢复责任并拒绝后续事务（`Inkeys/Inkeys/Window/Window.cpp:3190-3264`）。该屏障足以解释后续四次尝试继续失败，但当前提示和 `LastCommandReceipt` 不携带诊断，不能确定现场是否命中该路径。

现有 `window_tests.cpp` 的 workspace policy 测试将每个编排步骤替换为编号步骤，并验证失败后停止后续写入（`InkeysHeadlessTests/window_tests.cpp:1000-1053`）；这验证步骤顺序，不执行真实 USER32，也不覆盖 `ExecuteTracked` 的恢复结果如何映射到 `WorkspaceHandoff::Fail`。它不能证明也不能排除现场 API 的系统特定失败。

可静态确认的结论：代码结构能解释错误提示、四次失败后停止重试、背景已隐藏而主栏未提交，以及确定按钮本身不会重启程序。不能静态确认的结论：具体失败阶段/API/错误码、回滚是否失败，以及用户观察到的新进程由何处创建。需要将失败阶段与实际窗口命令、API 错误和 rollback 结果记录到可关联的日志，或以 headless 注入测试覆盖生产接缝，才能形成有把握的代码修复。

## Files Found

- `Inkeys/IdtState.cpp` — 工作区请求、重试预算及失败阶段归并。
- `Inkeys/Inkeys/Window/Window.CanvasPolicy.h` — 原生 Prepare/Complete 策略步骤。
- `Inkeys/Inkeys/Window/Window.cpp` — HWND owner 的策略执行、恢复事务及屏障。
- `InkeysHeadlessTests/window_tests.cpp` — 无 HWND 策略编排的故障注入测试。
- `.trellis/spec/native-desktop/whiteboard.md` — 三工作区与白板进出合同。

## Caveats / Not Found

- 缺少用户故障实例的命令收据、Prepare/Complete 阶段、Win32 错误码、恢复快照结果及新进程 PID/父 PID；根因不可唯一确定。
- 未进行 GUI/桌面测试；遵循用户本轮限制。
- 未改产品代码。未发现能仅凭静态代码安全地选择某个 USER32 操作并改写的证据。
