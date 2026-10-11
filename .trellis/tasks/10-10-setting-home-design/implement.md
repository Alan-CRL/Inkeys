> 2026-10-11 第四轮直接授权：先阅读 soft-sheen-revision.md；克制背景/文案反光、字标不参与及本轮commit边界覆盖旧冲突约定。

> 2026-10-11 第三轮直接授权实施：先阅读 ripple-revision.md；圆头接缝与明显横幅波纹要求覆盖下方冲突约定。

> 2026-10-10 第二轮已批准实施：先阅读 follow-up-plan.md。本轮布局、删除入口及动效要求覆盖下方第一轮的冲突约定；其余要求继续适用。

# 实施顺序
1. 用户已明确批准本计划实施，规划及context落盘后task.py start，不再次询问授权。
2. trellis-implement负责主页矢量/渲染/主题、三语言、HomeAction接线及setting_design_tests.cpp必要回归；保持其他人的修改，不网站/vendor/无关改动、不GUI/commit/push。
3. 实现代理sync/check/diff自检后报告，完整Solution/--no-window由主会话统一执行。
4. trellis-check独立对照PRD/设计/真实绘制和动作路径、格式审查并修复局部机械问题，修复后重跑受影响检查。
5. 主会话更新必要规范/验证记录，验收据证据勾选。任务保持in_progress供用户审阅，不自动归档/提交。

## 验证
- 可用Python C:/Users/alan-/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe；WindowsApps alias不可运行。
- 用vswhere动态定位VisualStudio ARM64原生MSBuild；同PowerShell invocation先Remove-Item Env:PATH -ErrorAction SilentlyContinue及MSBUILDDISABLENODEREUSE=1，再完整InkeysRepo.sln Debug|ARM64。
- 构建和测试各1800秒deadline，分段汇报，不无故重启进程；环境错误不能通过污染源码/依赖绕过。
- 真实产物Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window，不启动Inkeys.exe/浏览器/GUI。
- Scripts/i18n.ps1 sync/check，git diff --check，tracked+新文件编码/BOM/CRLF核查。
- 生产RenderHome/真实三语言/真实字体/ImGui输入/DrawData：720x520/960x700/1440x900对应内容宽/导航展开收起，浅深，现有1/1.25/1.5/1.875/2/3倍矩阵；字标下伸、署名/按钮预算、链接/点击/拖动/栈平衡与外层滚动。保留既有主页滚动控件回归。
- 只观察动作/目标映射，不在测试执行外链；CPU证据不能替代原生观感。
