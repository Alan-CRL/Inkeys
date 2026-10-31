# 第一批实施边界

- 最小行为缺口：同帧目标覆盖破坏描边同步；无影响光照触发跨窗重绘；只读DC错误声明全部修改；长帧缺少足够分段诊断。
- 所有权：目标在Bar.RenderLoop；光照贡献与广播在Bar.Scene；DC在各自Bar/PageControl呈现者；诊断计数留在正在执行的渲染回调，汇总与限频由唯一Scheduler负责。
- 预计修改：Bar.RenderLoop.cpp（S1/S3及Bar诊断）；Bar.Scene.cpp（S2）；PageControl.cpp（S3）；Bar.Rendering.cpp（mask及slice计数）；RenderPipeline.cpp/.cppm（每客户端计时、POD采样/汇总、sink）；IdtMain.cpp（已有logger桥）；必要的内部diagnostics header和已有headless测试文件；必要时FramePacing.cppm仅增加原始dt观测，不改Tick/Rebase行为。
- 新头文件仅在确实需要无窗口测试限频边界时使用，工程登记由主代理统一；不创建新build系统。
- 分工：implement_functional_fixes拥有Bar.RenderLoop/Scene/PageControl及相关动画验证；implement_diagnostics_core拥有RenderPipeline/IdtMain/logger桥/调度诊断测试；implement_light_counters拥有Bar.Rendering计数；主代理负责整体验证、工程项/规范和任务记录、交叉检查。
- 功能与诊断拆开审阅：诊断异常、sink异常、没有调度上下文时都不能改变原渲染结果。
- 明确排除：不修时钟/退避、缩容、exact矩阵判定、全局线程/backend、seqlock或零dt；不改变质量/速度，不GUI、commit、push。
- 验证入口：现有InkeysRepo.sln包含Inkeys/PptCOM/PptCOM.Tests/InkeysHeadlessTests；Debug|ARM64全solution构建。原生MSBuild由vswhere定位。headless运行加--no-window。
- 现有Scene.cpp不在headless链接范围；不能以复制bool表达式的测试冒充hooks集成。必要时采用真实damage算法用例、生产分支审查和完整构建，并明确该局限。
