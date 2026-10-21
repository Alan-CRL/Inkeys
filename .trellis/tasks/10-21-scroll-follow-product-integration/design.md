# 滚动批注产品接入设计

## 当前范围与依赖

用户批准完成全部后续阶段；本轮只允许无窗口或完全不可见HWND，不能Computer Use、可见内容或前台测试。先完成正式捕获与纯Tracker的实现/check，再接产品。实际接线锚点见捕获子任务research/product-integration-current.md；旧独立Mag方案已移除，不恢复。

## 会话与数据

Business/ScrollFollow维护Off/Initializing/Tracking/Paused/Stopping，提供异步toggle/stop/snapshot/availability；UI只投影选中/未选中，工具与笔属性保留。启动先Desktop/Freeze/能力检查，关闭新Down并收尾已接受contact，由Draw3 owner取真实基准和场景，然后Capture F0与Tracker initialize，最后辅助输出ready与窗口owner成对交接成功才Tracking。失败回滚本会话原因，不能释放其它输入blocker。

Capture.Frame BGRA借用限回调，Tracker只在同一采样者串行运行；CPU处理不持UI控制锁。Stop与目标发布必须共用短提交门：关闭发布并取最后已确认绝对目标，不能在UI等待LK/PW。旧callback/context以shared lifetime保活，晚结果靠session/scene丢弃。

Tracker输出原图像素double，源映射以实际Mag transform为准，identity=1；不能按source/client宽差自动缩放。Draw3实际client/canvas px，不乘96/DPI。固定workArea ROI，几何/DPI/client/display变化退役会话，不重选ROI。

## Draw3窄owner接口

Begin/ApplyAbsolute/End在唯一Host/Controller上处理，不另建GPU画布。Begin通过可靠唤醒，收尾accepted contact后在active.empty安全边界读取Workspace/Page GUID、host/scene/运行代次和基准viewport；UI线程不能用缺少viewport的runtime snapshot拼基准。

Apply走单槽latest-only，包含session/递增sequence/完整场景及geometry身份、绝对累计内容client位移。owner从baseViewport-total计算目标，传current-target增量进入现有TranslateViewport/ApplyCanvasContentTranslationChecked尾部，复用raster/hot-preimage失效、Canvas-local cache、可信L2及budgeted visible tiles恢复。拒绝重复/旧session/错scene/非finite，报告钳制，不改双指导航门禁、Shader、Stroke点、Undo/Redo或PageGuid。

End先关发布，可靠屏障自身携带最后可信目标，不依赖可覆盖的邮箱尾项。内容完成回执必须等新viewport真实full present且viewport recovery/refresh结束，commandProcessed不是该回执。正常退出跟随后，还要恢复原工具effective output、确认WindowService成对交接，再释放本会话输入gate。后续工具/Undo/Clear等命令保持End-before-action顺序，不在End未完成时放行Down。

Shutdown/scene退休是不同终态：退出进程时不再恢复可见输入表面，最后可信viewport必须排在ExitAutoSave屏障之前；不能等待已关闭WindowService的显示ACK导致退出死锁。工作区/页/文档替换与device失效退役身份并释放本会话原因，旧目标不得影响新场景；运行恢复不自动恢复追踪。

## 输入与呈现

follow原因必须加入Host::RefreshPresentationInputGateLocked聚合式，不能单独SetAdmissionBlocked后被状态泵覆盖。临时selection override同时参与IdtState Publish/Reconcile/guard与Bridge selection输出，不改真实工具；只强制Bridge selection会被原guard拒绝。Tracking前与正常End后都沿现有唯一双窗口就绪和WindowService显隐owner，不只改WS_EX_TRANSPARENT。

初始化/Tracking/Paused/Stopping均关闭绘制新准入；Pause保持最后可信位置且不自动恢复。严重捕获/格式无效立即Pause，普通视觉低可信3帧Pause。Freeze请求应完整End后走原功能，不覆盖原Freeze唯一observer。

## 动作与生命周期

工具入口在同工具early return之前结束follow；普通Undo/Redo/Clear命令在中央产品提交边界保证顺序，并保留shortcut lease/scene拒绝。白板/PPT/文档/显示DPI/resize/设备/退出分别在产品动作边界先撤销发布，owner场景门为最后防线。不要在各低层函数添加相互等待的stop。

## 主栏与资源

More中Setting前加入稳定官方ViewAdjust条目、三语视图调整/視圖調整/Adjust View与SVG资源。preset追加避免改已有enum值；注册允许列表、Load explicitMore、最终Selected/Disable投影都需覆盖。配置仅补缺少的新entry，保留现有顺序/Visible/未知插件与A区，不能附带恢复整个B区自定义。文案用i18n sync生成并check，SVG源和rc/工程登记齐全。

## 诊断与验证

--scroll-follow-console/--scroll-follow-verbose复用已有唯一DiagnosticOutput::Session与有界队列；状态/错误即时、默认每秒统计、verbose逐帧。采样/RTS/绘制路径不做同步外部I/O，不普通启动弹console。Win8.1+和WOW64能力门只限可选功能，不增加Win7不可用硬导入。

定向无窗口测试覆盖状态/seq/scene/邮箱/End竞态和配置补缺；全隐藏Host模板用metrics smoke，不直接运行会ShowWindow的旧hidden/eraser测试。新隐藏专测保持所有HWND不可见，可注入生成帧或绝对目标验证实际owner/GPU/历史；不能把它称作真实桌面滚动/系统穿透验收。当前授权无法覆盖的可见端到端成为最终停止门槛，不能标全功能验收完成。

## 接线时的初始化与并发细节

Capture自身在首个有效F0前允许预热重试，5s固定期限；不能把初始化预热的首张黑/低信息结果当成已Tracking后的严重丢失。F0前无可恢复位置，启动终止应回滚；已初始化/Tracking后的严重无效才锁定Paused并保最后可信位置。普通Tracker拒绝计数由Tracker管理，不另加会吞其已提交参考的confidence阈值。

F0和后续借用帧都只能在原采样回调内串行处理，控制线程不得并发Reset/Initialize/Process。状态变化用shared context保活和短提交门；图像CPU运算不占该门。等待owner Begin/End、辅助output/WindowService ACK必须在控制侧，不能在Mag回调里等待，也不能在UI事件里join PW。

More按钮仅调用无阻塞业务Toggle，最终Selected读取Initializing/Tracking/Paused/Stopping；Off为未选。Freeze有效时不能新启，但已在Stopping的取消/收尾不能因可用性门被禁止。初始化失败、用户取消和真正运行期Pause要在日志区分，不加正式提示页或参数设置。

## End-before-action补充调查

精确接线建议见Tracker子任务research/end-before-action-contract.md；其中API为建议，实施者可做保持相同语义的最小实现。普通工具/Freeze整个动作留原Bar coroutine，非阻塞ticket Ready后才调用原同步函数，保留实际bool、面板/haptic/同工具意图；shortcut保持原owner pending、expected/lease与固定回执，不能让follow本身把Shortcut Context.available=false导致按键落底层。

普通命令在Host原最终发布锁内保留原lease接纳判定，并可靠挂End前置屏障；owner必须跨帧阻止后继动作执行，直到指定viewport完整full present和原输出/窗口ACK。正常End、scene退休、Shutdown不同终态，退出能升级/打断正在等待可见ACK的正常End；最终CPU viewport先于ExitAutoSave，不因GPU/窗口失败永久等显示恢复。

临时selection使用独立projection代，不能递增真实modeRevision或shortcutIntentEpoch而废掉自己的条件动作。结束恢复当前真实工具的有效输出，不把启动时保存的旧工具快照覆盖期间合法的状态变更。身份使用真实Host/scene/device/文档代；不能把每帧递增的runtimeRevision或开始时必变的outputRevision误当稳定会话身份。
