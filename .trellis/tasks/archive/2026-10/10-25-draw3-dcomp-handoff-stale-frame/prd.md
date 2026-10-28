# Draw3 DComp 交接旧帧闪烁修复

## Goal

消除绘制与选择模式切换时偶发的一帧旧画布内容，尤其是激光消失后从 Selection ULW 切回主 DComp 表面时旧激光短暂复现的问题；同时确保激光在本帧最终进入 Inactive 时可靠清理内部瞬态资源。交接等待不得降低普通连续绘制的帧吞吐。

## Background

- 主 DComp 与辅助 Selection ULW 共用最终 backbuffer；输出目标变化已触发整幅重合成和全帧 Present，见 `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:14629`、`:14682`。
- 主 DComp visual 在模式切换期间持续绑定交换链，隐藏主窗不会清除其最后已显示帧，见 `Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cpp:657`。
- DComp 路径当前以 `Present1` 返回作为提交成功，随后 Host 立即把全帧发布为 handoff ready，见 `Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cpp:1278`、`Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:9060`、`Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp:992`。
- `Present1` 返回不证明 DWM 已消费并显示新帧；状态线程可能先恢复主窗可见性，导致隐藏前旧帧短暂露出。窗口成对显隐及目标/revision/content 门控本身保留，见 `Inkeys/IdtState.cpp:688`、`Inkeys/Inkeys/Window/Window.cpp:3121`。
- 用户确认采用最小修复：仅在 DComp 输出交接的全帧 Present 后、发布 ready 前等待 `DwmFlush`；普通逐帧呈现不增加该等待。

## Requirements

- R1：DComp 输出目标变化后的首次全帧呈现，必须先成功完成 `Present1` 和 `DwmFlush`，才能向 Host 发布可用于窗口交接的成功 ready。
- R2：`DwmFlush` 只覆盖需要建立交接上屏屏障的 DComp 帧；普通绘制帧、ULW 输出以及未发生交接的呈现不得新增该同步等待。
- R3：上屏屏障失败时不得把该帧发布为成功 ready；保留现有旧可见表面和重试/恢复收敛，记录包含目标、revision 与 HRESULT 的诊断。
- R4：保留现有 DComp → ULW 候选顺序、窗口 owner-thread 成对显隐、目标/revision/content 门控、脏区优化和透明像素合同；不通过固定延时、重复 Present 或改变交换链模式规避问题。
- R5：增加确定性的交接门控验证，证明 Present 返回但屏障未完成时不能 ready，屏障成功后才允许 ready，并覆盖失败与普通帧不等待的行为。
- R6：激光过期边沿必须根据本帧最终生命周期求值结果判定；无论第一次还是第二次求值进入 `Inactive`，都必须在当帧执行 coverage、bounds 和 stroke layers 清理，且不重复触发下一帧清理。
- R7：增加覆盖“第二次求值才过期”的回归测试，验证旧激光像素清除与内部瞬态资源退休一致；保持 Hold/Fade/新 Down 的既有生命周期语义。
- R8：按项目规范完成完整 Solution 构建、既有无窗口回归和差异检查；真实桌面视觉验证与软件测试证据分开记录。

## Acceptance Criteria

- [x] AC1（R1/R5）：交接帧在 `Present1` 已成功、`DwmFlush` 尚未成功完成时不发布 handoff ready；屏障成功后才发布相同目标、output revision 和 content revision 的 ready。
- [x] AC2（R2/R5）：普通 DComp 帧和所有 ULW 帧的交接屏障调用计数为零；每次需要的 DComp 输出交接只执行一次有效屏障，不多帧重复等待。
- [x] AC3（R3）：屏障失败不产生成功 ready，不错误切换窗口；后续沿现有失败重试/恢复路径收敛，并有可定位日志。
- [x] AC4（R4）：既有输出 generation 往返、双窗互斥可见、空 Selection clean 握手及普通墨迹呈现回归保持通过。
- [x] AC5（R6/R7）：构造第一次求值仍为 Fade、第二次求值进入 Inactive 的帧边界，断言当帧执行最终激光清理；下一帧不重复过期，Hold/Fade 和新激光 Down 行为不变。
- [x] AC6（R8）：完整 `InkeysRepo.sln` 的 `Debug|当前设备原生架构` 构建通过，`InkeysHeadlessTests.exe --no-window` 通过，`git diff --check` 通过。
- [x] AC7（R1/R8）：在真实桌面 DComp 路径反复执行“激光绘制 → 选择 → 等待消失 → 绘制”和普通笔迹快速往返，不再观察到旧内容回闪；若本轮无法执行可见窗口验证，明确记为设备待验收，不宣称视觉验证通过。

## Out of Scope

- 重构 DComp visual 生命周期、双目标并行呈现、修改窗口 Z 序/owner、替换交换链或修改普通帧调度。
- 除最终过期边沿外的激光生命周期、粒子或淡入淡出算法调整。
- 新增产品设置、设备厂商专用分支或扩大 Windows/架构支持承诺。

## Validation Notes

- 纯逻辑/可控屏障测试证明门控顺序，不能替代真实 DWM 上屏视觉证据。
- 隐藏 HWND 集成可复核 generation 和窗口交接回归，但不宣称覆盖用户桌面上的偶发时序。
- 本任务按轻量缺陷修复管理，仅需 PRD，不拆父子任务。
- 2026-10-25：完整 `InkeysRepo.sln Debug|ARM64` 构建通过；最终版 `InkeysHeadlessTests.exe --no-window` 通过（`PASS animation correctness`），日志为 `TestResults/draw3-dcomp-handoff-headless.log`。
- 2026-10-25：`Inkeys.exe --draw3-control-fence-test` 与 `Inkeys.exe --draw3-hidden-test` 通过；后者末行 `PASS: all hidden integration checks`，日志为 `TestResults/draw3-dcomp-handoff-hidden.log`。`git diff --check` 通过。
- 2026-10-25：用户确认人工真实桌面视觉验证完成，AC7 通过；全部验收条件满足，可以提交并归档。
