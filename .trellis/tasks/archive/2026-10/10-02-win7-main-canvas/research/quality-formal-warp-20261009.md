# Win7 WARP 正式默认修复：最终独立质量门（2026-10-09）

状态：**FINAL PASS，限本次正式默认落地的本机质量门**。继续同一10-02-win7-main-canvas任务，仍in_progress。新正式EXE尚未在目标Win7正常启动验收；既有Aero开/关两场景已验收的是同一完整shader字节在前轮EXE的诊断选择分支。冻结本报告后，ZIP/CRC/包身份由主会话另存，不回写已冻结报告。

本审查为已分派trellis-check，无再次派发、构建、GUI、Computer Use、commit/push/归档。GPU与collector实施者明确code-stable后进行独立源码、边界、双流日志和最终SHA检查。实际构建/执行由root完成，审查者核原始退出与终态而不重复启动。

## 审查范围与行为

依据formal-warp-plan-20261009.md、prd/design/implement及check.jsonl所列规范，详细静态证据见check-formal-warp-20261009.md。跨层真实driver从GraphicsDeviceResources创建事实经presenter传到Renderer.Init/LoadShaders；OS复用动态RtlGetVersion的GetWindowsVersion，不依赖manifest或启动全局状态。

共享constexpr选择器仅将实际OS6.1+WARP设为自动guarded。其他OS/Hardware/unknown自动original。diagnostics精确单字符1才接受guarded精确0强制original、精确1强制guarded，其余回auto；不启诊断时guard值无法覆盖auto。候选创建失败传播为初始化失败，没有静默回退。27项测试调用同一实际选择器，覆盖非法值与不同OS/driver，避免复制判断。

Probe保存并恢复两个shader环境变量，显式diag1+guard0保证87项旧基线不被auto污染，18项完整candidate独立直接Create。wrapper新package id为Inkeys-Win7-WARP-Fix-x64-Release，三阶段override0/0/1，full update均unset，finally恢复三变量，105/69/18与strict schema/Hardware unavailable合同保持。README清楚区分直接EXE自动修复与CMD原版对照。

## 完整构建与环境失败记录

复用现有InkeysRepo.sln、正确PptCOM依赖和Visual Studio原生ARM64 MSBuild；同一PowerShell按仓库规则规范PATH并禁止node reuse，每次预算900s，未改变solution/project/SDK/toolchain。

| 配置 | 最终真实退出 | 用时 | 证据 |
| --- | --- | --- | --- |
| Debug ARM64 | 0 | 27.63s | ARM64-Debug/build-result.json、build日志 |
| Release x64 | 0 | 276.4s | x64-Release/build-result.json、build日志 |

第一次ARM64构建91.54s因C1902 compiler/PDBmanager环境通信失败，原始first-C1902-build.*独立保留。随后相同源码/配置/工具链在允许执行环境重试通过；没有源码绕过或关闭检查。已有DrawingController C4834、IdtMain C4244、Headless LNK4075等不因本任务顺手修改，未发现新改源warning阻断。root的最终保存检查对HiddenWindowTest原本无BOM格式假设作了更正，改为逐snapshot比较原编码；仅验证脚本修正，未改源码或重跑已正确完成的测试。

## 真实GPU、策略与Headless

审查者独立从stdout/stderr重解析以下四次运行，核唯一case名称及PASS终态、分组摘要、guarded真实像素比较和实际ShaderSelection。既看退出0也看105/27/18实际结果，不以进程退出替代内容。

| 架构/后端 | GPU唯一终态 | 共享策略 | 完整candidate比较 | 基线实际SHA |
| --- | --- | --- | --- | --- |
| Debug ARM64 WARP | 105 PASS | 27 PASS | 18，delta0 | original F55D...1A69F8 |
| Debug ARM64 Hardware | 105 PASS | 27 PASS | 18，delta0 | original F55D...1A69F8 |
| Release x64 WARP | 105 PASS | 27 PASS | 18，delta0 | original F55D...1A69F8 |
| Release x64 Hardware | 105 PASS | 27 PASS | 18，delta0 | original F55D...1A69F8 |

本机四次共420个GPU终态、108项共享策略、72个完整candidate比较全部PASS。每个比较reference_status=available、comparison=PASS、mismatch_pixels=0、max_channel_delta=0，生产4/matrix69/guarded18摘要全PASS；两种目标与非零Retain、薄线、变半径和荧光fixture按既有合同执行。GetDeviceRemovedReason在最终binary-and-preservation证据中均为成功。

两架构Headless --no-window退出0（ARM64 82.203s、x64 13.058s）；独立核stdout末尾EraserAttribute layouts=216 failures=0和PASS animation correctness，stderr均空。没有启动产品主GUI。

## 实际采集器与mock

实际Windows PowerShell5.1新x64EXE的ProbeOnly在继承diag1+guard1条件运行，collector退出0，capture为actual WinPS x64 collector final/Win7-Diagnostics/20261009-003124-283。identity记录真实请求warp/hardware、expected105/69/18、超时900000ms、actual0退出及严格身份匹配。两个probe的实际LoadShaders均输出original/F55D...1A69F8/Create0，证明基线覆盖未被继承guard1污染。

独立重新读取此capture两个双流，各105唯一GPU/27唯一策略PASS、18candidate可用参考全部delta0；这一额外执行共210GPU/54策略/36比较，不混入上表四次420计数。actual-probe-result.json与verification-results.json均PASS。普通产品主窗口没有运行，hidden diagnostic HWND未显示。

47项WinPS5.1 mock及3项冻结环境恢复重测共50 PASS，核已有值与原本不存在环境的finally恢复、stage0/0/1、错误继续与身份拒绝。final-source-identity PASS。冻结结果README元数据是最后Hardware措辞澄清前版本，执行三个脚本与最终字节相同，差异已明示，不重写冻结结果。本机WinPS5.1不能冒称Win7PS2已重新实测；PS2 API合同以既有语法/源码静态审查确认。

## 数学、资源与旧资料保留

完整guarded数组再次独立解析，仍28980 bytes，SHA256：

`2171DDA6E709A3F4CD444E6281AB64635C3F969CD71722662B04DA0DD8055384`

header文件仍7B407018...5DB9F563。未改生成器、formal HLSL/CSO、23旧数组或完整candidate字节。check-aero-guarded-20261008.md已独立证明新增positive DefaultInk gate只改变控制结构，thin/fwidth/AA/光影/各工具/双源公式、ISGN/OSGN和17资源声明均保留；本轮复用精确现场成功字节，未重新生成另一程序。

最终两EXE的SHADER301–304均作为数据资源提取，并与四原CSO逐字节比较：PS28748/F55D...1A69F8、VS12484/1485...5ECD5、UpdateCS3572/C02E...8E8CF、EmitCS5360/21F8...87732。binary-and-preservation.json statusPASS，旧23数组保持。本审查者再次逐SHA检查87不可变文件，其中42份新Win7原始capture、前轮ZIP/identity/reports与shader等全部原样；另8个他人当前工作文件也逐SHA相同。

最终x64EXE SHA256：

`2CDE02EDEE54AECDC08825C8557D792E956CFC0DEEA646ECF29974E2EF6108D8`

Debug ARM64 EXE SHA256：

`2B978C1A1692530E1E44DB986BC0112DFF9BDEDE11217361DB2F3664B316CF71`

限定git diff --check通过；四既有模块逐实施前snapshot比较原BOM/CRLF，其中HiddenWindowTest原本UTF8无BOM，不强加BOM。新增小header与报告按明确编码/换行保存。当前新增行为与input-and-ink、draw3-shaders规范一致，历史2026-10-08诊断合同已标明历史，不与默认auto混淆。

## Findings (fixed)

- 本审查未发现需改生产实现的机械缺陷。
- 新spec文字‘未来源数学变化’缺‘码’，通知root由owner改为‘未来源码数学变化’，保持原文件格式。未改选择设计或任何shader数学。

## Findings (not fixed)

- 新正式EXE在目标Win7直接启动的auto分支尚待实测。既有Aero开/关两现场已验收同一PS字节，但不是这次新EXE的正常启动验收；本机probe刻意强制original，不能写成auto可见验证。
- Win7 Hardware尚无真实验收，实际已知VM硬件创建失败并落WARP；当前只默认修已证明的Win7 WARP，不扩大到Hardware。DDI10.1不是项目要求FL11_0成功证据。
- 一次偶发定格/荧光加深与旧WARP精确JIT/opcode内部机制未唯一证明；不靠窗口/ULW/Present/输入改写猜测修复，也不声称已解决。
- 报告冻结时新的ZIP尚由root生成并单独CRC/hash核验；这项包装证据后写verification记录，不回写本报告。原任务继续in_progress。

## Verification

- Lint：PASS，限定diff与原编码/换行静态合同。
- TypeCheck：PASS，两架构完整solution真实exit0。
- Tests：PASS，四本机105GPU/27policy、72比较；两Headless；实际WinPS两个额外105/27与36比较；50 mock与恢复；资源/字节/资料保留均通过。
- 目标Win7新EXE正常启动、Win7Hardware和残留偶发光学问题：未在本轮执行，不混入FINAL PASS范围。

证据根目录：C:/Users/alan-/.codex/visualizations/2026/10/03/01a100c6-aa67-7cc3-9d01-6cda328f70c3/win7-warp-fix-20261009；最终汇总verification-results.json、binary-and-preservation.json、actual-probe-result.json均PASS。无需读前轮聊天即可按上述真实身份、raw与边界复核。
