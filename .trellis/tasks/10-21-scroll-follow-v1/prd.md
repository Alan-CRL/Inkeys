# 通用滚动批注初版

## 目标
在 feature/move 分支实现 Desktop 主屏单一主要内容区域的视图调整。取得有效 F0 并初始化 OpenCV 后画布穿透，已有墨迹由真实视觉位移驱动 Draw3 Viewport，退出后继续绘制和编辑。

## 需求与验收
- 原始需求见 requirements-source.md；用户已在 2026-10-21 明确批准本设计及实现。
- 只支持二维平移，不启用原有双指导航产品门禁，不改变墨迹算法、Shader、预测或笔锋。
- 按用户后续调整，捕获先复用原定格 Mag Host/Child + PrintWindow(2)，沿用其排除与更新能力；不保留独立捕获原型。
- 捕获有效性、超时、可信度和特征接力必须实现；可信零位移接受，黑帧或持续不可信暂停，不自动恢复。
- Desktop 主栏 More 扩展区提供视图调整按钮。定格开启时禁用。其他工具/Undo/Redo/Clear/Freeze 先结束本会话再执行。工作区/文档/显示几何/设备生命周期变化安全结束。
- 复用 Draw3 双窗输出握手与平移/Tile/GPU cache 路径；保持原始 Stroke 坐标、Undo/Redo、Page GUID 和 UInk v10。
- 诊断复用异步有界 DiagnosticOutput，普通用户不弹控制台。
- 保持 Win7 主程序兼容目标；该可选功能按 Win8.1+ 和运行能力门禁。
- 捕获 PoC 真机门槛通过后才能实施追踪器；追踪器离线通过后才能正式接入。
- 当前授权限专用自动关闭的软件内截图测试与临时输出；浏览器/Word/PDF 或 Computer Use 按当次用户授权执行，不延续旧轮临时批准。未测硬件如实列明。

## 任务树
1. scroll-follow-mag-capture：共用原定格 Mag 的读回、有效性和真实 PoC。
2. scroll-follow-motion-tracker：LK/纯平移/可信度/接力与离线误差。
3. scroll-follow-product-integration：会话、Draw3、主栏/i18n/SVG。
4. scroll-follow-persistence-regression：UInk、编辑历史、ARM64/x64、受控 GUI。

## 边界
不做缩放/旋转输出、恢复/校准、UIA/COM/扩展、多区域、SLAM、正式提示与设置页。保留最小扩展接口，不存视觉特征到 UInk。不 commit/push；任务记录和归档使用 --no-commit。

## 上轮诊断里程碑（范围限制已结束）

用户后续收窄为移除多余原型并完成 Inkeys.exe 内 PrintWindow(2) 验证，保持现有定格线程设计，同时实际尝试图像缩放回调候选。完整追踪、会话、主栏与存储阶段尚未进入；本轮不能宣称滚动批注功能已实现。

## 本轮继续实施（2026-10-21）

用户已批准完成正式取帧及全部后续阶段，上一轮仅诊断的范围限制结束。本轮验证仅无窗口或完全不在屏幕显示的HWND，禁止Computer Use/前台/可见测试；必要可见验收时停止。当前授权见[implementation-authorization.md](../10-21-scroll-follow-v1/research/implementation-authorization.md)，其优先于本文旧轮次边界。原定格Mag的无callback基础验证见research/freeze-printwindow.md（捕获子任务）；后续正式路径须分别验证。本轮不自动commit/push。


## 2026-10-22 实际画面测试继续

用户现已允许创建窗口、操作电脑和截图，但不使用Computer Use。此前本轮不可见限制属于上一阶段历史，当前以父research/live-test-authorization.md为准；不自动commit/push、不结束任务。先正式worker取帧，成功后真实产品/系统窗口交接，逐项记录而非沿用seam结果。
