# 基线证据（4478887c）

- Bar.Theme.cppm 的 Light SurfaceFrame 为黑，图标与文字复用 TextPrimary，选中底板与内容复用 Accent；darkStyle 固定 true，无系统主题读取者。
- Bar.Initialization.cpp：主按钮 80 DIP superellipse，fold alpha 0.6、framePct 0.18、PenWhenDrawing PointLight；logo1/2 与 Frame94 color1 层叠加。主栏 alpha 0.8、framePct 0.18。
- Bar.RenderLoop.cpp：主按钮 pct/n 和主栏 w/x/pct 各自 target；主栏批次允许加入到 50% 进度。Logo 的 Geometry/Highlighter 源切换有防残留状态，不能随意移除。
- Bar.Rendering.cpp::DrawPointLightFrame 当前直接以边框作 lightColor；PrepareFrameLighting 提供 penColorBlend，diffuse 在 0.30/0.20 间变化。需要逐对象材质和独立光色。
- Bar.UI.cpp SVG color1 占位 rgba(10,0,7,0)，color2 为 rgba(9,0,2,0)，两者忽略输入 alpha；透明度由 SVG 或对象 pct 提供。
- logo1.svg、logo2.svg 原造型相同（部分导出小数细微不同），screen 有独立两条路径。Frame 94.svg 的笔/屏幕渐变共享着色槽，需要解除染屏幕语义。
- Bar.Interaction.cpp 的 WM_SETTINGCHANGE 仅刷新显示器；系统主题需额外发布并唤醒。D2D 继续只在 RenderPipeline 串行线程操作。
- Bar.Scene.cpp/Bar.Button.cpp 为标准按钮共享实现；PageControl 显式 Dark 的地方不能受主按钮 fold 材质误影响。
- InkeysRepo.sln 包含 PptCOM；无窗口测试开关 --no-window。新 worktree 不带 ignored 的依赖/缓存，需局部准备路径，禁止带入 draw 未提交源码。
