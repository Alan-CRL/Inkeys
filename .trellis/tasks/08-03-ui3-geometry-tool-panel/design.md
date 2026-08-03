# UI3 几何工具面板设计

## Scope

在 UI3 Bar 内部增加几何面板状态、动画、布局、渲染和输入处理。继续调用现有 Draw2 状态接口，不改动绘制链路或外部 API。

## State And Transitions

- 恢复 Geometry 注册可见性，保留 `DisplayStateChange()` 的选择模式门禁。
- Geometry 点击先调用 `ChangeStateModeToShape()`；仅在 `stateMode.Mode == IdtShape` 后选中并展开。
- Geometry 已选中时只切换面板展开状态。
- 切换 Draw、Selection、Eraser，折叠主栏或 Geometry 隐藏时收起几何面板。
- 增加几何面板动画值、上下换边状态、按钮按压状态和悬停阶段；结构与绘制属性保持同类。

## Layout And Rendering

- 展开尺寸 `335x100`；收起尺寸 `60 x (100*60/335)`，以 Geometry 按钮中心为动画锚点。
- 直线、矩形按钮分别位于 `(5,5,50,50)`、`(60,5,50,50)`。
- 分割线位于 `y=60`，左右内缩 `5`。
- 粗细按钮位于 `(230,65,30,30)`、`(265,65,30,30)`、`(300,65,30,30)`。
- 形状图标使用 D2D 线段与矩形，配合中文文字；粗细预览使用圆头斜线，随线宽缩短并在极粗时钳制为圆和显示像素数。
- 表面、边框、圆角、透明度、动画曲线和主栏上下换边复用绘制属性实现。
- 子按钮复用主栏的 hover/press/selected 视觉；选中使用 Accent 填充和第三光源。
- 扩展 PointLight 可见根区域容量，将几何面板、分割线和子按钮纳入归一缓存、脏区及第三光源范围。

## Draw2 Compatibility

- 直线写入 `IdtShapeStraightLine1`，矩形写入 `IdtShapeRectangle1`，初始化时读取并记忆现有选择。
- 粗细调用现有硬笔预设换算，更新 `stateMode.Pen.Brush1.width`；兼容 Shape 内部画笔状态的现有同步方式。
- 不新增配置字段，不修改历史、PPT 墨迹、Draw3 或 Draw2 绘制代码。

## Validation

- 静态审查枚举连续区间、状态初始化、动画推进、设备资源、渲染、输入命中、脏区和 PointLight 根区域。
- 执行 `git diff --check`。
- 使用 ARM64 Host MSBuild 构建 `InkeysRepo.sln` 的 `Debug | ARM64`。
- 手工项目记录鼠标、触摸、笔、高 DPI、上下换边和快速开关场景；自动环境无法替代的硬件检查在交付时明确说明。
