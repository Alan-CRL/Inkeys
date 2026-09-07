# 全页迁移：公共组件无窗口回归

日期：2026-09-06。本轮范围以 approved-full-migration.md 为准。测试代理仅修改 InkeysHeadlessTests/setting_design_tests.cpp 与本记录，生产修复由实现代理负责。

## 已执行结果

- 本机 ARM64 host cl，VS 18 Community / MSVC 14.51.36231，编译既有 RunSettingDesignTests()、Setting.Controls.cpp、ImGui 核心与 ImFluent；没有 Win32/DX11 backend，没有 HWND、D3D device 或桌面操作。
- 独立 probe 最终退出码 **0**，耗时约 **3.80 秒**。编译与运行记录在 Build/SettingDesignProbe/compile-full-current.log、run-full-final.log。
- probe 沿用 Build/SettingDesignProbe/main.cpp，将 CRT/系统错误写入 stderr，未使用 Computer Use。编译使用绝对 include/lib 路径，未更改 PATH 或其他全局环境变量。
- 完整 InkeysRepo.sln 与集成 --no-window 测试由主会话统一运行；本记录在收到该结果后补充，不用独立 probe 冒充完整工程验收。
- 实际窗口截图、滚动与全页视觉检查由主会话执行；本记录不宣称整页视觉或全部业务绑定已经由 CPU 回归验证。

## 本轮覆盖

1. **字体 96 组**：SC Regular/Bold、TC Regular/Bold（TC 合并 SC 回退），12/14/28/13 四层字号，六个有效倍率 1、1.25、1.5、2、1.875、4。新增控件 13/20 行框；校验中文、简繁、Agpqy、全角括号和标点的真实 glyph、descender、行框与光学 em。全角 advance 以 TextOpticalScale 对应的实际字面为基准；原裁切约束未放宽。
2. **多行文本 72 组**：保留直接调用生产 TextHeight()/TextAt() 的真实顶点与行框检查，没有在测试中另写换行算法。
3. **原设置行 72 组**：保留 252/320/624 DIP × Button/Toggle/Slider/Combo × 六倍率，控件改为调用生产 Design 封装；验证回调一次、idle 无改值、真实矩形内含/右锚、作为 child 末项直接 EndChild，保留分数 DPI 断言回归。
4. **控件字体与尺寸 18 组**：Button、AccentButton 默认宽度以及显式尺寸覆盖。验证实际字形来自独立控件字号、比同级正文小、自然宽度与实际绘制一致、显式尺寸保留、调用后恢复正文 font scope。
5. **按钮组**：自然宽度由生产 ButtonWidth() 提供，调用生产 ResolveButtonGroup() 验证窄宽换行、矩形不相交、每行尾沿统一、极长按钮受预算限制；真实 ButtonGroupRow() 在 252/624 DIP 下绘制，验证末个按钮的实际右边缘和换行后的行高增长。
6. **复合容器 96 组**：两种宽度 × 八种容器/状态 × 六倍率，覆盖按钮组、关闭与展开 Details、长 Notice、PageContentStart、SectionHeader、NavigationRow、Card。AutoResizeY 先经过一帧测量，随后检查实际可见内容。展开说明使用真正 Text() 绘制并核对顶点；关闭详情不调用内容、展开内容调用一次，结束后恢复父窗口/字体；各容器作为末项直接结束。
7. **导航图标**：使用 ApplyPalette() 后的 NavigationGlyphSize 16 DIP bake，实际 NavItemEx()/DrawIcon() 顶点保持展开/收起同轴；没有只替换预期矩形常量。
8. **滚动条六倍率**：重复 ApplyScrollbars() 不累计放大，轨道透明、hover/drag 的灰色 thumb 有状态差异；真正创建 ImGui 滚动 child 并提取其绘制顶点，验证实际 8 DIP 命中轨道与更窄可见 thumb。测试兼容 ImGui 将 child 装饰合并到 parent draw list 的优化。
9. **DrawData**：两帧均验证非空 CPU 绘制数据、有效 clip rect/顶点/索引及真实字体 atlas 像素。

字号至多 0.5 物理像素、控件尾沿至多 1 物理像素的容差沿用首批，用于 ImGui 固有量化；没有为通过测试扩大正文行框或允许可见裁切。

## 测试发现与修复

- 实测默认 Button 的自然宽度与统一测量不一致：100% 时示例标签测量 175 像素，实际绘制 165；200% 时 349 对 329。原因是 ButtonWidth() 使用文字 +32 DIP、最小 72，而零尺寸按钮仍直接采用 ImFluent 的文字 +22 DIP。
- 实现代理新增集中 ResolveButtonSize，Button/AccentButton 的零宽度走统一自然宽度，保留显式宽度、负值和高度语义。默认与显式尺寸回归随后全部通过。
- 初次滚动条提取只读取 child draw list，得到零顶点；实际装饰已经由 ImGui 合并进 parent。测试改为提取本次 BeginChild 产生的父/子真实顶点后通过，此项是测试取样问题，未修改生产滚动条来迎合测试。

没有修改主应用工程、创建窗口或提交 commit。
