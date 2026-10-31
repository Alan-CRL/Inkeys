# E02 A/B 启动失败边界实际代码独立复审

日期：2026-09-30。Active task：`.trellis/tasks/09-27-integration-and-release-check`。审查实际 `git diff HEAD` 的 `Inkeys/IdtMain.cpp` 和 B 私有夹具所在 `ShutdownSupervisor.h/.cpp`，对照 E02 设计/设计审查/运行前安全审查及生产 fatal 调用链。本 reviewer 不改源码、不占构建或 GUI 测试槽。主会话已有 D004 真实私有红灯：完整 Debug ARM64 构建 0；父 PID14912 自然 exit63，目标 child PID3924 的真实 CoInitializeEx=S_OK、D004 failure/gate1、intent/Arm0，25秒仍活后仅被测试父进程清理；两项鉴权负例拒绝。此证据不等于 A GREEN。

## 结论

**A 当前源码静态 GREEN；D005 前置日志和错误快照两个阻断已修。** D004 和所有本次显式 fatal 分支先建立退出意图，再进入已知可能阻塞的日志/提示/清理。主会话须在最后冻结的源码上重建并运行 D004 green、严格 Headless 和适用回归，不能沿用与最终 D005 补丁并发的旧构建。D005/D003/B002 的私有 GUI 夹具尚未获得逐站点运行安全放行；本静态结论不把它们标为动态通过。

## 已修复的发现

1. **D005 资源发布/验证失败现先 Arm。** `IdtMain.cpp` 的 `RecordPptComFailure` 在首个已知错误处锁存一次 code，随即 `SetOffSignal(1)`。`PublishEmbeddedResourceFileAtomically` false 和 `OpenVerifiedEmbeddedResourceFile` 无效句柄分别先调用它，再写原有 `IDTLogger->error`。Main logger 仍为 `async_overflow_policy::block`；此顺序使随后日志背压处于进程监督之下。资源发布/验证 helper 的 bool/HANDLE 合同不承诺保留 Win32 `LastError`，两者明确记录 `ERROR_GEN_FAILURE`，没有冒称底层原因。现有 DLL 校验/加载策略未被放松。
2. **D005 API 错误在覆盖前锁存。** `CreateActCtx`、`ActivateActCtx`、`LoadLibraryW` 任一失败时紧邻 API 读取 `GetLastError()` 并调用 `RecordPptComFailure`，早于后续 `CloseHandle(verifiedPptCom)`、块策略日志和清理。只保留首个实际失败；`originalError`、critical 日志和 auth-only observation 的 `HRESULT_FROM_WIN32` 沿同一快照。全部资源真实成功、仅私有 synthetic D005 时使用 `ERROR_GEN_FAILURE`，明确不伪报某个 Win32 API 错误。

## 已符合的调用链

- `PublishFatalStartupFailure` 现首先 `SetOffSignal(1)` 再 `ReportFailure`、350ms 失败帧、原文本 `ShowStartupMessage`、500ms 淡出/Stop (`IdtMain.cpp:178–199`)。早 D001/D002 使用同一 helper，保留原消息和 `return 1`；D004 经真实 `CoInitializeEx` 的同一路，私有 child 成功 COM 合成 fatal 后仍执行相应 `CoUninitialize`。若 E01 两监督均失败，`SetOffSignal` 的既有 Failed-only noreturn 接管可能阻止提示，这是守住强退边界的既定行为。
- 晚 D002 (`:2212–2221`) 与 D003 (`:2245–2267`) 在本分支的 logger 前 `SetOffSignal`；D003 的私有 gate 在 Arm 后且仅目标 site 才发布。D101/D201/D301/D401/D202/D102 的既有 `SetOffSignal` 保留，只将可能阻塞的 `critical`、`SetTopmostRefreshObserver({})` 移到后面；提示、返回码、Setting/Draw3/Window 逆向清理和关闭的 Whiteboard gate未改。DComp 首次 `StartProduct` 失败仍走原日志→StopProduct/旧 Window StopAndJoin→第二次 ULW StartProduct，没有被这个 fatal 补丁提前 Arm 或关闭；其未受监督的已知失败清理属于独立 C。
- B001/B002 的 `ActiveSnapshot.failed` 在 Main 每轮 100ms sleep 后、`config.Write()` 前新增一次检查并 Arm (`:2741–2745`)；末尾处理失败快照在失败帧等待、消息构造/Show/Stop 前也 Arm (`:2813–2827`)。B004 `StoppedBeforeReady` 的已知 producer 均以前置 offSignal 非零为条件，不能把它写成新的未保护生产来源。已接受 Close 的重复 SetOffSignal 由首 CAS 拒绝，不重置既有截止。
- B 私有 selector 仍精确鉴权后才进入真实 wWinMain，默认 site None/null；普通产品 `isolatedStartupFailure=false`，旧 E01/UEF argc 路由未被 A 改动。复制目录、单实例、更新/旧 EXE、SuperTop、注册表、shortcut/DDB、后期 PPT/Office/业务线程隔离仍按先前 `startup-failure-harness-safety-review.md`；父测试只持本 child HANDLE，不按进程名清理用户程序。

## 未阻断 A 的限制与后续验证

- **先前 B001/B002 与 `config.Write()` 竞态假设已排除。** Main 该处同步写仅在 `TakeCommittedStartupBarWidthDip` 取到值时发生；唯一发布者是 `Bar.RenderLoop.cpp:12729–12741` 的首次完整成功呈现。B001 的 `InitializeWindow` 失败 (`Bar.Initialization.cpp:133–137`) 和 B002 的 `Register` 失败 (`Bar.RenderLoop.cpp:916–925`) 都在第一次成功帧之前，不能同时已有宽度值。`SetBarStartupState` 在 `FirstFrameCommitted` 后拒绝晚到失败态 (`StartupPreview.cpp:1116–1133`)；`Rendering()` 仅在 Bar 初始化调用。因此这些真实 producer 不会在顶端检查之后、该 `config.Write()` 中途新发布 B001/B002。其它异步故障来源需各自证据，本排除不泛化为任意异步失败都立即 Arm。B002 synthetic green 仍只证明指定状态传播与主循环次序，不证明自然 Register 故障已动态注入。
- D005/D003/B002 私有夹具未做各自的 DLL/字体/Window-Host 前序副作用复审和运行；不使用 `--startup-failure-only` 无 site 的全套作为下一轮默认 GUI 测试。D004 绿灯需记录 mapped failure/gate/Arm/intent、原截止、精确 child 自然死亡和退出码；父进程因上限强杀仍是 FAIL。还应有不挂 gate 时原提示和清理的正常反例；这次尚未运行。
- `Host::Start(false)` 内部 join/drain 和 Window rollback/DComp→ULW 旧 generation 的可取消失败清理设计 C 尚未批准/实施；不能将本次 Arm-first 说成已消除所有初始化卡死。

## Verification

只读执行 scoped `git diff HEAD`、fatal 调用链及 Main logger/资源 API 检查、B001/B002 宽度发布者与 Bar 状态单调性核对、E02 报告核对；`git diff --check HEAD --` 三份源码退出 0。Lint/TypeCheck/Build/Tests/GUI：本 reviewer 未运行。主会话的 D004 红构建/测试记录已在实施记录注明，本审查未独立重跑；静态 GREEN 不替代最终冻结源码的构建及 green 测试。
