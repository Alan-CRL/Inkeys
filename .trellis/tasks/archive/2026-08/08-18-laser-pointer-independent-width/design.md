# 笔型粗细与 Bar 动画技术设计

## 1. 状态与 Draw3 工具合同

`StateModeClass::Pen` 保留三类宽度所有权：SoftPen/HardPen 共用 `Brush1`，Highlighter 使用 `Highlighter1`，Laser 使用独立 `Laser.width` 和三档 DIP 预设。`laserActive` 的优先级高于残留的 `Pen.ModeSelect`，因此状态读写和 Draw3 工具映射都先判断 Laser。

产品状态通过 `PublishDraw3State` 原子发布 tool、颜色和 `widthDip`。Draw3 Host 将 `Bridge::Tool` 映射到 `DrawingTool`，绘制线程在 Down 时锁存产品视觉样式，保证活动笔划不被后续 UI 修改重定向。

宽度模式固定如下：SoftPen 使用设备宽度策略；HardPen 始终 `Fixed`；Highlighter 固定使用用户选择的基准宽度，但几何逐点读取真实半径；Laser 的 Pen 使用 `LaserPressure`，Mouse/Touch 使用 `SimulatedPressure`。HardPen 的 live-tip protection/taper 都为零，prediction 仍正常运行。

Laser 的 DIP 仅在 Draw3 canvas 边界乘一次 DPI；实际笔迹和 Hover 光标共用 `CanvasDiameterForTool`。Highlighter 的矩形光标、笔迹几何和 Bar 候选宽度共用当前产品宽度。

## 2. 粗细候选与提交合同

普通笔和 Highlighter 可进入 Slider/FineDial；Laser 只使用三档快捷按钮。直接触摸 Preview 时先跨过 5 DIP slop，再进入 `thicknessPreviewDragging`。

拖动过程只更新 `thicknessSliderCandidateWidth`。RenderLoop 在候选活动时直接读取该浮点值并更新预览和数字，使 Move 连续跟手；交互结束且手势有效时才调用一次 `SetPenWidth`。取消、工具切换和捕获丢失必须清理候选状态，不提交真实值。

## 3. 主预览状态机

主预览分为两个相互解耦的维度：

- semantic 层负责 SoftPen 曲线、HardPen 直线和 Highlighter 圆角矩形之间的形状 morph，始终参与绘制。
- Laser overlay 负责红色外壳、白色内芯及二者宽度。外壳进度和 core/outer 宽度分别持有 current/target，进入、离开和中途反向均从当前值续接。

Laser overlay 最后绘制，红壳和白芯共享 semantic 路径与动画进度。目标外宽使用真实 Laser 视觉宽度，内芯为外宽三分之一；离开 Laser 时先保持连续几何，再由状态值收敛到目标笔型，禁止按工具枚举直接跳宽。

## 4. 快捷按钮状态机

快捷按钮视觉只有 `Circle` 与 `Number` 两类。单一 `drawAttributeThicknessPresetNumberProgress` 驱动两阶段透明度：前半段淡出旧视觉，中点两者均不可见，后半段淡入新视觉。

发生类型切换时锁存 outgoing 内容：Circle -> Number 不重定向旧圆直径，Number -> Circle 不改写淡出的旧数字。Circle -> Circle 则允许三个直径从当前值连续 morph。面板首次按 Highlighter 打开时直接建立稳定 Number 状态，不播放虚假的 Circle -> Number。

## 5. 笔型扩展与按钮交互

SoftPen、HardPen、Highlighter 支持扩展入口；Laser 不支持。命中资格读取目标工具状态并立即失效，视觉进度独立保留，使失去资格后的三角和分割线平滑退场。扩展锚点取选中按钮当前 `x/y`，颜色取其当前 `frame`，避免首帧从 Accent 或旧位置跳入。

Laser 按钮使用与其他笔型相同的独立 Hover stage 和 press scale。选中态背景只由当前工具决定；离开 Laser 时目标颜色、透明度和缩放均回到普通未选中状态。

## 6. 验证策略

- 静态审查 `IdtState -> Bridge -> Host -> DrawingController -> StrokeGeometry/PenCursor` 的 tool、width、pressure 和 cursor 数据流。
- 静态审查 Bar Layout/Interaction/RenderLoop 的资格门、候选提交边界和 current/target 状态调用点。
- `InkeysHeadlessTests --no-window` 覆盖 preview morph 端点/反向、Laser shell 反向、Preset 锁存和两阶段透明度、Circle morph、Highlighter 初始化及扩展资格/锚点/颜色/退场。
- 使用 ARM64 MSBuild 对 `InkeysRepo.sln` 执行 Debug/Release 完整构建，不单独构建 Inkeys 项目。
