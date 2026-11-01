# 本任务必须遵守的现有规范摘录

该文件是面向本任务的短上下文索引，不替代原规范；实施前按下列路径/行锚读取相关段落，避免大文档截断。

- .trellis/spec/native-desktop/rendering-and-ui.md:140–183：共用Bar动画，合法活动dt=0不完成；禁动画即时提交；FrameAnimationClock在Idle重基准/钳制dt。因此1000ms延迟必须独立用steady绝对时刻，不累计动画dt。曲线用Bar.Animation.cppm既有EaseOutCubic/EaseInCubic，不造第二套easing。
- rendering-and-ui.md:432–458：PageControl是数据适配，标准按钮/内容/损伤复用Bar.Scene与共享视觉；字号/几何/文字内容分开，Numeric即时更新，错误状态过渡可打断，不为角标重提交布局或取消原文字策略。显式inherit是父坐标真值，缩放/DPI必须一致。
- .trellis/spec/ppt-interop/native-session-ui3.md“恢复进度页码角标与稳定数字”：角标在Page id3右上空白区，原三个点由方向等待OR准备显示，所有四面跟随现scale；当前control/ACK与display override分离，Pending或Recovering显示同场稳定数字，无缓存-/-，真正Ready才更新。Recovering是控制态不可清None，错误/Retry与Page中心禁交互规则不变。本任务只换标记视觉，不改上述事实源。
- native-session-ui3.md:22–38：BasicControl可有HWND0且target0，不伪造InkReady；真实target→Draw3 Present→PageControl成功像素/Window receipt→ACK后才开放输入，旧session/target/pageAckEpoch不能确认新帧。新环或动画不得影响门禁/业务数字/EndShow。
- .trellis/spec/native-desktop/ui3-render-diagnostics.md：render owner只做必要有界数值，禁止新I/O/任意sink/每帧堆分配；FrameResult Retry不等于错误，callback-end不是实际像素成功；帧/资源/目标数据与设备epoch一致，不能重复累加包含耗时。默认诊断关闭，指示器不能为取证制造续帧，隐藏/退场/停产不可残留循环任务。
- 原主产品AGENTS构建入口：InkeysRepo.sln Debug|ARM64、动态定位VS ARM64 host、MSBuild前同invocation规范化Path/PATH、至少5分钟预算；不创建新的构建系统/升级SDK，不GUI或自动commit。

技术接缝的具体行锚见progress-ring-reuse.md；若实现发现这里与源文档或用户当前要求不符，应回到源文档核对，并向主会话报告，不自行扩大兼容/修改控制策略。
