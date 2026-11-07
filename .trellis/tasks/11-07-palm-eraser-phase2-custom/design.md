# 技术设计：手掌橡皮擦除样式与大小自定义

位置以基线 babdecf6 为准；shapes 子任务落地后复核。

## 1. 配置（Other.Config.cppm X 宏，NoUpload，IdtAtomic）

| 键 | 类型 / 取值 | 默认 |
| --- | --- | --- |
| `Drawing.PalmEraser.Shape` | 枚举 FollowDefault=0 / Circle / Rectangle / Square | FollowDefault |
| `Drawing.PalmEraser.SizeMode` | 枚举 Dynamic=0 / FixedFollowDefault / FixedStatic | Dynamic |
| `Drawing.PalmEraser.SizeMethodCircle` | 枚举 EllipseArea=0 / RectArea / Inscribed / Circumscribed | EllipseArea |
| `Drawing.PalmEraser.SizeMethodRectangle` | 同上枚举 | RectArea |
| `Drawing.PalmEraser.SizeMethodSquare` | 同上枚举 | EllipseArea |
| `Drawing.PalmEraser.SizeMultiplier` | float，有限且 0.25–3.0，否则回落 | 1.0 |

枚举按现有字段方式严格校验原始整数与宽度；浮点通用 codec 不校验有限性/范围，须在读取或应用处补校验（Other.Config.cpp:209/642）。

## 2. 传递

沿手掌现有路径：偏好 mutex → `SetPalmEraserPreference` 冻结保存并发布 Host → 绘制 owner 应用 Controller（IdtState.cpp:1305、Draw3.Host.cpp:1740）。扩展 `PalmEraser::Settings` 字段；独立 Down 时随偏好锁存（现有锁存点），并锁存当时的普通橡皮形状与默认大小以解析“跟随默认”。不跨线程直接改 Controller。

## 3. 定尺（Draw3.PalmEraser.cpp `State::Observe`）

- 识别：保持 `area = W×H` 与阈值比较、最大面积、长宽比、观察窗、Up 锁定、续接（R5）。
- 动态粗细：按生效形状与该形状方法计算形状尺寸（prd 表），乘倍率，再换算为等效半径 r 存入（矩形 `r = 半宽/0.72360`，正方形 `r = 半边/0.88623`，圆 `r = D/2`），以复用逐点标量 r 与 shapes 的等面积几何。
- 固定粗细（跟随默认）：直径 = 默认橡皮大小（DIP→画布 px）；全局自动粗细开启时手掌运行时按普通 Touch 笔速橡皮的尺寸控制器变化（复用 SpeedEraser 控制器，面积辅助保持关闭）。
- 固定粗细（不支持笔速）：恒为默认大小，Fixed 模型。
- 圆形 + 内切椭圆等面积 + 倍率 1.0 + 动态 = 现行为（回归基线）。

## 4. 屏蔽范围（R6）

扩展子任务 A 的 `PalmExclusionContains`：圆形保持 `r + max(0.15r, 4px)`；矩形/正方形以手掌中心、半宽 hx、半高 hy，判定 `|dx| ≤ hx + max(0.15hx, 4)` 且 `|dy| ≤ hy + max(0.15hy, 4)`。

## 5. 设置 UI（Inkeys/Inkeys/UI/Setting/）

- 手掌详情页（Setting.Pages.cpp:852 起）：在现有识别相关项之后新增：
  - 卡片“擦除样式”：复用 shapes 的 `EraserStylePicker`，选项含“跟随默认”；“跟随默认”时预览高亮当前普通橡皮样式并标注。
  - 卡片“手掌橡皮大小”：条目一 `InlineCombo`（三项）；动态时下方显式 i 提示“跟随接触面积计算”（`note` 在无普通说明时会降级，需显式布局，Setting.Controls.cpp:2260）。条目二 `InlineCombo`，选项文本按生效形状切换并在推荐项后加“（推荐）”。条目三 `SliderResetAction`（无文字重置）。条目二、三用 `ConditionalBlock` 仅在动态时显示。
- 命名：新倍率称“擦除大小倍率”，与现有“识别面积倍率”（阈值）区分；不改现有项文案以外的语义。
- i18n：zh-CN 基准，`Scripts/i18n.ps1 sync` + 补译 + check。

## 6. 风险

- 笔速模式下手掌复用 Touch 笔速控制器需确认与手掌 Fixed 模型/续接的交互；以生产 probe 覆盖。
- “跟随默认”在 Down 锁存，设置修改不影响进行中的手掌。
- 回退：所有新项默认值即现行为。
