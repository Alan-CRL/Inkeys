# 设计：Draw3 画布手势屏蔽与全屏窗口

依据：`prd.md`；调查 `research/codex-r1-r3.md`、`research/codex-r4.md`（关键事实已由 Claude 抽查属实）。行号以 refactoring/draw 当前 HEAD 为准，实现前再核。

## R1 Pointer 反馈关闭（Draw3.WindowControl.cpp）

- `DrawingPointerApi()` 增加 `SetWindowFeedbackSetting` 的动态解析，缺失时（Win7）为空，静默跳过。
- 关闭时机：在 HWND owner 线程上，`kMouseInputOwnerMessage` 的 start 成功分支里，且 `pointerRequested` 为真（即实际选中 Pointer 后端）。按 PRD 的 11 项逐项调用，`flags=0`，传 `BOOL FALSE`；成功的项记入成员位掩码 `pointerFeedbackSuppressed_`。
- 恢复时机：stop 分支（清 `pointerInputSelected_` 处）、`WM_DESTROY`、`DetachExternal`（兜底，只在 HWND 仍有效且位掩码非零时）。逐项传 `size=0, NULL`，恢复后清掉位掩码。
- RTS 和鼠标兼容模式不会走 Pointer start，所以天然不设置。
- 不新增日志通道；失败时用现有的 `LogWin32Error` 只记一次。

## R2 鼠标兼容信息条（Setting.cpp `RenderDraw3InputMode`）

- 在现有 RTS / Pointer 条件分支后加 `else if (preference == InputPreference::MouseCompatibility)`，调用 `Design::Notice("##draw3-input-mouse-compat", …)`，用默认信息级别（蓝色）。
- 新 i18n 键：`InputMode.MouseCompatibilityNotice`，放在现有 `RtsTouchpadNotice` 所在的同一段落，同步 20 种语言。

## R3 Drawpad 默认全屏与非全屏开关

### 配置与设置页

- 新配置项：布尔值，默认 `false`，`NoUpload`，带中文注释。键名在实现时就近放进窗口行为相关的配置组，与 `RenderWindowBehavior` 现有草稿字段同组。
- 设置页：在“窗口和显示器 → 行为”里加 `ToggleRow`，标题“桌面画布绘制时使用非全屏窗口”，不带说明。改动后写盘，并提交窗口服务命令立即重算策略。
- 启动时由 `IdtMain.cpp` 注入窗口服务。

### 窗口服务的底行策略（Window.CanvasPolicy.h / Window.cpp）

- 把 `CanvasPolicyKey.whiteboard` 的含义细化为一个解析后的布尔值：`keepNonFullscreen`，表示是否挖底行并设置 NonRude。
  - `CanvasBottomExclusion` 和 `CanvasNonRudeValue` 改为基于 `keepNonFullscreen` 判断，白板时恒为 false。
- 在 `EnsureCanvasPolicy` 里按角色解析 `keepNonFullscreen`：
  - 白板：false（不变）。
  - DrawpadPresentation：非白板时为 true（不变）。
  - Drawpad：非白板，且满足“开关开启”或“ULW 单窗口选择态”（R4）任一条件时为 true。
- 解析后的值纳入缓存键（现有的 `CanvasPolicyNeedsUpdate`），使开关变化和选择态变化都能触发重新应用，并同步更新 `WorkspaceNativePolicyMatches` 的准备回执校验。
- 不碰 `canvasPolicyDirty_`：它会连带重做激活样式。策略变化只依靠键的差异来触发。
- 第二步（`MarkFullscreenWindow`）不在本任务实现，只在 PRD 中记录改动点：在 `ResolveTargets` 中增加非白板主窗的条件。

## R4 ULW 单窗口选择态

### 目标判定

- `Bridge::SelectionUsesAuxiliaryOutput(selectionMode, workspace)` 增加参数 `auxiliaryAvailable`：主呈现实际为 ULW 时传 false。
  - 所有生产调用点都按实际后端传入：`DrawingController.cpp:21855`、`PresentationState.h:20`、`IdtState.cpp:714/4708`，以及 `Host.cpp:2414` 的首帧判断。
  - “实际后端”取自 Host 运行快照中已有的 active mode 字段；没有该字段则在快照中新增。
- `ResolveDrawpadPresentationSurface`：单窗口时选择态返回 `Primary`，不再走 `Hidden`/`Presentation`。空页时 Drawpad 仍保持可见：它本来就是穿透的，内容为空。
- `IdtState.cpp:1182/4633`：常规显隐以及工作区空页条件同步改用新判定；`Window.cpp:3276/3735` 的工作区显隐和安全放行角色同步调整。故障和最小化的隐藏分支保留不动。

### 呈现层（Draw3.TransparentPresentation.cpp）

- `InitializePresenter`：ULW 主 presenter 初始化成功后直接返回，不初始化 `selectionUlwPresenter`。
- 运行期从 DComp 或 Win7dwm 回退到 ULW 时，如果辅助 presenter 已建立：
  - 释放它的资源，并把辅助窗口隐藏。
  - 目标在同一轮按新的判定重算。
  - 用实际后端约束旧回执，防止旧的 `SelectionUlw` ready 被当作有效。
- 调试信息 `PublishInformation` 的 ULW 分支改为显示“辅助前置表面：由画布承担”。
- `rawOutputTarget` 和运行指标：单窗口选择态记为 `PrimaryDrawpad`；性能看板按“非白板选择态”排除样本的逻辑保持不变。

### 帧循环（DrawingController.cpp 21853–21942）

- 把 `outputTargetChanged` 拆成两部分：选择态边沿，以及输出目标是否真的变化。
- 单窗口下目标不变，选择态切换不强制整帧、不等待辅助清屏验证；光标清除等真实脏区照常保留。

### 穿透与底行切换顺序

- 主窗口在稳定态已经跟随选择意图切换 `WS_EX_TRANSPARENT`（`IdtState.cpp:804/1038/1114` → `SetClickThrough`），ULW 单窗口直接复用这条路径。
- 点击承接垫底（黑色，α=1/255，仅当源 α≤127 时补 1）保留，不整帧重传。
- 切换顺序：
  - 进入选择态：先开穿透，再挖底行。
  - 回到绘制态：先恢复区域，再关穿透。
- 输入收尾复用现有的选择态切换逻辑：拒收新笔触、离开移动态取消触控板导航、刷新光标、解绑触觉。此外，在窗口线程上确认按住时切换不会残留鼠标捕获或 Pointer/RTS 接触。如有缺口只补这一处，不改其他行为。

## 不做

- `MarkFullscreenWindow` 扩展（R3 第二步）。
- 不改窗口链的创建、Bar/PPT 等其他窗口的手势 flags、`EnableMouseInPointer`。
