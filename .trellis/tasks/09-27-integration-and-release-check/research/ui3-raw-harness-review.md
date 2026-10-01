# UI3 原始采样 RED 夹具独立静态审查

日期：2026-09-30。Active task：`.trellis/tasks/09-27-integration-and-release-check`。审查实际 `git diff HEAD` 中的 `RenderPipeline.cppm/.cpp`、`render_scheduler_tests.cpp`，以及最新采样设计/设计审查和生产 Scheduler；E02 Main/ShutdownSupervisor 的并行修改不在本报告内。本 reviewer 只写本文件，不改源码、不占构建槽。

## 结论

**可进入 root 的 RED Build/严格 Headless 运行。** 当前是明确的 RED 骨架：导出 DTO 与 API 已声明，`Scheduler::ConfigureRawCapture` 返回 false、`TakeRawCapture` 返回 nullopt、测试 hook 为默认空操作。没有把 stub 结果写成已实现或已验证。新测试调用实际 `Inkeys.UI.RenderPipeline` 模块和真实 Scheduler、TLS/FrameStageTimer；没有复制记录器或百分位算法。

## 实际 diff 核对

- `RenderPipeline.cppm` 导出的 `RawCallbackSample`、`RawBatchSample`、`RawCaptureReport`、test point/hook，以及 Scheduler/global `ConfigureRawCapture`、`TakeRawCapture` 的签名与 `.cpp` stub 相同。module global fragment 增加 `<cstddef>/<optional>/<vector>`，没有另造 module、重复定义或改变现有 `Start/Stop/Request/SetDiagnosticsSink` 实现。`RenderPipeline.Diagnostics.h` 本批没有修改。
- 默认产品路径仍不构造 recorder；stub 未改 `diagnosticsSink`、TLS 判据、16,666,667ns pacing、FrameContext、dispatch、idle/Retry 或设备恢复。正式实现时必须保持这一边界，不能把 RED stub 的静态结论外推到 GREEN 代码。
- `render_scheduler_tests.cpp` 已被 `InkeysHeadlessTests.vcxproj` 登记，`animation_tests.cpp` 的 `RunRenderSchedulerTests()` 在 `--no-window` 路径仍运行；新块只创建真实 Scheduler 的 Win32 event/thread，不创建 HWND，不读取产品用户配置，不用 Office、全局输入或桌面自动化。
- R02 注册 Bar 客户端，真实 callback 写 TLS、阶段与 present 标志，等 5 次 Continue/Idle 后要求 sealed 报告；stub 返回 false/null，故有实质的“未提供采样”红灯。R03 用 capacity2 和实际五次 callback 断言 seen/retained/dropped；R04 读取生产 callback 的数值 payload；R13/14 通过真实 Configure 的暂停点复现 reservation ABA 和 Prepared 释放交错，stub 不会把它们误记 PASS。R07 用一次性 Take 检查所有权。
- R13/R14 的 `CapturePause::Pause` 仅在指定 test point 等条件变量，hook 默认空。两段测试即使 `Wait(2s)` 超时，也在读取断言前调用 `Resume()`、`join()`、清 hook，避免普通断言失败把暂停线程留住。暂停期间不运行 GUI/全局输入。后续实现要确保 hook 调用点在 callbackMutex 外；否则 R13/14 测试本身可能阻塞 `Start` 或第二 Configure。
- 三个已改源文件保持原无 BOM、CRLF，`git diff --check HEAD --` 对本批退出 0；未见整文件换行或格式扰动。

## 非阻断的 GREEN 前修订

1. **R01 可空过。** 默认关闭测试在 `Start()` 后立即 `Stop()`；render callback 未必执行，`sawSample=false` 仍能让测试通过。应以既有 condition/event 等到至少一次真实 callback，再断言该 callback 内 `CurrentFrameDiagnostics()==nullptr` 和 Stop 后无报告。此问题不影响 RED 构建或其它红灯的有效性。
2. **R04 阶段计时断言偏弱。** `stageMs[Draw] >= 0` 对默认零数组也成立。测试已检查其它非默认标志与 payload，故非阻断；GREEN 时宜让阶段经过可确定的非零操作或直接检验 recorder 的 stage 有效标记，避免把未计时字段称已覆盖。
3. **R12/R04 覆盖不等于设计全部验收。** 目前只覆盖 overflow、payload、一次交接和两个 reservation 交错；legal Retry、idle/generation、sink 拒绝、device recovery、Stop finalTasks hold、异常分配/invalid tick、内存预算上界仍未在这次 diff 新增。RED 小批可以先行，但不能在此批 GREEN 后把 R01–R14 全部写 PASS。

## Verification

本 reviewer 只读执行 scoped `git diff HEAD`、`rg` 核实际测试登记和严格 `--no-window` 路由、源文件 BOM/CRLF 计数、scoped `git diff --check`（exit 0）。Lint/TypeCheck/Build/Tests：按任务分工未运行；root 取得唯一构建槽后应使用完整 `InkeysRepo.sln Debug|ARM64` 取得编译退出码，再运行严格 Headless 并保存 RED 断言与自然进程退出码。
