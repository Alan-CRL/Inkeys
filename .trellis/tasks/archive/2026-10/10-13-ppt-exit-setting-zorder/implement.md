# 执行计划

1. 读取规范及调查证据；两个 implement 分别拥有 PPT state/Bar同步与 Window/Setting层级，避免交叉编辑。
2. 实现 frame-ready 连贯交接和条件Selection后的Bar刷新，退出边沿根恢复，保留revision/输入/清理guard。
3. 实现设置 owner attach/show 时的即时层级和幂等，检查独立owner线程互等风险；扩充现有有效回归。
4. 独立 trellis-check 审查跨层顺序、fallback、白板策略、编码与minimal diff，修正已定位的问题。
5. 使用已安装 ARM64 原生 MSBuild，现有 InkeysRepo.sln Debug|ARM64，先同一PowerShell规范化 Env:PATH 并禁用node reuse，至少5分钟（本次10分钟）超时。
6. 构建成功后 InkeysHeadlessTests --no-window 和经确认无GUI的相关生产CLI。含可见窗口套件/真实PowerPoint/CUA不执行。记录API属性与视觉验证的区别。
7. 更新冲突规范与任务验收记录。用户未授权本任务commit/push，不延续前两任务授权。
