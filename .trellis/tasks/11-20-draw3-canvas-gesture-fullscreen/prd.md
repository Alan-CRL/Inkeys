# Draw3 画布手势屏蔽与全屏窗口

## 背景（2026-11-20 讨论结论，均已对照代码核实）

- 画布窗口（Drawpad）目前靠三样东西屏蔽手势：
  - `DisableEdgeGestures`：`System.EdgeGesture.DisableTouchWhenFullscreen`，在 `IdtMain.cpp` 的 created 回调里设置。
  - `MicrosoftTabletPenServiceProperty`：在 `Draw3.WindowControl.cpp` 附着时设置。
  - `WM_TABLET_QUERYSYSTEMGESTURESTATUS` 的返回值。
- 后两样属于 Wisptis 旧机制，管不到 Win8 起 Pointer 栈的触摸反馈。全工程没有调用 `SetWindowFeedbackSetting`。用户实测：WM_POINTER 下仍有按下圈和移动拖尾，RTS 下没有。
- 边缘手势属性只在系统认定窗口全屏时生效。桌面和 PPT 下，`CanvasBottomExclusion` 挖掉屏幕最后一个物理行，并设置 `NonRudeHWND=TRUE`，目的是兼容自动隐藏任务栏升起；代价是边缘手势无法屏蔽（用户实测证实）。白板下不挖底行，并对 Freeze 和 Drawpad 调用 `MarkFullscreenWindow(TRUE)`，边缘手势可以屏蔽。
- 桌面和 PPT 的选择态（穿透）走辅助前置窗口 DrawpadPresentation（ULW），绘制态走 Drawpad，两者互斥显示（`SelectionUsesAuxiliaryOutput`）。目前不论主呈现是 DComp、Win7dwm 还是 ULW，都会初始化并使用辅助窗口。

## 需求

### R1 WM_POINTER 下关闭系统触摸与笔的可视反馈

- 只在输入方式实际为 WM_POINTER 时生效，对 Draw3 画布 HWND 调用 `SetWindowFeedbackSetting`。
- 要关闭的反馈：
  - 触摸：`FEEDBACK_TOUCH_CONTACTVISUALIZATION`、`TAP`、`DOUBLETAP`、`PRESSANDHOLD`、`RIGHTTAP`。
  - 手势：`FEEDBACK_GESTURE_PRESSANDTAP`。
  - 笔：`FEEDBACK_PEN_BARRELVISUALIZATION`、`TAP`、`DOUBLETAP`、`PRESSANDHOLD`、`RIGHTTAP`。
- RTS 和鼠标兼容模式不设置；从 WM_POINTER 切走、或脱离窗口时，传 `NULL` 恢复系统默认。
- 从 user32 动态加载，Win7 上没有就跳过，不报错。

### R2 鼠标兼容模式的信息条

- 在设置的性能页中，输入方式选“鼠标兼容模式”时，标题下方出现蓝色信息条。信息条按严重度规则属于系统或环境条件。
- 文案（括号内用下拉项标签“鼠标兼容输入”，与 RTS 信息条的写法一致）：“当前输入方式（鼠标兼容输入）不支持多指绘图，触摸和笔的扩展属性（如压感、笔尾橡皮）不可用，也不会屏蔽系统的触摸反馈。”
  - 不写“无法屏蔽触摸手势”：屏幕边缘滑入是否屏蔽取决于窗口是否全屏，和输入方式无关（见 R3）。
- 按 i18n 流程同步 20 种语言。

### R3 桌面 Drawpad 全屏，并提供非全屏开关

- 非白板工作区（桌面、PPT）下，Drawpad 默认按全屏处理：
  - 不再挖掉最后一个物理行，不设置 `NonRudeHWND`，让系统认定全屏，从而屏蔽边缘手势。
  - 辅助前置窗口（选择态）维持现状：挖掉 1px 并设置 `NonRudeHWND`，兼容自动隐藏任务栏升起。
  - 结果：绘制态下任务栏不升起（用户接受），选择态下照常升起。
- 在设置的“窗口和显示器 → 行为”中新增开关“桌面画布绘制时使用非全屏窗口”：
  - 不带说明文字，默认关闭。
  - 开启后完全保留当前行为：Drawpad 也挖底行并设置 NonRude。
- 分两步落地：
  1. 只去掉 Drawpad 的挖底行和 NonRude。
  2. 真机上如果边缘手势仍未屏蔽，再在桌面绘制态对 Drawpad 调用 `MarkFullscreenWindow(TRUE)`。这需要放开 `ResolveTargets` 中 drawpad 只在白板下才启用的限制。
- 配置项需要中文注释、默认值、元数据测试以及 20 种语言。

### R4 ULW 模式由画布自己承担选择态

- 主呈现实际为 ULW 时：
  - 选择态不再切到辅助前置窗口，也不再把画面复制或读回到辅助窗口。
  - Drawpad 自己切换 `WS_EX_TRANSPARENT` 来实现穿透。
  - 主窗口每个像素都垫有极低的点击承接 alpha。切到选择态时不为去掉它而整帧重传：`WS_EX_TRANSPARENT` 下点击本来就会穿透，这层 alpha 的视觉影响可以忽略。这样还能避免切换时闪烁。
- 底行策略按状态切换：
  - 选择态：Drawpad 挖底行并设置 NonRude，兼容任务栏升起。
  - 绘制态：按 R3（默认全屏；开关开启时挖底行）。
- 画布调试信息中的“辅助前置表面”在 ULW 模式下显示为“由画布承担”。
- ULW 模式下不初始化辅助窗口的 presenter，也不显示辅助窗口。
- 辅助窗口 HWND 照常创建但不使用（见“已定决策”1）。

## 已定决策（2026-11-20 用户确认）

1. R4 辅助窗口 HWND 照常创建（窗口链零改动），ULW 模式下始终隐藏、零像素、不初始化 presenter；运行期回退到 ULW 同样适用。
2. R3 全屏策略与开关覆盖所有非白板工作区（桌面和 PPT）。
3. R2 文案以“当前输入方式（鼠标兼容模式）不支持……”开头，采用“不会屏蔽系统的触摸反馈”的说法。
4. R3 分两步：本任务只去掉 Drawpad 的挖底行和 NonRude；MarkFullscreenWindow 等真机结果再决定。
5. 运行期从 DComp/Win7dwm 回退到 ULW 时若处于选择态，由新的单窗口规则重算目标并交接（见 design.md）。

## 风险（实测项）

- 系统认定全屏后，Win10/11 的“全屏时自动免打扰”可能在绘制时触发。
- 绘制态和选择态切换等于反复进出全屏，Explorer 的判定有延迟，任务栏升起可能滞后，或层级闪一下。R4 的单窗口 ULW 下尤其明显。
- 对分层窗口反复调用 `SetWindowRgn`、在运行期切换 `WS_EX_TRANSPARENT` 时，正在进行的接触、鼠标捕获以及光标归属都要正确收尾。

## 验收

- [ ] WM_POINTER 下触摸按下没有圈，移动没有拖尾；RTS 和鼠标兼容模式下行为不变。Win7 正常启动。
- [ ] 鼠标兼容模式时，性能页出现蓝色信息条，20 种语言的 i18n check 通过。
- [ ] 开关默认关闭时：桌面绘制态下屏幕边缘滑入被屏蔽，选择态下自动隐藏任务栏能升起。开关开启时与当前行为一致。白板不受影响。
- [ ] ULW 模式下：选择态由 Drawpad 穿透承担，辅助窗口不显示，调试信息显示“由画布承担”；切换时不闪烁，选择态下任务栏能升起。DComp 和 Win7dwm 的行为不变。
- [ ] 构建 `InkeysRepo.sln` Debug|ARM64 通过，headless 测试和相关自检通过。

## 协作分工（用户 2026-11-20 指定）

- **Claude**：整体规划、代码实现、独立审阅。
- **Codex**：调查与审阅，只读。使用 `gpt-6.2-sol` High + fast；Astra 不可用。
