# 技术设计

## 边界与状态

- 改动集中在 `Bar.Main.cpp` 和 `Bar.Main.cppm`。新增内部浮窗 Surface、墨迹圆 Shape 和数值 Word 枚举，不修改公共接口、`Bar.State.cppm`、配置或资源。
- 浮窗目标由当前帧已有的 `thicknessSliderThumbVisible` 派生，不新增跨线程原子状态。`CloseThicknessSlider()` 的所有现有调用自然关闭浮窗。
- 新增渲染线程局部动画值：浮窗展开进度、数字移入圆内进度、Hold 控制行交换进度和水平避让偏移。所有值进入统一动画时钟并支持反转。

## 几何与数据流

- 继续使用 `CalculateBarThicknessPreviewGeometry()` 和 `ResolveThicknessSliderCenterY()` 获取 Thumb 中心、面板比例与连续 `previewSide`，不直接按目标 `primaryBar` 分支。
- 圆形输入为 `drawAttributePenThickness.val`。逻辑直径使用 `physicalWidth / barStyle.zoom`，由 Shape 渲染重新乘 `zoom` 后得到真实设备像素直径；拖动时上游已对候选值 `SetDirect`，其余变化保持现有动画。
- DWrite 每帧只测量当前 1–3 位整数内容并复用格式缓存。可容纳条件为圆直径至少覆盖实测文字宽高及圆内安全边距；条件变化驱动 0.18 秒 EaseInOut 迁移进度。
- 外置态为“圆 + 5 DIP + 数字”，圆内态为数字与圆同心；两套目标位置、颜色和 Surface 尺寸按迁移进度插值。Surface 使用 8 DIP 四周内边距，并始终包住当前动画几何。
- 稳态目标 Y 位于当前 DrawAttributeBar 外缘的 `previewSide` 一侧，间隔 5 DIP；展开进度从 Thumb 中心插值到目标矩形并等比增长，使用 0.40 秒 EaseOutBack / EaseInBack。
- 默认目标中心 X 等于当前 Thumb 中心。以两个笔类型按钮、扩展分割线/三角的当前动画联合边界作为排斥区，测试从 Thumb 锚点到目标矩形的扫掠包围盒；相交时选择绝对位移更小的一侧并保留 5 DIP 间距。避让偏移单独动画，未冲突时回到 0；不读取窗口或监视器边界。

## 渲染顺序、光影与脏区

- 从现有 Preview 绘制块中拆出 Thumb 的自定义 D2D 绘制，先结束 Preview clip，再绘制普通属性内容、浮窗 Surface/圆/数值，最后绘制 Thumb。
- Surface 复用 `BarUiShapeClass` PointLight：`framePrimaryLightEnabled=false`，第三光强度沿用按钮/提示 Surface 参数；动画期间设置完整几何归一比例，避免终点创建 diffuse mask。
- 白色圆形不使用边框或光源；数值外置时使用 TextPrimary，圆内使用白色背景的可读色。
- 在 BeginDraw 前把 Surface、圆、数值和 Thumb 的当前及预测边界加入 dirty union。最终绘制继续使用单个主 `BeginDraw/EndDraw`、现有帧租约和设备 epoch 资源重建。

## Hold 控制行交换

- `holdExchangeProgress` 由 `thicknessSliderHoldHintActive || thicknessSliderHoldLocked` 驱动。前半段只降低三个预设按钮、小三角及其数字/SVG 的透明度，后半段才提高提示文字/圆环透明度。
- 反向时提示先退场，进度进入前半段后四个控制才恢复。左侧 ThicknessDisplay 不乘该进度。
- 提示组宽度使用已测文字宽度、2 DIP 环间距和现有环尺寸，右边缘对齐 `BarDrawAttributeThicknessDividerRight`；锁定后的环/文字颜色仍由现有 Hold 动画值控制。

## 兼容与回滚

- 不改变 Slider 显示、候选提交、触摸分流、Hold-lock 计时或 Tooltip 命中逻辑。
- 回滚只涉及两个 UI3 Bar 源文件及本任务资料，无数据迁移和资源清理。
