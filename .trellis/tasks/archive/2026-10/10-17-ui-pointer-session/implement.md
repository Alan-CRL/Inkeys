> 2026-10-17 最新批准修正：请先读 [correction.md](correction.md)。普通点击仍由 HiMsg Down/Up 完成；Raw 只补充拖动。该修正覆盖下文旧的 Raw 独占与精确绑定要求。

# Implementation plan

- [x] Carry forward reviewed scope and user implementation approval; create task, inspect repository and specs.
- [x] Shared provider/session implementer: define header contract first; implement private ordered transport, registration ownership and deterministic tests; register source in existing projects.
- [x] Bar implementer: integrate input contract, WM_TOUCH cancellation/source ownership, existing cursor-light raw handling, all continuous and press handlers; remove gesture dependence on cache/GetAsyncKeyState; preserve hook files/calls.
- [x] PageControl implementer: integrate provider and own-context wake/drain for all four windows, preserve hit/pair/position/repeat/Whiteboard semantics.
- [x] Integrate and run a fresh trellis-check agent over full diff; fix verified findings.
- [x] Locate current VS ARM64 native MSBuild using vswhere; in the same PowerShell invocation Remove-Item Env:PATH -ErrorAction SilentlyContinue; set MSBUILDDISABLENODEREUSE=1; build InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 with >=5 minutes allowed. No build-system/dependency changes to bypass environment errors.
- [x] Run built InkeysHeadlessTests --no-window and relevant early-exit non-GUI probes only; record actual exit codes and output. No hidden-window tests or main GUI.
- [x] Check git diff/encoding/EOL, verify hook paths/calls unchanged, update relevant spec with learned stable contract, record validation. Do not commit/push.

## Execution
Main session owns task artifacts, integration/build and final spec update. Parallel implementer ownership is disjoint: shared Input + project/test registration; Bar files; PageControl files. They must coordinate header API before relying on it, preserve others' changes and report any necessary boundary expansion. Full-scope check follows implementation.

## Native click correction pass

- [x] 记录用户批准的 HiMsg 原有入口与拖动可选 Raw 边界，更新实现/检查上下文。
- [x] Input: MouseGesture 原生身份、可选 Raw、阈值准入、终态/轨迹去重、独立来源接管版本与生产策略测试。
- [x] Bar: 恢复原生 HiMsg Down/Move/Up，只有连续入口准备补充，按原阈值接受 Raw Up。
- [x] PPT: 原生 Down 立即进入，按钮/长按不申请 Raw，候选/实际拖动分离，保留 pair 更新。
- [x] fresh trellis-check 审查并修复确认问题。
- [x] 完整 Debug|ARM64 solution、headless --no-window 和适用生产探针；记录退出码。
- [x] 更新最终 spec、编码/换行/diff/hook 检查及验证记录；保持不提交。

## One-second native drag correction

- [x] 记录用户实机反馈与1000ms pending取消链，明确未绑定补充不能取消原生拖动。
- [x] 共享 MouseGesture 统一代次/取消裁决，Bar/PPT 删除 Dragging 旁路；保留1000ms期限。
- [x] 实际 channel+policy 多秒原生拖动、三类PPT入口、绑定失败/溢出与旧取消回归。
- [x] fresh trellis-check与最终完整solution编译、正常环境无窗口测试/生产探针。
- [x] 最终spec/格式/hook检查、验证记录；不提交推送。
## Edge-anchor and silent Raw follow-up

- [x] 记录同屏普通前台也可复现的用户证据、两份研究及最小修改边界。
- [x] 共享 MouseGesture 静默切换/回放下限/Raw Up 与生产回归；保留全部已有点击/一秒修正。
- [x] Seek 逻辑抓点跨显示发布保持与限频无消息期诊断；生产纯几何回归。
- [x] fresh trellis-check、完整 Debug|ARM64、无窗口测试和两个生产探针。
- [x] 最终 spec、格式/hook 检查与验证记录；不提交推送。

## Final acceptance and close-out

- [x] 2026-10-17 用户人工验收基本通过，授权 commit 和结束当前任务；范围限本任务，不 push。
- [x] 记录最终验收，沿用已完成的独立审查、编译和无窗口测试；未修改产品代码。
收尾顺序：当前任务工作提交 → 归档当前任务 → 记录会话日志 → 检查最终 Git 状态；完成状态以 Git 历史和归档任务元数据为准。
