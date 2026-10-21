# 原定格 Mag 正式取帧设计

## 基线与当前授权

基线3ace726b保留同线程Mag初始化/创建、readyPromise、普通定格协调线程，软件内冷窗口无callback与callback恢复后均取得正确图像。当前用户批准正式取帧及所有后续阶段。共用原窗口，不新建Mag对或进程级lease，不更换截图技术。

完整本轮测试限制见父任务research/implementation-authorization.md：无窗口或不可见HWND，禁止可见/前台/Computer Use；必要屏幕验收时由主会话停止。

## 正式接口与线程

提供Start/Stop/Snapshot与同步借用BGRA帧回调，附session/sequence、QPC、width/height/stride、source/workArea/display身份、API与质量统计。生产取帧优先由既有MagnifierThread作为串行执行者，避免独立worker与Freeze竞争，Overlay UI不承担可能阻塞的PrintWindow。原初始化/创建owner不变；具体接口和调度在implement.md记录。

原Win32MagnifierOperations复用PrepareHidden（alpha0且可绘制）、完整原角色排除、Source/Invalidate/Redraw；必要参数重载只作用于取帧。客户端与原Source少一像素几何必须如实记录并明确坐标比例。单DIB无FIFO，20FPS目标，首帧有界预热、稳态允许旧帧，静止图不作为错误；callback不作为依赖。

## 状态与停止

Start只代表接受异步请求，不是有效F0。活动Freeze、旧资源未退出、无效能力/几何拒绝。Freeze到来先关闭取帧发布，实际读回结束后再执行原事务。Stop立即撤销发布并唤醒等待，UI不join；系统调用未结束时旧状态与缓冲保留，重启拒绝。

有界监督区分启动准备与运行停滞，1秒无完成结果关闭发布；不TerminateThread、不提前清理系统调用仍使用的资源。回调依赖保持至真正释放。正常退出沿现有MagnifierThread退出后WindowService销毁；异常owner失效需防止旧发布并明确安全释放/保留策略，不做无关Window框架改造。

## 验证

纯状态或可注入backend测试覆盖参数/质量/首帧、静止、取消、超时、忙碌、代次、Freeze互斥、迟到回调与资源真正释放。不能将假backend或隐藏黑帧称为实际变化内容通过。当前真实屏幕回归只使用上一轮基础证据，本轮新生产路径与完整产品在最终必要可见验收处明确列为未测。

完成后顺序trellis-check，再开始下一个子任务；接口合同与实际结果写入research/production-capture.md。保持原BOM/EOL，关键中文注释，不commit/push。
