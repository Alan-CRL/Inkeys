# 技术设计

## 渲染模型

- 在 `BarUiShapeClass` 增加独立的 `frameLightPct`，将“按钮本体透明度”和“仅用于第三光源的边框透明度”分离。
- `Shape()` 在按钮本体 `pct` 为 0、但 `frameLightPct` 大于 0 时仍允许绘制 PointLight 边框；普通填充保持不绘制。
- 按钮边框使用现有 `DrawPointLightFrame`，设置 `frameRendering = PointLight`、`framePrimaryLightEnabled = false`，只允许 Cursor 光源。
- `framePct` 保持 0，避免未选中按钮出现常驻边框；`frameLightPct` 控制光影淡入、显隐和按下变暗。

## 颜色与强度

- 主栏按钮每帧根据 `BarWidgetState::Selected` 计算 `TextPrimary` 或 `Accent`，同步到按钮 frame 颜色。
- 绘制属性画笔/荧光笔使用其现有内容颜色作为 frame 颜色；禁用预留按钮不显示，也不设置可见光影。
- 按钮 `frameCursorLightIntensityScale` 使用颜色边框第三光源强度的一半，即 `BarColorSwatchCursorLightIntensity * 0.5`。
- 按压态将 `frameLightPct` 目标降为 0.5，并复用现有按钮按压缩放矩阵；可见普通态为 1，隐藏态为 0。

## 动画与脏区

- `frameLightPct` 纳入 Shape 的统一 `ChangePct` 推进，与按钮布局/折叠动画共享现有 `operationDur`。
- 现有 `GetFrameDirtyOutset` 对 PointLight 追加 `pointLightDiffuseExtraWidth`，继续覆盖约 3px 漫反射，不另建脏区算法。
- 绘制属性四按钮的几何和光影仍共用同一按压变换；Brush2/Laser 保留布局枚举但透明度为 0。

## 风险与回滚

- 主要风险是 `pct=0` 的按钮仍需渲染独立边框光影，以及隐藏按钮不能残留光影；通过独立透明度和统一状态目标避免耦合。
- 回滚只需恢复 `Bar.UI.cppm`、`Bar.Main.cpp` 和任务文档改动。
