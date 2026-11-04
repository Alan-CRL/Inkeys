# 最终验证

本报告记录第一批产品修复的冻结Debug/Release矩阵。随后新增的独立SVG计时仅修改诊断接缝，使用另一个Debug二进制验证，结果见[SVG独立计量](svg-numeric-timing.md)；第一批EXE哈希和完整矩阵不冒充新增计量版本的重测结果。整体R4仍未完成，任务保持in_progress。

## 实现与证据边界

2026-10-28，当前bugfix/mode分支直接实现，未commit/push。最终Debug源码冻结在恢复requestedWidth增长扫描守卫之后。保留公共Button/BarUIRendering、单一WARP render owner、默认动画/字体/双光、所有完整成功提交/失败保留与workspace ACK/fence逻辑。

实际修复两处：共享Scheduler复用既有无自旋高分辨率/兼容waitable timer，失败沿原deadline回到sleep_until；主栏保留横向对称安全预约，但依据稳定宽度曲线极值，纵向改由实际可见/在途包络预约，避免隐藏面板无条件大位图。根布局移动提前全Observe；纯late容量/映射变化保持全窗绘制和提交且不清掉未观察稳定子项边界。

未改白板SVG、Freeze同步SetBounds/Hide、背景ACK/250ms协调轮询。当前fixture为真实Bar coordinator+公共renderer/PageControl+Scheduler，替换最终ULW sink且直接发布Committed workspace；没有真实Window/Freeze/Office/Draw3端到端ready延迟、DWM可见流畅度或fresh设备容量证据。SVG分段probe没有挂入该fixture，不能声称已排除SVG占比。

## 构建与环境

完整InkeysRepo.sln Debug|ARM64，vswhere定位ARM64原生MSBuild；同PowerShell invocation保存并规范化Path，MSBUILDDISABLENODEREUSE=1，/m:1 /nr:false，WaitForExit600000。最终widthguard-build-result.json可靠exit0。原项目/依赖/编译设置未修改。已有hashlib++三条C4267警告保持。

初次/m worker握手失败与沙箱C1902 PDB IPC问题见build-environment.md；后续所有对照均同正常用户环境。vcpkg pwsh回退信息没有误记为构建失败。

## 真实离屏性能矩阵

每版六向×100/150/200%×三轮×5cycle=270次转场，每组最少700ms且等真实Idle，保留全raw。计划30cycle版每版最低18m54，三候选最低56m42，按pilot收敛为5cycle/版约3m20；不是原计划30cycle覆盖。冷=各fixture首cycle，热=后4cycle；Initialize已Desktop预热，因此不冒充fresh capacity。

原始文件位于忽略的TestResults/ui3-workspace-transition/{baseline-user,timer-only,debug-final}/，全部sealed、零掉样；统计在原生fixture内验证有效barCommitStatus、callback/epoch、成功serial连续性和真实barCommitStamp。Idle、空Continue、phase边界不连成commit间隔。三个版本进程均exit0；完整成功提交/ULW source包含由生产fixture校验。稳定段增长新断言仅最终版本具备，不能将baseline的exit0冒充其资源门禁通过。

| Debug版本 | EXE SHA256 | 全54组预算通过 | 热54组预算通过 | 组batch P95中位ms | 组GetDC P95中位ms | 组真实commit间隔P95中位ms |
| --- | --- | --- | --- | --- | --- | --- |
| baseline | EFE813F09DBA19EFDA402F8F5C3BCF7D9EA357DD607D8F77FBB070A93077B7EA | 6 | 9 | 24.026 | 11.945 | 37.739 |
| timer only | C8390A3423E16404352D7D05A45CB5C6EFCAEA6AB10FF13BFF03FD167F29F82F | 8 | 16 | 22.241 | 10.381 | 23.763 |
| final | 4E9B53F6D7600ED45044B8A4C4A65EC523F010084C5263F5D870CE19841FCBAC | 20 | 22 | 18.432 | 7.783 | 20.451 |

预算为batch P95≤16.67ms且P99≤33.4ms，按每方向/倍率/轮分别判定；不能只挑最好一组。最终全部组batch P95范围10.751–37.621ms，commit间隔P95范围18.605–43.896ms。100/150/200%分别10/18、9/18、1/18组预算通过。相同Debug构建A/B，以上组分位中位的batch约降23.3%，GetDC约降34.8%，成功commit间隔约降45.8%。这不是所有帧的聚合分位，也不承诺全部预算达标。

快速前batch<15ms时，对下一16.666667ms deadline超时的P50/P95：baseline14.430/15.931ms，timer only0.299/0.692ms，final0.278/0.586ms。保留实际batch/producer等待，不用固定16ms轨迹替代此证据。实际target面积全倍率样本中位4,316,800→893,760像素（约79.3%降）；GetDC含D2D flush，不能将全部阶段耗时当纯拷贝。

确定性资源断言：同一稳定目标段首帧后零增长；仅每fixture第一D→P的pending placement允许一次真实二帧重定向。最终三倍率三轮均通过，未放宽断言。converged-final的150%290→292反例可靠exit1已保留；更早紧缩预测候选亦保留，未作为已接受收益。

## 像素与几何

最终final.exe --ui3-workspace-centering-test，INKEYS_UI3_TRANSITION_PIXELS=1，正常用户无窗口，可靠exit0。默认动画与双光开启。六case（左右、100/150%、负屏幕原点）、六向/快速反向、工具数6/18、注入present失败均由既有生产fixture断言通过。

324张BGRA中276张能与同次samples.csv成功present serial和ULW tuple逐一关联，另外48张Settle/FreezeHide debug捕获无成功row，明确排除。对照baseline-verified-geometry.csv与final-geometry.csv：目标位置/viewport几何及完整替换字段一致；source/target存储映射随容量改变，均满足source+window包含于target，0几何/包含失败。CSV比较没有hit列，不能扩称逐字段命中数据相等；命中行为由既有生产回归断言另行覆盖。276成功帧中231帧有像素差异，40,245/49,188,996像素（0.0818%），单帧最大0.2990%，RGB和alpha最大1个8bit量化单位。差异全部位于baseline3×3非恒定AA/光影渐变邻域，平坦区域0；source/target origin与存储尺寸改变，默认颜色轨迹/曲线/AA代码不变，结果支持有限浮点/8bit舍入解释。此结论不是bit-exact。逐帧索引/serial/source/target和差异比例保留final-pixel-comparison.json。

## 先前验证记录的限制

前一headless wrapper240s提前返回后，内部stdout已全PASS但ExitCode=null，不能作为OS exit0证据；最终已由下述direct完整重跑替代。

## 最终Debug生产CLI

同一final.exe、正常用户环境、hidden无交互窗口逐项执行，final-cli-results.json记录三项OS exit0：--state-mode-production-test、--bar-eraser-offscreen-test、--ui3-bottom-dock-transition-test。State覆盖workspace工具隔离/重复意图/快速切换/迟到回调/clear事务，bottom dock含固定drag/release、多workspace、150%、负屏幕原点及animations-off，summary failures=0。已知根移动与late资源整窗绘制的稳定对象隐藏边界回归在最终Headless套件中验证。

当前系统10.0.26300 ARMv8 Qualcomm，Balanced电源；未更改用户电源策略或固定后台负载。测试依次执行且性能运行不与编译/其它测试并行，仍属交互开发机的顺序对照，不能将所有长尾都归因为某一阶段或承诺实机显示帧率。

## 隔离Release构建

最终完整InkeysRepo.sln Release|ARM64，命令行显式/p:EnableUi3PresentationHarness=true，/p:OutDir=<repo>/TestResults/release-harness/，其余仍/m:1 /nr:false与同invocation PATH规范化。真实exit0；首次Harness+LTCG约7m50，Generating code期间无输出未被误判卡死。未改项目默认发布开关、/GL、优化或依赖。Release构建的两份tracked FXC .cso副产物先以原路径备份TestResults/ui3-workspace-transition/fxc-build-artifacts，再由Python subprocess读取git show HEAD:精确路径的原始bytes恢复，hash和exact=true记录restoration.json；无PowerShell文本管道、无bulk restore，恢复后git diff仅8个预期tracked文件。

## Release当前绝对性能与剩余限制

TestResults/release-harness/Inkeys.exe SHA256=C0BAD63F710910B813595D09526AD6B7EC103EE93ED14F94FA439AF9937413BE；release-full-result.json可靠OS exit0。与Debug相同六向×三倍率×三轮×5cycle，270次转场、6,951次真实成功主栏提交；9份raw全部sealed、零掉样、最终资源/事务断言失败0。每fixture首D→P各一次pending placement增长，9次合计，其余稳定段0。Debug-final同口径6,828成功提交、9次允许增长；baseline/timer分别4,630/6,645提交、23/26次实际增长。旧96组fixed-time CSV无组内resize仍保留为反证，其fixture条件和是否首次跨新峰值不同；本次大多数方向零resize依然掉帧，所以容量不是唯一根因。

| 最终Release当前绝对预算 | 数值 |
| --- | --- |
| 全部/热态预算通过组 | 22/54、24/54 |
| 100/150/200%全部预算通过组 | 14/18、6/18、2/18 |
| 组batch P95中位/范围 | 17.215ms；11.672–23.671ms |
| 组batch P99范围 | 13.607–35.946ms |
| 组真实成功commit间隔P95中位/范围 | 21.086ms；19.557–24.717ms |
| 组真实成功commit间隔P99范围 | 20.739–30.098ms |
| 组compute/draw/GetDC P95中位 | 1.356/1.655/11.299ms |
| 快速前batch的deadline overshoot P50/P95 | 0.204/0.614ms |

这是最终Release绝对数据，没有同Release旧baseline，因此不用于计算跨配置收益。剩余本场景最大可见阶段是WARP flush/GetDC（该阶段含提交前刷新）；单独SVG占比没有测出，无法进一步细分所有Draw/GetDC来源。本次主要瓶颈已改善且确定性容量行为修复，但热态预算尚未全面通过，不能宣布全机型/三倍率稳定60FPS或把显式16.67/33.4门槛改小。Debug组batch P99范围12.530–60.551ms、真实commit间隔P99范围19.376–65.765ms，均完整保留长尾。

## 最终Headless与静态收尾

完整Debug InkeysHeadlessTests.exe --no-window在正常用户环境以PowerShell同步直接调用（非Start-Process短wrapper），完成后立即保存$LASTEXITCODE。headless-direct-result.json可靠OS exit0，SHA256=2E5583F06FE4199D5E2924CD3558C81144E126027556ECF4CDAB7E7DE9825CD7，19:41:15–19:47:02，约5m47，末尾PASS animation correctness。包括新增非法/正常真实timer接缝、纯容量full replacement与root提前全Observe后稳定子项隐藏边界测试，以及既有动画范围/脏区/成功失败事务回归。期间未并行运行性能矩阵或构建，没有因无日志或超过4分钟而kill/restart。

静态git diff --ignore-submodules --check exit0；6份改动代码源均CRLF，既有BOM保持，final-source-manifest.json保存hash/编码。tracked diff仅两份规范和6份代码源，无项目/依赖/shader修改、commit/push。Headless既有测试硬编码产生旧任务路径10张PNG，主会话在最终测试后负责仅搬移已确认生成文件并移除空目录；未改无关测试源。

本冻结批次编译、正确性和确定性资源门禁均通过；R4热态性能预算仍未全部达标，主会话继续只读研究剩余GetDC/flush和SVG计量接缝，不将本批handoff等同原PRD所有验收条件完成。

## 后续深采样的环境限制

独立SVG计量后，主会话又探测了现成WPR CPU栈采样。正常用户执行环境在开始时即被Windows profiling policy拒绝（0xc5585011），未启动额外测试、未获得ETL；结束确认没有录制会话。没有改系统策略或权限，详见[CPU采样探测](etw-cpu-probe.md)。此环境限制只影响后续GetDC内部归因，不推翻既有构建/离屏结果，也不能充作产品已修复的证明。
