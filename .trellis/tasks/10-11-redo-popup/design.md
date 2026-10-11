# 设计：主栏 Redo 弹窗

## 边界

- 修改包：`Inkeys.UI.Bar`（UI3 主栏唯一入口），只读消费 `Inkeys.Drawing.Draw3` 现有 Redo 命令。
- 不做：Draw3 新 API、Undo/Redo 变灰态、一次到最新的循环、IdtFloating、配置字段新增。

## 现状差距

- 现状：主栏 Recall 只有 `clickFunc` 发 `Undo`，无属性面板；`BarButtonPresetEnum::Redo` 存在但无 UI 接线。
- 目标：新增 `recallAttribute` 面板，单 70x70 Redo 按钮，行为对齐橡皮属性。

## 方案

### 状态与防抖

- `Bar.State.cppm`：`BarStateClass` 新增 `IdtAtomic<bool> recallAttribute = false`。
- `Bar.ToggleClickCoalescer.cppm`：`BarToggleChannel` 新增 `RecallAttribute`（300ms 合并窗，沿用现有机制）。

### 面板（镜像橡皮，最小单按钮）

- 新文件：
  - `Inkeys/Inkeys/UI/Bar/Bar.RecallAttributeLayout.cppm`：`ResolveRecallAttributeLayout`，单按钮布局；高=主栏高，宽=单 70+标准 gap，锚定 Recall 按钮中心，上下边/工作区 clamp/`panelGap=10`/`lockPanelSide` 抄橡皮。
  - `Inkeys/Inkeys/UI/Bar/Bar.RecallAttribute.cpp`（+必要 `.cppm` 声明）：`BarRecallAttributePanel`，`Initialize/Advance/Draw/Pointer/Close/Execute/Keyboard` 对齐 `BarEraserAttributePanel`。
  - Motion：优先复用 `EraserSurfaceMotion` 逻辑；若命名耦合强则新建 `RecallSurfaceMotion` 保持逐行一致，或抽通用 `AttributeSurfaceMotion` 模板。曲线/时长/压缩宽/父加入/换边与橡皮逐项一致。
- `Bar.Main.cppm`：`BarUISetClass` 新增 `recallAttribute` 成员；`BarUISetShapeEnum` 如需新增 shape 枚举则对齐 Draw/Geometry 命名。

### 按钮

- 弹窗内 `BarButtonClass`：`size=twoTwo`，`preset=Redo`，`StandardTwoTwo` metrics，Press/Release/Hover 复用主栏同一函数。
- 资源：新增 `barRedo` SVG（对齐 `barRecall` 主题占位色）；i18n `Inkeys/src/i18n/zh-CN.jsonc`（及 en-US 等）加 `UI.Bar.MainButtons.RedoLabel`，经 `i18n.ps1 sync/check` 维护。

### 接线

- `Bar.Button.cpp` Recall `clickFunc`：
  1. `PublishProductCommand(Undo)`
  2. `TryBeginToggle(RecallAttribute)` 防抖（注意：防抖失败也不应吞掉 Undo，见取舍）
  3. `CollapseAuxiliaryPanels()` 后置 `barState.recallAttribute = true`（已打开仍 true）
- 弹窗 Redo `clickFunc`：`PublishProductCommand(Redo)`，不关闭面板。
- `Bar.Interaction.cpp`：
  - `CollapseAuxiliaryPanels()` 加 `recallAttribute.Close` + `recallAttribute=false`
  - `Run()` 前置 `recallAttribute.Pointer()`（与 eraser 同级）
  - `HandleCanvasDrawingActivity` 已调 collapse，无需新增
  - Escape：先关 recall（对齐 eraser `Keyboard()`）
- `Bar.RenderLoop.cpp`：`Advance` 驱动 motion；`PresetHoming`/fold/模式切换清 `recallAttribute`。

## 数据流

```text
Recall主栏点击 -> Undo命令 -> Draw3 undoCurrentPage -> UndoLastVisible (+redoItems_.push)
              -> barState.recallAttribute=true -> RecallSurfaceMotion -> 弹窗展开
Redo弹窗点击 -> Redo命令 -> Draw3 redoCurrentPage -> LastRedoItem/RedoLastUndone
              -> RestoreComposition(冷)/直接重绘(热) -> 保持展开
开始绘制/其他按钮/fold/Escape -> CollapseAuxiliaryPanels -> recallAttribute=false -> 收起动画
```

## 取舍

- 防抖与 Undo 关系：Recall 每次点击都应执行 Undo；若 `TryBeginToggle` 返回 false（300ms 内重复），仍执行 Undo，仅跳过面板状态重复置位，避免吞撤销。这是与 Draw/Eraser toggle 语义的不同点，需在代码注释说明。
- Motion 复用 vs 新建：首选抽通用模板；若改动面大则复制橡皮 Motion 为 `RecallSurfaceMotion` 并保持一致，接受短期重复，换取零回归风险。
- 单按钮面板宽度：保持与橡皮一致的留白/裁剪语义（窄区裁剪不压缩），避免引入紧凑布局分支。

## 兼容与回滚

- 新增状态默认为 false，旧行为（无弹窗）即关闭态；回滚只需 revert 本任务文件。
- Draw3 侧无改动，无持久化/协议变更。
