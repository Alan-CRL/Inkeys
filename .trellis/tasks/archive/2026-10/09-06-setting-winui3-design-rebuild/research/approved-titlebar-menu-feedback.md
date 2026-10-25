# 当前批准增量：标题栏版本色与汉堡按钮反馈

2026-10-09用户要求先提交并结束调试子任务，完成后继续两处小修改。调试任务已完成实现926db4d0、归档76e33791、日志d8dda60c；父任务保持in_progress，本轮沿用父任务直接fluent，不新建任务/分支。当前仅以下增量实施授权，不继承此前GUI或commit/push授权。

## 需求与设计
- 标题栏实际 dev. 版本文字当前使用ImFluentCol_TextSecondary，改为与邻近theme/Debug/caption glyph相同ImFluentCol_TextPrimary；active=1.0、inactive=0.55保持，字号、命中、版本路由、窄窗让位不变。
- 汉堡##pane-toggle默认透明无边框保持。当前ControlFillSecondary/Tertiary为普通按钮色，浅色近白与pane底对比不足；局部将hover/held映射为当前palette subtleHover/subtlePressed，复用真实Fluent Button颜色动画、键盘焦点/原ID/尺寸与toggle逻辑。优先只增两个局部PushStyleColor并对应Pop；不改vendor/global button或架构，不增加持久字段或真实投影。
- 若代码/真实回归发现原因还有其他细节，按本轮需求最小修正，不顺手重构。

## 实施与验收
派发trellis-implement直接修改，随后trellis-check独立审查，不递归派发。保持原UTF8/BOM/CRLF与中文关键注释。
在现有真实DrawData/导航回归里追加必要覆盖：汉堡idle透明、hover/pressed和mouseleave恢复，浅深/展开紧凑overlay、多倍率，以及实际点击只切换一次；比较主题palette的真实背景颜色，不复制vendor混色公式。版本色静态核对实际标题栏路径与相邻控件一致，保留active/inactive系数。
动态ARM64原生MSBuild完整InkeysRepo.sln Debug|ARM64，同PowerShell PATH规范化/MSBUILDDISABLENODEREUSE=1，至少300秒timeout，复用上轮正常用户权限脚本（建议1800秒deadline、/m:1 /p:CL_MPCount=1），随后完整--no-window、i18n check、diff/编码。遇runner C1902使用已有环境修复，不改源码/工程绕过。
实施完成后保留本轮未提交改动；不GUI/Computer Use/commit/push，不结束父任务。记录research/titlebar-menu-feedback-validation.md。
