# Selection 翻页墨迹不可见：源码交接证据（本轮）

- 基线：`bugfix/pptui`，HEAD `2aeca374ea4c863eafe851ea41b52ca21018dd14`；本轮开始时工作区干净，既有 Trellis 任务为 `in_progress`。未执行 Office/设备 GUI，用户现场现象不能记作本轮复现。
- `Draw3.DrawingController.cpp::restoreAfterDocumentSlotSwitch` 把 `publishedCurrentPageHasContent` 先设为新页实际值的反值，再调用 `publishCurrentPageContent`。该函数递增 `currentContentRevision_`，设置 `contentRevisionNeedsPresent`，通过生产 observer 发布 `(hasContent, revision)`。因而不同的 true 页或 false 页会发布新 revision。
- `Draw3.Host.cpp::ObserveCurrentPageContent` 在 `contentMutex` 内只比较 `currentPageHasContent`，相同时直接返回，跳过 `contentRevision.store`、`contentCondition.notify_all` 和 `PublishRuntimeRevision`。这是**已确认源码缺陷**，与 COM、UInk 或工具按钮无关。
- 成功 `ObservePresented` 使用 Controller 传来的新 `presentedContentRevision`；`RuntimeSnapshot` 的 Host `contentRevision` 仍旧。`IdtState::ReconcileDraw3PresentationState` / `Draw3WorkspaceReady` 要求两者相等；选择态在失配时隐藏双表面并重试，但原状态已丢失，不会因重试自行恢复。绘制态可通过主窗路径再次显示，与用户现象相符，但尚无现场 HWND 日志证明这是唯一原因。
- 现有 `CheckPresentationPersistence` 测试使用 `pen.selectionMode=false`，主要等 `presentationReady` 与内容布尔值；此前的 PASS 不证明连续两个有墨迹页的 Selection ULW 目标、内容 revision 或像素正确。本轮先补修复前失败测试，再改 Host。
- 需复核：`ObservePresented` 的成功版本、Host `WaitForContentRevision`/runtime wake、Bar 内容状态是否仍只看 boolean；Host reset/restart 不可复用旧 ready。真实 PptCOM/SlideID/UInk 路径不修改。测试结果与 B 根因另记，不能提前宣称 Office 端到端通过。

## 修复前确定性回归（本轮实际执行）

- A 实施代理先只添加真实 Host 隐藏窗口 Selection 翻页测试，在未修改 `ObserveCurrentPageContent` 时构建完整 `InkeysRepo.sln Debug|ARM64`，**exit 0**；随后以隐藏窗口、隔离保存根运行 `--draw3-hidden-test`，**exit 1（预期失败）**。
- 失败断言：`Selection page content revision reaches the presented ULW frame`。一次可读日志为 `Selection page=0 has=0 content=14 presented=16 output=1/1 revision=3/3`：辅助输出的完整 Present 已报告较新内容版本，Host 仍保存旧版本。此为生产 observer/Host/呈现链的修复前失败证据，不是用户 Office 现场日志。[stderr](../../../../Build/ARM64/Debug/selection_content_revision_pre_fix.err.log)。
- 同次测试还出现最终辅助窗未隐藏断言；测试收尾仍需在修复后复核，不能把全部修复前失败都直接归因为 Host 去重。
- 另一组修复前日志直接覆盖 true→true：`Selection page=1 has=1 content=6 presented=8 output=1/1 revision=3/3`；同一 bool 的 B 页完成 ULW Present，Host 目标 revision 未推进。false→false 则有 `page=2 has=0 content=14 presented=18`。两类状态均由新增生产链用例触发。
- 下游审查：`ObservePresented` 已在成功内容版本变化时推进 runtime revision；`WaitForContentRevision` 等待 Host 的目标 contentRevision；`RuntimeSnapshot` 先 acquire revision 再取 bool；`IdtState` 仍严格比较目标/成功呈现版本；StateMonitoring 依据 runtime revision 唤醒。Bar 的 bool-only 设置只决定工具栏 affordance，不承担页内容版本真值。Host 启动重置目标与已呈现版本。因此修复聚焦 Host observer，不删下游 ready 条件。

## 修复后生产回归（本轮实际执行）

- `ObserveCurrentPageContent` 在同一 `contentMutex` 内按 `(hasContent, revision)` 完整载荷去重；任一字段变化均存储并通知内容条件变量/runtime revision，完全相同才跳过。未修改 Controller 版本产生逻辑、presentationReady、UI-ready、输入 admission 或 UInk/PptCOM。
- 整合 `InkeysRepo.sln Debug|ARM64` **exit 0**；同一隐藏 Host 用例在修复后 `--draw3-hidden-test` **exit 0**，两种 presenter 均 `PASS: real Host held-contact save, drain, cold reload and SlideID reorder`，最终 `PASS: all hidden integration checks`，无 `[Draw3Hidden] FAIL`。[stderr](../../../../Build/ARM64/Debug/selection_content_revision_post_fix.err.log)。
- 新用例全程 Selection 走 A→B→A→空页→B→独立 EndScreen→A，另测不同文稿 false→false；逐目标核对 Host `contentRevision == presentedContentRevision`、成功 ULW 目标/output revision、真实 Window Service 的辅助窗可见/主窗隐藏（空页双隐藏）、A/B/Z 在实际保存 UInk 中不同 ink-point 指纹。Host stop/reset/再进入以及返回 Pen 后的 held-contact 原回归均通过。该隐藏 harness 没有执行完整 `IdtState::ReconcileDraw3PresentationState`，也没有 GPU 像素 readback或真实 PowerPoint/WPS 系统输入；这些仍为 NOT VERIFIED，不能把测试通过写成用户设备现场复现。
