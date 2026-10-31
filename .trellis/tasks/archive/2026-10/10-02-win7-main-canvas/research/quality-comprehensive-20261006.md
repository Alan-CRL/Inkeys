# Win7 全面诊断候选质量门禁（2026-10-06）

结论：**FINAL LIMITED PASS**。限定为本轮冻结的诊断实现、生成 shader 字节、已有本机完整构建/无窗口验证和实际 WinPS5.1 采集验证。不存在已确认而未处理的本轮代码阻断。生产故障修复、Win7 PowerShell2 运行及两台 Win7 光学验收仍未完成，任务保持 `in_progress`。

本审查只新增本报告；没有改动冻结生产/采集文件，没有启动 Inkeys.exe、GUI、构建或重复测试，没有 commit/push/archive。复用独立研究者的 `check-comprehensive-20261006.md`（FINAL LIMITED PASS），不重复其 DXBC/header/资源与 GPU 源码审查。已读取任务 PRD/design/implement、check 上下文、全面计划、两作者冻结报告，以及 native-desktop 和 shaders 适用质量合同。

## Findings (fixed)

- 本审查未修改代码。独立预审提出的两项采集身份缺口已由作者在最终冻结前修复，并有最终负例验证：
  - File: `research/Collect-Win7.ps1`
  - Issue: 直接 ProbeOnly 曾只以退出码/status 判定，Hardware exit0 但缺 schema 或实际 driver 不符仍可能返回0。
  - Fix: 两后端分别核 schema 和真实 driver/requested 行，记录 identity-status；将 identityValid 纳入失败条件。WARP 身份不符时不向旧 EXE 发送新 Hardware flag。最终三种身份负例在 wrapper 和直接 ProbeOnly 均失败且保留原始结果。
  - File: `research/Run-Win7-Diagnostics.ps1`
  - Issue: manifest 身份曾未核本轮期望87/69数字。
  - Fix: 启动 EXE 前核 package_id、EXE SHA、schema 和87/69；错误包名/hash/数字拒绝并记录 incomplete。缺 manifest 可以收集，但完整性不冒称通过。

## Findings (not fixed)

- **生产修复及 Win7 验收未完成，属于原任务待验收范围。** 当前完整 PS 透明读回故障仍需新 Win7 矩阵结果判别，不能把 Win11 本机87项PASS或 full ULW 对照当成修复。新候选保持正式 shader/输入/渲染不变，不能以 CursorOnly 替代完整产品功能。
- **Win7 PS2 实际执行和普通/full光学对照待用户现场。** WinPS5.1 tokenizer、PS2语法/API静态子集及 mock 通过不等于 PS2现场通过。正常应用的 adapter 和独立 probe 分开记录；用户仍需两次可见观察及完整三阶段根目录。
- **最终 ZIP 尚待主会话生成核验。** 本报告不声称 ZIP CRC/逐文件身份已通过。主会话需在冻结文件基础上完成 manifest/README、唯一CMD入口、ZIP CRC/hash与逐文件比对；这项交付工作不要求再次构建或启动产品。
- **600秒预算不是驱动/系统硬实时保证。** 87项中有界 query 最坏约510秒，额外90秒只是资源/CPU余量。超时路径保存部分双流并精确清理自有子进程；没有实际等待600秒来冒充长时运行证明。
- 本轮开始就缺失的旧224927六文件仅记录于 `missing-before.json`，未计入24项保持PASS；未修改无关既有任务/发布差异。

## Verification

证据根：`C:/Users/alan-/.codex/visualizations/2026/10/03/01a100c6-aa67-7cc3-9d01-6cda328f70c3/win7-comprehensive-20261006`。以下编译/运行复用主会话和作者已有结果；本审查仅执行只读核对及限定 `git diff --check`。

- **Lint / 静态检查：PASS（限定范围）。** 本轮 tracked 源码、collector和spec的 `git diff --check` 无输出/exit0；作者格式检查及实际WinPS解析通过。独立重新hash IdtMain/Collect/wrapper/CMD四文件与冻结值相同；UTF8 BOM/ASCII及CRLF原格式保持，四文件 lone-LF=0。没有独立 C++ linter 被声明为已执行。
- **TypeCheck / 编译：PASS。** 完整 `InkeysRepo.sln Debug|ARM64` exit0/52.94秒、`Release|x64` exit0/161.59秒，均900秒预算。已核实际 `build.ps1` 用 vswhere 定位 ARM64 MSBuild，同一 PowerShell invocation 删除重复PATH并设置 node reuse禁用；保留PptCOM依赖，无新工具链/工程绕过。这里的C++类型检查由完整编译完成。
- **Tests：PASS（本机）。** 两架构 WARP/Hardware各87个唯一case均PASS，共四轮；旧production4、diagnostic7、sequence5和新matrix69分列，绑定失败与deviceRemoved均0、visible=0。两架构 Headless `--no-window` exit0。独立报告已直接核四份raw终态，不只依赖进程退出码。
- **真实 WinPS5.1 ProbeOnly：PASS。** `actual-probe-result.json` 最终顺序复测 exit0，x64双后端各87PASS，两driver identity matched，完整双流；首次因已有Inkeys进程拒绝的资料保留，最终证据没有与argv并行扰动混用。`argv-result.json` 六例 quoted、大小写/尾随空白、额外参数分别0/0/2。
- **最终采集 mock：PASS。** `collector-tests/final-check-2/results.json` 30/30PASS，覆盖空格路径、八份大双流、四次顺序调用、非零继续、hardware unavailable独立分类、partial/stage错误/旧schema/身份负例/manifest负例、直接入口/唯一CMD退出传播和同host finally环境恢复。`collector-tests/timeout-result.json` PASS：测试专用WaitForExit立即返回timeout，真实自有console child被原collector精确Kill/等待/Dispose，参数600000，仍继续Hardware。不是实等600秒。
- **Shader/资源/资料保持：PASS。** 复用独立审查23个header数组与CSO/hash、23个OSGN与正式PS、旧六数组逐字节同一，以及FullRecompiled实际ISGN/OSGN/SHEX/STAT同一的结果。`binary-and-preservation.json`及独立复核确认两EXE嵌入301–304匹配正式CSO、8个正式CSO/源/现场资料/旧包共24项保持。没有修改生产 HLSL或资源链。
- **Spec和范围：PASS。** `input-and-ink.md` 现有Scenario已同步87/69、两个probe/三阶段、600秒、退出与matrix分离、身份和Win7边界。保留历史18项语义；新增dummy/切片PS仅在显式隐藏fixture，原production4仍决定CLI退出，不把采集completed/exit-zero等同matrix或光学PASS。

最终候选 x64 Release EXE SHA256：`5D5254AAFE523640EC4F47E4AF132E25D43060B99FF426CD34666821672E672C`。manifest合同：`diagnostic-manifest.json`、package_id=`Inkeys-Win7-Comprehensive-x64-Release`、probe_schema=`comprehensive-20261006`、expected_cases_per_driver=87、matrix_cases_per_driver=69。最终打包需继续保持该身份。
