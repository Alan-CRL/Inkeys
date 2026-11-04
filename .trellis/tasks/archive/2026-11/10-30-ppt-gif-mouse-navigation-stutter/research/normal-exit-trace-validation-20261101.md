# 正常退出阶段定位版准备验证（2026-11-01）

## 本轮事实与优先级

用户从Inkeys菜单/设置正常退出，采样130949-399-0a27afde却返回0xE1430016（-515702762），退出验收失败。ROT Deferred确实触发且后续Observe与forced次序正确；本轮最大请求926.754ms，但ALL中位163.567ms对旧98.758ms、Previous中位226.654ms对168.752ms，没有受控重复证据支持稳定改善。性能记录见rot-yield-manual-results-20261101.md，退出研究见normal-exit-timeout-20261101.md。

此版只补退出阶段观察，不改15秒监督、线程/清理顺序或COM释放。E1430016不证明helper创建失败或COM泄漏；最后managed Perf约13:11:43.955而不是一直到强退，具体卡点仍未知。

## 实现与独立审查

- native独立[ShutdownTrace]固定数字协议，跨TU共享Recorder、独立前缀不与PptPerf编号/TLS混用。原SetOffSignal、main各join/组件/COM阶段、PPTLinkage及服务/reset边界仅包原调用。
- Arm前只读预热constinit指针捕获POD；未预热无冷静态初始化，Arm/GetLastError紧邻，Failed原noreturn守护先于任何emit。正常startup早期模式返回后且线程前预热，不输出/QPC。Arm若永不返回无持久化前段记录的限制保留。
- LastError仅分别保护诊断动作当刻，不跨整个业务操作恢复旧值；默认off无QPC/格式化，sink失败不抛。不依赖IDTLogger、不创建可见控制台或关闭借用stderr句柄。
- managed复用PptPerfTrace，final/rebind、邮箱stop、事件deactivate/disconnect/retire、window→presentation→application逐项SafeRelease和state reset；actualdynamic/取得/借用/转移/finally、IErrorInfo、CP失败lease均保持，没有GC/FinalRelease或额外COM读取。
- 独立trellis-check最终未发现代码缺陷，并确认原退出顺序/条件/等待次数/截止策略保持。已将协议、安全和未知限制同步errors-logging-and-resources.md。

## 验证结果

- managed ARM64MSBuild Release|AnyCPU及全部coretests exit0，最终stderr空。新6组启停操作/释放/state等价、CP成功/重试/保留lease、真实servicefinally、throwingwriter/IErrorInfo和配对通过。初轮旧Observe断言把所有新cleanup误认为Observe子项，修正为仅断言observe-/read-家族后通过；不是生产逻辑错误。日志Build/Validation/managed-shutdown-trace-20261101/managed-*-verified.*。
- 完整InkeysRepo.sln Debug|ARM64原配置，ARM64host、sameinvocation PATH规范化/禁node复用、900秒预算、PDB允许环境；最终exit0，23.43秒，0errors/7既有warning（hashlib类型转换及未改动布局float→int）。初轮新header不限定Format触发ADL C2668源码错误，单行显式限定Shutdown::ProgressTrace::Format后重跑通过，不修改工具链/项目选项。
- InkeysHeadlessTests.exe --ppt-session-only exit0，约2秒，无窗口。测试cold初始化/未预热/disabled、buffer/nested/异常、业务A→B错误码保留、throwingsink与invalidstderr；fakeRecorder事件仅内存，不污染生产trace。
- TLB字节仍C652B5DD0603576033B2E0D15869BE103F23FFDCAA2A1984E53F3B6A14DBA606；最终内嵌PptCOM SHA1061DDF51BD7CF646030AC74830DCD67CE74508448D879D70BBEE3EAA4D850E3，与交接DLL相同。EXE确认含ShutdownTrace标记。
- PrepareOnly通过，main.json前后SHA一致，未启动GUI/Office。元数据ppt-navigation-20261101-134919-004-471f8511/manifest.json只为prepared-only，不能当实测。
- BOM/CRLF与git diff --check通过；本轮未commit/push，不归档原任务。

## 下一人工证据

用户沿同一Scripts/Start-PptNavigationDiagnostic.cmd启动，在PPT保持原状时按刚才同一菜单/设置位置正常退出一次即可；若原操作先结束放映再退出则保持同顺序，不需再做来回大矩阵。采集结束后读取最后未闭合native/managed阶段，核对进程真正退出码；scope结束不是exit0证明。原退出故障尚未修复，不延长超时、不杀Office来制造通过。
