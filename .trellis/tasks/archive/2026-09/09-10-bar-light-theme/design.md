# 技术方案

## 边界与所有权

行为差距位于 Bar.Theme、主按钮状态/资源和 Bar.Rendering。沿用串行渲染线程、原有动画属性、成功呈现 dirty 事务。WndProc 只发布主题目标并唤醒，不创建 D2D 资源。颜色/材质使用小型可无窗口测试的生产 helper；原笔色/预设不变，仅 Dark 显示色提亮。

## 材质数据流

- 分开 IconPrimary、TextPrimary、SelectedFill、Accent、SurfaceFrame、Divider、EdgeLight 等角色，Dark 值按既有语义保留。
- 主按钮连续浅色材质权重 0=Dark Floating、1=Light Expanded，与现有收展 timeline 同批推进；主栏使用当前系统主题。反向从 current 重新定向，draw 不直接用 fold 跳变颜色。
- 填充/边框 RGB 与 alpha、key/ambient 阴影、高光、反射色、笔色混合比例、动态光强统一从权重解析。
- Shape/Superellipse 逐对象输入材质，默认 Dark，显式启用 Light；不让共享 renderer 的主按钮折叠权重污染 PageControl 等客户端。
- PointLight 的基础边框和光色解耦。Light 使用近白反射、轻笔色混合和弱 diffuse，Dark 权重 0 返回旧参数。
- 静态阴影沿用 D2D 几何/缓存方式，有有限可计算的外扩；绘制/dirty/viewport 同步。静态阴影不受动态 EdgeLighting 开关意外控制。

## 系统主题

初始化读取 Windows AppsUseLightTheme，在已有 WM_SETTINGCHANGE 和必要主题消息发布刷新，沿既有原子/唤醒机制由渲染线程应用；缺值回退 Dark。禁止逐帧读注册表或新增后台轮询。主题改变须覆盖全部颜色目标并触发 dirty。

## Logo

保留原笔各段路径作为完整颜色填充，屏幕独立为固定中性色。整体轮廓从现有外缘派生，不能对每段 closed path 单独 stroke。可增加浅色专用 Logo 层，与旧 Dark Logo/Frame94 连续交叉淡化；各层共用父继承、尺寸、脉冲、底栏变换、dirty 键和资源生命周期。

复用 SVG color1/color2 替换槽分别控制笔色/轮廓，screen 不使用笔槽。Light 为真实色，Dark 用纯函数派生明亮显示色；Geometry 从 brush1 取色，荧光/激光从对应选定色取色，不残留前一工具色。

## 预期修改文件

- Bar.Theme.cppm、Bar.ThemeMaterial.h：角色、材质/显示色策略。
- Bar.UI.cppm、Bar.Rendering.cpp/.cppm：逐对象材质、独立光色、静态阴影/高光、有界外扩。
- Bar.RenderLoop.cpp、Bar.Initialization.cpp、Bar.Main.cppm/State/Interaction：主题入口、动画、Logo 层、颜色消费者与 dirty。
- Bar.Button.cpp、Bar.Scene.cpp：图标/文字/选中底板的独立角色，维护共享消费者语义。
- src/UI/logo*.svg、Frame 94.svg：仅主按钮允许的结构变化。
- InkeysHeadlessTests 和必要工程登记：直接测试生产策略/动画/资源契约。
- .trellis/spec/native-desktop：记录经过验证的稳定合同。

## 兼容与回滚

不改配置 schema，不复用 draw 未提交代码。颜色/材质、Logo、集成分别审查；Dark 端点保持原 renderer 参数，PageControl 显式 Dark 语义仍有效。所有改动留为 theme 未提交差异；未经 GUI 验证不声明视觉实机通过。
