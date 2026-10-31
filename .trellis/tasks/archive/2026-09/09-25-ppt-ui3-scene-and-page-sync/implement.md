# Implementation checklist

## Planning gate
- [x] Baseline/working tree/current tasks/compiled source inspected; relevant specs read.
- [x] User approved complete latest plan in subsequent message.
- [x] PRD/design/research and implement/check manifests prepared.
- [x] validate/start.

## Execution
- [x] Baseline regression and timing probes.
- [x] Pair ownership; toggle/end/reset save semantics; transactional config failure handling.
- [x] Effective scale through actual Scene/hit/presentation/resource fit.
- [x] Managed cached show session/event wake/guarded exit + tests.
- [x] Native session observation, revision wait, timing.
- [x] Draw3 contact boundary and UI-ready gate + hidden tests.
- [x] Bar scene geometry, main-only modal/single-flight, focus and i18n.
- [x] trellis-check full-scope review/fixes.
- [x] Spec updates, verification report, journal --no-commit.

## Verification
Locate ARM64 MSBuild with vswhere (initial discovery C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/arm64/MSBuild.exe). In same PowerShell: Remove-Item Env:PATH -ErrorAction SilentlyContinue; set MSBUILDDISABLENODEREUSE=1; invoke MSBuild InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /m /nr:false. Allow >=5min, inspect exit/first meaningful errors.
- [x] Full solution.
- [x] Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window.
- [x] Build/ARM64/Debug/Inkeys.exe --draw3-hidden-test.
- [x] Existing Bar offscreen entry extended for actual Scene if necessary.
- [x] PptCOM.Tests.csproj Release|AnyCPU + executable.
- [x] Real Controller presentation persistence/cold-recovery harness added and passed (no storage-engine rewrite).
- [x] Scripts/i18n.ps1 check.
- [x] git diff --check; encoding/newline and scope diff.
- [x] Actual synthetic native wait-boundary before/after measured; real Office latency and idle CPU explicitly NOT VERIFIED.

## Matrix
Positions: on/off toggle edges, top/save-off/bottom-drag/reenter-top, reset-off, late snapshots, alternating pairs, old settings write, delayed/failed save, capture cancel/window failure, nonpersistent DPI fit.
Scale: dpi1/1.5/2/3/4 x user.5/1/2.5/3; >4 and fitted<.5; extremes; real pixels/hit; whiteboard.
Pages: ordinary/reverse/rapid/direct/animation, end/unknown/reentry, A-B-A/same names/rebind/reorder warm/cold, active pen/mouse/touch and old Up, stale ready/UI/load/save, failed Present/UI.
Bar: entry/exit table, animation/fold/whiteboard/repeats/taskbar/DPI, mutual nonexclusion.
Exit: main only/cancel/dismiss/fail/doubleclick/modal end-reopen/oldDLL. Real native keyboard/Office hardware tests NOT VERIFIED until actually run.

## Wrap up
Record actual commands/exit codes/measurements and NOT VERIFIED manual steps in research/verification.md. Keep source/environment failures separate. No commit/push. Journal/archive --no-commit; acceptance pending => in_progress, not falsely archived.
Manual Office/WPS/device acceptance remains NOT VERIFIED; task remains in_progress. See research/verification.md for authoritative command results and exceptions.

## Supplemental work (2026-09-25 follow-up; original checked items remain historical)

- [ ] Real Office/desktop system hit-test reproduction of the exact interceptor HWND; source-proven Waiting gap and default-off correlated diagnostics are recorded in research/exit-window-supplement.md and research/supplement-verification.md. GUI acceptance remains NOT VERIFIED.
- [x] Implement versioned true-exit Selection/Desktop handoff, owner-thread safe visibility/capture fallback and retry; hidden Host + Window Service verifies both presenter paths and no-input convergence.
- [x] Establish typed EndScreen identity and same-file marked UInk design; test codec encode/decode and strict import before final Host validation.
- [x] Implement target/ready/-1 UI ack, independent pageGuid/history, normal SlideID protection, old-file import and cold End-first Host restoration.
- [x] Regress A/B plus prior remember/scale/Bar/confirmation paths; run full Solution Debug|ARM64, headless, hidden, offscreen, complete UInk, managed, i18n and diff checks. Sandbox file replacement failure is distinguished from passing sandbox-exempt reruns.
- [x] Update touched specs and research with command/exit-code evidence and NOT VERIFIED Office/device steps. Keep task in_progress, no archive or commit for this follow-up.

## Regression supplement: selection content and large PageControl (new turn)

- [x] Recheck branch/HEAD/worktree/current task and applicable native-desktop/native specs; preserve previous product decisions. Baseline `2aeca374`, clean at start.
- [x] Add R17–R19, design boundary and source-vs-runtime research; keep task `in_progress` and validate implement/check contexts.
- [x] A: pre-fix real Host Selection regression exit1 on true→true and false→false content revision; Host pair-payload repair, post-fix A/B/E/EndScreen/restart/held-contact hidden test exit0. Actual Office/IdtState system hit-through still NOT VERIFIED.
- [x] B: pre-fix asymmetric old-budget headless exit1; shared group budget, Scene/ULW/window failure propagation, bounded resource retry/backing reset; four offscreen HWND/DPI/pixel/hit/one-sided ULW+resource recovery exit0. User现场单侧消失错误阶段仍 NOT VERIFIED.
- [x] Full `InkeysRepo.sln Debug|ARM64`, headless, Draw3 hidden, PageControl hidden, Bar offscreen, i18n and diff checks executed this turn; Trellis check found and fixed stale drag budget, reviewed build/hidden/headless again. See research/regression-verification.md for exact logs/environment limits.
- [x] Record session 24 with `--no-commit` after context/diff validation; task stays `in_progress`, no commit/push authorization for this follow-up.
