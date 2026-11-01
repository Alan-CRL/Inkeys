# 环形加载指示器实施验证（2026-11-01）

## 结果与实现

已按用户批准实现：原Page右上角单角标改为SurfaceFrame完整轨道/文字色TextPrimary粗圆头250度弧，最大8DIP，中心right-15/top+6.5（留文字/AA空隙），track1/前景2DIP。连续等待绝对1000ms才入场；入180/退140ms公共Cubic透明度和.75→1缩放；旋转约一秒一圈，global off静态/显隐即时但保留门槛。普通数字、稳定显示缓存、错误/Retry、业务/ACK/input与COM均未修改。

源范围共7文件：新Bar.PageLoadingIndicator.h；Bar.Scene.cpp(m)；PageControl.cpp；Bar.Main.cpp仅动画设置后PptPageMask唤醒；Bar.Interaction.cpp仅现worker测试与helper include；headless page_control_tests.cpp。后两处额外接线/测试范围已记design，无新线程/timer/依赖/Scheduler API。

单调延迟与公共animation dt分离，InputWorker注册tick无长按也合并原子deadline，到期只请求当前Surface重核；重复true不重计、新段重新计。Scene独立环视觉需求不混旧AnimationActive的窗口保活/input；隐藏/白板/session退休，退出清角标后Idle。私有arc/圆头stroke/双色brush冷建，避免共享单brush别名和GetRoundStrokeStyle同步日志失败路径；失败只跳环，不改数字ACK，按新段/session/epoch恢复，旧尾帧与最大包络局部损伤。

## 独立检查修正

正式trellis-check发现同frameTime false→true会丢结束边沿，已在Scene收到false即退休requested/deadline并保持当前显隐值，补真实Scene新段/退场连续回归。改用自有缓存圆头stroke，避免共享渲染器的失败日志/latch；InputTick fixture调用前清空并随后恢复长按时间。全部静态复核通过、源码冻结后才统一构建。

## 构建与无窗口执行

- 完整InkeysRepo.sln Debug|ARM64，动态定位VS ARM64原生MSBuild，同invocation规范化Path/PATH保留原搜索值、禁node复用，900秒预算，已证可用PDB环境；exit0/43.16秒/0error。82条既有类型转换warning来自未改hashlib/Setting/Rendering/IdtMain重编，无升级/配置或无关源码修正。
- InkeysHeadlessTests.exe --page-control-only exit0：生产helper999/1000、dt0/idle、重启/session/隐藏/off、deadline等回归。
- --ppt-session-only exit0：原数值协议/释放相关纯值状态不回归。
- Inkeys.exe --ui3-input-worker-test exit0，PASS production mailbox/dispatcher/continuation/deadline/cancel；无Pointer Down的真实注册/WaitUntil到期、取消和注销验证。
- Inkeys.exe --bar-eraser-offscreen-test exit0、PageControlUI3 failures0：真实Controller dt0到期Continue，四面三倍率/最大9999/999、短延迟无像素、旋转/进退场/partial=full/尾帧清除/资源epoch/globalOff静态/200%双色像素，原数字与错误状态保持。资源初始化失败的安全降级主要由静态/状态边界覆盖，不扩大为真实故障设备验收。
- PptCOM源三文件/DLL/TLB逐SHA与既有baseline完全一致；EXE内嵌DLL仍1061DDF51BD7CF646030AC74830DCD67CE74508448D879D70BBEE3EAA4D850E3，TLB仍C652B5DD...。未重复未变managed运行矩阵。
- 最终EXE SHA256 7E177A7802E0998D0E8A094CF3FE24A2F4D7AE86175DA9CF036D42416B8ECA55，Headless A141F2C39577DBD86C0BCF6860C515318981D5D90C94273C8810CBB100F6A2A7。
- PrepareOnly exit0，190145-423-0c109cb4仅预检；opt/deploy.json及实际Inkeys/Config/main.json前后SHA不变，无GUI。
- 原始日志Build/Validation/page-loading-progress-ring-20261101；git diff --check与原UTF8/CRLF保持。无GUI/Office/ComputerUse、无commit/push/归档。

## 人工交接与边界

沿原Scripts/Start-PptNavigationDiagnostic.cmd启动，用普通短加载和原慢3→4各观察一次：短于1秒不出现，持续加载一秒后小环渐显/旋转，结束渐隐，位置/颜色/大小合适且不遮数字。正常退出收集。无需大矩阵；静态/无窗口结果不替代桌面观感或其他Office/Windows版本。旧PPT性能问题不属于本任务，未声称延迟缩短。
