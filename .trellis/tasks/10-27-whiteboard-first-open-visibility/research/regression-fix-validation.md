# 本轮白板回归修复与验证

## 原因与实施范围

回归引入点为 77f76038（2026-10-24）。20261021a/该提交父版本没有这条完整组排序准入和通用恢复放大链。历史证据见 history-native-order.md、history-window-recovery.md；此前授权现场及资料解释修正见 authorized-gui-evidence-20261027.md。

1. 模式进入新增完整组置前成功硬门；后台隐藏 Middle 在 Bar 之上时，模式返回失败，首帧与背景尚未开始，StateMonitoring同epoch重做面板清理。现在白板保留Bar位置，仅相对下调PPT，固定代际、每次写后读回，保留真实owner/band/内部序检查。Desktop/PPT正常刷新保留。
2. 纯Refresh/Promote/Topmost/Fullscreen命令被snapshot.canvas=group扩大成画布事务；层级失败恢复反复FRAMECHANGED、CanvasPolicy、Hide/Show。现在快照及Restore早分流，只有政策/owner/band/原PPT兄弟序恢复，不写画布、几何、显隐和非band样式。真正模式/Canvas事务保留原完整恢复。
3. 同进入epoch准备UI一次，实际失败返回时间起250ms退避；新epoch与取消不受旧期限影响。生产CLI直接测同helper。

最终复核还收窄了band继承：只有固定旧身份root使用显式TOPMOST/NOTOPMOST，非root沿它继承；保留有限root校正，不使用可能已经替换的service新root。删除了无意义旁置bool断言，新增Freeze/Drawpad父代际退休的真实helper回归。

## 构建与当前自动验证

- 最终 InkeysRepo.sln Debug|ARM64 完整solution build退出0，耗时10.01秒（增量）；原生ARM64 MSBuild动态从VS定位。首轮也退出0，但最终测试/原生参数有调整，所以以重建结果为准。
- 同一PowerShell内规范Path/PATH并禁node reuse，20分钟预算；没有升级/更换工具链或改工程。
- 最终构建仅既有 Draw3.DiagnosticOutput.h C4702 warning；本轮测试局部变量阴影已修复。
- Inkeys.exe --state-mode-production-test：退出0，0.60秒；workspace/late callback/工具/快捷键及新进入重试helper均无失败。
- InkeysHeadlessTests.exe --no-window：退出0，302.96秒，最终 PASS animation correctness；WindowCommandCompletion 与 CanvasPolicy 均 failures=0。
- 最终构建前后12个已变更文本源/spec哈希一致，git diff --check通过；IdtState保持UTF-8 BOM，其余UTF-8无BOM，全CRLF。
- 全9份CSO内容及时间戳与构建前一致；用户原两份改动未覆盖。测试wrapper已恢复10份旧任务PNG的原内容和时间戳，不修改其他任务资料。

日志：Build/WhiteboardRegressionFixValidation/solution-build.log、*.result.json、state-mode.*、headless.*。这些路径均为仓库ignored验证产物。

## 独立复审

最终四个本轮代码文件已经 trellis-check 只读复审，未发现本轮范围内阻断的P1/P2；见 independent-regression-review.md。构建与测试完成后再次核对12份已变更文本哈希完全一致，9份CSO及10份PNG内容与时间戳一致。

## 验证边界

用户撤销后续GUI授权以后，本轮没有启动/操作GUI、窗口采样、读取运行时PDB或启动真实HWND测试。--no-window包含生产排序/身份/失败读回helper测试（24排列×保序与四目标共120组），以及Setting/Shell/Canvas等既有无窗口/离屏套件。

helper故障注入不等于完整私有Service dispatch、RetryCommandRecovery/Retired路径的动态执行。四类命令映射、提前Restore分流、原生参数、完整模式事务保持及安全覆盖由源码独立复审确认。不能把离屏通过写成真实Win7/Win11/其他系统实机或修复后桌面验收通过。最早原生排序调用逐次BOOL仍未捕获，但实际内部违约、前台恢复对照与确定新增控制流足以支撑本轮窄修复；未扩大到背景token/API/其他生命周期重构。

2026-10-27 用户人工确认首开不可见和后台闪烁两项修复，并授权提交本轮改动。代理未新增GUI测试；用户新反馈的退出白板异常将在该修复提交后另行调查，不纳入此次已验证修复结论。不push。

2026-10-27 提交前最终全范围复审：已覆盖12份变更文本（含此前同任务Settings/Shell修复与规范），未发现新增阻断；12份源码/spec哈希仍与最终构建一致。
