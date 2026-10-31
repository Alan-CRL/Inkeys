# F2：PPT冷槽暂时读取失败自动重试

## Goal

PPT 合法冷槽遇短暂读取 I/O 失败后，在既有预算内自动重试并恢复原墨迹和准入。

## Confirmed Facts

- 父任务需求：R2 / F2。
- `Draw3.PresentationAutoSave.cpp:1568–1575` 把 ReadUInkFile 暂时 IoError 误归 SourceChanged，导致 `Draw3.DrawingController.cpp:12061–12077` 进入 manualOnly。
- 原始报告共同根因仍未确认；本项成功不代表所有用户现场已解决。

## Requirements

1. 只纠正读取结果分类，复用既有250/500/1000/2000ms四次退避和同 target/generation 身份。
2. 真正源内容变化、损坏/不完整数据和跨进程身份冲突仍受保护，未读成功不得安装空白覆盖旧墨迹。
3. 预算耗尽行为保持；不增加永久自动重试、手动恢复界面或新的持久化格式/COM接口。
4. 用真实 worker/UInk 文件加真实生产 retry/admission 路径验证，不能仅靠历史摘录探针。

## Acceptance Criteria

- [x] 真实独占文件时 Load completion=IoError；预算内释放文件后同目标自动成功加载，无需重启或目标切换。
- [x] 真实 SourceChanged、损坏、身份冲突仍不自动准入写入；原文件内容保持。
- [x] 既有四次预算、迟到回执、generation改变、PrepareExit屏障与当前加载重试生产测试通过。

## Scope and Dependencies

- 功能依赖：无。可独立实施、检查、回退；最终交付必须与父任务其他三项一起通过集成验证。
- 执行依赖：父任务最终规划摘要经后续用户确认后才允许 task.py start；F1合入后再进行开启诊断的跨项GUI验证。
- 共用 IdtMain.cpp / 项目登记 / DrawingController 测试块的修改由主会话按序整合；F2/F3若涉及同文件，由同一实现者先F2后F3处理，不并发覆盖。
- 完整构建由主会话串行执行，不同时写同一MSBuild中间目录。
- 已授权隔离GUI原生验证；不使用Computer Use，不修改用户安装/原配置/原视频。
- 最小diff并保留编码/换行；不commit/push，不做未证实的其他冻结修复。

## Evidence

- 父PRD：`.trellis/tasks/archive/2026-10/10-14-canvas-freeze-investigation/prd.md`。
- 专项实施前研究：`.trellis/tasks/archive/2026-10/10-14-canvas-freeze-investigation/research/f2-f3-fix-plan.md`。
- 历史实验/模型与完整产品作用域以父任务两份调查报告为准，测试须执行修复后的真实生产代码。
