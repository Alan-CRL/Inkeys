# UI3/激光相关 7 commit 二次审查

- 基线：H0 `8b156fca59f0337a6afc6d722941666fcf143080`；对照 2026-09-28 未提交工作树。
- 方法：逐项读取实际 diff，并追到当前 Bar 输入、动画/布局/绘制与 Draw3 光标调用链；未启动 GUI、编译或采样。
- 结论边界：7 项均已二次静态审查，未确认这些提交本身仍存严重缺陷。F-009 涉及 H0 的裸状态读写，当前未提交改动已修其已知路径；F-004 属共享动画时钟，F-025 属 Draw3 激光栅格事务，均不能错误归因到本批次。

| SHA | 当前代码映射与结论 | 仍需验证 |
| --- | --- | --- |
| `9064374` | 光标与激光笔宽/预览单位逻辑仍在 `Draw3.DrawingController.cpp`、`Bar.Layout.cppm`、`Bar.RenderLoop.cpp`；早期白芯/红壳过渡被后续提交替换。H0 Layout 直读状态，关联 F-009。 | 真 GUI 高 DPI、切换反向、光标与笔迹对齐 |
| `18619a4` | `Bar.Interaction.cpp` 的激光独立 hover/press/选中反馈仍在；H0 点击裸写早于本提交，当前改走 `ChangeStateModeToPenTool`，关联 F-009。 | 按下取消、关窗、外部模式接管 |
| `5f6201c` | 预览包络及端点插值已由 `Bar.Animation.cppm::ResolveBarLaserPreviewEnvelopeThickness/ResolveBarLaserPreviewLayerGeometry` 接替。 | 像素级动态过渡 |
| `a6bf819` | 纯状态 helper、笔型通用动画和标注入口逻辑保留；激光 phase 后被 `ea277bf` 修订。 | 真菜单命中与互切显示 |
| `ea277bf` | 进入/退出 phase、白芯混色及退场状态仍可追；刷子复用颜色问题由 `ae91403` 修复。 | 快速反向、DPI/宽度中途变化 |
| `ae91403` | 红壳后才获取白芯 brush 的顺序在当前 `Bar.RenderLoop.cpp` 保留；该历史颜色问题已修。 | D2D 像素回读或 GUI 芯色 |
| `cf05a3d` | `ResolveBarLaserPreviewLayerGeometry` 与绘制调用保留；`animation_tests.cpp` 有纯几何样本。 | 真 D2D 壳芯重叠与窄面板 |

复查命令：`git show --format= --no-ext-diff --no-renames <SHA> -- Inkeys/Inkeys`，`git diff -- Inkeys/Inkeys/UI/Bar Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp`，并用 `rg` 追上述 helper 与 `ChangeStateModeToPenTool` 调用点。
