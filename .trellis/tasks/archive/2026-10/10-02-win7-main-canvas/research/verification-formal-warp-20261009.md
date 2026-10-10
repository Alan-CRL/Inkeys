# Win7 WARP 正式兼容修复验证（2026-10-09）

## Outcome and scope
新正式候选已完成默认选择实现及本机质量门。正常实际OS6.1+WARP自动使用真实Win7已验证的完整2171 shader；其它普通路径保留原资源，明确诊断0/1保留原版对照。输入、FLIP_SEQUENTIAL、ULW dirty、所有shader数学及正式四资源保持。用户授权修复已证实部分；本轮没有commit/push，没有关闭原任务。

## Actual Win7 evidence
两份新资料232746（用户确认Aero开启）/233615（关闭）使用相同旧EXE23D6...0C547、原版F55D...1A69F8与完整候选2171...55384。两台正常轮均Hardware失败后WARP FL11_0；独立Hardware请求FL11_0的0x887A0004为unavailable，不能当Hardware验收。

原版50份ULW源均透明，候选116份中112份源和DIB有颜色、4份仅初始透明清屏；166次ULW全部成功、fullOverride0。候选每台18项独立正确性全PASS，12有效原版reference逐像素delta0，6原版cursorreference不可用。正常笔迹/橡皮/激光、穿透/定格通过人工观察。边界详见capture-232746-233615-analysis-20261008.md和独立check-aero-guarded-20261008.md。

## Local build and tests
环境Windows11 ARM64；未启动真实产品GUI或ComputerUse。完整InkeysRepo.sln，ARM64原生MSBuild，由vswhere定位；同PS规范Path/PATH并禁node reuse，900s超时、独立OutDir。Debug|ARM64 exit0 27.63s；Release|x64 exit0 276.4s。

首次ARM64沙箱内构建C1902/exit1，91.54s，保存first-C1902-build.stdout/stderr/result；相同源码/工具链/命令在require_escalated环境重试exit0，没有改源码/SDK/编译配置绕过。既有DrawingController的C4834、IdtMain及Setting/hashlib类型转换、HeadlessLNK4075等warning保留；不进行无关warning清理。vcpkg pwsh缺少PATH提示沿已有PowerShell回退，构建最终exit0。

两架构WARP/Hardware各105唯一GPU终态PASS，共420；每次27共享实际selector策略PASS、候选18有效reference/maxchannel_delta0。两架构Headless --no-window均exit0。父级diag1+guard1的实际WindowsPowerShell5.1 ProbeOnly也成功，实际renderer仍明确original/F55D基线，再独立测候选；无正常主窗口、没有产品配置写入。collector47mock+3不存在环境恢复共50PASS，0/0/1、failurecontinue、independentHardware、strictschema和owned-child截止合同通过；本机PS5.1不等同新包实际Win7PS2验收。

## Preservation and identity
独立核旧数组、正式四EXE SHADER301–304与源CSO逐byte相同；精确完整候选28980bytes/SHA2171DDA6E709A3F4CD444E6281AB64635C3F969CD71722662B04DA0DD8055384存在两个新EXE。87个不可变文件保持，其中42份用户新原始资料、旧包/报告与shader。8个其它任务workspace文件哈希保持；源BOM/CRLF、最小增量、context规范已检查。

HEAD为e2c212d4d9cd68367c08363c450d392e9d63bcef，带既有未提交workspace及本次增量；manifest记录涉及来源hash，不冒称commit态。
包：C:\Users\alan-\.codex\visualizations\2026\10\03\01a100c6-aa67-7cc3-9d01-6cda328f70c3\win7-warp-fix-20261009\Inkeys-Win7-WARP-Fix-x64-Release.zip
ZIP SHA256：F28F981F9B66007336B58A387067C01745CB4BE82926EE24CD3964CE94F84559
EXE SHA256：2CDE02EDEE54AECDC08825C8557D792E956CFC0DEEA646ECF29974E2EF6108D8
ZIP bytes：29314559，唯一CMD、9文件、CRC与逐文件字节校验PASS。

## Remaining acceptance
已有实际Win7验收针对旧EXE诊断Part3同一完整字节；新正式EXE自动选择还未上Win7，下一次仅先双击Inkeys.exe确认正常默认恢复。可选唯一CMD用于Hardware升级后的双后端和原版/候选对照，Part2仍旧PS是故障对照。

一次荧光笔定格加深、早前Hardware源有色但desktop不刷新、Win7Hardware真实FL11_0及PPT有限回归保留未完成。没有证据唯一指定旧WARP某个opcode/JIT错误；已验证最小控制结构兼容修复。原任务继续in_progress。

独立包装复核：aero_shader_check只读确认9唯一条目/1CMD/CRC、全部member/8manifest文件字节SHA、26来源SHA与两个冻结报告均一致；结果另存independent-package-review.json，未回写冻结quality。
