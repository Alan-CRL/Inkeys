# 当前阶段执行计划：用户批准的新启动预设
1. 三个implement分工：A配置+预设UI+i18n生成；B共享位置resolver+IdtMain Mini编排+正式Bar初始化/首帧；CPreview超椭圆/反光/环形/缓存发布+真实配置/几何/状态/离屏测试。
2. B先发布StartupPlacement公共header和Initialization签名，C与A据此接入，避免文件冲突。C owns startup_preview_state_tests和共享superellipse路径header+Bar.Rendering原语提取；B不修改这些。
3. 代码就绪后独立trellis-check全范围审查，主会话按已有VS/ARM64 MSBuild构建完整solution，至少5分钟；已知sandbox PDB/临时写入限制时审批后外部验证。
4. i18n sync/check、headless --no-window与真实离屏像素测试、git diff/CRLF检查；修复实际发现后重测相关范围。
5. 主会话更新startup-preview/config/UI规格和任务journal；不commit/push/GUI、不finish或archive任务。

## 2026-10-14 现场反馈修正执行
1. Preview implementer仅负责共享条形/环形绘制选择、45°渐变方向和既有离屏/纯函数回归；保留上一轮IdtMain安全测试入口。
2. 独立Trellis审查检查旧条形尺寸/颜色、两形态选择、DPI/zoom角度一致性及端点裁切，主会话完整Debug|ARM64 solution + Headless --no-window与实际离屏图片检查。
3. 同步规格与当前PRD验收，不新建任务，不GUI/commit/push，状态继续in_progress。
