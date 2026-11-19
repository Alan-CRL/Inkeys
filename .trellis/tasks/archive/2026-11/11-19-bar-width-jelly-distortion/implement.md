# 实施计划

前置：用户确认 `design.md` 的 F1/F2 与待决策 D1 后再 `task.py start`。

## 步骤

1. 回归测试先行（`Bar.MultiTarget.Test.cpp` / `InkeysHeadlessTests`，按既有夹具就近添加）：
   - 非拖动悬浮/底栏/居中底栏，缩放倍率连续变化（含 0.5↔2.0 往返）。
   - `--ui3-workspace-centering-test` 进入矩阵：悬浮、悬浮折叠、底栏、底栏折叠 × 左右方向，进入与退出。
   - 非拖动 DPI/显示器过渡。
   - 断言：每个成功帧 `abs(horizontalMapping.scaleX - 1) <= 1e-6`，`visualLeft/Right` 等于布局端点；根 X 无往返。
   - 先确认新用例在当前基线失败（证明复现）。
2. F1：`Bar.RenderLoop.cpp:7619` 后新增 `gestureHorizontalHandoff`，替换 `farTargetDip`、播种块与两处弹簧推进门禁中的 `horizontalHandoff`。
3. F2：`Bar.RenderLoop.cpp:7632` 居中远端目标增加 `rootOwnedByTween` 门禁。
4. 按 D1 结论决定是否为自动居中捕获单独豁免。
5. 验证：
   - `InkeysRepo.sln`，`Debug | ARM64`，ARM64 原生 MSBuild（超时 ≥ 5 分钟）。
   - `--ui3-bottom-dock-transition-test`、`--ui3-workspace-centering-test`、`InkeysHeadlessTests`（`bar_bottom_dock_tests`、`bar_display_transition_tests`）。
   - 自检参数全等匹配，用 `& exe <flag> | Out-File` 启动，避免误启动产品。
6. Spec：`rendering-and-ui.md` 底栏水平合同补一条“像素级水平交接仅限手势；程序性根动画期间远端刚性跟随”。
7. 实机：用户复现截图中两种场景；同时复测 `11-06` followup 的进入动画观感。

## 回滚点

- F1、F2 互相独立，可分别回退；测试与 spec 随对应修改回退。
