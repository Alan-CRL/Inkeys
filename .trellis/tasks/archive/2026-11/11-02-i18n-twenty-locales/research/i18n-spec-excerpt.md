# 最终上下文摘录

来源：`.trellis/spec/native-desktop/configuration-i18n-and-assets.md`，2026-11-02本任务最终工作树。后续原规范变化时重新读取对应章节。

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
- `I18n::TextSnapshot { LANGID language; vector<wstring> text; }`；`capture(initializer_list<const char*> keys, TextSnapshot&, bool nonblocking=false) noexcept`。
- `BarButtonSetClass::RefreshLocalizedText()`、`BarUIRendering::RefreshLocalizedTypography()`；Setting帧边界语言请求与字体替换。

#### 3. Contracts

- prepare构造候选map但不改变当前语言；commit在独占锁内检查baseRevision，再交换map/语言并递增revision。未知语言、缺失资源或过期候选不发布；保存SelectLanguage仍沿旧配置路径/索引，只有成功应用才写回。
- 发布者不在i18n锁内执行字体生成、D3D或跨窗口调用。生产切换由共享渲染线程安全帧边界完成；不在绘制Combo期间修改锁定atlas。成功后主动唤醒Bar/PageControl，不能仅更新revision而等待下一次用户输入。语言页不显示即时生效说明，简中说明使用“界面使用的显示语言。”；其余注意提示默认保持图标。
- Bar固定/状态型/组件标签原位更新，保留对象与工具状态；每个独立renderer同时更新字体族、locale、测量缓存。PageControl在业务revision不变时也消费语言revision，隐藏表面下次呈现使用新版本。记录所消费的版本，不能用“旧语言数据+随后读取的新revision”跳过刷新。
- capture在一次shared_lock内获取整组UTF-16文案和LANGID；缺少任一键/异常时保持output不变。nonblocking模式仅try_lock，崩溃路径不得改为阻塞读取；失败整组使用一致fallback。
- 排队提示在生产者侧冻结正文、标题、按钮与语言，worker只消费owned数据；正文若由同一渲染帧生成，要维持该帧内不能插入语言commit的合同。其他线程用同一capture取得所有字段。MessageBox保存request.language，DPI/主题恢复仍从该快照选字体。
- 系统已托管的历史通知、外部插件内容和用户数据不改写；不得为了语言刷新重启进程或重建整个业务UI。

#### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| prepare失败/候选revision过期 | 不发布，不持久化新选择，旧语言可继续使用 |
| 字体候选初始创建/上传失败 | 恢复旧atlas及字体绑定，不提交语言 |
| 发布后动态新字形上传失败 | 不Present失败帧，沿既有Retry/DeviceLost恢复；不冒称全部未来字形均已预热或已回滚语言 |
| 静止可见Bar/分页窗口 | 提交后主动重绘，无需额外输入 |
| 旧提示入队后切换语言 | 旧请求全字段一致，新请求全字段采用新快照 |
| capture try_lock失败 | 不修改已有output，崩溃调用者使用整组英文fallback |

#### 5. Good / Base / Bad Cases

- Good：zh-CN→ja-JP时文字和字体优先级同步，Bar工具/白板内容不变；旧消息框仍为中文。
- Base：同字族语言复用字体，刷新文本和缓存即可。
- Bad：逐个IW再单独languageId拼消息框；只替换JSON却保留初始化标签或静止表面；先写配置再尝试加载。

#### 6. Tests Required

`--i18n-locales-test`包含候选/过期提交/并发成组快照；`--bar-eraser-offscreen-test`保留工具和按钮identity，并经PageControl::RunOffscreenTests验证四个表面的真实ConfigureSurface语言变化；`--ui3-multitarget-benchmark`验证真实Bar RenderFrame的语言刷新；`--setting-font-lifecycle-test`在无HWND WARP中经过生产字体替换和失败路径。Debug harness须在GUI初始化前返回；实际执行结果记录于任务验证文档，不以源码存在推断通过。

#### 7. Wrong vs Correct

```cpp
// Wrong：三次读取可能跨越语言发布。
auto title = IW(titleKey);
auto body = IW(bodyKey);
auto language = I18n::languageId();
// Correct：一次快照为整个请求选文案与字体身份。
I18n::TextSnapshot snapshot;
if (I18n::capture({titleKey, bodyKey}, snapshot)) {
    // 构造拥有snapshot.text和snapshot.language的消息框请求。
}
```
