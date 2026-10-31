# 验证记录

日期：2026-10-19。用户已批准实现；全部运行验证均为无交互窗口入口，没有启动主窗口/设置窗口/浏览器。

## 最终结果
| 验证 | 结果 | 证据 |
| --- | --- | --- |
| 完整 InkeysRepo.sln Debug\|ARM64 | exit 0，83.75秒，0 errors / 127 warnings | .trellis/.runtime/ui3-taskbar-scale-build/build-final.log |
| 本次新产物 InkeysHeadlessTests.exe --no-window | exit 0，88.06秒，PASS animation correctness | research/headless-final.log |
| 本次新产物 Inkeys.exe --config-boundary-test | exit 0，2.11秒，ConfigBoundary PASS | research/config-boundary-final.log |
| Scripts/i18n.ps1 check | en-US / zh-TW 均 366/366，PASS | 最终脚本输出；sync已由设置实现阶段执行 |
| git diff --ignore-submodules=all --check | exit 0 | 最终工作树检查 |
| task.py validate | exit 0，implement/check各8个上下文条目 | 超长spec有截断提示，已按需补读相关章节 |

构建使用vswhere定位的ARM64原生MSBuild，并显式PreferredToolArchitecture=arm64、/nr:false、/m；同一PowerShell调用先Remove-Item Env:PATH、MSBUILDDISABLENODEREUSE=1。使用900秒监督，未修改solution/工程/SDK/依赖。最终127条warning为本轮重编译触发的现有代码警告（包括hashlib++类型转换、既有Setting浮点窄化等），逐行核对没有warning落在本次新增/修改源码行。

测试启动前校验新Headless EXE时间戳晚于最后测试源码修改；不能用修改前基线替代新增测试结果。

## 本次无窗口覆盖
- 旧Zoom不迁移、Zoom2与默认true开关、新旧并存、ReadMini/ReadAll/Reset/复制/冻结写盘/NoUpload/非法类型。
- 四边任务栏完整厚度、负坐标、多个DPI及用户倍率、无副屏任务栏按主屏DPI比例回退、无可信Shell回退、快照语义差异。
- 自动隐藏矩形移入上下/左右相邻屏时，明确owner/edge优先于矩形大部分面积。
- 四个PPT分页UI基准×全局倍率×局部倍率、共享白板保持独立、20屏幕DIP吸附不随UI倍率变化、runtime fit不污染保存倍率。
- StartupPreview低于0.5的任务栏基准不误套用户倍率下限，finite签名/Source可接受DPI相对倍率0.15。

## 构建及测试中修正的明确问题
- 首次正常沙箱外构建首错：Bar.Zoom调用PageControl内部RequestAll未导出。三处改为现有公开NotifyLayoutChanged，不扩大模块API。
- 首轮新测试仅两项失败：折算值1.2000000000000002与1.2精确比较。确认差2.22e-16后断言改abs误差<1e-9，产品公式未改。日志research/headless-first.log；最终全绿见上表。
- 静态审查先行修正了自动隐藏跨邻屏错误归属、Windows near/far宏命名冲突、finite测试使用非零fixture offset三个问题，保留产品原fixture安全约束。

## 本机 / Codex 环境诊断
- 修改前原有Headless用默认sandbox TEMP：exit 1，35项配置/位置原子写盘相关失败，日志research/baseline-headless.log。
- 单独Win32 MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH)复现：默认sandbox TEMP返回ERROR_ACCESS_DENIED(5)，仓库内.trellis/.runtime/ui3-taskbar-scale-test-temp成功。仅对测试/构建进程设置TEMP/TMP到仓库scratch，未改产品写盘器。
- 使用仓库TEMP复跑修改前基线：exit0，日志research/baseline-headless-workspace-temp.log。
- 沙箱内完整MSBuild首轮扫描后exit1但无源码诊断；显式原生编译器和仓库TEMP后仍不推进。只读进程检查显示每30秒反复创建工作节点，同一Display.cpp独立CL依赖扫描exit0；表明runner/节点通信环境异常。
- 核实PID/父PID/完整命令后只终止本次挂起构建树（未终止已有其他构建进程），在沙箱外运行同一完整solution命令后可正常编译并得到上述真实源码首错、修复后全绿。未修改项目配置绕过沙箱问题。

## 未执行项
- 按用户规则未运行GUI/Computer Use。真实Explorer自动隐藏动画、Explorer重启、第三方Shell、多显示器任务栏配置及视觉像素对齐仍需人工验收；无窗口纯计算测试不代表这些实机场景已验证。
- 未创建git commit，未push，任务保持in_progress供用户审阅/后续归档。

## 用户确认
2026-10-19 用户确认改动有效，并授权本任务本地提交及归档。后续结案提交保持既有签名设置，不push；上述“未提交/in_progress”为实施验证阶段状态。
