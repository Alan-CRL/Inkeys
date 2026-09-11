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
    inline constexpr double SurfaceShadowOutsetDip = 8.0;
}
~~~

主按钮外观使用 `Bar.LogoAppearance.h` 中的 `BarLogoAppearance::Snapshot`、`Resolve(lightWeight, drawingWeight, actualPenColor)` 和 `ApplyAttributes(snapshot, setter)`。`BarUiSVGClass::mainLogoAppearance` 保存当前输入，`cMainLogoAppearance` 仅在位图创建成功后记录缓存快照。

`BarUiShapeClass` 与 `BarUiSuperellipseClass` 使用 `double lightMaterial` 和 `bool themeSurface`。前者为当前连续材质权重；后者表明这个对象是需要表面配色、阴影及顶部高光的背景。

## 3. Contracts

- `lightMaterial=0` 为旧深色端点，`1` 为浅色端点；非法/非有限权重按策略归一。主按钮在浅色主栏展开时到达 1，折叠成独立入口时到达 0。所有颜色、透明度比例、静态阴影/高光和动态光参数消费同一个 current 权重，不能在 draw 中直接按 fold 目标跳变。
- 主题背景存储显式 Dark 表面/边框作为端点，并保留原始动画 pct/framePct；renderer 解析完整材质并应用透明度比例。普通按钮不是 themeSurface，它们使用独立图标、文字、底板角色，lightMaterial 只改变其边缘光。
- Light 角色为 Surface `#F4F6F7`、IconPrimary `#53616A`、TextPrimary `#3D474D`、Accent `#006F68`、SelectedFill `#D7EEEA`。SurfaceFrame、Divider、EdgeLight、ShadowKey、ShadowAmbient、TopHighlight 各自独立；相同 Dark 值不表示这些角色可以合并。
- `DrawPointLightFrame` 的描边色与发光色分开；Light 以近白反射和少量笔色混合，Dark 恢复原主光/鼠标光行为。EdgeLighting 只控制动态边缘光，不能隐藏静态表面阴影。
- 静态 key/ambient 阴影的最大外扩为 8 DIP，另外保留原有像素抗锯齿余量。绘制、`GetFrameDirtyOutset`、`GetWeigetRect`、预测 viewport 和自绘面板的外扩必须读取同一上界，且与当前权重/EdgeLighting 开关无关，防止过渡裁切。
- 主题改变在初始化或 Windows 应用主题消息时读取一次并发布目标，渲染线程通过既有唤醒处理；不逐帧读注册表、不新增轮询线程。不可读回退 Dark。
- 收展使用已有时间轴和 current/target 语义；中途反向从当前值重新定向，关闭动画直接到一致端点，材质单独变化同样产生 dirty 并续帧。其他控件和显式 Dark 的分页客户端不继承主按钮的折叠权重。
- 原始笔 RGB 永远只作为输入。仅 `IdtPen || IdtShape` 启用颜色指示，Selection/Eraser 等状态收回到对应默认中性色，不显示记忆笔色。Geometry 明确使用 Brush1；活动 Pen 按既有槽解析激光/荧光/普通笔。主图标保留一个 Color 动画，普通换笔/换色从 current 继续；初始化和 Geometry 接管非 Brush 颜色历史时直接回到正确源，避免串色。PointLight 的激光替换仍限定为活动 Pen/Laser。
- 主按钮只使用一个稳定 `logo1` SVG 实例。它包含原中性笔/屏幕、4478887c 的 Frame94 渐变/笔尖/高光路径及浅色整体轮廓，通过内部属性加工实现深浅转换。Dark 端点保留原白笔底和渐变指示，不使用整笔实色或 DisplayPenColor 提亮；Light 绘制端点使用真实整笔色，默认态为石墨灰。轮廓只描真正外缘，不给各个闭合笔段加 stroke；Light screen 保持中性色，Dark screen 允许原有渐变指示贡献。
- 主图标部件颜色、原渐变 opacity、浅色轮廓及绘制态填色均由 current 材质权重、独立绘制态 Pct 和当前 Color 解析；禁止用两个完整 SVG/位图交叉淡化代替。外观改变必须同时触发 dirty 和缓存失效；外观失效重建失败不得显示旧语义，尺寸质量重建失败可保留旧位图。ResetCache/设备重建只清设备缓存，不清语义外观。单实例继续使用原点击脉冲、父继承、底栏刚性变换和窗口边界。普通 SVG 的 color1/color2、几何和缓存路径不变。

## 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| Light 展开或收起至中途 | 背景、边框、阴影、高光、PointLight、鼠标光、笔色均已处于连续中间值 |
| 中途反向 | 从当前权重接续，不重置到 0/1，不等最后一帧切换 |
| 关闭动画 | 全部材质通道同帧到正确端点 |
| Dark 普通表面 | 保留旧数值和鼠标光强；无新增浅色阴影 |
| EdgeLighting 关闭 | 静态浅色阴影继续绘制且范围仍被保留 |
| 白/浅黄笔 | 完整笔形有细外轮廓，内部断口透明、无横向黑边 |
| 自定义色/工具切换 | Light screen 中性，Dark 保留原渐变；真实 RGB 不写回 UI 显示处理 |
| 非绘制模式 | 深浅端点均为默认中性样式，与记住的笔色无关 |
| DPI、底部吸附或设备重建 | Logo 层与主按钮一起变换，阴影不裁切、不残留旧像素 |

## 5. Good / Base / Bad Cases

- Good：同一材质权重同时降低笔色染光、改变反射色、淡出浅色阴影并转成深色入口。
- Base：旧调用者未启用 themeSurface/lightMaterial 时仍为 Dark 绘制。
- Bad：只把 Surface 改白，PointLight 仍使用深色边框；或最后一帧根据 fold 同时切换边框/光源。

## 6. Tests Required

直接测试生产策略的 Light/Dark 端点与 0.25/0.5/0.75 中间值、全通道有限/单调变化、显示色与真实色隔离、弱笔色反射及阴影上界；用生产动画属性验证反向与关闭动画。对生产 ApplyAttributes 输出加工后的同一 SVG 做离屏检查：Dark 与 4478887c 原 logo1+Frame94 组合对照；Light 验证黑、白、浅黄、红、蓝、自定义色整笔 RGB、screen 中性及断口无描边；绘制态/非绘制态和过渡中点必须覆盖。

完整 ARM64 host MSBuild 构建 `InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64`，并运行 `InkeysHeadlessTests.exe --no-window`。实际桌面光影、白色文档覆盖和真实鼠标过渡属于另外的 GUI 验收；未执行时如实说明。

## 7. Wrong vs Correct

~~~cpp
// Wrong：边框同时作为发光色，且目标状态直接控制材质。
lightColor = GetThemeColor(BarThemeColorEnum::SurfaceFrame);
mainButton.lightMaterial = fold ? 0.0 : 1.0;

// Correct：消费同一动画当前值，发光色由独立反射角色解析。
mainButton.lightMaterial = currentMaterialWeight;
const auto material = BarThemeMaterial::Resolve(currentMaterialWeight, darkSurface, darkFrame);
const auto lighting = BarThemeMaterial::ResolveLighting(material, actualPenColor, drawingBlend);
~~~
