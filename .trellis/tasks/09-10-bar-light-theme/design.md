# 技术方案

## 边界与所有权

行为差距位于 Bar.Theme、主按钮状态/资源和 Bar.Rendering；本次扩展再连接 `Inkeys.Other.Config` 与设置页。沿用串行渲染线程、原有动画属性、成功呈现 dirty 事务。设置渲染线程只更新原子配置、发布主题目标并唤醒 Bar，不创建 D2D 资源。颜色/材质使用小型可无窗口测试的生产 helper；原笔色/预设不变，仅 Dark 显示色提亮。

## 材质数据流

- 分开 IconPrimary、TextPrimary、SelectedFill、Accent、SurfaceFrame、Divider、EdgeLight 等角色，Dark 值按既有语义保留。
- 主按钮连续浅色材质权重 0=Dark Floating、1=Light Expanded，与现有收展 timeline 同批推进；主栏使用当前配置主题。反向从 current 重新定向，draw 不直接用 fold 跳变颜色。
- 填充/边框 RGB 与 alpha、key/ambient 阴影、高光、反射色、笔色混合比例、动态光强统一从权重解析。
- Shape/Superellipse 逐对象输入材质，默认 Dark，显式启用 Light；不让共享 renderer 的主按钮折叠权重污染 PageControl 等客户端。
- PointLight 的基础边框和光色解耦。Light 使用近白反射、轻笔色混合和弱 diffuse，Dark 权重 0 返回旧参数。
- 人工验收后的扁平化续作：Light 表面不再放大原始填充/边框透明度，基础外框改为 `IconPrimary` 蓝灰单线，并停用顶部高光与 key/ambient 多圈阴影。第一光源以蓝灰为默认、绘制时使用经对比度修正的真实笔色；第三光源固定蓝灰，两者独立解析颜色、强度和半径。主栏及绘制属性分割线在 Light 端使用独立 Divider 角色的同值蓝灰。
- 静态阴影沿用 D2D 几何/缓存方式，有有限可计算的外扩；绘制/dirty/viewport 同步。静态阴影不受动态 EdgeLighting 开关意外控制。

## 配置与设置数据流

`Other.Config.cppm` 在 `Experimental.Inkeys3.UI3` 增加 `IdtAtomic<int> ThemeMode{1}`，继续复用 schema 的整数 JSON 编解码。主题模式通过一个小型纯函数归一化：仅 `2` 解析为 Light，其余值都解析为 Dark/`1`，供启动绑定、设置预览和无窗口测试共用，避免各消费者重复解释魔法数字。

启动顺序保持 `config.ReadAll()` 先于 Bar 初始化；Bar 初始化直接读取已归一的 `ThemeMode`，不再读取 `AppsUseLightTheme`。设置页下拉选择后按以下顺序执行：写入原子配置 → 调用 Bar 公开的主题请求入口 → `QueueConfigWrite()` 捕获配置快照异步落盘。主题请求入口只 exchange `requestedDarkStyle` 并请求渲染，渲染线程继续使用已有 `barLightMaterial` 和 `mainButtonLightMaterial` 动画。

`WM_SETTINGCHANGE` 仍用于显示器/系统设置刷新，`WM_THEMECHANGED` 不再改写 Bar 主题。这样用户的显式设置不会被系统外观变化覆盖，也不需要轮询配置或注册表。

设置页在现有“常规 > 外观”容器中增加 70 DIP 卡片并调整父容器高度；右侧复用 `Widgets::combo`，选项值严格映射为 `1/2`。主题文案复用并改造现有未使用的 `Appearance.Theme` i18n 节点，由 `Scripts/i18n.ps1` 生成同步键。

## Logo

保留原笔各段路径作为完整颜色填充，屏幕独立为固定中性色。整体轮廓从现有外缘派生，不能对每段 closed path 单独 stroke。可增加浅色专用 Logo 层，与旧 Dark Logo/Frame94 连续交叉淡化；各层共用父继承、尺寸、脉冲、底栏变换、dirty 键和资源生命周期。

复用 SVG color1/color2 替换槽分别控制笔色/轮廓，screen 不使用笔槽。Light 为真实色，Dark 用纯函数派生明亮显示色；Geometry 从 brush1 取色，荧光/激光从对应选定色取色，不残留前一工具色。

## 预期修改文件

- Bar.Theme.cppm、Bar.ThemeMaterial.h：角色、材质/显示色策略。
- Bar.UI.cppm、Bar.Rendering.cpp/.cppm：逐对象材质、独立光色、静态阴影/高光、有界外扩。
- Bar.RenderLoop.cpp、Bar.Initialization.cpp、Bar.Main.cppm/State/Interaction：主题入口、动画、Logo 层、颜色消费者与 dirty。
- Bar.Button.cpp、Bar.Scene.cpp：图标/文字/选中底板的独立角色，维护共享消费者语义。
- Other.Config.cppm、IdtMain.cpp：`ThemeMode` schema、启动归一化与 Bar 初始绑定。
- Setting.cpp、三份 i18n JSONC、IdtI18nKeys.g.h：外观下拉框、实时请求、异步持久化和本地化键。
- src/UI/logo*.svg、Frame 94.svg：仅主按钮允许的结构变化。
- InkeysHeadlessTests 和必要工程登记：直接测试生产策略/动画/资源契约。
- .trellis/spec/native-desktop：记录经过验证的稳定合同。

## 兼容与回滚

配置 schema 只新增一个默认深色的整数叶子，不迁移旧皮肤字段。颜色/材质、Logo、设置集成分别审查；Dark 端点保持原 renderer 参数，PageControl 显式 Dark 语义仍有效。回滚时移除设置卡片与 `ThemeMode` 入口，并让 Bar 固定回默认 Dark；配置文件中遗留的新键由现有宽容读取策略忽略。所有改动留为 theme 未提交差异；未经 GUI 验证不声明视觉实机通过。
