# 生产 PageControl 六向切换 unlimited 离屏采样

2026-10-28；当前 `bugfix/mode`，仅增加默认关闭的测试计时与隐藏窗口性能驱动，未改变产品窗口提交行为。

## 方法与口径

- 完整 `InkeysRepo.sln Debug|ARM64` 构建成功，0 error；既有 hashlib++ 三条 C4267 警告保持。
- 测试 EXE SHA-256：`EA7B51CAA0423037568A432CA1222D3BC0DA8EDC98B421F5797CAD64FE2DEBFA`。
- 复用 `--page-control-hidden-test` 的四个屏外真实 HWND、生产 `PageControl::RenderSurface`、真实 ULW 与 Window Service owner/回执，坐标位于 `-30000,-30000`；不是 `CreatePageControlOffscreenRenderCallback`。
- `INKEYS_PAGE_CONTROL_TRANSITION_PERF=1` 时直接顺序驱动四个生产 surface，不经过 60 FPS Scheduler 等待。每次转场等待全部 surface 连续两帧 Idle，六向三轮，共 818 个四窗口批次、3272 个 surface 回调，18 次转场均完整结束。
- 帧内只保存固定数值，停止计时后才格式化 CSV。`unlimited_fps=1000/batch_ms`；阶段占比以全部 surface 的 RenderSurface 总耗时为分母。`visibility_ms` 是 `Show/Hide` 总耗时，包含 PPT `RefreshOverlayGroup`；`other_ms` 是总耗时减去 PresentScene/SetBounds/visibility。
- 原始数据：`TestResults/ui3-workspace-transition/native-pagecontrol-unlimited/{frames.csv,summary.csv,metadata.json}`。

## 结果

| 方向 | 批次数 | batch P50 ms | batch P95 ms | FPS P50 | FPS P05（低尾） |
| --- | ---: | ---: | ---: | ---: | ---: |
| DesktopToPpt | 139 | 8.60 | 11.51 | 116.3 | 86.9 |
| PptToDesktop | 136 | 8.59 | 12.17 | 116.4 | 82.2 |
| DesktopToWhiteboard | 134 | 9.07 | 12.37 | 110.3 | 80.9 |
| WhiteboardToPpt | 135 | 8.99 | 11.26 | 111.3 | 88.8 |
| PptToWhiteboard | 136 | 8.95 | 11.77 | 111.8 | 85.0 |
| WhiteboardToDesktop | 138 | 8.81 | 11.78 | 113.5 | 84.9 |

全部 3272 个 surface 回调的阶段占比：

| 阶段 | 总 ms | 占 RenderSurface 总耗时 |
| --- | ---: | ---: |
| PresentScene | 1733.64 | 23.53% |
| SetBounds | 171.82 | 2.33% |
| Show/Hide（含 RefreshOverlayGroup） | 4837.30 | 65.66% |
| 其它布局/锁/提交工作 | 624.72 | 8.48% |

单 surface 最慢样本为 24.63ms，其中 Show/Hide 23.30ms。六方向没有显示只有白板相关方向才承担该成本。

## 结论与限制

这套真实生产 PageControl 离屏样本支持：`Show/Hide` 是 RenderSurface 内部最大耗时项，优先于 SetBounds 和 PresentScene；后续 A/B 应先分离幂等可见性提交与真正的 PPT promote/Z-order 重排。

但 unlimited FPS 中位数仍约 110–116，低尾约 81–89，未复现用户现场“只剩几帧”。因此不能宣称该函数已单独解释完整卡顿，也不能把屏外数据当成真实 DWM 帧率。隐藏 fixture 的 Drawpad/Freeze/Bar 使用 `DefWindowProcW`，未包含真实 Bar 渲染、Draw3 消息响应、原生 workspace coordinator/Freeze 以及实际可见窗口 DWM 合成；完整产品可能在这些边界继续放大同一窗口命令链。

默认 `--page-control-hidden-test` 复跑 `failures=0`。`git diff --check` 通过；只有 `PageControl.cpp` 的测试计时/性能驱动变更，无产品行为修复或提交。
