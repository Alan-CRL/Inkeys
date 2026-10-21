# 已批准的滚动批注完整设计

## 当前范围与基线

在 feature/move、3ace726b 基础上实施全部后续阶段，不 commit/push。原独立 Mag/runtime lease 试验已删除，历史原因与证据保留在 research。当前只使用原定格 Mag，保留同线程初始化/窗口创建及 readyPromise 和普通 Freeze 分工；不引入 WGC、禁止捕获属性或 DWM 修改。

用户本轮只允许无窗口或真正不在屏幕显示的 HWND，禁止 Computer Use、前台/置顶和可见测试内容。代码与允许的离线/隐藏验证按阶段完成，真实屏幕必要验收是停止门槛，不能因允许测试全部通过就标父任务完成。最新授权见 research/implementation-authorization.md。

## 基础能力与捕获

沿现有 OpenCV4.10.0#3 静态 core/imgproc/video，不改 manifest/triplets/features。正式 Capture 详见捕获子任务 design 与 research/production-capture[-check].md。原 MagnifierThread 串行 Begin/Update/PrintWindow(2)/End 与普通 Freeze；Start/Stop/GetSnapshot 异步，监督只关发布，不操作 HWND/DIB，不等 PW。原 owner 初始化及窗口创建不变；无第二个 Mag、owner RPC 或 runtime lease。

Frame 为回调限时借用 BGRA、宽高/stride、原主屏/Source/workArea/Display代、时间/session/sequence/质量/统计。后端核验实际 identity Mag transform，source到client为1:1；原 source/client 少1像素不构成缩放。无 callback 必须可用，不把等待/Redraw/callback返回当作最新整帧完成凭据。

目标20Hz但不是实测承诺，首等待800ms、重试100ms、稳态等待10ms、无FIFO；启动5s内有有效F0，之后1s无有效结果撤销。API/黑/低信息失败不能续期。Stop拒迟到发布，旧Read/回调/End返回前保持occupied，拒重启。正常join再原owner uninit；异常窗口失效只关门，inFlight保留runtime并禁再启，不强杀线程，不宣称OS销毁与永久阻塞均有界。

上一轮原owner两进程真实无callback变化图案和排除通过，是基础能力证据；本轮正式worker读回仍需单独实屏验收。纯离线Tracker可独立继续；不能把假backend视为实屏通过。

## 独立 Tracker

VisualMotionTracker::Initialize/Process/Reset只接标准图像/时间数值，无HWND/Draw3/UInk/UI和线程。输出接受、原图double二维delta/total、可信度/原因/统计与暂停锁存。实际接口见 Inkeys/Inkeys/CV/VisualMotionTracker.h。

半分辨率灰度按真实尺寸还原，8x6网格约320GFTT/LK21x21/maxLevel3、FB约1处理px，按网格空间计权纯平移共识。起点至少约40有效/内点、65%一致、残差约1.5处理px；覆盖/存活/FB/跳变及变形诊断作为硬门和可信度，不引入calib3d，不输出旋转缩放。

可信帧才事务提交参考/total/特征；短拒仍对最后可信参考，不能漏中间位移。保留固定出生内容锚点，稀疏格可信补点按当前total反算，不重设世界原点。有效同图零位移正常接受，不deadband吞小运动。严重无效立即Paused，普通拒绝连续3帧Paused，保持最后total，无自动恢复。

独立GT按不可变非周期长画布绝对camera生成。静止300帧漂移<=0.5原图px；每个prefix误差<=max(3px,实际路径*0.2%)，覆盖十屏/全初始特征离屏接力/亚像素小步/反向二维/固定UI/动态/拒绝恢复/缩放旋转/低信息/布局与寿命，不放宽误差或用GT干预算法。

## 会话与 Draw3

Business/ScrollFollow为Off -> Initializing -> Tracking/Paused -> Stopping -> Off，UI仅选中/未选中。先Desktop/Freeze/平台检查，关新Down并收尾已准入contact，由owner取得真实基准视口及场景；F0/Tracker成功后临时selection辅助输出ready+双窗owner交接成功才Tracking。失败只撤本会话blocker，不改用户工具和笔属性。

Begin/ApplyAbsolute/End为窄可靠owner协议，session/sequence/Workspace/Page GUID/Host/scene/runtime/几何身份齐全。高频目标单槽latest-only，发布绝对累计量；owner用baseViewport-total求目标，通过current-target差复用ApplyCanvasContentTranslationChecked/TranslateViewport。Draw3原生输入/stored/viewport是client/canvas px，不能再除96/DPI；tracker只还原一次真实降采样。

重复/旧会话/错场景/非finite拒绝。End先关发布并自带最后可信目标，实际viewport恢复和full present成功再回执，不能把commandProcessed当完成。正常结束再恢复用户effective output，等WindowService成对显隐完成才释本会话准入。Shutdown不恢复可见表面，最后viewport必须先于ExitAutoSave，不等已关闭窗口服务。

准入原因并入Host聚合gate，不能被下一次状态泵覆盖；contact沿原seal/quarantine直到真实Up，不Reset输入生产者。临时selection覆盖必须同时参与IdtState Publish/Reconcile/guards和Bridge，不改真实工具。产品动作入口保证End-before-action，同工具early-return也覆盖；白板/PPT/文档/几何DPI/窗口/device/退出撤销并退役身份，无自动恢复。

## GPU、历史与保存

唯一Draw3/辅助ULW输出握手不变，无第二个GPU画布。平移复用raster/hot-preimage失效、Canvas-local composition cache、可信L2、按预算可见Tile恢复；不改Shader/预测/笔锋/渲染参数，不开放双指导航门禁。

跟随不追加Undo、不清Redo、不改Stroke点/Page GUID。新笔划锁存当前viewport，UInk v10原字段viewport双向贯通。Desktop自动保存继续Clear/Exit，不逐帧保存，不更改原codec/schema或保存入口。

## UI、诊断和兼容

More中Setting前添加稳定官方ViewAdjust条目、SVG、中英繁体视图调整/Adjust View/視圖調整。enum追加保旧值，注册白名单、explicitMore、最终selected/disabled全覆盖。配置只补缺新条目，不重排已有顺序/Visible/未知插件或A区，不顺带恢复整个B区自定义。

Freeze active禁用，跟随中工具/橡皮/选择/Undo/Redo/Clear/Freeze先End再原操作，保留shortcutlease及场景门。状态日志复用唯一DiagnosticOutput::Session有界异步队列，[ScrollFollow]前缀；--scroll-follow-console/--scroll-follow-verbose后者含console，普通启动不新弹。状态错误即时，默认每秒捕获/追踪/viewport汇总，verbose逐帧；采样/RTS/绘制不阻塞外部IO。

功能Win8.1+、非WOW64、Mag能力有效；不提高全程序Win7门槛、不新增旧系统不可用API硬导入。沿原依赖和构建链。

## 分阶段与验收

Capture实现/check -> 纯Tracker实现/check -> 产品实现/check -> 保存与完整回归/check。主会话维护跨模块契约/spec，Trellis任务日志不自动提交。

所有允许的测试先完成：完整InkeysRepo.sln Debug ARM64、定向及--no-window、全隐藏Host A->follow->B->history->真实保存重载、x64构建尝试、i18n/PE/diff/物理BOM/EOL。全部不可见Host基线已通过，见 research/execution-current.md；禁止运行实际ShowWindow的旧--draw3-hidden-test/eraser测试。

正式原Mag worker实屏取帧/排除/持续性能、浏览器/Word/PDF滚动、真实双窗交接/穿透/墨迹稳定、主栏视觉、笔触控/旧Windows等无法由隐藏用例代替。必要可见验证时停止并逐项报告；未通过门槛不标完成。完整原始验收与产品规则保留在prd与各子任务。
