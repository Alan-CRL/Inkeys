# 单入口 Win7 诊断候选交付验证（2026-10-05）

用户请求同一 CMD 便于 VMware 硬件 Win7 与 WARP Win7 两台分别采集。继续原 Win7 主画布任务；这是定位候选，不声称已经修复；任务仍 in_progress，无 commit/push/archive。本轮实现报告、独立 check、plan 和 source hash 随任务保存。

## 最小变更

- 保留原13项及production_cases=4退出判定，追加五个独立sequence：原PS在Cursor-only后立即复验、完整源原样复编、完整Cursor type4入口常色、完整Cursor分支提前、最后原PS。新PS只在显式早退自检执行，未替换正式HLSL/8个CSO/301–304。
- 原样复编的ISGN/OSGN/SHEX/STAT与生产PS全同，仅RDEF不同；它是metadata/对象创建控制，不是指令修复。常色和分支提前保留完整其它功能源/17项资源及签名/插值，实际DXBC证据另存。
- 单CMD按一次pixel probe、同EXE普通ULW、同EXE完整ULW采集；一个时间根及三阶段子目录、完整双流、真实退出、identity、summary及两轮用户观察。普通轮显式清除继承full=1，末轮设1，finally恢复环境。probe失败仍继续两轮，缺输出/终态/阶段完成计数则incomplete；完成采集状态不代表视觉恢复。
- 局部精确INKEYS_DRAW3_WIN7_DIAGNOSTICS=1自动启用已有Draw3/Cursor控制台，配置不写入。Collect可选SkipProbe/CaptureRoot，旧默认/ProbeOnly保持；整体probe150秒、单query仍3秒，异常只处理自有子进程。已有Full-ULW只置空prcDirty，GPU dirty copy/DIB更新范围不扩大。

## 本机验证

| 验证 | 结果 |
| --- | --- |
| 原solution Debug ARM64，vswhere定位ARM64 MSBuild，PATH规范化，900秒上限 | exit0，40.03秒 |
| 同solution Release x64 | exit0，185.05秒 |
| 两架构18项pixel自检 | 全部逐项PASS；production4/0、diagnostic7/0、sequence5/0，removedReason=0 |
| Headless --no-window | ARM64 exit0/71.657秒；x64 exit0/14.335秒 |
| 新x64实际WinPS5.1 ProbeOnly（空格路径） | exit0/18项PASS；完整原始流与结束identity，无普通GUI/产品配置日志 |
| 实际WinPS5.1 mock + 单CMD | 11项collector/parser/direct cases、2项真实CMD和同host环境恢复PASS；含非零继续、大双流、缺输出、阶段异常、缺EXE |
| 独立FXC/代码审查 | FINAL PASS，旧三数组不变、三个新数组逐字节复编一致；报告research/check-one-click-20261005.md |
| 资源/资料身份 | 两EXE301–304逐字节匹配正式CSO；当前16项原capture/CSO/旧ZIP hashes保持 |
| 差异/格式/包 | git diff --check PASS；原BOM/CRLF保持；ZIP CRC和每文件逐字节PASS；仅1个CMD |

原有数值转换/对齐warning仍存在；本轮相关IdtMain C4244位于旧PPT逻辑而非新增诊断块，未无关清理。MSBuild使用调用级LinkIncremental=false沿用上一候选可用链，不改项目/SDK。其它并发构建结束后再运行，避免共享IntDir冲突。一次子代理PowerShell启动OutOfMemory在未执行源码前发生，轻量无profile runner恢复后验证完成，无代码绕过。

完整原始证据根：C:/Users/alan-/.codex/visualizations/2026/10/03/01a100c6-aa67-7cc3-9d01-6cda328f70c3/win7-one-click-20261005。verification-results.json、binary-and-preservation.json、actual-probe-result.json、collector-tests与gpu-fxc分别保存可追溯结果。本轮初始D:/Personal/Downloads/Win7-Diagnostics/20261004-113850-937已不存在，missing-before.json已记录，不能将其计入16项保留；之前研究hash/结论仍保存。

## 包身份及现场步骤

- 包：C:\Users\alan-\.codex\visualizations\2026\10\03\01a100c6-aa67-7cc3-9d01-6cda328f70c3\win7-one-click-20261005\Inkeys-Win7-OneClick-x64-Release.zip
- ZIP SHA256：6603AB072CDF57777E8B75699650DD17C4347DEF5A40C5E423117566CBF5AAAC
- EXE SHA256：1CB7D8887FD7AD85B923314566909198AC13EE1396B48FD0007E2791D0C8C0FA
- 源码baseline：8d15d05119b85777d8b0aabac4c99930cf438045 + manifest中记录的未提交Win7诊断delta，不能以HEAD代替候选完整身份。
- 在两台Win7分别解压，退出旧Inkeys，双击唯一Run-Win7-Diagnostics.cmd。先自检，再两轮新空白页：普通笔画5秒、等3秒，记录首笔出墨时机；然后移动橡皮、激光各3秒，正常退出并填写观察。回传每机整个Win7-Diagnostics时间目录，失败也保留全部。

Win7 PS2真实运行、两设备光学结果仍待用户；150秒实际超时未人为等待，PS2仅API/语法静态审查，不把WinPS5.1 mock当现场执行。未在本机启动正常产品GUI或Computer Use。下一次按原PS反向复验/完整变体判WARP执行差异，按正常/full光学与source/DIB/耗时判VMware刷新分界；不预设两台唯一共同根因。
