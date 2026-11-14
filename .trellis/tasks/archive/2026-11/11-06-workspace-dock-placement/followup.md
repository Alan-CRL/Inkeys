# 遗留问题（留待以后）

**折叠状态进入 PPT/白板（开启自动居中底栏）时，进入动画不自然，并且出现脏区遗漏。**

- 来源：用户 2026-11-06 在 `fa57e5f5` 修复“展开与居中互相打架”之后反馈，仍留有动画观感与脏区问题，决定留给以后处理。
- 已知现状：进入矩阵（`--ui3-workspace-centering-test` 的 `RunEntryPolicyMatrix`）只断言进入动画期间根 X 无往返、最终居中、退出还原；不覆盖逐帧视觉观感，也不检查脏区。
- 线索（未验证）：
  - `Bar.RenderLoop.cpp` 接管分支置 `workspaceEntryRootTween` 后，根 X 由 0.4 秒放置动画持有，而主栏宽度动画与之并行；放置动画终点按最终宽度计算，宽度动画未必同时结束，结束处可能有一次接管跳变。
  - 接管同时改 `fold`、`mode`、`centerMode`、展开侧，`state.unclassifiedDamagePending` / `dirtyRegionTracker.ForceFullDamage()` 只在放置块里置位；折叠→展开时按钮从折叠终态展开的区域，以及根 X 滑动期间旧位置的残影，可能没有进入脏区。
  - 真实 ULW/DWM 下的脏区包络（`ResolveBarWindowAnimatedRect` 的预测范围）在 `workspaceEntryRootTween` 期间不再按“居中根范围”扩展（`Bar.RenderLoop.cpp` 预测包络处我把 farEdge 门禁置为该标志），需要核对预测包络是否仍覆盖 X 滑动范围。
- 建议下一步：用远离屏幕中心的折叠/悬浮窗做起点，在离屏 ULW 测试里逐帧比对“实际脏区 vs 全量重绘”差异，再决定是改动画编排（宽度与 X 同步结束）还是补脏区。
