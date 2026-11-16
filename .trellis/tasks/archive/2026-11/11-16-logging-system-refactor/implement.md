# 第0步 日志系统重构：执行计划

> 依据：`prd.md`（需求/验收）、`design.md`（技术设计）、`research/console-output-inventory.md`（逐点清单）。
> 全程遵守：最小化修改、保持文件原编码（UTF-8 BOM）与 CRLF、不升级依赖、不提交（除非用户授权）。

## 阶段 A：日志基座（可独立编译验证）

- [ ] A1 新建 `Inkeys/Inkeys/Log/Log.h`、`Log.cpp`，加入 `Inkeys.vcxproj`、`.filters` 与 `InkeysHeadlessTests.vcxproj`；`Log.cpp` 不依赖 `IdtMain.cpp` 全局变量。
- [ ] A2 实现双缓冲 + SRWLock 前台写入、等级阈值/通道掩码原子量、丢弃计数（design §3）。
- [ ] A3 实现写线程：交换缓冲、解析记录、`basic_file_sink_st` + pattern、批次 flush 规则、丢弃计数行。
- [ ] A4 实现 `Start()`：`Inkeys\Logs` 目录、`YYYY-MM-DDTHH-MM-SS(_n).log` 命名、会话头、启动清理；失败进入丢弃模式。
- [ ] A5 实现 `FlushAndStop()`、`EmergencyFlush()`、`ApplyRetention()`、`Directory()`。
- 验证：`InkeysRepo.sln` `Debug|ARM64` 编译通过（此时旧 IDTLogger 仍在，两者并存）。

## 阶段 B：接入启动/退出与 C++ 现有日志迁移

- [ ] B1 `IdtMain.cpp` 日志初始化块替换为 `Log::Start(SessionInfo)`；删除旧清理代码、`init_thread_pool`、`UI3Diagnostics` logger；`SetDiagnosticsSink` 改接 `Log::TryWrite`。
- [ ] B2 退出路径：在业务线程 join 之后调用 `Log::FlushAndStop()`；CrashHandler 写 dump 后调用 `Log::EmergencyFlush()`。
- [ ] B3 按清单 §9 逐文件迁移 114 处 `IDTLogger` 调用（13 个文件；模块标签、等级复核、去手写前缀、删 `if (IDTLogger)`，结构化 trace 标签保留在正文）；修正 `IdtPlug-in.cpp` 假十六进制。
- [ ] B3b `Bar.Presentation.Test.cpp:685` fixture 改用日志服务测试钩子。
- [ ] B4 删除 `IDTLogger` 全局定义与 `IdtMain.h` 声明，确认全仓库无残留引用。
- 验证：整解决方案编译；`rg "IDTLogger|init_thread_pool|UI3Diagnostics"` 无结果（测试文件中的同名用法按实际处理）。

## 阶段 C：调试通道与控制台输出迁移

- [ ] C1 删除 `InitializeDebugConsole()` 及调用；启动时按 6 个开关 + `INKEYS_DRAW3_WIN7_DIAGNOSTICS` 设置通道掩码。
- [ ] C2 `Draw3::DiagnosticOutput` 增加“日志模式 / 标准流模式”：产品默认日志模式（`Write` 直接 `Log::TryWrite`）；`runDraw3SelfTest`、`--draw3-diagnostic-output-test` 等测试入口显式使用标准流模式，保留现有队列、字节校验与 `LastErrorGuard`。
- [ ] C3 按清单 §5 迁移 A 类 35 行（开关门控）→ `Trace(Channel…)`，B 类 22 行（环境变量/宏，门控保留）→ `Debug`，C 类 162 行（无门控）→ 按语义逐条定级；D 类 407 行测试协议、E 类 21 行应急路径、`[ShutdownTrace]` / 退出时 `[PptWait]` 的 stderr 直写不动。热路径（清单 §10）只用 `TryWrite`；`PageControl` `TraceDrag` / `TraceSurfaceStage` 去掉同步 stdout/ODS 写入。
- [ ] C4 核对清单 §11 中依赖 `GetLastError()` 顺序的调用点在迁移后保持原语义（尤其 `SetOffSignal` 的 `supervisorError` 锁存）。
- 验证：整解决方案编译；运行 `--draw3-diagnostic-output-test` 与 Draw3 自检（命令行、非交互）确认测试协议不变；`rg` 生产代码中剩余 printf/cout/ODS 均属 D/E/ShutdownTrace 类（与清单逐项核对）。

## 阶段 D：PptCOM 统一输出

- [ ] D1 `PptCOM.cs` 新增 `IPptCOMLogging`（新 GUID）与实现；新增 `ManagedLog`（.NET 4.0 API，非泛型 `GetDelegateForFunctionPointer`）。
- [ ] D2 C# 全部 `Console.*`（含死代码 `DiagnoseComStatus()`）、`PptPerfTrace` 标准错误输出改为 `ManagedLog`；`SetConsoleOutputEnabled` 仅设置启用标志，不再重定向 Console；`PptPerfTrace` 保持只受 `INKEYS_PPT_PERF` 门控，`[PptOwner]` 保持双门控。
- [ ] D3 C++ `IdtPlug-in.cpp`：实现 `PptComLogCallback`，创建 PptCOM 后 QueryInterface 注册，释放前撤销。
- 验证：解决方案构建（PptCOM 先于 Inkeys，tlb 重新生成）；确认 `PptCOM.csproj` 仍为 v4.0；`IPptCOMServer` / `IPptCOMSessionState` 定义未改动（diff 核对）。

## 阶段 E：配置与设置页

- [ ] E1 `Other.Config.cppm` 新增 `Logging` 组（RetentionDays=7、MaxSizeMB=0、Threshold=1），读取时越界收敛默认值；启动时传给 `Log::Start` / `SetThreshold`。
- [ ] E2 `Setting.Pages.cpp` 渲染 `Section::LogsLog`：等级下拉（立即生效）、清理时长下拉+自定义、大小上限下拉+自定义、打开日志文件夹按钮；自定义输入非法时恢复原值。
- [ ] E3 i18n：新增键写入各语言 `.jsonc`，按规范重新生成 `IdtI18nKeys.g.h`；更新“控制台”分区 6 个开关的说明文案为“写入日志”。
- 验证：整解决方案编译。

## 阶段 F：诊断脚本适配

- [ ] F0 `Scripts/Start-PptNavigationDiagnostic.ps1`、`Scripts/Start-ShutdownDiagnostic.ps1`：日志复制源改为 `Inkeys\Logs` 新命名文件；`[ShutdownTrace]` 仍从 stderr 读取。
- [ ] F0b `Scripts/analyze-ppt-navigation.py`：确认新行格式下 `[PptPerf]` 解析可用，C# managed 记录可从日志文件扫描到。
- 验证：脚本静态检查（PowerShell 语法解析、`python -I -m py_compile`）；用一份新格式样例日志跑解析脚本。

## 阶段 G：质量检查与验收（最小化测试时长）

- [ ] G1 `trellis-check` 全量审查 diff：编码/换行、无关改动、COM ABI、Win7 导入（`dumpbin /imports` 检查无新增 Win8+ 静态导入）。
- [ ] G2 运行时冒烟（需用户同意启动 GUI 时再做；否则在交付中列为未验证项）：启动生成 `Inkeys\Logs\<时间>.log`、会话头完整、等级切换生效、开启 Draw3/PptCOM 开关后通道日志出现、清理按设置执行、正常退出尾部日志完整。
- [ ] G3 回填 PRD 验收勾选；记录未验证项（Win7 实机、崩溃落盘、强制退出）。

## 风险点 / 回退点

- 清单数字基于 `c06697b1`，开工前对当前分支重跑一遍 `rg` 确认无新增调用点。
- B3、C3 改动面最大：按文件分批迁移，每批编译；出问题可按文件回退。
- 退出顺序（B2、D3）：先撤销 C# 回调，再 `FlushAndStop`；若发现退出卡住，优先检查写线程 join 与 `offSignal` 合同。
- 阶段 D 的 COM 改动只新增接口；若 tlb/`#import` 出问题，D 阶段可整体回退而不影响 A–C。

## 开始前确认

- [x] 清单 `research/console-output-inventory.md` 已完成并人工抽查（统计、§7.2 测试耦合、§9 前缀、§10 热路径）。
- [ ] 用户审阅并批准最终规划摘要后，才执行 `task.py start`。
