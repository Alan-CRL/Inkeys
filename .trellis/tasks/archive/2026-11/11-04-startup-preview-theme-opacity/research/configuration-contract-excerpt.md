# 配置与国际化合同摘录

源：.trellis/spec/native-desktop/configuration-i18n-and-assets.md，2026-11-04 规划时读取。仅提取本任务相关章节，实施时如遇超出摘录的配置规则应查阅原文。旧第56行关于 Startup Preview 不支持透明度的限制将由本任务获批范围替代。

# Configuration, I18n, and Assets

本文使用 `【直接确认】`、`【合理推断】`、`【待确认】` 和 `【历史/兼容】`。尤其注意：“新配置 module”是代码年代/结构描述，不表示维护者已宣布它取代传统配置。

## 两套配置系统分别服务哪些代码

| 配置系统 | 文件与磁盘位置 | `【直接确认】`的使用范围 |
| --- | --- | --- |
| 传统 `SetListStruct` | `IdtConfiguration.h`；`IdtConfiguration.cpp::ReadSetting/WriteSetting`；`globalPath + L"opt\\deploy.json"` | `IdtMain`、`IdtDraw`/`IdtDrawpad`、`IdtFloating`、`IdtFreezeFrame`、`IdtHistoricalDrawpad`、`IdtMagnification`、`IdtPlug-in`、`IdtRts`、`IdtWindow`、`Net.Update`、`Bar.Main`、`Setting` 等仍有读写 |
| `Inkeys.Other.Config` | `Inkeys/Inkeys/Other/Other.Config.cppm/.cpp`；`Inkeys::Config` class、全局实例 `Inkeys::config/configOnce`；`Config::ReadAll/ReadMini/Write`；`globalPath + L"Inkeys\\Config\\main.json"` | schema 中可见 `Config.AutoClean`、`Info`、`UI.Bar.Zoom2`、`UI.Bar.UseTaskbarHeightAsScaleBase`、`Experimental.Inkeys3.UI3.Animation/EdgeLighting`、`PlugIn.PPTHelper` 等；消费者包括 `IdtConfiguration.cpp`、`IdtMain.cpp`、`IdtPlug-in.cpp`、`Net.Update*`、`Bar.Zoom*`、`Bar.Main*` 和 `Setting*` |

`【直接确认】` `INKEYS_CONFIG_SCHEMA` 集中声明新 module 已支持的字段；源码中还把启动读取区标注为“新配置 Test”。这说明新系统正在实际使用，但不足以证明迁移已完成或所有新字段必须进入它。

### 容易混淆的真实边界

- `【直接确认】` `Inkeys.UI.Bar` 是唯一产品悬浮栏入口；传统 `setlist.Experimental.Inkeys3.UI3` 路由字段和 `useInkeys3UI` 已删除，写旧配置时清理遗留 key。
- `【直接确认】` 新配置中的 `Experimental.Inkeys3.UI3.Animation` 控制 UI3 动画；`Experimental.Inkeys3.UI3.EdgeLighting` 控制 UI3 边缘点光及第三鼠标光。它们都不是选择新旧悬浮栏的总开关。
- `【直接确认】` Bar 的 zoom 配置走 `Inkeys.Other.Config`；Bar 的其他行为仍可读取传统 `setlist`，因此不能把 Bar 简化成“只用新配置”。
- `【直接确认】` PPT helper 新字段由 `IdtPlug-in.cpp` 等读取，但 `IdtPlug-in.cpp` 同时仍使用传统 PPT/交互设置。
- `【直接确认】` 两套系统写入不同 JSON 文件；全仓未见通用的双向同步或完整迁移器。
- `【直接确认】` `Config.AutoClean` 会影响传统 `WriteSetting`，使其清空旧 `setlistVal`；这是一个明确交点，不代表两个 schema 普遍同步。

`【历史/兼容】` `SetListStruct` 仍是当前大量主路径的真实接口，不能仅因其文件名较旧而标记废弃。

`【待确认】` 维护者需要决定：未来字段按什么标准选择系统、是否计划迁移 `deploy.json`、发布包是否预置这些文件，以及向后兼容周期多长。Codex 在答案明确前不应自行把字段迁到“看起来较新”的 module。

## 配置修改的证据型审查清单

以下是从并存状态推导的 `【合理推断】`，不是声称项目已有统一迁移政策：

1. 搜索字段、JSON key、默认值和 UI 写回的全部读写点，先确定真实所有者。
2. 已在 `INKEYS_CONFIG_SCHEMA` 中的字段继续通过该 module 读写，除非任务明确更改 schema/持久化格式。
3. 仍由 `SetListStruct` 管理的字段保持当前文件兼容；迁移需同时设计旧值读取、默认值、写回和回滚。
4. JSON key 改名属于持久化格式变化；没有迁移代码时不能假设旧 key 会自动升级。
5. 记录设置窗口写的是哪套配置，避免 UI 显示值、运行时缓存与磁盘文件分叉。

## UI3 Bar 深浅主题配置合同

持久化路径为 `Experimental.Inkeys3.UI3.ThemeMode`，类型为整数，默认值 `1`。`ThemeModeDark=1`、`ThemeModeLight=2`；`NormalizeThemeMode()` 仅保留 `2`，其余值一律归一到 `1`，`ThemeModeUsesDarkStyle()` 是配置值到 renderer `darkStyle` 的唯一映射。

- `config.ReadAll()` 后先归一并写回内存，再调用 `Inkeys::UI::Bar::SetThemeMode()`；旧配置缺字段、类型不符或非法整数均得到深色。
- “常规 > 外观 > 主题”分区依次为“主栏主题”与“设置主题”两个下拉框，均只显示深色与浅色（同一选项顺序）。主栏主题选择后依次更新 `Inkeys::config`、发布 Bar 主题目标、调用 `QueueConfigWrite()` 异步保存；设置主题写 `setlist.settingDarkMode`，与标题栏切换按钮同一路径（`WriteSetting()` + `settingThemeSerial` 递增 + 请求 Settings 帧），两处始终显示同一值。
- `SetThemeMode()` 只交换 `requestedDarkStyle` 并唤醒 Bar；D2D 资源和连续材质动画仍由串行渲染线程处理。
- Windows `WM_THEMECHANGED` 不读取注册表或改写配置；`WM_SETTINGCHANGE` 继续承担显示器/系统设置刷新。
- 该字段不复用传统 `SetSkinMode`，也不改变设置窗口自身主题。

无窗口测试必须覆盖默认 `1`、有效 `1/2`、非法值回退、`darkStyle` 映射及 Config 快照编解码；完整验证仍使用 `Debug|ARM64` Solution 构建和 `InkeysHeadlessTests.exe --no-window`。

## UI3 常驻透明度配置合同

持久化路径为 `Experimental.Inkeys3.UI3.BackgroundOpacity` 与 `ContentOpacity`，均为整数百分比。背景默认 `0`（`ResidentBackgroundOpacityThemeDefault`，跟随主题：Dark 80%、Light 92%），其余值经 `NormalizeResidentBackgroundOpacity()` 收敛到 30~100；内容默认 `100`，经 `NormalizeResidentContentOpacity()` 收敛到 30~100。

- 背景为绝对值：用户值即展开主栏的填充透明度；主按钮（收起 0.6 基准）、底部吸附指示与分页按与主栏 0.80 基准的比例同步。内容透明度是图标/文字的倍率。两者只作用于常驻表面（主栏、主按钮、吸附指示、PPT/白板分页），弹出面板、属性栏、菜单保持原透明度。
- 启动在 `ReadAll()` 后归一并写回内存，再调用 `Inkeys::UI::Bar::SetResidentOpacity()`；该函数只交换 `BarStyleClass::requested*Opacity` 并唤醒 Bar 与 `PptPageMask()`，渲染线程在帧首应用并整幅重画，分页在 Advance 中比较已应用值。
- “常规 > 外观 > 透明度”分区两条滑块（30%~100%）带“恢复默认”按钮。拖动实时发布，松手、重置或离页时再 `QueueConfigWrite()`；背景重置写回 `0`，滑块显示当前主栏主题的默认值。动画速率（默认 1.0x）与主栏缩放（默认 100%）滑块同样带“恢复默认”，重置后沿原有释放保存路径。
- Startup Preview 仍使用主题默认透明度绘制占位，与自定义背景透明度交接时可能有一次可见跳变。

无窗口测试必须覆盖两个字段的默认值与归一、主题默认 80%/92%、绝对值在任意材质权重下对展开主栏成立、收起主按钮比例与非法覆盖值回退。

## 国际化源与生成物

| 路径 | `【直接确认】`的角色 |
| --- | --- |
| `Inkeys/src/i18n/zh-CN.jsonc` | 基准语言 |
| `Inkeys/src/i18n/*.jsonc`（除zh-CN） | 当前其他十九种语言，脚本自动枚举 |
| `Scripts/i18n.ps1` | `check`/`sync` 工具 |
| `Scripts/i18n.zh-CN.snapshot.jsonc` | 基准语言同步快照 |
| `Inkeys/IdtI18nKeys.g.h` | `sync` 生成的 key header |
| `Inkeys/IdtI18n.cpp/.h` | 运行时加载和查询 |

`【直接确认】` `Scripts/i18n.ps1 sync` 以 `zh-CN.jsonc` 为基准，更新其他语言结构、快照和 `IdtI18nKeys.g.h`；生成 header 带有 “Do not edit manually” 标记。

因此，文案变更应：先改基准 JSONC，再按任务授权运行 `sync`、处理翻译标记、运行 `check`，并审查所有生成差异。只审计时使用 `check`；`sync` 会写文件，不能在未授权的只读/文档任务中执行。不要手工编辑 `IdtI18nKeys.g.h`。

`【直接确认】` 本地化连接符允许有意空串：仅当基准节点本身为 `""` 时，目标语言显式 `""` 通过 `sync/check`（例如中文 HeroJoin，英文对应空格）。缺失或非字符串节点、非空基准的空翻译、明确待翻译标记及基准变更仍沿用原校验；不得把所有空翻译一律视为完成。

实验选项的控制台诊断由旧 `SettingsUI/Experimental/ConsoleOutput/TouchArea` 文案键和 `Experimental.Inkeys3.ConsoleOutput.TouchArea` 保存键继续承载；扩大输出内容时只更新全部支持语言的可见名称/说明，不改键以保留已有用户选择。

### 二十语言身份、字体与验证合同（2026-11-02）

#### 1. Scope / Trigger

新增地区语言、改变语言选择/资源/字体、修改带格式参数的译文或处理固定文字槽时适用；当前活动UI包括主栏/更多/组件、设置及DDB详情、白板/分页、MessageBox。原始诊断数据、初始化前故障兜底和已证实不可达旧UI不因资源扩展而恢复或改动。

#### 2. Signatures

- `Inkeys/IdtI18nLocales.h`：`Locales`、`IsValidIndex(int)`、`LocaleForIndex(int)`、`FindLocale(std::wstring_view)`、`IndexForSystemLanguage(LANGID)`、`NativeLabel(const Locale&)`；`DisplayOrder`、`DisplayIndexForLocale(int)`、`LocaleIndexForDisplay(int)`。
- `I18n::load(1, L"JSON", code)`、`I18n::languageId()`、`I18n::locale()`；元数据包含实际JSON resource、LANGID、Regular/Bold资源及字体族。
- `BarUIRendering::FitButtonLabelFontSize(content, fontSize, width, height, weight)`；`Word(..., bool fitButtonLabel = false)`默认不变，仅主标签开启；拟合比例同时作用于当前字面与fontCorrection的名义目标。
- `Setting::Design::TextFontConfig(rasterizerDensity, notoSource, TextFontRole role, notoPrimary)`：共享设置字体profile，区分源字族尺度、主字族基线和Legacy/Design角色。
- Debug harness入口 `Inkeys.exe --i18n-locales-test`，在配置/单实例/产品UI初始化前返回；另复用 `--bar-eraser-offscreen-test`、`InkeysHeadlessTests.exe --no-window`。

#### 3. Contracts

- 传统 `deploy.json/SelectLanguage` 保持 `0=en-US,1=zh-CN,2=zh-TW`；追加 `3=zh-HK,4=ja-JP,5=ko-KR,6=fr-FR,7=de-DE,8=es-ES,9=pt-BR,10=ru-RU`。后续追加 `11=id-ID,12=vi-VN,13=tr-TR,14=it-IT,15=pl-PL,16=uk-UA,17=es-MX,18=ro-RO,19=en-GB`。不能为下拉排序重排保存索引或迁移到main.json。
- 全部二十项菜单按语言码ordinal字典序显示，`DisplayOrder`由同一Locales表生成保存索引视图；Combo选中显示行须经`LocaleIndexForDisplay`再请求/保存，恢复时经`DisplayIndexForLocale`反查。非法保存值仍选英语，不能误用排序第一项de-DE；准备失败恢复原选中语言。
- 未保存选择时精确匹配新增地区，已有ReadSetting偏好优先；新加坡/澳门中文及未覆盖地区保持旧回退。非法值仅本次回退英语；未知嵌入代码加载失败，不能拿中文资源却记录为另一个语言。
- 列表显示母语名称、语言码及Beta，只有zh-CN不含Beta；通用品牌/新快捷方式zh-CN使用智绘教Inkeys，其余Inkeys，主页既定地区化标题/标语/版权例外保留。社区沿zh-CN→QQ、其余→GitHub分流。
- 新语言须同时进入JSONC、RC/resource、Locales及必要工程/Solution展示项；消息框默认按钮另按PRIMARYLANGID分派，须同步补齐且与JSONC术语一致，地区英/西复用既有按钮规则；脚本自动枚举其他JSONC，无需另加语言白名单。所有叶子完整翻译，不能把品牌/母语菜单/原生软件专名等有意同值误判为漏译。
- Noto Sans JP/KR采用固定官方来源的静态TrueType Regular/Bold，保留OFL与来源哈希；DWrite资源集合、ImGui普通/粗体/Design脸及MessageBox独立GDI+路径分别验证。主语言字形优先，SC/TC/JP/KR同字重合并仅补缺，保证任何当前语言下的母语列表可读；Noto不套Harmony光学校正。
- 语言选择改为安全帧边界热切换，具体发布/刷新/消息框快照见下节；组件注册保存的字符串副本必须随语言revision原位刷新。DDB等跨帧表存稳定key，每帧取文案，不能保存临时`IA(...).c_str()`。
- 脚本token正则只验证字面/顺序，不能证明std::format语法和参数类型。真实模板测试须使用生产参数类型（含IdtAtomic<int>下载线路、字符串笔宽与浮点指标），绕开生产异常兜底；兼容swprintf消费者另查百分号和固定缓冲边界。
- 设置Noto Design以1.448×TextOpticalScale（当前约1.40456）、Legacy以1.448/1.213（约1.193735）校准stb的hhea归一化；Harmony Design和Legacy原配置保持。merged字形共用primary的baked.Ascent，因此ExtraSizeScale按source字族，GlyphOffset按primary字族及role，不可分别给fallback套自身基线；图标、名义字号、行框和FontScaleDpi不随此补偿改变。
- 固定主栏/组件网格不因翻译扩张；短标签可在不误导操作的前提下精炼，必要局部适配必须让生产绘制与测量共用实际字体/几何。SelectClear/ClickThrough仅各自允许0或1个换行，其他资源继续按基准严格检查。

#### 4. Validation & Error Matrix

| 输入/条件 | 必须结果 |
| --- | --- |
| 旧0..10保存值，新11..19或未保存精确系统地区 | 保持旧值含义，新值/首次匹配选择正确，保存偏好不被系统覆盖 |
| 排序菜单选择、取消或失败 | 显示行和保存索引双向一致；导航未提交不写配置，默认英语不能变成de-DE |
| 非法索引/未知嵌入代码 | 前者内存回退英语；后者load失败且不伪造语言身份 |
| 资源ID错配 | 根Language、实际查询Language与请求code不符时测试失败 |
| 内嵌/合并字体缺字或真实槽溢出 | DWrite glyph-run/ImGui glyph及严格边界测试报具体locale/key/尺寸，不以RC条目存在或全局缩字冒作通过 |
| std::format说明符/类型不合法、兼容printf意外转换 | 真实模板专项失败，不允许靠fallback字符串隐藏异常 |
| 目标语言处于空串/待译标记 | 除基准自身为空的连接符外，check失败；发布资源不能靠中文fallback代替完整翻译 |

#### 5. Good / Base / Bad Cases

- Good：新增语言先登记共享元数据、内嵌资源及实际字体，补齐全部当前key后运行三类无窗口验证。
- Base：旧配置仍0/1/2，改变显示名称或新增语言不会改写原选择。
- Bad：只增加下拉或JSONC，让启动上限仍为2；只跑token比较；在测量里使用生产绘制没有的字体/字号；把Pierce伪造成Select标签转换验证不存在的生产路径。

#### 6. Tests Required

二十语JSONC完整性/同序/token/换行；排序后真实Combo按键选择与保存身份（含首行de-DE、en-GB/en-US及末行）；真实EXE资源Language与LANGID/索引/首次地区及Beta；所有活动格式模板的实际类型；DWrite实际glyph-run与37/70DIP槽、菜单/组件/提示；ImGui常规/粗体混合母语列表及窄/宽窗/不同倍率的真实组件、DDB、赞助长文；MessageBox默认按钮及私有字体。按用户要求完整Debug|ARM64 Solution构建后执行，无GUI/Computer Use授权时不声称真实桌面/触控/其他系统已验收。改已有文件时比较检出字节格式（本仓库core.autocrlf=true），不能把Git内部LF误当原工作区换行。

设置字体校准必须以真实字体共同汉字/H/数字的字面上下界和基线验证，并独立保留descender完整性、Noto主→Harmony缺字和Harmony主→Hangul混排基线；旧Noto profile用跨字号聚合负对照，防止仅“不溢出”放过整体过小。位图quad的小字号量化允许相对±10%或绝对≤1设备像素，不把该像素容差随DPI放大；同页真实ImGui离屏对照覆盖100/150/200%，不等同于GUI实机验收。

#### 7. Wrong vs Correct

```cpp
// Wrong：列表已有新语种，实际加载仍退回英语。
if (selection == 1) I18n::load(1, L"JSON", L"zh-CN");
else I18n::load(1, L"JSON", L"en-US");
// Correct：启动、选择、资源和字体共用稳定语言身份。
I18n::load(1, L"JSON", Inkeys::Localization::LocaleForIndex(selection).code);
```
### 全界面语言热切换与消息框快照（2026-11-02）

#### 1. Scope / Trigger

更改运行时语言、字体优先级、已缓存本地化文字、延后消费的提示请求时适用。全部程序自有表面更新；已打开独立消息框保持原语言，新请求使用新语言。原生字体渲染各自保留：设置FreeType、Bar/PageControl DirectWrite、MessageBox GDI+。

#### 2. Signatures

- `I18n::PreparedLanguage { language, values, baseRevision }`；`prepare(type,path,lang,output)`、`commit(PreparedLanguage&&) noexcept`、`revision() noexcept`。
