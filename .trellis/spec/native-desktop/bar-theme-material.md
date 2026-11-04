# UI3 Bar 主题与连续材质

## 1. Scope / Trigger

修改 Bar 深浅色、主按钮收展材质、Logo 颜色、PointLight、静态阴影或相关 dirty/viewport 范围时适用。普通图标仅重新配色，主按钮允许为整笔填充和整体轮廓调整 SVG 结构。不得由主按钮的收起状态改变真实画笔颜色或其他窗口的材质。

## 2. Signatures

生产策略位于 `Inkeys/Inkeys/UI/Bar/Bar.ThemeMaterial.h`，无设备依赖：

~~~cpp
namespace BarThemeMaterial {
    Color LightColor(ColorRole) noexcept;
    Material Resolve(double lightWeight, Color darkSurface, Color darkFrame) noexcept;
    Lighting ResolveLighting(const Material&, Color penColor, double drawingPenBlend) noexcept;
    Lighting ResolveCursorLighting(const Material&, Color penColor, double drawingPenBlend) noexcept;
    inline constexpr double SurfaceShadowOutsetDip = 8.0;
}
~~~

主按钮外观使用 `Bar.LogoAppearance.h` 中的 `BarLogoAppearance::Snapshot`、`Resolve(lightWeight, drawingWeight, actualPenColor)` 和 `ApplyAttributes(snapshot, setter)`。`BarUiSVGClass::mainLogoAppearance` 保存当前输入，`cMainLogoAppearance` 仅在位图创建成功后记录缓存快照。

`BarUiShapeClass` 与 `BarUiSuperellipseClass` 使用 `double lightMaterial`、`bool themeSurface` 和 `double lightFillOpacityScale`。前者为当前连续材质权重；`themeSurface` 表明这个对象需要主题表面配色；`lightFillOpacityScale` 是浅色端填充透明度比例，只有常驻表面（主栏、主按钮、底部吸附指示）设为 `LightResidentSurfaceOpacityScale=1.10`。PointLight 对象可用独立的 Dark/Light 鼠标光倍率端点。不经材质系统的独立 Surface（分页）用 `GetThemeResidentSurfaceOpacity()` 取得同一常驻透明度；橡皮、撤销与分页面板用 `GetThemeSurfaceFrameOpacity()` 取得与材质表面一致的边框透明度，禁用内容统一经 `BarThemeMaterial::DisabledContentOpacity()` / `GetThemeDisabledContentOpacity()` 解析。

## 3. Contracts

- `lightMaterial=0` 为旧深色端点，`1` 为浅色端点；非法/非有限权重按策略归一。主按钮在浅色主栏展开时到达 1，折叠成独立入口时到达 0。所有颜色、透明度比例、静态阴影/高光和动态光参数消费同一个 current 权重，不能在 draw 中直接按 fold 目标跳变。
- 主题背景存储显式 Dark 表面/边框作为端点，并保留原始动画 pct/framePct；renderer 解析完整材质并应用透明度比例。普通按钮不是 themeSurface，它们使用独立图标、文字、底板角色，lightMaterial 只改变其边缘光。
- Light「中性雾白」角色（v3，2026-11-04）：中性色全部无色相（R=G=B），与设置窗口 WinUI 3 浅色一致：Surface `#FBFBFB`、IconPrimary `#2B2B2B`、TextPrimary `#1A1A1A`（对表面约 13:1 / 16:1）、Accent 与 SelectedFill 同为品牌青 `#006A63`（与 Dark `#58FFEC` 同色相；压在选中底上于深色课件背景仍 ≥4.5:1）、EdgeLight `#3A3A3A`、ShadowKey/ShadowAmbient 黑。品牌青只用于选中内容、选中底和选中按钮的边缘光；中性角色不得带色相（带色相的灰与品牌青叠加会整体发绿）。SurfaceFrame `#8B8B8B`、Divider `#A7A7A7`、PressedFill `#BCBCBC`、SubtleFill `#E1E1E1` 为预混中性灰，按 `C = 251 - 目标黑色占比 × 251 / 对象透明度` 反算：在对象原有透明度（边框 18%、分隔线 30%、悬停 18%）下分别约 8%/10%/4.5% 黑；SwatchFrame 为纯黑，以色块 18% 透明度绘制即约 18% 黑。Dark SwatchFrame 与 Dark SurfaceFrame 同值，色块改用该角色后深色观感不变。不经材质系统的面板经同一角色与 `GetThemeSurfaceFrameOpacity` 得到一致结果。
- Light 选中底与 Dark 同为半透明主题色，透明度为 Dark 值乘 `LightSelectedFillOpacityScale=0.50`（选中 `0.20→0.10`，按下选中 `0.10→0.05`）；不得使用不透明色卡或额外指示条。显式 `fillOverride` 的客户端保持原透明度。
- 常驻表面 Light 填充透明度为 Dark 的 `1.15` 倍（主栏 `0.80→0.92`），深色课件背景下图标仍 ≥7:1。弹出面板（属性栏、菜单、提示）保持原透明度。基础外框保持单层 1 DIP 且全周等宽，Light 端不再加深透明度（`LightFrameOpacityScale=1.0`），8% 黑观感由预混色给出。Light 主题表面（`themeSurface`：主栏、展开的主按钮、属性/弹出面板）只绘制一圈中性 ambient 柔和投影（黑 `0.08`、半径 `6 DIP`、下移 `2 DIP`，半径 + 下移不超过 `SurfaceShadowOutsetDip=8`），随材质权重从 Dark 的 0 连续过渡；不绘制 key 阴影或顶部高光。禁用内容 Dark `0.30`、Light `0.45`。Dark 端点保持旧数值。
- 边缘光基色由 `BarThemeMaterial::ObjectEdgeLight(lightWeight, drawnFrame)` 解析：有色边框（选中按钮的主题青）保持自身颜色；中性边框 Dark 端为边框色本身，Light 端过渡到 EdgeLight 深灰（8% 黑的边框本身作光色在雾白底上不可见）。`PenWhenDrawing` 对象绘制时再向笔色过渡。`DrawPointLightFrame` 用该结果覆盖 `Material::edgeLight` 后解析两路光源；不得给非选中对象使用青色边缘光。PointLight diffuse Light 为 `0.08/0.06`（边框/笔色），低于 Dark `0.30/0.20`。
- `DrawPointLightFrame` 的基础描边、第一光源和第三光源分别解析。第一光与第三光（鼠标光）在深浅两端都默认取对象边缘光基色，绘制时跟随当前笔色并消费同一换色淡出；Light 端低对比笔色只在光影显示阶段向 IconPrimary 校正到约 `2:1`，不得写回真实 RGB。Light 第一/第三光半径约为 `360/200 DIP`，光强与 Dark 相同（`LightEdgeLightIntensity=1.0`，主栏第三光峰值约 `15%`），避免灰光发脏刺眼；选中按钮第三光倍率从 Dark `0.30` 连续到 Light `0.40`。
- 粗细滑块 Thumb 按 WinUI 3 Slider：Light 端外圈底为纯白（ControlSolidFillColorDefault），1 DIP 外圈描边为 Surface 混 TextPrimary `LightSliderThumbBorderOpacity=0.12`（对应 ControlElevationBorder 约 6%~16% 黑），中心为 Accent；不得用不透明 SurfaceFrame 画外圈。Dark 端保持旧值，两端随材质权重连续过渡。
- dirty、viewport 和自绘面板外扩继续使用覆盖最大 Dark 半径/旧阴影的保守上界；缩小 Light 光源时不得同步缩小到无法覆盖主题过渡中间帧。EdgeLighting 总开关与 Dynamic 门禁语义保持不变。
- 常驻表面另有 `residentSurface` 标记（主栏、主按钮、底部吸附指示）：填充比例由 `BarThemeMaterial::ResidentFillOpacityScale(lightWeight, lightFillOpacityScale, override)` 解析，用户背景透明度（绝对值，>0）优先，否则为主题默认（`DefaultResidentFillOpacity` = Dark 0.80 / Light 0.92）。常驻内容透明度经 `BarUIRendering::SetResidentContentOpacity()` 只在绘制主栏按钮、主按钮 Logo 与分页按钮时生效，绘制后必须恢复为 1，不得影响弹出面板。用户透明度与主题在渲染线程落地时不经动画，除 `ForceFullDamage` 外必须调用 `presentDecision.RequireVisualRetry()`；仅标记损伤时静止主栏不会提交新帧（表现为滑块可拖但主栏不变）。配置与设置页合同见 [configuration-i18n-and-assets.md](configuration-i18n-and-assets.md#ui3-常驻透明度配置合同)。
- 主题目标只来自 `Experimental.Inkeys3.UI3.ThemeMode`：`1` 为 Dark、`2` 为 Light，其余值归一到 Dark。启动与设置页均调用 `SetThemeMode()` 发布原子目标并唤醒渲染；`WM_THEMECHANGED` 不覆盖用户选择，不逐帧读配置或注册表。
- 收展使用已有时间轴和 current/target 语义；中途反向从当前值重新定向，关闭动画直接到一致端点，材质单独变化同样产生 dirty 并续帧。其他控件不继承主按钮的折叠权重。分页客户端（PPT/白板 `BarPageSurface`）不经主栏材质动画，逐帧以 `GetThemeLightWeight()`（Dark 0 / Light 1）设置自身形状与按钮的 `lightMaterial`，使边缘光强度与中性光色和主栏一致；分页不是 `themeSurface`，不绘制投影。分页按钮图标用 IconPrimary、文字用 TextPrimary、加载环弧用 IconPrimary，只有压在白板底色上的样式预览按背景亮度覆盖内容色；主题在主栏渲染线程落地时必须 `RenderPipeline::Request(PptPageMask())` 唤醒分页重绘。
- 原始笔 RGB 永远只作为输入。仅 `IdtPen || IdtShape` 启用颜色指示，Selection/Eraser 等状态收回到对应默认中性色，不显示记忆笔色。Geometry 明确使用 Brush1；活动 Pen 按既有槽解析激光/荧光/普通笔。主图标保留一个 Color 动画，普通换笔/换色从 current 继续；初始化和 Geometry 接管非 Brush 颜色历史时直接回到正确源，避免串色。PointLight 的激光替换仍限定为活动 Pen/Laser。
- 主按钮只使用一个稳定 `logo1` SVG 实例。它包含原中性笔/屏幕、4478887c 的 Frame94 渐变/笔尖/高光路径及浅色整体轮廓，通过内部属性加工实现深浅转换。Dark 端点保留原白笔底、白屏幕 `0.22` 和渐变指示，不使用整笔实色或 DisplayPenColor 提亮；Light 非绘制态笔身为 IconPrimary，屏幕为 9% 中性黑（`LightScreenOpacity=0.09`），绘制端点为真实整笔色。外轮廓（`#1A1A1A`）只给低对比笔色：`LightOutlineOpacity` 按笔色对 Surface 的对比度在 `3:1→1:1` 间给出 `0→0.55`，深色笔与非绘制态无描边。轮廓只描真正外缘，不给各个闭合笔段加 stroke；Light screen 保持中性色，Dark screen 允许原有渐变指示贡献。
- 主图标部件颜色、原渐变 opacity、浅色轮廓及绘制态填色均由 current 材质权重、独立绘制态 Pct 和当前 Color 解析；禁止用两个完整 SVG/位图交叉淡化代替。外观改变必须同时触发 dirty 和缓存失效；外观失效重建失败不得显示旧语义，尺寸质量重建失败可保留旧位图。ResetCache/设备重建只清设备缓存，不清语义外观。单实例继续使用原点击脉冲、父继承、底栏刚性变换和窗口边界。普通 SVG 的 color1/color2、几何和缓存路径不变。

## 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| Light 展开或收起至中途 | 背景、边框、阴影、高光、PointLight、鼠标光、笔色均已处于连续中间值 |
| 中途反向 | 从当前权重接续，不重置到 0/1，不等最后一帧切换 |
| 关闭动画 | 全部材质通道同帧到正确端点 |
| Dark 普通表面 | 保留旧数值和鼠标光强；无新增浅色阴影 |
| EdgeLighting 关闭 | 基础扁平边框继续绘制；第一和第三 PointLight 停止，dirty 范围仍安全 |
| Light 非绘制/绘制状态 | 两路光源从对象边框色（灰；选中按钮为主题青）连续切换到对比度合格的笔色，与 Dark 同规则 |
| 白/浅黄笔 | 完整笔形有细外轮廓，内部断口透明、无横向黑边 |
| 自定义色/工具切换 | Light screen 中性，Dark 保留原渐变；真实 RGB 不写回 UI 显示处理 |
| 非绘制模式 | 深浅端点均为默认中性样式，与记住的笔色无关 |
| DPI、底部吸附或设备重建 | Logo 层与主按钮一起变换，阴影不裁切、不残留旧像素 |

## 5. Good / Base / Bad Cases

- Good：同一材质权重连续插值表面、扁平边框、两路独立光源半径/颜色/强度，并在折叠时转成深色入口。
- Base：旧调用者未启用 themeSurface/lightMaterial 时仍为 Dark 绘制。
- Bad：只把 Surface 改白，PointLight 仍使用深色边框；或最后一帧根据 fold 同时切换边框/光源。

## 6. Tests Required

直接测试生产策略的 Light/Dark 端点与 0.25/0.5/0.75 中间值、全通道有限/连续变化、Light 中性角色无色相、预混灰的黑色占比（外框 8%、分隔线 10%、悬停 4.5%、色块 18%）、常驻表面 `0.92` 透明度、边框不加深与禁用 `0.30/0.45` 端点、深色背景上常驻浅色表面的图标 ≥7、选中内容 ≥4.5、Light 内容角色对比度（文字 ≥16、图标 ≥13、主题色 ≥4.5）、`ObjectEdgeLight`（中性边框 Light 端为 EdgeLight、有色边框保持、Dark 不变）且第三光随笔色、半透明选中底 `0.10/0.05`、单层边框、单圈 ambient 投影端点与外扩上界、第一/第三光独立强度与半径、`360/200 DIP` 半径、Light 光强 1.0、按钮 `0.30→0.40` 倍率、浅色笔对比度和真实色隔离；用生产动画属性验证反向与关闭动画。主按钮外观测试覆盖 Dark 白笔/0.22 屏幕端点、Light 非绘制态 IconPrimary 笔身与 9% 黑屏幕、真实整笔 RGB、对比度自适应描边（黑笔 0、白笔 >0.5、浅黄 >0）；对生产 ApplyAttributes 输出加工后的同一 SVG 做离屏检查，绘制态/非绘制态和过渡中点必须覆盖。

完整 ARM64 host MSBuild 构建 `InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64`，并运行 `InkeysHeadlessTests.exe --no-window`。实际桌面光影、白色文档覆盖和真实鼠标过渡属于另外的 GUI 验收；未执行时如实说明。

## 7. Wrong vs Correct

~~~cpp
// Wrong：所有对象共用固定青色光，且目标状态直接控制材质。
lightColor = GetThemeColor(BarThemeColorEnum::Accent);
mainButton.lightMaterial = fold ? 0.0 : 1.0;

// Correct：消费同一动画当前值，边缘光取本对象实际边框色，第一/第三光源分别解析。
mainButton.lightMaterial = currentMaterialWeight;
auto material = BarThemeMaterial::Resolve(currentMaterialWeight, darkSurface, darkFrame);
material.edgeLight = drawnFrameColor;
const auto primary = BarThemeMaterial::ResolveLighting(material, actualPenColor, drawingBlend);
const auto cursor = BarThemeMaterial::ResolveCursorLighting(material, actualPenColor, drawingBlend);
~~~
