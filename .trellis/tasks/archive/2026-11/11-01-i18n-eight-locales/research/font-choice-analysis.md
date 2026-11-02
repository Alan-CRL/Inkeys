# Research: HarmonyOS Sans 与日/韩字体的选型

- Query: 设置页日语/韩语偏小、偏细，而主栏正常时，是否可以改用HarmonyOS Sans，或应校准当前Noto？
- Scope: mixed；只读字体表、当前消费者及官方资料，不修改产品、不编译、不启动GUI、不安装系统字体。
- Date: 2026-11-02

## Findings

### 结论

建议先校准设置页的Noto字体配置，保留主栏当前表现；同时把Harmony SC日语作为用户偏好的真实对照方案，而不是直接否定它。现有SC确实能覆盖当前全部679条日语UI所需字符，但这只证明可显示，尚不能证明日本地区字形合适。韩语不能完全用当前Harmony字体替换：现有SC/TC没有现代韩文音节，必须保留具备Hangul的字体。

更换字体家族本身不能消除当前ImGui的字体度量归一化差异。最小可执行下一步是固定同一设置页面、相同DPI、相同行框，比较“现状Noto／校准Noto／Harmony SC日语”三个试样，并单独检查韩语Noto的字面高度和视觉字重。

### 当前资产的直接证据

使用本机隔离的fontTools 4.60.1只读检查 `Inkeys/src/ttf/`。下表是Unicode块内cmap条目数，不是该块所有码位均应有字符的声明。

| 当前文件 | cmap条目 | 平假名块3040–309F | 片假名块30A0–30FF | 现代Hangul AC00–D7A3 | hhea ascent/descent | UPM | usWeightClass |
|---|---:|---:|---:|---:|---|---:|---|
| Harmony SC Regular / Bold | 29527 | 88 | 94 | 0 | 928 / -285 | 1000 | 400 / 700 |
| Harmony TC Regular / Bold | 14593 | 88 | 93 | 0 | 928 / -285 | 1000 | 400 / 700 |
| Harmony Condensed Regular / Bold | 807 | 0 | 0 | 0 | 928 / -280 | 1000 | 400 / 700 |
| Noto Sans JP Regular | 16732 | 93 | 96 | 0 | 1160 / -288 | 1000 | 400 |
| Noto Sans KR Regular | 23174 | 93 | 96 | 11172 | 1160 / -288 | 1000 | 400 |

主会话另对当前679条完整UI做逐字符检查，结果记录在 `font-face-measurements.json`：Harmony SC对日语UI缺字0，韩语缺456；TC对日语缺71、韩语缺464。该结果比“字体支持多语”宣传更适用于本仓库，但它仍是当前资源字符串的覆盖，不包括未来译文、用户昵称、外部文档名称等任意输入。

### Harmony能显示日语，不等于具备日本地区字形

当前SC/TC的GSUB表确有 `locl` 标签，但ScriptList只有 `DFLT` 与 `latn`，语言记录只有 `CAT / MOL / ROM`，未发现 `hani / kana / JAN / KOR`。不能把“存在locl”解释成支持中日汉字地区替换。

Noto JP的GSUB中有 `hani / kana` 及 `JAN`，KR有 `hang / hani` 及 `KOR`。同时，当前ImGui的默认stb路径按码位直接 `stbtt_FindGlyphIndex`（`imgui_draw.cpp:4689–4691`、`:4717`），未发现按日语locale执行OpenType shaping/locl的路径。因而即便传入语言标签，也不能假定ImGui会把SC已有汉字换成日本字形。

Noto官方部署文档明确区分地区子集和语言专用字体，跨地区字形选择需要应用支持语言标签及OpenType locl。这支持继续保留JP/KR区域主字体，并以正确原生字形作对照基准。[Noto官方部署说明](https://github.com/notofonts/noto-cjk/blob/main/Sans/README.md)

### 官方Harmony发布情况：区分现有资产与新包

OpenHarmony官方资源库的fonts目录同时提供Harmony SC/TC与独立的NotoSans JP/KR Regular/Bold/Light资产；本次核验的该目录中未列Harmony JP/KR家族。README列出的105语言来自中文、拉丁、西里尔、希腊和阿拉伯语言系统。不能据此声称“只用Harmony即可替代所有日/韩字体”。[官方fonts目录](https://github.com/openharmony/utils_system_resources/tree/master/fonts)、[官方README](https://github.com/openharmony/utils_system_resources/blob/master/README.md)

但不能进一步断言“华为所有最新包都不含日/韩支持”：华为当前官方资源页列有21.6MB的HarmonyOS Sans包，日期为2026/06/12；旧resource-V1仍列52.2MB/2022包。已把当前公共下载链接交给主会话，仅供临时目录只读检查。主会话分别尝试InvokeWebRequest和带官方Referer/UA的curl，两次均返回HTTP200但响应体为0字节，未取得有效ZIP；这只说明本次runner未能取得包，不证明该公开下载对所有用户不可用。包内家族、cmap、字形及许可仍未核验，不能把它当作已验证替代品。[当前华为官方资源页](https://developer.huawei.com/consumer/cn/design/resource?ha_source=zxqy-IT&ha_sourceId=89000468)

公开字体包链接：

<https://alliance-communityfile-drcn.dbankcdn.com/FileServer/getFile/cmtyManage/011/111/111/0000000000011111111.20260627152129.89276966309836366526585265125586%3A50001231000000%3A2800%3AA0161E048334FE0271F9F5ECBBD5070D17381C7846125F4D7109DBC7B532C715.zip?needInitFileName=true>

### 为什么应先修设置页校准

`Setting.Typography.h:19–29` 的Harmony Design配置使用 `ExtraSizeScale=1.213×0.97=1.17661` 和baseline偏移；`:32–40` 的Noto配置保持默认1。`Setting.cpp:1883–1920` 按家族选择这两套配置，并把Noto配置同时用于legacy与Design源。

当前stb路径在 `imgui_draw.cpp:4667–4670` 先用 `stbtt_ScaleForPixelHeight` 再乘ExtraSizeScale；其使用hhea ascent-descent（`imstb_truetype.h:2667`）。相同名义字号下，现状Design的每字体单位几何比例为：

```text
Harmony = 1.17661 / 1213 = 0.00097
Noto    = 1 / 1448      ≈ 0.00069061
Noto / Harmony         ≈ 71.2%
```

字形本身的轮廓高度也不同，因此71.2%不是每个屏幕文字的精确像素比例，但它足以解释明显偏小并带来偏细观感，而不需要先认定Noto的400/700文件错误。

如果先以相同em字面作为起点，Noto Design的几何归一化起点约为 `1.448×0.97=1.40456`；legacy路径若只补偿与Harmony原生hhea差异，则约为 `1.448/1.213≈1.19373`。这两个数只是推导起点，最终须比较同字样真实ink高度、基线、行框和截图；不能不分legacy/Design直接给所有Noto源统一1.40，也不能直接复制Harmony的1.17661后便宣布等效。

主栏走DWrite em字号与局部槽适配；设置页走ImGui/stb。该差异与主栏正常、设置明显变小的观察一致。不要为修设置页去改全局Locales字体族或替换现有400资源，从而连带改变主栏与MessageBox。

## 方案比较与下一步

| 方案 | 可行性/收益 | 成本或限制 | 建议 |
|---|---|---|---|
| 校准当前Noto JP/KR的设置页字体源 | 保留正确区域主字形、已有字体与真Bold，针对现有度量问题修复 | 需分别校准legacy/Design与同字重fallback源，验证baseline/行框 | 第一阶段首选 |
| 日语设置页改Harmony SC主字体，Noto补缺 | 当前679日语文字可显示，接近用户喜爱的中文风格 | SC汉字地区形态/部分假名字形待对照；未来输入仍需fallback；不应顺带改主栏 | 必做A/B候选，视觉与地区字形共同验收后再选 |
| 韩语设置页Harmony SC主字体+Noto KR补缺 | 可让Latin/数字沿Harmony | 韩文主体仍全部来自Noto，汉字/Latin/Hangul可能混不同字面/字重；并未摆脱Noto校准 | 可试，但不是纯Harmony替换，也不是优先修复路径 |
| 换另一家JP/KR字体或更新Harmony包 | 可能获得更贴近目标的字形设计 | 必须重做覆盖、真实weight、stb度量、baseline、许可和体积验证；不能靠换名避开度量问题 | 新官方包先只读检查，其余家族后置 |

### 建议验收顺序

1. 同页、同DPI、同名义字号生成中文基准、日语现状、日语校准Noto、日语Harmony SC、韩语校准Noto试样。包含相同汉字、假名、Hangul、Latin/数字及混合语言下拉。
2. 优先统一可见ink高度和基线，保持控件/行框、图标、点击区域与中文现状；不要用全局FontScaleDpi或全局字号变化掩盖单独字体源的问题。
3. 先保留Regular400/Strong700。字号恢复后若仍偏细，再针对设置页比较Body500与400，标题维持真实700；不要把所有文字改700、绘制两遍或以全局alpha加深冒作字重。若增加500资产，保持现有主栏400资源不变。
4. 对Harmony日语候选人工核对具有地区差异的汉字与长句。中文同码位可显示、或cmaps零缺字均不能替代此检查。
5. 新增跨字体视觉尺度回归（同名义字号的实际ink高度/基线/strong层级），再跑所有现有文字槽/换行/混合fallback与无窗口回归。此前“零缺字、无越界”不足以保证视觉大小一致。

## Files Found / Code Patterns

- `Inkeys/src/ttf/HarmonyOS_Sans_SC_Regular.ttf`、`HarmonyOS_SansSC_Bold.ttf`：现有SC，v1.9；Kana/Han覆盖与上述GSUB检查对象。
- `Inkeys/src/ttf/HarmonyOS_Sans_TC_Regular.ttf`、`HarmonyOS_SansTC_Bold.ttf`：现有TC，v1.9；不提供Hangul音节。
- `Inkeys/src/ttf/HarmonyOS_Sans_Condensed_{Regular,Bold}.ttf`：v1.0，当前仅807码位，不能作JP/KR替换。
- `Inkeys/src/ttf/NotoSansJP-{Regular,Bold}.ttf`、`NotoSansKR-{Regular,Bold}.ttf`：当前静态400/700；来源及OFL见同目录README。
- `Inkeys/IdtI18nLocales.h:29–30`：JP/KR的全局字体资源/字体族映射，修改会波及多个消费者，不适合作为只修设置页的第一落点。
- `Inkeys/Inkeys/UI/Setting/Setting.Typography.h:19–40`：Harmony/Noto source配置差异。
- `Inkeys/Inkeys/UI/Setting/Setting.cpp:1883–1946`：primary、同字重fallback与legacy/Design绑定。
- `Inkeys/additional/imgui/imgui_draw.cpp:4667–4707`：stb缩放、ExtraSizeScale与layout度量；`:4689–4691`、`:4717`直接码位查glyph。
- `Inkeys/additional/imgui/imconfig.h:89`：默认没有启用FreeType。

## Related Specs

- `.trellis/spec/native-desktop/configuration-i18n-and-assets.md`：十一语言身份、区域主字体、同字重fallback与重启生效合同。
- `.trellis/spec/native-desktop/rendering-and-ui.md`：设置页legacy/Design光学校正、固定行框与真实字体回放；主栏DWrite/整数像素策略。
- `.trellis/spec/native-desktop/index.md`：设置ImGui与主栏DWrite的实际边界；未经授权不启动GUI。

## Caveats / Not Found

- 本研究不宣称当前SC是完整日本地区字体；也不宣称最新官方21.6MB包一定与本仓库v1.9相同。新包内容由主会话另行核验。
- GSUB表检查只证明当前字体的声明与路径，未制作日本字形人工审校清单，也未采用外部shaping引擎作替代渲染。
- 本次没有下载字体到产品目录、修改产品/字体资源、编译或GUI测试；数值是字体表与当前源代码的计算，不是修复后实机效果保证。
- 推荐的缩放数值必须通过同页真实字形测量和视觉比较收敛，不能当作无条件直接写入代码的最终常量。
