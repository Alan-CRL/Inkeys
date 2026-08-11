# 技术设计

## 边界与状态

- 改动集中在 `Bar.Main.cpp` 和 `Bar.Main.cppm`。新增内部浮窗 Surface、墨迹圆 Shape 和数值 Word 枚举，不修改公共接口、`Bar.State.cppm`、配置或资源。
- 浮窗目标由当前帧已有的 `thicknessSliderThumbVisible` 派生，不新增跨线程原子状态。Hold-lock 不再参与该可见条件，只冻结已有候选值；真实 Pointer Up 后仍由 `CloseThicknessSlider()` 的现有路径关闭浮窗。
- 新增渲染线程局部动画值：浮窗展开进度、数字移入圆内进度和 Hold 控制行交换进度。浮窗 X 不维护独立动画状态，直接读取 Thumb 当前帧最终布局中心。

## 几何与数据流

- 继续使用 `CalculateBarThicknessPreviewGeometry()` 和 `ResolveThicknessSliderCenterY()` 获取 Thumb 中心、面板比例与连续 `previewSide`，不直接按目标 `primaryBar` 分支。
- 圆形输入为 `drawAttributePenThickness.val`。逻辑直径使用 `physicalWidth / barStyle.zoom`，由 Shape 渲染重新乘 `zoom` 后得到真实设备像素直径；拖动时上游已对候选值 `SetDirect`，其余变化保持现有动画。
- DWrite 每帧只测量当前 1–3 位整数内容并复用格式缓存。可容纳条件为圆直径至少覆盖实测文字宽高及圆内安全边距；条件变化驱动 0.18 秒 EaseInOut 迁移进度。
- 外置态为“圆 + 5 DIP + 数字”，圆内态为数字与圆同心；两套目标位置、颜色和 Surface 尺寸按迁移进度插值。Surface 使用 8 DIP 四周内边距，并始终包住当前动画几何。
- 稳态目标 Y 直接由当前 Thumb 中心、Thumb 半径、10 DIP 间距和浮窗半高推导，允许浮窗局部覆盖 DrawAttributeBar；展开进度从 Thumb 中心插值到目标矩形并等比增长，使用 0.40 秒 EaseOutBack / EaseInBack。
- Thumb 先由 `drawAttributeThicknessSliderNormalized.val` 计算当帧布局，再以 `sliderThumb->Inherit(TopLeft, panel)` 得到最终视觉中心 `anchorX`；浮窗直接以该中心作为 desired X，因此拖动、离散变化与快速重定向都共享 Thumb 的同一动画过程。安全右界取笔类型按钮当前动画几何转换到窗口局部坐标后的最左边；`anchorX` 先 clamp，最终再按 Popup 回弹比例反解中心上限，使 Surface、圆和数字共用的实际右边缘当帧也不越界。
- direct-touch Preview 继续以按下屏幕 X 为零原点，超过既有 5 DIP 阈值后按 `deltaX / (3 * trackTravelScreenX)` 映射相对粗细；该倍率只用于 Preview 相对映射，不影响鼠标 Slider、Slider 已显示时的触摸、取整或 clamp。超过阈值并有效改值后允许进入同一 Hold-lock 状态机；无消息帧用最后触点推进静止计时，不读取系统鼠标光标。
- 精细区不新增 Shape 或共享状态：从当前 Thumb、控制行和 `previewSide` 推导外侧矩形。近边到 Thumb 的间距实时复用 Thumb 内缘到控制行的间距，远边取当前 Preview 外侧边界；水平取完整 `trackLeft` 到 `trackRight`，Overflow Badge 可见时将右边截到按钮左边，Preview Popup 完全忽略。
- 输入线程只锁存本次按下是否命中精细区。该分支不执行轨道绝对投影，任何水平位移后复用 `deltaX / (3 * trackTravelScreenX)` 相对投影，并继续走原有 candidate、pressed/dragging、Hold-lock、capture、commit 和 release 清理；`WM_TOUCH` 继续合成带来源标记的消息，窗口回调只丢弃其重复兼容鼠标消息，笔生成的兼容鼠标消息则与普通鼠标共同进入现有 ExMessage 路径。

## 渲染顺序、光影与脏区

- 从现有 Preview 绘制块中拆出 Thumb 的自定义 D2D 绘制，先结束 Preview clip，再绘制普通属性内容、浮窗 Surface/圆/数值，最后绘制 Thumb。
- Surface 复用 `BarUiShapeClass` PointLight：`framePrimaryLightEnabled=false`，第三光强度沿用按钮/提示 Surface 参数；动画期间设置完整几何归一比例，避免终点创建 diffuse mask。
- 白色圆形不使用边框或光源；数值外置时使用 TextPrimary，圆内使用白色背景的可读色。
- 在 BeginDraw 前把 Surface、圆、数值和 Thumb 的当前及预测边界加入 dirty union。最终绘制继续使用单个主 `BeginDraw/EndDraw`、现有帧租约和设备 epoch 资源重建。

## Hold 控制行交换

- `holdExchangeProgress` 由 `thicknessSliderHoldHintActive || thicknessSliderHoldLocked` 直接驱动，在 0.12 秒内使用 EaseOutCubic / EaseInCubic 完成一次可逆透明度交换，不再设置中点屏障或等待子控件动画。
- 交换中的 Shape、Word、SVG 和光影透明度由该进度当帧直接决定，避免子控件 `SetTar` 对逐帧变化目标反复重启。提示进入时文字和圆环立即开始可见；两者共用 `holdGroupScale`，显示以 0.82 -> 1.0 的 `EaseOutBack` 回弹，隐藏以当前值 -> 0.82 的短促 `EaseInBack` 退场，目标反转时只调用 `SetTar`，不重置当前值。左侧 ThicknessDisplay 不乘该进度。
- `holdRingLockOpacity` 只负责锁定后圆环淡出，并在整组完全隐藏后复位；普通提示显隐不再叠乘独立 Ring fade。圆弧长度仍读取 `thicknessSliderHoldProgress`，与组件 visibility progress 分离。
- 文字右边缘对齐 `BarDrawAttributeThicknessDividerRight`，圆环位于文字左侧并保留 2 DIP 间距；锁定后的环/文字颜色仍由现有 Hold 动画值控制。

## 兼容与回滚

- Hold-lock 与 interaction finished 保持解耦：Slider 与 direct-touch Preview 的候选值更新统一经过 `ApplyCandidateWidth()` 锁定门；锁定后的 Pointer Up 最终投影也不能覆盖冻结值，直至真实 Pointer Up 只提交一次并进入现有 release/cleanup。
- 不改变 Slider 候选提交、触摸分流、Hold-lock 计时、Pointer capture/release 或 Tooltip 命中逻辑。
- 不改变直接触摸 Preview 的 5 DIP 二维分类阈值、按下点零原点、3 倍精度、Thumb/Preview/Hold 动画、Preview X、安全边界与提示控制行视觉；触摸拖动和锁定期间始终保持 Preview 模式。
- 回滚只涉及两个 UI3 Bar 源文件及本任务资料，无数据迁移和资源清理。
