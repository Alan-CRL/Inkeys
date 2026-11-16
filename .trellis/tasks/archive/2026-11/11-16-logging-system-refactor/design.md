# 第0步 日志系统重构：技术设计

> 需求与验收见 `prd.md`；现有输出点清单见 `research/console-output-inventory.md`。

## 1. 总体结构

```text
C++ 业务代码 ── Inkeys::Log::Info/Warn/...(Module, fmt, args) ─┐
UI3 RenderPipeline 诊断 sink ── TryWrite(Module::UI3, Warn) ───┤
Draw3::DiagnosticOutput::Printf/Text（API 不变，后端改写）─────┤  调用线程：
PptCOM (C#) ManagedLog ─ 原生函数指针回调 ─ PptComLogBridge ──┤  过滤 → 格式化消息体 → 拷入前台缓冲（无 I/O）
                                                               ▼
                                   LogService 双缓冲（SRWLock，仅保护 memcpy）
                                                               │ 交换缓冲（O(1)）
                                                               ▼
                                   唯一写线程：解析记录 → spdlog 同步 sink（_st，无锁）
                                   → <EXE>\Inkeys\Logs\YYYY-MM-DDTHH-MM-SS.log
```

- 唯一写入者：进程内只有 1 个日志线程，持有唯一文件句柄。
- 保留 spdlog 1.17.0：只在写线程使用 `spdlog::sinks::basic_file_sink_st` + pattern formatter 负责文件打开与行格式化；不再使用 spdlog 的 async thread pool（其队列每次入队都要 mutex + condition variable，并且原配置是 64 worker）。不升级 spdlog/fmt。
- 删除全局 `IDTLogger`、`UI3Diagnostics` logger、`spdlog::init_thread_pool`、`InitializeDebugConsole()` / `AllocConsole`。

## 2. 新模块与对外接口

位置：`Inkeys/Inkeys/Log/Log.h`（头文件，供旧式 `.cpp` 与模块的全局模块片段 include；与 `Draw3.DiagnosticOutput.h` 的用法一致）+ `Log.cpp`。工程项按 `directory-structure.md` 与 filters 规则加入 `Inkeys.vcxproj(.filters)`，并同时加入 `InkeysHeadlessTests.vcxproj`（该工程直接编译 `Draw3.DiagnosticOutput.cpp`、`Window.cpp`、`UiPointerInput.cpp`、`RenderPipeline.cpp` 等会调用日志的产品文件）。`Log.cpp` 不依赖 `IdtMain.cpp` 的全局变量；会话头所需的版本、userId、路径等由调用方通过 `SessionInfo` 传入。

```cpp
namespace Inkeys::Log
{
    enum class Level : std::uint8_t { Debug, Info, Warn, Error, Critical };
    // 设置页档位：Debug/Info/Warn/Error/Off；Off 只关闭普通分级日志。
    enum class Threshold : std::uint8_t { Debug, Info, Warn, Error, Off };

    // 模块标签：只表达“哪个子系统”，不再手写 [主线程][IdtMain] 前缀。
    enum class Module : std::uint8_t {
        Main, Launch, Config, I18n, Window, UI3, Bar, Setting, Whiteboard,
        PageControl, SelectionOverlay, Freeze, Magnifier, Ppt, PptCOM,
        Draw3, Input, Haptics, Update, Shutdown, /* 以清单为准增删 */ };

    // 调试通道：对应设置页 6 个开关，开启即写入，不受 Threshold 过滤。
    enum class Channel : std::uint8_t { TouchArea, PptCOM, Draw3, Haptics, Cursor, CanvasPan };

    bool Enabled(Level) noexcept;            // 原子读阈值，未通过时不格式化
    bool ChannelEnabled(Channel) noexcept;   // 原子读掩码
    void SetThreshold(Threshold) noexcept;   // 设置页热更新
    void SetChannelMask(std::uint32_t) noexcept; // 启动时由配置设置

    template <class... Args> void Debug(Module, fmt::format_string<Args...>, Args&&...);
    // Info / Warn / Error / Critical 同形；Critical 不受阈值影响。
    template <class... Args> void Trace(Channel, Module, Level, fmt::format_string<Args...>, Args&&...);

    // 热路径：非阻塞，拿不到锁或缓冲满即丢弃计数，返回是否写入。
    bool TryWrite(Level, Module, std::optional<Channel>, std::string_view) noexcept;

    bool Start(const SessionInfo&) noexcept;  // 建目录、清理、打开文件、写会话头、启动写线程
    void FlushAndStop() noexcept;             // 正常退出：排空并落盘
    void EmergencyFlush() noexcept;           // 崩溃处理：有界时间内尽力排空
    std::wstring Directory();                 // 设置页“打开日志文件夹”
    void ApplyRetention(int days, int maxSizeMb) noexcept; // 设置修改后触发一次（投递到写线程）
}
```

- 消息体统一 UTF-8；提供 `std::wstring_view` 便利重载（内部 `utf16ToUtf8`）。
- 调用线程只做：阈值/通道检查 → `fmt::format_to` 到栈上 `fmt::memory_buffer`（512B 内联，超长才分配）→ 拷入前台缓冲。时间戳（`std::chrono::system_clock::now()`，MSVC STL 在 Win7 自动回退非 Precise 版本）与线程 ID（`GetCurrentThreadId()`）在调用线程采集，保留事件原始发生信息。
- 所有入口 `noexcept`，内部吞掉格式化/分配异常并计数；日志永远不能改变调用方业务结果。Draw3 入口继续保留 `LastErrorGuard`。

## 3. 同步与性能设计（核心）

### 3.1 双缓冲 + SRWLock

- 两块固定字节缓冲（每块 2 MiB，启动时一次分配），记录为变长 `[RecordHeader | text]`，按 8 字节对齐：
  `RecordHeader { u32 size; u8 level; u8 module; u8 channel(0xFF=无); u8 flags; u32 threadId; i64 timeTicks; }`。
- 生产者：`AcquireSRWLockExclusive` → 检查剩余空间 → `memcpy` → 释放。临界区内只有 memcpy（百字节级，几十纳秒），**没有 I/O、没有分配、没有格式化**。
- 写线程：`AcquireSRWLockExclusive` → 交换前后台指针 → 释放（O(1)）；之后在锁外解析后台缓冲、写文件。生产者永远不会等待磁盘。
- 唤醒：`WaitForSingleObject(wakeEvent, 1000ms)`。生产者在写入 Warn 及以上、或前台缓冲过半时 `SetEvent`；其余靠 1 秒超时批量处理，避免每条日志都触发一次线程切换。
- 选择理由：相比无锁 MPSC 环形队列，双缓冲天然支持变长记录、实现简单、易验证；单写者下 SRWLock 争用窗口极短。Win7 支持 SRWLock（Vista+）。

### 3.2 满缓冲与热路径策略（永不阻塞调用方）

| 调用方 | 取锁方式 | 缓冲满时 |
| --- | --- | --- |
| 普通业务日志 Debug/Info | `AcquireSRWLockExclusive`（只等 memcpy，不等 I/O） | 使用前 75% 空间；超出即丢弃并计数 |
| 普通业务日志 Warn+ | 同上 | 可用全部空间（保留 25% 给高等级）；仍满则丢弃并计数 |
| 热路径：Draw3 绘制/输入、RTS 回调、`DiagnosticOutput`、UI3 诊断 sink、C# 回调 | `TryAcquireSRWLockExclusive` | 拿不到锁或空间不足立即丢弃并计数，不自旋 |

- 丢弃计数按等级分别累计；写线程在下一批次开头写一行 `Warn [Log] dropped debug=… info=… warn=… error=…` 并清零，让查看者知道日志不完整。
- 这延续了 `Draw3::DiagnosticOutput` 现有“try-lock 入队、满则丢弃计数”的保护语义，同时把两个队列合并为一个。

### 3.3 落盘

- 每批次写完后：批次含 Warn 及以上 → 立即 `flush`；否则距上次 flush ≥ 1s 时 `flush`。
- `FlushAndStop()`：停止接受新记录 → 唤醒写线程 → 排空两块缓冲 → flush → join。放在 `wWinMain` 退出路径中、所有可能写日志的线程 join 之后（窗口服务、Draw3 Host、PPT 联动之后），并在 PptCOM 撤销回调之后。
- `EmergencyFlush()`：CrashHandler 在写 dump 后调用；用 `TryAcquire` + 有界等待（≤ 500ms），失败就放弃，绝不在崩溃路径死锁。CrashHandler 自身的 dump / 崩溃报告文件保持独立通道不变。
- `ShutdownSupervisor` 的强制 `TerminateProcess` 回退路径不保证落盘（已知风险，记录在 PRD）。

### 3.4 启动前缓冲

缓冲在静态初始化时即可用；`Start()` 之前的日志照常写入前台缓冲，`Start()` 写完会话头后由写线程一并落盘，避免早期启动日志丢失。若 `Start()` 失败（目录不可创建/文件不可写），服务进入“丢弃模式”：继续接受调用但不写文件，并通过 `OutputDebugStringW` 输出一次失败原因；不弹窗、不影响启动。

## 4. 文件、命名与行格式

- 目录：`globalPath + L"Inkeys\\Logs"`，不存在则创建（`create_directories`）。不读取、不创建、不删除旧 `log\`。
- 文件名：启动时本地时间 `YYYY-MM-DDTHH-MM-SS.log`；已存在则依次尝试 `_1`、`_2`…。清理只匹配正则 `^\d{4}-\d{2}-\d{2}T\d{2}-\d{2}-\d{2}(_\d+)?\.log$`。
- 行格式（spdlog pattern，写线程设置 `log_msg.time / thread_id / logger_name`）：

```text
[2026-11-16 14:32:05.123][INFO ][Ppt][T4812] 放映开始 session=3
[2026-11-16 14:32:05.130][DEBUG][Draw3][T5120][Cursor] cursor size=48 …
```

  `[%Y-%m-%d %H:%M:%S.%e][%-5l][模块][T线程ID]`；调试通道记录追加 `[通道名]`，便于 grep。等级用大写英文。
- 会话头（`Start()` 写入，位于文件最前，不受阈值影响）：

```text
==== Inkeys session start ====
time=2026-11-16T14:32:05 timezone=UTC+08:00
edition=<editionDate> code=<editionCode> channel=<Config.Channel> build=<Debug|Release>
arch=<x64|arm64|x86> os=<RtlGetVersion 主/次/Build, 产品名> pid=<pid> userId=<userId>
path=<globalPath> launch=<启动来源/crashTry 等已有状态>
log.threshold=<Info> log.channels=<Draw3,Cursor…> retention.days=<7> retention.maxMB=<0>
==============================
```

  字段只取当前代码中已可获得的值（`editionDate`、`userId`、`Config.Channel`、`Net.Update.cpp` 上报所用的 arch 判定等），不新增遥测采集逻辑。

## 5. 清理策略

- 输入：`RetentionDays`（0=永不）、`MaxSizeMB`（0=无限制）。
- 步骤：枚举匹配文件 → 排除当前会话文件 → 按文件名时间排序（文件名即时间，解析失败的跳过） → 删除超过天数的 → 若总量（含当前文件）仍超限，按最旧优先继续删除直到满足或只剩当前文件。
- 执行时机：`Start()` 内一次；设置页修改两项后 `ApplyRetention()` 投递给写线程执行一次（不在 UI 线程做文件遍历）。
- 删除失败（被占用）忽略并在日志中记一行 Debug。

## 6. 配置与设置页

### 6.1 配置项（`Other.Config.cppm` 新增组，NoUpload）

```text
GROUP(Logging,
    X(int, RetentionDays, 7)    // 0 = 永不清理；自定义范围 1..3650
    X(int, MaxSizeMB, 0)        // 0 = 无限制；自定义范围 1..102400
    X(int, Threshold, 1)        // 0 Debug,1 Info,2 Warn,3 Error,4 Off
)
```

读配置时对越界值收敛到默认值。具体放在哪个父组、命名方式按 `configuration-i18n-and-assets.md` 与现有 schema 习惯落实；旧配置完全不兼容策略不变，不写迁移代码。

### 6.2 “日志与配置 → 日志”子页（`Setting.Pages.cpp` 的 `Section::LogsLog`）

| 行 | 控件 | 选项 |
| --- | --- | --- |
| 日志输出等级 | `ComboRow` | 调试 / 信息 / 警告 / 错误 / 关闭（默认信息），修改立即生效 |
| 自动清理时长 | `ComboRow` + 自定义时出现数字输入 | 1 天 / 3 天 / 7 天 / 14 天 / 30 天 / 永不清理 / 自定义 |
| 日志大小上限 | `ComboRow` + 自定义时出现数字输入（单位 MB） | 50 / 100 / 200 / 500 / 1024 MB / 无限制 / 自定义 |
| 打开日志文件夹 | `ButtonRow` | `QueueShellExecute(Log::Directory())` |

- 自定义输入：在输入框失焦/回车时校验（整数、在范围内）；不合法恢复为修改前的值。合法则写配置并 `ApplyRetention`。
- 草稿、事件与 `QueueConfigWrite()` 接线沿用通用页现有模式（`Setting.cpp:2881` 一带）。
- 新增 i18n 键写入 `zh-CN.jsonc` 等语言文件并重新生成 `IdtI18nKeys.g.h`（按现有 i18n 规范）。
- “调试软件 → 控制台”分区 6 个开关位置、配置键、“需重启”标记不变；只更新说明文案（不再提“控制台窗口”，改为“写入日志”）。分区标题文案是否改名在实现时按 i18n 现状最小调整。

## 7. 调试通道迁移规则

- 删除 `InitializeDebugConsole()` 及两处调用；启动时把 6 个开关（以及非 Release 的 `INKEYS_DRAW3_WIN7_DIAGNOSTICS`）合成通道掩码 `SetChannelMask()`。
- `Draw3::DiagnosticOutput`：对外 API（`Printf/Text/Line/Write/Wake/Session`）全部保留，内部分两种模式：
  - **日志模式（产品默认）**：`Write` 直接调用 `Log::TryWrite`（非阻塞、带调用线程的时间戳与线程 ID），不经过它自己的队列和消费线程；`LastErrorGuard` 保留。`Session::Available` 改为依据日志服务状态与对应通道，不再看 stdout/stderr/调试器。
  - **标准流模式（仅测试）**：`runDraw3SelfTest`、`--draw3-diagnostic-output-test` 等测试入口显式以标准流模式启动 `Session`，沿用现有定长队列、后台消费、写 stdout/stderr 与字节校验逻辑，测试协议与 `RunProductionTest` 断言不变。`[Draw3.AutoSave.TestIo]`、`[Draw3.Presentation.TestIo]` 在测试模式下同样走标准流。
  - `Stream::Error` 默认映射为 Warn、`Stream::Output` 默认映射为 Debug；C 类调用点再按清单逐条复核等级（真实失败 Warn/Error，高频跟踪保持 Debug）。
- 归类规则（逐点映射见调研清单）：
  - 调用点已受某个开关控制 → `Trace(Channel::X, …)`，开启即写入，不受阈值影响。
  - 环境变量/编译宏控制的专项诊断（PptPerf、tracePptGate 等）→ 普通 `Debug` 级 + 原有门控保留。
  - 无门控的生产诊断 printf/cout → 按语义改为分级日志（多数为 Debug）。
  - 测试工具（*.Test / Probe / Fixture）stdout PASS/FAIL 协议 → **不动**。
  - CrashHandler / ShutdownSupervisor 的应急输出 → 保留原通道，可额外尝试写日志，但不能依赖日志服务。
  - `[ShutdownTrace]`（`ShutdownProgressTrace.h`）与退出时的 `[PptWait]`（`PptWaitingDiagnostics.h`）→ **保留直写 stderr**：它们发生在 `Log::FlushAndStop()` 之后的退出阶段，且 `SetOffSignal` 中有“Arm 之前不做诊断 I/O、Arm 后立即锁存 `GetLastError()`”的顺序约束（清单 §11）。
  - `PageControl.cpp` `TraceDrag`（203/204，当前在指针处理里同步写 stdout + `OutputDebugStringA`、无门控）→ 改为 `Debug` 级 `TryWrite`，移除同步标准流写入。`TraceSurfaceStage` 等环境变量门控的同类函数同样改为 `TryWrite`。
  - `Renderer.cpp:784` 直接读 `INKEYS_DRAW3_WIN7_DIAGNOSTICS`（Release 也生效）：保持现有门控行为不变，只改输出目标。
- 热路径调用点（RTS 回调、绘制线程、WndProc）只允许使用 `TryWrite` 系列；现有约 250ms 帧级节流等稀疏化逻辑保留。

## 8. 现有 IDTLogger 调用迁移

- 114 处调用（13 个文件，前缀与建议模块见清单 §9）改为 `Inkeys::Log::Xxx(Module::Y, …)`；去掉手写 `[主线程][IdtMain]`、`[BarUIRendering::Xxx]`、`[PPT]`、`[UI3]` 等前缀，模块由 `Module` 表达；原文中有价值的“线程/函数”信息只在确有诊断意义时保留在消息里。
- **结构化 trace 标签保留在正文**：`[PptPerf]`、`[PptNav]`、`[PptGate]`、`[PptExitSurface]`、`[SettingEntryPerf]`、`[UiPointerDrag]`、`[Draw3Topmost]`、`[UI3Diag]` 等 key=value 行被脚本/人工按标签检索，标签与字段格式不变，只在前面加统一标头。
- `IdtPlug-in.cpp:717` 原为 `debug` 级（旧 `set_level(info)` 下从不写入），迁移后保持 Debug。
- `Bar.Presentation.Test.cpp:685` 的 fixture 替换全局 `IDTLogger`：改为调用日志服务的测试钩子（如把阈值设为 Off 或启用“丢弃模式”），不改变该测试的断言。
- 等级按语义复核：启动里程碑 Info；可恢复失败 Warn；功能失败 Error；进程无法继续 Critical。`if (IDTLogger)` 判空全部删除（新接口在任何阶段可调用）。
- `IdtPlug-in.cpp` 中 `L" (0x" + std::to_wstring(err.Error()) + L")"` 假十六进制：迁移到该调用点时顺带改为真正的 `0x{:08X}`（同一行日志的规范化，属于本步范围）。
- `RenderPipeline::SetDiagnosticsSink` 的 sink 改为 `Log::TryWrite(Warn, Module::UI3, …)`，返回值语义（是否被丢弃）保持不变。

## 9. PptCOM（C# / .NET Framework 4.0）接入

### 9.1 方案：原生函数指针回调，经新增 COM 扩展接口传递

```csharp
// PptCOM.cs：独立扩展接口，QueryInterface 取得，不改 IPptCOMServer vtable
[ComVisible(true)]
[InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
[Guid("<新 GUID>")]
public interface IPptCOMLogging
{
    void SetLogCallback(IntPtr callback);   // IntPtr.Zero = 撤销
}

[UnmanagedFunctionPointer(CallingConvention.StdCall, CharSet = CharSet.Unicode)]
internal delegate void NativeLogCallback(int level, int module, string text, int length);
```

```cpp
// C++：静态函数，生命周期与 exe 相同，不存在悬垂指针
extern "C" void __stdcall PptComLogCallback(int level, int module, const wchar_t* text, int length) noexcept;
```

- C# 新增 `ManagedLog` 静态类：`Marshal.GetDelegateForFunctionPointer(IntPtr, typeof(NativeLogCallback))`（.NET 4.0 可用的非泛型重载）；delegate 存静态字段（它包装的是原生函数，不需要防 GC 回收托管回调）；`Volatile` 读写；`Write` 内部 try/catch 全吞，绝不改变 COM 业务返回值或 HRESULT。
- 未注册（注入前或撤销后）时 `ManagedLog.Write` 直接丢弃（控制台已取消，不再保留 Console fallback；PRD 中已决定彻底去掉控制台）。
- 原生回调：校验长度 → UTF-16 转 UTF-8 → `TryWrite`（非阻塞）；不重入 Office COM、不等待 UI。
- 选择理由：相比“C++ 实现 COM sink 接口传给 C#”，函数指针不涉及套间与 proxy/stub 封送（自定义 IUnknown 接口没有注册 typelib 封送，STA 线程调用会失败），开销也更低；仍通过新增扩展接口传递，符合 `com-contract.md` 的“扩展接口不插入旧 vtable”约定。
- 时序：C++ 在创建 PptCOM 对象、`SetConsoleOutputEnabled` 之后立即 `QueryInterface(IPptCOMLogging)` 并 `SetLogCallback(&PptComLogCallback)`；退出时在释放 PptCOM 前 `SetLogCallback(0)`，之后才 `Log::FlushAndStop()`。QueryInterface 失败只记一条 Warn，不影响 PPT 联动。

### 9.2 C# 输出迁移

- `Console.WriteLine`（`PptCOM.cs`、`ComEventConnection.cs`、`PptComDiagnostics.cs`，含无人调用的 `DiagnoseComStatus()`，机械替换以保证 PptCOM 内不再有 Console 输出）→ `ManagedLog.Write(level, module, text)`；module 取 `PptCOM`（`[PptOwner]`、`[PptComLife]` 等原前缀转成消息内子标签或 C# 侧子模块号，具体见清单）。
- 现有门控语义不变：这些输出仍受 `SetConsoleOutputEnabled`（即 PptCOM 调试开关）控制，作为 `Channel::PptCOM` 写入。`SetConsoleOutputEnabled` 方法签名与位置不变，内部不再重定向 `Console.Out/Error`。
- `PptPerfTrace` 不再 `Console.OpenStandardError()`，改走 `ManagedLog`（正文保留 `[PptPerf] source=managed …` 原格式），原有 `INKEYS_PPT_PERF` 门控保留，且不受 PptCOM 开关影响（与现状一致：它是独立的环境变量门控）。`[PptOwner]` 保持“开关 + `INKEYS_PPTCOM_TRACE`”双门控。
- 遵守 `com-contract.md`：诊断中不调用 `Marshal.GetHRForException`；不为日志额外取得 COM 对象；Office 事件回调中仍只入纯值队列。

## 10. 诊断脚本适配

| 脚本 | 现状 | 修改 |
| --- | --- | --- |
| `Scripts/Start-PptNavigationDiagnostic.ps1` | 设 `INKEYS_PPT_PERF/TIMING`，重定向子进程 stdout/stderr，复制 `<exe>\log\idt*.log` | 复制源改为 `<exe>\Inkeys\Logs\` 下本次运行新增的日志文件（按新命名匹配）；stdout/stderr 重定向保留 |
| `Scripts/Start-ShutdownDiagnostic.ps1` | 从 stderr 匹配 `[ShutdownTrace] v=1 source=native`，复制 `log\idt*.log` | `[ShutdownTrace]` 仍从 stderr 读取；日志复制源改为 `Inkeys\Logs` |
| `Scripts/analyze-ppt-navigation.py` | 扫描目录 `*.log`，按 `'[PptPerf]' in line` 后切分解析 key=value | 行首新增标头不影响解析；确认 C# `[PptPerf] source=managed` 现在位于日志文件内也能被扫描到（必要时把复制来的日志文件纳入扫描目录） |

只做路径与来源的最小修改，不重写脚本逻辑。

## 11. 兼容性

- Win7 SP1：SRWLock、`CreateEventW`、`WaitForSingleObject`、`_wfsopen`（spdlog file helper）、`RtlGetVersion`（ntdll 动态取址）均可用；不使用 `GetSystemTimePreciseAsFileTime` 等 Win8+ API 的静态导入。
- 不改 vcpkg baseline、spdlog/fmt 版本；PptCOM 仍为 .NET 4.0 AnyCPU。
- COM：只新增接口，`IPptCOMServer` / `IPptCOMSessionState` 不变；tlb 由解决方案构建重新生成，`#import` 自动获得新接口。

## 12. 主要风险与回退

| 风险 | 缓解 |
| --- | --- |
| 迁移 150 + 数百处调用时改坏格式化参数 | `fmt::format_string` 编译期检查；分批迁移、每批编译 |
| 退出顺序不当导致尾部日志丢失或写线程未 join | `FlushAndStop` 放在所有业务线程 join 之后；PptCOM 先撤销回调 |
| 崩溃路径死锁 | `EmergencyFlush` 只用 TryAcquire + 有界等待 |
| 高频通道（Cursor/Draw3）在 Debug 档压垮缓冲 | 热路径非阻塞丢弃 + 丢弃计数行；保留原有帧级节流 |
| 去掉控制台后外部采集脚本失效 | 已由用户确认改读日志文件；测试工具协议不受影响 |
| `TerminateProcess` 强制退出不落盘 | 已知限制，记录不处理 |
| DiagnosticOutput 模式切换破坏自检/字节校验 | 测试入口显式选择标准流模式；跑 `--draw3-diagnostic-output-test` 与 Draw3 自检 |
| C 类 152 行原本只在控制台可见，改写日志后默认可见的 Warn/Error 增多 | 按清单逐条定级，高频跟踪定为 Debug；Debug 档下靠非阻塞丢弃保护 |

回退：日志模块与调用点迁移按批次提交前可整体 revert；配置组为新增项，不影响旧字段。
