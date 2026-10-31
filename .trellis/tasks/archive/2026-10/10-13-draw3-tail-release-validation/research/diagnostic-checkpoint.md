# 正常颜色抬笔长斜线调查交付检查点

2026-10-13。用户确认截图长斜线在抬笔时突然出现，没有鼠标绘制；诊断版准备后选择稍后测试。真实异常根因仍未知，阶段四人工验收失败，保持进行中。阶段三提交重试成功：71858b0b（SSH签名，无push）。本轮阶段四修改尚未提交。

## 交付

- 正常颜色Debug程序：`Build/modeler-baseline/stage3-main-native/ARM64-Debug/Inkeys/bin/Inkeys.exe`。
- SHA256：`494f645b0289a89f41607ab8b7cffe4b3a41ef204913d1226b04d538dabd2221`。
- 手动启动脚本：`Build/modeler-baseline/Start-Draw3TailTrace.ps1`，已有实例时要求先退出，不自动结束用户程序。只在该次子进程启用`INKEYS_DRAW3_TAIL_TRACE=1`，原进程环境在finally恢复，不写持久环境设置。
- 日志：TEMP下`Inkeys-Draw3-tail-<pid>.log`，旧PID文件存在时采用`-<16hex文件时间>-<0..7>`后缀。脚本在用户关闭程序后显示新日志路径。代理未执行该GUI启动脚本。

## 验证结果

- 最终完整`InkeysRepo.sln Debug|ARM64`：动态原生ARM64 MSBuild，v143等已有配置保持；同PowerShell invocation规范PATH，timeout3600秒，退出0。
- 默认/显式开启两轮`--draw3-pen-taper-test`均334115项、0失败，确定性几何摘要一致。默认没有新增TEMP轨迹；on日志64个完整记录、1,845,792字节，各点数/字节硬限和end检查通过。对应证据：`Build/modeler-baseline/phase4-evidence/phantom-trace/verification.json`。
- 最终`--draw3-control-fence-test`：Python subprocess实际退出0；覆盖真实coordinator中Down+两次未消费Move+Up的合并计数、重复Up不重计、接续初始/当前身份、清除旧终态和Reset。旧PowerShell直接启动GUI subsystem EXE的空LASTEXITCODE不作为通过依据。
- 模块solution Debug ARM64：退出0；自适应9245项、0失败；原模型128412项、0失败，报告SHA保持`0119fd2a0edfdefa055ac3b5e1fde5aeadb4c693bd67f7157b55c09dae02b646`；完成态历史/composition/文件精确往返、等待/接续集成均0失败。
- UInk文件测试初次因隔离输出缺既有fixture、证据父目录失败；复制fixture到预期相对位置并创建`TestResults/release-hardening`后通过。没有为验证环境修改产品代码。
- 本阶段此前`InkeysHeadlessTests.exe --no-window`退出0，81.438秒，Headless源码/MTd未改；最终诊断源码修改未涉及该测试目标。
- 最终EXE仅作为数据加载核验shader资源301/302/303/304，均非空DXBC；无GUI执行。diff检查和五个变更源码BOM/CRLF核对通过。

## 当前边界与下一步

颜色拆段的2142次索引等价推演没有新增CPU边；普通WM_MOUSE仅光标。仍有异常Up、消费合并、完成态重构或GPU的候选解释，现有主日志不能区分。model/completed/Stored的512点裁剪及完整几何摘要不是整笔全点序列，必要时须针对异常区间追加有限证据。

用户复现后先读该次真正人工日志，定位长线第一次出现的阶段，建立对应失败回归再修复；最终Release/跨架构及实体复验随后继续。未运行GUI、真实GPU像素、D3D Debug Layer或Win7，不得宣称通过。创建冲突后缀分支有静态检查，当前CLI新PID均走主名称，没有实际碰撞实验。

磁盘清理已删除57项旧生成obj/调试缓存，约2020.6MiB；保留程序和报告，六个旧对照EXE另有hash清单。计划及证据在`Build/modeler-baseline/phase4-evidence/cleanup-plan.json`和`baseline-programs.json`，没有清理第三方依赖或用户文件。四个未跟踪cso仍属于生成物，排除提交。
