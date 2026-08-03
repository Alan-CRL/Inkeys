# UI3 几何工具面板实施计划

1. 阅读 UI3、输入和 C++ 规范，核对现有绘制属性、按钮交互和 PointLight 实现。
2. 扩展 Bar 内部枚举和状态，恢复 Geometry 注册并接入模式、选中态及主光锚点。
3. 实现几何面板布局、动画、换边、渲染、资源生命周期、脏区和 PointLight 可见区域。
4. 实现直线、矩形与三档粗细的 hover/press/selected 输入状态及 Draw2 状态写入。
5. 静态审查并执行 `git diff --check`，修正格式、枚举和覆盖问题。
6. 使用 ARM64 Host MSBuild 完整构建 `InkeysRepo.sln` 的 `Debug | ARM64`。
7. 汇总自动验证结果和仍需真实鼠标、触摸、笔执行的手工验证项。
