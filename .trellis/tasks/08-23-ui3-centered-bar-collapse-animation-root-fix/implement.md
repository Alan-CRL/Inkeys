# UI3 居中底栏主栏收缩动画根节点修复 - Implementation Plan

## Implementation

1. [x] 创建并启动子任务，读取 native-desktop、C++、构建和共享思考规范。
2. [x] 分析用户录屏、上一提交和未提交修复，确认上一轮 restart/rebase 方案未通过真实设备验证。
3. [x] 在 `Bar.BottomDock.h` 增加稳定居中根节点所有权门禁和纯几何反推 helper。
4. [x] 在 `Bar.RenderLoop.cpp` 的动画推进后、下游绝对几何派生前，以当前 `mainBar.x/w` 和可见描边逐帧反推 `mainButton.x`，同步 `displayCenterX` 并保持原继承树。
5. [x] 删除 centered correction、两阶段 rebase、方向锁存和遗留换向清理状态及调用路径。
6. [x] 让预测 viewport 传播主栏动画 range 所反推的根节点 X range。
7. [x] 保留 `PositionUpdate()` 的居中/无效宽度门禁与普通按钮 hover 的同快照双轴逆映射。
8. [x] 删除证明旧架构的测试，新增根节点所有权、左右/描边/无效几何和 Draw → Selection 每帧中心不变量测试。
9. [x] 更新任务研究记录与 `native-desktop/rendering-and-ui.md`，明确旧根因假设已被真实设备结果否定。

## Validation

10. [x] 运行 `git diff --check` 并恢复所有原文件的 BOM/CRLF。
11. [x] 使用 ARM64-host MSBuild 构建完整 `InkeysRepo.sln` 的 `Debug | ARM64`，超时至少 5 分钟。
12. [x] 运行 `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window`。
13. [x] 运行 `trellis-check`，复核所有权矩阵、数据流、测试和最小改动范围。
14. [ ] 不启动可见 GUI、不创建 commit；交付维护者进行原录屏步骤的真实设备复测。

## Verification Results

- ARM64-host 完整构建 `InkeysRepo.sln` 的 `Debug | ARM64` 通过：0 errors；保留仓库既有 warning。
- `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window` 通过：`PASS animation correctness`。
- `trellis-check` 发现下游绝对几何可能落后一帧；根节点派生已提前到所有相关动画推进之后、任何下游几何派生之前，并完成复核。
- `git diff --check` 通过；修改文件保持 CRLF，且已确认不存在误写成单行文本的换行转义。
- 未启动可见 GUI，未创建 commit；原录屏步骤的真实设备视觉复测仍待维护者执行。

## Risks

- 静态和 Headless 测试只能证明每帧几何合同，不能替代真实 HWND/ULW 视觉复测。
- 若主栏 `x/w` 仍有独立的重复重启来源，根节点反推会保持中心但不能单独保证宽度按时收敛；验证结果必须分别报告中心不变量和动画进度。
- 触摸指示器坐标问题明确留在本任务范围外。
