# Draw3 性能诊断验收记录

## 授权与验证边界

用户批准本 Trellis 任务规划、实现至最终；不再请求重复授权。使用现有 solution、工程配置、vcpkg manifest 和 triplet，不修改 SDK、依赖版本、编译器或全局 Git/SSH/环境配置。未启动 Inkeys 主窗口、设置/浏览器、真实或隐藏 HWND、RTS/D3D 会话，未使用 Computer Use，未 commit/push 或写远程内容。

## 最终源码与接口

Draw3 采集、启动/配置、包脚本分别完成后冻结。最后源码修正只有 Performance、GraphicsInitialization、DrawingController 三个 CPP；公开 API/CSV 列不变。独立 check 回读所有实调用与修正，阻断项已全部关闭；以最后构建与 CLI 原始输出确认，不用修前结果代替修后。

## 已完成验证

| 验证 | 实际结果 | 原始证据 |
|---|---|---|
| 完整 InkeysRepo.sln Debug/ARM64，最终冻结源码 | 自然 exit0，0 errors | Build/Draw3PerformanceValidation/build-debug-arm64-final.log、.status.txt |
| 修后性能 probe | 自然 exit0，recorder/proof failures=0，production assembler PASS | performance-debug-arm64-final.stdout.log、.status.txt |
| 完整 InkeysRepo.sln Release/ARM64，最终冻结源码 | 自然 exit0，0 errors | build-release-arm64-final.log、.status.txt |
| Release/ARM64 performance/launch/config 三个 GUI 前入口 | 各自然 exit0、PASS | cpu-tests-release-arm64.json 及 stdout/stderr |
| 完整 InkeysRepo.sln Release/x64，最终冻结源码 | 自然 exit0，0 errors；ARM64原生MSBuild交叉编译 | build-release-x64-final.log、.status.txt |
| Release/x64 performance/launch/config 三个 GUI 前入口 | 各自然 exit0、PASS；本机ARM64兼容层执行，不是兆芯性能实测 | cpu-tests-release-x64.json 及 stdout/stderr |
| Debug 严格 Headless --no-window | 自然 exit0，216 layouts failures=0，animation correctness PASS；其源在最后三 CPP 修正中未变 | cpu-tests-debug-arm64.json、headless-debug-arm64.stdout.log |
| Debug launch/config（其源未再变） | 各自然 exit0、PASS | cpu-tests-debug-arm64.json 及 stdout/stderr |
| WindowsPS5.1.26100.9711 完整离线套件 | 120 项断言 PASS，宿主 exit0 | Build/Draw3PerformanceTests/offline-276888d307384fb4b0378fd54e913b65/test-results.json |
| pwsh7.6.5 同版完整离线套件 | 120 项断言 PASS，宿主 exit0 | Build/Draw3PerformanceTests/offline-2affd768928f4ec5a1cefbea352eb1c3/test-results.json |
| ARM64 正式 Release ZIP | 7 个文件、PE arm64、最新 EXE/脚本/README SHA、ZIP 各 stream 与目录完全相同 | package-arm64-validation.json |
| x64 正式 Release ZIP | 7 个文件、PE x64、最新 EXE/同版脚本/README SHA、ZIP 各 stream 与目录完全相同 | package-x64-validation.json |
| i18n sync/check | zh-CN/zh-TW/en-US 均362/362、100% | research/check-startup.md 与 startup-implementation.md |
| 原格式与 diff | 最终30个已跟踪源码/工程/i18n/spec索引及Trellis journal文件的BOM、CRLF、EOF保持，diff --check通过；8个新PS1 AST/BOM/CRLF、CMD ASCII/CRLF | format-audit-final.json、script-static-audit.json、独立 check-performance.md |

表中未带目录的 Build 证据统一位于 `Build/Draw3PerformanceValidation`。Trellis research 位于本任务 `research/`。

## 构建方式、首次失败与环境问题

通过 vswhere 动态选择当前 VS 安装的 `MSBuild/Current/Bin/arm64/MSBuild.exe`。所有 MSBuild 都在同一个 PowerShell invocation 先执行 `Remove-Item Env:PATH -ErrorAction SilentlyContinue` 和 `MSBUILDDISABLENODEREUSE=1`。复用仓库已验证的命令范围参数 `/p:VcpkgManifestInstall=false /p:CL_MPCount=1 /p:MultiProcMaxCount=1 /p:LinkIncremental=false /m:1 /nr:false`；完整 solution 解析 PptCOM 依赖，不单独编译 Inkeys。

首次 Debug 使用 /m:4 在 module dependency scan 约10秒后退出1，MSBuild日志0 errors/0 warnings，未提供可归因的源码错误；未据此改源码或工程。改用近期任务已经验证的串行参数后完整 Debug 通过；最后三处审查修正后再次完整 Debug 通过。

首次 Release/ARM64 冷构建到20分钟上限时仍在 Modeler 末端/Generating code，退出124，未出现源码错误，不能算成功。保留该轮 stdout/log/status，待已确认三CPP计量修正后以40分钟预算做增量完整solution，最终自然 exit0；没有重复从头盲目启动同一冷构建。

Debug最终编译13 warnings 包含既有Modeler窄化及probe忽略nodiscard返回值（含新增模型fixture的两处有意忽略）。它们未造成源码/链接错误，不为了0warning修改无关生产逻辑。完整 Release warnings 见原始日志。

本机最初缺 x64 已安装依赖。沙箱内 vcpkg 依赖计划无法发现可用 Git，工具下载连接失败；仅修改本次 invocation PATH仍失败，未改全局配置。隔离外按既有 manifest/triplet 完成依赖计划后，17个锁定包全部从现有用户 vcpkg archives 恢复到 VcpkgInstalled/Win64，exit0；没有升级 manifest/库/SDK/编译器。vcpkg所需 CMake/解压工具只落入其工具缓存。依赖准备日志见 x64-dependency-*.log/.status.txt。

## 真实硬件查询的 CLI 验证

只执行用户脚本 `-HardwareQuery computer|cpu|os|gpu|monitor|power -CaptureRoot <新目录>`，在产品启动分支前 exit，每个进程隐藏且有界等待。6 worker均自然exit0，未启动 Inkeys。

- 沙箱内 WMI 5项明确 Access denied/unavailable，电源项 available：hardware-only-results.json。
- 同一只读查询在隔离外 computer/cpu/os/power available；gpu/monitor 各4个WMI null字段记录 partial/errors：hardware-only-unrestricted-results.json。不能因字段为null猜设备值，也不把沙箱权限错误误当产品源码错误。

## 合同与有效性回归

纯CPU probe调用实际Session、Coordinator及Controller assembler。覆盖关闭无采样 clock/分配/query、QPC负值/未知/mask、容量/并发唯一发布、源身份和偶数sequence、失败/noop/非S_OK/旧输出拒绝、Down确认后的Move自身贡献、Stored Up/失败重试、96次接触回收、receipt满不双计、真实初始化prior/跨Begin、保持真实ULW失败attempt、当前帧设备换代与旧帧冻结、细项无伪start、held eraser/reconnect idle断链。

脚本120项验证实际数值、状态和退出码：严格JSON类型/重复键、分位数P99/1000门、嵌套不求和、prior首笔和工具分组、低work但530ms间隔进入前100候选、idle/代次/输出断链、掉样前缀、missing/invalid/unavailable、失败/prepare failure/noop/过期或重复publication、Stored无假raster、分享改名、最终进程身份/有限权限同句柄/完成、双流各12000行无死锁、timeout不kill软件、硬件worker隔离、已有实例/manifest预拒绝、CMD退出码、合成PE/ZIP/SHA/AST/PS2禁用API扫描。

## 尚未现场验证的边界

未运行真实 Inkeys CMD→SuperTop/UIAccess→RTS/D3D采集、UAC权限跃迁、GPU时间查询、Win7 PS2/.NET2、兆芯/低性能设备或光学测量。WindowsPS5.1/pwsh数值/mock测试与本机硬件读取不能替代这些现场验证。用户脚本以Win7 PS2为兼容目标，不能声称该平台已实测通过。

正常关闭承诺为不执行新增采样工作，仅保留nullable门；没有正常模式FPS实测或机器指令逐条相同的结论。诊断模式额外QPC与旧RuntimeMetrics owner/proof成本在schema中公开，不自动开启像素/着色器诊断。固定前缀满后不覆盖，崩溃可丢尚未导出的RAM数据；成功Present为实际内容软件返回证据，不是原始坐标像素或触笔到光子。Laser/橡皮不支持完整内容落地证明，但输入消费与帧阶段仍保留。默认高帧率前缀约68秒，建议30–45秒短轮、多轮复现。

## 最终交付

最终两套正式Release包均由本次自然exit0构建的EXE和同版冻结用户脚本生成，并核对EXE/脚本/README/ZIP所有stream的SHA256：

- x64：Build/Draw3PerformancePackages/Draw3Performance-x64-20261015-002138-b302886a00484e84bdcdd1eb3438474d.zip，28.31 MiB，适用于兆芯等x64电脑。
- ARM64：Build/Draw3PerformancePackages/Draw3Performance-arm64-20261015-002142-00dd34ca17a944d1a7755a812d9929a7.zip，27.84 MiB，适用于ARM64电脑。

各包包含Inkeys.exe、CMD、3个用户PS1、README、manifest，共7文件。默认输出 CMD 文件所在目录下“Inkeys-Draw3-Diagnostics”的新会话目录；建议30–45秒短轮后正常退出，自动生成中文report.md、summary.json和top-long-frames.csv，保留硬件/初始化/原始CSV/普通日志/最终身份与完成状态。回传整轮目录，不只报告。用户包不附开发Build/Test/mocks，不改常规发布ZIP。

PRD A1–A8 均按本记录所述代码、纯CPU、离线/mock、真实只读硬件及编译证据验收；真实GUI/目标硬件边界明确保留。Trellis收尾使用--no-commit，工作区保留供用户审阅，未commit/push。

## 2026-10-15 默认输出目录跟进

按用户要求，采集器默认根目录改为 CMD/PS1 所在目录下的 Inkeys-Draw3-Diagnostics；不依赖命令行当前工作目录，显式 -OutputRoot 仍优先。仅修改采集脚本与 README，复用此前已验证的 Release EXE 重生成 x64/ARM64 诊断 ZIP。AST 语法、中文空格路径的默认表达式、显式覆盖、原 BOM/CRLF、两包各 7 文件及 manifest/源文件/ZIP 内容 SHA256、git diff --check 全部通过。未启动应用，无需重新编译 C++。结果见 Build/Draw3PerformanceValidation/output-root-followup.json。
