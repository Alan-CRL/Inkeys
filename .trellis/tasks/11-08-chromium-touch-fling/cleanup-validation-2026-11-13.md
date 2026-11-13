# 白板拖动收尾验证与交接（2026-11-13）

## 用户裁决与完成范围

用户确认“至少现在在我的电脑上这个惯性是合适的”，要求保持当前手感，清理无用实现、完成静态可证性能优化后归档并提交。当前两项相连任务按该范围收尾；其它设备/系统/光学与背景性能没有新增实测，不扩大为全平台体验验收。

尺度适配留给**下一个独立 Trellis 任务**，本轮未创建或实施该任务。下一次规划入口：`scale-stage-plan.md`、`research/scale-stage-units.md`、`research/scale-stage-integration.md`；它们是历史待评审研究，涉及Pen行为等旧假设应按当前四类导航重审。未来输入后端/PTP以 `research/input-backend-future-plan-2026-11-13.md` 和用户路线图快照为准，全部仍延期，不解除 DM quarantine。

## 代码清理与行为保持

- 只改 `Draw3.CanvasNavigation.cpp/.cppm` 和 `InkeysHeadlessTests/draw3_canvas_pan_tests.cpp`，净删除547行（162新增/709删除）。
- 删除产品未使用的旧LSQ2、PhysicsBased/Legacy、贝塞尔、未接产品的history seed、专属参考测试及随之无消费的计时状态；独立demo源码不动。
- 保留当前可靠尾段拟合、30–70ms衰减、15DIP起步、多指基线、向量摩擦tau=.30/c=1000、12000模长上限、当前残余正投影续接与终点补足。脚本核对26个保留常量定义完全一致，12个可靠尾段方法正文未改。
- 保留Mouse/Touch/Pen/PenTail准入、开关、hover不制动、页面与模式停止语义；没有改Controller、设置、配置、国际化、RTS、DM、Freeze、shader或任何工程文件。

## 静态可证的性能优化

生产Controller已经按QPC/phase排好样本；此前Dynamics仍逐批分配vector、复制和stable_sort。现在使用相同比较器做线性有序性检查，有序span直接只读消费；仅乱序输入复制后稳定排序。非空有序批次省去临时分配/复制及重复排序，空批次保持无分配。同QPC/phase顺序不变，乱序副本生命周期覆盖消费。没有计时/功耗基准，不承诺具体帧耗时收益。

新增真实内核回归覆盖有序/乱序、同QPC重复Down/Move、多指拓扑、独立几何预期81DIP（9+24+24+24）、3000DIP/s释放及后续120帧一致；补产品controller小delta、逆时钟和取消断言。独立trellis-check全范围只读审查未发现确定缺陷。

## 构建与本机环境问题

复用 `InkeysRepo.sln Debug|ARM64`，vswhere动态定位原生ARM64 MSBuild；同一PowerShell调用规范Path/PATH、禁node reuse，`/m:1 /nr:false`，1800秒预算。

1. 受限runner首轮exit1/65.297秒，唯一有意义错误C1902（模块第1行PDB manager mismatch）。未改源码/调试信息/工具链；进程CIM诊断在受限环境也不可用。
2. 相同源码、工具链与配置在主机环境通过C++编译，随后默认Inkeys.exe输出链接LNK1104，exit1/13.297秒。输出目录临时写入检查成功；现有运行实例位于另一个工作树，未停止或操作。LNK1104具体原因未确定，不把它归为源码错误或断言运行实例就是原因。
3. 仅通过命令行指定 `OutDir=Build/ARM64/Debug/PanCleanupValidation/`，完整solution最终exit0/51.156秒，0 errors。没有修改.sln/.vcxproj/依赖/SDK/全局环境；三次构建源码SHA完全相同，构建期间源码未变。

最终产物为 `Build/ARM64/Debug/PanCleanupValidation/Inkeys.exe` 和同目录 `InkeysHeadlessTests.exe`。默认输出路径本轮未成功重新链接，实际验证必须使用上述新产物，不能误用旧Headless或声称默认输出已恢复。

## 自动回归

所有入口CREATE_NO_WINDOW；没有打开主程序/设置/浏览器/Computer Use。完整Headless预算1200秒，专项180秒。命令/耗时/输出SHA和产物SHA已写入本任务 `research/cleanup-validation-results.json`；原始日志在 `Build/Logs/pan-cleanup-2026-11-13/`。

七项验证全部通过（exit0），包括完整Headless。pan-feel前后输出SHA均为 `c3c3e6975bac0b8998c98a291a72f71e823bf708b8a1e01701238df01e67d9df`，整份708090字节输出完全一致，实际2240条scenario全部一致；原末行汇总为 `rows=2112`（附加场景未包含于该汇总），没有修改无关汇总计数。

| 验证 | exit | 秒 |
| --- | --- | --- |
| `headless-pan` | 0 | 1.406 |
| `control-fence` | 0 | 2.063 |
| `pan-feel` | 0 | 2.406 |
| `config-boundary` | 0 | 2.14 |
| `setting-general` | 0 | 35.734 |
| `diagnostic-output` | 0 | 1.813 |
| `headless-all` | 0 | 498.469 |

完整Headless末行 `PASS animation correctness`，无失败记录。主程序SHA256 `1ebeadc363f26fb1ecd23aaa62c92739316e88ecf44cf00c731f652fe0ef94d5`；Headless SHA256 `c0de110523506eb361604b18b7ccaf7091757d9f12dfb503d238203b40da3617`。全部质量门禁已完成；后续提交、归档和会话记录按本轮用户授权执行。

## 剩余边界

当前用户机器手感已获确认；真实跨设备/多系统、RTS批内硬件时间、DWM光学滞后、4K背景成本和多屏变化仍没有本轮新证据。触控板偏好控件保留而前端停用的提示问题已记录，未扩展本轮UI范围。以上后续事项不以“归档”冒称已实现或全部验证。

## 静态与文档门禁

独立全范围review、删除API引用闭环、26个保留常量定义、三源码SHA冻结、BOM/CRLF、git diff --check与任务context验证均通过。原有大spec超32768字节的context截断告警仍保留，未改全局阈值；当前收尾摘要先行注入并按需直接读正文。
