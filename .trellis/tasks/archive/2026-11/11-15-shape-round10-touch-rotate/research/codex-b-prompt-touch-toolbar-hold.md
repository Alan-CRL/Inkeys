## Facts

以下 `Bar.*` 文件均位于 `Inkeys/Inkeys/UI/Bar/`。

**B1：提示控件已经能处理合成鼠标消息，缺的是触摸准入和接受反馈。**

- 提示有四个动作：Back、Forward、Complete、Cancel；视觉顺序为 Back、Forward、Cancel、Complete。Complete 只在折线/多边形显示；构造接触进行中全部禁用，没有独立“关闭编辑器”按钮。`Bar.ConstructionPrompt.cpp:25–28,122–125`
- 提示在主栏的 `barDeviceContext` 绘制，成功呈现后提交命中快照。交互 worker 在普通主栏按钮之前调用 `constructionPrompt.Pointer()`。该函数处理 `WM_LBUTTONDOWN/MOVE/UP`，按下和释放必须命中同一按钮、身份必须相同才发送意图，因此本身并不排斥触摸合成消息。`Bar.RenderLoop.cpp:11915–11917,12536`; `Bar.Interaction.cpp:8502–8520`; `Bar.ConstructionPrompt.cpp:277–296`
- **实际阻断点是触摸白名单。** `PreparePresentedButtonHits()` 创建 `PresentedTouchRegions`，收录主栏、More、几何、橡皮、撤回及绘制属性面板，却未收录 construction prompt。`ProbeTouchScreen()` 只检查这张成功提交的表；provider 在 Down 的 probe 不是 `Hit::Accept` 时直接拒绝，不创建业务路由。`Bar.Button.cpp:2905–3050,2827–2867`; `Inkeys/Inkeys/Input/UiPointerInput.cpp:990–1009`
- 其他主栏按钮走：WndProc → `Pointer::Touch::HandlePointer()` → 值型 `TOUCHINPUT` → probe/deliver → `DeliverBarTouch()` → `EnqueueBarPointerSample()` → worker `Session::Begin/Accept()` → 坐标转换后的 `ExMessage`。`DeliverBarTouch()` 已将 Down/Move/Up 合成为左键消息，并携带屏幕点、设备、触点、代次和捕获票据。`Bar.Interaction.cpp:3311–3316,3228–3270,1494–1555`; `Inkeys/Inkeys/Input/UiPointerInput.cpp:1071–1115`
- PointerMode 会丢弃派生 `WM_TOUCH`，并吞掉已处理的 Pointer 接触；不能指望系统再补一个鼠标点击。LegacyTouchMode 才解码 `WM_TOUCH` 进入相同 router。`Inkeys/Inkeys/Input/UiPointerInput.cpp:1114–1135`
- 提示已使用公共 `SetBarButtonPressedVisual()` / `AdvanceBarButtonVisual()`，但调用方只消费消息，没有像相邻面板那样取得 accepted 并调用 `AcceptBarButtonFeedback()`。现有触觉凭据只为笔绑定，普通手指没有笔触觉凭据。`Bar.ConstructionPrompt.cpp:230–235,293`; `Bar.Interaction.cpp:8502–8534,640–653`; `Bar.Haptics.h:95–96`

**B2：现状不是所有 x/y 变化都动画。**

- `Impl::Advance()` 从变换后的 frameRect 四角计算屏幕 AABB，生成上下锚点、16 DIP 间隔、屏内钳制和粘性换边；两侧不足时比较主栏重叠、图形重叠、移动距离。`Bar.ShapeEditor.cpp:1229–1234,1287–1327`
- 普通几何变化直接更新位置；只有 `toolbarPlacement` 分支改变，才创建 `.4s EaseOutCubic` 残差动画。最终写入 `dockTargetX/Y` 和 `toolbarX/Y`，再生成绘制/命中 pose；`CommitPresented()` 发布成功帧。`Bar.ShapeEditor.cpp:1330–1345,1439–1444,2216–2223`
- 当前源码已经固定两行：`nextTwoRows = true`。显隐、文字宽度、行宽/高度仍按 `.4s` 成员批次动画。`Bar.ShapeEditor.cpp:1166–1228`
- 所有离散动作集中于 `Impl::Activate()`：颜色入口、边数入口、自定义取色器、取色器关闭、Finish、Tone、样式、翻转、线型、粗细、边数、约束、辅助、色块。当前编辑浮条的粗细是三个预设按钮，**没有粗细滑块拖动路径**。`Bar.ShapeEditor.cpp:2060–2121`
- 按钮由 `Pointer()` 的 `ButtonPointerState` 在 Up 激活；颜色二维取色拖动另走 `paletteGesture`，Move 更新本地反馈，Up 仅提交一次 `SetStyle`。键盘 Enter/Space 也调用 `Activate()`。`Bar.ShapeEditor.cpp:2334–2350,2384–2409,2600–2603`
- 用户提到的三秒机制在主栏撤回/重做 chrome：`RecallChromeIdleSeconds = 3.0`，锁存布局及命令身份，每次点击递增 reset serial；已有保护期内的无效连点也续期。使用 `steady_clock` deadline，原生 timer 到期请求渲染，再由渲染前结算布局，**不累计动画 dt**。`Bar.RecallChrome.cppm:7–8,77–103,119–136`; `Bar.Button.cpp:1840–1861,1928–1943`; `Bar.Interaction.cpp:3385–3394,3643–3647`; `Bar.RenderLoop.cpp:13239–13241`

## Hypotheses

- 提示未覆盖主栏的部分会在触摸 Down 准入时被拒绝，这是症状的直接源码解释；压入主栏的重叠部分可能借主栏区域被接纳，所以“触摸永远到不了提示”并非所有坐标都成立。第三光源已经收录提示区域，但那张表不参与 touch probe。`Bar.Button.cpp:2853–2863,3000–3005`; `Bar.ConstructionPrompt.cpp:178–181`; `Bar.Interaction.cpp:9043–9046`
- 三秒需求应复用撤回 chrome 的**墙钟和布局锁存语义**，不直接复用其 HWND timer：编辑器已有独立 surface tick/deadline 接线。`Bar.RecallChrome.cppm:41–50`; `Inkeys/Inkeys/UI/SelectionOverlay/SelectionOverlay.cpp:128–146`

## Needs runtime test

本次只读检查，未修改文件，未运行 git、构建、程序或测试。

- **B1 回归：**在 `Bar.GeometryAttribute.Test.cpp:1669–1838` 的提示 fixture 中，对每个可用动作检查实际成功呈现后的 `ProbeTouchScreen()`；复用同文件 `1060–1078` 的屏幕映射检查，特意选择主栏之外的按钮中心。覆盖 folded、窄屏、DPI、初次呈现失败、禁用按钮和退场阻挡。
- 在 `RunSharedInteractionWorkerTests()` 扩展真实 mailbox 的 Touch Down/Up、Cancel、迟到 Up 和笔凭据测试，让输入实际调用提示分发，验证一次意图、标准按压/释放、仅成功动作一次触觉。现有接线测试位于 `Bar.Interaction.cpp:2323,2718–2817`。
- **B2 回归：**在 `RunShapeEditorOffscreenTest()` 注入时钟，验证 2999/3000ms、重复操作续期、长按超过三秒、取色拖动及取消、相同停靠分支也平滑恢复、Finish/换对象清理、呈现失败不提前改变命中。扩展现有布局显隐与换边 fixture。`Bar.ShapeEditor.cpp:2609,3880–3906,4110–4139`
- 真机验证 PointerMode、LegacyTouchMode、手指/笔/鼠标的点击和按压观感；项目合同明确离屏测试不能代替实体输入验收。`.trellis/spec/native-desktop/ui-pointer-session.md:90–96`

## Recommendation

**B1 最小修改：**

1. 在 `Bar.Main.cppm:484–513` 为 owner 提供候选提示 frame 的只读访问，例如增加 `friend class BarUISetClass`。
2. 在 `Bar.Button.cpp:2907–2934` 的白名单构造开头加入提示 `visualPanel`，使用 **Rigid** 空间：visible 且 input 时 Accept；可见但不可交互时 Blocked。必须取同帧候选，沿既有成功呈现事务发布，避免使用落后一帧的 `PresentedRegions()`。现有发布点为 `Bar.RenderLoop.cpp:12323,12525–12536`。
3. 扩展 `BarConstructionPrompt::Pointer()` 声明/实现，返回独立 accepted 标志；在 `293` 行保留 `SubmitShapeUiIntent()` 的 bool。`Bar.Interaction.cpp:8502–8503` 仿照紧邻面板调用 `AcceptBarButtonFeedback()` 并清理下层 hover。按压动画无需重写。`Bar.Main.cppm:492`; `Bar.ConstructionPrompt.cpp:234–235,280–296`; `Bar.Interaction.cpp:8504–8520`

**B2 最小设计：**

- 在 `Impl` 增加受 `inputMutex` 保护的会话身份、最后操作/deadline、冻结屏幕 DIP 锚点和 pending-position 标志。首次有效按钮 Down 冻结**成功呈现位置**并停止既有位移残差；Up/`Activate()`、子层操作和取色 Move/Up 续期。活动按钮/取色手势期间始终冻结，结束后再满三秒；hover 不续期。修改入口为 `Bar.ShapeEditor.cpp:637–648,2264–2409,2060–2077`。
- 在 `Advance():1311–1339` 将“计算期望位置”与“应用位置/换边状态”分开。保护期只更新 pending；到期从冻结位置向最新目标启动现有残差动画，**即使 placement 未变也要启动**，随后继续现有实时几何跟随。
- 新增 `TickToolbarIdle(now)`，在 `Bar.Scene.cppm:247` 附近声明；由 `SelectionOverlay::Tick():128–146` 合并 deadline，到期一次 `Request(Client::SelectionOverlay)`。worker 已按 surface 最早 deadline 等待，无需三秒持续渲染。`Bar.Interaction.cpp:1663–1691,8726`
- 边界策略：尺寸/按钮显隐继续动画，但保持左上锚点；屏幕/DPI变化只做必要直接钳制，普通图形重叠延至到期；图形拖动不续期，严格保护期内仍冻结浮条。Finish 立即退出；新 session/object/page/display 清除旧保护，普通 revision 更新不清除。清理放在现有 `Configure()` 及退出路径。此需求需明确替代现有“普通几何实时跟随”合同。`Bar.ShapeEditor.cpp:1006–1034,1217–1227,1659–1672`; `.trellis/spec/native-desktop/rendering-and-ui.md:2988`