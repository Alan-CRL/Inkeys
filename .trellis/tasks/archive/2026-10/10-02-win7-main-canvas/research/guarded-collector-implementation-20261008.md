# Win7 guarded 单 CMD 采集实现（2026-10-08）

继续原任务。本分工仅更新 collector、wrapper、中文 README 和独立 mock 证据；不改 C++/shader、工程、旧包或现场日志，不启动真实产品 GUI，不构建 Solution，不 commit/push/archive。

## 行为与边界

- 包身份 Inkeys-Win7-Guarded-x64-Release；probe_schema=guarded-20261008，capture schema=win7-guarded-1。每可用后端 105=旧87+guarded18；旧 matrix69 保留。manifest 严格匹配 EXE SHA、包名、schema 与105/69/18，缺 manifest 仍安全采集但最后标 incomplete。
- 唯一 Run-Win7-Diagnostics.cmd 保持原字节，三阶段同 EXE：01-pixel-probe（guarded/full都清除）；02-original-shader（guarded/full都清除）；03-guarded-shader（guarded=1/full清除）。每阶段 diagnostics=1。identity 和 summary 记录实际环境；finally 恢复调用者全部三个环境原值，含原值缺失的情况。
- GPU probe 仍 WARP 与 Hardware 独立进程、不向缺少新 WARP header/driver 的旧 EXE 发送 Hardware CLI。严格单一 header105/69/18与精确 requested/actual driver。每 driver 等待900000ms（15分钟），两后端最坏预算30分钟；按旧query510秒+新36次EVENT108秒=618秒另留初始化/CPU余量。没有人为生产 sleep。
- 保留原自有 Process.Handle、WaitForExit、真实 ExitCode、timeout精确 Kill/5000ms wait/Dispose，以及完整双流。Hardware exit3 为 unavailable；其它失败/timeout/exception如实记录。原版自检 FAIL 仍继续两正常应用轮。
- 原版 production/diagnostic/sequence/matrix 摘要原行独立保存。guarded 只有18个合法唯一终态、一个完整汇总、精确失败数及reference_unavailable计数一致时才记 PASS/FAIL；缺失、重复、错误总数、错driver、reference/comparison矛盾标 incomplete。候选FAIL不能被原版CLI退出0遮盖；原版退出1不能覆盖候选实际PASS。
- reference unavailable 必须比较 unavailable，不能冒称像素相等；reference available必须有PASS/FAIL比较，comparison FAIL不能伴终态PASS。18项候选自动检查也不代替画布视觉验收。
- 新 README 与中文提示明确候选保留全部数学、DefaultInk、细笔、AA、光影和双源公式；删除功能群变体只存在原隐藏诊断，不是两应用轮候选。每轮新空白页先普通笔/细笔/变宽/荧光笔，等待，再橡皮/激光、撤回重做、选择穿透、定格返回；正常退出后分开保存观察。诊断开关不写配置；正常应用保存测试页的既有行为仍如实说明。

## 修改与格式

- Collect-Win7.ps1：新 schema/数量/候选摘要校验与环境身份、预算900000。
- Run-Win7-Diagnostics.ps1：原版/guarded三阶段、严格新manifest、summary独立候选结果、三个环境隔离恢复、中文步骤。
- README-Guarded-Win7.txt：新包中文步骤、效果保留/结果分级/回传范围。
- Run-Win7-Diagnostics.cmd 未修改；仍是唯一CMD。

Collect/CMD保持ASCII+CRLF；wrapper/新README为UTF-8 BOM+CRLF。限定 git diff --check 通过，不进行无关格式化。

## 验证证据

根目录：C:/Users/alan-/.codex/visualizations/2026/10/03/01a100c6-aa67-7cc3-9d01-6cda328f70c3/win7-guarded-20261008/collector-test。

- run-1/results.json：47项实际Windows PowerShell5.1检查均PASS。覆盖空格路径、完整三stage四次调用、8份大双流、baseline FAIL/candidate PASS、candidate FAIL或ERROR但CLI0、Hardware3 unavailable及Hardware1、失败仍继续、missing/duplicate terminal或summary、错误计数/参考计数、错误header/driver、manifest各字段拒绝、原版正常轮异常、缺raw、缺manifest、旧EXE安全门、直接collector模式、唯一CMD0/1传播、同host finally恢复及PS2子集静态解析。
- frozen-recheck/results.json：3项冻结源码复验PASS；重复前3核心分类，并验证三个原本不存在的环境变量最终仍不存在。run-1最初3case早于reference/comparison一致性补充，冻结复验与后续CMD覆盖保证最终源再次通过这3项。
- timeout-result.json：测试专用 ScriptMethod 在实际console mock发出ready marker后模拟900000ms截止；原collector精确清理该自有PID并Wait5000/Dispose，验证PID不存在，随后Hardware18项仍PASS。没有真正等15分钟，没有修改生产脚本等待或按进程名杀进程。
- 实际WinPS parser无语法错误，静态PS2 API/语法子集未见Get-FileHash、ConvertFrom/To-Json、ordered/pscustomobject、::new、using、Wait-Process等新版依赖。

## 最终 SHA-256

| 文件 | SHA-256 |
| --- | --- |
| Collect-Win7.ps1 | 13B3A7B27D8733D5FEDC317E63E0B5783C055C858A27FEFD6E97E7C410EC639F |
| Run-Win7-Diagnostics.ps1 | D00102B5C238CF87BE5FCB9E1AFC878CE7C9ECCB1CC06292092D63822DF6BEED |
| Run-Win7-Diagnostics.cmd | F555222FCED3A2D8A9F18B3272FB5082694334739653BCBD9F6043EE5CE53C9D |
| README-Guarded-Win7.txt | 630C9142EC2D98CBC23EA918176E0D2DA17DE9C0F165D6C580AE7CD151997951 |

## 未验证与交付责任

- 上述是Win11上Windows PowerShell5.1与mock证据，不是实际Win7 PowerShell2.0运行。
- 本分工没有运行正常产品GUI、真实shader像素CLI或Solution；主会话负责两架构完整构建、真实105项/Headless、EXE资源与数学身份、实际WinPS ProbeOnly及新9文件manifest/ZIP。
- Win7 WARP实际恢复、细笔/荧光笔/擦除光学效果、全部人工步骤待用户同包验证。此前VMware非零源但延迟刷新的ULW问题不因本候选自动宣告修复。
