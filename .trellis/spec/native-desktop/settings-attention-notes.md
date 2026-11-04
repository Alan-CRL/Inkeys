# 设置行注意事项

## 1. Scope / Trigger

设置项解释中含重启/下次启动、明确权限/外部依赖、数据风险等特别注意信息时使用。普通功能定义、模式名、运行状态与独立确认/错误对话框不按关键词机械拆分。

## 2. Signatures

- `SettingRow(..., bool keepDescriptionInline=false, const char* note=nullptr)`。
- `ToggleRow(..., bool& value, const char* note=nullptr)`、`ComboRow(..., items, const char* note=nullptr)`。
- `SliderRow/SliderIntRow(..., format, const char* note=nullptr)`；ButtonRow在accent后、ButtonGroupRow在buttons后、NavigationRow在iconTint后追加同一可选note。
- `InfoText(const char* text, ImFluentTextStyle style=ImFluentTextStyle_Caption)`：分组注意信息复用相同样式。
- `ResolveRowWithNote(measure,titleHeight,descriptionHeight,inlineDescriptionHeight,noteHeight,inlineNoteHeight,reveal) -> MultilineRowGeometry`，结果包含独立note矩形。
- `InkeysHeadlessTests.exe --setting-attention-only --no-window`：本功能专项，不调用HomeWARP基准或创建HWND。

## 3. Contracts

- 页面显式传入普通解释与注意文字。共用控件不依赖i18n聚合头，不运行语言相关的“重启/需要”等拆句规则。
- 注意只用于具体条目已有普通解释之外的补充信息，Caption说明对应Caption提示：12 DIP字号、16 DIP行高，填充InfoSolid U+F167图标用Accent，文字用TextPrimary；普通说明保留TextSecondary。不得用描边E946替代该小型附注图标。
- 注意图标占位及文字间隔一起计入换行宽度。说明非空时与注意之间留4 DIP；若只有note而无普通description，SettingRow把其归一为普通说明、清空note，不画i，使用原说明几何和TextSecondary。
- 提示作为同一文字块参与原有DescriptionWidth/退让动画，测量先于绘制，卡片/下一行高度与action避让均含提示。窄窗stacked模式必须把action排到完整文字块之后。
- note为空或null直接返回原ResolveMultilineRowLayout，不改变已有行的几何和动画。可选参数不改变业务回调执行次数，不新增点击/确认/权限逻辑。
- InfoText是低层绘制辅助，不用于把原有类/页级信息框拆为单独i行；原说明块使用Notice/原卡片，保留教程按钮、协议链接和条件判断。PPT详情不重复插件主页的支持范围。
- 国际化说明/注意同步三语言、生成Keys和中文snapshot；已有硬编码组件说明只按原语义拆到note，不借此重写整个页面。
- 激光多指的“修改后对新开始的笔画生效”和粒子的“当前版本暂不可用”按用户要求直接删除，不能改放note。统一新笔提示不恢复；BETA、无粒子toggle和运行时强制关闭保持。
- 语言重启确认、更新/绘图恢复错误等独立流程保持；旧WPP等无活动消费者的键不因整理注意信息恢复UI。

## 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| note=null/空串 | 与旧行布局逐项相同，没有额外高度或图标 |
| 只有note、无普通解释 | 作为普通说明，无i，几何/前景色与直接description一致 |
| 长中文/繁中/英文或窄窗 | 减去图标占位后自动换行，完整高度纳入卡片 |
| 分数DPI/高DPI或退让中间帧 | 图标/字号/边界一致，不进入action矩形或下一行 |
| 需要重启或依赖前提 | 从描述移至note，不改变实际生效时机或安装/权限动作 |
| 普通功能/模式含“重启/清空”等词 | 保持原定义，不自动变成风险提示 |
| 用户明确要求删除的句子 | 三语言源及活动调用均不显示，不迁移到任何新note |

## 5. Good / Base / Bad Cases

- Good：画布帧率解释刷新/资源权衡，下面蓝i显示重启后生效；下拉值和默认120不动。
- Base：没有特殊注意的普通设置保持原排版。
- Bad：把风险直接拼回description、用大InfoBar代替行提示、改变重启流程，或把“静默重启”选项名拆掉。

## 6. Tests Required

- 纯几何：无note完全兼容、空描述、宽窄stacked、长文、退让动画中间帧、action和后行不重叠。
- 真实ImGui/字体/DrawData：七种DPI、三语言、明暗主题、实际图标顶点/颜色/字号，以及所有七类Row封装和InfoText透传。
- 对照活动设置盘点核重启、权限、协议、数据风险；检查普通解释不重复、原动作和状态条件不丢。
- i18n sync/check、完整InkeysRepo.sln Debug|ARM64及专项--no-window；不得把纯布局测试称为实际桌面GUI验收。

## 7. Wrong vs Correct

Wrong：在SettingRow里搜索中文“重启”并切割说明，或绘制完后仅向下增加游标而不改变卡片测高。

Correct：页面提供显式本地化note，ResolveRowWithNote先把完整提示纳入原布局，随后按同一测量结果绘制解释和信息行。
