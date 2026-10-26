> 第三轮直接续作：当前要求与验收以 [scrollbar-revision.md](scrollbar-revision.md) 为准，修复缓存到终态的滚动条与页面布局跳变；前两轮结果保留。

> 2026-10-14 第二轮已直接批准继续优化和测试。最新要求、实现边界与验收以 [submission-revision.md](submission-revision.md) 为准；首轮验证保留在 validation.md。UI共用WARP、Draw独立hardware；保留CPU帧以维护延迟动画/光标/轮询，精确相同输出跳过实际提交。

# 实施与验证

1. 主会话核对worktree/stash与旧记录，保留当前PptCOM.dll原字节备份。
2. trellis-implement 负责 Setting.Pages.h/.cpp、Setting.cpp、现有setting_home_warp_tests和setting_design_tests必要回归；必要独立头文件可复用生产缓存，尽量不改工程。关键中文注释，原编码/CRLF，禁止GUI/build/commit。独立离屏硬件诊断可在同测试源码实现。
3. 主会话ARM64原生MSBuild完整InkeysRepo.sln Debug|ARM64，同PowerShell Remove-Item Env:PATH 与MSBUILDDISABLENODEREUSE=1；构建允许1800s且定期汇报。现有--no-window全套测试允许1800s。
4. trellis-check独立审查资源/输入/材质/恢复及基准真实性；主会话统一构建运行，必要修复后重跑。
5. 定向规范更新、性能证据报告、i18n check、git diff --check及编码审计；不commit/push，任务保留供用户继续。

产品改动边界：缓存与开屏状态所在Setting.Pages/Setting.cpp及共用缓存helper；测试仅已有夹具/必要入口，不改共享管线、vendor或无关工具栏。