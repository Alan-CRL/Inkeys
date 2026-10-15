# Research: Draw3 性能诊断包、启动链与实验配置

- Query: 调查既有 Win7 CMD→PS1 包的启动、收集、身份与兼容合同；确认现有 Draw3 启动诊断、实验设置 owner、国际化和工程登记；提出正式 `Scripts/Draw3Performance` 最小交付方案。
- Scope: mixed（仓库代码与历史研究只读，Microsoft 官方 API 文档；只写本任务 research/）。
- Date: 2026-10-14

## Findings

### 已确定的实现方向

1. 新实验开关应进入 **`Inkeys.Other.Config` 的 `Experimental.Inkeys3.ConsoleOutput`**，建议字段 `Draw3Performance : IdtAtomic<bool> = false`，上传模式继续 `NoUpload`。当前同组 `PptCOM/Draw3/TouchArea/Cursor` 的 schema、设置页和启动消费者都直接确认属于这个 owner；不需要猜测，也不需要迁移到传统 `SetListStruct`。
2. 正式诊断包通过 **`--draw3-performance <绝对会话目录>`** 显式启用，Release 中可用，无需提前勾选设置。只改变本次局部诊断状态；不要赋值到 `config.Experimental...`，因为启动随后会执行 `config.Write()`。
3. **保留真实 SuperTop/UIAccess 启动路径**。主会话明确选择命令行透传、最终进程握手：越过 SuperTop 的最终进程写 `launch.json`（final PID、进程创建时间、session/schema），结束导出后写 `completion.json`。初始进程退出码 0 不算完成；初始化证据由最终进程自写会话文件，不依赖初始进程 stdout/stderr。
4. 性能原始 CSV 的生产者只能写有界数值样本，关掉诊断时不申请采集数组、不读新增诊断时钟、不写文件、不格式化。脚本负责启动前硬件收集和退出后分析，避免采集脚本在低性能机器的书写阶段竞争 CPU/磁盘。
5. 旧 Win7 的长矩阵、shader/full-ULW 覆盖不进入默认性能包。它们改变路径且最坏耗时 30 分钟；本任务只测真实 Draw3 性能，不把独立像素 PASS 当作产品性能或屏幕可见 PASS。

### Files found

| 文件 | 已确认职责 |
|---|---|
| `.trellis/tasks/archive/2026-10/10-02-win7-main-canvas/research/Run-Win7-Diagnostics.cmd` | 唯一 CMD 入口，setlocal、引用脚本目录、保存并返回真实 PS 退出码 |
| 同目录 `Run-Win7-Diagnostics.ps1` | 单次目录、三阶段编排、manifest/EXE/脚本 SHA、临时进程环境恢复、用户观察 |
| 同目录 `Collect-Win7.ps1` | 拒绝已有 Inkeys、拒绝复用目录、EXE SHA、双流文件、确切子进程等待、复制本轮 idt 日志 |
| 同目录 `formal-warp-collector-implementation-20261009.md` | PS2 子集与 WinPS5.1 mock 47+3 项验证、编码、包身份和平台边界记录 |
| `Inkeys/IdtMain.cpp` | wWinMain 早期 CLI、单实例、SuperTop、最终启动 T0、日志、配置到局部诊断的桥、退出顺序 |
| `Inkeys/Inkeys/Text/Text.Split.cppm` | 既有参数 parser，按空白分 token，保留引号及自定义括定内容 |
| `Inkeys/SuperTop/IdtSuperTop.cpp`、`IdtToken.h` | UIAccess helper 和最终应用再启动，不能依赖原重定向/环境/进程 handle |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Diagnostics.cppm/.cpp` | 环境诊断开关、适配器/驱动日志、旧 Debug 限频帧日志与光标队列 |
| `Draw3.GraphicsInitialization.cpp` | 真实 Hardware→WARP 尝试、feature level、实际 adapter、HRESULT |
| `Draw3.Host.cpp/.h` | 唯一 Draw3 Host、既有 metrics 开关、实际初始化/显示边界、RTS 与绘制生命周期 |
| `Inkeys/Inkeys/Display/Display.cppm` | 已有显示快照、拓扑、像素/DPI/方向/EDID/物理标尺可用状态 |
| `Inkeys/Inkeys/Startup/Startup.Progress.cppm/.cpp` | 启动进度与失败快照；目前没有所有初始化阶段耗时序列 |
| `Inkeys/Inkeys/Other/Other.Config.cppm/.cpp` | `INKEYS_CONFIG_SCHEMA` 和 `Inkeys/Config/main.json` 读取/写回 |
| `Inkeys/Inkeys/UI/Setting/Setting.cpp` | 实验 ConsoleOutput 卡片、局部 UI 状态和 QueueConfigWrite |
| `Inkeys/src/i18n/{zh-CN,zh-TW,en-US}.jsonc` | 三语言源文案 |
| `Scripts/i18n.ps1`、`Scripts/i18n.zh-CN.snapshot.jsonc`、`Inkeys/IdtI18nKeys.g.h` | sync/check 和生成键链 |
| `Inkeys/Inkeys.vcxproj/.filters` | 当前 diagnostics、Host、probe 的编译和显示登记 |
| `InkeysRepo.sln` | 当前原生/托管项目及 I18n solution items；新增 scripts 不必为编译改 sln |

### 旧诊断包可复用的代码模式

- CMD 全文仅 8 行：`setlocal`、`powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0...ps1"`、立即保存 `%ERRORLEVEL%`、pause 后 `exit /b` 返回原值。见旧 `Run-Win7-Diagnostics.cmd:1`。新入口沿用此形状，只有脚本文件名改变；不要要求 pwsh/Python。
- 旧 collector 在启动前拒绝已有 Inkeys（`Collect-Win7.ps1:9`）、现有输出目录（`:13`），用 .NET SHA256+stream 计算 EXE hash（`:15–18`），记录时间、OS、PS 版本和系统 DLL 版本（`:19–39`）。这些可以保留，但 EXE 路径应来自明确参数或交付包旁的 Inkeys.exe，不搜索任意旧 Build。
- 旧 wrapper 独立核 manifest EXE SHA/schema/package/case 数（`Run-Win7-Diagnostics.ps1:39–58`），又记录执行脚本 hash（`:60–68`）。缺 manifest 当 incomplete；新正式性能包建议直接拒绝身份不匹配，避免把新 CLI 传给旧 EXE 后进入正常 GUI。源码树使用明确 `-ExePath` 时也应生成/验证对应身份记录。
- 使用 `Start-Process` 文件重定向保存完整 stdout/stderr（`Collect-Win7.ps1:231–233`）；持有自有 `Process.Handle`、WaitForExit 后取真实 ExitCode（`:234–238`）；异常只处理该确切 Process（`:241–244`），从不按名称结束已有用户进程。
- 日志只复制本次触及文件（`Collect-Win7.ps1:249–257`），不清理原日志、不改配置。新包可沿用时间窗口作为辅助，但必须以最终进程写入的日志文件路径/PID/session 身份为首选，不能把同名 `idt*.log` 认为唯一会话证据。
- 旧 wrapper 所有覆写只针对 Process environment，finally 恢复原值（`Run-Win7-Diagnostics.ps1:21–23,79,87–88,203–207`）。新方案以 CLI 为主，不必新建机器/User 环境变量；如为了旧诊断兼容临时加环境，只传本次子进程或 finally 恢复，不写 setx/注册表。
- 收集与测试结论分开：原 wrapper 保留 captured/completed/incomplete/failure/unavailable（`:180–223`），用户观察单独文件并明确不是自动像素验收。新报告也应分开“包完整性”“性能统计”“因果判断”。

### 现有 startup diagnostics 与缺口

- `IdtMain.cpp:294–330` 已有 `InitializeDebugConsole()`：双方都是文件/管道时不 AllocConsole；否则保留已有重定向。这能支持旧脚本，但最终进程重新启动可能丢失这些 handle，故不能成为新会话唯一输出通道。
- `IdtMain.cpp:2614–2622` 中 **`INKEYS_DRAW3_WIN7_DIAGNOSTICS` 只在 `#ifndef IDT_RELEASE` 读取**；Release 局部值恒为 false。直接复用不能满足正式性能包。
- `IdtMain.cpp:2623–2647` 只把 config/旧环境 OR 到局部 Draw3、Cursor、TouchArea；设置 `SetStartupEnvironmentDiagnosticsEnabled`，设备初始化前绑定控制台。`IdtMain.cpp:2653–2654` 很快会 `config.Write()`，所以临时 OR 必须保持局部，不得把会话 override 填进 config。
- `Draw3.GraphicsInitialization.cpp:50–89` 记录 preferred/fallback HRESULT；`:93–145` 实际先 Hardware 失败后 WARP，输出实际 driver/feature、GetAdapter，环境开启时记录 adapter。`Draw3.Diagnostics.cpp:812–844` 已有 Vendor/Device/SubSys/Revision、显存和 UMD driver raw version。不由 WMI/CPU 型号推断正在用的 backend。
- `Draw3.Host.cpp:1512–1519` 记录 graphics ready/failed 与当前实际 driver/feature，`Draw3.Host.cpp:1182–1202` 只在 touchAreaTrace 打开且显示发布时输出真实快照/EDID 状态。性能包应自写显示快照，可以复用这些已计算字段；**不要为获得显示信息强开所有 TouchArea/Cursor 每帧文本**，那会扰动低性能设备。
- `Display.cppm:96–126` 有像素尺寸、effective DPI、方向、primary/fallback、显示拓扑、generation 和物理标尺可用性；active target 的时序信息另在该 module 中。EDID 原始物理尺寸/时序和当前活动分辨率必须分列，缺失保持 unavailable。
- `Startup.Progress.cppm:71–80` 的 Snapshot 只有 completed units、失败代码、revision 和 startTime，没有逐阶段 timestamp。性能会话若要求初始化异常与阶段时间，需要在最终进程真实调用边界追加一次数值初始化记录，不把进度百分比倒算成时长。
- `IdtStart.cpp:80–104` 已用动态 `RtlGetVersion` 取得真实 OS major/minor/build；PS 的 `Environment.OSVersion` 是辅助值。每条可选查询保留 executed/status/error，不把 unavailable 填成 0 或初始化失败。
- IDT logger：`IdtMain.cpp:2252–2335` 输出 `globalPath/log/idt<timestamp>.log`，启动清理超过 7 天/负时间差/目录超过 10MiB 的匹配日志；`:2337–2343` 原 logger 使用 block，flush_on(info)。它不能成为逐帧性能 CSV sink。`:2345–2357` UI3 诊断桥已有共享池+`discard_new`，可作为稀疏事件的非阻塞参照；不得为了性能包改变普通 logger 策略。

### SuperTop 与命令行透传：最小修改证据

**主会话决策：不绕过 SuperTop，不修改真实运行权限/环境。**

- 既有 `Inkeys::Split::Run(GetCommandLineW(), '*')`（`IdtMain.cpp:1440`）不是只按 `*` 切分。`Text.Split.cppm:22–29` 在非 quote/custom 位置按空格/tab 分 token，`:47–57` 保留引号。输入 `"Inkeys.exe" --draw3-performance "C:\path with spaces\session-guid"` 会得到 exe、flag、带引号 path 三个 token。
- `IdtMain.cpp:1449–1466` 识别旧内部标志；未知普通 token 原样加空格拼入 `LaunchState::commandLine`，因此新 flag 和带引号路径可以透传，不需重构通用 parser。
- `IdtMain.cpp:2096` 将该字符串传 `LaunchSurperTop`；`IdtSuperTop.cpp:269–274` 包在 `-CmdLine=?... ?` 中，`:114–120` 解包后保留内容，`:245–246` 给最终进程拼上 `-SuperTopComplete` 与原参数。合法 Win32 路径不能含 `*`/`?`/引号，能避开旧 custom delimiter；采集器传不带末尾分隔符的 session 目录，以免关闭引号前的反斜杠产生 argv 歧义。
- `IdtSuperTop.cpp:280–292` 成功 ShellExecute(run as) 后初始进程 exit(0)。`IdtToken.h:231–244` 的最终 CreateProcessWithTokenW 用 `CREATE_NEW_CONSOLE`、NULL environment、没有自定义标准流句柄。**不能假设脚本原 stdout 或本次 Process 环境仍到达最终进程。**
- 在 `wWinMain` 很早用 `CommandLineToArgvW` 验证新 flag 的参数数量、重复、绝对路径与支持 schema；`IdtMain.cpp:1207–1223` 是已有早退 CLI 模式参照。执行 helper 的内部 `-SuperTop=*...*` token 不应被误判成顶层新 flag。
- 最终会话 owner 应在 `IdtMain.cpp:2108–2109` 已越过 SuperTop、建立 T0 的位置附近初始化并写 `launch.json`，先于后续共享设备/COM等初始化。设置开关途径在 config.ReadAll 后启用也需要记录该区别：不会拥有前面完整初始化时间；CLI 途径是包的正式完整入口。
- `launch.json` 至少含 capture_schema、session_id、EXE hash/version/architecture、final_pid、实际进程创建时间（UTC FILETIME/ticks）、启动时间、UIAccess 真实状态。采集器读取后打开该 PID 的 Process.Handle，复核创建时间/EXE，防 PID 重用；初始退出只能记 bootstrap_exit。
- `completion.json` 在 Host/RTS 真停止、样本封口、CSV/summary 落盘以后发布，至少含 session_id、PID/创建时间、sealed/export 状态、行数、drop/truncation/invalid 和 shutdown reason。已有顺序 `IdtMain.cpp:3495–3500` 为 StopProduct→窗口/RenderPipeline/Display stop。需与实际封口 API 协调，不能先写 completed 再等 worker。
- 超时/崩溃/15秒强退/未出现最终握手/权限拒绝：保留已有 init/partial artifacts，明确 incomplete；缺 completion 时永不自动合成成功。若用户在会话中主动重启，应结束这一子会话并标 restart，或使用不同 run/PID 文件，不能在同一路径静默覆盖 launch/completion。

### 配置、设置页、国际化与工程登记

- owner 的直接证据：`Other.Config.cppm:349–361` 含 Experimental/Inkeys3/ConsoleOutput 四个 bool，默认 false，NoUpload。`Other.Config.cpp:813–815` 路径是 `globalPath + Inkeys/Config/main.json`；不是 `opt/deploy.json`。`IdtMain.cpp:2600,2612–2630` 启动消费者一致。
- 设置页 `Setting.cpp:1351–1372` 启动局部 UI 状态从同一 config 初始化；`:7696–7722` 共用 `drawConsoleOutput` 卡片；`:7724–7759` 切换只改对应 config bool，再 QueueConfigWrite。新卡片应复用同一 helper，描述“下次启动采集 Draw3 性能，测试结束后导出；诊断包无需预先打开此项”。沿既有 next-launch 行为，无需热切换 Host/RTS 或增加 ImGui 同步等待。
- 建议新增 `SettingsUI/Experimental/ConsoleOutput/Draw3Performance/{N,E}`，三语言同步。保留旧 TouchArea/Draw3/Cursor 键。三语言已有组位于 `{zh-CN,zh-TW,en-US}.jsonc:479–505`。
- `Scripts/i18n.ps1:11–16` 指向源、header、snapshot；`:710–735` sync 按 zh-CN 更新其余语言、生成 header、snapshot，再 check。开发机运行 `pwsh ./Scripts/i18n.ps1 sync` 和 `check`，先处理翻译标记。**该开发工具包含 `::new`/`$PSScriptRoot`，不能作为 Win7 用户端依赖**（`:11,18`）。新采集器独立按 PS2 子集实现。
- 目前 diagnostics `.cpp`/`.cppm` 在 `Inkeys.vcxproj:1042–1045` 登记，interface 用 CompileAsCppModule。Host/probes 在 `:1109–1114`；`.filters:2054–2059,2180` 设所属 Draw3 Filter。若新增独立 `.cpp/.cppm/.h`，按相邻模式登记两文件；在现有 diagnostics module 内补函数则无新增工程项。
- `Scripts/Draw3Performance` 的用户脚本不是编译输入。除明确要求 Visual Studio solution 展示或正式发布 CI 装包外，不改 sln/build workflow；专用诊断包可由仓库内本地包装脚本复制并生成 manifest 到忽略的输出目录。既有发布 ZIP 内容固定的合同不意味着可以顺手扩大发行 ZIP。
- 只读 byte 检查（2026-10-14）：IdtMain 为 UTF-8 BOM+CRLF；Other.Config.cppm、Setting.cpp、三个 JSONC、vcxproj/.filters 为 UTF-8 无 BOM+CRLF，均无裸 LF。旧 Collect 为 ASCII+CRLF，旧 Run PS 为 UTF-8 BOM+CRLF，CMD 为 ASCII+CRLF。新增用户中文 PS 采用 BOM+CRLF；只改必要字段/卡片，不借 sync 引入大范围无关格式变化。

### 已存在的无可见窗口 CLI

| 入口 | 实际范围与适合用途 |
|---|---|
| `--draw3-renderer-pixel-test` / `...-hardware` | `IdtMain.cpp:1207–1223` 先于产品配置/窗口早退；`HiddenWindowTest.cpp:2376–2380` 输出 guarded-20261008 schema。创建未显示 fixture HWND，执行长矩阵；硬件 unavailable 退出3。可做独立渲染能力诊断，不能作为默认性能包的书写数据 |
| `--draw3-host-metrics-smoke --output-root <path>` | `IdtMain.cpp:1258–1264` 早期精确 argv 入口，调用 hidden fixture；适合 Host采样/导出合同验证，不是真人 RTS latency/正常产品环境 |
| `--draw3-geometry-benchmark` | `IdtMain.cpp:1344–1346`，`PerformanceProbe.h:5–6`/`.cpp:228–229`：只测生产激光增量范围规划+游标更新；不能泛化成所有 Modeler/renderer 性能 |
| `--draw3-history-benchmark` / `--draw3-ulw-copy-benchmark` | `IdtMain.cpp:1347–1352`；与对应领域成本有关的无窗口子范围，不等同整体帧/输入延迟 |
| `InkeysHeadlessTests.exe --no-window` | `InkeysHeadlessTests/animation_tests.cpp:1644` 跳过会建 HWND 的测试；适合软件统计/配置/边界检查，不是目标硬件真人书写结果 |

建议追加短的 `--draw3-performance-capabilities` / `--draw3-performance-probe` 早退入口，输出固定 schema/可用功能并拒绝坏参数，在正式 app UI 之前 return；它只验证包匹配/软件算法，默认不主动跑长 GPU matrix。向旧 EXE 发未知参数可能落入正常启动，必须先靠正式 manifest EXE SHA 证明版本身份。能力缺失用 unavailable，与错误/失败分开，按既有硬件 probe 的模式记录。

### `Scripts/Draw3Performance` 最小交付结构与流程

**建议源文件**：`Run-Draw3-Performance.cmd`、`Run-Draw3-Performance.ps1`（含内部 HardwareOnly 模式）、`Analyze-Draw3-Performance.ps1`、`README.md`。如需要真实可交付 ZIP，可加小的开发机包装入口，用已构建 EXE 生成 manifest并复制这四个文件，输出不进源码；不新增构建系统。

**专用交付包**：对应架构 Inkeys.exe、单 CMD、采集/分析 PS、说明和 `diagnostic-manifest.json`。manifest至少包含 package_id、capture_schema、EXE SHA/version/architecture、脚本 SHA、所需 CSV schemas；不把 EXE 自己的 SHA 循环写入其资源。

**流程**：

1. 检查脚本/EXE/manifest/输出目录写权限，拒绝任一已有 Inkeys实例；新目录用 timestamp+GUID，拒绝复用。明确提示正常退出原实例，不代为 kill。包旁的独立目录或用户 Documents 下的 Inkeys diagnostics 目录皆可，先确保可写再启动。
2. 执行有界的硬件 worker，然后退出 worker；即使单项 WMI失败/超时仍记录 query status 并继续主要应用。长查询放独立 PS worker、持确切 handle，超时只终止该 worker；不能仅设置 WMI EnumerationOptions.Timeout 就假定整个 query 已有界。
3. 以实际包中的 EXE、正确工作目录和精确引用的 `--draw3-performance "<session>"` 启动。只有脚本 CMD/PS 属于启动入口，normal 双击 Inkeys不自动采集。捕获 bootstrap stdout/stderr作辅助，最终 app自身在会话目录写 init/events/CSV。
4. 等最终 launch握手，复核 session/schema/PID/创建时间/路径。测试用户按说明新建空白页进行普通笔、快慢Move、Down/Up、荧光笔/橡皮等，遇到卡顿在观察文件描述动作/时间；不要求先开实验设置，不自动输入、不改变帧率/后端/画质。
5. 只等待确切最终进程；退出后核 completion、所有 raw CSV/header/行数/identity及丢样。缺样保留 partial状态，不能“退出0即PASS”。复制最终 app指出的 idt日志和本轮辅助日志；不要复制任意旧配置为本次输出。
6. 离线 analyzer产生 `summary.json` + `report.md`/`report.txt` + `anomalies.csv`，支持再次 `-CaptureRoot` 分析。返回整个目录；Win7端ZIP可选，缺Archive模块时目录仍是完整交付，开发机装包可以用现有现代PS压缩工具。

**建议最小会话文件**：identity/manifest副本、hardware信息与query-status、bootstrap.stdout/stderr、launch.json、init.json或init.csv、frames.csv、input.csv、events.csv、completion.json、summary/report、anomalies.csv、observations.txt、本轮 idt日志。会话锁/CREATE_NEW writer reservation阻止第二个app写同目录；结果原子发布，避免半个JSON被collector当完成。

### 硬件/环境采集字段与成本

| 来源 | 建议字段 / 处理 |
|---|---|
| Win32_ComputerSystem | Manufacturer、Model、TotalPhysicalMemory、NumberOfLogicalProcessors；不收序列号/产品密钥 |
| Win32_Processor | Name（保留兆芯等真实名称）、Manufacturer、Architecture、NumberOfCores/LogicalProcessors、MaxClockSpeed；CurrentClockSpeed为快照且注明不能证明实测频率 |
| Win32_OperatingSystem | Caption、Version、BuildNumber、ServicePackMajorVersion、OSArchitecture、可用内存；与 app真实RtlGetVersion分列 |
| Win32_VideoController | Name、PNPDeviceID、DriverVersion/Date、AdapterRAM，available/failure状态；它不是app实际DXGI adapter/driver选择的替代 |
| 一次系统查询 | 电源方案（powercfg /getactivescheme）、远程会话标记、目标架构、PS/CLR版本、system DLL file versions；可选KB2670838定向查询，不枚举大量补丁/安装程序 |
| app最终进程 | UIAccess、DPI awareness、显示generation/拓扑/实际尺寸/DPI/方向/refresh、EDID可用原因、真实Hardware/WARP、featureLevel/adapter、presenter/target、初始化HRESULT/fallback/失败、实际帧率和渲染选项 |

收集在书写前进行，不连续运行 WMI/dxdiag/任务列表采样；必要的进程资源/CPU相关性只低频且明确诊断启用后才采样。不自动强开 D3D debug layer或改变device创建flag来“获取更多日志”，否则不能把结果当正常路径成本。

### CSV与离线报告边界（与核心采样研究协调）

- 帧数据应保留 frame/run/epoch/idle segment、原始单调tick/frequency、工作与等待/间隔分离、真实各CPU阶段、actual present status、tool/contact/point/dirty pixels/copied bytes、设备/显示/重试/失败因素。阶段名称/包含关系由真实生产边界确定，报告不要把包含阶段重复加总。
- 输入数据分 RTS/mouse，保留 packet/callback/accepted序号、contact generation、phase、decode/publish/consume/geometry/present边界与关联frame/success序号，以及coalesced/rejected/unmatched/invalid状态。必须按实际lineage匹配，不能用“时间最接近帧”伪造点完成。
- “输入到绘制完成”在本包可以证明 **RTS callback入口→包含其结果的成功present API提交**，不包含笔硬件/RTS之前排队，也不是光学可见/scanout。只有非阻塞GPU query有效时才报告GPUduration，其不可用/掉样/跨epoch必须分列；CPU D3D API耗时不命名GPU耗时。
- summary：有效样本数/覆盖时间/idle/invalid/drop/truncation各分母；工作/活动呈现间隔/输入延迟及各阶段 p50/p90/p95/p99/max、标准差或变异系数；失败/retry/device-reset counts；明确requested budget。异常表保留超预算、50/100/500ms等描述阈值、对应阶段和相关因素，阈值不是本次性能PASS门。
- 已知idle间隔不进入活动帧抖动；没有active frames或RTS输入时报告no-data，不输出0 FPS/0ms成功。Down/Move/Up、不同来源/工具/epoch分别统计，避免长期idle或prediction帧稀释不跟手。
- 用每一慢帧实际最大阶段、等待比例和设备/复制/输入因素给出“优先检查方向”。例如WARP+大脏区+copy/present成本高是关联证据；不能仅因CPU型号或一组高相关性宣称根因。
- 所有CSV数字使用invariant culture，tick保留int64，缺值留空并有status，不能由地区逗号/小数分隔符改变列结构。回放分析不依赖Python/Excel。

### Windows PowerShell兼容策略与验证建议

- 用户端以 PS2语法/.NET基本 API子集为目标，现代WinPS5.1也能运行；使用 `$MyInvocation.MyCommand.Path` 解析目录、.NET SHA256/IO、`Get-WmiObject` 或 `System.Management` 指定字段，避免 pwsh、CIM、`Get-FileHash`、`ConvertTo/From-Json`、`[ordered]`、`[pscustomobject]`、`::new`、现代async/ArgumentList API和Archive强依赖。
- 应用C++已有JsonCpp可写JSON。PS2侧可用严格固定schema的manifest标量读取与小型escaping writer；如果靠regex读取JSON，至少限定唯一键/类型/值，不接受重复identity字段，不能把regex宣称通用JSON parser。也可用text/CSV辅助身份，以减少PS端JSON需求。
- 用户中文PS保存UTF-8 BOM+CRLF；CMD保持ASCII+CRLF。使用cmd `%~dp0` 引用和明文 -File args；别用Invoke-Expression或拼命令字符串执行用户路径。
- 初始stdout/stderr用现成Start-Process文件重定向。若改用ProcessStartInfo手写双流，必须并行排空，不能依次ReadToEnd或先WaitForExit让子进程堵管道。
- 主会话验证：实际WinPS5.1+无GUI mock覆盖空格/中文路径、两大双流、非零/超时/WMI失败、已有实例、manifest错配、初始进程退出后最终握手、最终PID不存在/重用/身份错配、缺completion/CSV/掉样、环境不泄漏、CMD退出码；新统计测试用synthetic CSV验证inclusive stage、不计idle/invalid、分位数、drop、不完整状态。
- 构建/真实软件CLI由实现/主会话按根AGENTS执行完整Solution Debug|ARM64、ARM64原生MSBuild、同一PS规范化PATH，超时至少5分钟。本研究不编译、不打开产品GUI；本机WinPS5.1和mock不等于真实Win7 PS2或兆芯目标机实测。

### External references（官方，访问日期2026-10-14）

- [ProcessStartInfo.EnvironmentVariables](https://learn.microsoft.com/en-us/dotnet/api/system.diagnostics.processstartinfo.environmentvariables?view=netframework-4.8.1)：可修改子进程environment字典；必须UseShellExecute=false。仅用于可选process环境隔离，不替代SuperTop最终进程CLI透传。
- [RedirectStandardOutput](https://learn.microsoft.com/en-us/dotnet/api/system.diagnostics.processstartinfo.redirectstandardoutput?view=net-9.0)：顺序排空stdout/stderr或等退出可能死锁；文件重定向/并发排空。
- [about_PowerShell_exe（5.1）](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.core/about/about_powershell_exe?view=powershell-5.1)：-File、CMD参数和显式exit码行为；Ctrl+C可能返回0，所以不能仅用PS退出0判断采集完整。
- [ConvertTo-Json（5.1）](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.utility/convertto-json?view=powershell-5.1)：JSON cmdlet从Windows PowerShell3.0引入，不能做PS2依赖。
- [Introduction to CIM Cmdlets](https://devblogs.microsoft.com/powershell/introduction-to-cim-cmdlets/)：CIM cmdlet属于PS3.0引入；Win7默认PS2兼容路径需WMI/API。
- [Archive module发布说明](https://devblogs.microsoft.com/powershell/the-archive-module-is-now-open-source/)：Archive随PowerShell5.0/WMF5.0提供；目标端不要求它。
- [ManagementOptions.Timeout](https://learn.microsoft.com/en-us/dotnet/api/system.management.managementoptions.timeout?view=net-11.0-pp)：返回集合的timeout约束枚举，不是整个查询；这是建议独立worker硬截止的原因。文档view含新版本，但引用的是现有System.Management合同；仍需WinPS实际验证。

### Related specs

- `.trellis/workflow.md:363–390`：研究必须落盘，主会话负责规划/实现派发；本角色不加载implement/check.jsonl。
- `.trellis/spec/native-desktop/index.md`：配置owner按实际消费者判定；源码/moudle工程登记。
- `.trellis/spec/native-desktop/configuration-i18n-and-assets.md:5–35,268–283`：两套配置并存、owner证据、i18n生成链、旧实验输出键兼容。
- `.trellis/spec/native-desktop/errors-logging-and-resources.md:21–33`：现有日志清理、避免逐packet同步I/O、显示快照诊断边界。
- `.trellis/spec/native-desktop/ui3-render-diagnostics.md:32–45`：可借鉴有界raw、true join封口、drop计数、门关闭不读新时钟、CPU/GPU/成功present边界；本任务不采UI3性能。
- `.trellis/spec/native-desktop/draw3-integration.md`：唯一Draw3 Host、独立设备/输入owner、正常模式不启metrics、真实present生命周期。
- `.trellis/spec/native-desktop/build-and-compatibility.md:5–34,142–159`：Solution验证、新增module登记、发布ZIP现有合同。与本轮根AGENTS对PptCOM依赖描述不完全一致，实际验证仍服从用户给出的完整Solution入口，不为此任务修改工程依赖。

## Caveats / Not Found

- 以上新文件/字段/CLI/schema是研究建议，尚未实现；精确CSV字段和采样成本以核心Draw3/RTS研究与最终design为准。
- 未找到当前正式`Scripts/Draw3Performance`；现有Win7脚本留在历史任务research，不宜修改历史证据。新包应复制模式到正式Scripts目录。
- SuperTop的普通带引号路径透传已按源码确认，旧parser不是Windows argv的完全等价实现；需用mock/纯字符串测试覆盖空格/中文、不能对未知异常路径作支持承诺。不存在必要的通用parser重构证据。
- `completion`必须建立在真实数据封口/导出上；正常logger尾部flush、15秒退场、崩溃/断电可能截断，诚实记录partial，不能假定必有尾部数据。
- WMI硬件信息是快照，查询失败应unavailable；CurrentClockSpeed、AdapterRAM或OSVersion的缺陷不应改变渲染路径。硬件/driver根因和RTS真输入仍需目标机回传包。
- 没有执行编译、CLI或GUI，也没有读写Git；只读查阅与研究文件落盘不构成Win7/兆芯性能实测。
