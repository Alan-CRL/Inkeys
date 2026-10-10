# 独立激光笔粗细、笔型语义与 Bar 动画

## Goal

在 Draw3 产品路径中为 Laser 建立独立、真实可见的粗细和压感语义，并完善 SoftPen、HardPen、Highlighter 的工具边界；同时恢复粗细预览触摸跟手能力，以统一状态机消除 Bar 在笔型切换时的闪烁、跳变和残留交互状态。

## Requirements

- Laser 独立保存 `3/5/7 DIP` 三档宽度，默认 `5 DIP`，不得覆盖 SoftPen/HardPen 共用的 Brush1 宽度或 Highlighter 宽度。
- Laser 的 Draw3 实际笔迹与光标读取同一产品宽度；Pen 使用真实压力，Mouse/Touch 使用模拟压感。
- Laser 在 Bar 中保留细/中/粗三个纯色圆形快捷按钮，不显示 Slider、FineDial 或普通粗细调节入口。
- Laser 粗细预览使用无辉光的红色外壳和白色内芯，外宽与芯宽保持约 `3:1`，宽度按真实 DIP/DPI 比例绘制。
- 默认画笔语义为 SoftPen；HardPen 使用独立 Draw3 tool，固定半径、无压力变化、无实时笔锋，但继续使用预测，并与 SoftPen 共用 Brush1 粗细和颜色。
- Highlighter 支持 `35/50/70 DIP` 快捷预设和 `30-100 DIP` 连续调节；预览、实际笔迹和光标使用同一真实宽度。
- 在粗细预览区直接触摸拖动时，Move 阶段连续更新候选宽度、预览和数字；只在 Up 时提交 `SetPenWidth`，候选值不得提前取整。
- 粗细主预览始终保留 semantic 底层，Laser 红白层作为独立 overlay；Laser、SoftPen/HardPen 与 Highlighter 的几何、颜色、内外宽度切换可连续反向，不闪烁。
- 快捷按钮在 Circle/Number 切换时锁存 outgoing 内容：先淡出旧视觉，再淡入新视觉；Circle/Circle 允许直径 morph，重新打开 Highlighter 属性栏时直接进入稳定 Number 状态。
- SoftPen、HardPen、Highlighter 可显示笔型扩展入口；Laser 不可命中扩展入口，但已有扩展视觉应平滑退场。扩展入口锚点和颜色必须读取选中按钮的当前动画值。
- 选中 Laser 时隐藏其他笔型按钮的小三角和分割线；Laser 按钮具有与其他笔型按钮一致的 Hover 和 Press 缩放反馈，离开后不得残留灰色选中背景。

## Acceptance Criteria

- [x] `IdtState` 默认 SoftPen，并独立保存 Laser `3/5/7 DIP` 预设及默认 `5 DIP`；`GetPenWidth`/`SetPenWidth` 不污染其他笔型。
- [x] 产品 Draw3 将 HardPen、Highlighter、Laser 映射为独立工具；HardPen 固定宽度且无实时笔锋，Laser Pen/Mouse/Touch 分别使用真实/模拟压感并保留 prediction。
- [x] Highlighter 预设、连续范围、实时几何和光标均使用当前真实粗细。
- [x] 触摸预览拖动在 Move 阶段更新候选视觉，且仅在 Up 阶段提交真实粗细。
- [x] Laser 不开放 Slider/FineDial，三个快捷按钮以真实粗细圆形显示；普通笔圆形过大时可切换为数字语义。
- [x] 主预览的 semantic、Laser 红壳/白芯、Highlighter 圆角矩形在正向、反向和中途反向时保持连续，无端点闪回。
- [x] 快捷按钮 Circle/Number 使用两阶段交接并锁存 outgoing 内容，Circle/Circle 直径平滑变化。
- [x] Laser 按钮 Hover/Press 与其他按钮一致；Laser 选中时其他笔型的小三角、分割线及扩展命中立即失效，视觉平滑退场。
- [x] 近两天最终提交链经产品路径静态复核；被 revert 的 `5d0777b6` 不计入最终实现。
- [x] ARM64 Debug/Release 完整解决方案构建、无窗口动画测试及 `git diff --check` 通过，验证过程未启动可见窗口。

## Out Of Scope

- 实现标注线本身的绘制和硬笔自动切换行为；本轮仅提供入口、文字和状态边界。
- 重构整个 Bar UI、Draw3 架构或未涉及的绘图工具。
- 修改已归档的错误实现 commit 历史。

## Commit Record

- `5d0777b6`：首轮错误实现；`540cd8b9`：完整撤销该实现及其任务文件。
- `39c32be1`：正确重建 Laser 独立粗细和快捷预设。
- `9064374b`、`18619a41`、`5f6201c4`：修复 Laser/Highlighter 预览切换、按钮交互、小三角和内芯宽度动画。
- `97b90fba`、`6dccc597`：引入并修正 SoftPen/HardPen、Highlighter 动态粗细、Laser 压感、触摸预览提交语义和 Bar 状态机。
- `a6bf819c`：收敛 Bar 工具切换、快捷按钮和扩展入口动画。
- `9c9faeed`：仅更新 Trellis/hooks/scripts，不改变上述产品行为。
