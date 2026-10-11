# 主栏 Redo 弹窗

## Goal

主栏撤回属性弹窗内置70x70 Redo按钮，复用橡皮动画与主栏按钮体系，单步重做可到最新。

用户原始需求：
0. 开启 trellis 任务
1. 按钮位于撤回属性中，类似橡皮属性一样，当用户点击了撤回按钮时，撤回属性会自动弹出，并且内置一个 70*70 的 Redo 按钮
2. 弹出窗口的弹出动画和收起动画和绘制属性、橡皮属性一致，包括回弹数值以及速度等
3. 里面的 70*70 按钮效果和主栏无异，包括相同的动画、交互、样式等
4. 按下 redo 后调用 draw3 的合成树等进行重做，可以重做到最新位置

## Requirements

### R1 撤回属性弹窗
- 点击主栏「撤回」按钮：执行一次 Undo，同时展开并显示撤回属性弹窗。
- 弹窗内只放一个 70x70 DIP 的 Redo 按钮（`BarButtonSizeEnum::twoTwo` / `BarButtonTwoSideDip`）。
- 弹窗锚定主栏 Recall 按钮中心，高度与主栏一致，上下边、工作区 clamp、10 DIP 间隙对齐橡皮属性。

### R2 动画一致
- 弹出/收起动画与绘制属性、橡皮属性一致：
  - 时长 `BarUiDefaultOperationDur = 0.4s`
  - 几何 `EaseOutBack`（展开）/ `EaseInBack`（收起），`back = 1.1`
  - 透明度 `EaseOutSine` / `EaseInSine`
  - 压缩锚点宽 60 DIP（`BarDrawAttributeCompactWidth`）
  - 父时间线 50% 内加入、换边 collapse/expand 各半
- 不引入新的弹簧/时长/曲线常量；复用现有 Motion 逻辑。

### R3 按钮一致
- 弹窗内 Redo 按钮与主栏 70x70 按钮相同的样式、动画、交互：
  - `StandardTwoTwo` metrics：圆角 4、边框 1、图标 28（Y-10）、文字 13（Y+20）
  - `pressScale 0.95`，Press `EaseOutCubic` / Release `EaseOutBack`
  - hover 0.24s 过渡、5s 衰减
- 复用 `BarButtonPresetEnum::Redo`；新增 `barRedo` 图标资源与 i18n `RedoLabel`。

### R4 重做接线
- Redo 按钮每次点击发送一次 `PublishProductCommand(Bridge::CommandType::Redo)`。
- 单步语义：连续点击可回到最新（直到 `RedoDepth()==0`），不做一次到最新的循环。
- 复用 Draw3 现有链路：Host 映射、`redoCurrentPage`、`RedoLastUndone`、`CompositionRangeTree::DecomposeRange` / `RestoreComposition`；不新增 Draw3 API。
- 新笔画自动 `DiscardRedoBranch`，UI 不特殊处理。

### R5 显隐与互斥（已确认）
- 继续点击主栏 Undo 或弹窗 Redo：各执行一次命令，保持展开，不切换关闭。
- 收起条件：开始绘制（`HandleCanvasDrawingActivity`）、点击主栏其他按钮（`CollapseAuxiliaryPanels`）、折叠主栏、Escape。
- 与绘制属性/几何属性/橡皮属性/More 互斥；打开任一即关闭撤回属性。

### 约束
- 只改 UI3 Bar（`Inkeys.UI.Bar`），不碰 `IdtFloating` 旧代码。
- 新文件沿用相邻 module 名与目录结构，保持原文件 UTF-8 BOM + CRLF，不格式化无关代码。
- 中文简短注释关键步骤。

## Acceptance Criteria

- [ ] AC1：点击主栏撤回执行一次 Undo 且弹窗展开，弹窗内可见 70x70 Redo 按钮。
- [ ] AC2：连续点主栏 Undo 或弹窗 Redo，每次各执行一次命令，弹窗保持展开。
- [ ] AC3：开始绘制、点其他主栏按钮、折叠主栏、Escape 均可收起弹窗。
- [ ] AC4：弹窗动画参数与橡皮属性一致（0.4s、Back 1.1、Sine、60 DIP 压缩、换边同批次）。
- [ ] AC5：Redo 按钮视觉/按压/hover 与主栏 70x70 一致；图标与文案正常显示。
- [ ] AC6：每次 Redo 单步重做，连续点击可回到最新；新笔画后旧 Redo 失效符合 Draw3 语义。
- [ ] AC7：绘制属性/橡皮属性现有行为无回归；`InkeysHeadlessTests --no-window` 通过。

## Notes

- 轻量/复杂判定：跨 Bar 状态、布局、动画、交互、Draw3 接线，多文件多层，按复杂任务处理，附 `design.md` / `implement.md`。
- 变灰（Undo/Redo 可用态）为原 TODO 提及但本次未要求，不在本任务范围；如需后续单独立项。
