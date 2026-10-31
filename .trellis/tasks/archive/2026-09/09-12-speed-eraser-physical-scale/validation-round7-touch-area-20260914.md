# Round 7: Touch response and contact-area assistance

Status: in progress. Not acceptance-ready. No commit, push or task archive.

## Baseline and scope

- Baseline: feature/eraser at a427e84ad181f0a0a930950a2561b34e0731e504.
- The only pre-existing working-tree change was Inkeys/PptCOM.dll; it was preserved.
- All work was performed by the main session. No agents were delegated.
- The user approved the round-7 Touch-only instruction document on 2026-09-14.
- Mouse and ScreenPenHybrid response parameters, target compensation and handoff behavior are frozen.
- The original headless suite reproduced five failures. The original focused hidden suite passed.
- Four baseline failures were physical Touch reversal checks at 144/192 DPI in both device modes.
- The other baseline failure applied a steady-state bound at 200 ms: the measured sequence was 19.1918, 16.9266, 16.972, 16.972 DIP.
- New pre-implementation regressions also showed that 36 mm local sweeps at 180 mm/s did not grow adequately, and Touch targets still depended on physical density.

## Implemented checkpoint

- DirectTouch targets now remain DIP. ScreenPenHybrid beta compensation is unchanged.
- Physical Touch: fine/enter/exit/large target = 30/90/60/250 mm/s.
- Touch DIP fallback: 100/240/160/700 DIP/s.
- Touch resolution/DPI heuristic: 100/120/80/400 reference DIP/s.
- Touch history window: 50 ms. Evidence start/full/decay: 25/60/180 ms.
- Touch growth tau: 120 ms, or 100 ms for larger sizes. Log growth limits: 6/8 per second.
- Existing shrink/hold behavior was not globally retuned.
- Independent temporary development toggle: touchContactAreaAssistance, default false.
- The toggle follows the existing in-memory development-options behavior; it is not persisted.
- The toggle is latched with the contact batch and stripped from the Mouse/Pen display-scale revision.
- Area is eligible only for real mapped screen Touch, never merely a forced Touch response.
- Existing raw WIDTH/HEIGHT decoding is preserved. Additional metadata records raw values and whether the per-context conversion can be treated as canvas pixels.
- Unknown units, inconsistent axis/span resolution or invalid declared ranges disable assistance without rejecting the input point.
- Area converts per-axis pixels to DIP once. It does not use EDID to calculate coverage.
- Default floor: clamp(1.10 * max(widthDip,heightDip) + 6, standard, min(maximum,max(standard,64))).
- Reported spans outside 2..96 DIP, aspect ratio above 3.5, invalid numbers and outliers are rejected before floor clamping.
- Reference confirmation requires 50 ms of consistent real dragging. Initial displacement protection still applies.
- The first confirmed reference is locked for the contact; later pressure/shape changes do not increase it.
- Missing packets retain the reference for up to 2 seconds. Explicit invalid samples have a 200 ms grace period. Release uses a 180 ms reference decay.
- Area growth requires real motion and follows the same accepted width timeline. No-Move growth from refreshed area is prohibited.
- Reconnect shifts area clocks; terminal zero dimensions are not new area input.
- An existing idle wait also schedules area expiration, rather than continuously rendering a settled floor.
- Hidden tests use an explicitly synthetic logical display surface, with no fake EDID, and mirror the real RTS Touch begin/end notifications.

## Actual validation

### Full build

Located ARM64-native MSBuild with vswhere and built:

~~~powershell
$buildPath = $env:Path
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:Path = $buildPath
$env:MSBUILDDISABLENODEREUSE = '1'
& $armMsbuild InkeysRepo.sln /m:4 /nr:false /p:Configuration=Debug /p:Platform=ARM64 /v:minimal /fl '/flp:logfile=Build/eraser-touch-build.log;verbosity=normal'
~~~

Exit code: 0. PptCOM remained part of the solution build.
Warnings include existing third-party/alignment warnings and test-local shadow/conversion warnings.

### Headless

Command: Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window

- Original baseline: exit 1, five failures.
- Baseline with new regressions: exit 1, ten failures.
- Current checkpoint: exit 1, nine failures, all from the new area sampling-rate comparison.
- Original five failures are no longer reported.
- The 200 ms low-speed assertion was separated from its later steady-state checks; Mouse behavior was not retuned.
- Twelve frozen Mouse/ScreenPenHybrid trajectories matched the baseline float bit patterns exactly.
- Existing speed/sample/frame comparison maximum deviation: 1.99186%.
- New area sampling-rate comparison maximum deviation: 10.1285%, above the required 5%. This remains unresolved.

Measured constant physical Touch motion, area off:

| Speed (mm/s) | Final target (DIP) | Permission (s) | 32 DIP (s) | 64 DIP (s) | 96 DIP (s) |
| --- | --- | --- | --- | --- | --- |
| 10 | 20.1481 | not reached | not reached | not reached | not reached |
| 30 | 32 | not reached | 0.719 | not reached | not reached |
| 60 | 32 | not reached | 0.691 | not reached | not reached |
| 90 | 32 | not reached | 0.681 | not reached | not reached |
| 120 | 37.124 | 0.104 | 0.252 | not reached | not reached |
| 180 | 83.1423 | 0.070 | 0.147 | 0.306 | not reached |
| 250 | 160 | 0.055 | 0.137 | 0.251 | 0.320 |
| 350 | 160 | 0.047 | 0.132 | 0.246 | 0.315 |

These are deterministic replay measurements, not physical-device feel tests.

### Focused hidden product integration

Command: Inkeys.exe --draw3-eraser-hidden-test

Launched with Start-Process -WindowStyle Hidden, redirected output, and WaitForExit(300000).
Both DComp and ULW scenarios were exercised. Current exit code: 1.

Reported failed assertions:

- held Touch settles at accepted assistance floor
- expired-area resume uses current radius without fat tail
- area-assisted size boundary survives actual UInk save/read/import

Do not classify these as product defects or fixture timing issues without further diagnosis.
Do not claim cursor/geometry/persistence acceptance has passed.

Logs:

- Build/eraser-touch-baseline.log
- Build/eraser-touch-baseline-hidden-err.log
- Build/eraser-touch-probe-baseline.log
- Build/eraser-touch-build.log
- Build/eraser-touch-tests.log
- Build/eraser-touch-hidden-err.log

## Remaining work

- Diagnose and fix the new area sampling-rate deviation without weakening the 5% target.
- Diagnose the three focused hidden failures; distinguish stale diagnostic snapshots, fixture timing and actual geometry faults.
- Rerun affected validation after an approved correction.
- Complete the code-spec update and final diff/encoding checks.
- Run human Surface A/B acceptance with area off first, then on, and inspect actual input relationship, units and manual calibration values.
- Real teaching displays, external tablets and Windows 7 runtime behavior have not been tested.

