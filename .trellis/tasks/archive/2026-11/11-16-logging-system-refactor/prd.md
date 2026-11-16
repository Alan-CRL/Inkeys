# 第0步 日志系统重构

> 父任务：`11-16-inkeys3-midterm-refactor`。调研来源：父任务 `research/第0步 Inkeys3_日志系统与白板切换调查_2026-11-14.md`（基准 `215184d4`），本 PRD 中的现状已按当前分支 `refactoring/drawpad-ui`（`c06697b1`）重新核实。

## Goal

重建 Inkeys3 的统一日志框架：一个进程只有一个日志文件写入者，C++ 业务日志、调试诊断输出与 C# PptCOM 日志都进入同一文件；日志标头能看出时间、等级、模块；用户可在设置页控制保留时长与输出等级；不让日志拖慢绘制/输入热路径。本步只重建框架并规范现有日志，**不大规模扩展日志覆盖面**（白板切换失败诊断闭环等留给后续子任务）。

## 已确认的现状（代码证据）

- 初始化在 `Inkeys/IdtMain.cpp:3154-3274`：目录 `globalPath + L"log"`，文件名 `idt<毫秒epoch>.log`；`basic_file_sink<std::mutex>` + `spdlog::init_thread_pool(8192, 64)`（**64 worker 写同一文件**）+ `async_overflow_policy::block`；`flush_on(info)`（每条 info 都 flush）；pattern `[%l][%H:%M:%S.%e]%v`。
- 历史清理硬编码：7 天或目录 > 10 MiB，仅启动时执行，不按最旧优先（`IdtMain.cpp:3161-3237`）。
- `UI3Diagnostics` 第二个 async_logger 共用同一 sink/线程池，`discard_new`，经 `RenderPipeline::SetDiagnosticsSink` 接入（`IdtMain.cpp:3253-3264`）。
- 全局 `shared_ptr<spdlog::logger> IDTLogger`（`IdtMain.cpp:458`，`IdtMain.h:131`），114 处日志调用 + 3 处配置调用，分布于 13 个文件（`research/console-output-inventory.md` §9）；标头格式不统一：`[主线程][IdtMain]`、`[PPT]`、`[UI3]`、`[放大API线程][SetupMagnifier]`、无标头等混用。
- 其他路径约定：`globalPath` = EXE 目录；已有 `Inkeys\Config\main.json`（`Other.Config.cpp:1122`）、`Inkeys\AutoSave`（`IdtMain.cpp:4031`）、`Inkeys\Plugin`。新目录 `Inkeys\Logs` 与之同级。
- 调试控制台：设置页“调试软件 → 控制台”分区有 6 个开关（`Setting.cpp:4744-4773`）：TouchArea、PptCOM、Draw3、Haptics、Cursor、CanvasPan，对应配置 `Experimental.Inkeys3.ConsoleOutput.*`（`Other.Config.cppm:574-581`），均需重启生效；开启任一项时 `InitializeDebugConsole()` 调用 `AllocConsole`（`IdtMain.cpp:323-357, 3392-3397, 4220-4226`）；非 Release 构建另有 `INKEYS_DRAW3_WIN7_DIAGNOSTICS` 环境变量。
- `Draw3::DiagnosticOutput`（`Draw3.DiagnosticOutput.cpp/.h`）：非阻塞 try-lock 入队、定长约 1024 条×4096 字节、丢弃计数、后台消费、`LastErrorGuard`；`Stream { Output, Error }`；可用性按 stdout/stderr/调试器存在与否判断。另有仿 `cout` 的 `DiagnosticOutput::Line`（111 处）。
- 非 logger 输出共 658 行（清单 §1）：A 受 6 个开关控制 35、B 环境变量/编译宏 22、C 无门控产品诊断 162（其中 152 经 DiagnosticOutput）、D 测试工具协议 407（44 个文件）、E CrashHandler/ShutdownSupervisor 应急 21、I 输出基础设施 11。C# 侧：开关控制 11、`[PptOwner]` 1（开关 + `INKEYS_PPTCOM_TRACE`）、`PptPerfTrace` 1（`INKEYS_PPT_PERF`，直写 stderr）、死代码 `DiagnoseComStatus()` 11。
- 测试协议依赖 DiagnosticOutput 标准流：`runDraw3SelfTest` 为 30+ 自检启动输出会话；`--draw3-diagnostic-output-test` 校验管道实际字节与 LastError 保持；`[Draw3.AutoSave.TestIo]`、`[Draw3.Presentation.TestIo]` 协议行也走该通道（清单 §7.2）。
- `InkeysHeadlessTests.vcxproj` 直接编译 `Draw3.DiagnosticOutput.cpp`、`Window.cpp`、`UiPointerInput.cpp`、`RenderPipeline.cpp` 等产品文件（不编译 `IdtMain.cpp`）；`Bar.Presentation.Test.cpp:685` 在 fixture 中替换全局 `IDTLogger`。
- 外部诊断脚本 `Scripts/Start-PptNavigationDiagnostic.ps1`、`Scripts/Start-ShutdownDiagnostic.ps1`、`Scripts/analyze-ppt-navigation.py` 复制旧 `log\idt*.log`，并从 stderr 解析 `[PptPerf]`、`[ShutdownTrace]`（清单 §6）。
- 设置页“日志与配置 → 日志”子页（`Section::LogsLog`）目前只有页标题，没有内容（`Setting.Pages.cpp:1778-1779`）。
- PptCOM：`TargetFrameworkVersion v4.0`、AnyCPU；C++ 通过 `#import "PptCOM.tlb"` 使用（`IdtPlug-in.cpp:317`）；已有“独立 QueryInterface 扩展接口”先例 `IPptCOMSessionState`（`PptCOM.cs:103-110`）。C# 侧约 30 处 `Console.WriteLine`（`PptCOM.cs`、`ComEventConnection.cs`、`PptComDiagnostics.cs`），`PptPerfTrace.cs:138` 直接 `Console.OpenStandardError()`；`SetConsoleOutputEnabled(false)` 把 Console 指向 `TextWriter.Null`（`PptCOM.cs:238-260`）。
- 依赖：`vcpkg.json` 固定 spdlog 1.17.0（fmt 12.1.0）。

## Requirements

### R1 日志目录、文件命名与会话头
- 日志目录为 `<EXE目录>\Inkeys\Logs`（与 `Inkeys\Config`、`Inkeys\AutoSave` 同级）。
- 文件名为本地时间 `YYYY-MM-DDTHH-MM-SS.log`（例 `2026-11-16T14-32-05.log`），无前缀、无时区偏移，属项目自定规范；同秒冲突时追加序号（例 `2026-11-16T14-32-05_1.log`）。文件内每行时间同样用本地时间。
- 旧 `<EXE目录>\log\` 目录及其中 `idt*.log` 保持原样：不删除、不迁移、不读取，也不再创建 `log` 文件夹。
- 日志创建时先写会话头：PID、版本（editionDate / editionCode / 通道）、进程架构、系统版本、userId 等当前代码已可获得的环境字段（参照 `IdtMain.cpp:3265` 启动行与 `Net.Update.cpp:534-540` 上报字段），不新增遥测采集逻辑。

### R2 写入线程与性能
- 进程内只有 1 个后台写文件线程（唯一文件写入者）。
- 调用方线程不做文件 I/O；任何调用方都不因日志等待磁盘。Draw3 绘制/输入、RTS 回调等热路径非阻塞，满时丢弃并计数，丢弃情况在日志中可见。
- 落盘：Warn 及以上立即落盘；其余每 1 秒批量落盘；正常退出与崩溃处理路径主动排空并落盘。接受硬崩溃时最多丢失约 1 秒内的 Info/Debug；强制 `TerminateProcess` 回退路径不保证落盘。

### R3 标头规范与分级
- 每行统一标头：本地时间（含毫秒）、等级、模块、线程 ID；调试通道记录额外标出通道名。
- 引入统一模块标识；现有 114 处 `IDTLogger` 调用全部迁移到新接口，按语义复核等级，去掉手写模块前缀（`[主线程][IdtMain]`、`[BarUIRendering::Xxx]` 等）。被外部脚本解析的结构化 trace 标签（`[PptPerf]`、`[PptNav]`、`[PptGate]`、`[PptExitSurface]`、`[SettingEntryPerf]`、`[UiPointerDrag]`、`[Draw3Topmost]`、`[UI3Diag]` 等 key=value 行）保留在消息正文中。迁移到的调用点中 HRESULT 的“假十六进制”（`IdtPlug-in.cpp` 中 `std::to_wstring` 拼 `0x`）改为真正的 8 位十六进制。
- 设置页输出等级档位：调试 / 信息 / 警告 / 错误 / 关闭，默认“信息”。内部另有 Critical（致命），永远写入；“关闭”只关闭普通分级日志，会话头、Critical 与已开启的调试开关输出仍写入。

### R4 不扩展覆盖面
- 本步只迁移和规范现有日志；不新增白板切换、workspace 事务等新诊断事件。

### R5 设置页“日志与配置 → 日志”子页
- 日志输出等级：下拉框，修改后立即生效（无需重启）。
- 自动清理时长：下拉框，含若干天数档位、“永不清理”、“自定义”；默认 7 天。
- 日志大小上限（单位 MB，指 `Inkeys\Logs` 目录总量）：下拉框，含若干档位、“无限制”、“自定义”；默认无限制。
- “自定义”可输入数字；输入不合法（非整数、≤0、超出上限）时恢复为修改前的值。
- 清理规则：超过天数的日志删除；总量超限时按最旧优先删除；当前会话文件永不删除；只匹配本规范命名的日志文件。清理在启动时与修改这两项设置后各执行一次。
- “打开日志文件夹”按钮（资源管理器打开 `Inkeys\Logs`）。不加“当前占用显示”和“立即清理”。

### R6 调试控制台输出改为日志输出
- 彻底去掉控制台：删除 `InitializeDebugConsole()` / `AllocConsole` 路径，不弹窗口、不镜像到 stdout；外部采集改读日志文件。
- 6 个调试开关保留在“调试软件 → 控制台”原位置，配置键与“需重启”语义不变；开启时对应诊断写入日志文件，不受输出等级过滤。
- `Draw3::DiagnosticOutput` 及受开关控制的 printf/cout 改走统一日志；无门控的生产诊断输出按语义改为分级日志。
- 测试工具（*.Test / Probe / Fixture、产品源文件内的 `Run*Test/*Probe/*Benchmark`）的 stdout PASS/FAIL 协议保持不变；Draw3 自检与 `--draw3-diagnostic-output-test` 期间 DiagnosticOutput 仍输出到标准流。
- CrashHandler / ShutdownSupervisor 应急通道保持可用、不依赖日志服务；`[ShutdownTrace]` 继续直写 stderr（退出阶段晚于日志服务停止，且有 I/O 顺序约束）。
- 同步适配 3 个诊断脚本：日志目录改为 `Inkeys\Logs` 与新文件名；C# `[PptPerf]` 改从日志文件读取；`[ShutdownTrace]` 仍从 stderr 读取。

### R7 PptCOM 共用同一输出
- C# PptCOM 日志进入同一日志文件，由 C++ 唯一写入者落盘；PptCOM 保持 .NET Framework 4.0、AnyCPU。
- 只新增 COM 扩展接口，`IPptCOMServer` / `IPptCOMSessionState` 的 vtable 不变；日志回调失败不得影响 COM 业务返回值或 HRESULT。
- C# 输出的现有门控语义保持：仍由 PptCOM 调试开关控制。

## Acceptance Criteria

- [x] AC1（R1）启动后在 `<EXE目录>\Inkeys\Logs` 生成 `YYYY-MM-DDTHH-MM-SS.log`，首段为会话头且字段齐全；`<EXE目录>\log\` 不被创建或修改。
- [x] AC2（R2）代码中只有 1 个日志写线程；`init_thread_pool`、`IDTLogger`、`UI3Diagnostics` logger 不再存在；热路径调用点只使用非阻塞写入。
- [ ] AC3（R2）制造缓冲溢出（如 Debug 档 + 高频通道）时，日志出现丢弃计数行，绘制不卡顿。 —— 未实测：冒烟中 18604 行通道日志零丢弃，未人为制造溢出
- [x] AC4（R2）正常退出后日志文件末尾包含退出阶段的日志行。
- [x] AC5（R3）每行包含时间、等级、模块、线程 ID；全仓库无残留 `[主线程][IdtMain]` 式手写前缀。
- [x] AC6（R3/R5）等级设为“警告”后，Info 日志立即停止写入；设为“关闭”后普通日志停止，但会话头、Critical、已开启通道仍写入。 —— “警告”“关闭”档均已实测（用户确认）
- [x] AC7（R5）清理时长/大小上限的预设与自定义均可设置；非法自定义输入恢复原值；修改后与下次启动时按规则清理，当前文件不被删除。
- [x] AC8（R5）“打开日志文件夹”打开 `Inkeys\Logs`。 —— 用户手动点击确认
- [x] AC9（R6）不再出现控制台窗口；开启任一调试开关（重启后）对应诊断出现在日志中并带通道名，且不受等级影响。
- [x] AC10（R6）测试工具 stdout 协议未改动（diff 核对）；`--draw3-diagnostic-output-test` 与 Draw3 自检仍通过。
- [x] AC10b（R6）3 个诊断脚本能从 `Inkeys\Logs` 复制日志并解析 `[PptPerf]`；`analyze-ppt-navigation.py` 对新行格式可用。
- [x] AC11（R7）开启 PptCOM 开关并进行一次 PPT 放映，C# 日志出现在同一日志文件、模块为 PptCOM；`PptCOM.csproj` 仍为 v4.0，既有 COM 接口定义无改动。 —— PptCOM.Tests 通过、接口定义未改；真实 PPT 放映实测：4912 行 [PptCOM] 通道记录进入同一日志文件
- [ ] AC12 `InkeysRepo.sln` `Debug|ARM64` 构建通过（含 `InkeysHeadlessTests`）；无新增 Win8+ 静态系统导入。

## Out of Scope

- 白板切换失败诊断闭环（`WorkspaceFailureDetail`、receipt 错误透传、HRESULT 就地保存等）——后续子任务。
- 升级 spdlog / fmt / vcpkg baseline。
- CrashHandler dump 通道重构（保留独立应急通道）。
- 新增大量日志覆盖点。
- 修改测试工具的 PASS/FAIL 输出协议。
- 控制台镜像（已决定彻底去掉）。
- 强制 `TerminateProcess` 回退路径的日志落盘。

## 未验证 / 延后项

- Win7 SP1 实机运行（文件写入、中文路径、PptCOM 回调）需另行验证。
- 崩溃路径落盘效果需故障注入验证。
