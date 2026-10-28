# UI3 居中底栏动态宽度居中修复 — 执行计划

> 2026-10-26 收尾授权：用户明确要求提交当前修复，并结束、归档本居中移动子任务；吸附闪动子任务继续保持in_progress。先提交代码及验证记录，再单独归档提交。真实PPT GUI未执行的验证边界保留，不将关闭任务视为新增人工验证。

> 2026-10-26 当前补充授权：用户要求先恢复退出PPT时缺失的竖直返回动画。范围为RenderLoop的桌面恢复Y（从最后成功位置按既有0.4秒曲线恢复，居中X继续逐帧派生）和同一路径的离屏动画连续性断言；快速退出再进入须保留原桌面基线。复用既有场景动画标记，不改拖动固定表面、松手交接、曲线、线程、项目配置；不新建任务、不提交、不结束任务，不启动真实GUI。

> 2026-10-26 接力：本计划已获本会话授权并完成代码与自动化，最新证据见父任务 `research/final-integration-validation.md`；原文未来式保留作历史设计。GUI/真实系统呈现验收待授权，任务未归档。

> 后续审批后实施。本轮只完成调查与规划，不修改产品代码。

## 实施步骤

1. **先建立真实生产链失败回归**
   - 复用 `CreateBarOffscreenRenderCallback` 与 `Inkeys/Inkeys/UI/Bar/Bar.MultiTarget.Test.cpp` 的现有离屏 fixture 模式，在现有 UI3 presentation harness 增加 `--ui3-workspace-centering-test` 场景入口；不新建平行测试框架，不改变 Release 禁止内部 harness 参数的边界。
   - 初始化展开、稳定的 Centered BottomDocked 和已知单屏，通过 `PublishPptSession` / `SetWhiteboardActive` 驱动场景，以受控 `frameTime` 逐帧推进，并用 Draw↔Selection 或实际可见按钮制造宽度变化。
   - sink 记录 ULW tuple，只在成功呈现 serial 前进后采样 `visualBodyCenterScreenX`。先证明 Desktop→PPT/Whiteboard 与 PPT↔Whiteboard 至少一个中间成功帧偏心；Leave 单独记录，不能与 Switch 合并判定。

2. **补辅助门禁与范围回归**
   - 在 `InkeysHeadlessTests/bar_bottom_dock_tests.cpp:1165-1235` 保留普通 display transition 不得被接管的负向用例，新增居中 workspace placement 的正向用例。
   - 使用生产根位置/根范围纯函数和真实门禁覆盖连续宽度、左右展开、描边及无效几何；证明实际根与 viewport 范围采用相同分类。纯 helper 测试不替代步骤 1。

3. **实现共享的窄门禁**
   - 在 `Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h` 为 `ShouldDeriveBarBottomDockCenteredRoot` 增加显式 workspace placement 输入，生产使用现有 `whiteboardDockAnimationActive`（同时覆盖 PPT/Whiteboard），不新增并行状态。
   - 在 `Bar.RenderLoop.cpp:5537-5588` 仅允许符合 BottomDocked、Centered、Stable、Expanded、无拖动/弹簧完整条件的 Enter/Switch 场景帧执行现有联合外框反推。
   - 保留 `ApplyDisplayTransition` 的 Y 动画和既有 Leave 路径；符合门禁的帧在主栏动画后覆盖根 X，现有 `displayCenterX.SetDirect` 终止独立 X 插值。Free、Floating、折叠和普通显示切换不变。
   - 不新增配置、动画曲线、补偿位移或并行 serial。

4. **同步 viewport 与呈现**
   - 在 `Bar.RenderLoop.cpp:9039-9084` 让根 X range 预测调用同一纯门禁，继续使用 `ResolveBarBottomDockCenteredRootRange`；禁止两处分别拼接场景布尔表达式。
   - 核对 root-layout full replacement、capacity/source、dirty、命中和失败重试都来自本帧最终根位置；保留 `pptSrc + psize` 位于 target 的断言。
   - 检查过渡到稳定态、快速反向和 Leave 没有第二套根位置或终点跳变。若 Leave 独立样本仍失败，返回研究定位其独立原因，不强行套用 Enter 修复。

5. **验证与审查**
   - 运行下述完整构建、新增真实 RenderLoop 场景入口和既有无窗口测试；检查修复前红、修复后绿的是同一生产场景。
   - 请求真实 GUI 逐帧验收授权后，覆盖 Desktop↔PPT、Desktop↔Whiteboard、PPT↔Whiteboard、最终 Leave 和快速反向。
   - 按父任务执行吸附组合场景集成审查；未做 GUI 时明确标记视觉未验证。

## 验证命令

后续实现新增场景入口后的标准验证：

```bash
"C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/arm64/MSBuild.exe" InkeysRepo.sln -p:Configuration=Debug -p:Platform=ARM64
./Build/ARM64/Debug/Inkeys.exe --ui3-workspace-centering-test
./Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window
```

完整 Solution 构建至少给予 5 分钟。`--ui3-workspace-centering-test` 已由本任务实现并用于修复前后验证，结果见 `research/implementation-validation.md`；完整 `--no-window` 和真实视觉验收仍按该记录接力。

## 必过场景

- Desktop→PPT/Whiteboard、PPT↔Whiteboard 的中间成功帧在宽度非线性变化与 Y 动画并行时仍逐帧联合居中；最终 Leave 单独满足同一可观察结果。
- 左展/右展、变宽/变窄、100%/150% DPI、负 monitor 原点、快速反向、Whiteboard 最小化/恢复和动画结束交接。
- Free 底栏、浮动、折叠、拖动、水平捕获/脱离弹簧和普通显示环境切换保持原所有权。
- 提示、Popup、命中、dirty、source/viewport 与真实 HWND 使用同帧根位置，无新增裁剪或残影。

## 回退点

- 若真实 RenderLoop 回归没有复现原缺陷，先修 fixture 的初始 Centered BottomDocked、宽度变化和场景 Y 条件，不改生产公式。
- 若居中场景分支影响普通显示切换、Leave 或拖动，回退共享门禁与根范围的窄改动，保留失败回归作为证据。
- 不用调整弹簧、关闭动画、固定宽度或补偿常量来规避回归。

## 2026-10-26 实际执行状态

- [x] 接力差异、所有权、DPI/范围与原始证据复核。
- [x] 本任务软件实现、能够先红后绿的回归及共享文件串行集成。
- [x] 当前 Debug|ARM64 Solution、完整 no-window、两个真实离屏入口及共用隐藏锚点的既有像素回归。
- [x] 全范围 trellis-check、CSV/编码/换行与最小变更范围复核、规范和上下文更新。
- [ ] 授权后真实 GUI/Office/WPS/ULW/DWM、多屏、白板最小化恢复与主观手感验收。

原规划清单保留历史；本节及父任务最终验证报告表示当前执行状态。未收到 GUI 授权，不 commit/push/archive。

## 2026-10-26 退出竖直动画补充

用户要求的Leave Y动画已恢复；完整ARM64构建、增强的workspace-centering及原bottom-dock离屏回归均通过。旧基线12条退出序列dt=0首帧瞬移，新18条普通退出序列均23个Y移动帧且首帧Y差0，联合居中误差为浮点舍入量级；快速重新进入与动画关闭覆盖通过。详见 `research/workspace-return-animation-2026-10-26.md`。未启GUI，未提交，不结束任务。
