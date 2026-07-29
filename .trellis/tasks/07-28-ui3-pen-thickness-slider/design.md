# 技术设计

## 边界与状态

- 改动集中在 `Bar.Main.cpp/.cppm` 与 `Bar.State.cppm`，继续通过 `GetPenWidth()` / `SetPenWidth()` 访问当前笔型粗细；不新增公开 API、配置字段或资源文件。
- 在 `drawAttributeBar` 状态中加入 `thicknessSliderHover`、`thicknessSliderPinned`、`thicknessSliderDragging` 和按压视觉状态；有效显示条件为支持笔型、属性栏可交互且 `hover || pinned || dragging`。
- 新增可逆的 `drawAttributeThicknessSliderProgress` 动画值表示普通预览到滑块态的连续进度；固定状态只控制小三角选中视觉，不直接决定几何进度。
- 关闭绘制属性、收起主栏或离开支持笔型时统一清除悬停、固定、拖动与捕获状态，并调用既有 `CloseDrawAttributeTooltips()`。

## 几何、渲染与动画

- 继续从动画中的粗细外框和控制行几何推导预览区域、上下方向、水平内边距与中心，不直接读取已经切换的上下方向布尔值。
- 滑轨中心行程使用既有预览左右端点，并向内收一个圆点半径，保证端点时圆点完整位于粗细框内；相同端点同时用于数值映射和命中。
- 轨道目标高度为 `4 × barStyle.zoom × panelAnimationScale` 设备像素，圆角为高度一半；目标色为主题 Accent，并按 Rest/Hover/Pressed 平滑过渡到 `100%/90%/80%` 强度。
- 硬笔阶段把 Bezier 振幅乘以 `(1-sliderProgress)`，并把实际预览粗细连续插值到轨道高度；荧光笔把左端透明度从 `0.35` 插值到 `1.0`、圆角插值到半高，同时把粗细插值到同一轨道高度。两种笔型在 `sliderProgress=1` 时得到完全一致的轨道几何。
- 滑轨目标纵坐标从当前动画中的粗细文字边缘、标注线徽标边缘及两侧间隙推导，上下换边时连续镜像。进入时先把 `sliderProgress` 推到 `1`，到达后才启动圆点透明度和 `0.75→1.0` 回弹；退出时先隐藏圆点，透明度到 `0` 后才把 `sliderProgress` 恢复到 `0`。
- 轨道进入/退出使用默认 `0.4s`、可打断的 cubic 曲线；圆点退出使用平滑曲线。外径保持 `20px` 逻辑尺寸，内圆按 WinUI 比例在 Rest/Hover/Pressed 的 `12px/14px/10px` 间动画。
- 圆点由主题派生灰色圆底、细灰外缘和 Accent 内圆构成；深色圆底按 WinUI 的 `#454545` 观感由 Surface 向 TextPrimary 混合得到，浅色继续使用 Surface。使用现有 D2D 画刷缓存，不在稳定帧创建资源。圆点、轨道及其旧/新边界加入同一 predicted/current 脏区并集。
- 固定小三角复用预设按钮的选中填充、青色内容、`frameLightPct` 和 PointLight 强度；临时 hover 不设置选中目标。

## 范围与绝对映射

- 文件内统一提供笔型范围函数：硬笔 `[1, round(30 × dpiZoom)]`，荧光笔 `[round(30 × dpiZoom), round(100 × dpiZoom)]`，并确保 `max >= min`。
- 非拖动时圆点位置使用 `clamp((GetPenWidth()-min)/(max-min), 0, 1)`，拖动时把分子替换为原子候选值；范围外值只影响圆点显示钳制，不调用 `SetPenWidth()`。
- 按下时记录屏幕坐标 `pressScreenX`、起始粗细、起始笔型，以及圆点中心可达的滑轨屏幕左右端点。未发生水平移动时保留起始候选值，只按点击固定处理。
- 首次发生水平移动后，拖动值为 `min + (screenX-trackStartScreenX) / trackTravelScreenX × (max-min)`，随后钳制并四舍五入到整数设备像素；圆点直接跟随指针/触点在滑轨上的绝对投影。
- 拖动中把整数候选值写入 `IdtAtomic<float>`，渲染线程只用它计算圆点归一化位置，不调用 `SetPenWidth()`，也不让候选值进入文字、预设按钮或超限判断。正常抬起且笔型未变化时，若候选值不同于起始值，只调用一次 `SetPenWidth(finalValue, true)`。

## 输入捕获与命中优先级

- 预览区域使用从当前动画几何派生的独立命中矩形，排除控制行；正常和倒转布局共用同一推导，不新增固定坐标分支。
- 指针移动先按视觉层级命中标注线和超限徽标；徽标、已显示浮窗正文及其 `100ms` 离开宽限期都维持滑块 hover，浮窗正文消费点击避免穿透。进入对应浮窗会取消待执行的宽限定时器；滑块刚激活时关闭旧浮窗，但后续徽标仍可重新打开提示。
- 预览区 `WM_LBUTTONDOWN` 只记录按压前是否已经固定，并开始候选拖动。若后续屏幕横坐标改变，则进入 dragging 且未固定手势不选中小三角；直接抬起才按普通点击固定。
- 小三角按下时锁存原固定态；原本已固定时先进入收起目标，确认点击后保持取消固定，拖出取消时恢复原固定态，避免预览单击固定后紧接着点击三角又被反向选中。
- 鼠标候选拖动开始时对 `floating_window` 调用 `SetCapture`；用局部 RAII/统一清理函数保证正常抬起、模式切换、属性栏收起、`WM_CAPTURECHANGED` 和线程退出路径都调用 `ReleaseCapture` 并清除按压状态。
- WndProc 在本任务拖动激活时把捕获丢失转成一次可消费的结束事件，避免 `hiex::getmessage_win32` 永久等待；结束坐标使用 `GetCursorPos` 后转客户区。
- 现有 `WM_TOUCH` active touch id 继续提供同一接触序列的屏幕坐标；触摸和鼠标最终进入同一个绝对投影函数。手工验证触点离开 Bar 命中窗口后仍收到 MOVE/UP；若平台不继续投递，则在不改变多点锁定规则的前提下补充等价捕获结束信号。
- 滑块激活时先关闭已有提示浮窗，但徽标继续保持绘制和顶层命中；之后从徽标重新打开的浮窗正文及其离开宽限继续参与滑块 hover。本任务不改徽标资源和布局。

## 兼容、性能与回滚

- 不改变 `SetPenWidth`、粗细记忆或绘制引擎接口；本滑块拖动期间只维护 UI 候选值，正常抬起后才一次性写入真实粗细和记忆。
- 小三角 SVG 使用 UI3 现有可着色标记色，继续由 `BarUiColorClass::SetTar()` 在主题文字色与 Accent 间动画，不硬编码最终白色或青色。
- 维持 UI3 单帧租约、单组 `BeginDraw/EndDraw`、设备 epoch 重建和 layered-window 脏区约定；稳定滑块帧不得新增 D2D 资源创建。
- 保持源文件原编码、CRLF 与关键路径中文注释。回滚只需恢复三个 UI3 Bar 源文件和本任务文档，不涉及数据迁移。
