# Research: 设置页日/韩字体偏小偏细的渲染管线分析

- Query: 解释用户截图中 zh-CN 设置页效果正常、ja-JP 设置页偏小偏细而主栏正常的原因，提出最小修正与验收方案。
- Scope: internal；只读源码和既有字体测量结果，不改产品代码、不编译、不打开GUI、不提交或归档。
- Date: 2026-11-02。

## Findings

### 1. 最主要差异是 ImGui 的字号归一化，不能只归因于 Noto 字重

设置采用仓库内 ImGui 1.92.7（`Inkeys/additional/imgui/imgui.h:32`）和默认 stb_truetype 后端；FreeType 开关未启用（`imconfig.h:89`）。字体加载器使用：

1. `imgui_draw.cpp:4667`：`stbtt_ScaleForPixelHeight(..., 1.0F)`。
2. `imgui_draw.cpp:4670`：再乘 `ImFontConfig.ExtraSizeScale`。
3. `imgui_draw.cpp:4724-4727`：按该比例乘实际 baked 字号及 rasterizer density，生成字形尺寸和advance。

`imstb_truetype.h:2639-2644` 明确从 **hhea** 读取 ascent/descent；`:2665-2669` 的缩放是 `pixelHeight / (ascent - descent)`，不是按 unitsPerEm。与 `:2671-2675` 的 `ScaleForMappingEmToPixels` 是两种尺度。

项目另给 Harmony Design 字体 `ExtraSizeScale = 1.213 × 0.97 = 1.17661` 及参考基线偏移0.75，而 Noto 配置保持默认1（`Setting.Typography.h:16-40`）。这两个因素相乘，不是仅有17.7%的差距。

根据主会话的 `research/font-face-measurements.json`：

| family / role | unitsPerEm | hhea ascent / descent | span | ExtraSizeScale | 名义14时的等效em |
|---|---:|---|---:|---:|---:|
| Harmony SC/TC Design，Regular/Bold | 1000 | 928 / -285 | 1213 | 1.17661 | 13.580 |
| Noto JP/KR 当前，Regular/Bold | 1000 | 1160 / -288 | 1448 | 1 | 9.669 |

模型公式为 `nominalSize × ExtraSizeScale × unitsPerEm / span`。当前 Noto 仅为 Harmony Design 的 **71.20%** 等效em。同一公共字「設」的实际轮廓bbox模型高度从 Harmony Regular 的12.439降为Noto Regular的8.586，方向与截图高度一致。

这不表示图片中的每个字都精确小28.8%：截图页面/文字不同，字体字面、DPI、舍入、栅格化也会影响具体像素。

### 2. 真粗体已绑定；先把尺度修正，再评估是否改字重

- `Setting.cpp:1921-1924` 分别生成普通/粗体及Design普通/粗体脸。
- `Setting.Shell.cpp:15-21` 将Body/Caption绑定regular，将BodyStrong/Subtitle/Title/TitleLarge/Display绑定strong；Title名义28，Body14，Control13（`Setting.Typography.h:12-15`、`Setting.Controls.cpp:107`）。
- `PageHeader` 使用Title，`SectionHeader` 使用BodyStrong（`Setting.Controls.cpp:541-552`）。并不是所有文本误用了Regular。
- 元数据里的JP/KR明确使用对应Regular/Bold资源（`IdtI18nLocales.h:30-31`），测量文件确认OS/2权重为400/700。

公共字「設」同em归一化轮廓面积，Harmony400≈0.333994，Noto400≈0.349037；Harmony700≈0.480432，Noto700≈0.531953。此样本不支持“Noto天生更细”的判断。乘上当前0.712的尺寸比例平方后，预计ink总面积只有Harmony约53%（Regular）/56%（Bold），足以产生明显偏细感。

轮廓面积只作为单字对照证据，不能替代全字库视觉字重评价，也不等于实际抗锯齿像素覆盖。建议先修正尺度，再比较Noto400/700与既有Harmony；不应先把全部Body改成Medium或描边加粗。

### 3. 主栏正常有独立、可解释的原因

主栏走DWrite而非ImGui：

- `Bar.Rendering.cpp:288-298` 按同一locale快照选择family和locale。
- `Bar.Format.cppm:40-81` 把字号直接传入 `IDWriteFactory::CreateTextFormat`。
- `Bar.Rendering.cpp:3138-3153` 的Word通过TypographyCache获取DWrite format；`:3214-3241` 的测量也使用DWrite。

这条路径没有调用stb的 `ScaleForPixelHeight`，也没有使用设置页 `NotoFontConfig` 的ExtraSizeScale。因此当前Noto的真实em字面在主栏基本正常，而在ImGui设置中缩小；用户观察并不矛盾。

设置中的图标另有 `IconFontConfig`（Typography.h:44-55）和独立图标脸，导航图标还使用固定24DIP轴（`imfluent.cpp:5331-5335`）。图标/间距没有同步变小，也是“文字局部问题”的佐证。不要为修文字放大FontScaleDpi或所有设置几何。

### 4. 修正必须区分 family 与 role，避免一个函数改坏其他字体

当前 `RebuildSettingFonts` 的选择条件是 `design && !noto ? HarmonyFontConfig : NotoFontConfig`（`Setting.cpp:1897-1900`）。所以名为Noto的配置还被用于 **legacy Harmony脸**，以保留它原来的不放大配置。

不能直接把 `NotoFontConfig.ExtraSizeScale` 改为1.40456：这会同时放大legacy Harmony，影响标题栏，并把legacy Noto放得超过它的对照目标。

最小方案应明确区分四种组合：

| role | Harmony | Noto的等em起点 |
|---|---|---|
| Design（内容/侧栏） | 保持现有1.17661 | `1.448 × 0.97 = 1.40456` |
| Legacy（标题栏边界等） | 保持1 | `1448 / 1213 ≈ 1.193735` |

内容与侧栏分别在 `Setting.cpp:2822`、`:4662` 绑定Design，离开后`:4640`、`:4675`恢复Legacy。标题栏的Caption在`:2644-2708`测量和绘制。因此两种role都应检查，不能仅修其中一套。

建议将当前模糊选择拆成“按family+role取profile”，保留现有名义字号、行框、图标、DPI几何；无需修改全局 `IdtI18nLocales`，也无需动已经正常的主栏。

### 5. ExtraSizeScale只改字面，基线/混合fallback需另行实测

`imgui_draw.cpp:4703-4707` 初始化baked ascent/descent时特意除掉ExtraSizeScale；字形位置还受`:4776-4781`的GlyphOffset缩放、舍入及baked Ascent影响。因此仅改Extra不能假定基线已经正确。

两字体的hhea ascent/span分别约0.765和0.801；同一名义字号的baked基线会有不同舍入结果。应以相同公共汉字、Latin ascender/descender、假名及韩文实际DrawData/像素边界比较，校准Noto的GlyphOffset或受约束的角色基线补偿，不盲拷Harmony的0.75参考偏移，也不在研究阶段拍定一个固定像素数。

fallback也必须按自己的family profile校准。现在Design fallback Harmony已经是较大字面，Noto primary较小；混合文字可能出现同一行大小不一。把所有source按同一目标em校准后再检查baseline，不能仅放大primary而漏掉fallback。

### 6. 下一步实施顺序与验收建议

1. 保留当前Noto JP/KR与字体权重/资源；先只修改设置字体profile选择与Noto Design/Legacy尺度、基线。
2. 用真实ImGui后端做无窗口对比板：相同汉字「設定・言語・基本・漢・骨・直・返」、同一Latin样本、日文假名和韩文，包含普通/粗体及Body13/14、Subtitle20、Title28、TitleLarge40，在100/125/150/200%等适用倍率输出图像或有效像素/顶点数据。保留zh-CN作为不变基准。
3. 第一个候选Extra采用上表推导值，再按实际字面微调；不是先改global字号。主栏和图标不参与这次补偿。
4. 同时验收字面高度、基线差、普通/粗体层级、混合fallback同排稳定、英文数字字号以及卡片/控件包络。此前“无缺字、无溢出”测试不能发现“全部过小”，应增加相对字面大小和视觉粗细的下限/对照，而非仅上限。
5. 尺度与基线匹配后，若Noto在目标小字号仍明显偏轻，再仅为Body候选试500、Strong试600/700的真实字重；不使用假粗体，不先升级渲染后端。
6. 若用户仍偏好Harmony日文，作为备选单独比较正确日文字形与视觉一致性；韩文不能单靠当前Harmony字库。任何替代字体也须走相同尺度/基线/字重验证，单纯换文件不保证解决stb归一化问题。

## Files Found

- `Inkeys/Inkeys/UI/Setting/Setting.Typography.h`：语义字号/行框、Harmony与Noto配置。
- `Inkeys/Inkeys/UI/Setting/Setting.cpp:1871-1945`：普通/粗体/Design/fallback脸加载。
- `Inkeys/Inkeys/UI/Setting/Setting.Shell.cpp:11-21`：文字角色绑定。
- `Inkeys/Inkeys/UI/Setting/Setting.Controls.cpp:183-213,541-563`：按角色测量、显式行框绘制、页/节标题。
- `Inkeys/additional/imgui/imgui_draw.cpp:4667-4781`：当前stb字号、字面、基线及密度实现。
- `Inkeys/additional/imgui/imstb_truetype.h:2639-2675`：hhea与em两种缩放公式。
- `Inkeys/Inkeys/UI/Bar/Bar.Format.cppm:40-81`：主栏DWrite字号通路。
- `research/font-face-measurements.json`：主会话生成的实际资产metrics/cmap/轮廓测量。

## Related Specs

- `.trellis/spec/native-desktop/rendering-and-ui.md:2147-2148`：Harmony字面校准与语义字号/行框分离，旧face和Design角色边界。
- `.trellis/spec/native-desktop/configuration-i18n-and-assets.md`：十一语言身份、字体fallback及真实验证合同。

## External References

未访问外网；以上按仓库实际ImGui 1.92.7及vendor stb源码核实，不使用其他版本文档推断当前行为。字体轮廓数据为实际已嵌入资产测量。

## Caveats / Not Found

- 本次没有编译或运行新候选profile，也没有生成校正后的对比图；1.40456/1.193735是明确的等em起点，不是已验收的最终视觉常量。
- 用户截图不是同一页面/同一文案的严格受控对比；推导比例来自代码和字体头，不是从截图强行拟合。
- Harmony对日文字形的地区适配、替代font家族选择不由本文件的管线研究作最终认定；主会话另外汇总cmap/地区字形证据。
- 只读覆盖测量不能证明字体在所有未来翻译字符、不同Windows版本或GUI实时呈现均正确；后续需保留现有字形/布局回归，再补视觉大小/字重验收。
