# 实施顺序与验证

- [x] 用户批准实施，新建10-09子任务并关联现有父任务，直接fluent，不新建工作区/分支。
- [x] 记录PRD/设计/实施计划和规范上下文。初始git status --short --ignore-submodules为空。
- [x] 派发trellis-implement，直接修改产品并扩展现有测试，不递归派发。
- [x] 动态定位ARM64 MSBuild，完整InkeysRepo.sln Debug|ARM64（同PowerShell先Remove-Item Env:PATH；MSBUILDDISABLENODEREUSE=1）。用已有验证方式，超时至少300秒，遇环境错误不改代码绕过；必要时/m:1 /p:CL_MPCount=1。
- [x] 现有InkeysHeadlessTests.exe --no-window，Scripts/i18n.ps1 check，git diff --check与编码换行检查。
- [x] 派发trellis-check全范围独立检查，局部问题自修；必要时针对修复重新构建/测试。
- [x] 主会话更新规范和verification.md，报告实际结果及GUI未验收边界。保持任务in_progress，不自动归档/commit/push。

## 测试重点
标题栏960/720/窄预算、多倍率，Debug/主题/version/caption disjoint且命中安全；导航展开/紧凑/overlay、移除footer调试空间；普通->Debug->普通连续帧，动画中途/反向/禁用/滚动，不残留旧蓝条位置；EDID空/单屏/多屏/长名称/窄宽/多倍率，完整字形、卡片内部无滚动、超过视口父滚动；四开关原配置绑定/ID/保存保持且IDT_RELEASE无遮挡。优先真实DrawData/产品helper，不复制实现公式当测试。
