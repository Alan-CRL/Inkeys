# F-041 旧 PPT page-index 文件的安全迁移决策

现有同进程 PPT 保存服务会把旧 page-index UInk/index 与同路径 StableSlideId target 判为可升级，但旧文件没有 SlideID 或旧 bindingToken。仅凭路径、页数、Inkeys sessionId 不能证明旧 ordinal 页对应新 Office SlideID；自动贴合可能串页。F-039 后生产 materialize 对缺 SlideID 返回失败，保留旧 UInk/index；然而活动页可能显示空白，新 Stable 笔迹随后可能因旧 index/fileGuid 冲突无法保存。研究证据在 `ppt-fallback-upgrade-research.md` 与独立 `ppt-pending-topology-load-review.md`。

用户于 2026-09-28 明确选择：**保留旧 page-index 文件，新 StableSlideId 会话独立保存**。旧 `presentation_descriptor_tests.cpp` 曾期望同路径重新放映即允许 ordinal 升级，该期望缺页身份凭证，不能作为正确性证明；本次应修订该测试并记录行为差异。旧文件/index 原字节必须保持，不能把旧页猜贴新 SlideID；新的 Stable 会话保存须使用独立的持久化身份和可回读索引，不得在用户书写后才得到 SourceChanged/静默丢保存。跨 Inkeys 进程恢复仍保持未开放。

实施依赖先查 `PresentationAutoSave` 对同 `key/source` 的索引约束与可安全保留旧记录的分支方式，按同一生产 worker 的失败路径建无 HWND 红测，再让新 Stable 会话的 fileGuid/workspaceGuid/index 成组分叉；旧文件不能被覆盖/删除。加载冲突与旧笔迹不自动显示须有准确状态/诊断，不能报“已恢复”。真 Office 重进、页重排、结束页和 Win7 验收仍需人工。
