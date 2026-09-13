# Mouse interaction validation (2026-09-13)

## Baseline and execution
- Branch feature/eraser; HEAD exactly c433b6f5c34cde5e3286f3beaf6a433a5fabc623 (ahead/behind0/0).
- Initial local diff: pre-existing Inkeys/PptCOM.dll only. Kept uncommitted; no branch/reset/cherry-pick/commit/push/archive.
- All investigation, implementation, review and verification executed by the main session. No subagent was created or invoked.

## Actual modified implementation files
- Inkeys/Inkeys/Drawing/Draw3/Draw3.SpeedEraser.h
- Inkeys/Inkeys/Drawing/Draw3/Draw3.SpeedEraser.cpp
- Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp
- Inkeys/Inkeys/Drawing/Draw3/Draw3.InkPrediction.cpp
- InkeysHeadlessTests/speed_eraser_tests.cpp

Behavior contract updated in .trellis/spec/native-desktop/input-and-ink.md; this task's prd.md/design.md and validation records describe the current interaction. Display, Host, WindowControl, geometry persistence, input acquisition and project configuration were read/verified but not modified.

## Red before implementation
Added short50/120ms burst and established-sweep280/550ms hold assertions while retaining the old code. Complete Debug|ARM64 build succeeded. InkeysHeadlessTests.exe --no-window then failed exactly8 new assertions; prior correctness checks passed. Log: Build/speed-eraser-interaction-red.log.

## Final commands and results
- Located the installed native ARM64 MSBuild with vswhere, then built:
  MSBuild.exe InkeysRepo.sln /m:4 /nr:false /p:Configuration=Debug /p:Platform=ARM64 /v:minimal /fl
- In the same PowerShell invocation, captured existing Path, removed duplicate PATH, restored the captured content under Path, set MSBUILDDISABLENODEREUSE=1. Global environment and project/toolchain configuration untouched.
- Complete solution build: PASS, exit0,0 errors. Remaining warnings originate in existing third-party hash/modeler conversions.
- Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window: PASS, exit0, failures0.
- Worst sampled checkpoint width deviation across60/125/240/1000Hz input,30/60/144/240Hz frames, both modes/physical/DIP/DPI96/144/192:2.16943%, below unchanged5% tolerance.
- Established large-sweep drop after500ms pause:0%.
- Logs: Build/speed-eraser-interaction-build.log and Build/speed-eraser-interaction-tests.log.

## Quantitative acceptance meaning
- Short50/120ms actions including subsequent residual frames stay <=1.25*standard; unlike the old behavior, waiting does not finish an expansion.
- Sustained fast erasing is visibly expanding by350ms (>=1.25*standard and<45%Dmax), reaches>=65%Dmax by800ms and>=85%Dmax by1s. Maximum range is not changed.
- Established sweep remains>=90% after280/500/550ms pause; sustained slow motion is visibly smaller by900ms, then eventually fully settles. The old1.3s near-min requirement was intentionally replaced, not globally loosened.
- Moderate continuous reversals remain near their mapped middle diameter instead of drifting to maximum.
- MouseLifecycle tests exercise production Begin/End/Observe/Advance methods: fast locating hover, zero history at Down, immediate Up reset, accepted-size visual start, no-move completion,20ms repress, late/duplicate events, cancellation, concurrent owners, configuration change, failed Down cleanup, delayed presentation, and an older Up accepted after a newer contact ended plus a configuration change; the final owner is released without reviving old visuals.
- Pen-style controller handoff, real pause/resume without bridge speed, Touch jitter/taps/unlock, invalid/duplicate/backward/late raw time, geometry widths and batch-QPC boundaries remain covered.
- Static review confirms Contact uses accepted realPoint.r, mouse closing only alters cursor state, and storage still receivesr*2. Mouse explicit Up is not a reconnect candidate. Inverted Pen uses the actual eraser enum value.

## Parameters and tuning
- StandardDiameterPx(): same minimum as before; coverage min/max unchanged.
- Evidence160/380ms: raise to require longer fast activity;200ms decay reduced means easier forgetting of brief bursts.
- Growth tau280..160ms/log-rate4..6: raise tau or lower rate for more resistance; interpolation follows actual size.
- Large hold650ms/confirmation680ms: raise for stronger stop/reversal protection, at the cost of later fine-mode return; both timers run concurrently.
- Shrink tau160..300ms/log-rate4..2.2: large sweep releases more heavily; small local corrections are faster.
- Mouse visual release140ms: changes visual speed only; logical reset remains immediate at accepted Up.

## Not run / manual acceptance
No visible or hidden GUI runtime was launched. Real mouse/tablet/touch hardware feel, screen hotplug/rotation and Win7 runtime behavior are not claimed tested. The ~28x19cm laptop is not hardcoded. User should try locating -> short swipe -> sustained back-and-forth -> Up/repress, plus true Touch/inverted-pen interruption and multiple displays.
