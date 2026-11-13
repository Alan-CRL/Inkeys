# 2026-11-13 续轮调研：浮条、属性控件、字体与提示

## 范围与证据

只读检查 `feature/shape` 当前源码，未改产品文件、未运行会产出截图或日志的测试。编号对应父任务 PRD 的 C1/C2/C3/C8/C12/C13/C14。

## C1 浮条跟手与换边

- 本地候选已经在每次 Move 经 `EditSession::Preview` 更新，并重建 handles、`frameRect` 和 bounds：`Inkeys/Inkeys/UI/Bar/Bar.ShapeEditor.cpp:705-719`；`Configure` 合并本地与权威快照：同文件 `:855-870`。
- 当前浮条锚点使用对象、控制点和外框的联合包络，而不是 `frameRect` 顶/底边中心：同文件 `:1039-1057`。
- 当前每帧按空间在上下侧重新选择，没有换边粘性；主栏重叠还会二次改 Y：同文件 `:1056-1069`。
- 旧代码明确在 tracking 期间冻结浮条、抬手后 0.4 秒追赶：同文件 `:1070-1074`。这是 C1 的直接原因。
- 回弹锚点固定使用底边，弹层方向则由浮条动画位置反推：同文件 `:1075-1103`。

最小边界：只改 `ShapeEditorSurface::Impl::Advance` 的放置策略。新编辑会话默认 below；锁存当前侧，仅当前侧空间不足才换到另一侧，不因原侧重新有空间而自动返回；两侧不足时保持当前侧并复用 `FitEditorPose`（同文件 `:66-82`）保证屏内稳定。tracking 时锚点直接跟随本地候选，不做抬手追赶。回弹和弹层消费同一放置侧。主栏遮挡沿用既有“空间不足”语义。

测试：更新同文件 `:3254-3262` 与 `:3299` 的旧冻结/追赶断言；在 `:2989-3060` 的四角/小工作区矩阵增加换边粘性、双边不足和旋转对象；逐 Move 断言浮条与变换后的 frameRect 边中心相对偏移恒定。

## C2 外框只隐藏、不删除

- 共享核心始终计算带 8 DIP 间隙的 `frameRect`，同时服务旋转连接线和立体角缩放：`inkStrokeModelerTest/draw3/shape_interaction.inl:131-224`。
- 权威与本地快照均发布 frameRect：`Inkeys/Inkeys/Drawing/Draw3/Draw3.ShapeRuntimeActions.inl:387-394`、`Bar.ShapeEditor.cpp:713-719`。
- UI 在 `Bar.ShapeEditor.cpp:1536-1543` 仅按 `frameRect.valid` 画框；同一块 `:1544-1558` 还包含旋转连接线，不能整体隐藏。

最小边界：增加纯 UI 可见性谓词，只包围圆角框绘制；frameRect、旋转连接线、立体角标、命中、窗口 bounds 和浮条锚点保持不变。保留 `:2360` 与 `:2594-2607` 的核心几何测试，新增实际像素负例。

待用户决定：“直线、箭头、矩形等”的完整可见性清单，不能自行把“等”扩展为所有平面图形。

## C3 专属符号文字

- 主栏已有 Center/Foci/Diagonals 文案映射：`Bar.GeometryAttribute.cpp:66-81`，并在 hover/keyboard focus 时显示 hint：同文件 `:697-714`、`:825-829`。
- 选中浮条只有图标：`Bar.ShapeEditor.cpp:352-361`；OneOne 默认没有文字槽，编辑器尚无等价 hint。

若采用 hover/focus 说明，可提取共享标签 helper，在编辑器复用 `MeasureText` 与 `FitEditorPose`，不改按钮宽度。若要求常驻文字，则须改 `ToolbarItems` 布局、按钮尺寸和整体浮条宽度。需用户确认是哪一种。

## C8 辅助点上控制柄半透明

- 所有非 Scale 控制点目前共用 Thumb opacity：`Bar.ShapeEditor.cpp:1603-1615`。
- `DrawBarSliderThumb` 已支持整体 `compositeOpacity`：`Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp:3482-3504`。
- 44 DIP 输入覆盖单独绘制：`Bar.ShapeEditor.cpp:331-349`，视觉半透明不影响热区。

最小边界：只对下面确有辅助元素的 Center/Focus 使用额外视觉透明度，以 `SupportedAuxiliary(preset)` 限定范围；普通 Vertex/Radius/Rotate 不变。默认建议整枚 Thumb 使用 0.5 倍透明度，在最终规划中交由用户验收，不改共享 helper 默认样式。

## C12 只留边框/填充

- 浮条三项仍位于 `ToolbarItems`：`Bar.ShapeEditor.cpp:51-53`；点击三态映射位于 `:1692-1696`。
- 浮条辅助开关只看 `SupportedAuxiliary`，未看 showStroke：同文件 `:947-967`。
- 主栏 `AttributeItems` 同样含三项：`Bar.GeometryAttribute.cpp:32-33`；点击可写 OutlineAndFill：`:877`。
- 主栏参数行高度和辅助布局未随 Fill 收缩：同文件 `:345`、`:485-500`、`:553-557`。
- 删除第三项后主栏 Divider placement 下标从 3/8 变为 2/7：同文件 `:680-693`。
- `FillMode::OutlineAndFill` 是共享模型与兼容数据：`shape_core.h:36`，只移除 UI 入口，不删枚举。
- 核心当前支持无描边时以填充色绘制辅助元素：`shape_core.cpp:526-543`，已有测试 `inkStrokeModelerTestTests/shape_core_tests.cpp:66-72`。

最小边界：主栏和浮条同时移除第三个入口，辅助 wanted 与 showStroke 联动；只有辅助项的参数行在 Fill 下撤掉，构造方式/边数行仍保留。旧混合对象不静默改文档，需在两项选中态中定义兼容展示。需用户确认 Fill 下是只隐藏开关，还是连已持久化的辅助视觉也隐藏。

测试更新：`Bar.GeometryAttribute.Test.cpp:417-420`、`:762-772`、`:870`、`:899-901`；`Bar.ShapeEditor.cpp:3116-3127`。保留旧 OutlineAndFill 兼容夹具。

## C13 首次展开字体增大

- 主流程同步初始化字体集合，之后启动 Bar：`Inkeys/IdtMain.cpp:3636-3654`、`:4163-4166`。未发现字体异步就绪通知；`RefreshLocalizedTypography` 只响应语言 revision：`Bar.Rendering.cpp:291-310`。
- 形状类别在 Advance 中完成真实测字：`Bar.GeometryAttribute.cpp:581-595`。
- 但 `:629-646` 把当前回弹倍率同时写进当前字号和 `size.tar`；未设 `fontTargetSize` 时，`ResolveWordFontSize` 使用 size.tar 作为静止目标：`Bar.Rendering.cpp:254-261`，导致字体修正继续追赶。
- More 已有按 `morePanelProgress.tar` 设置最终目标的修复：`Bar.RenderLoop.cpp:4522-4524`、`:4664-4669`。
- 可复用 `BarAttributePanelFrame::targetPose`、`BarUiWordClass::fontTargetSize` 以及 `Bar.EraserAttribute.cpp:301-315` / `Bar.RecallAttribute.cpp:259-264` 的模式。

最小边界：形状类别、分段和构造方式文字都设置最终 pose 下的 fontTargetSize，保留长标签既有缩字。More 先通过完整 offscreen RenderLoop 冷启动展开验证，不凭独立按钮测试重复修改。测试覆盖动画开启的首次展开、回弹过冲、语言、DPI 和中途反向；通用字体算法测试位于 `InkeysHeadlessTests/animation_tests.cpp:105-162`。

## C14 完成时进度环走满

- 进行中公式为 `(step-1)/totalSteps`：`Bar.ConstructionPrompt.cpp:189-195`，故第 1/2 步为 0，第 2/2 步为 0.5。不能改成 step/total，否则进入第二步就提前走满。
- 第二次 Up 后 Complete/Cancel 与 runtime reset 紧挨着发生：`Draw3.DrawingController.cpp:2994-3008`、`shape_interaction.inl:632-643`、`Draw3.ShapeRuntimeActions.inl:490-496`，通常没有稳定 active+finished 快照。
- 关闭绘制已经保留旧环：`Bar.ConstructionPrompt.cpp:176-179`。

优先采用显式完成沿：在 ShapeUiSnapshot 协议区分 completed 与 cancel/tool-switch，关闭帧将 ringProgress 重定向到 1，与 0.4 秒收缩并行。仅推断“出现同 objectId 新对象”是最小 UI 方案，但跨页/失败/取消边界较弱。测试保留 `Bar.GeometryAttribute.Test.cpp:1321-1327` 的进行中进度，新增完成、取消、工具切换和跨会话正负例。

## 相关既有测试入口

- `--ui3-shape-editor-test`
- `--ui3-geometry-attribute-test`
- `--shape-tool-state-test`
- `--state-mode-production-test`
- `InkeysHeadlessTests --no-window`
- `Scripts/i18n.ps1 check`

## 2026-11-13 用户决策：外框可见性

C2 仅直线、箭头、矩形隐藏编辑外框，不扩展到其他平面图形。其他平面及立体图形保持现有外框显示；frameRect、旋转连接线、命中与编辑语义不变。

## 2026-11-13 用户决策：专属符号文字

C3 采用图标旁常驻文字，不用悬停/聚焦 hint 代替。主栏参数行和选中浮条都直接显示「圆心」「焦点」「对角线」等说明；浮条变宽后的小屏适配沿用现有缩放/换行策略。

## 2026-11-13 用户决策：纯填充辅助显示

C12 纯填充模式同时隐藏辅助开关和画布上的辅助图形，但保留内部辅助开关状态，切回边框后恢复。旧「边框+填充」对象不静默改写；两项 UI 可同时处于选中态以准确表示旧混合状态，用户点击某项后才转换成对应单一模式。
