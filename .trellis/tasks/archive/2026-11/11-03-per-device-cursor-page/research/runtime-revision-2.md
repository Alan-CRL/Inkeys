# 运行时修订 2：箭头细黑边（2026-11-04）

## 用户反馈与边界

用户已确认校准后的箭头大小正确；新的反馈是黑色边框过粗，192 DPI截图中系统约1px、自绘原为2px。本次只改Arrow描边，不改13×19.5 DIP尺寸盒、尖端热点、DPI传递、HLSL轮廓/数学、Pen/Dot/高亮描边或落笔隐藏策略。设置预览由主代理同步同一helper。

## 实现

- Draw3.CursorSettings.h新增共享kCursorArrowEdgeDip=0.5f与constexpr CursorArrowOutlineWidthPx(float scale) noexcept，结果为max(1物理像素,0.5 DIP×scale)。96/144/192/264 DPI分别为1/1/1/1.375px；不会在低DPI只剩半像素。
- Draw3.PenCursor.cpp的ApplyCursorStyle仅Arrow分支由该helper取outlineWidth；其余样式仍用原公式。
- Controller生产probe独立预期为max(1.0,0.5×dpi/96)，不通过同一helper镜像断言。
- contact测试补0.5/1/1.5/2/2.75/4倍率的边宽、盒尺寸与热点，以及触摸Arrow在2倍率仍保持26×39盒、1px边。
- 既有无HWND GPU upload probe在192 DPI实际staging像素中读取竖直左边中段：输入热点(8,8)，(8,24)应为不透明黑边，紧邻(9,24)必须为不透明白色填充，正式PS与完整guarded PS均覆盖。旧2px描边会使第二像素仍黑，从而不满足白填充断言；保留原25×39可见bbox与尖端断言。日志输出Draw3CursorEdge的两组BGRA，便于核查。

## 格式与验证状态

修改前五个运行时/测试文件的原始字节、SHA-256及BOM/EOL统计已保存到忽略目录Build/cursor-revision2/before-r2/manifest.json及同路径镜像。修改后五文件BOM与换行类型均保持，git diff --check通过；没有格式化或无关重构。

本代理没有构建或运行新二进制，统一solution build、受影响Headless/production probe由主代理执行。此时只完成代码与静态验证，不能提前宣布新的GPU边宽断言通过。没有启动GUI、改系统光标或创建commit。源码已冻结。
