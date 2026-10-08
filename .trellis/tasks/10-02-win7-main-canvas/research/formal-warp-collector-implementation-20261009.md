# Win7 WARP 正式修复的采集器变更边界（2026-10-09）

最小行为缺口：正常 Win7 WARP 自动选择修复程序后，既有未设置 guarded 的原版对照阶段会失去原版身份。wrapper 实际拥有三阶段临时环境，故仅在该处将覆盖值明确为 0/0/1。

必要文件：Run-Win7-Diagnostics.ps1 改覆盖值、中文原因与新包身份；README-Guarded-Win7.txt 更新普通双击默认修复和可选诊断含义。Collect-Win7.ps1 的身份记录已泛化读取实际环境，暂无需修改；唯一 CMD 不变。

不调整 105/69/18、schema、完整双流、超时、退出判定或 Hardware unavailable；不修改 shader、C++、工程、旧包/报告/原始捕获。无需局部重构。验证复用旧 mock 但复制至新目录，核三阶段 0/0/1、路径空格、环境恢复、异常继续与身份边界。

## 完成实现

- Run-Win7-Diagnostics.ps1 仅四处必要变更：新包 identity `Inkeys-Win7-WARP-Fix-x64-Release`、默认 stageGuarded 从 unset 改成单字符 `0`、解释正式 Win7 WARP 自动策略的中文注释、解释恢复三个调用者变量的中文注释。Part3 仍覆盖 `1`；diagnostics 仍 `1`，full 三阶段仍 unset。
- README-Guarded-Win7.txt 说明正常双击 Inkeys.exe 在实际 Win7 WARP 自动选同一已实测完整字节码，其它 OS/Hardware 沿原版；新 EXE 的正常启动验收仍区分于既有候选实测。唯一 CMD 是可选对照诊断，Part2 在旧 Win7 WARP 的预期失败不泛化到 Hardware。保留完整数学/效果和未解决定格荧光偶发、Hardware 边界。
- Collect-Win7.ps1 与 Run-Win7-Diagnostics.cmd 字节完全不变：现有 collector 会记录实际 0/0/1，strict 105/69/18、guarded-20261008/win7-guarded-1 schema、原版退出、候选独立结果及 Hardware unavailable 判断不需修改。
- wrapper/README 继续 UTF-8 BOM + CRLF；Collect/CMD 继续 ASCII + CRLF，没有裸 LF；保留 source-before/after 和逐文件最小 diff。

## 验证

证据根目录：`C:/Users/alan-/.codex/visualizations/2026/10/03/01a100c6-aa67-7cc3-9d01-6cda328f70c3/win7-warp-fix-20261009/collector-test`。旧 helper 只读取并复制，旧目录文件 SHA 未变；本轮不重写任何旧包、旧报告或现场捕获。

- `run-1/results.json`：47 项实际 Windows PowerShell 5.1/mock 全部 PASS。覆盖空格路径、完整 0/0/1 三阶段四次调用、8 份 >512KiB 双流、原版 FAIL 但完整候选 PASS、候选 FAIL/ERROR 与原版 CLI0 分开、Hardware exit3 unavailable 与其它非零失败、失败/不完整仍继续两轮、错 header/driver/数量与重复/缺失候选终态/汇总、reference/comparison 矛盾、manifest 身份拒绝、旧 EXE Hardware 安全门、直接 collector 模式与唯一 CMD 真实退出码传播。
- 其中同一 WinPS host 的 success、baseline-fail 和 manifest-mismatch 三例均恢复调用者已有的三个不同环境值；异常/失败不泄露 0/1 覆盖。
- `frozen-recheck/results.json`：另 3 项 actual WinPS/mock 全部 PASS，success、baseline-fail 和 candidate-fail 时，原先不存在的三个环境值最终仍不存在，并逐调用核 0/0/1。三个执行脚本 SHA 与最终源码完全相同；其 hash 元数据采集之后，README 另有一次 Hardware 观察边界文字澄清，保留该原始元数据并在 final-source-identity 中明确差异。
- tokenizer 与 PS2 API/语法子集静态审查 PASS；未引入 Get-FileHash、JSON 新版 API、ordered/pscustomobject、::new、using 或新版 wait。该静态及 WinPS5.1 证据不等于实际 Win7 PS2 运行。
- `source-after.json`、`final-source-identity.json` 确认最终稳定源和格式；两个变更文件的 git diff --no-index --check 没有 whitespace findings（exit1 只表示有内容差异）。严格 case/schema/超时/精确自有进程清理未改，故没有额外重复超时测试。

## 最终源 SHA-256

| 文件 | SHA-256 |
| --- | --- |
| Collect-Win7.ps1 | 13B3A7B27D8733D5FEDC317E63E0B5783C055C858A27FEFD6E97E7C410EC639F |
| Run-Win7-Diagnostics.ps1 | D6891A7ED72FA3ED046D1A01E43CA68995805550270C5BF406DCFF3F7C871C57 |
| Run-Win7-Diagnostics.cmd | F555222FCED3A2D8A9F18B3272FB5082694334739653BCBD9F6043EE5CE53C9D |
| README-Guarded-Win7.txt | 3E64770A3F94995F528644DFA61B55E17028054E30939CE8356082A45FB9954F |

## 后续责任/限制

本分工不编译、不运行正常产品 GUI、不执行 Computer Use、不 commit/push/archive。root 负责两架构 Solution、真实像素 CLI/Headless、实际新 EXE 的 WinPS ProbeOnly 与新 manifest/ZIP。旧 Win7 现场已证明同一 shader 候选，新增正式默认 EXE 仍需正常启动回归；Hardware 和荧光定格偶发不在此次 collector 修复验收中。
