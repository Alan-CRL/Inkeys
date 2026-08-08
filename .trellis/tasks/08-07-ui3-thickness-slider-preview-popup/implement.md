# 实施计划

1. 在 `Bar.Main.cppm` 连续 Shape/Word 枚举中登记浮窗 Surface、白色墨迹圆和数值对象；在初始化路径配置 Surface 第三光与无交互内容对象。
2. 在渲染状态区加入浮窗展开、数字迁移、Hold 控制行交换和水平避让动画，绑定既有 Thumb 可见目标与统一动画时钟。
3. 从动画中的 Thumb、DrawAttributeBar 和笔类型按钮几何计算浮窗自适应尺寸、面板外侧目标位置、扫掠避让与数值内外布局。
4. 将三个快捷按钮/小三角的可见度接入分阶段交换进度，并把 Hold 文字/圆环改为控制行右对齐布局。
5. 拆分 Thumb 最终绘制通道，按“普通属性内容 → 浮窗 → Thumb”渲染，补齐 PointLight 几何归一和 predicted/current 脏区。
6. 静态检查所有 Slider 清理、直接触摸 Preview、Hold-lock、颜色选择器和上下换边路径，确保新视觉没有独立残留状态。
7. 运行 `git diff --check`、换行检查和 ARM64 host `MSBuild.exe InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64`；记录无法自动化的视觉验收矩阵。

## 风险与回滚点

- Thumb 当前位于 Preview clip 内自绘；拆分后必须保证 clip 成对弹出并保留原视觉参数，否则优先回滚绘制顺序拆分。
- Surface 动态尺寸会影响 PointLight mask key；必须使用现有几何缩放归一，稳定状态不得持续 cache miss。
- 避让只针对笔类型控件且明确不做窗口边缘 clamp，不能复用现有 Tooltip 的 `BuildPopupLayout()` 夹取逻辑。

## 验证记录

- 2026-08-08：`git diff --check` 通过；目标源码为 UTF-8 无 BOM、纯 CRLF。
- 2026-08-08：ARM64 host MSBuild 完整构建 `InkeysRepo.sln` 的 `Debug | ARM64` 通过，0 个错误；47 个警告均为现有编译警告。
- 仍需在实际 UI3 运行环境完成 DPI、上下展开、Hold 分阶段交换、触摸 Preview 和快速反转动画的视觉验收。
