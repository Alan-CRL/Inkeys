# C1.6a 设计：教学安全模式与致命路径统一自动重启

依据：`prd.md`、`research/codex-fatal-paths-20261126.md`；Codex 设计审查见 `research/codex-design-review-20261126.md`（第 1 版设计的 5 条阻断与 3 条应改已在本版处理，见 §7）。行号以 2026-11-26 为准，实施前复核。

## 1. 总体思路

**所有致命情况最终都汇入现有的未处理异常过滤器（UEF）。**

UEF 已经具备这些能力：写 dump 与报告、三档处理、抢 `Intent::CrashRestart`、由监督进程确认旧进程死亡后再拉起、新进程带 `-CrashTry`、5 分钟内再次崩溃不再拉起。所以不另造重启逻辑：

1. **Windows 回调补漏**：会绕过 UEF 的进程内入口（`abort`/`terminate`、invalid parameter、pure call）改为主动抛出自定义 SEH 异常，进入 UEF。
2. **软件内致命与卡死**：新增看门狗线程。收到“致命故障报告”或发现卡死、持续失败时，在**看门狗自己的线程**上抛出同一个自定义 SEH 异常，进入 UEF。
3. **已知失败的强退链**（`FailedCleanupDeadline`）不能等异步看门狗，在其 publisher 内直接按档位抢 `CrashRestart` 意图（§3.7）。

预期成立的前提：项目使用默认 `/EHsc`，编译记录中未见 `/EHa` 或 `/guard`；产品代码里没有 VEH、`__try/__except`，也没有其他地方调用 `SetUnhandledExceptionFilter`（Codex 审查核实）。在此前提下，C++ `catch (...)` 不会吞掉 SEH，所以自定义异常**预期**能到达 UEF。**这一点必须用隔离子进程测试逐个入口实测（§5）**，不能只凭推断。某个入口若实测收不到 UEF，该入口改为直接调用 UEF 的同一处理核心，再显式终止进程。

```
Windows 回调 ───────────────────────┐
SIGABRT / invalid param / purecall ─┤→ RaiseException(kInkeysFatal) ─┐
Fatal::Report(reason) → 看门狗线程 ─┘                                 ├→ UEF → 三档处理（现有）
看门狗发现卡死 / 持续失败 ──────────↗                                  │
FailedCleanupDeadline publisher ──→ 直接抢 CrashRestart 意图 ─────────┘（不经 UEF）
```

## 2. 设置与配置

| 项 | 改动 |
|---|---|
| `Application.TeachingSafetyMode` | 收紧为 0/1/2：0 提示并重启（默认）、1 静默重启、2 直接退出。`Other.Config.cpp:747–754` 的上限改为 2，越界或类型错误回 0（项目约定旧配置不兼容）。`Setting.cpp:3350` 的 clamp 改为 0–2 |
| `Application.CrashRecoverInk`（新） | `IdtAtomic<bool>`，默认 `true`，供 C1.6b 读取 |
| UEF 档位映射 | 0 → 现有提示流程；1 → 现有自动流程；2 → 写报告后返回 `EXCEPTION_EXECUTE_HANDLER`（不交给系统 WER）。删掉旧的 3 → `EXCEPTION_CONTINUE_SEARCH` |
| 界面 | `Setting.Pages.cpp:1412–1417`：分组标题改为“教学安全模式”；`ComboRow` 改为三项；下方新增 `ToggleRow`。草稿与事件按现有 `safetyMode` 的写法补 `recoverInk`（`Setting.cpp:3344–3350,3468–3472`） |
| i18n | `SettingsUI.Design.SafetyRecoverySection` 改为“教学安全模式”；`SafeMode.Mode1–3` 改为三档文案，删 `Mode4`；新增 `SafeMode.RecoverInk.N/E`。先改 zh-CN，再用 pwsh 7 跑 `Scripts/i18n.ps1 sync` 并补译其余语言 |

**“提示并重启”的弹窗**（现状核实：`MakeOkRequest` 的默认结果和关闭结果都是 `Ok`，`MessageBox.cpp:184–193`）：

- 用户点“确定”或关掉窗口：都返回 `Ok`，重启（现状如此，不改）。
- `Show()` 返回 `Failed`（无法建立提示，例如内存不足）：**新增**按重启处理。
- 弹窗返回后仍保留现有的 `TryClaimCrashRestartIntent(..., true)` CAS：如果此时用户的关闭或重启已经先占了意图，就沿用对方的意图。
- **已知限制**（现状，不改）：报告阶段（写 dump 和报告）有 15 秒截止，超时直接 `TerminateProcess`，不会重启（`Helper.CrashHandler.cpp:48–58`）。“提示并重启”档在报告前不抢意图，所以这种极端情况下不会重启。写入验收说明。

## 3. 新模块 `Helper/Helper.Fatal`（cppm + cpp）

```cpp
namespace Inkeys::Fatal
{
    enum class Reason : uint32_t { Draw3Stopped = 1, Draw3PresentFailures, UiDeviceRecovery,
        UiClientFailures, FreezeFailures, ThreadHang, CrtAbort, CrtInvalidParameter, CrtPureCall,
        DebugInjected };
    constexpr DWORD kExceptionCode = 0xE1430100;            // 与现有 0xE14300xx 退出码同前缀
    void Report(Reason, uint32_t detail = 0) noexcept;       // 任意线程：写原子量并唤醒看门狗
    [[noreturn]] void RaiseNow(Reason, uint32_t detail = 0) noexcept;

    enum class Slot : uint8_t { UiScheduler, Draw3Loop, Count };
    void Beat(Slot) noexcept;   // 开始/继续处理一个工作单元：记为忙碌，并刷新时间戳
    void Idle(Slot) noexcept;   // 即将进入真正的空闲等待：记为空闲

    enum class FailureSlot : uint8_t { UiDeviceRecovery, UiBar, UiSetting, UiPageControl,
        UiWhiteboard, UiSelection, UiStartupPreview, Draw3Present, Freeze, Count };
    void RecordFailure(FailureSlot) noexcept;   // 真实故障（API 失败）
    void RecordSuccess(FailureSlot) noexcept;   // 该客户端一次成功提交
    void ClearFailure(FailureSlot) noexcept;    // 隐藏、注销、换代：清除计时，不算成功也不算失败

    void StartWatchdog() noexcept;  void StopWatchdog() noexcept;
    bool RuntimeArmed() noexcept;   // 看门狗已启动（启动门禁已完成）
    void InstallCrtHandlers() noexcept;  void RestoreCrtHandlers() noexcept;
}
```

- 除 Start/Stop/Install/Restore 外，接口全部 `noexcept`，只写原子量；不分配、不加锁、不写日志。可以在渲染热路径和内存不足时调用。
- `RaiseNow` 调用 `RaiseException(kExceptionCode, EXCEPTION_NONCONTINUABLE, 2, {reason, detail})`。
- `WriteCrashReportTxt` 遇到 `kExceptionCode` 时额外写出原因名与 detail，便于事后区分真崩溃与兜底触发。

### 3.1 看门狗线程

- **启动**：绑定到启动门禁完成（`Inkeys::Startup::ActiveSnapshot()` 显示启动成功完成），而不是“主循环开始”。主循环开始时异步启动线程可能仍在运行、随后还会发布启动失败（`IdtMain.cpp:4222–4265`）。启动期和启动早退不受看门狗监督，仍走现有的启动失败提示。
- **周期**：每 500 ms 醒一次，`Report` 时立即唤醒（事件）。
- **暂停**：
  - 用 `InterlockedCompareExchange(GetOffSignalInteropPointer(), 0, 0)` 读意图槽（0 未占、1 Close、2 Restart/UpdateRestart、3 CrashRestart）。非 0 表示退出已被受理，关闭流程有自己的 15 秒监督强退（`IdtMain.cpp:358`），看门狗不再触发。
  - UEF 正在处理（`g_isGeneratingDump` 已置位，例如提示档正在等用户）：不再触发，避免重复。
  - `IsDebuggerPresent()`：只写一次 warning 日志，不触发。
- **判定**，满足任一条且触发前复查意图槽仍为 0，就调用 `RaiseNow`；最终谁生效仍由 UEF 内的意图 CAS 决定：
  1. 收到 `Report`。
  2. 某个 `Slot` 处于忙碌，且距最后一次 `Beat` **≥ 15 s** → `ThreadHang`，detail 为槽号。
  3. 某个 `FailureSlot` 自第一次失败起持续 **≥ 10 s**，期间没有 `RecordSuccess` 或 `ClearFailure` → 对应 Reason。
- 触发前尽力写一条 `Log::Critical`（`TryFormat`，拿不到锁就放弃）。

### 3.2 卡死检测：以“工作单元无进展”为准

第 1 版用“连续忙碌时长”判定，这是错的：UI3 连续播放动画时可能一直不进入空闲，但每批都在推进。改为：**每开始一个工作单元就 `Beat` 一次**，判定的是“当前工作单元已经执行了多久”；只在真正的空闲等待前 `Idle`。

| 线程 | Beat | Idle |
|---|---|---|
| UI3 共享调度器（`RenderPipeline.cpp:681–913`） | 每次从 `WaitForSingleObject(wakeEvent)` 返回后、执行 `takeControl()` 前；每批开始时；每个控制任务执行前；每个客户端回调前 | 紧挨 `WaitForSingleObject(wakeEvent, ...)` 之前。帧节拍等待（`Bar.FramePacing.cppm:390–406`）由定时器唤醒，等待前后各 Beat 一次，不需要 Idle |
| Draw3 `Run`（`DrawingController.cpp`） | `while (true)` 循环顶部；`WaitDequeue` 返回后、处理取到的命令之前；橡皮空闲的每秒等待循环每醒一次 | 只在 `WaitDequeue(record)` 之前（`:25903–25905,25941–25942`），以及 `input_.WaitForWake` 的有限等待之前（`:27940–27950`） |

**保留在忙碌状态、受监督的同步工作**：GPU `Map` 阻塞（`CompleteUlwBeforeIdle`、`ServiceUlwBeforeFrameWait`、`BeginUlwFrame`）、设备重建、页面物化与重放、Clear 与离页的同步整页复制。它们卡住超过 15 秒就视为卡死。

**合法长任务的风险**：Draw3 和 UI3 都会在本线程同步重建设备；重页的物化、重放与排空也在绘制线程。正常情况下远小于 15 秒，但低端设备的最坏耗时没有实测过，见 §5 的实测项。读盘和导出在 worker 上，PPT COM 轮询在单独线程，都不在监督范围内。

Draw3 退出与封存路径（`Drain`、`whiteboardStore` 析构里的 `join`）发生时，意图槽已非 0，看门狗已暂停，所以不受影响。

### 3.3 持续失败检测：在客户端的真实故障点记录

第 1 版在调度器层把 `Retry` 当作失败，这是错的。核实结果：`Retry` 同时用于延期、退避、等待回执和真实故障。例如设置窗口被遮挡时一直返回 `Retry`（`Setting.cpp:5823–5834`）。所以调度器返回语义不变，改为**在各客户端已有的故障分支上记录**：

| FailureSlot | RecordFailure（只在真实 API 失败） | RecordSuccess | ClearFailure |
|---|---|---|---|
| UiDeviceRecovery | `deviceRecovery()` 返回 false（`RenderPipeline.cpp:779–811`） | 返回 true | — |
| UiBar | `EnsureDeviceResources` 失败（`Bar.RenderLoop.cpp:9350–9371`）、`EndDraw`/source 真实错误 | 成功提交一帧 | 隐藏、注销、epoch 换代 |
| UiSetting | `ResolveSettingResourceFailure` 各调用点（`Setting.cpp:5716–5842`），**不含**遮挡探测的 `Retry` | 成功 Present | 窗口隐藏、会话结束 |
| UiPageControl / UiWhiteboard / UiSelection / UiStartupPreview | 各自 ULW/Present/资源 API 返回失败的分支（不含等待回执、lease 竞争、版本变化、退避） | 成功提交 | 隐藏、注销、换代 |
| Draw3Present | 实际光栅提交失败或 Present 失败（活动帧与空闲帧两条路径都要覆盖，`DrawingController.cpp:25341–25378,27620–27624,27863–27881`） | 成功 Present | — |
| Freeze | 运行时 DIB 分配失败及所有 `submitSurface` 失败结果（`IdtFreezeFrame.cpp:171–258`，目前有几处丢弃了结果，要接齐） | 成功提交 | Freeze 窗口隐藏 |

- 共享设备丢失只记到 `UiDeviceRecovery`，客户端因设备丢失返回的结果不再重复计入客户端槽。
- 退避期间不清零，让计时延续到退避前的那次故障。
- 每个客户端的具体故障分支在实施 S4 时逐个列出，并在 Codex 审查时复核。

### 3.4 Draw3 意外停止

`IdtMain.cpp:4265–4290` 发现 `!offSignal && !ProductRunning()` 时，现在会 `SetOffSignal(1)`，然后隐藏画布、退出码 6、`break`。

改为：直接 `Fatal::RaiseNow(Draw3Stopped)`。**不能先 `SetOffSignal(1)`**，否则 Close 意图先占住意图槽，UEF 就抢不到 `CrashRestart`。这里是主线程，位于所有 C++ `catch` 之外，现有真实 SEH 测试也是在这类位置执行的。

**明确接受跳过的受控清理**（Codex 审查核实）：
- 画布成对隐藏、owner 释放鼠标捕获、可见性读回、退出码 6；
- `StopProduct()` 里的触控板/鼠标停产、`stylus.Shutdown()`；
- 保存排空和正常日志落盘。

理由：进程随后会被结束，系统会回收窗口、捕获和 RTS；单实例契约由 `CrashRestart` 的 `ParentExit` 交接保证（等旧进程死亡后才拉起）。墨迹不依赖这里的保存排空，而是依赖 C1.6b 在每次抬笔后已经持久化的数据，**恢复承诺是“恢复已持久化的数据”**。提示档等待用户期间，画布可能仍然可见，这一点可以接受。

Draw3 停止前的设备恢复失败、`Run()` 异常、内存不足重抛，最后都会让 Host 置 `running=false`，因此都汇入这一处。Host 清理本身卡住时，Draw3 槽仍处于忙碌（`Run()` 调用后 Host 清理段前 `Beat` 一次，`Draw3.Host.cpp:2940` 之后），由看门狗兜底。

### 3.5 Windows 回调补漏（`InstallCrtHandlers`）

在 `CrashHandler::Initialize()`（`Helper.CrashHandler.cpp:107–115`）中调用：

- `signal(SIGABRT, handler)`，handler 调 `RaiseNow(CrtAbort)`。MSVC 的 `std::terminate` 默认调用 `abort()`，所以这一项也覆盖 worker 线程未捕获异常和 `noexcept` 违约（`set_terminate` 按线程生效，不用它）。
- `_set_invalid_parameter_handler` 改为调 `RaiseNow(CrtInvalidParameter)`；默认行为是 `__fastfail`，会绕过 UEF。
- `_set_purecall_handler` 改为调 `RaiseNow(CrtPureCall)`。
- `_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT)`：handler 万一返回，也不弹 Debug 对话框、不走 fastfail。

安装与还原：
- 只在**首次成功安装**时保存各项旧值；`signal` 返回 `SIG_ERR` 时记录部分安装，只还原已装上的项。
- `CrashHandler::Shutdown()` **先还原 CRT 回调，再恢复旧 UEF**。
- 回调入口同样检查意图槽：已在正常退出中时直接结束进程，不再进入重启。

不覆盖（用户已确认接受）：`__fastfail`、栈溢出、堆损坏、被强制结束、C# 托管层逃逸的异常。被强制结束和断电的墨迹由 C1.6b 的“非正常结束后恢复”兜底。

### 3.6 重启循环与双实例

完全沿用现有机制：5 分钟内再次进入 UEF 直接结束；意图槽单次竞争；监督进程确认旧进程死亡后才拉起。看门狗触发的 `RaiseNow` 走同一条路。

### 3.7 已知失败的强退链（`FailedCleanupDeadline`）

现状：已知失败后清理超时时，`PublishFatalFailedCleanupNoWait()`（`IdtMain.cpp:408–421`）抢 Close 意图，随后 `TerminateProcess`（`FailedCleanupDeadline.cpp:60–121`），不会重启。`Draw3.Host.cpp` 中的 `BeginKnownFailure` 调用点绝大多数在启动初始化阶段（附着窗口、图形初始化、首帧、绘制线程创建），只有 `StopPointerInput` 失败（`:3053`）可能发生在运行期停止时。

改为：publisher 判断 `Fatal::RuntimeArmed()` 为真（启动已完成）且意图槽为 0 时：
- 档位 0 或 1 → 抢 `CrashRestart` 意图并 `ArmShutdownSupervisor(Intent::CrashRestart)`。这条链不能等待用户，所以档位 0 在这里退化为静默重启。
- 档位 2 → 保持现状，抢 Close。

启动期的已知失败和已受理的用户退出保持原有意图。原有的强退截止和“publisher 不等待”约束不变。

### 3.8 退出时等待存储 worker 无超时（父任务待办）

正常退出已由 `SetOffSignal` 建立 15 秒监督强退（`IdtMain.cpp:358`），磁盘卡住最多 15 秒，不会永久卡死，不新增机制。被强退的进程不会写入 C1.6b 会话记录里的“正常结束”标记，下次启动走“非正常结束后恢复”。

## 4. 不做的事

- 不加常驻守护进程（用户确认）。
- 不处理启动期的无超时等待（窗口 future、Draw3 / Setting 初始化条件变量），启动错误仍走现有提示。
- 不改白板工作区切换失败的预算与局部失败（可恢复的降级）。
- 不改 PptCOM（C#）。

## 5. 测试

- **无窗口单元测试**（`InkeysHeadlessTests`）：看门狗判定抽成纯函数（输入时间与各槽状态，输出是否触发、原因），覆盖：
  - 最后一次 Beat 后 14.9 s 不触发、15 s 触发；空闲任意长不触发；持续 Beat 不触发；
  - 失败 9.9 s 后成功一次清零；ClearFailure 清零；持续失败 10 s 触发；
  - 意图槽非 0、UEF 处理中、调试器附加时不触发。
- **配置**：`TeachingSafetyMode` 读取 0/1/2 原样保留，3、99、-1、非整数回 0；`CrashRecoverInk` 缺省为 true。
- **隔离子进程测试**（扩展 `ShutdownSupervisor.cpp` 现有的 `crash-*` 场景与 `SuppressTestWer` 机制）：Debug 与 Release ARM64、不挂调试器，分别验证看门狗线程 `RaiseNow`、主线程 `RaiseNow`、`SIGABRT`、worker 线程 `std::terminate`、`noexcept` 违约、invalid parameter、purecall。记录 UEF 是否命中、监督是否建立、实际拉起次数。
- **CRT 生命周期测试**：在现有 UEF 生命周期测试基础上，加重复初始化/关闭、部分安装的还原顺序。
- **实机**（借助 C1.6c 三个按钮）：三档 × {野指针、模拟 Draw3 致命错误、模拟 UI3 卡死}；5 分钟内二次触发不再拉起；正常关闭、重启、更新重启不受影响；旧进程结束后新进程的触控和笔输入正常。
- **实测最坏耗时**：低端设备（Win7 / 兆芯留到最终集成）上设备重建、重页物化与重放、保存排空的耗时，确认远低于 15 s。
- 已有测试：`Bar.Presentation.Test.cpp:628` 涉及 `TeachingSafetyMode`；`manual-uef-cancel-hold` 只证明等待超过 15 秒，不能替代 `Failed` 结果的验收。

## 6. 风险

- 自定义 SEH 的可达性依赖编译设置，以隔离子进程测试为准（§1、§5）。
- 合法长任务超过 15 秒会被误判为卡死：用实测最坏耗时确认；必要时只调高 Draw3 槽的阈值（需用户同意）。
- 漏掉某个客户端的真实故障分支：S4 逐个列出，并由 Codex 复核。

## 7. Codex 设计审查处理记录（2026-11-26）

| # | 审查意见 | 处理 |
|---|---|---|
| 1 阻断 | `/EHsc` 不能证明一定到达 UEF | §1 改为“预期”，加入逐入口的隔离子进程实测；收不到时改为直接调用 UEF 处理核心 |
| 2 应改 | Draw3 改走 UEF 会跳过受控清理 | §3.4 列出被跳过的清理并说明接受理由；恢复承诺改为“已持久化的数据” |
| 3 阻断 | 忙碌时长不等于无进展 | §3.2 改为每个工作单元 Beat；补全 Draw3 的等待点；删掉“冷页约 0.3 s”的上限说法 |
| 4 阻断 | `Retry` 混合了延期、退避和故障 | §3.3 改为在客户端的真实故障点记录，增加 ClearFailure，共享设备丢失单独计 |
| 5 阻断 | 提示档的降级晚于可阻塞步骤 | §2 只把 `Failed` 新增为重启；保留弹窗后的意图 CAS；报告超时直接结束写为已知限制 |
| 6 应改 | 意图槽语义与启动早退 | §3.1 启动门禁完成后才启用；用 CAS 读取；UEF 处理中抑制重复触发 |
| 7 应改 | CRT 生命周期缺覆盖 | §3.5 定义首次安装、部分安装和还原顺序；§5 加 CRT 隔离测试与生命周期测试 |
| 8 阻断 | 实施表漏掉已知失败强退 | 新增 §3.7 与对应实施步骤 |

## 8. 实施中的调整（2026-11-26）

| 项 | 调整 | 理由 |
|---|---|---|
| 触发方式 | `RaiseNow` 不再 `RaiseException`，而是合成异常记录（代码 `0xE1430100`、原因与 detail 两个参数）直接调用已注册的 UEF 处理核心，返回后 `TerminateProcess`。处理核心未注册（未安装或正常退出已撤销）时直接结束进程。另一线程已在处理时本线程挂起，不提前结束进程 | 即 §1 预留的“直接调用 UEF 处理核心”路径。SEH 穿过 `noexcept` 帧可能转去 `terminate`，直接调用对所有入口一致；8 个入口的隔离子进程实测全部命中 |
| 时钟 | 卡死与失败计时用 `QueryUnbiasedInterruptTime` | 不含系统睡眠时长，避免唤醒后把睡眠误判为卡死 |
| 持续失败 | 增加 5 秒陈旧窗口：最近一次失败须在 5 秒内才触发；距上次失败超过 5 秒再失败时重新计时 | 客户端隐藏、空闲或停止尝试后旧计时不再继续走；不必在每个隐藏/注销路径都补 `ClearFailure` |
| Draw3 意外停止 | 只在看门狗已启动（启动门禁完成）后改走 `RaiseNow`；启动期仍走原受控退出 | 与“启动期不受监督”一致，避免启动失败进入重启循环 |
| 已知失败强退链 | 额外遵守“崩溃拉起后 5 分钟内不再拉起”（新增 `CrashHandler::CrashRestartSuppressed()`，UEF 共用） | 与 UEF 行为一致 |
| UiStartupPreview | 不接入 | 只在启动期运行，启动期不受看门狗监督 |
| 头文件 | `Helper.Fatal.h` 不 include `<Windows.h>`（前向声明 `_EXCEPTION_POINTERS`） | 被许多不同 `NOMINMAX` 状态的 TU 引入，不能提前定义 min/max 宏 |

## 9. Codex 实施审查处理记录（2026-11-26，原文见 `research/codex-impl-review-20261126.md`）

| # | 意见 | 处理 |
|---|---|---|
| B1 | 已知失败强退 publisher 同步 Arm，违反“不等待”约定 | publisher 只 `Fatal::Report(KnownFailureCleanup)` 后返回，不抢意图、不 Arm、不建线程；看门狗线程走 UEF 抢 `CrashRestart` 并建立监督，UEF 遇此原因把提示档按静默重启处理；原强退截止照常执行 |
| B2 | UEF 接管与 `RaiseNow` 的竞争窗口 | 新增 `Fatal::ClaimCrashHandling()`（线程 ID 原子接管，UEF 与 `RaiseNow` 共用）：其他线程挂起由首个处理者结束进程，同线程递归放弃 |
| B3 | 提示框可交互前可能无限卡住 | `MessageBox::Request` 新增 `interactiveEvent`（首帧揭示或进入系统回退框时置位）；提示档另设 15 秒建窗截止线程，超时按“提示失败”抢意图、建立监督后结束进程（退出码 `0xE1430101`） |
| B4 | Draw3 批量消费与空闲分支内命令的监督粒度 | `processCommandAndReconcile` 入口 Beat（每条命令一个工作单元） |
| B5 | 首末失败时间分开取样可错配；触发前未复验 | 取样前后各读一次首次时间，不一致跳过；看门狗触发前重新取样复验同一原因 |
| S | CRT 入口先于处理核心发布；退出已受理窗口 | 先注册处理核心再装 CRT；意图槽为 Close/Restart 时 `RaiseNow` 挂起，交给退出 owner 的 15 秒监督 |
| S | `Draw3Stopped` 前阻塞日志 | 改 `Log::TryFormat` |
| S | Settings 隐藏期间累计、`DXGI_STATUS_OCCLUDED` 清槽 | 只在窗口可见时累计，隐藏时清槽；只有 `S_OK` 清成功 |
| S | Draw3 帧外 ULW 失败漏记；只累计时误清 | `CompleteUlwBeforeIdle`/`ServiceUlwBeforeFrameWait`/`BeginUlwFrame` 失败计入；成功以 `CommittedPresentCount()` 推进为证据 |
| S | 白板/Freeze 混合换代与 ULW 失败 | `SubmitFreezeSurface` 新增 `apiFailed` 输出，两侧按同一次结果计数；Freeze 只在退休或交给白板后清槽 |
| S | Bar/Selection 设备丢失误归客户端 | Bar 用 `HasSharedDeviceLoss()`；Selection 先排除 DXGI 设备丢失 |
| S | PageControl 无效几何计失败、四表面共槽 | 只计明确 API 失败；新增 `UiPageControl2..4`，每个表面独立计时，隐藏时清除 |
| S | `Report` 原因与 detail 可错配；时钟回退基准不同 | 单个 64 位原子成对发布；去掉 `GetTickCount64` 回退 |
| U | 并发 `SIGABRT` 时处理器已复位 | 处理器入口立即重装 `SIGABRT` |
