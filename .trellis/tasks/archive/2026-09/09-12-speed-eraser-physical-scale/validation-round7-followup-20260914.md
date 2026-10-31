# Round 7 follow-up: verified area fixes and Experimental Options entry

Date: 2026-09-14.
Task remains in progress; no push or archive.

## Commit boundary

The requested checkpoint was committed first:

- 8c05076c: feat(draw3): checkpoint lighter Touch response and contact-area assistance

PptCOM.dll was excluded and remains a pre-existing/generated working-tree change.
The corrections and UI entry described below were made after that commit and were not committed again.

## Corrections

1. High-rate Touch samples repeatedly snapped to a moving target whenever the per-packet difference was below settleLogTolerance. This bypassed damping at 1000 Hz. At 300 ms, the failing area trace was 20.2586 DIP at 1000 Hz versus 18.2792/18.2067/18.2198 at 60/125/240 Hz. DirectTouch now only snaps when the target is stable. IndirectDip and ScreenPenHybrid retain their previous behavior.
2. The hidden test waited for idleSeconds > 0.4, although a correctly sleeping renderer stopped publishing snapshots sooner. That added the default 15-second test timeout.
3. The artificial delay then caused the unchanged modeler to reject requests for 5560 and 10965 samples, above its existing 2000-sample per-input limit. No new geometry meant there was no new size boundary to save. The test now waits for explicit needsAnimation=false and verifies the frame sequence stops. Modeler rules and the persistence format were not changed.

The existing modeler limit still applies to sufficiently long input gaps. This follow-up does not claim arbitrary-duration pause/resume support.

## Experimental UI

Location: Settings > Experimental Options > Inkeys3.

The new temporary Touch contact-area switch uses the existing card/toggle style, adds 75 DIP of panel height, and reads/writes the same Host DevelopmentOptions value as the diagnostic-page control.

- Default: off.
- Applied to the next independent contact batch.
- In-memory only; resets on restart.
- Real mapped screen Touch speed eraser only.
- No separate setting state, no JSON persistence field, no Mouse/Pen parameter changes.

## Actual validation

Located ARM64-native MSBuild through vswhere. In the same PowerShell invocation, preserved Path, removed the duplicate PATH entry, restored Path and set MSBUILDDISABLENODEREUSE=1.

Build command:

~~~text
MSBuild.exe InkeysRepo.sln /m:4 /nr:false /p:Configuration=Debug /p:Platform=ARM64 /v:minimal /fl /flp:logfile=Build/eraser-area-fix-build.log;verbosity=normal
~~~

- Full solution build: exit 0.
- InkeysHeadlessTests.exe --no-window: exit 0.
- Area sampling/frame-rate maximum deviation: 0.429525%, below the unchanged 5% target.
- Existing speed sampling/frame-rate maximum deviation: 1.99186%.
- Twelve frozen Mouse/ScreenPenHybrid float-bit traces: zero differences.
- Added assistance-off slow Touch moving-target regression: passed.
- Inkeys.exe --draw3-eraser-hidden-test: exit 0.
- Both DComp and ULW exercised area-assisted cursor/geometry agreement, no-Move sleep, scheduled area expiry, current-radius resumption, fixed bypass, Undo/Redo and actual UInk save/read/import.
- Resumed geometry maximum radius: 8 px in both hidden scenarios; old history radius remained 19.5 px before resumption.
- git diff --check: exit 0.
- Modified text decodes as UTF-8. C++ BOM/EOL conventions were retained; the existing mixed-EOL portions of the specification were not normalized.

Warnings remain in third-party code, aligned contact records and test-local shadow/conversion sites. This was not a warning-cleanup task.

The older full --draw3-hidden-test suite was not rerun or declared passed.

## Touch parameters and measured behavior

Unchanged from the preceding Touch checkpoint:

| Motion scale | Fine / enter / exit / large-target speed |
| --- | --- |
| Trusted/manual physical | 30 / 90 / 60 / 250 mm/s |
| DIP fallback | 100 / 240 / 160 / 700 DIP/s |
| Resolution/DPI heuristic | 100 / 120 / 80 / 400 reference DIP/s |

History: 50 ms. Evidence start/full/decay: 25/60/180 ms.
Growth tau: 120/100 ms; logarithmic growth limits: 6/8 per second.

Deterministic physical replay, area off:

- 180 mm/s: permission at 70 ms; 64 DIP at 306 ms; steady target about 83.14 DIP.
- 250 mm/s: permission at 55 ms; 32/64/96 DIP at 137/251/320 ms.

Area assistance retains its 50 ms stable-drag reference, 1.10 * max-axis + 6 DIP floor, normally capped at 64 DIP, separate 96 DIP rejection threshold and 3.5 aspect-ratio bound. Unknown metrics and bad data disable assistance, not Touch input.

## Files and records

Product/test changes in this follow-up:

- Draw3.SpeedEraser.cpp/.h: Touch settle condition and explicit diagnostic animation state.
- Draw3.DrawingController.cpp: publish the actual animation-needed state.
- Draw3.HiddenWindowTest.cpp: state-based waiting and bounded phase diagnostics.
- Setting.cpp: Experimental Options card sharing the existing temporary option.
- speed_eraser_tests.cpp: failure detail and assistance-off moving-target regression.

Updated input-and-ink.md, task PRD and design to remove superseded standard-floor/Touch physical-target contracts. The earlier validation-round7-touch-area-20260914.md remains an accurate historical checkpoint.

Logs:

- Build/eraser-area-fix-probe.log
- Build/eraser-area-probe-hidden-out.log
- Build/eraser-area-probe-hidden-err.log
- Build/eraser-area-fix-build.log
- Build/eraser-area-fix-tests.log
- Build/eraser-area-fix-hidden-out.log
- Build/eraser-area-fix-hidden-err.log

## Manual acceptance remaining

No visible settings/app UI or Computer Use was launched.
The Experimental Options layout is compiled but has not been visually exercised.
Surface A/B: first test Touch speed with assistance off, then turn it on to compare slow-drag visibility and over-erasure risk. Inspect actual input relationship, area-unit validity and manual calibration values.
No physical teaching display, external tablet or Windows 7 runtime acceptance is claimed.

