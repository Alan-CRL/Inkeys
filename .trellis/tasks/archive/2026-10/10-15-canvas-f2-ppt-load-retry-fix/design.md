# F2：PPT冷槽暂时读取失败自动重试：设计

## Change Boundary

行为缺口是 reader 的 IoError 被误归 SourceChanged，现有 Controller 因此完全跳过自动预算。唯一必要产品行为点是 Draw3.PresentationAutoSave.cpp::LoadPresentation；不更改reader、预算、Host去重、存储身份或界面。

## Selected Design

在 ReadUInkFile(path) 后、现有 strict guard 前，单独把 IoError 映射为 PresentationPersistenceStatus::IoError。其余七种reader状态与document/provenance/sourceRevision/import/workspaceGuid校验沿用原路径。索引存在但文件不可读不等于 NotFound，不能进入可写空白lane。

真实completion进入既有PresentationCurrentLoadRetry / TrySubmitCurrentLoadAtRunSafePoint；四次delay仍为250/500/1000/2000ms，target、slot generation、pending、mutation和exit barrier不变。预算内恢复后，只有成功materialize/install才解除准入；预算耗尽或真实身份/损坏终态继续保护。

## Expected Files

- Inkeys/Inkeys/Drawing/Draw3/Draw3.PresentationAutoSave.cpp：IoError分类与简短中文原因注释。
- Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp：扩充已有RunPresentationCurrentLoadRetryProbe，使用真实worker/文件completion接入同一生产retry与安装helper；与F3串行整合。
- inkStrokeModelerTestTests/presentation_autosave_tests.cpp：仅在需要复用既有真实服务严格分类fixture时扩充；不是必须新增的独立构建系统，不修改demo协调器代替主产品。

## Tests and Limits

隔离root保存含已知墨迹的合法snapshot/index/UInk，对真实文件持不共享句柄直到load completion；验证IoError、未到期不提交、到期一次提交、pending不重复。释放句柄后同目标真实Loaded，检查原笔迹和真实安装/准入。源revision变化、坏header/tail、foreign session和workspace不符仍安全失败，不写空白覆盖。

复用 --draw3-ppt-current-load-retry-test 和 --draw3-pending-topology-load-test；保留四次预算耗尽、target/generation变化、PrepareExit迟到结果回归。旧摘录probe仅是历史红例，不能替代真实生产回归；完整Office联动未测部分单列。

## Dependencies and Rollback

功能无其他子任务依赖；执行上由同一实现者在F3前完成Controller共享文件部分，主会话统一构建。恢复原分类即可回退本行为点，但不自动回退他人文件；不commit/push。

专项依据：`.trellis/tasks/archive/2026-10/10-14-canvas-freeze-investigation/research/f2-f3-fix-plan.md`。本方案已获父最终摘要审批，当前进入实施。
