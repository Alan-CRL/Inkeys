# 当前手感修复验证记录

## 构建尝试1：执行环境未进入编译

- 2026-11-08，动态vswhere定位原生ARM64 MSBuild 18.11.0.47511；`-version`成功。
- 同次PowerShell删除Env:PATH、MSBUILDDISABLENODEREUSE=1；InkeysRepo.sln Debug|ARM64 /m:2 /nr:false；Python subprocess总超时1800秒。
- 工具session26892，300.9秒自然退出，exit=1。日志`Build/Logs/canvas-pan-repair-build.log`仅空行，没有源码错误。
- 沙箱外只读进程检查：root80232→每约30秒创建一个nodemode:1/nodeReuse:false的MSBuild子节点，均低CPU，无cl/link启动。未执行到C++编译，判为执行环境/节点启动问题，不能判代码通过或失败。
- 试图停止时root已自然退出；仅清理与该root匹配的残留子节点，不触碰其他进程。后续改用正常执行环境单节点构建，不修改源码/工程/依赖以绕过环境。

## 静态验证（构建前）

- i18n官方sync/check：20语言通过（实现代理与check代理）。
- git diff --check：通过；保留原文件BOM/EOL，未改GUI运行行为以便测试。
- 交叉审查确认并修订：overflow后新epoch Down准入、Stop前尚未登记Down锁批次、同批Touch/Mouse切换残余、反向拟合冲突/坏Up闭合、限位后残余轴同步。
- 后续真实构建/测试结果待填；真机手感未自动验收。

## 构建尝试2/3与首轮生产测试

- 正常执行环境 /m:1：101.1秒exit1，主程序源码编译完成；首个错误为link.exe 14.44.35229的C0000005 / LNK1000 Internal error during IncrBuildImage，属于增量链接器内部故障。
- 仅移除已核实位于Build/ARM64/Debug的生成文件Inkeys.ilk；未修改工程/工具链。相同solution重试24.8秒：主程序成功链接，Headless首个源码错误为新增测试ClearAxis漏booster参数(C2660)，另新增count遮蔽(C4456)。已由内核代理修复，仅测试文件。
- 首轮实际生产probe（主程序已新编译，均无GPU/HWND）：control-fence exit2，两处旧fixture只有Up改变位置而没有真实Move；已补92/96/100ms Move并将delta>=12收紧到12±0.001，不放宽其他断言。
- pan-feel exit12/rows2112，所有失败集中240fps/subframe的12000边界与连续加速。已定位10MHz QPC取整令合法12000变成12000.768049，时间异常检测误清；需修可计算量化误差界并重跑，禁止放宽0.85–1.15体验断言。
- diagnostic-output exit0，1.92秒，真实生产queue/pipe/window=0验证PASS；本轮之后未改诊断生产实现，无需无故重复同一测试。
- 原始日志保留：Build/Logs/canvas-pan-repair-build-native.log、build-relink.log、control-fence.log、pan-feel.log、diagnostic-output.log（均使用canvas-pan-repair前缀）。后续验证另存文件，不覆盖失败证据。

## 最终验证结果（2026-11-08）

| 项目 | 退出码 | 结果/耗时 |
| --- | ---: | --- |
| InkeysRepo.sln Debug ARM64 第4轮 | 0 | 26.0s，主程序+PptCOM+Headless完成 |
| Headless --no-window --canvas-pan-only | 0 | 1.14s；新产品回归与Chromium参考/背景/Freeze通过 |
| 完整Headless --no-window | 0 | 282.03s；PASS animation correctness |
| --draw3-control-fence-test 第2轮 | 0 | 0.94s；真实输入/视口/页面/停止保护通过 |
| --draw3-canvas-pan-feel-test 第2轮 | 0 | 2.39s；PASS failures=0 rows=2112 no_gpu=1 no_hwnd=1 |
| --draw3-diagnostic-output-test | 0 | 1.92s；真实sink、gate、队列/pipe与LastError通过 |
| 最终solution构建（第5轮） | 0 | 23.5s；仅收紧DIP probe测试oracle后重建 |
| 最终control-fence（第3轮） | 0 | 1.47s；当前摩擦精确距离±.5px断言通过 |

第5轮没有生产逻辑更改，仅Controller测试三行；Headless EXE的SHA256与通过完整回归时完全相同，不重复282秒长测试。平移2112行矩阵与诊断生产代码也未再改变。新增960Hz/10MHz/四比例恒速边界回归已经随第4轮Headless通过；12项高速失败消失，没有放宽体验容差。

最后审查进一步将DIP probe旧v²上界改成(.30*v−90*log1p(v/300)−initialOffset)/.75，并从单边上界收紧到双边绝对误差≤.5px，消除虚假通过。

原有第三方转换/对齐、其它绘制测试nodiscard等警告未顺手修改。新增测试漏参和局部遮蔽已修。PATH规范化令vcpkg先找pwsh失败；仓库vcpkg.targets明确按绝对路径回退系统PowerShell，未出现其applocal失败警告，EXE三个实际入口可运行；未改vcpkg、工程、SDK或全局环境。

## 范围与文件验证

- 37个源码/测试/生成文本逐个对比HEAD的BOM，并严格解码UTF8、检查CRLF，无整文件换行转换。
- git diff --check通过；无.sln/.vcxproj/.props/.targets修改。新设置20语言及生成keys/snapshot由官方脚本sync/check通过。
- 新小规范native-desktop/canvas-pan.md为当前产品合同；原任务保留阶段1历史并明确当前implementation-scope优先。
- 本轮未启动产品GUI/设置窗口/浏览器，未使用Computer Use，未commit/push。
- 真机鼠标/Touch主观手感、RTS批内硬件时间与屏幕物理尺度仍未验收；设备尺度/profile明确延后。任务保留in_progress，等待用户实测及后续范围，不归档整个任务。

## 交付与实测日志入口

产物：Build/ARM64/Debug/Inkeys.exe。
设置→软件调试→控制台输出→“白板平移与惯性诊断”，默认关闭，开启并重启后生效。日志前缀[CanvasPan]，包含producer批数/减少点数、sourceKey/phase、sample/frame/consume时间、历史溢出/争用、原始/选定/boost速度、尾段上限、拒绝/停止原因；[Draw3DiagnosticOutput]报告输出队列丢弃。诊断日志不自动承诺全部保留，也不伪装硬件时间。

后续用户实测优先：鼠标/Touch各做同向连续甩、短反向后立即松开、减速后松开、惯性中静止按住、笔靠近停止；异常时保留从Down前至停止后的完整控制台片段并说明输入类型。自动通过不能替代这些观感确认。

## 用户确认

2026-11-08：用户明确“确认当前的没问题”，授权本轮commit但保持任务进行中。此反馈不扩大为跨设备全部验收。后续交接见handoff.md。
