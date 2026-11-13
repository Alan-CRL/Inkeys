# Mouse latency：独立静态审查

- Date: 2026-11-08
- Role: trellis-check；没有再派生代理。
- Current scope: mouse-latency-scope.md，优先于已有scale-stage及历史Chromium方案；源码基线4b4edb91。
- Reviewed: WindowControl.cpp/.cppm、DrawingController.cpp/.cppm，本轮canvas-pan规范与mouse-latency研究/分析。未改生产源码、既有脏任务文档或屏幕尺度方案。

## Findings (fixed)

没有发现需要局部修复的源码问题。

## Findings (not fixed)

没有待修复的本轮源码问题。文档中的“1000Hz位置流”建议改为“1000次burst及持续位置流”，已交主会话修改其持有的canvas-pan.md：Sleep(1)不能证明实测报点率达到1kHz，测试验证的是唤醒协议，不是硬件性能基准。

## 生产路径复核

1. `WindowController::PublishMouseCursorSample`先发布原mailbox；只在CanvasPanMode且valid/inContact不变时跳过cursor render wake。CanvasPanMode实际为Selection且Whiteboard；非Pan原行为保留。没有更改WM_MOUSE来源过滤、RTS接触、Touch、动力学或全局control优先级。
2. 真实Mouse owner/suppression setter旧值相同即返回。位置流不再从publisher产生control marker/generation；真正owner/suppression变化、valid/contact边沿、Clear以及Selection变化仍走原render/control与系统光标刷新。
3. Coordinator的control marker合并仍每次SignalWake，并推进活动frame generation；恢复循环仍检查pending/wake，WaitForFrameDeadline仍允许真正控制/Down/Up抢占。修复在无绘制收益的生产者源头，不削弱下游保护。
4. 新测试位于RunCanvasPanProductionProbe（约7280行），由RunDraw3ControlFenceProductionProbe（约8167行）调用，真实CLI为`--draw3-control-fence-test`。它调用真实PublishHiddenTestMouseCursor及ClearHiddenTestMouseCursor，先清初始化wake；断言最新mailbox、1000次burst无wake/pending/render、并发流与真实WaitForFrameDeadline、边沿/clear/退出Pan和非Pan位置仍urgent。
5. 并发测试以原子握手和等待期间moves计数证明流重叠，除wall-time下限外同时核generation、control计数、pending与render位。线程在Rig销毁前join，样本只有producer线程局部修改；运行过程中不创建HWND。

## 日志与呈现复核

- tracePanRender同时要求CanvasPan gate/sink可用及导航/恢复活跃；关闭无新增诊断取时或格式化，空Idle不刷render日志。
- diagnosticSampleQpc来自当帧实际消费的导航样本最新QPC，Run保留最近一次实际样本；viewport在渲染线程读取当前canvas。
- recovery-ms、before-composite-ms、composite-ms、work-ms、frame-ms的计算均为毫秒，sample-to-submit/submit-interval按真实qpcFrequency换算。
- PresentFrame的可选QPC输出仅在本帧gate为真时传地址。采集点紧随presentation_.Present返回，早于observer等回调；原metrics采集仍沿用。成功Present才推进previousPanPresentQpc，空循环不会冒充成功提交间隔。
- fallback实际bool只记录，不参与原compositeSucceeded/失败策略；fallback-signature明确是CPU签名，不宣称GPU资源可用。格式参数逐项相符，4096字节有界输出容量足够容纳本轮render行。
- 有效trusted snapshot仍按当前viewport重投影。源码中的无效wake副作用确定，但GPU/光学延迟、RTS回调以前延迟及真实OS消息派发未由这些无窗口测试测得；不能宣布现场唯一归因或手感已验收。

## Verification

- Lint/static: 本审查执行本轮4源码及canvas-pan.md的`git diff --check --ignore-submodules`，exit0；没有为本次修改运行全仓格式化。
- Encoding: 本审查用Python读取HEAD原字节和4源码，严格UTF8解码、BOM一致、CRLF/bare-LF=0全部通过。
- TypeCheck/build: 主会话执行原生ARM64 `InkeysRepo.sln Debug|ARM64`，build-result.json记录exit0/28.58s；本审查读取日志确认没有本轮新增源码警告。既有第三方转换及旧测试nodiscard警告不修改。
- Tests（主会话执行，已读取结果JSON/末尾PASS）: control-fence exit0/1.33s；pan-feel exit0/2.39s、2112行0fail；diagnostic-output exit0/1.78s；Headless pan exit0/1.24s。
- 完整Headless --no-window仍由主会话执行与记录；不等待它才交回静态审查，不宣称其已完成。
- 未执行GUI/Computer Use、commit、push或修改工程/依赖。
