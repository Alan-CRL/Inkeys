# 白板工作区切换超时放宽与进展归零

## Goal

用户反馈进入/退出白板时偶发弹出“白板窗口切换未完成，已停止自动重试”。当前失败预算过短（约 2 秒无进展即判死），且同一次切换内各种失败共用 4 次额度、中间成功不清零，低性能设备上“慢但仍在动”的切换会被误判为失败。本任务只放宽预算并让真实进展清零计数，不改变切换流程本身。

## Background（只读调查结论）

- 弹窗唯一来源：`FailWorkspaceRequest`（`Inkeys/IdtState.cpp:458`）→ `QueueWorkspaceTransitionFailureNotice`（`Inkeys/Inkeys/UI/Setting/Setting.cpp:5624`）；文案 `Dialogs/WorkspaceTransitionFailure/{Whiteboard,Return,Primary}`。
- 判死逻辑：`WorkspaceHandoff::Fail`（`Inkeys/IdtState.cpp:263`）`++failures >= 4` 即 `failed`，退避 250/500/1000ms；`failures` 仅在 `Request()` 时清零。
- 三类失败共用该额度（`StateMonitoring`，`Inkeys/IdtState.cpp` 约 4340–4690）：
  1. **等待无进展**：`WorkspaceWaitTracker::Stalled`（`IdtState.cpp:303`）250ms 无变化计一次，调用点 4557 / 4593 / 4606 / 4634（BackgroundInactive / TargetReady / BackgroundPrepared / FinalBackgroundActive）。退避后立即再判，单阶段实际约 2.0–2.75 秒无进展即弹窗；白板冷加载（`whiteboardLoadPending`）与 UI 背景准备不计为进展。
  2. **原生命令在途超时**：`WorkspaceNativeOperation::NoteExecutingTimeout`（Executing > 1500ms 计一次，调用点 4404）。
  3. **原生回执 Rejected**：`nativeFailed`（约 4534）把 `Cancelled/Expired` 以外阶段计为故障；包括 Prepare/Complete 校验失配、恢复事务卡住后的连锁拒绝等。

## Requirements

- R1 预算放宽：任一失败类别连续无进展满 **15 秒** 才判定切换失败并弹窗；判定时刻不因退避间隔而明显超过 15 秒。
- R2 分类别进展归零：失败按类别独立计时——等待（Wait）、原生 Prepare、原生 Complete。某类别取得真实进展时只清零该类别：
  - Wait：等待跟踪器观察到阶段变化或已跟踪字段（输出/内容/PPT UI ready/背景 ready/revision）变化。
  - Prepare / Complete：对应原生回执 `Committed` 且被消费。
  - 一个类别的进展不清零另一个类别（防止“Prepare 成功、Complete 反复失败”无限循环显示/隐藏）。
- R3 保留既有语义：新请求（`Request()`）仍清零全部计时；`Cancelled/Expired` 仍不计失败；失败通知仍只在首次判死边沿发布一次；`Retry` 行为不变；原生失败后的源预览恢复不变。
- R4 原生重试退避：保留指数退避起点 250ms，上限 2 秒，且下一次重试不晚于该类别的 15 秒判定时刻。
- R5 最小修改：不改 Window 服务 1500ms 命令期限、不改 Prepare/Complete 校验内容、不加诊断日志。

## Acceptance Criteria

- [ ] AC1（R1）Headless：同一类别以 2 秒间隔持续失败时，未满 15 秒不判死；满 15 秒的那次失败判死且只产生一次通知边沿。
- [ ] AC2（R2）Headless：Wait 类别在接近 15 秒时取得进展后重新计时，再需连续 15 秒无进展才判死。
- [ ] AC3（R2）Headless：Prepare 进展不清零 Complete 类别；Complete 连续失败满 15 秒仍判死。
- [ ] AC4（R3）既有用例语义保留：取消到源仍保留旧原生责任、`Retry` 只接受当前失败 token、早于退避期限的唤醒不重试、`failed` 后不重复原生 show/hide。
- [ ] AC5（R1/R4）退避后的重试时刻 ≤ 类别判定时刻；首次退避仍为 250ms。
- [ ] AC6 `InkeysRepo.sln` Debug|ARM64 完整构建成功，`Inkeys.exe --state-mode-production-test` 返回 0。
- [ ] AC7 `git diff` 仅涉及计划内文件，编码与换行符保持不变。

## Out of Scope / Deferred（需记录，后续任务处理）

- **诊断日志系统**：未来接入日志系统，在各失败点记录类别、阶段、`CommandPhase`、`NativeError/RollbackError`、耗时、`whiteboardLoadPending`、`canvasPolicyDirty_` 等，用于定位真机偶发问题。本任务不加日志。
- **白板窗口化与冗余判断移除**：白板未来将成为窗口化存在；届时 Prepare/Complete 中的层级（Z 序、owner、topmost、band）校验等多余判断应移除，只需确保窗口出现，层级交由周期置顶刷新（再经过一次置顶即正确）。涉及 `WorkspaceNativePolicyMatches`、`ApplyBand`、`ReconcileRelativeOverlayGroup` 等。
- 调查中发现但本任务不修的竞态：`ExecuteTracked` 两次 `CommandCurrent` 之间 guard 失效被记为 `Rejected`（`Window.cpp:3918-3935`）；Complete 阶段 `WorkspaceNativePolicyMatches` 失配不走 Cancel；恢复事务卡住后的连锁拒绝。
- 等待进展口径扩展（如把 `whiteboardLoadPending` 变化或 runtimeRevision 计为进展）不在本任务内。
