# Draw3 性能诊断完整实现检查

- Reviewer: `diagnostics_check`（已派发的 `trellis-check`，未递归派生）
- Date: 2026-10-15
- Active task: `.trellis/tasks/10-14-draw3-performance-diagnostics`
- Scope: Draw3 生产 Host/RTS/Contact/Controller/Renderer/Presenter、Performance 记录器与导出、启动合同、用户端 CMD/PowerShell/离线分析/包装及测试。
- **最终检查结论：PASS。** 已完成真实生产路径集中核查、全部已发现阻断项的修后回读及最终验证证据核对；产品与脚本保持只读，本 reviewer 未覆盖并发编辑。构建及 CPU/脚本测试由主会话统一执行，reviewer 实际读取自然退出码、日志与成品包校验结果。不启动 GUI、隐藏 HWND 或 Computer Use。新增 `.trellis/spec/native-desktop/draw3-performance-diagnostics.md` 及索引已纳入源码/字段合同核对。

## Findings (fixed)

本 reviewer 没有直接修改产品源码。以下问题已按所有权协调实现者修复，并核对源码及下方相应构建/回归证据：

1. **成功 Up 的 64 个 pending 槽泄漏**：正常 CaptureStored 清 live，runtime 回收不再调用 Invalidate；完成呈现只清 stored，sidecar pending 永不释放，超过 64 次成功接触提前溢出。当前源码在真实 Stored Up/不支持/未准入/取消/失效终态回收关联槽，保留历史 receipt，新增 96 次实际 assembler 成功/不支持终态回归。
2. **空闲后首笔/帧起点前计时归错**：旧实现 `StageFrame` 在关闭时借 lastFrame，可能丢弃工作或写入已结束上一帧。当前源码使用固定 prior-work buffer 与 wait 分开，导出 `prior_*` 及独立 mask/count，明确不纳入 frame work/owner CPU；跨 initializeStroke 内部 Begin 的 scope 按实际 QPC 起点拆段。新增真实 Modeler 的关闭帧之后初始化及跨 Begin 回归，closed consume/down/tool facts 亦携入。
3. **真实 ULW API 失败被改成无 API**：UpdateLayeredWindowIndirect 失败先记录 attempted=true，再由 fail lambda 覆写 false，漏计实际失败。当前源码已将实际 attempt 事实保持为单调 true；HRESULT 继续保留，不改原 API/flags/调用次数。
4. **工具字段缺少生产者**：frames.tool_mask 原始实现始终为 0。当前源码在诊断门内从实际有效 DrawingTool 取位号，复用既有活动 contact 遍历，包含倒转/右键解析后的橡皮；effective_tool、schema/报告已同步。
5. **已知采集不可用未进入新完成状态**：Host 的 runtimeMetrics.unavailable 原已能检测 session/Controller 旁挂失败，新导出却只按频率判 unavailable，可能空数据仍 capture_complete=true。当前源码已把该事实传入 ExportHost 的 unavailable 参数及 session/export 聚合，脚本新增 unavailable 完成状态回归。
6. **JSON 身份宽松类型与重复输入键**：脚本关键 schema/exit_code 原用 int cast，可接受 JSON bool/string/小数；输入仅去重 callback_token，同一 publication key 换 token 可重复计延迟。当前脚本只接受已解析 JSON Int64/真正 string、核范围，按每 Host 完整 publication-key 去重，已新增 bool/string/小数/重复键负例。
7. **准备失败与 noop 归类混同**：分析器原把所有 attempted=true/api=false 都叫 noop，包括明确失败 HRESULT 的准备/Map 失败。package 已分 preparation failure、实际 API 非 S_OK、成功/空提交，冻结双 PowerShell 套件均通过。
8. **receipt 满时一次消费双计**：实际 Scope.Consume 与 Register/NoteConsumed 先后重复调用；保留成功可 Find 去重，但容量/无 pending/非法 source 时没有键可找，seen/drop 会翻倍。当前生产消费只有 ProcessScope 单站点，Register 改为支持性/工具标记，legacy NoteConsumed 不再重复新 Consume；真实 frame consume/down facts 先于采样容量判断，新增 cap=1 两次消费回归。
9. **离线结果移动目录后被拒绝**：脚本原要求分析机当前 Root 等于现场绝对 output_directory。当前仅离线分析允许移动/改名，仍核原 recorded 目录叶名=session_id、launch/completion 相同原身份、collector 同会话/EXE。采集器继续严格验证真实输出路径与 PID/FILETIME/EXE。
10. **中途恢复设备的 frame.device_epoch 滞后一帧**：Controller 先 Begin，再 RecoverFromRuntimeFailure；原 CaptureGraphics 增加 gpuEpoch，却没有同步尚未封口 current.epoch。当前 CaptureGraphics 共用 AdvanceDeviceEpoch 冷路径 helper，只更新 open 当前帧，已封口旧帧不回写。CPU probe 使用同一 helper 验证先旧帧、新恢复帧、无真实 D3D/query。
11. **冷初始化细项伪 duration**：DXGI device/adapter/factory、map_no_overwrite、GPU queries 原在真实操作返回后才取 Clock 作 Initialization.start。当前这些项 start=0，保留实际 HRESULT/end；D3D create 和 Host aggregate 已有真实 start，未改。probe 验证 capability start 未测且原 E_FAIL 保留。
12. **真实已收敛接触等待未断 idle 链**：Controller 的 eraserIdle/awaitingReconnect 原只有旧 BeginIdle/WaitForWake。当前该实际分支先加 nullable IdleBoundary，IdleWait scope 仅包原 while/单 wait；原唤醒、timeout、Exit 条件和循环不改。其它 idle/retry WaitForWake 补纯诊断 span，错误退避保持原 active chain，仍诚实呈现失败 stall。probe 验证 physical=1 仍主动断链、3000 ticks 等待单列而非 frame work。
13. **未解码 callback 误默认 Touch**（主会话补充发现）：Input.device 初值改为 UINT32_MAX=unknown，真实 publication 后才取快照设备类型，官方 spec 已同步。
14. **最终 UIAccess 进程读取/等待权限过大**（package 补充修复）：采集器改用 native ProcessOwner，以 QUERY_LIMITED_INFORMATION|SYNCHRONIZE 持同一进程句柄读取 EXE/FILETIME、等待和取退出码，finally 关闭。access denied 不当作正常快速消失；已消失仅在同身份 completion 存在时继续。不按名称查杀，不扩软件控制权限。
15. **低工作成本但长间隔的候选遗漏**（package 最终完善）：top-long-frames 原仅按 frame/prior 工作成本保留，可能在超过 100 个候选时遗漏活动/成功呈现长间隔。当前 derived diagnostic_peak_ms 取工作、有效 prior 阶段、活动/成功呈现间隔的最大值，不求和，仍固定 100 个候选。报告明确原等待归前帧，间隔异常不能单独判调度或硬件型号原因；105 帧中 10 ms work/530 ms gap 的回归已通过冻结双 PowerShell 套件。

## Findings (not fixed)

没有遗留的源码或合同阻断项。上述最后同批修改已只读回核，与更新后的官方 spec 一致；最终构建、无窗口回归、冻结双 PowerShell 套件及 ARM64/x64 正式包证据均已取得并核对。没有扩大审查到无关问题。下方未测现场边界是验证范围限制，不宣称已经通过。

## 核查的真实调用证据

### 关闭路径、预算和线程

- `PerformanceSession::CreateHost` 空 root 在 exportMutex、目录/Session 分配前返回；正常 Host 只保留 nullptr，Renderer/Presenter/RTS/Controller 的新增 Clock/统计/GPU 调用均在非空 gate 后。
- `PerformanceScope`/InputScope/ProcessScope 的空指针路径不读 Clock、不写统计、不分配/写盘；Contact writer 仅在 trace/publication 回调存在时记录纯值。Headless 不应链接新 JSON/GPU 实现或改变 MTd CRT，当前改为 noexcept 函数指针边界，真实 producer 仍写同一采集函数。
- 新 payload 有最大容量及 static_assert 32 MiB，原 RuntimeMetrics 独立 32 MiB 合同保留；不扩正常 ContactSnapshot/Record、不改准入、模型、渲染质量、Map flags、shader 选择、帧预算或活动 wake 策略。
- 每个 RTS callback 原子预留唯一 Input 槽，各 writer 不覆盖已保留前缀；owner 独占 receipt/frame/query，导出只在 RTS Shutdown 后、drawingThread 真 join/drawing 销毁后调用。未证明真实驱动 callback 交错已在目标机测试，不由静态顺序冒称现场验收。
- GPU 8 个固定 query slots，32 帧稀疏采集；每轮只轮询固定 slot，GetData 都 DONOTFLUSH，S_FALSE 返回，不 spin/Flush/wait；owner stop 仅固定池一轮并输出未完成/替换状态。

### 输入与成功呈现

- RTS Down/Up/Packets 的第一条新诊断语句建立 callback scope；decode 前源 QPC 保留，批次 packet_count 和 last-packet 既有合并策略不变。
- Down 在不可变 snapshot/真实新 generation/sequence=2 的发布接缝记录；Move/Up 在持 writer、even sequence+2 release commit 前锁存纯值，未在 Publish 返回后回读 record 猜身份。
- owner 消费记录真实一致 snapshot 的 record/generation/sequence/source/admission；Register/Adopt/ObserveLiveRaster/CaptureStored/FreezeCandidates 使用生产 assembler，未用 lastConsumedSequence/hasContent/邻近帧补成功。
- Live 要实际非预测几何、当帧 raster、完整 scene/raster/output/frame 和合成；Stored 要可见 item/contentGeneration/afterState、当前权威 L2 与投影覆盖，允许纯值 Up 跨 record 回收/失败帧保留。
- `Performance::Confirm` 仅由通过生产 currentFrame/output/composite/L2 gate 的 owner 调用；可信内部参数不是外部 API 安全边界。新 software landing 还必须 `LastPresentApiSucceeded()`（actual API 且 S_OK），拒绝失败、noop、DXGI 非 S_OK、旧帧/输出。
- 已确认 receipt 端点应冻结；新版本 Adopt/Raster/Stored 及 ProcessEnd 已加入 confirmed 门，避免同一 sequence 的停笔收敛改写首个成功回执。
- QPC 保留 int64 原值，未知端点为 0，阶段负值/跨线程量化逆序不 clamp 成零；报告只是实际内容 software Present return，不是原始坐标像素/光学可见/触笔到光子。

### CPU/ULW 与分析

- ModelReset/Update/Predict、ingress、geometry、composite、Map/copy/Draw、Present/ULW Copy/Map/rows/Update 和 deadline/idle wait 在原调用周围采样；父子段为 inclusive，不求和。
- ULW Update 后的 GetLastError 在 scope Finish/QPC 前锁存；Map HRESULT 先存局部，不改正常 READ/flags=0、dirty 拷贝模板或像素扫描开关。新性能模式不强开 StartupEnvironmentDiagnosticsEnabled。
- 离线数字先按 long 解析/取差，再转 double/frequency；阶段有 observed/invalid mask，合法观察到的零值与未知值区分；P99 小于 1000 为 null。
- 原始 CSV 按行读取，有文件/行/Host/frame/metric 上界；seen/retained/dropped 与 raw 行数、completion 总量交叉核对，不把保留前缀说成全程完整。
- 帧 work、连续 active_chain 间隔、同目标/revision/epoch 成功 Present 间隔分开；idle 断链，不将闲置后间隔称低 FPS；失败/noop/输入无内容 proof 不进成功延迟。
- 包先核 manifest/EXE/脚本 SHA，硬件 worker 在书写前结束；超时 Kill 仅对刚创建且持有 Handle 的硬件 worker，软件 timeout 仅返回 incomplete，不按名称关闭应用。
- 启动等待使用最终 PID、完整 FILETIME、EXE 和 completion；bootstrap exit(0) 不代表成功；输出原始文件不被 report/summary 覆盖。

## Verification

构建、产品 CLI 和包套件由主会话串行统一运行；本 reviewer 未并发构建/测试，实际读取下列最终结果。构建为现有 `InkeysRepo.sln`，实际使用原生 ARM64 MSBuild、同一 PowerShell invocation PATH 规范化；x64 锁定依赖仅恢复现有 cache，无修改 manifest/版本/SDK/编译器。

| 验证项 | 最终结果 | 证据 |
|---|---|---|
| 全工作树 whitespace | PASS | 最后 `git diff --check` 无 diagnostics |
| 既有文件 BOM/CRLF/EOF | PASS，28 文件、0 failures | `Build/Draw3PerformanceValidation/format-audit.json`；reviewer 另核 27 tracked 源文件及最后三文件 |
| 冻结 PowerShell AST/UTF-8 BOM/CRLF、CMD ASCII | PASS | `script-static-audit.json` 及 reviewer 最后冻结版复核；五顶层 ps1 均无 parse error |
| i18n sync/check、工程登记镜像 | PASS，362/362 | `check-startup.md`：XML 真解析、两个工程登记一致、原格式保持 |
| 完整 Solution Debug / ARM64 | PASS，自然 exit0、0 errors | `build-debug-arm64-final.status.txt/.log/.stdout.log/.stderr.log` |
| 完整 Solution Release / ARM64 | PASS，自然 exit0、0 errors | `build-release-arm64-final.status.txt/.log/.stdout.log/.stderr.log` |
| 完整 Solution Release / x64 | PASS，自然 exit0、0 errors | `build-release-x64-final.status.txt/.log/.stdout.log/.stderr.log`，自然耗时 20m52.67s |
| Debug ARM64 performance（修后） | PASS，自然 exit0 | `performance-debug-arm64-final.status.txt/.stdout.log/.stderr.log`：recorder/proof failures=0、production content assembler PASS |
| Debug ARM64 launch/config/Headless | PASS，三入口自然 exit0 | `cpu-tests-debug-arm64.json`、对应 stdout/stderr；Headless `--no-window` PASS，之后相关源码未改 |
| Release ARM64 performance/launch/config | PASS，三入口自然 exit0 | `cpu-tests-release-arm64.json`、对应 stdout/stderr；recorder/proof=0、assembler/launch/ConfigBoundary PASS |
| Release x64 performance/launch/config | PASS，三入口自然 exit0 | `cpu-tests-release-x64.json`、对应 stdout/stderr；recorder/proof=0、assembler/launch/ConfigBoundary PASS |
| 冻结包 WindowsPS 5.1.26100.9711 | PASS，120 断言、自然 exit0 | `Build/Draw3PerformanceTests/offline-276888d307384fb4b0378fd54e913b65/test-results.json` |
| 同冻结包 pwsh 7.6.5 | PASS，120 断言、自然 exit0 | `Build/Draw3PerformanceTests/offline-2affd768928f4ec5a1cefbea352eb1c3/test-results.json` |
| 正式 ARM64 Release ZIP | PASS，7 文件、源/manifest/各 ZIP entry SHA 一致 | `package-arm64-validation.json` |
| 正式 x64 Release ZIP | PASS，7 文件、源/manifest/各 ZIP entry SHA 一致 | `package-x64-validation.json` |
| 真实 HardwareQuery 只读分支，runner 隔离内 | PASS，六 worker 自然 exit0、worker_failures=0 | `hardware-only-results.json`：五 WMI denied 如实 unavailable，power available |
| 同只读分支，runner 隔离外 | PASS，六 worker 自然 exit0、worker_failures=0 | `hardware-only-unrestricted-results.json`：computer/cpu/os/power available；gpu/monitor 各四 null 字段如实 partial/errors |

上表除明确 `Build/Draw3PerformanceTests` 的路径外，最终验收日志/JSON 都位于 `Build/Draw3PerformanceValidation`。已核自然退出码和完整关键输出，所有产品入口 stderr 为空。ARM64 最后增量各 13 warnings，x64 完整构建 397 warnings，包含外部 modeler 窄化、既有工程及 probe nodiscard；均 0 errors，未为 0 warning 做无关修改。Performance/Launch 新 TU 没有找到直接编译 warning。

无窗口性能 probe 覆盖默认 off clock/分配/query、固定容量/并发 callback、实际 publication key、生产 assembler Live/Stored/失败/noop/旧 frame/旧输出、超过 64 pending 回收、receipt 满不双计、真实 Modeler prior/跨 Begin、API failure、共享设备变代 helper、physical=1 held idle 断链。双 PowerShell 套件核真实数值/分母/状态，包括严格 JSON/重复键、准备失败、Host unavailable、prior/工具、迁移改名、105 帧低 work/长 gap 候选、idle、native owner、硬件 timeout、双流各 12000 行排空、超时不 Kill 软件、CMD exit、PE/七文件 ZIP/SHA、PS2 禁用 API 静态扫描。合成测试不是现场设备实测。

正式产物位于 `Build/Draw3PerformancePackages`：

- ARM64：`Draw3Performance-arm64-20261014-172642-bd8cabca44f64076bb487b4077f9f650.zip`，27.84 MiB，ZIP SHA256 `8D8E2DFA5EFEA44B002A37CA0047389110B1127400CA62BE0E2B10FCB8C463F9`。
- x64：`Draw3Performance-x64-20261014-174926-3a9cb2fdc9084791a93d127ee6b6f7b6.zip`，28.31 MiB，ZIP SHA256 `700D1EEE2DB23B3E5718653216F289733513A5389F5A8E26BCF4414DD792A327`。

**现场未测边界保留**：未运行可见/隐藏 GUI、真实 SuperTop/UIAccess/UAC 权限跃迁、RTS 笔输入、真实 D3D/GPU 时间查询、Win7 PowerShell 2.0/.NET2、兆芯及其他低端机。软件成功 Present 返回不是光学可见或触笔到光子证据；上述兼容性与正常模式实际帧率不能由编译、CPU probe、mock 或本机 WMI 数据推定。

Git/环境：未 commit/push，未变更 SDK、依赖版本、工具链或正常模式参数。reviewer 仅写本审查记录；主会话继续完成验收、无 commit journal 和任务归档。
