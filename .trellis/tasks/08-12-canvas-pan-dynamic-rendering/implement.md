# Implementation Plan

1. 增加 `canvas_navigation` 纯 CPU 模块与测试：坐标转换、范围保护、触摸状态机、惯性/接续、tile 预测排序和预算。
2. 扩展 `InkCanvas`、Canvas command 和 RTS proximity 通道；接入方向键、分页、Undo、Resize 及 Pen 接管。
3. 将实时输入、Stored raster、history footprint 和 composition restore 全部改为 Canvas/屏幕显式变换，并支持负坐标可见范围。
4. 增加 L2 稳定快照、重投影/定向模糊 pass、清晰 tile 恢复队列和自适应帧预算；确保瞬态层后绘制。
5. 扩充 document/history/navigation/render planner 测试，执行 `git diff --check`、ARM64 Debug/Release 完整 solution 构建及两种测试程序。
6. 使用 `trellis-check` 完成跨层审查；把稳定契约更新到 native/shader spec，记录无法执行的真实触摸和 D3D Debug Layer 验证。
7. 修复小数 viewport 热前像：改为 screen-local copy contract，覆盖捕获/恢复、边缘 partial tile 和 viewport mismatch 测试。
8. 为 runtime history 增加 tile 内容索引，移除平移时整页 tile 拼接和 Resize footprint 重算；Stored Stroke 单 tile 重建提取局部连续几何并测试长笔画/高亮/擦除/Shape 边界。
9. 调整 Windows/CPU 惯性减速度，增加滑行距离测试；验证 Pen Down 在平移/惯性同帧刹停、吞 Touch 并立即进入绘制。
10. 修复抬手瞬停：速度改为 Touch Move/QPC 驱动，Up 前锁存 Windows 速度，清理旧 inertia Completed，并在 COM 初始化/启动/步进失败时切换 CPU fallback。

## Rollback Points

- 导航状态机可独立回退，不改变 Stored 数据格式。
- 动态兜底资源创建或 shader pass 失败时退回权威 tile 重建；不得影响输入、文档或 history。
