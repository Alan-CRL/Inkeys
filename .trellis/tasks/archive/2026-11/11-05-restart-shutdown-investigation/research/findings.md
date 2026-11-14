# 已完成修复与现场证据

## 普通重启交接

普通R重启由已验真的监督器检查精确旧父PID的顶层窗口可见性，隐藏后只创建一次替代实例，同时监督旧进程至原绝对截止。连续单实例presence不释放后重抢。

固定布局handoff v2仅供同镜像/同物理安装根普通重启，新实例Ready后无需等待仍在监督旧进程的helper。默认v1仍等交接父退出；已知更新U与崩溃X在旧父死亡后创建新实例。UpdateRestart状态原子发布，意外发现暂存更新的新实例沿原20秒等待及超时保留指令/正常启动或失败分支处理。

权威合同是 `.trellis/spec/native-desktop/single-instance-and-startup-handoff.md`。本次没有归档旧 `11-01-single-instance-safe-relaunch` 任务，也没有把其尚未验证的GUI/UAC/Win7等边界改成通过；旧PRD关于普通重启必须等旧父死亡的历史要求已由本次用户需求更新。

## Settings退出竞态

初次18轮记录位于 `Build/Validation/shutdown-series-20261105-161358-031-78294f48`。第9轮PID41908、11轮PID56712约15.15/15.14秒以0xE1430016退出，最后未闭合阶段为main-setting；白板、PPT及Draw3清理此前已返回。第3轮5.5662秒则是另一路PPT等待，不能混为同一问题。

源码确认：Shutdown投递最终control并等待drained后，才注销Settings帧；control清空协程并通知true，调度器随后仍能执行迟到Settings请求。SessionState即使隐藏、只要没有resident就要求初始化，帧回调会将drained置false；新协程首次只预热并挂起，隐藏路径Idle，不再排空。主线程可能永久等待。

修复仅改变生命周期顺序：私有QueueFinalSettingDrain先Unregister并等待在途回调结束，再PostControl执行原有草稿提交、协程及资源清理；原CV与业务FIFO排空、15秒监督保持。不能直接用!settingInitialized阻止首帧，否则破坏Register后等待预热的初始化握手。

## 复测26轮

修复版EXE SHA-256：`B8674ACEEDA7AE2F20E9C91D5EBCFD9786D38AC899E5E3D86A0D93AFF09B949A`；产品提交 `1b0db5fc56a55610677a6838eef7670b1fc06daa`。

- 第一组6轮：`Build/Validation/shutdown-series-20261105-190912-000-9c8b534b`。
- 第二组20轮：`Build/Validation/shutdown-series-20261105-192055-340-5646d8dc`，状态completed。
- 26轮均exit0、native与managed final阶段完整，逐条原始native时戳独立重算与汇总吻合；无capture errors，双路流排空。
- main-setting最小1.0781ms、最大9.7916ms；未再出现原设置关停15秒兜底。
- 总关闭观测耗时最小563.6056ms、中位1277.91375ms、最大12045.0999ms；22轮低于3秒。人工不同场景不构成受控性能对照，不能据此宣称所有长尾已解决。
- 第一组第6轮曾标incomplete并中断，是全局同名进程扫描误把 `Inkeys-draw` 的进程当成当前安装的后继；该轮自身exit0、阶段完整。原记录保持原样，未改写成成功。

## 保留探针及使用

- 双击 `Scripts/Start-ShutdownDiagnostic.cmd` 默认20轮，用户操作界面并点击“关闭软件”，每轮结束后自动衔接。单独关闭设置窗口不是退出软件。
- `.ps1 -Rounds N -PromptForNotes` 可指定轮数并逐轮备注；`-PrepareOnly`仅校验版本/标记且不启动进程；`-SelfTest`只运行固定的两轮无窗口PowerShell模拟进程，不启动产品。两个模式互斥。
- 每轮有manifest/stdout/stderr/native快照；会话有manifest及summary.json/csv，保留PID、退出码、未结束/无效native和managed final阶段、主清理与主清理后耗时。无set-off则耗时为空并停止，不伪造0ms成功。
- 计时是“接纳关闭→采集器观测进程终止”，不是物理点击时刻；死亡观测先于流排空和离线分析，不把生命周期或管道排空相加为关闭耗时。
- 进程守卫只含本安装目录与精确installer子目录，外部worktree/类似前缀排除；空、不可读及非完全限定路径保持unknown阻止启动；不关闭任何现有进程。
- `INKEYS_PPT_PERF=1`/`INKEYS_PPT_TIMING=1`只设给采集器启动的child，不修改永久环境或配置。原生及managed探针默认关闭，关闭时不格式化/读QPC/写诊断日志，故保留供未来定位；正常使用不要持久启用这两个变量。
- PowerShell7自检和实际采集通过；5.1语法检查通过，runtime受本机Restricted策略限制，未绕过策略。入口优先使用可用pwsh。
