# 技术设计：手掌橡皮大小自定义（R2–R4、R6）

2026-11-07 按 shapes 子任务落地后的实际代码重写（工作区基线未提交，见 `refs/inkeys-baseline/custom-start`）。R1 手掌形状已在 shapes 中实现：`Drawing.PalmEraser.Shape` 取值 -1（跟随）/ 0 圆 / 1 竖长矩形 / 2 正方形，`PalmEraser::ResolveShape` 解析。

## 1. 配置（`Other.Config.cppm` PalmEraser 组，`IdtAtomic<int>`，严格整数校验，非法回落默认）

| 键 | 取值 | 默认 |
| --- | --- | --- |
| `Drawing.PalmEraser.SizeMode` | 0 动态粗细 / 1 固定粗细（跟随默认橡皮）/ 2 固定粗细（不支持笔速橡皮） | 0 |
| `Drawing.PalmEraser.SizeMethodCircle` | 0..3，按 §3 表中圆形行的选项顺序 | 0 |
| `Drawing.PalmEraser.SizeMethodRectangle` | 0..3，竖长矩形行顺序 | 0 |
| `Drawing.PalmEraser.SizeMethodSquare` | 0..3，正方形行顺序 | 0 |
| `Drawing.PalmEraser.SizeMultiplierPercent` | 整数 25..300（%） | 100 |

- 方法索引按“每种形状各自的选项顺序”存储，索引 0 恒为该形状的推荐项，因此三项默认都是 0。
- 倍率用整数百分比，与现有识别阈值的存法一致，避免浮点非有限值的校验问题。
- 校验方式与 `Drawing.PalmEraser.Shape` 一致：类型不对或超出范围，回落默认值；`IdtMain.cpp` 的 config-boundary 测试要覆盖。
- 读写放在 `IdtState.cpp` 的手掌偏好中，与 `Shape` 并列。

## 2. 传递与锁存

- `PalmEraser::Settings` 新增 `sizeMode`、`sizeMethods[3]`、`sizeMultiplierPercent`。
  - `NormalizeSettings` 负责逐项校正；`operator==` 仍为 default。
- 传递沿用现有路径：偏好 → `SetPalmEraserPreference` → Host 发布 → 绘制 owner 应用。不跨线程直接改 Controller。
- 生效形状在 `State::Begin` 时确定：
  - 由手掌设置和独立 Down 时锁存的普通橡皮形状，经 `ResolveShape` 得出，存入 `State`。
  - 定尺（§3）和屏蔽范围（§4）都使用这个形状。
  - 激活时 `PromotePalmRuntime` 写入笔划的仍是同一个形状，所以光标、擦除、屏蔽三者一致。但普通橡皮笔划已开始且手掌指定另一形状时，会保留该笔 Down 时锁存的形状，并按保留形状重新定尺。
- 设置在 Down 时锁存，进行中的手掌不受修改影响。

## 3. 定尺（纯函数 + `State::Observe`）

识别部分不变（R5）：
- 识别仍按 `area = W×H` 与阈值比较，并保持长宽比 ≤3.5、最大面积单调、观察窗、Up 锁定、续接逻辑。
- 变化只有一处：把 `diameterPx = √(W×H)` 换成下面的纯函数结果。

纯函数 `PalmEraser::ContactEraserDiameter(EraserShape, int method, double W, double H, int multiplierPercent)`：
- 先按下表算出形状尺寸 L（圆为直径 D，矩形为宽 w，正方形为边 s），再乘以 `multiplierPercent/100`。
- 最后换算为“等效圆直径”返回，以复用逐点标量半径和 shapes 的等面积几何（D6：形状面积 = πD²/4）：
  - 圆：直径 = D。
  - 竖长矩形：直径 = w / √(π/6)，即 w / 0.723601。
  - 正方形：直径 = s / (√π/2)，即 s / 0.886227。
- 结果非有限或 ≤0 时，返回 0，调用方按无效样本处理。

| 形状 | 0（推荐） | 1 | 2 | 3 |
| --- | --- | --- | --- | --- |
| 圆 D | 内切椭圆等面积 `√(WH)` | 矩形等面积 `2√(WH/π)` | 内切圆 `min(W,H)` | 外接圆 `√(W²+H²)` |
| 竖长矩形 w（高 1.5w） | 矩形等面积 `√(WH/1.5)` | 内切椭圆等面积 `√(πWH/6)` | 内切矩形 `min(W, H/1.5)` | 外接矩形 `max(W, H/1.5)` |
| 正方形 s | 内切椭圆等面积 `√(πWH)/2` | 矩形等面积 `√(WH)` | 内切正方形 `min(W,H)` | 外接正方形 `max(W,H)` |

回归基线：圆形 + 方法 0 + 100% 必须与现在的 `√(WH)` 逐位一致。
已知变化：竖长矩形的默认改为“矩形等面积”，比之前（相当于内切椭圆等面积）大，面积比为 4/π。这符合 D10。

## 4. 三种粗细方式（`PromotePalmRuntime` 及其运行时）

- **0 动态**：使用 §3 的结果。现有行为是 `Fixed` 宽度模型和 `EraserKind::Fixed`，保持不变。
- **1 固定（跟随默认橡皮）**：直径 = 普通橡皮默认大小（DIP），按普通橡皮同一路径换算成画布 px，Down 时锁存。
  - 全局“自动粗细”（笔速橡皮总开关）开启时，手掌运行时按普通 Touch 笔速橡皮的尺寸控制器变化：复用 `SpeedEraser` 控制器，面积辅助保持关闭。
  - 全局自动粗细关闭时，等同于方式 2。
- **2 固定（不支持笔速）**：恒为默认大小，使用 `Fixed` 模型。
- 方式 1、2 仍以 W×H 阈值判定激活，只有尺寸不取接触面积。
- **停止条件**：如果不改 `SpeedEraser` 的对外接口就无法让手掌复用笔速控制器，方式 1 先按方式 2 实现，并在报告中说明，不得硬改接口。

## 5. 屏蔽范围（R6，`PalmExclusionContains`）

- 圆形：保持 `r + max(0.15r, 4px)`。
- 竖长矩形和正方形：以手掌中心为原点，由等效直径和形状算出半宽 hx、半高 hy（与 shapes 几何同一换算）。命中条件为 `|dx| ≤ hx + max(0.15hx, 4)` 且 `|dy| ≤ hy + max(0.15hy, 4)`。
- 尺寸取手掌橡皮当前的实际直径。方式 1 开笔速时，取当前笔速尺寸；无法取到时，用激活时的直径，并在报告中说明。

## 6. 设置 UI（`Setting.Pages.cpp` 的 `RenderPalmEraser`）

页面顺序：基本设置 → 预览卡 → 精细控制（适用工具、边擦边写）→ **手掌橡皮大小（新）** → 橡皮光标（i 信息块 + 形状卡）。

“手掌橡皮大小”分组（`SectionHeader`）：
1. `ComboRow` “粗细计算方式”：三项，依次为“动态粗细（推荐）”、“固定粗细（跟随默认橡皮）”、“固定粗细（不支持笔速橡皮）”。行描述固定为一句通用说明。通过 `note` 参数给出当前选项的 i 提示：
   - 动态：“跟随接触面积计算”。
   - 跟随默认：“使用默认橡皮大小，开启“自动粗细”时按笔速变化”。
   - 不支持笔速：“始终使用默认橡皮大小”。
2. `ComboRow` “大小计算方式”：四项，取生效形状那一行（手掌形状；选“跟随”时取普通橡皮的当前形状）。首项后加 `RecommendedSuffix`。`note` 显示当前形状，例如“当前形状：竖长矩形”。改动只写该形状对应的配置项。
3. “擦除大小倍率”：`SettingRow` 加 `SliderResetAction`，即带图标、无文字的重置按钮，范围 25%–300%，按整数百分比显示。重置时写回 100。描述说明“按比例缩放橡皮的边长或直径，不影响识别”。

条目 2、3 用 `ConditionalBlock`，仅在动态粗细时显示。草稿和事件扩展 `PalmEraserDraft` / `PalmEraserEvents`，保存逻辑在 `Setting.cpp`，与 `shapeChanged` 同一模式。

命名：新倍率叫“擦除大小倍率”，与现有的“识别面积倍率”（阈值）区分。现有文案不改。

## 7. 风险

- 笔速复用见 §4 的停止条件。
- 竖长矩形手掌的默认尺寸变大（§3）。这是需求本身，但实机效果待确认。
- 回退：除竖长矩形外，所有新项的默认值即现行为。
