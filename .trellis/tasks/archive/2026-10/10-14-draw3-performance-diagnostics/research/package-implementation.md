# Draw3 性能诊断包实施记录

2026-10-15，package_implement。所有权仅为 `Scripts/Draw3Performance/**` 和本记录；没有修改产品 C++、项目、配置、i18n、spec，没有启动产品 GUI、构建、commit 或 push。

## 交付文件

- `Run-Draw3-Performance.cmd`：双击或传 EXE 路径；保留 PowerShell 退出码到 pause 之后。
- `Run-Draw3-Performance.ps1`：manifest/EXE/脚本身份预检，拒绝已有 Inkeys/指定 EXE 实例，唯一结果目录，测试前硬件查询，指定 EXE 的单次诊断 argv，最终进程握手、普通日志、离线分析及可选 ZIP。
- `Performance.Common.ps1`：严格 JSON/CSV、流式 SHA256、原子写文件、最终进程身份与退出等待。用户脚本以 PS2/Win7 现有 .NET API 为目标；JSON 不依赖 ConvertFrom/To-Json、CIM 或 WMF 升级。
- `Analyze-Draw3-Performance.ps1`：中文报告、summary.json、最多 100 条 top-long-frames.csv，保持原始数据可复核。
- `Build-Package.ps1`：PS5.1 开发端打包，静态核对 PE 架构及诊断参数字面量，EXE/脚本 SHA256 manifest 与独立 ZIP；不执行 EXE，不改普通发布包。
- `README.md`：双击、短轮复现、退出与重新分析说明；推荐实际架构匹配的 Release EXE（Debug/Release 优化配置 /Od 与 /Ox 不同）。
- `Test-Draw3-Performance.ps1` 和 `Tests/Fake-{Bootstrap,Final,Hardware}.ps1`：PS5.1 开发端无 GUI 数值与启动生命周期 fixtures，不放入用户包。

## 启动与采集边界

1. 本轮显式 `--draw3-performance <绝对已存在唯一目录>`；不使用环境变量穿过 SuperTop、不绕过 UIAccess、不写用户配置，不需要提前勾选普通调试输出。仅通过 manifest 认证的软件版本才能被脚本启动。
2. 测试前六个独立隐藏硬件 worker 查询 OS/CPU/RAM/GPU 驱动/显示/电源，逐项记录 null、error 或 timeout。结束/超时只回收脚本本次创建且持有的 worker，不终止用户软件。任何清理失败都会阻止产品启动，避免未结束的查询干扰性能测试。
3. 初始 launcher 退出 0 不是完成。默认 180 秒等待 final launch；核对 session、最终 PID、精确 FILETIME/UTC、实际模块 EXE、输出目录与本轮开始时间；用 `QUERY_LIMITED_INFORMATION | SYNCHRONIZE` native handle 持有同一进程身份和等待实际退出，避免 .NET Process.Handle 要求 AllAccess。UIAccess/权限实际拒绝仍明确判 incomplete，不降为 bootstrap 证据。
4. completion 必须为 final 身份，记录实际导出和退出；无 completion、过期 PID/身份、晚到握手、采集等待超时都不会误判完成，也不会任意 kill 软件。快速自然退出可在明确“进程已不存在”错误下复核同身份完成记录，access denied 不使用此降级。
5. Bootstrap stdout/stderr 保留；普通日志按 completion.log_file 与本轮时间范围辅助收集，限制 32 文件/16 MiB，不把日志名称当 owner 唯一证明。ZIP 不可用时保留完整目录和原因。

## 分析口径与完整性

- 逐行 CSV，有界索引/分布：16 Host、65536 frame、262144 input/GPU rows、每分布 262144 数值、每 CSV 64 MiB、每行 16 Ki 字符、最多 256 条问题详情。任何界限、丢样、缺文件、非法字段/时间、Host unavailable 或导出失败标记部分/失败证据；`complete` 只表示数据完整性，不是性能 PASS。
- JSON schema/PID/count/exit 等必须是范围内真实 JSON 整数，拒绝 true、"1"、1.4、1.0；重复 JSON 键、NaN、尾随文本也拒绝。CSV 校验唯一 frame/callback/publication key，逐行非法计数，不用 PowerShell coercion 或重复输入扩大成功分母。
- CPU 工作墙钟、owner 线程 CPU、嵌套各阶段、工作后的 deadline/idle wait 分开；`prior_*` 单独报告 frame.start 前首笔/初始化成本，不加进 frame_work 或当前 owner CPU。携入 down/consumed 已在总计数中，不重复求和。
- 连续活动与成功呈现间隔独立统计，idle 首帧、active_chain/输出目标/设备代次边界断链。帧序号可合法跳过无 render loop，不据此制造 missing frame。长帧候选按 `max(frame_work, 各有效 prior 阶段, 活动间隔, 成功呈现间隔)` 保留 100 条；derived `diagnostic_peak_ms` 只是排序值。low-work stall 单独提示，不归因为 prior 或具体硬件。
- Down/Move/Up/Cancelled callback 与原始 packet 分母、实际工具、publication/consume/内容成功呈现各自统计。device_type 4294967295 保持 unknown，不猜为 Touch。callback_total 是包含等待/解码/发布的整段墙钟，source age 与 callback latency 分开。
- publication key、production proof_valid 和同 frame/output/API S_OK 的可导出事实共同确认。旧 proof、失败/noop、无贡献、missing endpoint、未支持工具不能补 0ms 或算落地。API 前 Map/Copy 等准备失败、非失败 noop、API 非 S_OK 状态分列。
- Live 使用真实 adopt/raster 边界；Stored Up 使用独立 consume→stored commit→Present，可跨先前失败帧，不能借旧 Down/Move raster。process_end 可能晚于 adopt，consume→process 与 consume→adopt 为各自包含段，不能串行相加。
- QPC 逆序含 1 tick 负值明确 invalid，不 clamp 0。GPU ready/frequency/endpoint/epoch 与 retained frame 校验；无 render/无 marker 的合法候选保留为未知，pending/disjoint/query_failed/unsupported 不是零成本。GPU 命令区间不是 shader 纯忙时；软件 Present 返回不是光学/扫描完成或触笔到光子。
- 最近秩 `ceil(N×p)`；P99 至少 1000 个有效样本。报告实际保留前缀覆盖与 seen/retained/dropped，容量满后分位数不代表后半程；README 建议 30–45 秒短轮、多轮复现。normal-mode disabled_path_contract 明确为静态合同，本报告不伪造关闭路径实测计数。
- 离线复制/改名整个结果目录可分析：保持原采集 session/目录/EXE 身份并精确比较 launch/completion，运行中的 collector 仍严格要求真实本轮目录。

## 验证

冻结的用户脚本与 README 已交给主会话作真实 Release EXE 打包；开发端打包测试仅使用不可执行的合成 PE fixture，不能证明真实 EXE 或 RTS/D3D 的运行能力。

- Windows PowerShell **5.1.26100.9711**：最终完整套件 **120 项断言 PASS，进程 exit 0**。结果：`Build/Draw3PerformanceTests/offline-276888d307384fb4b0378fd54e913b65/test-results.json`。
- PowerShell **7.6.5**：同一冻结脚本最终完整套件 **120 项断言 PASS，进程 exit 0**。结果：`Build/Draw3PerformanceTests/offline-2affd768928f4ec5a1cefbea352eb1c3/test-results.json`。
- 120 项覆盖 strict JSON/typed negatives、数值分位数与 P99/1000、idle 断链、失败/准备失败/noop、非贡献/过期/重复 publication、掉样、invalid/missing/unavailable/uncorrelated、Stored 无假 raster、prior 首笔、工具分组、105 帧中低工作 530ms stall、GPU unavailable/epoch/合法非render、离线迁移、final PID/时间/EXE/completion、初始 exit0、双流各 12000 行无死锁、late timeout 不 kill、早期拒绝、manifest mismatch/已有实例拒绝、硬件隔离超时、CMD 退出码、PE/ZIP/所有文件 SHA、AST 与 PS2 禁用 API 静态扫描。断言核对实际数值/状态/退出码，不只检查文件存在。
- 新 PS1 为 UTF-8 BOM + CRLF，CMD ASCII + CRLF，README UTF-8 无 BOM + CRLF；无 bare LF/CR。`git diff --no-index --check` 对全新目录无 whitespace diagnostics（差异 exit 1），编码检查 exit 0。

尚未实测 **Win7 PowerShell 2.0/.NET2、真实 SuperTop/UIAccess 权限跃迁、RTS 笔输入、真实 D3D/GPU 时间查询、兆芯/低性能目标硬件**；以上兼容性只具有 API/语法限制和 WinPS5.1/pwsh fixture 证据。正常模式隔离与产品 CPU probe/solution 编译由主会话和产品代理另行验证，不由包的离线测试声称已完成。

## 冻结用户文件 SHA256

| 文件 | SHA256 |
|---|---|
| Performance.Common.ps1 | 17ECA7F48088AA6E606A720EE052391ACB2CE33FF4AE9047D0AD0E41D5329DBF |
| Run-Draw3-Performance.ps1 | E841757FE83EB34F38DE71249DF30BDDED43EC0C2C8A584E53C183103DDAA631 |
| Analyze-Draw3-Performance.ps1 | 2C5F66E8EDEE6316DAFC0B7286B0524B4AB49EB45C4FA4C39C85226057ECDC52 |
| Run-Draw3-Performance.cmd | CEC8E268E069854AD0103CB2372D8712E0941382A73C13FF75870FCA5F84B385 |
| README.md | 96B537AD11A0AE6404E2F1E83B548C8C08886D0C9502A7A98EDA55EE3117939D |
| Build-Package.ps1 | 7FA8B1A7DAB54DFC21D74A4C7009D1A636A0866160FAE295CC5D0095714D77CE |
