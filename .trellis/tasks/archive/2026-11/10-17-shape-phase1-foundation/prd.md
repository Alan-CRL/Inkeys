# Inkeys3 图形绘制与选择系统

## 已批准范围（2026-10-17）
本轮完成 Phase0→1 及 Phase2 的通用 shader 准备。不新增 UI 按钮、新图形用户入口、选择窗口或交互编辑。保留原四类图形及普通笔性能。

## 验收
- 图形类型、创建方式、样式独立；稳定 GUID 与 contentId/undoId 分离。
- 共享核心支持线段、矩形、圆、椭圆、折线、凹简单多边形与有序复合对象；控制点、命中、边界、缓存和纯命令模型。
- 原四类图形走兼容路径；创建/取消/Undo/Redo/Clear 行为不退化。
- 独立图形 shader 复用设备、t0 上传、操作层和资源链，离屏验证真实生产 renderer。
- UInk10 标准几何加带版本 inkeysShape；桌面/PPT/白板恢复保留身份参数，白板隐藏历史兼容。
- 完整 Debug|ARM64 构建；CPU/离屏GPU/持久化回归；无 GUI、commit、push。

## 后续阶段
Phase2 面板与构造 → Phase3 SelectionOverlay → Phase4 多阶段与空间几何 → Phase5 坐标与吸附 → Phase6 函数与动态关联 → Phase7 墨迹统一编辑。
未来阶段只建立规划任务；详细设计在各阶段正式实施前完成。
