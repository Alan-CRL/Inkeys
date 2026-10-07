# F-027 退出可见性单调门

日期：2026-09-29。Active task: `.trellis/tasks/09-27-crash-restart-recovery`。

## 实施前边界

- 行为缺口：正式 Close/Restart 的 `SetOffSignal` 首次 CAS 已接受意图，或自动/手动已确认 UEF 抢到 CrashRestart 意图后，Window owner 可能继续处理先前排队的 `Show`、`SetDrawpadSurfaceVisibility(Primary/Presentation)` 和白板恢复命令，把透明画布重新显示。状态线程的 `ProductRunning/revision` guard 只覆盖部分画布切换调用，不覆盖普通 `Show` 或执行中的旧 callback。
- 权威层：`Window::Service` 的两个 owner 线程是 HWND 显隐唯一执行者。关闭门应在首次退出意图成功时无锁发布；owner 在实际执行可能显示的命令时读取，`Hidden/HideAll/Destroy` 继续允许。关闭门单调且不可由普通隐藏、模式切换、未确认的手动崩溃提示触发。
- 文件：`Window.cppm/.cpp` 增加服务关闭门和 owner 复核；`IdtMain.cpp::SetOffSignal` 在成功 CAS 后接线；`Helper.CrashHandler.cpp` 仅对已成功抢到 UEF 意图的路径接线；`InkeysHeadlessTests/window_tests.cpp` 与最小命令行入口做隔离隐藏 HWND 红绿测试。
- 不改 Draw3 bridge、Controller、Host、15 秒监督器、工程与产品恢复功能；不以函数调用推断实际桌面画布或 Win7 行为已验证。

## 已确认调用链

`CloseProgram/RestartProgram -> SetOffSignal -> RequestHideAllUserWindows`；`SetOffSignal` 首次 CAS 在建立 15 秒保护前。旧可见性命令进入 overlay queue 后由 `DrainCommands -> Execute` 执行。现有 `Execute(Show)` 没有退出门；`Execute(SetDrawpadSurfaceVisibility)` 只看调用方可选的 `stillDesired`，随后 `ApplyDrawpadSurfaceVisibility` 可显示 Primary/Presentation。白板恢复也调用 `ShowWindow`。UEF 自动模式在报告前抢意图，手动模式仅 OK 后抢意图；取消/已有 Close 获胜不得关门。

## 验证设计

隐藏 HWND owner 测试使用真实 `Window::Service`：在自建 WndProc 或 `stillDesired` 中阻塞 owner，先排队旧显示请求，再发布关闭门，放行 owner 并读取 Primary/Presentation 实际可见性；`HideAll` 后仍不能被晚到 Show 重显，画布 capture 须释放。先留证当前旧行为红，再实施门禁和回归。命令行测试只使用测试进程拥有的 HWND；真实产品桌面、真笔输入、Win7 仍需单独验收。

## 实施与阶段证据

- `Window::Service::BeginShutdown` 仅 release-store 一次进程本地单调关闭门，无锁、无 HWND/磁盘/GPU 工作；同一 Service 之后的 `Start` 被拒绝。owner `Execute` 拒绝晚到 `Show/Create/Whiteboard 恢复/非 Hidden 成对切换`，但允许 `Hidden/Hide/HideAll/Destroy`。对已进入 `stillDesired` 的旧成对命令、在途 `Show` 和 DWP 完成后的显示再次检查；白板样式恢复按门隐藏，不把普通暂时隐藏当退出。正式 `SetOffSignal` 首次 CAS 后立即关门，建立 15 秒监督后统一无等待排队 `HideAll`。UEF 自动模式成功抢意图后或手动 OK 成功抢意图后才关门；提示/取消/竞争失败均不关门。
- `HideAll` 在 owner 线程释放由本组 HWND 持有的 capture，逐窗隐藏后读回可见性；返回值不再无条件 true。其失败仍可能是 HWND/owner 卡死，不能据此宣称物理桌面已安全。
- 第一轮红证仅将 `BeginShutdown` 暂设 no-op，完整 `InkeysRepo.sln Debug|ARM64` Build exit 0，`InkeysHeadlessTests.exe --exit-visibility-gate-test` exit 4；四项失败分别为晚到 Show、owner capture、执行中 callback、关闭后 Presentation。原始 `TestResults/release-hardening/f027-visibility-red-build-debug-arm64.log` 与 `f027-visibility-red-debug-arm64.log`。后续仅修改本单元测试补原始值诊断，重新暂置同一个 no-op 并再次完整 Build exit 0：`f027-visibility-raw-red-build-debug-arm64.log`、定向 `f027-visibility-raw-red-debug-arm64.log` exit 3，明确 `[ExitVisibilityQueued] show=1 primaryVisible=1 presentationVisible=0`；执行中 callback 为 `applied=1 primaryVisible=1 presentationVisible=0`。这次红版的 HideAll capture 修复仍在，因此不再有 capture 失败；首轮红版记录其旧行为。
- 恢复正式门后第一轮完整 Debug ARM64 Solution Build exit 0，定向测试 exit 0。二次 in-flight 复核补丁后的完整 Build exit 0（`f027-visibility-final-build-debug-arm64.log`），定向测试三次 exit 0（`f027-visibility-final-r{1,2,3}-debug-arm64.log`），`InkeysHeadlessTests.exe --no-window` exit 0（`f027-visibility-final-no-window-debug-arm64.log`）。追加诊断后的最终完整 Build exit 0（`f027-visibility-raw-green-build-debug-arm64.log`）；定向测试 `f027-visibility-raw-green-debug-arm64.log` exit 0，`[ExitVisibilityQueued] show=0 primaryVisible=0 presentationVisible=0`、callback `applied=0 primaryVisible=0 presentationVisible=0`；最终严格 `--no-window` `f027-visibility-raw-green-no-window-debug-arm64.log` exit 0。
- 全部有窗口 Headless 回归曾 exit 1（`f027-visibility-full-window-debug-arm64.log`）：第一项 `message_box_test GDI objects return to the per-process baseline` 在窗口门测试之前报告 initial=45/final=49，窗口门测试本身未报告失败；这是同次运行中的独立失败，不能写成本单元全套 PASS，也未证明它是 H0 既有缺陷。严格 `--no-window` 与定向隐藏 HWND 子集分别成功。

## 仍需验证

真实产品 Close/Restart 点击后的同 HWND 可见性与桌面输入、UEF 崩溃后的可见性、Win7 SP1+仅 KB2670838 及 FL11 HARDWARE/WARP、DComp/ULW 仍缺实机证据。若 owner 卡在 Win32/驱动调用，异步 HideAll 不能保证即时完成；15 秒监督器是进程退出兜底，待保存请求可能按用户既定边界丢失。门在队列执行前和提交后复核，但不宣称比 Win32 单次 ShowWindow 调用更强的原子性。
