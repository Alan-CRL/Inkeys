# 设计：形状第十轮

调研报告：`research/codex-a-touch-polyline.md`、`research/codex-b-prompt-touch-toolbar-hold.md`、`research/codex-c-rotate.md`（Codex Sol High 只读，主 Agent 已抽查关键行）。

## R1 触摸折线/自由多边形加点失败

根因：`Bar.ShapeEditor.cpp` `DrawEditorInputCoverage()` 在构造期间仍把 `snapshot.bounds`（整个构造包围盒 +3px）填成 alpha=1/255 的输入覆盖；SelectionOverlay 是 ULW 分层窗口，系统按像素 alpha 把触摸交给它。而 `Impl::Hit()` 构造期只接受顶点圆（22×scale），`ContactNative` 返回 false 后 `WM_TOUCH` 仍被关闭句柄并 return 0，不转交画布，于是包围盒内、顶点外的触摸被吞掉。鼠标依赖 `WM_NCHITTEST → HTTRANSPARENT` 能穿透，所以只有触摸出现，且构造跨过屏幕中心时尤其明显。

修复：包围盒那一块填充加 `!snapshot.construction.active` 条件，让原生输入覆盖与 `Hit()` 一致；顶点圆和已完成对象的覆盖保持不变。
回归：在 `--ui3-shape-editor-test` 中构造进行中的折线/多边形快照，渲染后读回位图，断言包围盒内空白处 alpha 为 0、顶点处 alpha 非 0。

## R2 长按缩短（已实现）

`HoldRingDelay=200ms`、`HoldCommitDelay=1000ms`，进度 `(still-ring)/(commit-ring)` 由常量推导；测试时间点按旧时间线等比映射。

## R3 步骤提示栏触摸

根因：触摸 Down 的准入看 `PreparePresentedButtonHits()` 生成的 `PresentedTouchRegions` 白名单（`Bar.Button.cpp`），里面没有构造提示，所以 provider 在 probe 阶段就拒绝了；提示的 `Pointer()` 本身能处理合成的左键消息。
修复：
- 在白名单中加入提示的同帧候选面板：可见且可交互时为 Accept，可见但禁用或在退场时为 Blocked，空间类型 Rigid。必须用同帧候选，并沿既有的成功呈现事务发布。
- `BarConstructionPrompt::Pointer()` 额外返回 accepted；`Bar.Interaction.cpp` 调用方仿照相邻面板调用 `AcceptBarButtonFeedback()`，同时清理下层 hover。

## R4 顺时针旋转 45° / 90°

- 核心：`RotateShape(before, radians, revision, options)` 与 `FlipShape` 并列，pivot 用旋转把手同样的"变换后局部路径包围盒中心"，在外层组合旋转（y 向下坐标系中正角为顺时针：+π/4、+π/2），保留 construction 并重建。立体图形显式拒绝。
- 动作：新增 `ShapeUiActionKind::Rotate45Clockwise`、`Rotate90Clockwise`，在 `BuildShapePropertyCandidate` 中分派，并加入运行时属性动作白名单；历史走与翻转相同的 Replace 事务。
- UI：新增 `Rotate45`、`Rotate90` 两个按钮 ID（追加在 Count 前），`ToolbarItems` 顺序为翻转H、翻转V、旋转45、旋转90、完成。图标为 `barShapeRotate45`/`barShapeRotate90`，`.rc`/`.vcxproj`/`.filters` 按翻转同样方式登记。可见性：非圆且非立体（立体的变换合同不允许屏幕旋转；圆旋转无视觉变化）。
- i18n：`UI/Bar/GeometryAttributes/Rotate45Label`、`Rotate90Label`，各语言补齐并重新生成 `IdtI18nKeys.g.h`。

## R5 样式栏操作后 3s 内不移位

- 参照主栏撤回 chrome 的 `RecallChromeIdleSeconds=3.0`：用 steady_clock 计算期限并锁存布局，不累计动画 dt。
- `Impl` 新增受 `inputMutex` 保护的状态：`toolbarHoldUntil`、冻结时的屏幕锚点（已成功呈现的位置）和 pending 标志。
  - 样式栏按钮 Down 时冻结当前已呈现位置，并停止既有的位移残差；`Activate()`、子层操作、取色 Move/Up 都把期限续到 now+3s。按钮或取色手势进行中始终冻结，结束后再满 3s 才解冻；hover 不续期。
  - `Advance()` 把"计算期望位置"和"应用位置/换边状态"拆开：保护期内只更新 pending；到期后从冻结位置用现有 0.4s EaseOutCubic 残差动画移向最新目标，即使停靠分支（placement）没变也要动画，之后恢复实时跟随。
  - 到期唤醒：编辑器 tick 合并该 deadline，由 SelectionOverlay 的 surface deadline 请求一帧，不做 3s 持续渲染。
- 边界：
  - 用户直接操作图形（本体、控制点拖动、多指变换）立即解除冻结，并从冻结位置动画回到跟随。
  - 尺寸或按钮显隐变化照常动画，但保持冻结锚点。
  - 屏幕或 DPI 变化时只做必要的屏内钳制。
  - Finish、切换对象、页面或显示器变化时清除冻结。

## 兼容与回滚

每项改动独立，可单独回退。R5 修改了 `rendering-and-ui.md` 中"普通几何实时跟随"的合同，需要在 spec 中写明例外。
