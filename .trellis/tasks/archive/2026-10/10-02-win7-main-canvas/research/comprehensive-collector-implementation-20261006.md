# 全面单CMD采集设计/实现（2026-10-06）

## 写入前设计选项

- 保留现有 pixel-test stdout/stderr与identity键作为WARP记录；仅增加hardware独立文件/keys。避免破坏此前分析脚本，并保留每个精确自有子进程完整双流。
- 大矩阵每driver等待600000ms，两个probe顺序执行，最坏约20分钟；没有故意sleep，GPU每步有界等待仍由fixture控制。600秒实际等待不做本机验证，按GPU最终用例数核上界。
- Hardware返回3明确unavailable，不当PASS；其它非零failed。probe失败/硬件不可用仍收两轮正常应用，普通应用保持默认后端选择。
- summary版本升级comprehensive-2：列出期望三阶段/两driver、脚本hash/包manifest身份、各probe退出与标签、complete/incomplete状态。目录形态由脚本保证，用户仅交扁平单stage不得被当完整单CMD结果。
- PS2兼容：复用Start-Process NoNewWindow+redirect及Handle/Wait/Exit模式，无新版cmdlet或.NET构造语法；新wrapper继续finally还原环境，不改用户持久化设置。
- 保留旧默认/ProbeOnly/SkipProbe含义，但probe阶段扩为两driver；hardware unavailable使完整摘要completed-with-unavailable，退出0仅表示收集完成，不能等价两个GPU PASS。

## 实施/验证结果

已完成且代码/脚本冻结。CPP编译、真实fixture自检与最终包验证由主会话负责；本分工不启动正常产品GUI，不solutionbuild/commit/push。

### 文件与实现

- IdtMain.cpp：新增 --draw3-renderer-pixel-test-hardware，在原argv分支中case-insensitive识别，严格argc=2，quoted/trailing space由argv规范化；额外参数返回2早退。调用RunRendererHardwarePixelTest，不读产品config或进入普通GUI。原WARP入口及局部环境诊断激活不改。
- Collect-Win7.ps1：WARP与Hardware各自独立进程、两个文件、每driver开始/结束/退出/status。原pixel-test.*仍代表WARP，新pixel-test-hardware.*代表硬件；ProbeOnly阶段只采两个probe，默认再采正常应用，SkipProbe仍只普通应用，冲突开关拒绝。
- 两个driver最多各600000ms，正常应用仍手动结束；保留Handle/WaitForExit/ExitCode和精确Process.Kill，最终释放probe句柄。600秒是截止预算，不是故意等待。
- 旧EXE防误启动：先WARP，从原始输出核固定schema token与精确driver/requested行；缺身份时不向旧EXE发送新hardware CLI，记录not-run-schema-mismatch。每driver独立identity-status matched/mismatch，直接ProbeOnly即使exit0但缺/错身份也返回1。raw与真实exit不覆盖。
- 退出状态采用exit-zero/exit-nonzero/unavailable/error/timeout，避免原production4决定的exit0被当成matrix69全部PASS。Hardware仅exit3记unavailable，采集完成不等价硬件通过；WARP失败与硬件不可用同时存在时失败保留。
- Wrapper：format=win7-comprehensive-2，expected-stages、两probe/schema及当前包/EXE/scripts/manifest哈希。diagnostic-manifest.json存在时严格核包名Inkeys-Win7-Comprehensive-x64-Release、schema comprehensive-20261006、87/69及EXE SHA；不引PS3 JSON API。缺manifest诚实标身份缺失/incomplete仍安全采集；直接Collect不要求manifest。
- 三阶段schema、每driver真实标签/schema/identity/exit和四份流，以及两正常轮SkipProbe、环境、同EXE hash均核；不完整与失败区分，硬件不可用且其它采集齐备是completed-with-unavailable，不写GPU PASS。
- 中文提示为“每个后端最多10分钟，最坏共20分钟”。唯一CMD不变，暂时开启诊断，普通轮unset/full轮1，finally恢复继承环境。强调完整三阶段根目录回传；新空白页、普通笔先画和等待，再橡皮/激光各3秒和观察记录，配置无需手动开启。

### 冻结SHA-256

| 文件 | SHA-256 |
| --- | --- |
| Inkeys/IdtMain.cpp | 6DD8358EB8A13C8436028636BE459FE40EB06B9ED58CBD8CDD110CF404056052 |
| Collect-Win7.ps1 | BA82C159D110C80E9891A8A73D1693B0799194F09D071E064FED21167681DB34 |
| Run-Win7-Diagnostics.ps1 | 28D8A09091FECABDC277EF5FDB86378A9B9E108AD93E688E10C9BCCC125CC0A1 |
| Run-Win7-Diagnostics.cmd | F555222FCED3A2D8A9F18B3272FB5082694334739653BCBD9F6043EE5CE53C9D |

Collect仍ASCII/CRLF，wrapper UTF-8 BOM/CRLF，CMD ASCII/CRLF，CPP原UTF-8 BOM/CRLF保持。关键CPP/非直观wrapper逻辑有中文注释。限定文件git diff --check通过。

### 验证证据

根目录C:/Users/alan-/.codex/visualizations/2026/10/03/01a100c6-aa67-7cc3-9d01-6cda328f70c3/win7-comprehensive-20261006/collector-tests；不覆盖旧20261005或先前现场日志。

- final-check-2/results.json：30项实际WinPS5.1/mock检查PASS，包括全部三阶段4次调用、空格路径、8份>512KiB双流、WARP1失败仍继续、hardware3 unavailable和hardware1失败分开、WARP1+hardware3不抹失败、partial/missing/stage-error、环境恢复、唯一CMD0/1传播、直接默认/ProbeOnly/SkipProbe/冲突。
- 旧EXE无schema时只warp+normal+full，mock若收到未知hardware flag会显式BAD/99；最终并无该调用，summary incomplete。hardware exit0缺schema/错误driver、WARP schema存在但缺driver：wrapper及独立ProbeOnly均正确失败；WARP缺driver不发送hardware flag。
- manifest错误EXE SHA、包名、87或69数字均在调用EXE前拒绝，summary incomplete。缺manifest可收集但不冒称当前完整包身份。
- 同一WinPS host调用wrapper后原trace=original/full=1均恢复；正常轮/两个probe实际unset、最后full=1，均trace=1。用户输入两观察记录保存。
- timeout-result.json：test-only ScriptMethod覆盖WaitForExit，对实际自有console mock child立即返回600000ms timeout，原collector精确Kill/5000ms等待/Dispose路径真实执行并继续Hardware；timeout与四份流/真实Hardware结果保留，PASS。没有实际等待600秒。
- 首版timeout注入读取仍被Start-Process锁定的活动stderr，造成测试harness异常；没有修改产品为此绕过，失败目录保留。修为测试内仅500ms让mock打印header后触发，已PASS。
- 第一轮严格身份新增harness误把hardware缺schema归入“manifest错误不能调用”断言；产品实际是正确incomplete且继续两应用。修正harness并新目录完整复跑，最终30项PASS，原证据保留。
- 实际WinPS tokenizer无语法错误，PS2 API/语法子集静态核查无Get-FileHash、ConvertFrom/To-Json、ordered/pscustomobject、::new、using或新版wait命令。该检查不等于实际Win7 PS2执行。

### 未验证/边界

- 本分工未构建Solution、运行真实产品正常GUI、测试Win7光学结果；CLI真实编译/早退边界由主会话验证。没有本机Win7 PS2 runtime，两个Win7设备待用户测试。
- 600秒按最终87项/69矩阵、GPU最多约510秒query加90秒初始化估计预算；并非所有驱动JIT/系统调度实时上界保证。超时保存不完整证据，不凭超时猜修复。
- module版本不等于已加载module/完整补丁清单；script只复制真实driver日志，不强制普通应用后端。
- 原四production退出和每matrix结果是不同合同；summary完成/exit-zero不证明矩阵全部正确或画布可见。

