# 文字展开/收起不等比：根因调查（Claude）

日期：2026-11-26。只读调查，未改产品代码。

## 结论摘要

问题不在文字字号动画本身（字号每帧按面板当前倍率 `SetDirect`，本身是等比的），而在 **UI3 字号整数像素修正系数**：

1. 修正系数 = `floor(targetPixels) / targetPixels`，`targetPixels` 由 `fontTargetSize`（名义目标字号）× 目标缩放算出。
2. More 面板与绘制属性面板把 `fontTargetSize` 设成“面板最终姿态”下的字号；**收起时最终姿态是紧凑倍率 0.16**，名义目标字号只有约 2 px，`floor` 误差变成 30%–50%。
3. 修正系数本身是一条独立动画（`UiFontCorrectionClass::value_`，默认曲线 `EaseInOutCubic`，时长 `UiDefaultOperationDur`），与面板进度曲线（More：展开 `EaseOutBack`、收起 `EaseInBack`）不同。
4. 结果：展开时系数从约 0.52 按 InOutCubic 慢慢爬回 1，面板用 OutBack 早已到位 → “到位后继续放大”；收起时面板 InBack 前半程几乎不动，系数已开始下降 → “收起前比等比更小”。

所以用户“曲线不匹配”的推测方向对，但更根本的是：**收起目标被当作字号取整的静止目标**，取整误差在小字号下非常大，然后再被另一条曲线插值出来。

## 证据链（file:line 以当前 HEAD 000b830c 为准）

### 字号绘制

- `Inkeys/Inkeys/UI/UI3/UI3.Renderer.cpp:192-200` `ResolveWordFontSize`：`nominalTarget = word.fontTargetSize`（≥0 时）；`word.size` 已落定（`SetDirect`）时 `duration = 0`。
- `UI3.Renderer.cpp:202-235` `ResolveAnimatedFontSize`：`targetPixels = nominalTarget * targetZoom * targetPixelsPerUnit`；绘制字号 = `fontSize * correction.Resolve(...)`。
- `Inkeys/Inkeys/UI/UI3/UI3.Animation.cppm:24-30` `ResolveFontCorrectionTarget`：不允许小数字号时返回 `floor(targetPixels)/targetPixels`。
- `UI3.Animation.cppm:836-865` `UiFontCorrectionClass::Resolve`：目标变化时 `value_.SetTar(target, duration>0 ? duration : UiDefaultOperationDur)`；`value_` 声明在 `:899`，`UiValueClass` 默认曲线 `EaseInOutCubic`。
- 只有 `targetPixels == 0` 时才保留当前系数（`:843`）——设计时考虑了“退出到 0”，没考虑“退出到紧凑倍率”。

### More 面板（真实主栏与组件页预览共用）

- `Inkeys/Inkeys/UI/Bar/Bar.ContentInstance.cpp:96-100`：`scale` 取 `progress.val`，`fontTargetScale` 取 `progress.tar`，收起时为 `BarMorePanelCompactScale`。
- `Bar.ContentInstance.cpp:254-256`：`ConfigureButtonVisualTargets(..., scale, direct=true, nullopt, fontTargetScale, labelSize, ...)`。
- `Inkeys/Inkeys/UI/UI3/UI3.Button.cpp:614-617`：`name.size` 设为 `labelSize*scale`（direct），`name.fontTargetSize = labelSize * fontTargetScale`。
- `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp:353`：`BarMorePanelCompactScale = 0.16`。
- `Inkeys/Inkeys/UI/Bar/Bar.MainContent.cpp:509-519` `ApplyMainBarMoreTargets`：progress 展开 `EaseOutBack`、收起 `EaseInBack`。
- 调用方：真实主栏 `Bar.RenderLoop.cpp:5146`，组件页预览 `Bar.ContentInstance.cpp:1311`，两边同一函数，所以两边现象一致。

### 绘制属性面板（真实主栏）

- `Bar.RenderLoop.cpp:5799`：`fontPanelTargetScale = panel->w.tar / BarDrawAttributeExpandedWidth`；收起时 `w.tar = BarDrawAttributeCompactWidth`（`:3788`），倍率 = 60/370 ≈ 0.162（`Bar.Main.cpp:233-235`）。
- `Bar.RenderLoop.cpp:6412-6413`、`6448`、`6531`：文字 `size.SetDirect(N * panelScale)`，`fontTargetSize = N * fontPanelTargetScale`。

### 引入时间

- `3d9d04d5`（2026-10-24）fix(ui): keep font animations continuous with integer targets —— 引入修正系数动画与 `fontPanelTargetScale`。
- `eff9011c`（2026-10-26）fix(bar): preserve final font targets during more panel animation —— More 面板改用 `progress.tar` 求 `fontTargetScale`。
- `56ed26e9`（2026-11-18）UI3 分层只是搬移。
- `0557a861` / `016a86ec`（组件页编辑器 / 共享编排）把 More 定位抽成 `PositionContentMore` 并让预览也调用，**没有改变上述公式**；目前看是让问题在预览里也出现、更容易被注意到，而不是引入。（待 Codex 独立核对。）

## 数值模拟

`python -I %TEMP%\inkeys-codex\sim.py`：标签 12 DIP，面板曲线按 More（OutBack/InBack，back=1.1），系数按 InOutCubic，同一时长。

| 缩放 | 展开系数 | 收起系数 | 展开 t=0.5 时面板倍率 / 字的等比比例 | 收起 t=0.5 时 |
|---|---|---|---|---|
| 100% | 1.000 | 0.521 | 1.011 / 0.76 | 1.010 / 0.76 |
| 125% | 1.000 | 0.833 | 1.011 / 0.92 | 1.010 / 0.92 |
| 150% | 1.000 | 0.694 | 1.011 / 0.85 | 1.010 / 0.85 |

展开后半程（t 0.5→1.0）面板只在 1.0 附近回弹，字从 76% 长到 100%，即“到位后放大”；收起前半程面板仍在 1.0 附近，字已缩到 76%，即“收起前更小”。

## 待测（建议实机日志/trace 字段）

- 每帧：面板 `progress.val/tar`、`scale`、`fontTargetScale`、某个按钮 `name.size.val`、`name.fontTargetSize`、`fontCorrection.Current()/Target()`、最终绘制字号。
- 现有 `UI3.VisualDirtySampler.cppm:173` 已采样 `fontCorrection.Current()/Target()`，可直接用 UI3 trace dump 验证。

## 修复方向（只列方向，未选定）

- A（推荐）：收起（目标为紧凑态/隐藏）时不把紧凑倍率当字号取整目标——`fontTargetScale` 只在展开目标时生效，收起时沿用展开态的修正系数（与 `targetPixels == 0` 分支同语义）。改动集中在 `fontTargetScale` 计算处（More 一处、绘制属性一处，Scene 系面板需同查）。
- B：在 `UiFontCorrectionClass` 里忽略小于某阈值的目标像素（例如 < 6 px 时保持当前系数）。通用但阈值是经验值。
- C：修正系数动画改用与面板相同的曲线/批次。只能让两者同步，不能消除收起时 30%–50% 的偏小，不推荐单独使用。

## 与 Codex（gpt-6.2-sol High）交叉核对

报告原文见 `codex-sol-report.md`。

- 一致：高置信根因相同——修正系数在开合两端的取整值之间做 `EaseInOutCubic` 追赶（1× 下收起端约 0.52、展开端 1），面板用 Back 曲线，于是展开到位后继续放大、收起初段偏小；`0557a861` / `016a86ec` / `85331018` 沿用了修正时长与曲线，没有引入新公式。
- Codex 补充且已抽查属实：
  - 绘制属性窗口的笔型、粗细等文字是 `size.SetTar`，与面板共用 `syncedValueCurve`（`Bar.RenderLoop.cpp:4952-4958`、`5096-5107`）；它们未设 `fontTargetSize`，取整目标取 `size.tar`，收起时同样是紧凑倍率，问题相同。
  - More 外框宽高从 60×30（`Bar.Main.cpp:354-355`）插到网格尺寸，内容倍率从 0.16 插到 1，两者比例不完全相同。属于次要的不等比，只在接近收起端、透明度很低时可见，不是本次两个现象的主因。
  - 组件页预览用包络算出的 `zoomTarget` 取整；`85331018` 降低了预览高度，目标缩放变化只会改变两端系数的具体数值，不改变机制。
- Codex 的低置信项（More 标签拟合缓存变化导致重定向）只对几何类标签的换字路径有影响，常规开合不触发，暂不作为根因。

## 最终定位

1. 主因：`UiFontCorrectionClass` 把**收起端（紧凑倍率约 0.16）**当作静止目标做整数像素取整，取整误差 30%–50%，再用与面板不同的曲线插值出来。
   - 系数机制：`UI3.Animation.cppm:24-30`、`:836-865`；`UI3.Renderer.cpp:192-229`。
   - More（两宿主共用）：`Bar.ContentInstance.cpp:98-99` 的 `fontTargetScale` → `UI3.Button.cpp:617`。
   - 绘制属性窗：`Bar.RenderLoop.cpp:5799`（菜单/锁定提示等 SetDirect 文字）与 `size.tar`（笔型/粗细等 SetTar 文字）。
2. 次要：More 外框与内容紧凑倍率不一致（`Bar.Main.cpp:353-355`）。
3. 不是近期组件页提交引入；它们把 More 定位共享给预览，让问题在预览里同样出现。
