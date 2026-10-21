# 独立 Magnification 连续捕获与 PoC设计

父设计：../10-21-scroll-follow-v1/design.md，作为本子任务完整契约。开工先阅读父设计和research/current-architecture.md。

## 模块边界
拥有 Graphics/MagCaptureService.*、必要的 MagnificationRuntime.*、IdtMagnification.cpp/.h 的运行库寿命窄修改、新 capture tests 与现有工程登记。不要修改任何 Draw3/Bar/Tracker 生产逻辑。

## 接口与失败
独立实现 MagCaptureService、共享 Mag runtime lease、图像有效性、超时与重复启停、[ScrollFollow]基础日志，并在现有 Headless 工程中提供 no-window 单测及显式真实 PoC。
必须实现父design相应接口/线程所有权/错误回滚和门槛。记录额外调查与实际接口到本文件，不引入未批准的替代实现。

## 实际接口与线程合同（2026-10-21）

`Inkeys::Graphics::MagCaptureService` 提供 `Start(MagCaptureOptions, FrameCallback)`、`Stop()`、`Snapshot()`。Start 的 true 仅表示异步请求被接受，不能表示已有有效 F0。参数为非零 session、1..60 FPS（默认20）与 verbose。旧 owner 或 monitor 尚未真正退出时返回 false / `ERROR_BUSY`，不覆盖旧会话状态。

一次 Display 快照固定主屏 sourceRect、workArea、DPI 和 generation；不随会话移动 ROI。帧含借用的 top-down BGRA span、尺寸/stride、source/workArea、session/sequence/display generation、QPC 时间/频率、capture_ms、quality 和 error。alpha 不参与质量判断；span 仅在本次同步回调返回前有效。

独立 Win32 owner 线程先成功初始化 STA COM，再取得 runtime lease，拥有 Host/Child、消息循环与复用 DibSurface。Host 设 alpha=0 后显示，保持不可激活、穿透、不最小化；保留可绘制生命周期。每帧刷新同进程顶层窗口排除表，依次 Filter→Source→Redraw→PrintWindow(child, dc, 2)，没有图像 FIFO。只在同步回调期间借用同一缓冲；慢帧跳过采样周期。

监督线程在启动阶段允许独立5秒 grace；初始化后按最近一次完成结果设置1秒期限。首次 MagInitialize/窗口建立的冷启动耗时与运行中的停滞分别记录，F0前没有绘制穿透或 Viewport 发布，因此启动 grace 不放宽运行时的失效保护。最终 cold PoC 启动耗时109/156ms；生命周期测试中的会话为375/203ms。早期旧1秒总计时曾在初始化尚未返回时失败，日志保留。

Stop/析构不等待同步系统调用或已准入回调。超时或 Stop 关闭发布并请求停止；原 owner 的 HWND、DIB、runtime lease、回调依赖保留到真实返回与清理，monitor 不强杀线程。Snapshot.resourcesReleased 还要求两个原生线程句柄已 signal，防止最后清理标志与真实线程退出之间的再启竞态。调用者在此之前不得释放回调引用的外部依赖。

## 有效性与诊断

AnalyzeMagFrame 先校验尺寸、stride 与缓冲溢出，再采样约256×144的 luma 网格。`mean<2 && variance<4 && nearBlackRatio>=0.995` 判近黑；`variance<4 || edgeRatio<0.002` 判低信息。暗色纹理可有效，重复静止帧可有效。API失败的帧保留具体 stage/error，不能因 PrintWindow 返回TRUE就判像素有效。

生产日志复用已有 DiagnosticOutput 的有界异步队列，前缀 `[ScrollFollow]`，服务不创建新 Session 或控制台。版本/WOW64门禁只作用于捕获入口，较新能力动态解析，不增加 Windows7 硬导入。当前没有主栏/命令行产品入口。

## Mag 与定格的运行库寿命

MagnificationRuntimeLease 动态加载函数表，保护进程首次 Initialize / 最后 lease 释放才 Uninitialize；失败清理保留 DLL 并拒绝下一代。定格只替换 Prepare/Shutdown 的取得/释放两处，不改变其窗口、显示、排除和 Source 协调流程。各 owner 仍销毁自己的窗口。

该寿命合同只解决提前卸载风险，不是跨 owner 可捕获的证明。最终 fixture 已模拟实际 Window Service 的 STA→CreateGroup→首次 Mag 初始化顺序，记录 HRESULT，COM失败直接退出。预存 Freeze lease 时捕获 owner 的 Filter 仍每帧 FALSE/error50，130结果/0有效；共享运行库真实门槛未通过。

## 实测边界与后续

无预存 lease 的最终服务两轮验证得到真实静止源、4次源 phase 更新、完整同进程窗口排除和重复启停。2880×1920，mean=54.523/64.991ms，p95=67.716/79.128ms，实际10.302/8.491 results/s；20FPS仍是目标，不是本机已达指标。一次历史 cold 复跑曾出现无信息帧和1234ms慢调用触发超时，也保留在报告。

对齐 COM 顺序的同 owner 两独立控件原型取得真实像素并通过三轮交替更新，但 private bytes 为310784000→376082432→440078336，尚未证明有界，也未解决阻塞 API 对 UI owner 的影响。每 owner Initialize/提前 Uninitialize 或额外未平衡 Initialize 的实验不满足批准合同，均未落地生产。

本阶段保留安全捕获基础与可重复 PoC，等待 trellis-check。Tracker、Draw3、Bar、UInk 后三阶段保持未进入，不把本任务归档为完成。完整证据：[capture-poc.md](../10-21-scroll-follow-v1/research/capture-poc.md)。
