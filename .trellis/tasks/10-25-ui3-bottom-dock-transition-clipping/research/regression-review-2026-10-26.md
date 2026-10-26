# 2026-10-26 用户实测回归与窗口闪烁调查

## 范围与结论分级

- 当前 HEAD：`a7dc3a08 fix(ui): anchor bottom dock capture and drag geometry`。用户实测确认：吸附果冻时上端向上移动（回归），闪烁仍存在；录屏较难复现，怀疑窗口尺寸变化。
- 本次只调查，不修改产品代码。结论来自当前代码、提交历史、现有逐帧 CSV 和用户新增设计约束。未再启动 GUI，因此真实 DWM 闪烁的直接光学原因仍需最终实机确认。
- 核心判断：**上端回归已能由代码直接证明；窗口尺寸变化也真实存在，并是闪烁的高度可疑触发条件，但不能在未记录真实 DWM 帧前宣称它是唯一原因。**

## 1. 上端回归：新 anchored 映射违反了原设计

### 用户明确的硬约束

进入吸附果冻时，主栏可见顶边应保持其原有位置（只允许正常指针带来的整体平移），底边独立向任务栏目标恢复。顶边不能因底端果冻而反向上升。

### 当前公式的直接反例

`Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h:896-946` 的 `ResolveBarBottomDockAnchoredVerticalMapping` 在 Docked (`pinBottom=true`) 时同时满足逻辑抓点和底端：

```text
height = (desiredBottomDip - desiredGrabDip) / (1 - ratio)
top    = desiredGrabDip - ratio * height
bottom = top + height
```

当抓点和 desiredGrab 不变、底端向下移动时，非中心 `ratio` 会使顶边向上。例：`baseTop=0, baseBottom=80, ratio=0.5, grab=40, bottom=100`，当前公式得到 `height=120, top=-20`；用户要求 `top=0`。

`git log -S ResolveBarBottomDockAnchoredVerticalMapping` 仅命中 `a7dc3a08`，`git blame` 也全部由该提交引入，因此这是本轮修复新增的行为，不是用户原本就接受的设计。

### 原映射为何不同

仍在同文件 `:1574-1604` 的旧 `ResolveBarBottomDockVerticalMapping` 使用：

```text
visualTop    = baseTop + elasticOffset
visualBottom = baseBottom + captureBottomOffset
```

普通偏移和底端偏移是独立自由度；底端恢复不需要反推并移动顶边。新 anchored 分支在 `Bar.RenderLoop.cpp:7057-7060` 取代了这条几何语义，并把结果用于实际 `bottomDockMapping`（`:7426-7435`）。

### 现有 CSV 的直接现象

`TestResults/ui3-bottom-dock/samples.csv`：

- `right-100` 在 `RecaptureFailure` 前窗口高度为 169，失败后为 191；`target_h` 始终为 946。
- `RecaptureRetry` 顶/底为 `950.394 / 1032.88`；随后 `Recapture` 为 `950.112 / 1033.29`、`949.593 / 1034.05`、`948.965 / 1034.96`。
- 即顶边连续向上、底边继续向下，而 window_h/target_h 已稳定。这证明顶边回归不是单纯的窗口扩窗副作用，而是内容映射本身错误。
- 同类捕获序列在 150% 下出现单帧顶边约 `-0.976 px` 的上移。其他场景可能有不同幅度，不能以现有小样本限制用户实测幅度。

## 2. 测试也写错了验收约束

`Inkeys/Inkeys/UI/Bar/Bar.MultiTarget.Test.cpp:689-699` 对拖动帧直接要求 `actualGrab == pointer`，`:716-722` 又要求整幅形状随 `effectiveShift` 平移。它把“真实局部抓点必须始终跟手”当作绝对不变量，却没有检查用户刚明确指出的顶边所有权。

因此此前实现和测试形成了自洽但错误的闭环：

1. 实现为了维持抓点和底端而反推顶边；
2. 测试认可该抓点约束，不检查顶边是否上升；
3. source/dirty/位置辅助断言全绿，但用户看到的设计回归仍发生。

后续不能只修实现而保留该验收。必须先把“顶边不动、底边独立果冻”写为独立产品不变量，再决定真实抓点在每类动画中的允许行为。

## 3. 闪烁：窗口 viewport/source 确实会在捕获时改变

### 代码触发点

`Bar.RenderLoop.cpp:8037-8045` 以 `maximumIndicatorScale` 计算提示峰值包络，但 predicted viewport 仅在 `:9405-9410` 的 `maximumIndicatorScale > 0.000001` 时包含它。按下后、尚未建立提示资格时，完整 Back 峰值不一定进入预留范围；捕获提示出现后又可能扩大 viewport。

`git blame` 显示该判断源自 2026-08-21 的 `f41fd3e2` / `ea7da650`，不是 `a7dc3a08` 新增。这解释了为什么多轮内容/弹簧修复仍可能不消除原始闪烁。

### CSV 证据

- `right-100` 首次 Recapture 相关阶段 `window_h` 从 169 增至 191（+22），`source_y` 同步减少 22，`target_h=946` 未变。
- 其他案例首次捕获有 `+10/+13/+16/+25/+30/+36` 等窗口高度变化，backing target 尺寸在同 case 内保持不变。
- 因此至少有一种真实的 `pptDst/psize/pptSrc` 变化发生在捕获瞬间。它更像 viewport/ULW 窗口更新风险，而不是 backing bitmap 重建风险。
- 既有测试只验证 source/target 合法、dirty 合法和位置辅助量，没有验证整个手势的窗口尺寸/source 是否稳定，也没有验证真实 DWM 光学帧不闪。

这支持用户“窗口大小改变导致闪烁”的怀疑，但还不能排除旧位图直移、DWM 合成或其他光学因素。需要以修复后的真实窗口逐帧结果鉴别。

## 4. “按下时预扩窗”是否可行

**可行且有直接针对性，但只能作为呈现稳定措施，不能替代顶边修复。**

现有 `BarWindowViewportController` 已支持“动画期间只扩张、idle 才收缩”（`Bar.WindowGeometry.h:414-484`），所以无需新造窗口模型。需要补的是**同一次手势从 Down 开始的完整、保守包络**：

1. 在 Down 后第一次完整呈现和第一次直移之前，预留可能用到的完整吸附/居中/果冻范围。
2. 即使当前提示资格尚未建立，也预留提示 Back 峰值、描边、Gaussian 和 AA；不能等 `maximumIndicatorScale>0` 才加入。
3. 同步覆盖 viewport 和 backing capacity，避免窗口尺寸变化与位图重建任一项落在捕获瞬间。容量仍保持既有高水位和保守增长，不能无限扩到整屏。
4. 手势期间不缩窗；只有出现预测外几何才允许安全扩张并整窗替换。松手后仍按现有 idle 规则收敛。
5. 必须保留透明外缘 `HTTRANSPARENT`、Down 时冻结的输入坐标和命中身份，避免扩窗改变点击/落笔归属。

如果按下扩容本身也产生可感知跳变，可以把同一份保守包络前移到 hover/idle，而不是继续扩大尺寸或接受捕获时闪烁。

## 5. 建议的修复顺序

### A. 先恢复顶边设计不变量

- 将“顶边只允许正常整体平移，底边独立果冻”写入 PRD 和测试，不能用 `actualGrab == pointer` 替代。
- 审查 `a7dc3a08` 的 anchored Docked 分支，恢复顶边与底端独立的所有权；不要为了维持错误抓点约束继续反推顶边。
- q0/base 信息若仍用于输入、hit 或交接可保留，但不得让它改变 Docked 顶边设计。保留此前有效的 direct barrier、吸收、容量和其他修复。

### B. 将 Down 预扩窗作为独立呈现修复

- 在真实第一次吸附前完成完整手势包络与必要容量预留，避免捕获时 `window_w/h` 和 `pptSrc` 改变。
- 该变更必须可独立回退，不能用“窗口已扩大”来掩盖顶边或其他内容错误。

### C. 新验收必须同时通过

- 每个捕获/果冻成功帧都满足顶边设计；不能只检查高度、抓点或终态。
- 从 Down 到捕获、重捕获、脱离的整个手势，除真实 DPI/显示环境变化外，`window_w/h`、`pptSrc` 和 backing target 不得发生意外变化；`pptDst` 只能有正常输入位移。
- 记录每帧 ULW/真实 HWND、透明像素/非空像素和 source/target，证明不是只把几何错误隐藏。
- 完整 ARM64 Solution、完整 no-window、两个离屏入口及 full-scope 质量检查仍需通过。
- 最后必须由用户做真实拖动和闪烁复测；离屏通过不得再直接升级为“修复完成”。

## 当前状态

- `a7dc3a08` 的上端回归已确认，原闪烁也仍有未处理的 viewport 触发点。
- 父/吸附任务的软件状态改为 `historical_tests_passed_symptom_unresolved`，真实视觉标记为 `authorized_pending_live_reproduction`；保持 `in_progress`，不归档。
- 另一路原生可见窗口重放记录 `research/live-reproduction-2026-10-26.md` 尚未确认原始剧烈闪烁复现，并明确要求观察到相同症状前禁止产品修复。上述修复顺序仅作为该复现门解除后的候选方案，当前不进入实施。
- 本调查未修改产品代码，仅新增研究记录与后续任务约束。
