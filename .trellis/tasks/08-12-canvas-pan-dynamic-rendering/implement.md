# Implementation Plan

1. 增加 `canvas_navigation` 纯 CPU 模块与测试：坐标转换、范围保护、触摸状态机、惯性/接续、tile 预测排序和预算。
2. 扩展 `InkCanvas`、Canvas command 和 RTS proximity 通道；接入方向键、分页、Undo、Resize 及 Pen 接管。
3. 将实时输入、Stored raster、history footprint 和 composition restore 全部改为 Canvas/屏幕显式变换，并支持负坐标可见范围。
4. 增加 L2 稳定快照、重投影/定向模糊 pass、清晰 tile 恢复队列和自适应帧预算；确保瞬态层后绘制。
5. 扩充 document/history/navigation/render planner 测试，执行 `git diff --check`、ARM64 Debug/Release 完整 solution 构建及两种测试程序。
6. 使用 `trellis-check` 完成跨层审查；把稳定契约更新到 native/shader spec，记录无法执行的真实触摸和 D3D Debug Layer 验证。
7. 修复小数 viewport 热前像：改为 screen-local copy contract，覆盖捕获/恢复、边缘 partial tile 和 viewport mismatch 测试。
8. 为 runtime history 增加 tile 内容索引，移除平移时整页 tile 拼接和 Resize footprint 重算；Stored Stroke 单 tile 重建提取局部连续几何并测试长笔画/高亮/擦除/Shape 边界。
9. 调整 Windows/CPU 惯性减速度，增加滑行距离测试；验证活动 Touch 跟手期间 Pen 锁存抑制到 Up，惯性阶段的新 Pen Down 同帧刹停并立即进入绘制。
10. 修复抬手瞬停：速度改为 Touch Move/QPC 驱动；先补强 Windows/CPU 失败接续诊断，再根据长日志移除双后端，收敛为应用层唯一权威。
11. 明确 Windows Tablet/RTS 混合输入限制：活动双指跟手时不允许 Pen 抢占；保持惯性中双指立即接续残余速度，以及抬笔后新 Pen Down 抢占惯性。
12. 将活动 Touch Pan 的 Pen suppression 同步到 `WM_POINTER` 光标与触觉入口：按下时清空 mailbox、停止触觉并锁存到对应终态，增加纯判定测试。
13. 将普通惯性收敛到 `4000 DIP/s^2`，用 Touch 终态 QPC 锁存最终速度并覆盖低残余同向接续；主窗口改为显示器高度减 1、创建期 TOPMOST，并暂时允许独立测试宿主激活以验证方向键。
14. 移除 `IManipulationProcessor`/`IInertiaProcessor`：以 QPC、固定 `24` 样本/约 `100ms` Move 窗口和线性拟合统一估速；Up 只补最终位移，拓扑变化重建基准，应用层线性惯性成为唯一实现。
15. 修复长日志暴露的问题：以 double 候选值判断真实硬边界；新零 Touch 批次清除旧中断资格；零位移不覆盖速度；Mouse mailbox 不把 Touch-to-Mouse 提升误作物理 Mouse 抢占；增加对应无窗口回归测试和生产源码 COM 禁用检查。

## Rollback Points

- 导航状态机可独立回退，不改变 Stored 数据格式。
- 动态兜底资源创建或 shader pass 失败时退回权威 tile 重建；不得影响输入、文档或 history。
