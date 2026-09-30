# F-029：停放 Desktop 槽的退出自动保存修复

日期：2026-09-28。当前实现仅修改 `Draw3.DrawingController.cpp/.cppm`；主 agent 独占的 `IdtMain.cpp` 增加显式 CLI 接线。本单元没有改工作区切换触发点、UInk/索引格式、worker、PPT 自动保存、FLIP 或窗口路径。

## 行为和所有权

- 将原 `DrawingController::Run` 内的 Desktop 可见 history → `Draw3UInkExportSnapshot` 捕获逻辑原样提取到同翻译单元 `CaptureDesktopAutoSaveForScene`。它接收借用的 `DesktopAutoSaveSource{document,pageRuntimeStates,pageIndex}`，仍按 `DesktopAutoSavePolicy::ShouldCapture`、`window_.AutoSaveEnabled()`、当前页可见 RenderItem、同一 `workspaceGuid/pageGuid`、同一 `DesktopAutoSaveTrigger` 和原 `observer.desktopAutoSaveRequested` 走 Host 单 worker。没有在 helper 中读写文件、访问 GPU 或跨线程等待；值快照在绘制线程构造并转交既有 worker。
- `SelectDesktopAutoSaveSource` 对活动 Desktop 保持原源；仅在 `trigger==Exit` 且活动 workspace 为 Presentation/Whiteboard 时选择 `desktopSlot`。非 Desktop Clear 仍不读取停放槽；空槽、空页或开关关闭不提交。`PrepareExitAutoSave` 仍调用一次 `captureDesktopAutoSave(Exit)`，该 wrapper 已传入活动和停放两份明确源，因此不会把当前 PPT/Whiteboard 的页 GUID 或墨迹写入 Desktop 文件。工作区切换时不新增保存。
- 独立 no-HWND 入口 `RunParkedDesktopExitAutoSaveTest()` 调用上述**同一生产捕获函数**，使用真实 `InkCanvasCollection`、`InkCanvas::AppendStroke`、`BuildStrokeTileFootprint`、`CanvasRuntimeHistory::AppendStroke`，以回调接收不可变 UInk 快照，不触碰用户配置或磁盘。它覆盖活动 Desktop、PPT/Whiteboard 活动时的 parked Desktop、Desktop/PPT 不同工作区和页 GUID/点隔离、非 Desktop Clear、关闭开关和空页。

## 红绿证据

- Stage 1 保留原只认活动 Desktop 的生产选择条件。完整 `InkeysRepo.sln Debug|ARM64` Build，命令级 `/p:LinkIncremental=false`，退出码 0；同一 `Inkeys.exe --draw3-parked-desktop-exit-test` 无 HWND 进程退出码 1，明确失败两格：`PPT-active Exit captures parked Desktop, not PPT page or ink` 和 `Whiteboard-active Exit captures parked Desktop exactly once`。原始日志在忽略目录 `TestResults/release-hardening/draw3-parked-desktop-red-build-debug-arm64-fixed.log` 与 `draw3-parked-desktop-red-debug-arm64.stderr.log`。首次构建因主入口缺 module import 失败，主 agent 修正 CLI import 后取得上述可用红灯；没有改动本模块以绕过构建。
- Stage 2 仅修改 `SelectDesktopAutoSaveSource` 的 Exit 源选择。完整 `InkeysRepo.sln Debug|ARM64` Build 退出码 0；同一无 HWND CLI 退出码 0，stderr 为 `PASS: production Desktop Exit source and snapshot`。日志：`draw3-parked-desktop-green-build-debug-arm64.log` 与 `draw3-parked-desktop-green-debug-arm64.stderr.log`。主 agent 另运行隔离数据的既有 Desktop AutoSave service 全套，退出码 0；其验证的是 worker/UInk/index 路径，不能替代本次 Controller 源选择回归。
- `git diff --check` 通过；两个 Controller 文件仍保持原 UTF-8 BOM 与 CRLF。独立 reviewer 正检查最终 diff，本报告不把实施者自己的检查记为独立通过。

## 未验证和边界

- 实际产品 `Desktop → PPT/Whiteboard → 正常退出` 的 Host、RTS、Window Service、真实磁盘文件和 Office/Win7 组合未运行：本仓库 AGENTS 默认不允许无授权启动 GUI。当前自动测试证明绘制线程应提交的 Desktop 值快照及 worker 的独立持久化能力，不证明真进程退出时二者已经贯通，也不证明新进程自动可见恢复（该入口当前未开放）。
- 若 presenter/device 持久恢复失败导致 Controller 在最终屏障前直接退出，F-026 仍可丢失尚未入队的 active/parked 文档；活动 contact 封口也不在 F-029 范围。F-029 修复只覆盖**正常到达** `PrepareExitAutoSave` 的场景。
- 仍须在最终 Release/三架构及可用的 Win7 SP1+仅 KB2670838 环境复验；现有 Win11 ARM64 Debug Build 不可推导 Win7 硬件 FL11.0、WARP 或 DComp/ULW 实际呈现。用户已实测 Win7 FLIP 可用，本改动没有改变 `FLIP_SEQUENTIAL`。
