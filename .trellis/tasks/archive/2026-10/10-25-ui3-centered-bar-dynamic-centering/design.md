# UI3 居中底栏动态宽度居中修复 — 技术设计

> 2026-10-26 收尾授权：用户明确要求提交当前修复，并结束、归档本居中移动子任务；吸附闪动子任务继续保持in_progress。先提交代码及验证记录，再单独归档提交。真实PPT GUI未执行的验证边界保留，不将关闭任务视为新增人工验证。

> 2026-10-26 当前补充授权：用户要求先恢复退出PPT时缺失的竖直返回动画。范围为RenderLoop的桌面恢复Y（从最后成功位置按既有0.4秒曲线恢复，居中X继续逐帧派生）和同一路径的离屏动画连续性断言；快速退出再进入须保留原桌面基线。复用既有场景动画标记，不改拖动固定表面、松手交接、曲线、线程、项目配置；不新建任务、不提交、不结束任务，不启动真实GUI。

> 2026-10-26 接力：本计划已获本会话授权并完成代码与自动化，最新证据见父任务 `research/final-integration-validation.md`；原文未来式保留作历史设计。GUI/真实系统呈现验收待授权，任务未归档。

## 已确认现状与直接原因

当前主栏目标宽度来自公共列表布局，`mainBar->x/w` 有自己的动画（`Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp:1710-1712,2865-2902`）。稳定居中路径在按钮和主栏动画推进后，以主按钮、主栏及可见描边的当前值反推 `mainButton->x`，再执行 `MainBar.Inherit(Center, MainButton)`（同文件 `:5537-5588`；纯函数位于 `Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h:889-917`）。

该反推目前被 BottomDocked、Centered、Stable、Expanded、无拖动/弹簧以及 `!displayTransitionActive` 等条件门控（`Bar.BottomDock.h:875-887`；`Bar.RenderLoop.cpp:5554-5559`）。PPT/白板场景就位则对 `displayCenterX/Y` 设置 0.4 秒目标并激活 display transition；`ApplyDisplayTransition` 推进后直接把插值写入 `mainButton->x/y`（`Bar.RenderLoop.cpp:1413-1425,1467-1469,2763-2767,2792-2806,2825-2838`）。

对 **Desktop→PPT/Whiteboard 及 PPT↔Whiteboard（Enter/Switch）**，场景 Y 移动与主栏宽度变化并行时，主按钮 X 和主栏 X/W 分别插值，当前帧联合中心不再等于屏幕中心；只有过渡结束、稳定居中门禁恢复后才重新对齐。这直接解释相应用户症状，不需要预设 ULW/DWM 丢帧。

**最终 Leave→Desktop 是不同路径**：`desktopRestorePending` 在 `ApplyDisplayTransition` 后直接恢复桌面 X/Y 并清 `displayTransitionActive`，随后本帧稳定根反推仍可运行（`Bar.RenderLoop.cpp:12934-12950,13053-13071`）。Leave 必须独立复现和验收，不能未经验证套用 Enter/Switch 根因或重写该路径。

详细证据与历史见 `research/centering-investigation.md`。

## 设计目标

在保留现有场景 Y 动画、主栏宽度曲线、Free/Centered 正交状态和桌面基线恢复的前提下，使居中底栏每个呈现帧都满足实际联合外框水平居中。不得只在终态修正，也不得添加第二套补偿平移。

## 根位置所有权设计

将“显示过渡期间一律禁止居中反推”细化为明确的二维所有权矩阵：

| 状态 | 主按钮 X | 主按钮 Y |
| --- | --- | --- |
| 稳定 BottomDocked + Centered + Expanded | 继续由当前联合几何反推 | 现有所有者不变 |
| 同一居中底栏的 Enter/Switch 场景竖直就位 | **由当前联合几何逐帧反推** | 继续由 `displayCenterY` 场景动画拥有 |
| 最终 Leave→Desktop | 保留 desktop restore 与随后稳定根反推的现有路径，独立验证 | 保留现有 desktop restore |
| BottomDocked + Free 的场景/显示过渡 | 现有 `displayCenterX` / 场景所有者不变 | 现有所有者不变 |
| 拖动、水平捕获/脱离/远端弹簧 | 现有抓手与弹簧映射所有，不反推 | 现有所有者不变 |
| Floating、折叠、普通显示环境切换 | 现有门禁和场景所有者不变 | 现有所有者不变 |

关键是**只窄化居中 Enter/Switch 竖直就位这一分支**，不是全局移除 `displayTransitionActive` 保护。生产上可把现有 `whiteboardDockAnimationActive` 作为场景放置活动输入；该历史命名同时覆盖 PPT 与 Whiteboard，而 `pptDockPlacementPending` 会在 Submit 中清除，不能误作跨帧活动标志。

为 `ShouldDeriveBarBottomDockCenteredRoot` 增加显式的 workspace placement 输入，让实际根反推与 viewport 根范围预测都调用同一纯门禁。禁止在 `:5554` 与 `:9043` 两处分别拼接受场景布尔表达式；不增加另一份原子状态或并行事务。

## 帧内数据流

1. 现有 `ApplyDisplayTransition` 继续推进 displayCenter X/Y 和 `displayCenterY` 的 0.4 秒场景动画，不重写 Leave，也不提前引入另一套场景判定。
2. 主按钮尺寸、主栏 `x/w`、描边和布局批次按现有动画正常推进，不因逐帧居中而重启。
3. 对满足完整居中条件的 Enter/Switch 场景帧，在现有根反推阶段以本帧实际联合外框计算并覆盖 `mainButton->x`，再执行 `MainBar.Inherit(Center, MainButton)`；本帧下游只消费该最终根位置。
4. 根反推现有的 `displayCenterX.SetDirect` 会终止独立 X 插值，使 Centered 场景 X 改为由屏幕中心和当前宽度约束派生；Y 与主栏宽度曲线不变。Free 和其他显示过渡仍保留原 X 动画。
5. 之后再派生 Popup、提示、命中、dirty、capacity/viewport 和光源等下游几何。
6. 过渡结束时不切换公式：同一个逐帧联合中心反推继续接管稳定居中，因此终点不产生二次跳变。

`displayCenterX` 不在本任务中全局删除；它对 Free、普通显示过渡和既有 Leave 路径仍有用途。

## Viewport、dirty 与命中

- `Bar.RenderLoop.cpp:9039-9084` 的 viewport 预测与实际根反推共用上述门禁，并继续用 `ResolveBarBottomDockCenteredRootRange` 从主按钮尺寸、主栏 `x/w` 和描边的动画范围保守反推根 X range；不能只修 `:5554` 的绘制根位置。
- 优先复用现有稳定居中范围预测和容量控制，不增加全局 correction outset，不每帧缩窗。
- 保留 `bottomDockRootLayoutChanged` 到 dirty/full replacement 的现有事务（`:5586-5587,7293-7317,9409-9419`）；稳定 tuple 的普通帧才允许局部 dirty。
- 绘制、命中和提示等下游必须读取本帧反推后的继承结果，不能保留 `ApplyDisplayTransition` 早期写入的 X 副本。
- 检查 `pptSrc + psize` 始终位于 target、旧新 damage 完整以及失败重试仍从上一成功快照恢复。

## 方案权衡

### 采用：场景 Y 与居中 X 分轴 ownership

改动集中在现有纯门禁、实际根反推和同一 viewport 预测，直接满足原始设计意图，且终点与稳定态使用同一公式。风险是场景/普通显示切换的分类错误，因此必须有所有权矩阵和真实 RenderLoop 场景回归。

### 不采用：每帧按当前宽度重设 `displayCenterX` 动画目标

这会反复重定向 0.4 秒动画，可能滞后、改变曲线并继续产生中间帧误差；本质上仍把主按钮固定位置当作中心基准。

### 不采用：呈现前额外 correction / rebase

会形成第二套位置真值，并扩大 dirty、viewport、命中和直移的协调面；08-20/08-23 已有多阶段方案失败史，不能恢复旧补偿路线。

## 回归设计

### 主复现：真实 RenderLoop + 离屏呈现

- 复用 `CreateBarOffscreenRenderCallback`（`Bar.Main.cppm:1208-1211`；`Bar.RenderLoop.cpp:13142-13153`）和 `Bar.MultiTarget.Test.cpp:168-219,342-364` 的现有 fixture 模式，保留真实目标提交、动画、D2D/GetDC/ReleaseDC/EndDraw 与成功快照，只替换最终 ULW sink。
- 以受控 `frameTime` 驱动 Desktop→PPT、Desktop→Whiteboard、PPT↔Whiteboard，并在场景中切换 Draw↔Selection 或实际可见按钮，确保宽度与 Y 同时变化。
- sink 记录 `pptDst/psize/pptSrc/prcDirty`；只在成功呈现 serial 前进后采样 `BottomDockPresentedSnapshot().visualBodyCenterScreenX`。修复前必须至少一个 Enter/Switch 中间成功帧偏离 monitor 中心，终态恢复不能充当通过。
- 最终 Leave、快速反向、Whiteboard 最小化/恢复分别报告；Leave 失败时保留独立样本并返回调查，不与 Switch 合并归因。

### 辅助纯算法回归

- 在 `InkeysHeadlessTests/bar_bottom_dock_tests.cpp:1165-1235` 扩展所有权矩阵：居中 workspace placement 允许反推，Free/普通 display transition/拖动/弹簧仍禁止。
- 用生产 `ResolveBarBottomDockCenteredRootPlacement`、根范围函数和真实门禁覆盖连续宽度样本、左右展开、描边和无效几何；同时证明实际根与 viewport 范围使用同一分类。
- 纯算法用例不能替代真实 RenderLoop 场景复现。

完整构建和既有无窗口回归通过后，仍需真实 GUI 逐帧检查 Office/白板、100%/150% DPI、负 monitor 原点及跨屏场景。

## 回退边界

变更应限定在 `Bar.BottomDock.h` 的居中门禁/纯几何、`Bar.RenderLoop.cpp` 的实际根反推与同一预测，以及现有测试/离屏 harness。若出现普通显示切换、Leave 或拖动回归，回退该窄分支，不修改吸附 serial、弹簧参数或动画曲线作为补偿。
