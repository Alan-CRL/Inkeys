> 历史记录：本文对应首次三场景可选高亮的需求/实现。当前两类型选择、固定设备边缘规则以 cursor-policy-revision.md 和 verification-cursor-revision.md 为准；本文验证不作为修订代码的通过证据。

# Research: 光标外观澄清与按输入场景保存

- **Query**：落实用户对鼠标画笔、荧光笔及高亮边缘的澄清，并检查当前外观参数的对应关系。
- **Scope**：internal；用户需求确认 + 当前生产源码静态核对。
- **Date**：2026-10-19。
- **阶段**：planning；本轮只更新任务文档，未改代码、构建、运行测试或启动 GUI。

## 用户已确认的行为

用户说明：“画笔还是默认鼠标笔形”，荧光笔使用原来给屏幕笔的光标；鼠标场景带高亮边缘，效果类似现有笔光标。高亮可选择；屏幕笔使用半透明，数位板使用高亮边缘。随后明确选择“按输入场景分别保存”。

| 决策 ID | 已确认内容 |
| --- | --- |
| D1 | 默认按工具区分：鼠标画笔使用笔形（沿用 R5 的左上笔尖、右下笔尾），鼠标荧光笔启用现有屏幕笔的笔尖/粗细预览。 |
| D2 | 原先“亮/暗”意图明确为半透明与高亮边缘风格，高亮为可选项。当前不需要先找出另一个独立 RGB 亮暗 UI 才能规划。 |
| D3 | 隐藏系统触摸光标始终启用，无开关，不新增隐藏偏好或迁移旧 Beta 值；优先确认当前隐藏效果和 Win7 正常。 |
| D4 | 鼠标、屏幕笔、数位板分别保存高亮偏好；默认鼠标高亮、屏幕笔半透明、数位板高亮。改变一个场景的值不能覆盖另外两个。 |

鼠标荧光笔复用原有预览几何与真实粗细；高亮开启/关闭只影响对应场景的光标外观，不修改实际墨迹、输入坐标或工具透明度。绘制/穿透仍按原 R6 处理。Pen 接触态是否显示预览继续遵守现有生命周期合同，本次澄清没有要求改动该显隐规则。

## 静态核对

| 位置 | 当前事实 | 对后续计划的影响 |
| --- | --- | --- |
| `Draw3.PenCursor.cppm:26-38` | appearance 有整体 opacity、fillAlpha、outlineWidth 和 outline RGB；没有独立 outlineAlpha | 现有字段可以描述填充与轮廓，但尚不能单独调边缘透明度 |
| `Draw3.DrawingController.cpp:302-339` | Pen 使用 Circle，荧光笔使用随实际笔宽变化的 Rectangle；荧光笔 opacity 取工具 alpha、fillAlpha=1 | 复用实际宽度/形状；无需重新制作一套荧光笔几何 |
| `Assets/inkPixelShader.hlsl:269-285` | `outlineAlpha = outlineCoverage * opacity`；`fillAlpha = innerCoverage * fillAlpha参数 * opacity` | 当前边缘与内部均受同一个整体透明度限制；单调 RGB/width 不等于独立边缘高亮已实现 |
| `Draw3.RendererPrimitives.cpp:329-364` | opacity 在 color.a，outlineWidth/fillAlpha 在第二个 InkPoint，outline RGB 在 constants padding | 若增加边缘强调参数，要同步 CPU/GPU 数据传递，不误复用其他 shader 字段 |
| `Draw3.PenCursor.cpp:318-329` | 旧 translucent 开关只对特定普通 Pen 填充做 alpha 归一化；Mouse 另有归一化分支 | 不可只把旧 bool 改名就宣称满足荧光笔边缘风格；不得将实际荧光笔改为不透明 |
| `Draw3.PenCursor.cpp:34-45` | appearance 等价包含当前 fill/outline 属性 | 新增高亮或边缘参数要纳入等价判定与静止重绘 |
| `Draw3.PenCursor.cppm:54-65`、`Draw3.RealtimeStylus.cpp:1596-1611` | 光标样本携带 InputSource；RTS 从 snapshot 复制来源 | 按场景选择可复用只读来源数据，不必改 producer 身份或创建新输入链 |
| `Draw3.SpeedEraser.h:15-34`、`Draw3.SpeedEraser.cpp:84-100` | 已区分 Mouse、IntegratedPen、ExternalPen、Unknown；无现代 API 或来源冲突时保守为 Unknown | Win7 分别保存偏好不等于 Win7 已自动识别屏幕笔/数位板；Unknown 路由需明确回退，不伪造设备类型 |

代码路径缩写均位于 `Inkeys/Inkeys/Drawing/Draw3/`。这些是代码事实，不是边缘增强的像素验收结果。

## 最小设计增量

- 设置中按 Mouse/ScreenPen/Digitizer 分别维护高亮偏好，与工具形状选择分开。系统触摸光标隐藏是固定策略，无对应开关或存储字段；三份高亮偏好不联动橡皮 PenResponseChoice、隐藏策略或实际绘制配置。
- 按当前输入样本选择对应场景，再应用已选工具的预览几何。场景切换应重新解析 appearance，避免上一设备高亮泄漏；不能只在工具/颜色/宽度变化时更新。
- 视觉上将边缘强调与工具填充分开，使半透明荧光笔仍保持原填充语义，同时让轮廓清楚。具体 shader 参数沿现有 CPU/GPU 布局做最小扩展；不预设高亮必须意味着白色实体或改变整个填充 alpha。
- 如调整 shader，正式 HLSL/资源与 Win7 WARP guarded PS 同步；具体链和生产/demo 差异仍以 [mouse-cursor-capabilities.md](mouse-cursor-capabilities.md) 及 native-desktop/draw3-shaders.md 为准。
- InputSource 为 Unknown/Conflict 时不自动宣称已辨明设备，也不借用橡皮响应设置修改真实身份。实施前补齐视觉场景的选择/回退表，并在 Win7 单独验证。

## 后续验证要点

1. 默认值：鼠标/数位板高亮，屏幕笔半透明；鼠标画笔笔形与荧光笔预览分别正确。
2. 独立持久化：分别切换三种场景的高亮，另两份不变，重启恢复各自值。
3. 即时生效：静止时切换开关、Mouse→ScreenPen→Digitizer 交替，appearance、重绘与系统光标策略同步，无旧设备外观残影。
4. 像素与几何：半透明/高亮边缘对比，荧光笔内部透明度、粗细与热点不变；Win7 guarded 与正式 shader 分别验证。
5. 来源与隔离：Known/Unknown/Conflict、Win7 API 缺失、绘制/穿透；不改 Touch 橡皮、Laser、Pen Contact 显隐或墨迹保存/撤销。

## Caveats

- 用户已澄清目标效果，原报告“未找到独立 palette UI”的事实不再是外观需求未决项；原调查记录保留用于理解代码现状。
- 当前代码尚无完整的三场景偏好或独立边缘强调接线；不将用户确认记为功能已经实现。
- 本轮没有运行程序、编译、像素测试或 Win7 真机测试；系统光标残留与闪现的调查结论不因本次外观澄清而升级。
