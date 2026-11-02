# Research: locale pipeline, fonts, and headless validation

- Query: 为 zh-HK、ja-JP、ko-KR、fr-FR、de-DE、es-ES、pt-BR、ru-RU 规划完整内嵌翻译、字体、语言列表、Win32 语言及文本验证。
- Scope: internal; 不下载字体、不修改产品代码、不启动 GUI。
- Date: 2026-11-02（研究跨午夜；任务目录按创建日保留 11-01）。

## Findings

### 已确认的产品决定

- 用户选择语言项显示“母语名称 + 语言码 + Beta”，zh-CN 不带 Beta，其余十语带 Beta。所有当前界面语言下都要正确显示混合文字的下拉列表。
- 用户确认新安装且无已保存语言选择时自动匹配新增八语；已有选择不变。现有 0=en-US、1=zh-CN、2=zh-TW 索引必须保持，八语追加。
- 当前仅规划；未批准实施。主会话负责最终设计与批准门。

### 文件与边界

| 文件 | 当前模式与本任务影响 |
|---|---|
| `Inkeys/IdtI18n.cpp:20` | `loadJsonContentFromResource` 只有 en-US=246、zh-TW=275，其他一律245（zh-CN）；未知代码会加载中文却在 `:109` 标记为请求语言。增加八语必须登记明确资源映射。 |
| `Inkeys/IdtI18n.cpp:65` | `toLanguageId` 只认识 zh-CN/zh-TW，其他返回 en-US；新增八语必须映射实际 LANGID。 |
| `Inkeys/IdtI18n.cpp:93` | 加载采用 zh-CN 完整底层 + 所选语言覆盖，空串或 `[[Need translation: ` 前缀跳过；保留该现有回退合同。 |
| `Inkeys/IdtConfiguration.h:19` | `SetListStruct::selectLanguage` 是既有整数配置，0/1/2含义明确。 |
| `Inkeys/IdtConfiguration.cpp:251`、`:546` | 传统 `opt/deploy.json` 的 `SelectLanguage` 读取/写入；不迁移到 main.json。 |
| `Inkeys/IdtMain.cpp:3018` | 启动用 `GetUserDefaultUILanguage`，简体/新加坡→1，台湾/香港/澳门→2，其余→0；新增匹配须区分已保存偏好与新装缺省。 |
| `Inkeys/IdtMain.cpp:3199`、`:3229` | 仅接受0..2，随后三语硬编码加载；要扩展合法上限与统一映射，不能只扩下拉。 |
| `Inkeys/Inkeys/UI/Setting/Setting.cpp:2918`、`:2933` | 语言列表三项，切换后写盘并立即载入字符串，再保持重启提示；保留现有交接语义，避免把语言热切换扩大成字体/所有UI即时重建承诺。 |
| `Inkeys/Inkeys.rc:149`、`:232` | TTF资源8个；JSON资源仅3个。 |
| `Inkeys/resource.h:38`、`:69`、文件末尾 | 现有语言/字体ID；最新资源已用到309，APS next=310；新ID需搜索全仓冲突后分配。 |
| `Inkeys/Inkeys.vcxproj:1263`、`Inkeys/Inkeys.vcxproj.filters:2497` | 当前6个Harmony文件有Font登记；新字体资源加入对应现有分组即可，不引入新构建方式。 |
| `InkeysRepo.sln:16` | I18n Solution Folder登记3语和脚本；新增8语可跟随现有登记模式，不改项目依赖/工具链。 |

### 脚本与 format 合同

- `Scripts/i18n.ps1:631` 自动枚举 i18n 目录中所有非 zh-CN 的 `.jsonc`，无需新增语言白名单；新增空骨架后 `sync` 可补齐所有key和待翻译标记。
- `:708` 的 sync 会写基准、所有目标、快照、生成头，然后执行 check；基准文案变化会给对应其他语言加待翻译标记（`:285`），所以最后必须逐语清零。
- `:498` 比较key缺失/多余/顺序、未完成翻译、占位符与换行。仅基准自身为空的节点可有空译文（例如 HeroJoin）。
- `:191` 占位符检测只是 `\{[^}]*\}` 正则，`:564` 起逐顺序比较token；它能抓普通占位符不一致，但不能证明 C++ `std::format` 语法或参数类型正确，也不完整理解 `{{`/`}}` 转义。
- `:583` 只允许 `UI/Bar/MainButtons/SelectClearLabel` 和 `ClickThroughLabel` 各语言0或1个换行；其他key换行数必须与基准相同。不要为长译文随意在任意key加手工换行；自然换行由布局处理。
- `:95` 写UTF-8无BOM，`:366` 固定CRLF；实施前核对现有文件实际字节格式，避免无关改动。
- 当前读取执行 `./Scripts/i18n.ps1 check`：en-US、zh-TW 都 **595/595 PASS**。报告写594，说明有快照差异，翻译应跟随当前基准而非照搬报告总数。

实际动态格式字符串（当前基准）：

| 基准路径/行 | 格式 | 调用/参数合同 |
|---|---|---|
| `UI.Bar.DrawAttributes.ThicknessFormat` `zh-CN.jsonc:115` | `粗细 {}` | `Bar.I18nFormat.h:34` 的宽度格式使用数字字符串；旧厚度函数`:13`接受int。 |
| `UI.Bar.Diagnostics.FrameRateFormat` `:137` | 两个`{:.2f}` | `Bar.I18nFormat.h:63` 两个double。 |
| `SettingsUI.Regular.Appearance.BarZoom.Ind` `:527` | `{:.2f}` | 当前活动`Setting.Pages.cpp:1033`的主栏倍率行未消费Ind；仍随资源校验保留格式合同，不恢复旧行。 |
| `SettingsUI.Draw.Eraser.PalmEraser.ThresholdSizePhysical` `:678` | 两个`{:.2f}`+两个`{:.0f}` | `Setting.Pages.cpp:638` 的widthCm/heightCm/widthPx/heightPx。 |
| `SettingsUI.Draw.Eraser.PalmEraser.ThresholdSizePixels` `:679` | 两个`{:.0f}` | `Setting.Pages.cpp:640` 的widthPx/heightPx。 |
| `SettingsUI.PlugIn.PPTHelper.WidgetScale.Ind` `:840` | `{:.2f}` | `Setting.cpp:3904` 单float。 |
| `SettingsUI.Update.Downloading` `:974` | `{}` | `Setting.cpp:3007` 的downloadLine。 |

上述七条路径已对当前JSON递归核对。验证至少包括所有实际格式调用的真实参数类型、格式说明符、参数个数与顺序、转义大括号，不能只因脚本PASS宣称安全。`Bar.I18nFormat.h:20/:44/:72` 捕获format_error并回退，测试不能把“未抛出”当成模板成功；须验证期望的单位/文本片段与数值都保留。`Setting.cpp:3904` 没有相同的兜底，损坏格式可能直接逸出。

### 字体不仅是RC登记

- 当前 `Inkeys/src/ttf/` 只有SC/TC Regular/Bold、Condensed Regular/Bold及两个图标字体；没有Noto JP/KR，也没有其授权文件。未在本次研究下载字体。
- DWrite：`IdtMain.cpp:3373` 仅向 `RenderPipeline::InitializeFontCollection` 注册SC/TC四font。`RenderPipeline.cpp:1191` 通过现有资源file loader构建custom font collection，字体由sharedAssets持有；沿用生命周期，不安装系统字体。
- `Bar.Rendering.cpp:288` 的 `ConfigureLocalizedTypography` 仅区分TW→TC、其他→SC，textLocale也仅en-us/zh-cn/zh-tw。新增语言需选择JP/KR族，HK使用TC（实际香港字形覆盖须验证），西文/俄语使用已确认覆盖的现有字体并传实际locale。该函数还清空细刻度数字layout缓存，保留语言/字体失效行为。
- PageControl `PageControl.cpp:124`、Whiteboard `Whiteboard.cpp:42` 均复用 `BarPageSurface`，因此共享Bar渲染字体策略，但各自短文字槽/面板仍需实际文本测量。
- ImGui：`Setting.cpp:1854` 资源内存加载、`:1871` 重建；`:1883`只识别TW，普通/粗体`:1926`使用SC或TC；传统脸后合并SC回退。Design脸`:1936`另建，需同时接入，不能仅修旧ImFontMain。
- `Setting.Typography.h:24` 的 `HarmonyFontConfig` 含Harmony专用光学校正`ExtraSizeScale`及baseline偏移；Noto不能未经实际字形测量就假定同一校正适用。尽量保持行框和现有SC/TC行为，按face局部校准。
- ImGui `imconfig.h:89` 未启用FreeType，默认stb_truetype（`imgui.h:3797`）。应优先选择经本仓库stb和DWrite实测可读的静态TrueType字体；变量字体/格式及字重未经验证，不能只确认扩展名。Regular/Bold或其他字重方案由实际可用资产与测量决定，无须先让用户解决工具兼容问题。
- 列表始终混合英语、三种中文、日文、韩文、西文和俄文；即使当前en-US或zh-CN，列表也必须能显示`日本語`和`한국어`等。需给普通与粗体/Design实际使用字体增加对应fallback/merge，选定语言的主字形优先，其他语言仅补缺，避免日本/中文共享汉字误换地区字形。
- `Setting.Controls.cpp:1454` 的ComboRow按最长文本测宽+48DIP，但最终上限320DIP；`Setting.Design.h:373` 空间不足会stack。母语+码+Beta后，应使用真实字体检查最长项、箭头和选中项/展开项，不假定320足够，也不要先全局扩大全部combo。

### 弹窗及隐含本地化消费者

- `MessageBox.Layout.cpp:119` `ResolveButtonLabels` 只提供繁/简/英语；新增语言只有JSONC翻完仍会残留英文OK/Cancel/Yes/No。应在本轮添加默认按钮对应语言并保持独立模块的fallback合同，不强制引入应用全局i18n依赖。
- `MessageBox.Window.cpp:761/:802` 按LANGID仅选简中、繁中、Latin系统字体候选。检查日/韩与俄文实际显示及测量；不能把“DWrite内嵌字体已注册”等同于GDI+私有/system字体路径也已接入。
- `message_box_tests.cpp:206` 已有三语默认按钮测试，`:158` 保留system fallback请求语言；可扩展语言矩阵。这组测试含会创建窗口的条目，运行必须带总入口`--no-window`，不要单独启动GUI专项。
- 语言切换后重启提示正文即用新语言，重启前字体仍可能保留旧atlas，因此混合字形支持也保障这段交接可读。

### 可复用的验证入口

- `Scripts/i18n.ps1 check`：只读结构/完整率/token/换行检查，已执行baseline通过。
- 主Solution Debug|ARM64构建：遵守用户动态定位ARM64原生MSBuild、同invocation Path/PATH规范化、至少5分钟超时规则。当前spec与用户对PptCOM依赖描述有历史差异，本任务不因此改工程配置，按用户指定Solution执行。
- `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window`：总入口 `animation_tests.cpp:1743/:1822`；覆盖CPU ImGui、MessageBox无窗口部分、UI3算法。`InkeysHeadlessTests.vcxproj:60`确认输出位置。
- 设置真实字体/实际renderer回放：`setting_design_tests.cpp:5785`，现载5种源并循环七倍率，`:5851/:5868/:5883/:5942`仅三语；扩至11语并加入Noto字体/母语列表特例。支持`--setting-home-only`、`--setting-general-only`等专项入口，但总no-window仍为最终回归。
- UI3真实DWrite测量/离屏：`Bar.EraserAttribute.Test.cpp:1124`只有三语字体/locale矩阵，`:1192`还有“只有英语短标签换行”的旧测试假设，新增语言后应按key允许换行合同核验。生产命令`Inkeys.exe --bar-eraser-offscreen-test`由`IdtMain.cpp:1723`在产品UI/配置/单实例之前返回，不创建产品窗口。
- 其他已有UI3无窗口命令：`--ui3-multitarget-benchmark`、`--ui3-workspace-centering-test`，如改到共享字体/页面路径可选足够的既有专项，而非无条件反复跑所有耗时项。
- 最终保留编码/换行和git diff审查；本research未执行sync、编译或可执行文件测试。

## Related specs

- `.trellis/spec/native-desktop/index.md`：产品UI3与设置路径、无窗口边界。
- `.trellis/spec/native-desktop/configuration-i18n-and-assets.md:281`：zh-CN基准→sync→翻译→check；不手改生成头。
- `.trellis/spec/native-desktop/rendering-and-ui.md:2192`：实际字体ImGui回放；`:2316`语义换行；`:2571`起字体整数像素/连续动画合同。
- `.trellis/spec/native-desktop/build-and-compatibility.md`：现有构建入口、输出与平台声明不等于实测。

## External references

- 本次没有浏览或取得外部字体。实施前应从Noto官方发布核验具体字体格式、字重、版本/来源与OFL授权，记录资产校验及体积；不需为此升级渲染依赖。

## Caveats / Not Found

- 未查到现成十一语资源加载/format完整类型矩阵测试；现有脚本token比较不足以替代真实C++格式化。
- 未执行逐字符cmap覆盖测量，因此不能从“有TC”推断全部香港用字已覆盖，也不能从Harmony名称推断全部俄文/重音字符覆盖。
- 默认按钮翻译和系统字体fallback需纳入实现，但启动失败/极早期系统对话框应保留系统可用性，不能强制等应用字体初始化。
- 本轮无GUI授权，只能声称静态/无窗口测量覆盖，不能声称实机视觉/窗口输入已通过。
