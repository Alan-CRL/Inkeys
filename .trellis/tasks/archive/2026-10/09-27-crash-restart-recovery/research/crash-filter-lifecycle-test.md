# F-016 无窗口过滤器安装/卸载回归设计

`CrashHandler::Shutdown` 的 H0 缺陷是用 `PreviousFilter != nullptr` 代替“本过滤器已安装”；前一个过滤器为空时正常退出后仍留崩溃处理器。构建通过不能证明此时序。

在 `wWinMain` 配置、互斥体和窗口初始化之前设显式 `--crash-filter-lifecycle-test` CLI，只在独立测试进程中调用真实 `CrashHandler::Initialize/Shutdown`。先把系统 UEF 设为 null，安装两次确认幂等，临时读回已安装指针再恢复它，Shutdown 两次后读回并恢复进程原值；随后以本地哨兵过滤器重复一次，确认非 null 前处理器也被正确恢复。全程不制造异常、不写 dump/配置、不启动新进程或 GUI；退出码和 stderr 说明结果。

这只验证过滤器注册生命周期，不验证未处理异常实际回调、自动拉起、单实例交接、新实例 ready、退出期其他线程崩溃或 UInk 恢复。真实 UEF/restart 仍按仓库 AGENTS 的 GUI 授权门禁列人工测试，不用本测试冒充完整链路。
