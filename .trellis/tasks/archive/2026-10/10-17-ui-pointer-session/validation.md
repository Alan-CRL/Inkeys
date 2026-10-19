# Validation — 2026-10-17

## Delivered
- Shared process-wide Raw Mouse provider, bounded private mailbox and source/contact/generation sessions, active desktop checks and pending-start cancellation.
- Bar main drag, swatches, 2D picker, thickness slider/preview/popup/FineDial, discrete pressed lifecycle; Raw does not enter HiMsg and WM_TOUCH retains HiMsg path.
- Four PageControl PPT clients consume private raw input in own WndProc; touch source isolation, pen compatibility, mode/session cancellation, original pair movement/persistence retained.
- Full MSG.pt/time dispatch seam avoids current-position substitution for queued history; native/Raw binding uses screen point + timestamp + ordered generation.
- Specs updated, including old border-light registration rules. Hook files and install/stop calls unchanged.

## Build
- Used existing `InkeysRepo.sln`, `Debug|ARM64`, native ARM64 MSBuild discovered through vswhere.
- Same PowerShell invocation first removed shadow Env:PATH and set MSBUILDDISABLENODEREUSE=1. No project/toolchain/dependency changes were made to bypass environment issues.
- Final normal-environment full build: **exit 0**, **0 errors**, 435 existing warnings, elapsed **00:03:30.24**. Warning locations in modified files: **0**.
- Log: `Build/ARM64/Debug/ui-pointer-session-build.log`.
- Earlier builds: one intentionally interrupted to apply independently reviewed correctness fixes; one sandbox run stopped after confirming idle MSBuild worker respawn/IPC blockage. Host process inspection showed multiple near-zero-CPU MSBuild nodes, and no active compiler; normal environment immediately compiled. This was an execution-environment problem, not a source error. Separate interrupted/sandbox logs remain under Build/ARM64/Debug.

## Executed tests
| Command | Result |
| --- | --- |
| `InkeysHeadlessTests.exe --no-window` | exit 0; final `PASS animation correctness` |
| `Inkeys.exe --state-mode-production-test` | exit 0 |
| `Inkeys.exe --pen-width-production-test` | exit 0 |

Logs: `Build/ARM64/Debug/ui-pointer-session-tests.log` and `ui-pointer-session-production-probes.log`. Both production probes were explicitly waited with ProcessStartInfo UseShellExecute=false/CreateNoWindow=true and 60s timeout; their CLI branches return before configuration/window initialization. The initial direct GUI-subsystem invocation did not yield an exit code and was not counted as verification.

New regression coverage: source/device/contact/generation isolation; early Up; native-before-Raw and missing Raw; pending-only 1000ms cancellation with stationary bound hold retained; same-time FIFO/outside clicks; 32-bit time wrap; bounded overflow; full queued excursion/time; dispatch exact match/nesting; cross-client touch lease; null-device touchpad; Pen isolation; PageControl hSource/primary/fallback behavior.

## Final static checks
- `git diff --check --ignore-submodules=all`: exit 0.
- Project, filters, headless XML parse: PASS.
- All 12 modified tracked files: UTF-8 decode PASS, original BOM and CRLF retained.
- Hook and Bar.Initialization paths: `git diff --exit-code` exit 0. No existing hook installation/uninstallation change.
- Independent Trellis checker fixed confirmed issues and reported no remaining definite in-scope issue; review notes under research/check-findings.md.
- No GUI/Computer Use/hidden HWND tests, commits or pushes were performed.

## Manual verification remaining
- Normal-integrity app with elevated foreground; actual Win7 WM_TOUCH beyond window; mouse/touchpad/high-rate inputs; pen compatibility capture; UAC/lock/unlock and rapid source switching.
- Background Raw Input and the automated regressions do not prove universal UIPI bypass or actual device-event completeness.
- Existing MouseHook private clickCollapsePending vs Bar ConfirmaNoMouMsgSignal disconnect remains by explicit hook exclusion. Selection/clickRecover delayed collapse may still occur.
- Task kept unarchived because code is intentionally uncommitted; no archive/journal auto-commit scripts were run.

## Native click correction — final verification

### Final behavior
- Ordinary Bar native Down/Move/Up retains HiMsg entry; native Down immediately starts independent mouse identity and native Up completes ordinary clicks without Raw availability or matching.
- Optional private Raw starts only in continuous/drag paths. Candidate Raw Up cannot fold/click/navigate. Confirmed Raw identity supplies one ordered movement stream; native Down/Up stay valid. Duplicate/old terminal, older final position/time and queued source-admission regressions were fixed during independent check.
- PageControl native Down/Move/Up restored in its own context; arrows/long press have no Raw claim, eligible drag regions retain original thresholds and pair movement/persistence.
- Touch/Pen replacement is independent of Raw; queued Bar Down stores a value-only source admission stamp. No hook edits or install/uninstall changes.

### Build
- Existing InkeysRepo.sln Debug|ARM64, native ARM64 MSBuild found by vswhere; same invocation normalizes Env:PATH and MSBUILDDISABLENODEREUSE.
- Initial correction build exit0, 00:00:26.21. It compiled main changed modules and tests; 14 new test timestamp narrowing warnings were then fixed, leaving only three pre-existing hashlib third-party warnings from main compilation.
- Final solution incremental build exit0, 00:00:05.20; 0 warning, 0 error. No dependency/config/toolchain change.
- Logs: Build/ARM64/Debug/ui-pointer-session-correction-build.log and ui-pointer-session-correction-final-build.log.
- Existing vcpkg app-local target first tries unavailable pwsh.exe (exit9009), then successfully falls back to absolute Windows PowerShell; solution succeeds. Same fallback exists in prior task build. No project change was made for it.

### Tests
| Final command | Environment | Result |
| --- | --- | --- |
| InkeysHeadlessTests.exe --no-window | normal host | exit0, PASS animation correctness |
| Inkeys.exe --state-mode-production-test | restricted runner | exit0 |
| Inkeys.exe --pen-width-production-test | normal host | exit0, preferences/state/FineDial PASS |

- Final headless log: Build/ARM64/Debug/ui-pointer-session-correction-tests-normal.log.
- Final pen-width log: Build/ARM64/Debug/ui-pointer-session-correction-pen-width-normal.log.
- Restricted headless first returned exit1 with 35 failures in existing config/atomic-file persistence cases; restricted pen-width failed retired-legacy-preferences-and-memory-ignored. Exact same final binaries pass on normal host with no source changes. These are execution-environment failures, not corrected by modifying production or build configuration. Initial failed logs retained separately.
- All processes were explicitly waited, CreateNoWindow/Hidden; probes return before application/config/Window Service startup. No GUI/hidden HWND test.

### Static and independent review
- Fresh trellis-check fixed optional Raw Replaced leaking to clicks, queued Down source resurrection, older terminal point/time rewind, same-ms stream duplication/loss, and new C4244 chains. See research/correction-check-findings.md.
- UTF-8/BOM/CRLF checks, project XML and task jsonl target checks PASS; git diff --check --ignore-submodules=all exit0.
- Hook/init hashes match correction baseline; git diff for IdtDrawpad, IdtFloating, MouseHook and Bar.Initialization is empty.

### Remaining limits
- Strict time+point claim is still only an OPTIONAL Raw association. It may fail, but ordinary native clicks are not gated by it.
- On late Raw binding, historical Raw at/before already-consumed native time floor (including same-ms) is skipped to avoid duplicate movement. Already coalesced earlier excursion cannot be reconstructed; after binding ordered Raw trajectory is retained.
- Actual elevated-foreground, Win7 touch, touchpad/pen and secure-desktop hardware behavior was not GUI tested. Automated PASS is not a UIPI bypass guarantee.
- Existing hook delayed-collapse state disconnect remains explicitly outside scope. No Git commit/push or archive/journal auto-commit.

## One-second drag correction — verification

User device feedback confirms clicks and drag start work with elevated/UIPI foreground, but drag stopped after about one second without release. This is user-reported device evidence, not an agent GUI run.

### Cause and change
- Provider's pending Raw Down deadline remains 1000ms and emits Cancel with generation0. Clients had elevated every supplement cancellation to business Cancel just because Dragging=true, even though Raw had never bound.
- MouseGesture now centralizes the distinction: generation0 revokes only optional supplement; already-native actual drag continues. Nonzero channel generation is proof of acquired Raw ownership, including lost queued Down, so actual bound transport failure still explicitly cancels.
- Bar/PPT removed their `|| Dragging()` override; stale cancellation time/known generation is rejected. Explicit desktop/mode/source/lifecycle cancellation remains separate. No fabricated Up, timer extension, hook, touch, activation or privilege change.

### Build and tests
- Complete InkeysRepo.sln Debug|ARM64, discovered ARM64 native MSBuild; same invocation PATH normalization and MSBUILDDISABLENODEREUSE=1.
- Build exit0, elapsed00:00:22.48, 0errors, 3 existing hashlib++ C4267 warnings, no new warning. Existing vcpkg pwsh fallback succeeded through absolute Windows PowerShell as in prior builds.
- State-mode production probe exit0; pen-width production probe exit0 (preferences/state/FineDial PASS), normal host, no HWND.
- Headless --no-window final binary: exit0, PASS animation correctness; includes all new multi-second pointer/PPT regressions. Executed normal host with CreateNoWindow/Hidden and explicit five-minute bound.
- Logs: Build/ARM64/Debug/ui-pointer-session-timeout-build.log, ui-pointer-session-timeout-tests.log, ui-pointer-session-timeout-production-probes.log.

### Independent/static review
- Fresh checker reported no blocking source issues and made no code changes; research/timeout-check-findings.md.
- Actual MouseChannel+MouseGesture tests exercise 999/1000/1500/5000ms, absent/mismatched Raw Down, unbound overflow, nonzero bound failure/overflow before Down consumed, stale cancellation and native Up exactly once. PPT Background/Page/DragHandle use same production policy.
- UTF-8/BOM/CRLF, project XML, diff-check and hook/init hash/path checks PASS. No GUI/hidden HWND, commit or push.
- Real post-fix elevated foreground/device behavior still requires user retest; upstream reason for Raw not binding (missing packet vs conservative association mismatch) has no new event trace and is not asserted.
## Edge-anchor and silent Raw follow-up — verification

### User evidence and causal limits
- User reports large drag against screen limits permanently shifts the cursor-to-grip offset until release. This also reproduces on one monitor with ordinary non-elevated foreground; neither UIPI nor a real cross-monitor DPI change is sufficient as the asserted explanation.
- In elevated foreground, moving back to an ordinary window does not reliably resume; returning near the stopped toolbar position may resume. This is consistent with own-window input hit coverage, but current logs contain no event-stream proof. The new trace is intended to discriminate rather than assume the cause.

### Implemented known defects and evidence
- Fixed definite shared policy lockout: a once-bound Raw Down no longer suppresses every future valid native Move forever. A valid native event at least50ms beyond last valid Raw activity may supply movement and advance strict replay floor; stationary native continuation also advances floor. This is an event-time authority threshold, never an Up/Cancel timer. Valid stationary Raw refreshes activity, old/floor samples do not. Ordered same-ms Raw excursions and true Raw Up during native continuation remain supported.
- Fixed constructible conditional Seek rebase defect: environment adoption no longer rebuilds grabOffset from current pointer minus constrained/lagging presentation. Original logical point is preserved, with offsets scaled only by actual presented zoom ratio through production ScaleBarDragGrabOffsetForZoom. This does not by itself establish the full ordinary same-monitor incident root.
- Added active Seek begin/end and500ms UiPointerDrag summaries, including during no-message waits. Consumer-side native/Raw arrivals before policy and accepts, latest samples, raw bound/disabled/activity, accepted/cursor/grab/desired/presented, applied/presented translation, display serial/zoom, foreground and owner GetGUIThreadInfo capture are compared. GetCursorPos is diagnosis only, never movement/button inference. No global Raw arrival claim when conservative matching never bound.

### Independent check
- Fresh trellis-check verified six files and found no additional product logic defect; corrected only new test C4456 by monitor-to-edgeMonitor local identifier rename. Report research/edge-stream-check-findings.md.
- Production MouseGesture regressions cover50ms boundary, bound stream silence with native continuation for seconds, stationary freshness/floor, late/same-ms Raw rejection, new Raw recovery, real Raw Up while native owns movement, both terminal orders, timestamp wrap, and preserved existing source/generation/1000ms cases.
- Pure production geometry policy tests cover large edge excursions and return, repeated equal zoom, and actual zoom changes preserving the original logical point. These plus static Seek wiring do not replace HWND/device evidence.

### Build and executed tests
- Complete existing InkeysRepo.sln Debug|ARM64, current VS ARM64 native MSBuild located through vswhere; same invocation normalized Env:PATH and set MSBUILDDISABLENODEREUSE=1. Normal host avoids the previously demonstrated restricted runner MSBuild/config-file failures. No configuration/toolchain/dependency workaround.
- Main implementation build exit0,00:01:04.32,0errors,122warnings:121 existing broader third-party/Setting/render warnings and one new test local-shadow warning. The new warning was fixed by the checker.
- Final complete solution incremental build after that identifier-only test correction: exit0,00:00:06.74,0warnings,0errors. Product behavior/source was unchanged after the executed tests; no redundant test rerun for a local test variable rename.
- Headless --no-window exit0, final PASS animation correctness; state-mode production probe exit0; pen-width production probe exit0 with preferences/state/FineDial PASS. All executed on normal host, sequentially, explicit ProcessStartInfo UseShellExecute=false/CreateNoWindow/Hidden and waited for actual ExitCode (300s headless/60s probes). CLI probes exit before application/window initialization.
- Logs: Build/ARM64/Debug/ui-pointer-session-edge-build.log, ui-pointer-session-edge-final-build.log, ui-pointer-session-edge-tests.log, ui-pointer-session-edge-state-probe.log, ui-pointer-session-edge-width-probe.log.
- Existing vcpkg app-local step first attempted missing pwsh.exe and successfully used absolute Windows PowerShell fallback, as in prior builds; no source workaround.

### Final static verification and next evidence
- All modified tracked UTF-8/BOM/CRLF preserved; new source CRLF, project XML and task jsonl references PASS; git diff --check --ignore-submodules=all exit0.
- Hook, Other.Inputs and hook installation/init paths match the pre-follow-up SHA256 baseline; git diff for these paths is empty. No activation/capture/privilege/manifest or PPT business change in this follow-up.
- No GUI, Computer Use, hidden HWND, commit or push. Task stays active/unarchived pending real reproduction evidence.
- New binary Build/ARM64/Debug/Inkeys.exe. On user retest the ordinary log/idt*.log contains UiPointerDrag automatically; use begin/tracking/end to distinguish rejected or absent claimed samples, coordinate mismatch, changing anchor and presentation divergence. If both streams are absent, no arbitration-only fix can guarantee cross-privilege movement or release; activation/capture or signed/deployed UIAccess remain separate product-policy options.

## User acceptance and task closure — 2026-10-17

- 用户明确反馈：“好的，人工验收基本通过，提交 commit 并结束当前任务吧”。据此接受当前实现并授权本任务工作提交及 Trellis 归档/会话记录提交；没有授权 push。
- 人工验收是用户实机反馈，不是代理 GUI 测试；未补充具体 Win7、设备或所有 UIPI 组合的覆盖声明。既有两路输入均缺失等 API 能力限制作为后续调查边界保留，不阻止本次用户确认的结案。
- 收尾仅补记验收和任务文档，产品代码与上一轮通过独立审查、完整 Debug|ARM64 构建、--no-window 及两个生产探针的版本一致，因此不重复编译或启动 GUI。
- 核对全部 dirty paths 均属于本任务；两种 hook、安装/卸载、激活/权限/部署策略保持未修改。按一个工作提交，然后任务归档和会话日志提交结束，不推送。
