# 2026-11-01 分段诊断设计与准备（用户已批准）

## 问题与边界

Previous请求epoch16总等待1228ms，managed owner执行141.347ms，约1087ms未分清native排队、COM封送/owner排队及焦点收尾。用户已批准设计并准备；人工PPT/GUI测试全部由用户执行，AI只运行构建/无窗口验证。不修改翻页顺序、调度、2秒超时、公共COM ABI/schema、动态访问、RCW所有权、窗口焦点策略或ready门禁，不修等待UI、不commit/push。

## 诊断协议 v1

新增默认关闭的进程启动环境开关 INKEYS_PPT_PERF=1；与既有PptOwner/PptSync兼容。数值事件前缀[PptPerf]，固定字段v=1 source=native|managed pid tid qpc frequency span parent event=begin|end|instant stage op；局部身份字段 native local_session/epoch，managed request/show_session/binding，绝不把不同层session或序号当成相同ID。

时间戳使用Windows QPC/Stopwatch.GetTimestamp，记录frequency。span为本source进程内递增ID，begin/end配对；parent关联本source请求/阶段。begin必须在实际操作前取时，end在finally/RAII取时，异常路径也闭合。disabled不格式化/写日志，不调用额外COM属性。日志错误不得影响原控制流/异常/释放，不使用Marshal.GetHRForException。不开新Office线程、不缓存/跨线程传递RCW、不枚举每个Slide.Item诊断。

跨层关联：优先pid+OS tid+op+native COM调用时间区间内的managed RequestOwnerCommand；封送改变线程时使用同进程同op唯一包含区间关联，保留两侧原ID；多个候选/缺begin或end/clock不一致一律unknown，不靠行顺序猜测。owner执行可晚于wait超时：request ID保留，把等待返回与owner完成明确分开。

## 阶段

native：成功入队到业务取出（用epoch/本地span关联，不改变发布顺序，不持队列锁写日志）；业务命令；NextPptSlides/PreviousPptSlides的实际COM调用前后；FocusPptSession前后；最终Complete。需要数值标明丢弃/过期和异常路径，不泄露路径、标题或墨迹。

managed：RequestOwnerCommand入站/return、邮箱等待、owner执行；Next/Previous的当前页预检查、View取得、实际InvokeMethod；service中的ObserveBoundShow、MaintainBinding/ROT、descriptor维护、annotation与Caption读取。给owner维护span定名，不用只给慢阶段end日志造成无法观察当前挂在哪段。现有对象取得/finally不重构。

## 输出

native复用异步IDTLogger，事件QPC在调用线程捕获；managed使用显式INKEYS_PPT_PERF启用的安全诊断输出到标准错误（可直接OpenStandardError，独立于ConsoleOutput.PptCOM，避免测试时改用户配置）。缓存writer并逐行完整串行写，异常吞掉；不创建可见控制台。两侧均只记录数值/固定阶段词。

## 实施所有权

- managed代理：PptCOM/PptCOM.cs、必要internal PptPerfTrace.cs与csproj、PptCOM.Tests现有测试文件/csproj。负责managed所有span和回归；不得改native/Scripts/任务/spec。
- native代理：Inkeys/IdtPlug-in.cpp、必要独立PptNavigationDiagnostics.h及必要现有headless测试。不得改managed/Scripts/任务/spec。
- 采集代理：Scripts/Start-PptNavigationDiagnostic.ps1、Scripts/Start-PptNavigationDiagnostic.cmd、Scripts/analyze-ppt-navigation.py及必要自测。不改产品代码/任务/spec。
- 主会话：任务/spec/设计协同、构建/产物交接/最终独立审查。任何临时扩展先通知。

## 用户测试入口

提供一键.cmd/PS启动脚本：固定本工作区Build/ARM64/Debug/Inkeys.exe与工作目录，检测已运行Inkeys并提示用户自行正常退出，不kill/自动关窗；仅用户运行时启动GUI，本轮AI只跑PrepareOnly。每轮Build/Validation下唯一会话目录，元数据记录EXE/DLL SHA、源码HEAD、QPC频率/UTC锚；启动前设置本子进程INKEYS_PPT_PERF=1及INKEYS_PPT_TIMING=1，捕获stdout/stderr/native日志，不修改永久环境/用户配置/PPTX。脚本等待测试进程正常退出后收集文件并给出路径；提前/异常结束有明确状态；不误把旧实例/旧EXE当成本轮测试。

分析器仅标准库，按QPC而非文本行顺序配对，输出每个控件请求各段及不明确/缺失区间，区别owner执行、排队和focus，timeout后owner继续不得算作取消。统计不声称为屏幕光学翻页耗时。不覆盖既有测试证据目录。

## 验收与执行顺序

1. 补诊断协议后各模块实现；managed行为/动态释放现有测试继续通过，补启用/禁用/异常writer/配对/维护阶段真实路径测试，不改业务语义。
2. native静态/无窗口检查、分析器样例含正常/排队长/焦点长/超时晚完成/乱序/缺记录/重复日志/不同clock域，PrepareOnly证明不启动GUI、不改配置。
3. 独立review，managed Release|AnyCPU构建/测试；显式DLL/TLB交接，TLB必须与原ABI字节一致；ARM64原生MSBuild完整InkeysRepo.sln Debug|ARM64，same invocation规范化PATH，至少5分钟预算。既有沙箱PDB问题已由同工具链探针对照确认，实际native构建可直接申请允许PDB通信的环境。仅遇增量链接LNK1000时核验后清本工作区生成.ilk，不改工程。
4. 适用headless与脚本自测通过、BOM/EOL和minimal diff、EXE内嵌DLL哈希确认，写交接操作（3↔4，分别控件/键盘，记录卡顿操作/近似时间，正常退出后交日志）。不自行启动GUI/Office或Computer Use，不未经授权commit。
