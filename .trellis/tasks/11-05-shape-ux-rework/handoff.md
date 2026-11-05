# 交接：图形交互与样式整改（检查点 2026-11-05）

> 用户设的检查点：完成 edit-performance 后停下，**尚未开始** edit-interaction 和 edit-visual。下次继续时先读本文，再读 `prd.md`、`design.md`、`implement.md`。

## 当前状态

- 分支 `feature/shape`，HEAD `4d00bf11`，工作区干净，未推送。
- 总进度约 30%，按工作量加权（规划 5、图标 5、性能 15 已完成；交互 25、样式 25、分类 10、提示 10、收尾 5 未开始）。

| 子任务 | 状态 | 提交 |
|---|---|---|
| 规划（6 个子任务的 PRD、设计、调研） | 完成 | `238f3afb` |
| 11-05-shape-ux-edit-performance | **completed**（独立检查通过） | `4d00bf11` |
| 11-05-shape-ux-icons | 资源已替换并通过自动测试，仍是 planning | `a58400aa` |
| 11-05-shape-ux-edit-interaction | 未开始（planning） | — |
| 11-05-shape-ux-edit-visual | 未开始（planning） | — |
| 11-05-shape-ux-toolbar-category | 未开始（planning） | — |
| 11-05-shape-ux-construction-prompt | 未开始（planning） | — |

本任务之外的相关提交：
- `e4d33796`：合并数据兼容修复，包括高 DPI 旧矩形导入、白板孤儿笔迹、SelectionOverlay 启动失败。
- `e2f33d86`：P7 套索与 Z 序中间移动调研归档，位于 `10-17-shape-phase7-ink-selection/research/`。

## 待用户实机验收（离屏测试无法覆盖）

- edit-performance：笔/触屏拖动控制点和旋转钮的手感，以及松手后不出现追赶。
  - 需要实机验证的操作：两指平移、真实 PPT 放映按键（包括只推进动画的按键）、最小化、设备丢失时的落定。
  - 日志：`[Draw3.ShapeEdit] action=gesture_end …`（延迟）、`action=focus_layer|materialize result=fallback`（回退）。
- icons：在深浅主题、1.0/1.5/2.0 倍率下，选中态变色是否正常、立体图标的虚线是否清晰；确认后把 icons 子任务标记为完成。

## 下次继续的顺序

1. `task.py start .trellis/tasks/11-05-shape-ux-edit-interaction`，派发 trellis-implement。需求见该子任务的 prd.md（G1–G5、逐图形表、辅助显示清单、inkeysShape v4），设计见父任务 design.md §3。
2. edit-visual（design §4）。重写 `Bar.ShapeEditor.cpp` 时必须保留：本地跟手、`NeedsPresent`、拖动期间窗口只增不减（spec `shapes.md`「编辑焦点层与预览合并」）。同时把该文件改成常规格式（不再一行写多条语句）。
3. toolbar-category（design §5）。参数行中的辅助开关依赖第 1 步的数据模型。
4. construction-prompt（design §6）。新增 Forward。
5. 收尾：更新 rendering-and-ui.md 和 bar-theme-material.md，完整构建并跑全部回归。

## 注意事项与遗留

- 顺序必须串行：交互、样式、提示都会改 `Bar.ShapeEditor.cpp` 和 `shape_interaction`。
- 已知的旧问题（与本任务无关，需单独调查）：`--draw3-hidden-test` 在 HEAD 上就有约 50 项失败，首个失败是 DComp 模式下的普通笔接触；不在基线回归列表中。
- 计数细节：`CanvasFocusEndReason::PresentationAction/WindowHidden` 已声明但未使用，PPT 操作和最小化目前计入 Escape。
- 性能还能继续优化：WARP 上每帧约 26–39 ms，主要花在清除整幅 4K L1 上，可以改为只清脏区（未做）。
- 未来 P7（套索）前要先完成的工程项：多对象排除、可配置的缓存预算和无缓存降级、全局唯一的 generation；擦除语义还没有定论。详见 `10-17-shape-phase7-ink-selection/research/user-decisions-and-notes.md`。
- 环境：Windows ARM64，使用 vswhere 定位 arm64 MSBuild；构建日志是 UTF-16，需要用 iconv 转换。基线回归命令见 implement.md 和 edit-performance 的 validation.md。
