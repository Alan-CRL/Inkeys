# 独立激光笔粗细与预览设计

## State and Draw3 Contract

在 `StateModeClass::Pen` 内新增独立激光宽度字段及细/中/粗 `3 / 5 / 7 DIP` 三档预设，初始化为中档 `5.0f`。`GetPenWidth()` 在 `laserActive` 时先返回该字段，激光快捷按钮通过同一 `SetPenWidth()` 更新它并继续发布产品状态。这样不新增额外状态通路，既有 `SyncDraw3State()` 和 `ProductState::widthDip` 仍是唯一发布入口。

Draw3 `DiameterForTool(DrawingTool::Laser, ProductVisualStyle)` 改为使用 `widthDip`；既有 `CanvasDiameterForTool()` 保留激光按 DPI 缩放的规则。因此活动笔迹在 Down 时锁存独立宽度，且 `ConfigureDrawingCursor()` 取同一计算结果，无须引入光标专用状态。

## Bar Capability Gate

在 Bar 布局辅助层区分“普通墨迹粗细控件”和“激光三档快捷控件”。普通笔型的展开箭头、滑条、预览弹窗和精细轮盘要求 `!stateMode.laserActive`；激光态单独显示三档纯色圆形按钮，并使用激光预设数组判断选中和处理命中。切换工具时，普通控件失效则调用既有关闭路径回收滑条与精细轮盘状态。

笔型扩展箭头使用同一激光态门禁，覆盖按钮配置、扩展菜单资格、视觉几何和命中。这样激光保持独立工具状态时，不会因残留的 `Pen.ModeSelect` 将硬笔/软笔/荧光笔小三角重新显示。

## Preview

保留预览区域和面板动画几何。激光态跳过普通笔曲线及荧光笔渐变，先以激光独立宽度画红色圆头外套，再以外套直径三分之一画同中心白色圆头内芯。两层使用同一裁剪、位置和 slider 过渡计算；但由于激光态不开放 slider，过渡值稳定为预览态。红色取 Draw3 当前实体外套 `RGB(255, 11, 30)`，不绘制散射、edge 或 glow。

## Compatibility and Rollback

未激活激光时，画笔、荧光笔、形状和橡皮的现有宽度、预设与交互不变。回滚只需移除激光宽度分支及 Bar 门禁，不涉及配置格式或持久化迁移。
