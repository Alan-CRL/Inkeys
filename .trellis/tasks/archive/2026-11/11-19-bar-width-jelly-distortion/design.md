# 设计：主栏水平果冻映射的触发边界

## 1. 机制回顾

`AdvanceAnimationsAndDeriveLayout` 中的底栏块（`Bar.RenderLoop.cpp:7223` 起）每帧计算水平映射：

```
state.bottomDockHorizontalMapping = ResolveBarBottomDockHorizontalMapping(
    mainBarLeftDip, mainBarRightDip, opensRight,
    centerSpring.positionDip,                                   // 近端（随主按钮刚性平移）
    farTargetDip - baseFarDip + farEdgeSpring.positionDip);     // 远端
```

`ResolveBarBottomDockHorizontalMapping`（`Bar.BottomDock.cppm` 的 `ResolveBarBottomDockHorizontalMapping`）让近端与远端独立移动，`scaleX = 新宽度 / 布局宽度`，主栏背景、普通按钮、图标、文字全部按 `scaleX` 横向缩放，主按钮不参与——与截图特征完全一致。只要“远端偏移 − 近端偏移”不为零，主栏就会被拉伸或压缩。

该块不受 `dockMode` 限制，悬浮态同样执行。

## 2. 根因

### 根因 A：显示环境/缩放变化被当成“手势交接”播种（场景 1 主因）

`Bar.RenderLoop.cpp:7619-7623`：

```
const bool horizontalHandoff = presented.centerMode != centerMode
    || presented.dragSession != frame.bottomDockDragSession
    || presented.dragActive != dockDragActive
    || abs(presented.zoom - frameZoom) > 0.000001              // ← 缩放
    || presented.displaySerial != state.observedDisplaySerial;  // ← 显示环境
```

交接帧（`:7640-7656`）调用 `ResolveBarBottomDockShiftedHorizontalShape`，把远端钉在**上一成功帧的屏幕像素**：`farError = world(旧远端像素, 新 zoom) − farTarget`。非拖动时 `targetGripScreenX = presented.mainCenterScreenX`，主按钮屏幕位置不变，因此：

- 缩放变大（`newZoom > oldZoom`）：远端仍在旧像素，主栏被压扁到约 `oldZoom/newZoom` 倍（截图 2）。
- 缩放变小：主栏被拉宽（截图 1）。
- 交接帧不推进弹簧（`if (!horizontalHandoff) Advance...`）。`ApplyConfigZoom` 连续调整或 `displayUiBaseScale` 0.4 s 动画期间 zoom 每帧都变，每帧都重新钉住远端，形变会持续到缩放停止后再由弹簧回弹（`preservePresentedOffset` 允许超过 24 DIP）。

这条“显示环境交接”的设计本意只针对**拖动中**（spec rendering-and-ui.md “手动模式/session/显示环境交接”与 `ApplyDisplayTransition` 的 dragging 分支），非拖动时由 `ApplyDisplayTransition` 自己保证屏幕连续，不需要也不应该再做像素级播种。

### 根因 B：居中目标在根节点被程序性动画持有时仍固定远端（场景 2 主因）

`Bar.RenderLoop.cpp:7632-7639`：只要 `centerMode == Centered`，`farTargetDip` 就取**最终稳定居中**联合外框的远端：

```
if (centerMode == BarBottomDockCenterMode::Centered)
    farTargetDip = opensRight ? placement.bodyRightDip : placement.bodyLeftDip;
```

而根节点 X 是否被派生居中由 `ShouldDeriveBarBottomDockCenteredRoot`（`:6115`）决定；`fa57e5f5` 为了消除“展开与居中打架”，在 `workspaceEntryRootTween` / 普通 `displayTransitionActive` 期间关闭了派生，根 X 交给 0.4 s 放置动画。于是：

- 近端 = 正在滑动的根节点；远端 = 固定在最终居中位置。
- 动画过程中主栏宽度 = 远端 − 近端，持续变化 → 拉伸/压缩，直到放置动画到达终点。

`fa57e5f5` 只暂停了根派生和预测包络（`:11628`），没有同步暂停映射里的居中远端目标。这正是 `11-06` followup “进入动画不自然”的来源之一。DPI/显示器切换时若处于 Centered 也走同一路径（派生被 `displayTransitionActive` 关闭）。

### 根因 C：场景接管清零的弹簧被同帧交接重新播种

场景接管（`:13540-13560`）把 `centerMode` 改为 Centered 并清零四个弹簧，但同帧布局阶段 `presented.centerMode(Free) != Centered` 成立，`horizontalHandoff` 再次从旧像素播种远端/抓手弹簧，覆盖清零。PPT 屏幕切换（`UpdatePptSceneMonitor` → 新显示快照）还会叠加根因 A。

## 3. 修复方案

### F1：只在手势参与时做像素级水平交接（修 A、C）

```cpp
const bool horizontalHandoff = ...;  // 原条件不变，仍用于判定“需要重基准”
// 果冻交接只属于手势：按住中、刚松手、或换了 drag session。
// 缩放倍率、显示快照和场景接管等程序性变化由显示过渡/放置动画保证连续，不能钉住旧像素。
const bool gestureHorizontalHandoff = horizontalHandoff
    && (dockDragActive || presented.dragActive
        || presented.dragSession != frame.bottomDockDragSession);
```

- `farTargetDip` 的 `dockDragActive || horizontalHandoff ? gripOffsetDip : ...`、播种块 `if (horizontalHandoff)`、两处 `if (!horizontalHandoff) Advance...` 改用 `gestureHorizontalHandoff`。
- 非手势交接：不播种，弹簧保持原 DIP 值继续推进（场景接管已清零 → 映射恒等；松手恢复中途改缩放 → 以 DIP 偏移继续衰减，随 zoom 等比，无跳变）。

### F2：根 X 由程序性动画持有时远端刚性跟随（修 B）

```cpp
// 根 X 由放置/显示过渡持有且未被居中派生时，远端随根刚性移动；
// 只有派生居中或手势捕获时才把远端吸到稳定居中边界。
const bool rootOwnedByTween = !dockDragActive
    && !state.bottomDockCenteredRootApplied
    && (state.workspaceEntryRootTween || state.displayTransitionActive);
if (centerMode == BarBottomDockCenterMode::Centered && !rootOwnedByTween) { ...原居中目标... }
```

- 场景接管、DPI 过渡期间映射恒等，主栏随根平移/缩放；动画结束后派生恢复，根已在居中位置，远端目标与基准远端一致，不产生收尾跳变。
- 白板 `whiteboardDockAnimationActive` 期间派生仍允许（只让出 Y），`bottomDockCenteredRootApplied == true`，行为不变。
- 拖动捕获/松手恢复（`Capturing/Recovering`）不受影响：拖动中 `dockDragActive`；松手后 `displayTransitionActive` 通常为 false。
- 同时 `centerMode == Free || rootOwnedByTween` 时远端取推进后的 `baseFar + centerSpring`，与近端（刚性抓手）同帧一致。

### F3：居中远端取主栏自身远端（实施中补充，修根因 D）

**根因 D**（回归测试发现）：原 Centered 远端目标为联合外框远端 `placement.bodyRight/LeftDip`。PPT 进入时翻转展开方向、退出时翻回，换向关键帧期间主栏尚未越过主按钮，联合远端等于主按钮的边，主栏被拉到那里（基线测试 `leaveScaleErr = 0.132`）。

```cpp
farTargetDip = baseFarDip + placement.mainCenterDip - dockMainButton->x.val;
```

由 `inhX = x + o - w/2` 可得 `baseFar - x = o ± wv/2`；当主栏越过主按钮（`o + wv/2 ≥ bw/2`，镜像同理）时与 `bodyRight - mainCenter` 恒等，稳定状态与拖动捕获行为不变；根已派生居中时目标精确等于 `baseFar`，映射恒等。

### 依赖顺序

帧内顺序为 `ApplyDisplayTransition`（`:13663`，更新 `displayTransitionActive`）→ `SubmitTargetsAndLayout` → `AdvanceAnimationsAndDeriveLayout`（`:6115` 派生写 `bottomDockCenteredRootApplied`，`:7223` 底栏映射）。F2 读取的两个标志在映射前已是本帧值。

## 4. 决策

- **D1 底栏重新展开的自动居中捕获**（`:7598-7616`）：用户 2026-11-19 同意按建议处理——作为程序性移动一并改为刚性滑到中心，不保留果冻。
- 以下为原始待决策说明：当前非拖动，按 F1 不再播种；按 F2 根由 `displayCenterX.SetTar` 过渡持有 → 刚性滑到中心。修改前这里会产生“远端先吸到中心、近端随后跟上”的果冻观感。

## 5. 风险与验证

- 风险 1：拖动中 DPI/显示切换仍走手势交接，行为不变；需确认 `presented.dragActive` 在松手首帧为 true，确保松手恢复不被误判为非手势（现有 `Bar.MultiTarget.Test.cpp` 的 `ReleaseAbsorbFailure` / rebase 用例覆盖）。
- 风险 2：dirty/viewport 预测在 Centered 非派生时按独立区间计算（`:11628` 已用相同门禁），映射恒等后预测只会更保守，不会漏脏区。
- 验证：新增回归用例——非拖动缩放变化、四种入口状态进入 PPT/白板、DPI 过渡，逐成功帧断言 `abs(scaleX − 1) ≤ 1e-6` 与端点等于布局端点；跑既有底栏/居中/工作区测试；`InkeysRepo.sln Debug|ARM64` 构建；实机复现截图场景。

## 6. 实施与审查记录（2026-11-19）

- 实现：Claude（F1/F2/F3 + 回归测试 + spec）。审查：Codex `gpt-6.2-sol`，`model_reasoning_effort=high`，`service_tier=fast`，`--sandbox read-only`（首次直连服务端重连失败，经本机代理重跑成功）。
- 基线（仅新增测试，未修复）：`--ui3-workspace-centering-test` 48 失败，缩放 `scaleX` 偏差 0.348、DPI 0.268；F1+F2 后仅剩换向退出 0.132（据此补 F3）；最终 0 失败。`--ui3-bottom-dock-transition-test` 19 个手势用例 0 失败。
- Codex 结论处理：
  - P1 场景接管与松手同帧仍按旧按住快照播种：成立，新增 `bottomDockProgrammaticHandoff`（接管时置位，交接元组一致或新手势按下时清除）。
  - P1 刚性缩放后沿用旧倍率内容外框：新增 `bottomDockHorizontalHandoffPending` 并入外框重收集条件（恢复修复前交接帧本就会触发的重收集）。反证实验（临时去掉该条件）中新加的视口包含检查未复现裁剪，判定为“未复现、防御性保留”。
  - P2 测试可沿用旧快照通过：成立，改为每次环境变化等待新成功帧，并检查视觉端点与布局端点、窗口覆盖（按显示器边界截断）。
  - P2 居中松手恢复约 0.3 s 内改倍率，恢复结束时有一帧宽度跳变：属实但触发窗口极窄，记为已知限制，未修。
  - 疑点（场景放置中途改倍率的收尾平移跳变；展开/换向中立即起拖捕获时 F3 改变收敛目标）：未实测，留待实机观察。
