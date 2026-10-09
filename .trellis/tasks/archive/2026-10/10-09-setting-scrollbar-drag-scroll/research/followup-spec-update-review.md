# 后续规范审视（2026-10-09）

本轮沿用已有子任务。上一轮严格原函数正文、帧末投递和额外RowGap合同已由用户最新要求覆盖；第一笔已提交 b8beea0b，本次还授权性能优化和再次提交。

主会话按 workflow Phase3.3 同步 rendering-and-ui.md 的相关Setting签名、两条滚动合同、验证矩阵和真实测试要求，并同步注入摘录。更新限于当前需求，不改其他窗口/输入/vendor/依赖/构建链。

- native右padding已是4DIP，重复RowGap导致8DIP空隙；完整态按真实按钮/轨道测量，动画过程中同源width与clip，固定区不变。
- Begin内装饰可能写parent drawlist，完整Combo/Expander封装必须在真实items前参与width；不能事后全页vertices修改或扫描，退出装饰无输入且最终零留白。
- 每window状态随context销毁，稳定storage key不受PushID栈影响；Style/card跨BeginEnd残留栈需精确恢复实际style字段，不能只比栈大小。
- 持续已捕获手势先准备当前帧位移，实际宿主首次Begin前提交target，本帧native更新真实Scroll/items/DrawData；未提交或已关闭popup不能收到残留target。首次归属/候选控件使用现有拖动阈值，保留click与Slider/Input/native scrollbar控制权。
- CPU测量与一帧延迟分别记录；真实坐标先被ImGui floor，不能用直接改MouseDelta声称实机亚像素改善，baseline/optimized应同实际输入与有效样本，打印median/p95，不能以混合idle mean或no-op重复调用作性能证据。

最终完整ARM64 Solution退出0/65.726秒，完整--no-window退出0/125.807秒、stderr空；独立审查PASS，详细过程及真实基线/CPU采样见followup-implementation.md / followup-review.md。两条短Combo在1.875倍率与原生均有0.5px实际溢出，另用确实零溢出的内容测完整退出，保持严格endpoint。实际触屏、鼠标观感和DX11呈现保留人工边界，整页CPU采样不作一致提速结论。父子任务继续in_progress，本轮按明确授权commit、不push/GUI。
