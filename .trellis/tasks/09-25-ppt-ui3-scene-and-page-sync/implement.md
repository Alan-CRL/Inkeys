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
