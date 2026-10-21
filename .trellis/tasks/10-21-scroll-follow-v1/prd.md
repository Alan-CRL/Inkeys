# 通用滚动批注初版

## 目标
在 feature/move 分支实现 Desktop 主屏单一主要内容区域的视图调整。取得有效 F0 并初始化 OpenCV 后画布穿透，已有墨迹由真实视觉位移驱动 Draw3 Viewport，退出后继续绘制和编辑。

## 需求与验收
- 原始需求见 requirements-source.md；用户已在 2026-10-21 明确批准本设计及实现。
- 只支持二维平移，不启用原有双指导航产品门禁，不改变墨迹算法、Shader、预测或笔锋。
- 捕获使用独立 Capture-Only Mag Host/Child + PrintWindow(PW_RENDERFULLCONTENT)，排除同进程窗口。不得换用其他截图 API。
- 捕获有效性、超时、可信度和特征接力必须实现；可信零位移接受，黑帧或持续不可信暂停，不自动恢复。
- Desktop 主栏 More 扩展区提供视图调整按钮。定格开启时禁用。其他工具/Undo/Redo/Clear/Freeze 先结束本会话再执行。工作区/文档/显示几何/设备生命周期变化安全结束。
- 复用 Draw3 双窗输出握手与平移/Tile/GPU cache 路径；保持原始 Stroke 坐标、Undo/Redo、Page GUID 和 UInk v10。
- 诊断复用异步有界 DiagnosticOutput，普通用户不弹控制台。
- 保持 Win7 主程序兼容目标；该可选功能按 Win8.1+ 和运行能力门禁。
- 捕获 PoC 真机门槛通过后才能实施追踪器；追踪器离线通过后才能正式接入。
- 受控 GUI 测试已经用户授权，限专用内容、临时数据及自动关闭窗口。未测硬件如实列明。

## 任务树
1. scroll-follow-mag-capture：独立捕获、有效性和真实 PoC。
2. scroll-follow-motion-tracker：LK/纯平移/可信度/接力与离线误差。
3. scroll-follow-product-integration：会话、Draw3、主栏/i18n/SVG。
4. scroll-follow-persistence-regression：UInk、编辑历史、ARM64/x64、受控 GUI。

## 边界
不做缩放/旋转输出、恢复/校准、UIA/COM/扩展、多区域、SLAM、正式提示与设置页。保留最小扩展接口，不存视觉特征到 UInk。不 commit/push；任务记录和归档使用 --no-commit。
