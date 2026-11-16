# Research: Inkeys3 非 logger 控制台/诊断输出调用点完整清单

- **Query**: 盘点 `Inkeys/`（排除 `Inkeys/additional/`）C++ 与 `PptCOM/*.cs` 中所有非 logger 的控制台/诊断输出调用点，逐点记录 API、门控、语义级别、建议模块标签，并给出分类计数；同时列出现有 `IDTLogger` / `UI3Diagnostics` 调用文件及其手写前缀。
- **Scope**: internal
- **Date**: 2026-11-16
- **基准**: 分支 `refactoring/drawpad-ui`，HEAD `c06697b1`（工作区仅有未跟踪的 `.trellis/tasks/` 目录，产品代码无改动）

---

## 0. 统计口径

- 扫描模式（C++，`*.cpp/*.h/*.cppm/*.inl/*.hpp`）：
  `\b(printf|wprintf|fprintf|fwprintf|puts|fputs|fputws|_putws|vprintf|vfprintf|vfwprintf)\s*\(`、`std::(cout|wcout|cerr|wcerr|clog|print|println)`、`fmt::print`、`OutputDebugString*`、`DiagnosticOutput::(Printf|Text|Write|Line)`、`WriteConsole*`、`GetStdHandle`。
  - `snprintf/swprintf/sprintf_s` 只是格式化，**不计**。
  - `DiagnosticOutput::Line` 是 `Draw3.DiagnosticOutput.h` 中仿 `cout` 的定长拼接类（析构时入队），**任务原文未列出但属于同一输出通道，已计入**（111 处）。
- 计数单位是“源码行”：一条跨多行的语句只在出现 API 名的那一行计 1 次；`std::fputs(line, stdout); OutputDebugStringA(line);` 同一行计 1 次。
- C++ 合计 **658 行**。按 API：`fprintf` 281、`DiagnosticOutput::Line` 111、`fputs` 91、`DiagnosticOutput::Printf` 44、`DiagnosticOutput::Text` 38、`OutputDebugStringW` 27、`GetStdHandle` 14、`DiagnosticOutput::Write` 13、`printf` 13、`OutputDebugStringA` 7、`fwprintf` 5、`std::cout` 5、`std::cerr` 4、`puts` 3、`std::wcout` 2、`std::wcerr` 1（`std::wcout/cerr` 行大多是 `InitializeDebugConsole` 里的 `.clear()`）。
- `cppm` 模块接口文件中**没有**任何输出调用（唯一命中是 `Other.Config.cppm:574` 的 `GROUP(ConsoleOutput, ...)` 配置定义）。
- 另有大量“包装函数”的间接调用点（如 `HapticDiagnostics::Log`、`LogCanvasPan`、`RecordCursorDiagnostic`、`LogHResult`），它们本身不含上述 API 名，单独在第 4 节列出，不计入 658。

### 分类定义

| 桶 | 含义 |
|---|---|
| **A** | 受 6 个 `config.Experimental.Inkeys3.ConsoleOutput.*` 开关之一控制（TouchArea / PptCOM / Draw3 / Haptics / Cursor / CanvasPan） |
| **B** | 受环境变量或编译宏/编译期常量控制（不受设置页开关控制） |
| **C** | 无显式开关的产品诊断（注意：DiagnosticOutput 通道仍受“有无 stdout/stderr/调试器”这一隐式条件约束，见 2.3） |
| **D** | 测试工具/自检的 stdout/stderr 协议输出（PASS/FAIL/指标行），需保持不变 |
| **E** | CrashHandler / ShutdownSupervisor 应急路径，不能依赖日志服务 |
| **I** | 输出基础设施本身（控制台分配、DiagnosticOutput 写端、自检 stderr 句柄补齐），不是业务调用点 |

---

## 1. 汇总表

### 1.1 C++（658 行）

| 桶 | 行数 | 细分 |
|---|---:|---|
| A | 35 | Draw3 7、Cursor 1、CanvasPan 2、Haptics 12、TouchArea 13（PptCOM 开关在 C++ 侧没有直接输出调用点，只通过 `SetConsoleOutputEnabled` 传给 C#） |
| B | 22 | `DRAW3_RTS_DIAGNOSTICS` 9、`_DEBUG` 1、编译期常量 1、`INKEYS_DRAW3_WIN7_DIAGNOSTICS` 2、`INKEYS_DRAW3_TAIL_TRACE` 1、`INKEYS_DRAW3_ULW_FULL_UPDATE` 1、`INKEYS_PPT_TIMING` 1、`INKEYS_PPT_PERF` 2、`INKEYS_PAGECONTROL_PRESENT_TRACE` 3、`INKEYS_WHITEBOARD_PATTERN_PROFILE` 1 |
| C | 162 | 其中 DiagnosticOutput 通道 152 行；OutputDebugString 或 stdout/stderr 直写 10 行（IdtConfiguration:274、IdtMain:1426、InkHistoryGpu:163、Renderer:131、RendererSnapshot:29、PageControl:203/204、Ppt:97、Window:1721/1722） |
| D | 407 | 分布在 44 个文件（含大量写在产品源文件里的 `Run*Test/*Probe/*Benchmark/*Validation` 自检函数） |
| E | 21 | `Helper.CrashHandler.cpp` 20、`ShutdownSupervisor.cpp:683` 1 |
| I | 11 | `IdtMain.cpp` 328/329/350/352/353/355/356（`InitializeDebugConsole`）、1741（`runDraw3SelfTest` 补齐 stderr 句柄）；`Draw3.DiagnosticOutput.cpp` 128/146/301 |
| **合计** | **658** | |

> 注：其中 GraphicsInitialization:75 与 TransparentPresentation:1449 两处是“成功路径受 Draw3 开关，失败路径始终输出”的混合门控，计在 C。

### 1.2 C#（PptCOM）

| 桶 | 行数 | 说明 |
|---|---:|---|
| A（PptCOM 开关） | 11 | `Console.WriteLine`，在开关关闭时 Console 被 `SetOut(TextWriter.Null)` 吞掉 |
| A+B | 1 | `PptCOM.cs:195` `[PptOwner]`：需要 PptCOM 开关 **且** `INKEYS_PPTCOM_TRACE=1` |
| B | 1 | `PptPerfTrace.cs:126` 写 `Console.OpenStandardError()`，仅受 `INKEYS_PPT_PERF=1`，**不受**开关/`SetOut(Null)` 影响 |
| 死代码 | 11 | `PptCOM.cs:2010-2069` `DiagnoseComStatus()`，全仓无调用者 |
| I | 7 | `PptCOM.cs:244/245/251/252/258/259`（SetOut/SetError/OpenStandardOutput/Error）、`PptPerfTrace.cs:138` |
| `Trace.*` / `Debug.Write*` | 0 | 未发现 |

### 1.3 现有 logger

- `IDTLogger->level(...)` 日志调用 **114** 处 + 3 处配置调用（`set_level/set_pattern/flush_on`），分布在 **13** 个文件（见第 9 节）。PRD 中“约 150 处 / 15 个文件”与本次计数不同，本次按 `IDTLogger\s*->\s*\w+\s*\(` 精确匹配统计。
- `UI3Diagnostics`：1 个第二 logger（`IdtMain.cpp:3254-3264`），消息来源是 `RenderPipeline.cpp:121` 拼出的 `[UI3Diag] ...` 行。

---

## 2. 输出原语与基础设施

### 2.1 `InitializeDebugConsole()`（将被删除）

`Inkeys/IdtMain.cpp:321-358`（匿名命名空间）：`std::call_once` 内检查 stdout/stderr 是否被重定向（`GetFileType` 为 DISK/PIPE），若两路未全部重定向则 `AllocConsole()` + `freopen_s("CONOUT$")`，保留已重定向的句柄；随后 `sync_with_stdio(true)`、清各流状态、`std::wcout.imbue(std::locale("chs"))`。

调用点：
- `IdtMain.cpp:3392-3397`：`draw3 || touchArea || cursor || canvasPan || HapticDiagnostics::Enabled()` 任一为真时调用（在设备初始化前）。
- `IdtMain.cpp:4220-4227`：仅 PptCOM 开关开启（其他 4 个 Draw3 系开关均关）时延后调用，随后再次 `draw3DiagnosticOutput.Start()`。注意 4220 的条件里**没有**检查 Haptics，Haptics 已在 3392 覆盖。

### 2.2 开关读取与下发（`IdtMain.cpp:3370-3411`）

| 开关 | 读取 | 下发 |
|---|---|---|
| PptCOM | `pptComConsoleOutputEnabled = ConsoleOutput.PptCOM`（3370） | `IdtPlug-in.cpp:377-378` 在 `CreateInstance` 后立即 `pptCom->SetConsoleOutputEnabled(...)` |
| Draw3 | `draw3ConsoleOutputEnabled = ConsoleOutput.Draw3 \|\| win7DiagnosticsEnabled`（3381） | `SetStartupEnvironmentDiagnosticsEnabled(...)`（3383） |
| TouchArea | 3385 | `EraserDevelopmentOptions.touchAreaTrace = true`（3406-3411）→ `Host` 的 `touchAreaTraceEnabled` |
| Cursor | `ConsoleOutput.Cursor \|\| win7DiagnosticsEnabled`（3387） | `SetCursorDiagnosticsEnabled(...)`（3403） |
| CanvasPan | 3389 | `SetCanvasPanDiagnosticsEnabled(...)`（3405）；退出时 4358 置 false |
| Haptics | — | `HapticDiagnostics::SetEnabled(ConsoleOutput.Haptics)`（3390） |
| 环境变量 | `INKEYS_DRAW3_WIN7_DIAGNOSTICS=1`（仅 `#ifndef IDT_RELEASE`，3372-3380） | 同时打开 Draw3 + Cursor，并在 3399-3402 打印 `[Draw3Win7Diagnostics] enabled=1 draw3=1 cursor=1` |

### 2.3 `Draw3::DiagnosticOutput`（`Draw3.DiagnosticOutput.h/.cpp`）

- API：`Available(stream, mirror)`、`Write(stream, text, len, mirror)`、`Text(stream, text, mirror)`、`Printf(stream, fmt, ...)`、`Wake()`、`Line(stream=Output, mirror=false)`（`operator<<` 定长拼接，析构时 `Write`）、`Session{Start,Stop,Snapshot}`、`RunProductionTest`。
- `RecordCapacity=1024`，`TextCapacity=4096`；生产者 try-lock 入队，满/竞争/关闭时丢弃并计数（`droppedFull/droppedContention/droppedClosing/truncated`）；单后台 worker 写出。
- 写端：`WriteRecord`（`.cpp:128` 控制台句柄走 `WriteConsoleA`，否则 `WriteFile`；`.cpp:146` `mirror=true` 时额外 `OutputDebugStringA`）。
- `Session::Start`（`.cpp:301`）取 `GetStdHandle(STD_OUTPUT/ERROR_HANDLE)`；`available` 掩码 = stdout 有效(1) | stderr 有效(2) | `IsDebuggerPresent()`(4)（`.cpp:319`）。
- **隐式门控**：`Write/Text/Printf/Line` 都先判 `Available()`；产品进程是 GUI 子系统，无控制台/无重定向/无调试器时全部直接丢弃。因此“C 桶”中的 DiagnosticOutput 调用，在当前产品里**实际只在任一控制台开关开启（触发 AllocConsole）、外部重定向 stdout/stderr、或附加调试器（仅 mirror 行）时可见**。迁移到日志文件后若不另设门控，这些行会变成“始终写入”。
- 生命周期：`IdtMain.cpp:2158` 定义 `Session draw3DiagnosticOutput`；3398 `Start()`（无论开关是否开启都会调用）；4226 仅 PptCOM 时再 `Start()`；4359 `Stop()`（位于最后几条 `IDTLogger->info` 之前）。
- **`LastErrorGuard`**：`Write/Printf/Session` 内保存并恢复 `GetLastError()`（`.cpp:23-27, 234, 278, 291, 300, 305, 373`）；`Line` 构造时保存、析构后 `SetLastError`（`.cpp:437-446`）。`RunProductionTest` 还专门断言 `"printf preserves LastError"`（`.cpp:541-543`）。
- 自检复用：`IdtMain.cpp:1739-1752` 的 `runDraw3SelfTest` 会为每个 Draw3 自检启动一个 `DiagnosticOutput::Session`，所以**自检运行期间的产品 DiagnosticOutput 行也会进 stderr/stdout**（例如 `[Draw3.AutoSave.TestIo]`、`[Draw3.Presentation.TestIo]` 就是靠这个通道输出给测试的，见 7.2）。

### 2.4 PptCOM 托管侧开关（`PptCOM/PptCOM.cs:238-260`）

`SetConsoleOutputEnabled(bool)`：`PptComDiagnostics.SetEnabled(enabled)`；关闭时 `Console.SetOut/SetError(TextWriter.Null)`；开启时 `Console.SetOut(new StreamWriter(Console.OpenStandardOutput()){AutoFlush=true})`、`SetError(...OpenStandardError())`，并写一条 `PptComDiagnostics.Event("enabled", "module=...")`。
注意：在 `CreateInstance` 与 `SetConsoleOutputEnabled` 之间以及该调用之前，Console 为 .NET 默认状态（`IdtPlug-in.cpp:376` 注释称“在任何 PptCOM 诊断输出前应用独立开关”）。

---

## 3. 门控来源总表（环境变量 / 编译宏）

| 门控 | 读取位置 | 控制的输出 | 输出通道 |
|---|---|---|---|
| `INKEYS_DRAW3_WIN7_DIAGNOSTICS=1` | `IdtMain.cpp:3376`（`#ifndef IDT_RELEASE`）；`Draw3.Renderer.cpp:784`（**无** `IDT_RELEASE` 保护） | 打开 Draw3+Cursor 诊断；`IdtMain.cpp:3400` banner；`Renderer.cpp:123` `[Draw3ShaderSelection]` | DiagnosticOutput |
| `INKEYS_DRAW3_TAIL_TRACE=1` | `Draw3.StrokeGeometry.cpp:40` | `WriteTailDebugRecord`（`StrokeGeometry.cpp:141` ODS + 写 `%TEMP%\Inkeys-Draw3-tail-<pid>.log`，最多 128 条/2 MiB） | ODS + 独立文件 |
| `INKEYS_DRAW3_ULW_FULL_UPDATE=1` | `Draw3.TransparentPresentation.cpp:487` | `TransparentPresentation.cpp:490` `[Draw3Diag][ulw-control]` | DiagnosticOutput |
| `INKEYS_PPT_TIMING=1` | `IdtPlug-in.cpp:160`（`TracePptNavigation`）、`IdtPlug-in.cpp:1282-1287`（`tracePptGate`）、`Draw3.PptTiming.h:16` | `[PptNav]`（IDTLogger）、`[PptGate]`（IDTLogger）、`[PptSync]`（DiagnosticOutput Error+mirror） | 见 6 |
| `INKEYS_PPT_PERF=1` | `PptNavigationDiagnostics.h:49`（C++ `PptPerf::Enabled`）、`PptCOM/PptPerfTrace.cs:40` | `[PptPerf] source=native`（经 IDTLogger）、`[PptPerf] source=managed`（C# 直写 stderr）、`[PptWait]`（`PptWaitingDiagnostics.h:129` 退出时 raw stderr）、`[ShutdownTrace]`（`ShutdownProgressTrace.h:49` raw stderr） | 见 6 |
| `INKEYS_PPT_EXIT_TRACE=1` | `IdtPlug-in.cpp:984`、`IdtState.cpp:947` | `[PptExitSurface]`（IDTLogger info） | IDTLogger |
| `INKEYS_PPTCOM_TRACE=1` | `PptCOM.cs:188` | `[PptOwner]`（C# Console，且需 PptCOM 开关） | Console |
| `INKEYS_PAGECONTROL_PRESENT_TRACE=1` | `PageControl.cpp:2160` | `TraceSurfaceStage`（`PageControl.cpp:2224/2225/2236`，stdout + ODS） | stdout+ODS |
| `INKEYS_WHITEBOARD_PATTERN_PROFILE=1` | `Whiteboard.cpp:109` | `[WhiteboardPatternPerf]`（`Whiteboard.cpp:128`） | DiagnosticOutput |
| `INKEYS_SETTING_ENTRY_PROFILE=1` | `Setting.cpp:245` | `[SettingEntryPerf]`（`Setting.cpp:296/306`，IDTLogger info） | IDTLogger |
| `INKEYS_POINTER_PROBE=1` | `UiPointerDiagnostics.h:144` | 探针模式下额外的 `[PenHaptics] event=ui_input stage=probe_*` 行，且仍需 Haptics 开关 | DiagnosticOutput |
| `INKEYS_STARTUP_PREVIEW_RETRY_FAILURE` + `..._MANUAL_DELAY` | `IdtMain.cpp:247-249`（仅 `INKEYS_ENABLE_UI3_PRESENTATION_HARNESS`） | `IdtMain.cpp:255/267/276` 人工测试 warn/critical（IDTLogger） | IDTLogger |
| `#if defined(DRAW3_RTS_DIAGNOSTICS)` | 工程未定义（`Inkeys.vcxproj` 无此宏） | `[RTS_TRACE]`、`[CURSOR_TRACE][app/touch-mouse/system/...]` 等 | DiagnosticOutput |
| `#if defined(_DEBUG)` | `Draw3.Diagnostics.cpp:789` | `LogFrameTiming`（每 250ms 一行帧计时） | DiagnosticOutput |
| `kInterruptedStrokeReconnectSimulationEnabled = false` | `Draw3.InkPrediction.cppm:83` | `RealtimeStylus.cpp:259` `[StrokeReconnectSimulation]` | DiagnosticOutput |
| `INKEYS_ENABLE_UI3_PRESENTATION_HARNESS` | 多处 `#if` | 绝大部分 D 桶自检入口（关闭时返回 2） | — |

其他读取的 `INKEYS_*` 环境变量（`INKEYS_UI_TOUCH_LEGACY`、`INKEYS_DRAW3_WIN7_GUARDED_SHADER`、`INKEYS_TEST_WHITEBOARD_REPLACEMENT`、`INKEYS_UI3_*` 基准开关等）只影响行为或测试参数，本身不产生输出，未列入。

---

## 4. 包装函数与间接调用点

下表中“包装体行”已计入 658；“调用点”是包装函数的调用位置（未计入 658），迁移时需要整体考虑。

| 包装函数（定义） | 包装体输出行 | 门控 | 调用点 |
|---|---|---|---|
| `RecordCursorDiagnostic(fmt,...)`（`Draw3.Diagnostics.cpp:719`） | 738（`[CURSOR_TRACE] seq= tick= thread= ...`） | **Cursor**（`CursorDiagnosticsEnabled() && Available()`） | `Diagnostics.cpp` 663/669/679/682/700/707/709/714；`DrawingController.cpp` 18592/18836/18840/18858/18868/18878/23388；`RealtimeStylus.cpp` 454/1679/1725；`WindowControl.cpp` 105/167/661/717/1446/1622/1647/1789/1827/1842/1861/1891/1915/1924/1944/2100/2150/2505/2549/2687/2872/2910/3036/3071；`IdtMain.cpp:1833`（自检） |
| `RecordCursorRawInput(LPARAM)`（`Diagnostics.cpp:692`） | 经 RecordCursorDiagnostic | Cursor | `Bar.Interaction.cpp:3386`（WndProc WM_INPUT） |
| `LogCanvasPan(fmt,...)`（`Diagnostics.cpp:837`） | 861（`[CanvasPan] seq= qpc= tid= ...`，mirror=true） | **CanvasPan**（`canvasPanDiagnosticsEnabled && Available(Output, mirror=true)`，即调试器附加时也可用） | `DrawingController.cpp` 3320/3474/3479/3492/3557/3582/3649/3688/3763/3826/3844/16332/16798/16831/16843/18433/18441/18452/18462/18474/18483/18503/21162/23437；`RealtimeStylus.cpp` 959/964/1410/1528；`WindowControl.cpp` 384/410/416/454/530/540/2357；`Diagnostics.cpp:824`；`IdtMain.cpp` 1828/1829/1831（自检） |
| `SetCanvasPanDiagnosticsEnabled(false)` | 827 `[CanvasPan] disabled` | CanvasPan（状态切换时） | `IdtMain.cpp:3405, 4358` |
| `HapticDiagnostics::Log(event,fmt,...)`（`Draw3.HapticDiagnostics.h:62`） | 71（`[PenHaptics] event=...`，Error 流）；`ProductMessageOutputScope` 析构 38/44 | **Haptics** | `DrawingController.cpp` 16976/16982/16991/17956/17962/21807/21815/21834/21876/21880/21887；`HapticFeedback.cpp` 193/211/228/234/383/442/453/455/457/474/478/483/503/522/562/646/760/765/770/787；`Host.cpp:2167`；`Product.cpp` 118/129/135；`WindowControl.cpp` 2445/2499/2674；`Bar.Interaction.cpp` 355/382/384/648/3261/3276 |
| `UiPointer::Diagnostics::{Log,Raw,WindowSnapshot,TouchRegistration,PointerDownSnapshot,LeaseEvent,Probe::*}`（`Input/UiPointerDiagnostics.h`） | 73（`Write` → DiagnosticOutput Error） | **Haptics**（`Enabled()` = `HapticDiagnostics::Enabled()`）；探针另需 `INKEYS_POINTER_PROBE` | `UiPointerInput.cpp` 60/65/78/530/565/587/624/657/868/882/1006/1059/1087/1093/1099/1109/1124/1130/1133/1135；`IdtMain.cpp:3782`；经别名 `UiDiag::` 在 `Bar.Interaction.cpp`（643/670/1201/1260/1367/1432/1434/1479/1493/1509/1516/1641/2030/3220/3225/3228/3285/3313/3330/3511/6459/6490/10177/10180）与 `PageControl.cpp`（921/946/2867/2874/2875/2965/2971/2977/2980/2991/3054/3089/3100/3140/3245/3272/3336/3340/3353/3364/3401/3407/3490/3492/3494/3498/3505/3509/3534/3599/3625/3627） |
| `LogHResult(step, hr)`（`Diagnostics.cpp:866`） | 868 | 无（C） | `DrawingController.cpp:472`；`GraphicsInitialization.cpp` 149/157/167；`RealtimeStylus.cpp` 2770/2789/2804/2817/2842/2858/2888/2893/2914/2925/2943/2959/2994/2995；`TransparentPresentation.cpp` 740/775/977/988/997/1031；`Diagnostics.cpp` 937/961/985/998；`IdtMain.cpp:1836`（自检） |
| `LogWin32Error(step, err)`（`Diagnostics.cpp:872`） | 874 | 无（C） | `WindowControl.cpp` 660/716；`IdtMain.cpp:1837`（自检） |
| `LogDwmModeDiagnostics / LogDwmColorizationState / LogSwapChainDescription / LogSwapChainRuntimeDescription`（`Diagnostics.cpp:895-965`） | 902/907/921/932/944 | 无（C） | `TransparentPresentation.cpp` 748/783/947/948/956/960/1007/1037/1119/1383/1386 |
| `LogAdapterDiagnostics`（`Diagnostics.cpp:967`） | 974/992 | **Draw3**（调用点 `GraphicsInitialization.cpp:160-161` 受 `StartupEnvironmentDiagnosticsEnabled()`） | `GraphicsInitialization.cpp:161` |
| `LogFrameTiming`（`Diagnostics.cpp:786`）→ `WriteFastConsoleLine` | 43 | `_DEBUG` + `Available()`，250ms 节流 | `DrawingController.cpp:23522`；`IdtMain.cpp:1835`（自检） |
| `LogRtsInitializationState / RecordRtsCallback / FlushRtsCallbackTrace` → `WriteRtsTraceLine` | 136 | `DRAW3_RTS_DIAGNOSTICS` + 运行时 `rtsTraceEnabled` | `RealtimeStylus.cpp` 2752/1669/3015 |
| `TracePptTiming(stage,...)`（`Draw3.PptTiming.h:10`） | 32（`[PptSync]`，Error+mirror） | `INKEYS_PPT_TIMING` | `IdtPlug-in.cpp` 1358/1359/1360；`DrawingController.cpp` 18252/20795；`Host.cpp` 940/1293/3440；`PageControl.cpp:2254`；`IdtMain.cpp:1839`（自检） |
| `LogDraw3BuildIdentity / LogDraw3WindowState`（`Host.cpp:131/140`） | 135/153 | 无（C） | `Host.cpp` 1079/2093/2094/2134/2350 |
| `LogPresentEvent`（`TransparentPresentation.cpp:281`） | 289 | 无（C；只在失败/恢复/首次成功时） | `TransparentPresentation.cpp` 514/557/638 |
| `TraceDrag(fmt,...)`（`PageControl.cpp:192`） | 203（stdout）、204（ODS） | **无门控（C）** | `PageControl.cpp` 1792/1803/1816/1916/1993/2015/2036/2058/2087/2906/3020/3145/3208 |
| `TraceSurfaceStage`（`PageControl.cpp:2166`） | 2224/2225/2236 | `INKEYS_PAGECONTROL_PRESENT_TRACE` + 2s 去重 | `PageControl.cpp` 2383/2422/2605/2642/2651/2665/2755 |
| `WriteWindowDiagnostic(text, mirror)`（`Window.cpp:111`） | 118 | 无（C） | `Window.cpp` 151/170/1143/1690 |
| `PptPerf::Recorder`（`PptNavigationDiagnostics.h`）+ `WritePptPerfEvent`（`IdtPlug-in.cpp:80-86`） | 经 `IDTLogger->info("{}")`（不计入 658） | `INKEYS_PPT_PERF` | `IdtPlug-in.cpp` `PptPerfRecorder()` 26 处：87/174/191/204/217/218/222/223/567/573/580/592/598/605/689/736/743/757/1558/1560/1561/1565/1590/1602/1689/1690 |
| `ShutdownTrace::*`（`ShutdownProgressTrace.h`）→ `WriteRaw` | 49 | `INKEYS_PPT_PERF` | `IdtMain.cpp` 375-429（`SetOffSignal`）、2154（WarmUp）、4334-4395（主线程退出序列）；`IdtPlug-in.cpp` 510/517/535/548/1668/1670/1675/1676/1680/1698 |
| `PptWaitTrace::*`（`PptWaitingDiagnostics.h`）→ `RawOutput` | 129 | `INKEYS_PPT_PERF`（`Prepare()` 时决定是否分配 16384 槽） | `IdtMain.cpp` 2155（Prepare）/4355（Flush）；`Bar.Scene.cpp` 359/360/374/405/526；`PageControl.cpp`、`Ppt.cpp`（Capture/Scope） |

---

## 5. 逐文件清单（A / B / C / E / I）

列说明：**流** out=stdout / err=stderr / dbg=OutputDebugString；**级别** 为按文本语义建议的级别（trace/debug/info/warn/error）；**模块** 为建议模块标签。

### 5.1 `Inkeys/IdtMain.cpp`

| 行 | 函数 | API | 流 | 桶/门控 | 级别 | 模块 | 说明 |
|---|---|---|---|---|---|---|---|
| 328-329, 350, 352, 353, 355, 356 | `InitializeDebugConsole`（call_once lambda） | GetStdHandle / `std::(w)cout/(w)cerr.clear()` / imbue | — | I | — | Main | 控制台分配与流复位，R6 要求删除 |
| 1426 | `PrepareDraw3WindowTopmost` | `fprintf(stderr,"%s\n")` | err | C（仅 `IDTLogger` 为空时的回退） | error/info | Window | `[Draw3Topmost] phase=... result=...`；正常路径走 1423/1424 的 `IDTLogger->error/info("{}")` |
| 1741 | `wWinMain` → `runDraw3SelfTest` | GetStdHandle | err | I | — | Main | 自检进程补齐 CRT stderr 句柄 |
| 3400 | `wWinMain` | `DiagnosticOutput::Printf(Error)` | err | B：`INKEYS_DRAW3_WIN7_DIAGNOSTICS`（非 Release） | info | Draw3 | `[Draw3Win7Diagnostics] enabled=1 draw3=1 cursor=1` |

### 5.2 `Inkeys/IdtConfiguration.cpp`

| 行 | 函数 | API | 流 | 桶 | 级别 | 模块 | 说明 |
|---|---|---|---|---|---|---|---|
| 274 | `WritePptComSettingJson` | `OutputDebugStringW` | dbg | C | warn | Config（或 Ppt） | `[Ppt] settings commit failed; frozen request retained for retry` |

### 5.3 `Inkeys/Inkeys/Drawing/Draw3/Draw3.DiagnosticOutput.cpp`

| 行 | 函数 | API | 桶 | 说明 |
|---|---|---|---|---|
| 128 | `WriteRecord` | `WriteConsoleA` / `WriteFile` | I | 唯一写出 worker |
| 146 | `WriteRecord` | `OutputDebugStringA` | I | `mirror=true` 镜像 |
| 301 | `Session::Start` | `GetStdHandle` ×2 | I | 决定 `available` 掩码 |

### 5.4 `Inkeys/Inkeys/Drawing/Draw3/Draw3.Diagnostics.cpp`

| 行 | 函数 | API | 流 | 桶/门控 | 级别 | 模块 |
|---|---|---|---|---|---|---|
| 43 | `WriteFastConsoleLine`（← `LogFrameTiming`） | DiagOut::Write | out | B：`_DEBUG` | trace | Draw3 |
| 136 | `WriteRtsTraceLine` | DiagOut::Write | out | B：`DRAW3_RTS_DIAGNOSTICS` | trace | Input/RTS |
| 738 | `RecordCursorDiagnostic` | DiagOut::Printf | out | A：Cursor | trace | Input（Cursor） |
| 827 | `SetCanvasPanDiagnosticsEnabled` | DiagOut::Text（mirror） | out | A：CanvasPan | debug | Input（CanvasPan） |
| 861 | `LogCanvasPan` | DiagOut::Write（mirror） | out | A：CanvasPan | trace | Input（CanvasPan） |
| 868 | `LogHResult` | DiagOut::Line | out | C | warn/error（视调用点） | Draw3 / Input/RTS |
| 874 | `LogWin32Error` | DiagOut::Line | out | C | warn/error | Draw3 |
| 902, 907 | `LogDwmColorizationState` | DiagOut::Line | out | C | info（失败 warn） | Draw3 |
| 921, 932 | `LogDwmModeDiagnostics` | DiagOut::Line | out | C | info | Draw3 |
| 944 | `LogSwapChainDescription` | DiagOut::Line | out | C | info | Draw3 |
| 974, 992 | `LogAdapterDiagnostics` | DiagOut::Line | out | A：Draw3 | info | Draw3 |

### 5.5 `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp`（产品部分 76 行；另 68 行自检见第 7 节）

绝大部分在 `DrawingController::Run`（**Draw3 绘制/输入线程**）。除标注外均为 C（无开关，只受 `Available()`）。

| 行 | 函数 | API | 流 | 桶 | 级别 | 模块 | 文本 |
|---|---|---|---|---|---|---|---|
| 491 | `TryCreateInkGuid` | Line | out | C | error | Draw3 | `CoCreateGuid returned a nil ink document GUID.` |
| 505, 511 | `TryAppendBlankPage` | Line | out | C | error | Draw3 | 新页创建/追加失败 |
| 698 | `CaptureClearRedoContinuation` | Text | err | C | error | Draw3 | `[Draw3.ClearRedo] result=failed reason=cpu_snapshot` |
| 1515 | `Schedule`（场景重试） | Printf | err | C | warn | Draw3 | `[Draw3.Scene] action=apply result=retry delay_ms=` |
| 1586, 1597 | `OnIoError`（当前页加载重试） | Printf | err | C | warn | Draw3 | `[Draw3.Presentation] ...current_load ...` |
| 1667 | （lambda，exit snapshot） | Text | err | C | error | Draw3 | `[Draw3.AutoSave] action=exit_snapshot result=failed reason=exception` |
| 1911, 1947 | `CaptureDesktopAutoSaveForScene` | Text | err | C | error | Draw3 | `[Draw3.AutoSave] action=capture result=failed ...` |
| 1938 | `CaptureDesktopAutoSaveForScene` | Printf | out | C | info | Draw3 | `[Draw3.AutoSave] action=capture trigger= strokes= bytes= elapsed_ms=` |
| 2071 | `BuildPresentationSaveRequest` | Text | err | C | error | Draw3 | `[Draw3.Presentation] action=capture result=failed` |
| 2334 | `InstallLoadedActivePresentationSlot` | Text | err | C | error | Draw3 | `[Draw3.Presentation] action=load_install result=failed reason=target_copy` |
| 15329 | `DrawWhiteboardPatternUnder` | Printf | err | C（thread_local 只报一次） | warn | Draw3 | `[Draw3.Pattern] whiteboard pattern pass failed ...` |
| 15485, 15493 | `ProcessPendingResize` | Line | out | C | error | Draw3 | resize D3D / presenter 失败 |
| 16078 | `Run` | Line | out | C | warn | Draw3 | `[InkHistory] GPU cache unavailable ...` |
| 16096 | `Run`（启动时一次） | Line | out | C | info | Draw3 | `[StrokeReconnect] enabled=... window_ms=...` |
| 16149 | `Run`（lambda） | Line | out | C | error | Draw3 | `Failed to preheat stroke model:` |
| 16304 | `Run` | Line | out | C | warn | Draw3 | `[Laser] resources unavailable ...` |
| 16564 | `Run` | Text | err | C | warn | Draw3 | `[Draw3.CanvasDebug] GPU overlay resources unavailable ...` |
| 16862 | `Run`（lambda） | Line | out | C | error | Draw3 | `Failed to initialize expanded stroke model:` |
| 17183 | `Run` | Line | out | C（`kInterruptedStrokeReconnectManualTestModeEnabled \|\| observer_.penDiagnostics`） | debug | Input | `[StrokeReconnect] rejected device=` |
| 17375 | `Run`（lambda） | Line | out | C | debug | Input | `[StrokeReconnect] linked device=` |
| 17424 | `Run` | Line | out | C | error | Draw3 | `Failed to continue interrupted stroke:` |
| 17593, 17643, 17862, 18104, 22333 | `Run` | Line | out | C | error | Draw3 | `Error: <status.message()>`（墨迹建模器错误） |
| 19068, 19074, 19092 | `Run` | Line | out | **B：`DRAW3_RTS_DIAGNOSTICS`**（19023-19106） | trace | Input（Cursor） | `[CURSOR_TRACE][cursor-sources/primary-samples/active-runtime]` |
| 19197 | `Run` | Line | out | C | error | Draw3 | `[InkHistory] page/runtime index mismatch.` |
| 19377, 19424, 19453, 19482, 19496, 19502 | `Run`（退出快照/致命封存） | Printf | err | C | error | Draw3 | `[Draw3.AutoSave] action=exit_snapshot/fatal_* ...` |
| 19491 | `Run` | Text | err | C | error | Draw3 | `[Draw3.AutoSave] action=fatal_cpu_seal result=failed reason=allocation` |
| 19755, 19771, 19778, 19825, 19884, 19897, 19920（具名 `Line undoDiagnostic`） | `Run`（Undo） | Line | out | C | debug（noop 为 info） | Draw3 | `[Undo] page= ... result=...` |
| 19939, 19953, 19964, 19974, 20024, 20048, 20093, 20109, 20140 | `Run`（Redo） | Line | out | C | debug | Draw3 | `[Redo] page= ...` |
| 20655, 20677 | `Run` | Printf | err | C | warn/error | Draw3 | `[Draw3.Presentation] action=current_load result=manual_required ...` 等 |
| 20905 | `Run` | Printf | err | C | warn | Draw3 | `[Draw3.Scene] action=command result=cancelled reason=scene_not_applied` |
| 20977 | `Run` | Text | err | C | error | Draw3 | `[Draw3.Clear] result=failed reason=snapshot` |
| 21192, 21209, 21221, 21294 | `Run`（页切换） | Line | out | C | debug（failed 为 error） | Draw3 | `[Page] key=... action=... result=...` |
| 21422, 21445 | `Run`（设备恢复） | Line | out | C | error | Draw3 | `Failed to recover Draw3 graphics after HRESULT` / GPU history rebuild 失败 |
| 21505 | `Run` | Line | out | C | info | Draw3 | `Recovered Draw3 graphics with mode` |
| 21570 | `Run` | Line | out | C | debug | Draw3 | `[InkHistory] resize generation=` |
| 23005, 23014 | `Run` | Line | out | C | error | Draw3 | 笔画栅格化/追加失败 |
| 23361 | `Run` | Printf | err | C（1s 节流） | error | Draw3 | `[Draw3.Raster] submission failed; attempts= device=` |
| 23576, 23583, 23592, 23598 | `Run`（异常处理） | Text/Printf | err | C | error | Draw3 | `[Draw3.AutoSave] action=run_exception ...` |

### 5.6 `Draw3.ShapeRuntimeActions.inl`（`#include` 进 `DrawingController.cpp:19743`，运行于 `Run`）

| 行 | API | 流 | 桶 | 级别 | 模块 | 文本 |
|---|---|---|---|---|---|---|
| 69 | Printf | out | C（先判 `Available()`） | debug | Draw3 | `[Draw3.ShapeEdit] action=gesture_end result= previews= ...` |
| 231 | Text | err | C | warn | Draw3 | `[Draw3.ShapeEdit] action=materialize warning=not_z_suffix` |
| 256, 270 | Text | err | C | warn | Draw3 | `... result=fallback path=authoritative_replay` |

### 5.7 `Draw3.Host.cpp`（产品 35 行；自检 4 行见第 7 节）

| 行 | 函数 | API | 流 | 桶/门控 | 级别 | 模块 | 文本 |
|---|---|---|---|---|---|---|---|
| 135 | `LogDraw3BuildIdentity` | Line | out | C | info | Draw3 | `[Draw3Diag][build] id=draw3-<date>T<time> arch= msvc=` |
| 153 | `LogDraw3WindowState` | Line | out | C | debug | Draw3 | `[Draw3Diag][window] event= role= hwnd= ...` |
| 931 | `RefreshPresentationInputGateLocked` | Printf | out | C（reasons 变化时） | debug | Input | `[Draw3.Input] action=admission reasons= ...` |
| 1062 | `ObservePresented` | Line | out | C（目标/恢复变化时） | info | Draw3 | `[Draw3Diag][present] event=...` |
| 1350 | `ObserveDesktopAutoSave` | Printf | err | C | error | Draw3 | `[Draw3.AutoSave] action=submit ... result=failed` |
| 1372, 1383 | `ObservePresentationSave/Load` | Text | err | C | error | Draw3 | `[Draw3.Presentation] action=save_submit/load_submit result=failed` |
| 1454, 1477, 1492, 1500, 1522, 1542, 1543 | `ObserveEraserDiagnostics` | Printf/Text（部分 mirror） | err | **A：TouchArea**（1412 `touchAreaTraceEnabled`） | trace | Input（TouchArea） | `[EraserEntry]`、`[TouchArea]`、`[FineBand]`、curve 等 |
| 1696 | `PumpDisplayScale` | Text（mirror） | err | **A：TouchArea**（1676 `development.touchAreaTrace`） | debug | Input（TouchArea） | `[EraserDisplay] ...` |
| 1963 | `PumpOneBridgeCommand` | Text | err | C | warn | Draw3 | `[Draw3.Input] action=command_wake result=failed reason=bridge_empty` |
| 2139, 2142 | `Start` | Text | err | C | error | Draw3 | AutoSave / Presentation worker 启动失败 |
| 2170, 2182, 2222, 2256, 2263, 2318, 2329, 2343 | `Start`（绘制线程启动） | Line | out | C | info | Draw3 | `[Draw3Diag][startup] stage=...` |
| 2363, 2369 | `Start` | Printf/Text | err | C | error | Draw3 | `[Draw3] startup failed: ...` |
| 2393, 2400 | `Start` | Printf/Text | err | C | error | Draw3 | `[Draw3] drawing loop stopped: ...` |
| 2498 | `Start` | Text | err | C | warn | Input/RTS | `[Draw3] RTS unavailable; Win32 mouse compatibility input enabled` |
| 2507 | `Start` | Line | out | C | info | Input/RTS | `[Draw3] Input initialization result= input_mode=` |
| 2645, 2647, 2650 | `Stop` | Text | err | C | error | Draw3 / Shutdown | `[Draw3.AutoSave] action=exit_barrier result=failed reason=...` |

### 5.8 `Draw3.TransparentPresentation.cpp`（产品 24 行；`RunUlwDirtyCopyBenchmark` 16 行见第 7 节）

| 行 | 函数 | API | 流 | 桶/门控 | 级别 | 模块 | 文本 |
|---|---|---|---|---|---|---|---|
| 289 | `LogPresentEvent` | Line | out | C（失败/恢复/首次成功） | info/error | Draw3 | `[Draw3Diag][ulw] event=` |
| 325 | `LogPixelDiagnostic`（具名 `Line line`） | Line | out | **A：Draw3**（568-576 `inspectPixels` 需 `StartupEnvironmentDiagnosticsEnabled()`） | trace | Draw3 | ULW 像素采样 |
| 425 | `CreateStagingTexture` | Line | out | C（失败时） | error | Draw3 | `[Draw3Diag][ulw-init] ... stage=CreateTexture2D ...` |
| 490 | `Initialize` | Line | out | **B：`INKEYS_DRAW3_ULW_FULL_UPDATE`** | info | Draw3 | `[Draw3Diag][ulw-control] ... dirtyParameter=null` |
| 511 | `Present`（`fail` lambda，仅首次） | Line | out | C | error | Draw3 | `[Draw3Diag][ulw] stage= result=failed` |
| 973, 978, 989, 998, 1004 | `TryCreateWaitableSwapChain` | Line | out | C | info/warn | Draw3 | waitable swapchain 尝试/回退 |
| 1034 | `CreateSwapChain` | Line | out | C | info | Draw3 | `Ordinary swapchain enabled in mode` |
| 1088, 1099, 1104, 1114, 1128, 1136, 1140 | 模式初始化（`<lambda>`） | Line | out | C | info/warn | Draw3 | present mode 尝试、失败（1104/1114 带 `lastError`）、生效 |
| 1239 | `TransparentPresentationController::Initialize` | Line | out | C | warn | Draw3 | `... failed; fallback to ...` |
| 1266 | 同上 | Line | out | C | error | Draw3 | `All transparent present modes failed.` |
| 1284 | `SetOutputTarget` | Line | out | **A：Draw3** | debug | Draw3 | `[Draw3Diag][target] requested=` |
| 1434 | `Present` | Line | out | C | warn | Draw3 | `[Draw3Diag][handoff] DwmFlush failed` |
| 1449 | `Present` | Line | out | C/A 混合（`!succeeded \|\| StartupEnvironmentDiagnosticsEnabled()`） | info/error | Draw3 | `[Draw3Diag][present] mode= target= ...` |
| 1471 | `Present` | Line | out | C（仅首次失败） | error | Draw3 | `Present failed in mode` |

### 5.9 `Draw3.GraphicsInitialization.cpp`（产品 5 行）

| 行 | 函数 | API | 桶/门控 | 级别 | 模块 | 说明 |
|---|---|---|---|---|---|---|
| 75 | `LogDeviceAttempt` | Line | C/A 混合（成功且硬件且非 fallback 时需 Draw3 开关；失败始终输出） | info/warn | Draw3 | `[Draw3Diag][device] driver= levels= attempt= hresult=` |
| 121 | `InitializeGraphicsDevice` | Line | C | warn | Draw3 | `Hardware device initialization failed. Falling back to WARP.` |
| 130 | `InitializeGraphicsDevice`（具名 `Line line`） | Line | C | error | Draw3 | `[Draw3Diag][device] final=failed ...` |
| 141, 142 | `InitializeGraphicsDevice` | Line | A：Draw3 | info | Draw3 | 当前设备、feature level |

### 5.10 其它 Draw3 文件

| 文件:行 | 函数 | API | 流 | 桶/门控 | 级别 | 模块 | 说明 |
|---|---|---|---|---|---|---|---|
| `Draw3.AutoSave.cpp:810` | `ProcessRequest`（commit 失败 lambda） | Printf | err | C | error | Draw3 | `[Draw3.AutoSave] action=commit request= result=failed stage= path=` |
| `Draw3.AutoSave.cpp:1193` | `WorkerMain` | Printf | err | C | error | Draw3 | `... stage=root-copy` |
| `Draw3.AutoSave.cpp:1220` | `WorkerMain` | Printf（成功 out / 失败 err） | out/err | C | info/error | Draw3 | `[Draw3.AutoSave] action=commit request= trigger= result=` |
| `Draw3.PresentationAutoSave.cpp:752` | `CollectOwnedVersions` | Printf | err | C | warn | Draw3 | `... collect_version result=retained error=` |
| `Draw3.PresentationAutoSave.cpp:935` | `SelectStorageTrack` | Printf | err | C | warn | Draw3 | `... select_track result=retained_invalid` |
| `Draw3.PresentationAutoSave.cpp:1817, 1824` | `Run`（worker） | Printf | err | C | error | Draw3 | save/load 失败 |
| `Draw3.WhiteboardStore.cpp:61, 113, 118` | `PublishIndex` / `Run` | Text | err | C | warn | Whiteboard | `[Whiteboard.Store] ... retaining ...` |
| `Draw3.Product.cpp:385, 389` | `DrawpadMsgCallback` | Printf/Text | err | C | error | Draw3 | `[Draw3] drawpad message stopped: ...`（WndProc 异常兜底） |
| `Draw3.HapticFeedback.cpp:402, 488, 510, 551, 581, 603, 655, 699` | `Initialize` / `AttachPointerId` / `PlayDiscrete` / `StopFeedback` / `LogDebugFailure` / `LogContinuousResult` / `LogUnavailableOnce` / `CachePointerController` | Line | out | **A：Haptics**（各自前置 `HapticDiagnostics::Enabled()`，分别见 401/487/509/547/575/592/653/695） | trace（655 为 warn） | Haptics | `[PenHaptics] event=backend ...` |
| `Draw3.HapticDiagnostics.h:38, 44, 71` | `~ProductMessageOutputScope` / `Log` | Write | err | **A：Haptics** | trace | Haptics | `[PenHaptics] event=...`、`event=log_drop` |
| `Draw3.RealtimeStylus.cpp:259` | `InterruptedStrokeSimulation<true>` 构造 | Line | out | B：编译期常量 false | debug | Input/RTS | `[StrokeReconnectSimulation] enabled=true ...` |
| `Draw3.RealtimeStylus.cpp:2706` | `RealTimeStylusInput::Initialize`（每次尝试） | Printf | err | C（注释“不依赖控制台开关”） | info/warn | Input/RTS | `[RTS] attempt= profile= stage= hr= initialized= cleanup=` |
| `Draw3.RealtimeStylus.cpp:2754` | `InitializeAttempt` | Line | out | B：`DRAW3_RTS_DIAGNOSTICS`（2740-2753） | debug | Input/RTS | `[RTS] initialize currentThread=` |
| `Draw3.RealtimeStylus.cpp:2886` | `InitializeAttempt` | Line | out | C | info | Input/RTS | `[RTS] MultiTouchEnabled=` |
| `Draw3.RealtimeStylus.cpp:3050, 3059, 3066, 3082, 3090` | `TraceTouchAreaDiagnostics`（由 `Host.cpp:1429` 在 TouchArea 门内调用） | Printf | err | **A：TouchArea** | trace | Input（TouchArea） | `[TouchAreaDevice] ...` |
| `Draw3.Renderer.cpp:123` | `ReportSelectedPixelShader` | Text（mirror） | out | B：`INKEYS_DRAW3_WIN7_DIAGNOSTICS`（直接读环境变量，**Release 也生效**） | info | Draw3 | `[Draw3ShaderSelection] actual= sha256= bytes= create_hr=` |
| `Draw3.Renderer.cpp:131` | `ReportLaserParticleUnavailableOnce` | OutputDebugStringW | dbg | C（只报一次） | warn | Draw3 | `[Draw3] D3D11 Compute Shader 激光粒子不可用...` |
| `Draw3.RendererSnapshot.cpp:29` | `ReportTrustedSnapshotUnavailableOnce` | OutputDebugStringW | dbg | C（只报一次） | warn | Draw3 | `[Draw3] 可信 L2 快照资源不可用...` |
| `Draw3.InkHistoryGpu.cpp:163` | `ReportCacheFailureOnce` | OutputDebugStringW | dbg | C（只报一次） | warn | Draw3 | 调用方传入的缓存失败文本 |
| `Draw3.RendererPrimitives.cpp:398` | `InkRenderer::DrawTransientDrawingCursor` | Write | out | **A：Draw3**（335 起，首帧及每秒最多一次） | trace | Draw3 | 光标绘制 GPU 绑定状态 |
| `Draw3.StrokeGeometry.cpp:141` | `WriteTailDebugRecord` | OutputDebugStringA（+ 写 `%TEMP%` 独立文件） | dbg | B：`INKEYS_DRAW3_TAIL_TRACE` | trace | Draw3 | `[Draw3 tail] decision= ...` |
| `Draw3.PenCursor.cpp:167` | `TraceResolvedDrawingCursor` | Write | out | B：`drawingCursorTraceEnabled` 只由 `ConfigureDrawingCursorTrace` 设置，而它唯一调用者 `WindowController::SetRtsTraceEnabled` 在 `#if DRAW3_RTS_DIAGNOSTICS` 内 | trace | Input（Cursor） | `[CURSOR_TRACE][app] ...` |
| `Draw3.WindowControl.cpp:1347` | `ConfigureDrawingCursor` | Line | out | C | error | Draw3 | `Invalid drawing cursor appearance for tool=` |
| `Draw3.WindowControl.cpp:2047, 2074, 2223` | 系统光标决策 / `TraceTouchMouseMessage` | Write | out | B：`DRAW3_RTS_DIAGNOSTICS`（1952-2079、2160-2229） | trace | Input（Cursor） | `[CURSOR_TRACE][system/touch-mouse]` |
| `Draw3.PptTiming.h:32` | `TracePptTiming` | Text（mirror） | err | B：`INKEYS_PPT_TIMING` | trace | Ppt | `[PptSync] stage= session= target= qpc= frequency=` |

### 5.11 UI / Window / Input / 顶层头文件

| 文件:行 | 函数 | API | 流 | 桶/门控 | 级别 | 模块 | 说明 |
|---|---|---|---|---|---|---|---|
| `Input/UiPointerDiagnostics.h:73` | `Write` | DiagOut::Write | err | **A：Haptics** | trace | Input | `[PenHaptics] event=ui_input/ui_schema ...`（测试构建可经 `testSink` 替换） |
| `UI/PageControl/PageControl.cpp:203, 204` | `TraceDrag` | `fputs(stdout)` + `OutputDebugStringA` | out+dbg | **C：无任何门控** | debug | PageControl | `[PageControlDrag][tid=] down/up/threshold/candidate/move-pair/constraint ...`；同步写 stdout 与调试器 |
| `UI/PageControl/PageControl.cpp:2224, 2225, 2236` | `TraceSurfaceStage` | `fputs(stdout)` + ODS | out+dbg | B：`INKEYS_PAGECONTROL_PRESENT_TRACE`（+2s 去重） | debug | PageControl | `[PageControlPresent] ...` |
| `UI/Ppt/Ppt.cpp:97` | `LogTopmostRefreshState` | OutputDebugStringW | dbg | C（失败/恢复边沿，见 340-341） | warn/info | Ppt | `[Ppt] topmost refresh failed/recovered: ...` |
| `UI/Whiteboard/Whiteboard.cpp:128` | `FollowProfiler::Flush` | DiagOut::Printf | err | B：`INKEYS_WHITEBOARD_PATTERN_PROFILE` | debug | Whiteboard | `[WhiteboardPatternPerf] M1/M2 ...` |
| `Window/Window.cpp:118` | `WriteWindowDiagnostic`（调用点 151/170/1143/1690） | DiagOut::Write（mirror 可选） | err | C | warn | Window | 窗口 owner 有界格式化入队 |
| `Window/Window.cpp:1117` | `ReportCommandResult` | DiagOut::Line(Error, mirror) | err | C（失败/恢复边沿） | warn/info | Window | `[Window.ClickThrough] result= role= error= ...` |
| `Window/Window.cpp:1721, 1722` | 窗口创建（旧版 Setting 框架） | `OutputDebugStringW` + `fwprintf(stderr)` | dbg+err | C | error | Window | `Inkeys.Window legacy setting frame failed: error=` |
| `PptWaitingDiagnostics.h:129` | `PptWaitTrace::RawOutput`（← `FlushAfterProducersJoined`，`IdtMain.cpp:4355`） | `GetStdHandle(STD_ERROR)`+`WriteFile` | err | B：`INKEYS_PPT_PERF` | trace | Ppt | 退出时一次性输出全部 `[PptWait] v=1 event=record ...` 与 summary |
| `ShutdownProgressTrace.h:49` | `ProgressTrace::WriteRaw` | `GetStdHandle(STD_ERROR)`+`WriteFile` | err | B：`INKEYS_PPT_PERF` | trace | Shutdown | `[ShutdownTrace] v=1 source=native ...`（同步写 stderr） |

### 5.12 E 桶：应急路径

| 文件:行 | 函数 | API | 说明 |
|---|---|---|---|
| `Helper/Helper.CrashHandler.cpp:161` | `GetExeDirectory` | ODS | 无法获取模块路径 |
| `Helper.CrashHandler.cpp:474, 491, 500, 517, 523, 530, 543, 552, 576, 588, 591, 594, 606, 611, 614, 616` | `UnhandledExceptionHandler`（UEF） | ODS | 重入、重启监督未建立（500 含 `GetLastError`）、目录、dump/report 生成进度与失败 |
| `Helper.CrashHandler.cpp:696, 780, 783` | `GenerateMiniDump` | ODS | pending dump 创建/删除失败（696 前先锁存 `createError`，输出后 `SetLastError(createError)`） |
| `Helper/ShutdownSupervisor.cpp:683` | 监督进程等待旧进程 | ODS | `ShutdownSupervisor: 旧进程终止超过5秒...`（独立监督进程，不加载产品日志） |

CrashHandler 与 ShutdownSupervisor 均未使用 `IDTLogger` / spdlog（`grep` 无命中）。建议模块标签：Crash / Shutdown。

---

## 6. 现有环境变量门控 trace（含经 IDTLogger 的）

| 名称/前缀 | 门控 | 产生位置 | 通道 | 备注 |
|---|---|---|---|---|
| `[PptPerf] source=native` | `INKEYS_PPT_PERF` | `IdtPlug-in.cpp:80-91` `WritePptPerfEvent` → `IDTLogger->info("{}")`；`PptPerfRecorder()` 26 个调用点 | IDTLogger | `Recorder::Emit` 吞掉 sink 异常 |
| `[PptPerf] source=managed` | `INKEYS_PPT_PERF` | `PptCOM/PptPerfTrace.cs:115-142` | C# 直写 stderr（UTF-8 无 BOM） | 不受 PptCOM 开关；`UseTestOutput` 可替换出口 |
| `[PptNav]` | `INKEYS_PPT_TIMING` | `IdtPlug-in.cpp:154-165` `TracePptNavigation` | IDTLogger info | |
| `[PptGate]` | `INKEYS_PPT_TIMING`（`tracePptGate`） | `IdtPlug-in.cpp:1282-1390`（1295/1348/1362/1383/1390） | IDTLogger info/warn | 注释：不写演示文稿路径/标题 |
| `[PptSync]` | `INKEYS_PPT_TIMING` | `Draw3.PptTiming.h:10-33` | DiagnosticOutput Error+mirror | |
| `[PptExitSurface]` | `INKEYS_PPT_EXIT_TRACE` | `IdtPlug-in.cpp:979-991`（`TraceTrueExit`）；`IdtState.cpp:940-1005`（2s 节流） | IDTLogger info | |
| `[PptWait]` | `INKEYS_PPT_PERF`（`Prepare` 时） | `PptWaitingDiagnostics.h`；Flush 于 `IdtMain.cpp:4355` | raw stderr `WriteFile` | 必须在所有 producer join 之后 flush |
| `[ShutdownTrace]` | `INKEYS_PPT_PERF` | `ShutdownProgressTrace.h`；`IdtMain.cpp` `SetOffSignal` 375-429 与 4334-4395；`IdtPlug-in.cpp` 10 处 | raw stderr `WriteFile` | `SetOffSignal` 中 Arm 前只 `Capture` 纯值，Arm 返回后才 `Begin/Emit` 输出（374 注释“不能让诊断 I/O 抢在独立退出保护之前”） |
| `[PptOwner]` | `INKEYS_PPTCOM_TRACE` + PptCOM 开关 | `PptCOM.cs:187-197` | C# Console | |
| `[SettingEntryPerf]` | `INKEYS_SETTING_ENTRY_PROFILE` | `Setting.cpp:242-310` | IDTLogger info | |
| `[WhiteboardPatternPerf]` | `INKEYS_WHITEBOARD_PATTERN_PROFILE` | `Whiteboard.cpp:101-136` | DiagnosticOutput | |
| `[PageControlPresent]` | `INKEYS_PAGECONTROL_PRESENT_TRACE` | `PageControl.cpp:2155-2240` | stdout + ODS | |
| `[Draw3 tail]` | `INKEYS_DRAW3_TAIL_TRACE` | `StrokeGeometry.cpp:36-150` | ODS + `%TEMP%\Inkeys-Draw3-tail-<pid>.log` | 自带独立文件 |

外部采集脚本依赖（迁移时受影响）：
- `Scripts/Start-PptNavigationDiagnostic.ps1`：设置 `INKEYS_PPT_PERF=1`、`INKEYS_PPT_TIMING=1`，重定向子进程 stdout/stderr 到 `stdout.log/stderr.log`（140-157），并从 `<exe目录>\log\idt*.log` 复制原生日志（9、185-187）。
- `Scripts/Start-ShutdownDiagnostic.ps1`：同样设两项环境变量，从 stderr 中匹配 `[ShutdownTrace] v=1 source=native`（62、309），并复制 `<exe目录>\log\idt*.log`（342）。
- `Scripts/analyze-ppt-navigation.py`：按 `'[PptPerf]' in line` 后 `split('[PptPerf]',1)[1]` 解析 `key=value`（37-39），对行首附加标头不敏感；扫描目录内 `*.log`（539）。

---

## 7. D 桶：测试工具协议（保持不变）

### 7.1 每文件清单（407 行）

| 文件 | 行数 | 函数：行号 |
|---|---:|---|
| `Inkeys/IdtConfiguration.cpp` | 2 | RunPenWidthPreferencesProductionTest: 696(puts stdout),699 |
| `Inkeys/IdtFreezeFrame.cpp` | 2 | RunFreezeSurfaceResourceLifecycleTest: 362,432 |
| `Inkeys/IdtI18n.cpp` | 3 | `IdtTest::RunI18nLocaleTests`: 281(cerr),384(cout),405(cout) |
| `Inkeys/IdtMain.cpp` | 30 | RunCrashFilterLifecycleTest: 571,584,587; RunDesktopShortcutTest: 602,673,676; RunConfigBoundaryTest: 713,726,737,749,846,853,867,953,962,974,986,999,1026,1054,1190,1193,1198; RunDdbDisabledPreservationTest: 1225,1228,1233; RunPptComResourceVerificationTest: 1323,1328,1332,1337 |
| `Inkeys/IdtPlug-in.cpp` | 3 | RunPptFocusProductionTest: 805,927,930 |
| `Inkeys/IdtState.cpp` | 8 | RunShapeToolStateProductionTest: 2861,3076,3077; RunStateModeProductionTest: 4436,4441,4445; RunPenWidthStateProductionTest: 4837,4840(puts stdout) |
| `Draw3.AutoSave.cpp` | 1 | CommitIndex: 514（`[Draw3.AutoSave.TestIo]`，仅 `faults.logIndexCommitDiagnostics`，**经 DiagnosticOutput**） |
| `Draw3.DiagnosticOutput.cpp` | 3 | RunProductionTest: 484,624,734 |
| `Draw3.Diagnostics.cpp` | 2 | RunDiagnosticFormattingProductionTest: 413,456 |
| `Draw3.DrawingController.cpp` | 68 | RunPenWidthProductionProbe: 6327,6482,6534; RunLaserRasterFailureProductionProbe: 6549; RunEraserIdleWakeProductionProbe: 6684,6836,6873; RunIndependentTouchDiscardIdentityProductionProbe: 6882,6929; RunSingleTouchReconnectAdmissionProductionProbe: 6938,7032; RunReconnectContactIdentityProductionProbe: 7043,7207; RunPalmEraserProductionProbe: 7216,7965; RunCanvasPanScaleProductionProbe: 8090,8413; RunCanvasPanAdapterOrderingProbe: 8423; RunCanvasPanStreamProbe: 8468; (PanFeel helpers) Check: 8641, Finish: 8853; RunCanvasPanFeelProductionProbe: 9019,9192,9193; RunCanvasPanProductionProbe: 9204,10217; RunDraw3ControlFenceProductionProbe: 10228; RunFallbackStableControllerIsolationProbe: 10456; RunFallbackStableControllerLaneProbe: 10477,10907; RunPresentationWorkerLoadRetryProbe: 10920,11199; RunPresentationCurrentLoadRetryProbe: 11210,11399; RunPresetCreationProductionTest: 11410,11624; RunShapeEditHistoryProductionTest: 11633,11796; RunShapeDocumentProductionTest: 11806,11878,11879; RunWhiteboardThemePreferenceProductionTest: 11886; RunWhiteboardStorageProductionTest: 11933,12372,12374; RunGeneralDrawingOptionsProductionTest: 12383,12456; RunIgnoredLaserTouchProductionTest: 12467,12633; RunRejectedStrokeInitializationProductionTest: 12644,12816; RunRuntimeMetricsSessionProductionProbe: 12827,13290; RunDraw3ContentProofProductionProbe: 13301,13686,14039,14045,14152; RunParkedDesktopExitAutoSaveTest: 14163,14274,14592,14597; RunParkedPresentationRetainedSaveTest: 14614,14730; RunPresentationLoadedRetainedInstallTest: 14742,14853; RunPendingPresentationTopologyLoadTest: 14865,15011 |
| `Draw3.FailedCleanupFixture.cpp` | 3 | RecordFixtureFailure: 113; RunPptStorage: 1137; CheckDesktopReaderBoundaries: 1263 |
| `Draw3.GraphicsInitialization.cpp` | 2 | RunGraphicsDeviceSelectionTest: 206,211 |
| `Draw3.HiddenWindowTest.cpp` | 68 | Report: 73(fputs stderr),74(ODS); CheckPenDwellAndResume: 630; CheckPenPhysicalRelease: 709; CheckPresentationPersistence: 786,1054; RunMode: 1205,1468,1523,1588,2226,2236,2390,2425,2489,2761; CheckShaderSelectionPolicy: 2850,2854; RunRendererPixelTestForDriver: 3071,3073,3076,3138,3153,3158,3162,3188,3201,3247,3331,3337,3352,3406,3424,3445,3482,3487,3489,3498,3529,3545,3554,3561,3629,3687,3697,3718,3722,3730,3737,3773,3803,3830,3899,3983,4018,4025,4027,4029,4031,4037; RunRendererMapCompatibilityTest: 4144,4157; RunHiddenWindowRuntimeMetricsSmoke: 4450; RunShapeFocusHiddenTest: 4961,4983,5082,5125,5148 |
| `Draw3.HistoryProbe.cpp` | 7 | Fail: 142; CheckExtremeFootprintBoundary: 204; RunBlock: 342; RunPlannerBenchmarks: 489; RunDraw3HistoryBenchmark: 503,515,520（含 stdout 指标行） |
| `Draw3.Host.cpp` | 4 | RunPresentationCompletionDeliveryTest: 211,343; RunHostWorkspaceTransitionInputProductionProbe: 3188,3428 |
| `Draw3.PerformanceProbe.cpp` | 6 | Require: 41; RunScenario: 202,210; RunDraw3GeometryBenchmark: 228,237,242（含 stdout 指标行） |
| `Draw3.PresentationAutoSave.cpp` | 2 | SaveVersionedUInk: 630; SavePresentation: 1156（`[Draw3.Presentation.TestIo]`，仅测试 fault 标志，**经 DiagnosticOutput**） |
| `Draw3.Renderer.cpp` | 11 | RunLaserResourceLifecycleValidation: 822,855(stdout),998(stdout),1001; RunRendererUploadValidation: 1011,1260,1267,1400,1402,1403,1406 |
| `Draw3.RendererDebugOverlay.cpp` | 3 | RunCanvasDebugOverlayValidation: 276,514,517 |
| `Draw3.RendererPattern.cpp` | 14 | RunWhiteboardPatternValidation: 163,200,273,397,485,496,529,624,631,680,722,733,749,753 |
| `Draw3.ShapeRendererBenchmark.inl` | 1 | 133 |
| `Draw3.ShapeRendererEditTests.inl` | 1 | 58 |
| `Draw3.ShapeRendererTests.cpp` | 5 | RunShapeRendererTests: 35,252,342,602,608 |
| `Draw3.ShapeRuntimeTests.inl` | 2 | RunShapeInteractionProductionTest: 6,595（`#include` 于 `DrawingController.cpp:11800`） |
| `Draw3.StrokeGeometry.cpp` | 6 | RunPenTaperProductionProbe: 1198,1206,1736,1740,1893,1899 |
| `Draw3.TransparentPresentation.cpp` | 16 | RunUlwDirtyCopyBenchmark: 1563,1582,1593,1603,1615,1626,1635,1644,1718,1726,1732(stdout),1768(stdout),1846,1857(stdout),1871,1876 |
| `Draw3.WindowControl.cpp` | 2 | RunTouchpadIngressProductionProbe: 736,803 |
| `Helper/ShutdownSupervisor.cpp` | 7 | PrintTestResult: 870; PrintCaseObservation: 883; 用例观察行: 1626,1675,1883,2191; RunCleanupRealProcess: 2606（全部 `GetStdHandle(STD_OUTPUT)`+`WriteFile`，`shutdown-supervisor ... PASS/FAIL` 协议） |
| `Helper/Ui3PresentationFixtureAuth.cpp` | 9 | RunParent: 624(printf),627(fwprintf stderr); RunPathTests: 734,740,759,772,778; RunAuthTests: 798,803（stdout `[UI3FixtureAuth] ... PASS/FAIL`） |
| `Net/Net.Update.cpp` | 3 | RunStagedUpdateJsonBoundaryProbe: 132,137,142 |
| `UI/Bar/Bar.GeometryAttribute.Test.cpp` | 4 | RunGeometryAttributeOffscreenTest: 966,1610,1659,1816 |
| `UI/Bar/Bar.Interaction.cpp` | 9 | RunConstructionAutoCollapseProductionTest: 978; RunSharedInteractionWorkerTests: 2362,2999,3006; RunThicknessFineDialProductionTest: 4258,4263,4267; RunBarNativeDirectMoveTests: 9201,9348 |
| `UI/Bar/Bar.KeyboardShortcuts.h` | 6 | `BarKeyboardShortcutOwner::ProductionTest`: 602,606,607; `WhiteboardProductionTest`: 647,651,652 |
| `UI/Bar/Bar.MultiTarget.Test.cpp` | 28 | MeasureRun: 245,271,495; Run: 611,628; RunPerformance: 801,810; RunFoldedClickRelease: 874; RunDockTransitions: 887,1217,1250,1274,1301,1342,1345,1347,1366,1369; RecordWindowEvent: 1423; Fail: 1431; RunFreezeHideTransition: 1542,1562; Initialize: 1704; Step: 1784,1808; Report: 1908; RunEntryPolicyMatrix: 2072; RunUi3GeometryTests: 2218 |
| `UI/Bar/Bar.RenderLoop.cpp` | 2 | RunIdleBackingResourceLifecycleTest: 13317,13369 |
| `UI/Bar/Bar.ShapeEditor.cpp` | 4 | RunShapeEditorOffscreenTest: 3314,3650,3870,4466 |
| `UI/Bar/Bar.StatusIndicator.cpp` | 1 | BarStatusIndicatorPanel::RunOffscreenTest: 538 |
| `UI/PageControl/PageControl.cpp` | 20 | fixture 清理回调: 3693(ODS); RunOffscreenTests: 3755,3995,4591,5115,5118,5120,5192,5321; RunHiddenWindowTransitionPerformance: 5436; RunHiddenWindowCandidateContracts: 5457,5554; RunHiddenPageDirectMoveTests: 5561,5577,5662; RunHiddenWorkspaceWindowContracts: 5672,5812; RunHiddenWindowTests: 5825,6072,6180 |
| `UI/SelectionOverlay/SelectionOverlay.cpp` | 2 | RunNativeInputRoutingTests: 256,280 |
| `UI/Setting/Setting.cpp` | 8 | RunTextureRecoveryProductionTest: 5666,5791(printf),5801(printf); RunFontLifecycleProductionTest: 5811,5934(printf); RunSettingShutdownRenderingTests: 6042; RunBusinessCancellationProductionTest: 6142,6221 |
| `UI/Whiteboard/Whiteboard.cpp` | 9 | RunHiddenBackgroundTests: 673,723,885,891,900,959,1112,1160,1193 |
| `Launch/IdtCommandPipe.h` | 7 | RunStartupUpdateTests: 364,390; RunTests: 559,565,584,619,664 |
| `Launch/IdtSingleInstance.h` | 9 | TryRunTests: 419,431,473,476,483,500,503,514,516 |
| `SuperTop/IdtSuperTop.cpp` | 4 | RunSuperTopTokenFailureTest: 61,84,89,160 |

未注明流的均为 `stderr`（`fprintf/fputs(stderr)`）。全部入口由 `IdtMain.cpp:1758-2150` 的 `--xxx-test/--xxx-benchmark` 命令行早退分发，多数位于 `#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS` 内。

### 7.2 D 桶与 DiagnosticOutput 的耦合（迁移风险点）

- `runDraw3SelfTest`（`IdtMain.cpp:1739-1752`）为 30+ 个 Draw3 自检启动 `DiagnosticOutput::Session`，因此这些自检运行时**产品 DiagnosticOutput 行也写入 stderr/stdout**。
- `--draw3-diagnostic-output-test`（`IdtMain.cpp:1817-1845`）调用 `DiagnosticOutput::RunProductionTest(helpers)`，helpers 中依次调用 `LogCanvasPan`、`RecordCursorDiagnostic`、`FlushCursorDiagnostics`、`LogFrameTiming`、`LogHResult`、`LogWin32Error`、`TracePptTiming`，并由 `RunProductionTest` 校验真实管道里的字节（如 `.cpp:549-550` 的 `"stdout bytes unchanged"` / `"stderr bytes unchanged"`、`.cpp:543` LastError 保持）。
- `[Draw3.AutoSave.TestIo]`（`AutoSave.cpp:514`）、`[Draw3.Presentation.TestIo]`（`PresentationAutoSave.cpp:630, 1156`）是测试 fault 标志开启时经 DiagnosticOutput 输出的协议行。
- `Draw3.DiagnosticOutput.cpp`、`Draw3.PenCursor.cpp`、`Draw3.HapticFeedback.cpp`、`Window.cpp`、`UiPointerInput.cpp`、`RenderPipeline.cpp`、`Ui3PresentationFixtureAuth.cpp` 也被 `InkeysHeadlessTests/InkeysHeadlessTests.vcxproj` 直接编译（该工程不编译 `IdtMain.cpp`，没有 `IDTLogger` 定义）。
- `Bar.Presentation.Test.cpp:685` 在 fixture 中把全局 `IDTLogger` 替换为 `std::make_shared<spdlog::logger>("ui3_fixture")`。

---

## 8. PptCOM（C#）逐点清单

| 文件:行 | 函数/上下文 | API | 桶/门控 | 级别 | 模块 | 文本 |
|---|---|---|---|---|---|---|
| `ComEventConnection.cs:221` | 事件连接清理失败 | `Console.WriteLine` | A：PptCOM | warn | PptCOM | `[PptCOM] event-cleanup-pending=1 hr=0x` |
| `PptCOM.cs:195` | `TracePptOwnerDiagnostic` | `Console.WriteLine` | A+B：PptCOM 开关 + `INKEYS_PPTCOM_TRACE` | trace | PptCOM | `[PptOwner] ...` |
| `PptCOM.cs:545` | cleanup（`cleanup-message-before`） | `Console.WriteLine` | A | debug | PptCOM | `try CLEAN` |
| `PptCOM.cs:577` | cleanup（`cleanup-message-after`） | `Console.WriteLine` | A | debug | PptCOM | `CLEAN!` |
| `PptCOM.cs:795` | 前台进程名检查（WPS） | `Console.WriteLine` | A | warn | PptCOM | `  Process Name Check Failed: <ex.Message>` |
| `PptCOM.cs:937` | `BindEvents` catch | `Console.WriteLine` | A | warn | PptCOM | `[PptCOM] events=0 polling=1 hr=0x` |
| `PptCOM.cs:941` | `BindEvents` finally | `Console.WriteLine` | A | info | PptCOM | `[PptCOM] events= polling=` |
| `PptCOM.cs:949` | `TraceRotScan`（调用点 1067/1145/1157/1178/1183/1206） | `Console.WriteLine` | A | debug（1157/1183/1206 为 warn/error） | PptCOM | ROT 扫描去重/优先级/错误 |
| `PptCOM.cs:1482` | `ObserveUnknown` 失败 | `Console.WriteLine` | A | warn | PptCOM | `[PptCOM] show-observation=0 busy= hr=0x` |
| `PptCOM.cs:1501` | `PptComService` 开始 | `Console.WriteLine` | A | info | PptCOM | `PPT Monitor ReStarted` |
| `PptCOM.cs:1556` | `PptComService` catch | `Console.WriteLine` | A | error | PptCOM | `[PptCOM] owner-failed hr=0x` |
| `PptComDiagnostics.cs:60` | `Write`（← `Event`/`TraceComState` 等 18 处） | `Console.WriteLine`（`outputGate` 锁内） | A：`PptComDiagnostics.Enabled`（= 开关） | trace | PptCOM | `[PptComLife] v=1 seq= pid= tid= qpc= event= ...` |
| `PptPerfTrace.cs:126`（`Emit`） | Begin/End/Instant | `TextWriter.WriteLine` → `Console.OpenStandardError()`（138） | B：`INKEYS_PPT_PERF` | trace | PptCOM | `[PptPerf] v=1 source=managed ...` |
| `PptCOM.cs:2010-2069`（11 行） | `DiagnoseComStatus()` | `Console.WriteLine` | 死代码（无调用者） | — | PptCOM | `COM 对象存活诊断` emoji 文本 |
| `PptCOM.cs:244, 245, 251, 252, 258, 259` | `SetConsoleOutputEnabled` | `Console.SetOut/SetError/OpenStandard*` | I | — | — | 开关实现 |

说明：
- `PptComDiagnostics.QueueOfficeEvent`（`.cs:68-80`）在 Office COM 回调中只入队纯值（上限 64，超出计 `droppedOfficeEvents`），输出在别处 drain；注释“控制台锁和写入不得阻塞 Office 的关闭通知”。
- 所有 `Console.WriteLine` 均包在 `try {} catch {}` 中或位于 finally 内，注释强调“诊断失败不能中断 COM 根引用清理/释放路径”。
- `PptCOM.Tests/*.cs` 是独立测试工程，未纳入本清单。

---

## 9. 现有 `IDTLogger` / `UI3Diagnostics` 调用文件与前缀

| 文件 | 调用数 | 现有手写前缀（出现次数） | 级别分布 | 门控 | 建议模块标签 |
|---|---:|---|---|---|---|
| `Inkeys/IdtMain.cpp` | 39（+3 配置） | `[主线程][IdtMain]` 35；`[退出监督]` 2（419/422）；`"{}"` 2（1423/1424，消息体自带 `[Draw3Topmost]`） | info/warn/error/critical | 255/267/276 为人工测试（harness + 两个环境变量） | Main（启动/退出主流程）、Launch（255/267/276 重试）、Shutdown（`[退出监督]`）、Window（`[Draw3Topmost]`）、Config（3430/4289 main.json 写入）、PptCOM（3564/3607 资源验证/激活） |
| `Inkeys/IdtMagnification.cpp` | 12 | `[放大API线程][MagnifierThread]` 8；`[放大API线程][SetupMagnifier]` 4 | info/warn/error | — | Magnifier |
| `Inkeys/IdtFreezeFrame.cpp` | 1 | `[定格线程][FreezeFrameWindow]`（112） | error | — | Freeze |
| `Inkeys/IdtPlug-in.cpp` | 17 | `"{}"`（85，消息自带 `[PptPerf]`）；`[PptNav]` 1；`[PPT]` 5（717/977/1622/1662/1665）；`[PptExitSurface]` 1；`[PptGate]` 5；`[PPT 线程][PPTLinkageMain]` 1（1530）；`[DesktopDrawpadBlocker]` 1（1887）；`[DesktopShortcut]` 2（2029/2040） | debug/info/warn/error | PptPerf/PptNav/PptGate/PptExitSurface 为环境变量门控；717 为 `debug`，在当前 `set_level(info)` 下永不写入 | Ppt；Plugin（DesktopDrawpadBlocker）；Launch 或 Plugin（DesktopShortcut） |
| `Inkeys/IdtState.cpp` | 8 | `[Workspace]` 1（495）；`[状态线程][Draw3Gate]` 2（858/882）；`[状态线程][ReconcileDraw3Presentation]` 2（913/1264）；`[PptExitSurface]` 3（963/981/1002） | info/warn | PptExitSurface 为 `INKEYS_PPT_EXIT_TRACE`；Draw3Gate 按原因变化去重 | State（或 Whiteboard/Workspace）、Draw3（Draw3Gate/Reconcile）、Ppt（PptExitSurface） |
| `Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp` | 17 | `[BarUIRendering::<函数名>]`（每处不同函数名） | error 16 / warn 1 | — | Bar/UI3 |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp` | 2 | `[BarUISetClass::Rendering]`（9361/12625） | error | 12625 有“只记一次”标志 | Bar/UI3 |
| `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp` | 5 | `[UI3]` 3（1609/2104/8639）；`[BarUISetClass::ScheduleBorderCursorGraceTimer]` 1（8927）；`[UiPointerDrag]` 1（9702） | error/info | 9702 无开关，500ms 节流，拖动期间持续写入 | Bar/UI3；Input（UiPointerDrag） |
| `Inkeys/Inkeys/UI/Bar/Bar.Initialization.cpp` | 2 | `[KeyboardShortcuts]`（239）；`[MouseHook]`（243） | warn | — | Input（或 Bar/UI3） |
| `Inkeys/Inkeys/UI/Bar/Bar.KeyboardShortcuts.h` | 1 | `[KeyboardShortcuts]`（329） | warn | — | Input |
| `Inkeys/Inkeys/UI/Setting/Setting.cpp` | 4 | `[Setting]` 2（215/5583）；`[SettingEntryPerf]` 2（296/306） | error/info | SettingEntryPerf 为 `INKEYS_SETTING_ENTRY_PROFILE` | Setting |
| `Inkeys/Inkeys/Window/Window.Legacy.cpp` | 5 | `[窗口线程][TopWindow]`（56/68/99/108/123） | info/warn | — | Window |
| `Inkeys/Inkeys/Helper/Helper.SecRandom.cppm` | 1 | `[SecRandom]`（78） | warn | — | Main（或新增 Helper/Security） |
| `UI3Diagnostics`（`IdtMain.cpp:3253-3264` 定义；消息由 `UI/RenderPipeline/RenderPipeline.cpp:121` 组装） | 1 sink | `[UI3Diag] batches= ...` | 统一 `warn`（logger level=warn, flush_on=warn） | `discard_new` 溢出策略；`EmitDiagnostics` 调用于 `RenderPipeline.cpp` 716/805/896/916 | Bar/UI3（或 RenderPipeline） |

前缀风格归纳：
1. `[<中文线程名>][<函数/类名>]`：`[主线程][IdtMain]`、`[放大API线程][...]`、`[定格线程][...]`、`[状态线程][...]`、`[窗口线程][TopWindow]`、`[PPT 线程][PPTLinkageMain]`。
2. `[<类::函数>]`：`[BarUIRendering::Xxx]`、`[BarUISetClass::Xxx]`。
3. `[<子系统/事件>]`：`[PPT]`、`[UI3]`、`[Setting]`、`[Workspace]`、`[KeyboardShortcuts]`、`[MouseHook]`、`[SecRandom]`、`[DesktopShortcut]`、`[DesktopDrawpadBlocker]`、`[退出监督]`。
4. 结构化 trace（`key=value`，外部脚本解析）：`[PptPerf]`、`[PptNav]`、`[PptGate]`、`[PptExitSurface]`、`[SettingEntryPerf]`、`[UiPointerDrag]`、`[Draw3Topmost]`、`[UI3Diag]`。
5. 无前缀：`"{}"` 透传（IdtMain 1423/1424、IdtPlug-in 85）。

当前 logger 配置（`IdtMain.cpp:3242-3250`）：`basic_file_sink<std::mutex>`、`init_thread_pool(8192, 64)`、`async_overflow_policy::block`、`set_level(info)`、`set_pattern("[%l][%H:%M:%S.%e]%v")`、`flush_on(info)`；没有显式 `spdlog::shutdown()`。

---

## 10. 热路径调用点标记

| 线程/路径 | 调用点 | 当前机制 |
|---|---|---|
| **Draw3 绘制/输入线程**（`DrawingController::Run`） | 5.5 表中 `Run` 内全部行（约 60 行）；`ShapeRuntimeActions.inl` 4 行；`HapticDiagnostics::Log` 11 处调用；`LogCanvasPan` 24 处调用；`RecordCursorDiagnostic` 7 处调用；`LogFrameTiming`（每帧调用，`_DEBUG`+250ms 节流）；`TracePptTiming`（18252/20795） | 全部经 DiagnosticOutput 非阻塞入队；`FlushCursorDiagnostics` 注释“只尝试通知，不在该线程执行任何输出” |
| Draw3 present 路径 | `TransparentPresentation.cpp` 289/325/511/1434/1449/1471；`RendererPrimitives.cpp:398`（每秒一次）；`Host.cpp:1062`（`ObservePresented`） | DiagnosticOutput |
| **RTS 同步回调线程** | `RealtimeStylus.cpp` 中 `LogCanvasPan` 959/964/1410/1528、`RecordCursorDiagnostic` 454/1679/1725、`RecordRtsCallback` 1669（B） | DiagnosticOutput；RTS trace 注释“同步回调绝不等待；并发写入时丢弃诊断样本” |
| **Draw3 WndProc / 窗口线程** | `WindowControl.cpp` 中 `RecordCursorDiagnostic` 24 处、`LogCanvasPan` 7 处、`HapticDiagnostics::Log` 3 处、`[CURSOR_TRACE]` 2047/2074/2223（B）；`Product.cpp:385/389`（DrawpadMsgCallback 异常兜底） | DiagnosticOutput |
| **UI 窗口 WndProc**（Bar / PageControl / Whiteboard 等） | `UiPointer::Diagnostics::Raw/Log/...`（UiPointerInput.cpp、Bar.Interaction.cpp、PageControl.cpp 共 70+ 处，Haptics 开关）；`RecordCursorRawInput`（`Bar.Interaction.cpp:3386`，WM_INPUT）；`HapticDiagnostics::Log`（Bar.Interaction.cpp 6 处） | DiagnosticOutput；Move 按 250ms/槽限频 |
| **PageControl 指针处理** | `TraceDrag`（`PageControl.cpp:203/204`，13 个调用点，含 `constraint`（1916）等拖动中路径） | **同步** `fputs(stdout)` + `OutputDebugStringA`，**无门控、无队列** |
| PageControl 渲染回调 | `TraceSurfaceStage`（2224/2225/2236，env 门控 + 2s 去重） | 同步 stdout + ODS |
| Whiteboard follow 渲染 | `Whiteboard.cpp:128`（env 门控，平移片段结束时汇总一次） | DiagnosticOutput |
| Window owner 线程 | `Window.cpp:118`（WriteWindowDiagnostic）、1117（ClickThrough 边沿） | DiagnosticOutput；注释“标准流/debugger 背压不可阻塞安全隐藏”“不能在窗口 owner 同步写标准流” |
| Bar 拖动 | `Bar.Interaction.cpp:9702` `[UiPointerDrag]`（IDTLogger，500ms 节流） | 当前 IDTLogger 为 `block` 溢出策略 |
| UI3 渲染管线 | `UI3Diagnostics`（`discard_new`） | 不阻塞渲染线程（`IdtMain.cpp:3252` 注释） |
| Office COM 回调（C#） | `PptComDiagnostics.QueueOfficeEvent` | 只入队纯值 |

---

## 11. 依赖 `GetLastError()` 顺序的调用点

| 位置 | 现状 |
|---|---|
| `Draw3.DiagnosticOutput.cpp` 全部入口 | `LastErrorGuard`；`Line` 构造保存/析构恢复；自检断言保持 LastError（`.cpp:541-543`） |
| `Draw3.Diagnostics.cpp:657-685` `SetCursorDiagnosticsEnabled` | 保存 `previousError`，`GetRegisteredRawInputDevices` 失败时把 `GetLastError()` 作为参数传给 `RecordCursorDiagnostic`，最后 `SetLastError(previousError)` |
| `Draw3.Diagnostics.cpp:692-717` `RecordCursorRawInput` | 同上（`GetRawInputData` 失败时取 `GetLastError()`） |
| `Draw3.Diagnostics.cpp:719-744, 816-864` `RecordCursorDiagnostic` / `SetCanvasPanDiagnosticsEnabled` / `LogCanvasPan` | 保存并恢复 LastError；自检 `IdtMain.cpp:1827-1829` 断言 `LogCanvasPan` 后 LastError 仍为 `ERROR_INVALID_DATA` |
| `Input/UiPointerDiagnostics.h` | `ErrorScope` 保存/恢复；`WindowSnapshot` / `PointerDownSnapshot` 先 `SetLastError(ERROR_SUCCESS)` 再读 `GetLastError()`；`Probe::Requested` 读环境变量前后保存/恢复 |
| `ShutdownProgressTrace.h` | 每个函数都有 `LastErrorGuard`（注释：首次读开关、取时钟和写 stderr 都不能覆盖业务/Arm 刚产生的 Win32 错误） |
| `IdtMain.cpp:386-388, 418-422` `SetOffSignal` | `ArmShutdownSupervisor` 返回后立即锁存 `supervisorError = GetLastError()`，随后用于 `ShutdownTrace::CaptureEnd` 与 `[退出监督]` 日志 |
| `PptWaitingDiagnostics.h:44-48, 58, 104, 127-133` | `LastErrorGuard` |
| `Window/Window.cpp:111-120` `WriteWindowDiagnostic` | 保存/恢复 `savedError` |
| `Window/Window.cpp:1705-1722` | `frameError` 先锁存，再 ODS + `fwprintf` |
| `Draw3.TransparentPresentation.cpp:1100-1116` | `initializeError = GetLastError()` 先于 `beginFailure()` 与输出（1104/1114 输出 `lastError=`） |
| `Draw3.WindowControl.cpp:660, 716` | `LogWin32Error` 传入调用方已锁存的错误码 |
| `Draw3.HapticFeedback.cpp:192` | `diagnosticError = GetLastError()` 后再记录 |
| `Draw3.StrokeGeometry.cpp:75-80`（B） | `CreateFileW` 失败后立即两次读 `GetLastError()` 判断 EXISTS |
| `Draw3.PresentationAutoSave.cpp:626-633`（D） | 注释：使用 UInk result 的真实错误，不能拿 caller 的 stale LastError |
| `Helper.CrashHandler.cpp:494-500, 690-697` | 先锁存 `error/createError` 再输出；697 输出后 `SetLastError(createError)` |
| `IdtMain.cpp:186-189` | `replacementError` 锁存并恢复（非输出，但相邻启动逻辑） |
| D 桶：`IdtSingleInstance.h:483, 503`；`Ui3PresentationFixtureAuth.cpp:734, 772` | `GetLastError()` 直接写在 `fprintf/printf` 实参中 |

相关规范（`errors-logging-and-resources.md:197-198`）：`IDTLogger` 使用 block 溢出策略，fatal 日志本身可能等待；已确定失败时必须先 `SetOffSignal/Arm` 再写日志；D005 失败需在失败 API 后立即锁存 `GetLastError`。

---

## 12. Related Specs

- `.trellis/spec/native-desktop/errors-logging-and-resources.md` — 现有日志初始化/清理描述（21-31）、退出监督与“先 Arm 后日志”合同（133-136、187-230、313-354）、日志政策待确认（300）
- `.trellis/spec/native-desktop/ui3-render-diagnostics.md` — UI3Diagnostics / RenderPipeline 诊断
- `.trellis/spec/native-desktop/pen-haptics.md` — `[PenHaptics]` 诊断
- `.trellis/spec/native-desktop/canvas-pan.md`、`canvas-pan-release-quality.md` — `[CanvasPan]` 诊断
- `.trellis/spec/native-desktop/draw3-cursors.md` — `[CURSOR_TRACE]`
- `.trellis/spec/native-desktop/ui-pointer-session.md` — UiPointer 诊断
- `.trellis/spec/native-desktop/palm-eraser.md`、`input-and-ink.md` — TouchArea/RTS 诊断
- `.trellis/spec/native/terminal-tail-reconstruction.md` — `INKEYS_DRAW3_TAIL_TRACE`
- `.trellis/spec/native/quality-and-validation.md` — 自检/测试协议
- `.trellis/spec/ppt-interop/com-contract.md`、`native-session-ui3.md` — PptCOM 接口与 PptPerf/PptGate
- `.trellis/spec/native-desktop/configuration-i18n-and-assets.md`、`settings-*.md` — ConsoleOutput 配置与设置页

## 13. Caveats / Not Found

- `observer_.penDiagnostics`（`DrawingController.cpp:17179` 等）是否由 Host 恒定注册**未进一步核实**；17183 暂按 C 归类。
- 级别（trace/debug/info/warn/error）为按文本语义给出的建议，不是代码中已有的字段；`LogHResult/LogWin32Error` 的实际严重度取决于调用点（RTS 初始化可恢复尝试 vs 致命失败）。
- “C 桶”的 DiagnosticOutput 行在当前产品中依赖 `Available()`（控制台/重定向/调试器）才可见；它们不是“当前用户日志里能看到”的输出。
- `Renderer.cpp:784` 直接读取 `INKEYS_DRAW3_WIN7_DIAGNOSTICS` 且没有 `#ifndef IDT_RELEASE`，与 `IdtMain.cpp:3372` 的非 Release 限制不同（仅记录现状）。
- `PptCOM.cs:2010-2069` `DiagnoseComStatus()` 全仓无调用者。
- `InkeysHeadlessTests` 工程编译若干含输出的产品源文件但不含 `IdtMain.cpp`（见 7.2）。
- 未扫描：`Inkeys/additional/`（第三方）、`PptCOM.Tests/`、`InkeysHeadlessTests/` 测试源、构建产物目录。
- `Trace.*` / `Debug.Write*`（C#）、`std::print/println`、`fmt::print`、`WriteConsoleW`、`_putws`：**未发现**。
