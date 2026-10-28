# 剩余GetDC归因的CPU栈采样探测

2026-10-28，SVG独立计量交接后，主会话尝试使用现成本机WPR/WPAExporter做一次有界离屏CPU栈采样，以区分GetDC内的WARP执行与等待。本次未改源码、系统策略、权限、注册表或电源设置。

## 工具与命令

- 本机wpr.exe 10.0.26100、Windows Performance Toolkit的xperf.exe/WPAExporter.exe已安装。没有安装或升级工具。
- 开始前wpr -status明确未录制；使用独立实例InkeysUi3Transition20261028，命令为wpr -start CPU -instancename InkeysUi3Transition20261028（CPU.Verbose.Memory有界内存模式）。若成功才运行一次90s上限的svg-pilot，并在finally中仅停止该实例写本任务ETL，不影响其他会话。
- 已按[Microsoft WPR命令文档](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/wpr-command-line-options)确认CPU profile、memory模式和独立instancename语法；没有改profile interval或启用boot trace。
- WPAExporter -help最初在沙箱因注册表读权限失败；正常用户环境同命令exit0，可显示CLI帮助。该差异是沙箱边界，不是项目代码问题。

## 结果与限制

正常用户环境的WPR CPU启动即失败，可靠OS退出码-984068079（0xc5585011）：Failed to enable the policy to profile system performance。未进入测试进程分支，未启动GUI、未录制CPU轨迹或新增转场结果。原始start/status/result在忽略目录TestResults/ui3-workspace-transition/etw-cpu-probe/。

结束后再次wpr -status -instancename InkeysUi3Transition20261028返回exit0、WPR is not recording，未遗留录制会话。本次没有提升Windows管理员token、修改系统profiling权限或以其它接口绕过策略。

这只说明当前执行token不能取得系统CPU采样；不能由此认定WARP、驱动或Windows是产品卡顿根因。剩余GetDC内部成本仍未拆开；需要可使用CPU profiling的执行环境/轨迹，或先设计更细且不移动同步边界的诊断实验。当前没有已证明足以关闭R4差距的安全小修，不增加猜测优化，保留现有改善与R4未完成事实。
