# 方案B：本轮实现与验证

## 基线与历史
2026-09-15，在feature/eraser、af22b4a6基础上由主Agent顺序完成。无子Agent/background Agent，未切分支、建worktree、commit、push或archive。
旧request.md、validation.md、format-check.md与af22b4a6一致；上一轮图片与日志未改写。本轮产物统一在Build/eraser-b和scheme-b-*记录中。
PptCOM.dll仍与本轮开始时副本逐字节一致，SHA256见scheme-b-format.json。

## 实际方案B布局
- 标准顺序：24 / 32 / 40圆 | 清空 | 自动粗细。清空矩形的centerX始终等于panel.centerX；正常空间下又等于主栏擦除按钮centerX。
- 左右各128逻辑单位（取圆组128与自动整体102.5的较大者），中央清空70，两条1单位分割线，六份共享5单位留白：默认总宽358。高度取当前MainBar布局，稳定默认80。
- 自动整体宽102.5（70主体+32.5扩展），在自己的分组居中；没有用大矩形尺寸槽凑对称。
- 子菜单182.5×90，centerX锚定完整自动按钮，而非箭头或主栏擦除。工作区修正分别作用于panel/menu，边缘避让后清空仍在panel中轴。
- 上下优先级、间隔和停靠刚性位移沿用现有属性规则。倒转交换两侧完整分组；圆顺序、A、字与竖纹不反转。菜单方向锁定，不跟随Hover变化。
- 预览DIP先通过DiameterToCanvasPx变成画布像素，再除frameZoom进入Bar逻辑空间。稳定pose严格为1，自定义UI缩放只改变外壳/留白。
- 默认两处圆边缘留白都是16逻辑单位；圆心距=半径+gap+下一半径。命中扩展以32.5最小目标/圆半径+5为起点，再按相邻边缘中界和面板可见区域裁剪；不是旧70方槽。
- 极窄路径减弹性留白，必要时只处理内容溢出，圆不缩略/压扁；超高圆裁上下。35% UI的生产截图还覆盖了超高路径。

## 预设迁移和保留的控制器
BaseSize与集中BaseSizePresets表为24/32/40，默认32；16→24、64→40，32/24/40幂等，非法/缺失回32。UI点击/选择/提示不再使用16<<index。

| B | minimum / standard / maximum | Touch起点 | Fixed |
|---|---|---|---|
|24|12 / 24 / 120 DIP|12 DIP|24 DIP|
|32|16 / 32 / 160 DIP|16 DIP|32 DIP|
|40|20 / 40 / 200 DIP|20 DIP|40 DIP|

五入口类型、penResponse、0.85/1/1.15灵敏度增益、触发门、Touch面积开关与报告转换均保留；不回写历史逐点半径。
Draw3.DrawingController.cpp、Draw3.PenCursor.cpp、EraserGripVisual.h和inkPixelShader.hlsl均无本轮改动。SpeedEraser.cpp只改RestoreBaseSize，既有尺寸派生公式/动作阈值/时间常数/迟滞/会话算法未修改。
本轮重跑的1728精细量化案例仍为worstPeakToPeakDIP=0、heldExits=0；采样/帧率偏差仍为1.99186%。

## 圆、按钮、图标和菜单
- 三个尺寸矩形背景、胶囊及按压缩放分支已删除。预览自身alpha=1；白色Contact主体、共享灰轮廓/双竖纹，未更改画布Hover的0.5常量。
- 选中以Theme Accent内描边和共享PointLight显示，线宽向内扩展，外径不变。Hover/Pressed只变轮廓/光照，焦点为独立内虚线。
- 自动视觉只绘制一个BarButtonClass，只有动作区分body/arrow。无内部竖线、第二块底色或共同clip伪拼接。跨内部边界不重启Hover；整体按压包含箭头和Mixed标记。
- Down锁存动作；body→arrow释放仍切开关，arrow→body释放仍开菜单，取消/离开不执行。使用原Window消息层的Pen/Touch兼容Mouse过滤；contact Up与窗口leave清掉残留反馈。
- 新barAutoEraser.svg保留斜橡皮路径语汇，右下角A是独立路径且有负空间，无徽章底、字体或AI星光；通用barEraser.svg未改。按rc/vcxproj/filters登记。
- 自动、圆、灵敏度选中边缘都使用同一Accent，外壳/分割线仍用SurfaceFrame。完整自动的Selected仅由五入口状态决定，menu open不冒充开启。
- 菜单两层：左标题右禁用齿轮；低中高等宽行。旧底部长行及分割线删除。齿轮提示“自动粗细设置，暂未开放”。
- 新UI/Bar/EraserAttributes键通过i18n.ps1 sync生成，简/繁/英已提供；check显示两种非默认语言298/298、100%。保留原文件编码/行尾，并撤销sync附带的繁体文件头注释变更。

## 实际复用的动画
新增小型EraserSurfaceMotion，使用现有BarUiValueClass、BarUiPctClass、BarUiTimelineClass与BarDrawAttributeCompactWidth=60。
几何开/关分别为EaseOutBack/EaseInBack，alpha独立为EaseOutSine/EaseInSine。前半程可加入父余时；只在状态变化时Retarget，反向以当前val为起点。
父panel pose与子menu pose复合；同源pose正向生成当前Bar坐标的完整几何，Renderer按原frameZoom绘制，SVG使用contentScale复用缓存。没有额外D2D父Scale，不修改主栏全局光源状态机。
Visible按生命周期/alpha判定，不把Back几何clamp到0..1。原动画底层dt<=0代表完成，因此局部motion对同一时刻重定向作保留处理，未全局改动画系统。
Bounds含当前shell实际缩放、位置及阴影外扩；内容在当前shell内裁剪，窗口容量和成功CommitPresented后的命中共用同一几何。

### 本轮真实组件多帧证据
由生产Advance→Draw→CommitPresented在60Hz虚拟帧时钟下采样，保存226张PNG和完整frames.csv，并非生成式示意图。
- 主面板开峰值：1.037；关峰值：1.037。
- 菜单开峰值：1.02983；关峰值：1.02983。
- 稳定态scale=1。像素读取确认最终panel/menu矩形之外确实存在回弹画面，未被最终Bounds裁掉。
- 覆盖主开、子开、子关、父子同时关、两者半程反向、移动锚点换边、动画关闭、2倍速度、窗口leave和三类归一化Pointer跨区释放。
- 开栏Up无Down票据，不产生Clear；helper与真实Pointer/clickFunc均覆盖。其他时刻仍按当前呈现几何允许交互。
- GIF由这些PNG顺序合成，未插帧、未缩放，裁剪仅为了查看；按2倍慢速播放并在末帧停顿。原PNG/CSV保留实际60Hz采样语义。

## 本轮实际运行
完整原生ARM64 MSBuild通过vswhere定位，沿用Build/eraser-build.ps1：
```text
MSBuild.exe InkeysRepo.sln /m:1 /nr:false /p:Configuration=Debug /p:Platform=ARM64
InkeysHeadlessTests.exe --no-window
Inkeys.exe --bar-eraser-offscreen-test
Inkeys.exe --draw3-hidden-test
Inkeys.exe --draw3-eraser-hidden-test
```
最终solution退出0，保留PptCOM依赖；日志Build/eraser-b-build-delivery.log。已有第三方/历史源码warning未绕过或修改。本轮采用已确认的当前进程Path规范化与TEMP/TMP目录，未改全局环境或工具链。

| 测试 | 退出码 | 实际时间 |
|---|---|---|
| `InkeysHeadlessTests.exe --no-window` | 0 | 35.96s |
| `Inkeys.exe --bar-eraser-offscreen-test` | 0 | 14.53s |
| `Inkeys.exe --draw3-hidden-test` | 0 | 1.86s |
| `Inkeys.exe --draw3-eraser-hidden-test` | 0 | 111.22s |

- Headless：216组DPI/UI缩放/方向/位置及补充极窄、240测试直径、中心/间距/动作归属/Back/alpha/反向/禁用检查，旧模型/面积/连续会话回归保留。
- Offscreen：21个面板状态（96/144/192 DPI × 65/100/150% × 深浅，加35%超高和英语/繁体），18组A图标正常/选中/禁用图；逐帧/像素/点击测试全部通过。
- 通用隐藏测试：Clear、Undo/Redo、跨页/场景、迟到Up与既有业务回归；本轮未重写Clear。
- 橡皮隐藏测试：真实Host的五入口、24/32/40固定首点/光标、活动24锁存后变40、Touch面积与非Touch间隔恢复。
- 四套均是本轮重新执行，机器可读结果Build/eraser-b/final-results.json；各final-*.log保留。

## 图片与人工检查范围
已查看生产深浅色、英语、低缩放A图标、超高圆、菜单开与父子关的逐帧图。
- Build/eraser-b/visuals/0-dark.png：96/100%标准朝向。
- Build/eraser-b/visuals/3-light.png：96/150%下方/倒转。
- Build/eraser-b/visuals/18-dark.png：192/35%真实大圆上下裁剪。
- Build/eraser-b/visuals/19-light.png、20-dark.png：英语/繁体。
- Build/eraser-b/visuals/icon-*-*-*.png：A图标三态与多DPI/UI缩放。
- Build/eraser-b/open-close.gif、reverse.gif：真实采样帧组合预览。
- Build/eraser-b/*-contact-sheet.png：开关关键帧，frames/存原图。

**未做真人GUI验收**：实体Mouse/Pen/Touch触感、实际主栏停靠拖动和多屏切换、Windows7运行、真实Office/WPS及图像底图场景均不据离屏/隐藏测试宣称全设备验收。测试里的主栏参照来自标准Bar组件，动画记录来自生产橡皮组件，不是完整桌面录像。未新增不兼容框架或静态高版本API。

## 修改文件
- `Inkeys/IdtI18nKeys.g.h`
- `Inkeys/Inkeys.rc`
- `Inkeys/Inkeys.vcxproj`
- `Inkeys/Inkeys.vcxproj.filters`
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.HiddenWindowTest.cpp`
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.SpeedEraser.cpp`
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.SpeedEraser.h`
- `Inkeys/Inkeys/UI/Bar/Bar.EraserAttribute.Test.cpp`
- `Inkeys/Inkeys/UI/Bar/Bar.EraserAttribute.cpp`
- `Inkeys/Inkeys/UI/Bar/Bar.EraserAttributeLayout.cppm`
- `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp`
- `Inkeys/Inkeys/UI/Bar/Bar.Main.cppm`
- `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp`
- `Inkeys/src/i18n/en-US.jsonc`
- `Inkeys/src/i18n/zh-CN.jsonc`
- `Inkeys/src/i18n/zh-TW.jsonc`
- `InkeysHeadlessTests/InkeysHeadlessTests.vcxproj`
- `InkeysHeadlessTests/eraser_attribute_tests.cpp`
- `Scripts/i18n.zh-CN.snapshot.jsonc`
- `Inkeys/Inkeys/UI/Bar/Bar.EraserAttributeMotion.cppm`
- `Inkeys/src/UI/barAutoEraser.svg`

本轮另更新PRD、scheme-b记录和eraser-attributes规范。旧验证记录与DLL校验通过，git diff --check通过，无整文件格式化。
