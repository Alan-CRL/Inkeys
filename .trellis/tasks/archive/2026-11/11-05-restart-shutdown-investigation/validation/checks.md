# 验证与边界

- 原生ARM64 MSBuild动态发现，完整InkeysRepo.sln Debug|ARM64 exit0；同一PowerShell先规范化PATH并禁用node reuse，未改项目/工具链配置。
- 重启版本：ordinary-restart-only的5项、single-instance-tests（含白板管道）、instance-lease-only、完整shutdown-supervisor-tests的32项、General无窗口专项均通过。
- Settings最终版本：完整solution exit0，22.108秒；setting-business-cancellation-test exit0，630ms，old-order-recreated/hidden-drained/active-callback-drained均为1；font-lifecycle exit0，913ms。
- 完整InkeysHeadlessTests.exe --no-window，同一PID55380最终exit0，463.988秒。初始5分钟监视到期后继续等待原进程，没有重跑；普通Get-Process观察对象未保留句柄时ExitCode为null，最终采用显式保留原生句柄的观察记录确认0。
- 采集脚本自检：首轮exit7且阶段不完整，随后第二轮exit0/normal；每路输出超过256KB，恰好两轮、没有第三轮；覆盖PID/命名空间、QPC乱序/缺段/无效、managed final跨Capture及旧rebind排除。
- 进程范围14例覆盖本根、精确installer、大小写/点段/尾分隔符、盘根、外部worktree/相似前缀/嵌套目录、空/相对/盘符相对/当前盘根路径；自检和PrepareOnly通过。
- 用户实测重启成功；修复后26轮关闭结果见retest26-summary.json，全部exit0。不能将此扩展成所有退出长尾或所有平台均通过。
- BOM/CRLF与git diff --check通过。原始18轮及6+20轮、Build/Tests验证记录均保留在忽略目录，不提交大型产物。
- 未运行真实Win7、跨架构更新、UAC/UIAccess全链专项；PPT/末尾收尾长尾未修复。

另一次扩展诊断 `--shutdown-supervisor-tests --failed-cleanup-real-only C10-ppt-release` 退出65：producer关闭及fresh committed reader通过，但foreign-session reader负例退出90。该失败没有归因为本次设置竞态、未修；原记录在 `Build/Tests/shutdown-investigation-20261105/probe-3-C10-ppt-release.stdout.txt`，不能把本任务已通过项目表述成所有额外故障夹具全绿。
