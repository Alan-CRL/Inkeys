# 设计：开合式面板字号取整目标统一取展开态

## 1. 问题回顾

调查结论见 `research/root-cause-claude.md`（Codex 交叉核对见 `research/codex-sol-report.md`）。一句话：字号整数像素修正系数 `c = floor(T)/T` 的静止目标 `T` 取的是“开合进度目标”的姿态，收起时就是紧凑倍率（≈0.16），`c` 掉到 0.5–0.8，再用 `EaseInOutCubic` 在两端之间插值；面板用 Back 曲线，于是展开到位后字继续放大、收起初段字偏小。

## 2. 决策

**开合式面板里的字号取整目标，一律取面板“展开态姿态”（几何进度 = 1，含父级组合），与开合方向、当前进度、进度目标 `.tar` 都无关。**

效果：

- 一次开合里 `T` 恒定，`c` 恒定，实际字号 = 原始轨迹（`nominal × 当前 pose × 当前 zoom`）× 常数，全程严格等比。
- 展开静止时 `T` 就是展开态字号，依然落在整数像素，保留 10-24 字号取整任务的目的。
- 收起后面板不可见，`c` 停在展开态的值；再次打开没有追赶尾巴（与现有 `T = 0` 保持语义一致）。
- 主栏缩放变化（`targetZoom` 变化）仍按原有修正动画过渡，行为不变。

### 不选的方案

- **只同步曲线**（让 `c` 用面板曲线）：只能同步，不能消除收起端 30%–50% 的偏小。
- **在 `UiFontCorrectionClass` 里加像素阈值**：通用但阈值靠经验，且会影响所有文字，不如在“名义目标怎么取”这一层修正。

## 3. 统一范围

| 面板 | 宿主 | 改动点 |
|---|---|---|
| More 面板 | 真实主栏 + 组件页预览（共用 `PositionContentMore`） | `Bar.ContentInstance.cpp:98-99` `fontTargetScale` 改为展开态倍率 |
| 橡皮 / 撤回 / 几何属性窗、Scene 面板菜单 | 真实主栏（Scene 面板同结构） | `Bar.EraserAttributeMotion.cppm` `BarAttributePanelFrame::targetPose` 改为展开态姿态 `expandedPose`（几何进度 1，父级同样组合 `expandedPose`），使用方随改名 |
| 绘制属性窗 | 真实主栏 | `Bar.RenderLoop.cpp:5799` `fontPanelTargetScale` 改为展开态；`:4287`（笔型 12）、`:4408`（粗细显示 13）、`:4469`（档位数字 10）在设置 `size` 目标处同时设 `fontTargetSize = 基准字号 × 展开态倍率` |
| 颜色选择器 | 真实主栏 | `UI3.ColorPicker.cpp:409` `fontPickerTargetScale` 改为展开态（`hostOpacity` 分支保持 1） |

- 组件页预览没有属性窗（点画笔只切换工具模式），它的 More 与真实主栏走同一个函数，改一处两边同时生效；不需要为预览单独写分支。
- `targetPose` 目前只用于字号目标（以及 `Bar.Scene.cpp:229` 的 trace 字段），改名不影响几何、命中和脏区。trace 字段语义随之变为展开态倍率，在 spec 中注明。
- 收起到 0 的弹窗（粗细预览弹窗、底栏指示等 `FontTargetScope(progress.tar)`）走 `T = 0` 保持分支，本来就正确，不改。

## 4. 面向后续“用户展开倍率”的约定

后续属性窗口 / More 窗口会在主栏缩放之外叠加一个用户设定的展开倍率 `m`。本任务不实现 `m`，但把落点固定下来：

- **`m` 必须乘在“展开态姿态”上**（展开时几何与字号同源），不能作为额外的 DC 变换叠加。这样字号目标自动包含 `m`，开合全程仍是等比，静止展开字号仍是整数像素。
- 本任务把各面板的展开态倍率收敛到上表的固定位置：
  - 属性窗系列：`ResolveBarAttributePanelFrame` 里求 `expandedPose` 的那一处（届时 `ResolveEraserSurfacePose` 的展开端倍率由 1 变为 `m`）。
  - More：`PositionContentMore` 里 `scale` 公式的展开端（届时 `1.0` 变为 `m`，`fontTargetScale` 同取该值）。
  - 绘制属性窗：`fontPanelTargetScale` 与三处 `fontTargetSize` 用的同一个展开态倍率局部量。
  - 颜色选择器：`fontPickerTargetScale` 的展开端。
- 紧凑态倍率是否随 `m` 变化由那个任务决定；无论怎么定，字号目标只看展开态，本约定不受影响。
- 写入 spec（`rendering-and-ui.md` 字号节），作为后续任务的前置约束。

## 5. 非目标

- More 外框（60×30 → 网格尺寸）与内容（0.16 → 1）的紧凑倍率不完全一致：次要，只在接近收起、几乎透明时可见，本任务不改。
- 不改面板开合曲线、时长、回弹强度；不改 `UiFontCorrectionClass` 与 `ResolveAnimatedFontSize`。
- 不实现用户展开倍率设置项。

## 6. 兼容与风险

- 静止展开态的绘制结果不变（`T` 与原先展开端相同），golden 静止截图应不变。
- 开合中间帧的字号会变大（回到等比），轨迹类 trace 中 `fontCorrection` 字段在收起段不再变化，属预期变化。
- `Bar.EraserAttribute.Test.cpp:824` 的 More 用例把 `scale.tar` 当 `fontTargetScale` 传入，固化了旧写法，需改为展开态并补“开合途中等比”断言。
- 几何属性窗测试只断言“展开静止时 `fontTargetSize == size.val`”，仍成立。
- Win7 / 兆芯：只改数值来源，无新 API。

## 7. 回滚

所有改动为局部表达式替换 + 一个字段改名，单个 feat 提交可整体回退。
