# 设计：主栏 Redo 弹窗

## 本轮追加宽度修复边界

最小行为差距：Redo 2×2 点击没有新增主栏按钮，却出现暂态额外列。沿点击、PresetHoming/显隐、RenderLoop 占位与窗口尺寸链定位；只修改直接导致暂态布局的生产路径和必要回归，不改通用几何/动画参数或固定窗口宽度。延续中文关键注释、原编码换行及3秒保护。

## 本轮追加修复边界

- `RecallChromeIdleSeconds` 是 Undo/Redo 等待的唯一来源，改为 3.0；修改相关注释/既有边界测试。
- 属性按钮已有 TransitionContent 目标，核对并补齐 icon/name 的 AdvanceContentTransition；使用同一动画上下文，避免只改策略布尔值而遗漏真实显示提交。
- 本轮不涉及通用 hover/grace、构建超时、工程与依赖；保持此前所有未提交修改。

## 2026-10-12 修复边界（本节优先于下方历史设计）

- 行为差距在 Bar 的 RecallChrome 状态解析、命令发出前后时序、关闭入口和正常/禁用颜色目标；先核对现有 Host 可用性链，只在发现与本需求直接相关的断点时补齐。
- 预期修改 `Bar.RecallChrome.cppm`（锁存布局/目标与 5 秒策略）、`Bar.Button.cpp`（命令可用性快照、计时和关闭）、`Bar.Main.cppm`（必要状态声明）、`Bar.RenderLoop.cpp`（单一正常/禁用颜色目标）、`Bar.Scene.cpp`（既有交互 helper 接收该目标，避免再次覆写正常色）、`Bar.RecallAttribute.cpp`（必要弹窗身份/颜色处理）和既有 `recall_attribute_tests.cpp`。
- 布局事实和实时命令可用性分开；保持期内实时可用性只控制颜色与动作，不能通过 Resolve 或 dismiss 直接推导新尺寸/身份。点击前认可的命令不能被异步执行后的 availability 反过来取消 UI 点击反馈。
- 复用现有 steady_clock deadline、WM_TIMER 唤醒和 UI 颜色动画，不增加线程、持久化配置或新构建系统。主栏与弹窗各帧只提交一个正常/灰色颜色目标，避免同一帧反复重启相反动画。
- 仅补当前需求及必要回归，不重构无关按钮、绘制/历史、资源或窗口系统；保持原编码与换行。


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

## 主栏 2×1

- 纯状态在 `Bar.RecallChrome.cppm`：`Idle` / `Popup` / `Compact`。Undo 进入 Popup；任意收起或回到桌面从 Popup 进入 Compact；绘制进入 Idle。主栏 Redo 点击不改变相位。
- 5 秒计时只在 Popup 累加，到点走与其他收起相同的 Dismiss。
- `Load()` 把运行时 Redo 插在 Recall 前，加载后再套当前相位，避免配置把尺寸写回 2×2。
- 选择且空页隐藏 Recall 时，主栏 Redo 一并隐藏。

## 取舍

- 防抖与 Undo 关系：Recall 每次点击都应执行 Undo；若 `TryBeginToggle` 返回 false（300ms 内重复），仍执行 Undo，仅跳过面板状态重复置位，避免吞撤销。这是与 Draw/Eraser toggle 语义的不同点，需在代码注释说明。
- Motion 复用 vs 新建：首选抽通用模板；若改动面大则复制橡皮 Motion 为 `RecallSurfaceMotion` 并保持一致，接受短期重复，换取零回归风险。
- 单按钮面板宽度：保持与橡皮一致的留白/裁剪语义（窄区裁剪不压缩），避免引入紧凑布局分支。

## 兼容与回滚

- 新增状态默认为 false，旧行为（无弹窗）即关闭态；回滚只需 revert 本任务文件。
- Draw3 侧无改动，无持久化/协议变更。
