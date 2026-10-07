# F-026：持久图形恢复失败前的已完成文档保存尝试

日期：2026-09-28。此单元只改 `Draw3.DrawingController.cpp`；不改 Host、RTS、窗口或产品退出协调。它保护 controller 尚存时的**已完成** CPU 文档，不承诺活动接触无损或 durable 提交完成。

## 实施和调用顺序

- 将 `CanvasCommandType::PrepareExitAutoSave` 原有 Desktop → 当前 Presentation → parked Presentation 顺序抽为 `DrawingController::Run` 局部 `captureExitAutoSave(fatal, reason)`。正常退出仍只调用一次并在提交尝试后 `reportCommand`；`reportCommand` 是 worker 入队屏障回执，不是文件 durable 回执。
- 只有两条明确的持续图形失败出口调用相同闭包：`presentation_.RecoverFromRuntimeFailure()` 返回 false，以及恢复后 `historyGpuCache.Initialize()` 返回 false；均在 `window_.RequestExit()`/`Run` break、Host 销毁 controller 之前执行。fatal 捕获按值构造 UInk 快照、经原 observer 向既有 worker 入队，不做 GPU 调用、磁盘 I/O、跨线程等待或未知文件清理。
- 对按现有 policy/revision 判定 eligible 却未入队的 Desktop、active/parked PPT 记录 `reason/source/result=not_queued`；fatal 分支汇总 eligible/queued 并明确写 `durable=pending_worker`。fatal 个别 callback 异常按失败处理且继续尝试其余槽；正常路径维持原调用语义。Desktop 源沿 F-029 的 active/parked 精确选择，PPT 请求仍按本任务前既有的 slot 提交代码。
- 未强制把仍 Down 的 contact 转成 Stored Stroke，也未调用 UEF 崩溃处理或尝试在同一 DComp HWND 上伪造 ULW 成功。活动笔迹、未在当前 history 可见的 GPU/预测数据及窗口代次另属 F-026/F-027 未闭环范围。

## 验证与证据等级

- 主 agent 串行执行完整 `InkeysRepo.sln Debug|ARM64` Build，退出码 0；日志为忽略目录 `TestResults/release-hardening/draw3-f026-fatal-snapshot-build-debug-arm64.log`。随后 Desktop 槽、Laser、普通光栅、CrashFilter、`InkeysHeadlessTests --no-window` 与 `PptCOM.Tests` 六个无窗口入口均退出 0。`git diff --check` 通过，Controller 保留 UTF-8 BOM/CRLF。
- **没有 F-026 真实 fatal 红→绿进程测试。** `DrawingController::Run` 的这两条分支依赖真实 WindowController/Presenter/SwapChain，不能用只调用镜像决策的纯函数冒充故障链。F-029 的生产 Desktop 快照 CLI 与现有 PPT worker 测试分别覆盖源身份和持久化模块，但不证明本分支已在真实设备故障中排队/落盘。
- 独立 reviewer 尚需逐项核对最终 diff、当前/parked slot 身份、正常退出回执顺序、fatal 失败日志与 Host worker drain。Release/三架构、Win7 SP1+仅 KB2670838、Hardware FL11.0 与无 FL11.0→WARP、DComp/ULW/FLIP 实际 Present 均未由本单元验证；未改变 `FLIP_SEQUENTIAL` 或两种 DWM 禁用策略。

## 保留的发布风险

- Host/主协调器必须观察 Draw3 意外停止，成对隐藏两画布并在 Start/Stop 所在线程关闭 RTS、排空 worker；该跨模块 containment 由主 agent 单独实现/验证。本单元只保证在 controller 销毁**之前尝试入队**，不确认后续 `CloseAndDrain`/UInk/index 的终态。
- 活动 contact 的最后真实点未进入当前可见 history；设备彻底失败时它们仍可能丢失。没有安全的 GPU-free CPU seal/文档历史事务和端到端验证前，F-026 只能记部分处置。
- 对 parked Presentation 的 retained Slides，既有 `buildPresentationSaveRequest` 仍捕获当前 active map 而非各 slot 自身 map，存在独立 F-031 身份风险；本单元没有混入修复，须优先另建红→绿及严格读取验证。
