# Implementation Record

## Task state

- Task: `08-08-ui3-thickness-fine-dial`
- Final status for this session: `in_progress`
- No commit, push, archive, GUI launch, public API, project-file, resource, configuration, or i18n change.
- The old `08-07` task was not modified.

## Completed work

- [x] Reconfirmed the input -> candidate -> animation -> geometry -> render chain in `research/current-thickness-flow.md`.
- [x] Added the unique `ThicknessViewMode { Preview, Slider, FineDial }` target and minimal FineDial shared snapshots.
- [x] Added mirrored Drag/Click activation geometry, Popup exclusion, Slider-outward dwell, and zero-jump re-anchoring.
- [x] Preserved direct-touch Preview behavior and existing Slider absolute mapping.
- [x] Split Preview morph, Slider track/Thumb opacity, and FineDial progress.
- [x] Added Overflow suppression, external Popup interpolation, and continuous `previewSide` side-flip interpolation.
- [x] Added bounded analytical tick projection, major-label cache, center selectors, fade/envelopes, and device-reset cleanup.
- [x] Added persistent FineDial dragging, Hold freeze/release, fixed-sample inertia, residual swipes, rubber-band, and spring/snap settle.
- [x] Integrated preset/pen-type cancellation, triangle confirmation, capture/touch/lifecycle cancellation, and inactive fast paths.
- [x] Completed static hot-path review, encoding/EOL checks, Git checks, and ARM64 full-solution rebuild.
- [x] Recorded the 65-item acceptance matrix below without treating unrun GUI checks as passed.

## Actual product files

- `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp`
- `Inkeys/Inkeys/UI/Bar/Bar.Main.cppm`
- `Inkeys/Inkeys/UI/Bar/Bar.State.cppm`

All three files are strict UTF-8 without BOM and contain CRLF only.

## Final constants

| Area | Values |
| --- | --- |
| Existing precision source | `BarThicknessPreviewTouchDragTravelScale = 3.0` |
| Activation | Drag gap `3 DIP`, Drag depth `12 DIP`, Click guard `8 DIP`, Click depth `18 DIP`, dwell `500 ms`, existing click slop `5 DIP` |
| Transition / placement | FineDial transition `0.28 s`, Popup panel gap `8 DIP` |
| Projection | `thetaLimit=1.20 rad`, Y lift `4 DIP`, edge fade start `68%`, visible ticks `<=64` |
| Tick visuals | normal `7 DIP`, major `12 DIP`, label `10 DIP`, selector `7x5 DIP` |
| Polling / samples | poll `8 ms`, `dt<=32 ms`, `6` samples over `96 ms` |
| Velocity | release `80 DIP/s`, maximum `900 DIP/s`, friction `10/s` |
| Re-grab | residual weight `0.35`, residual decay `6/s` |
| Boundary / settle | rubber-band limit `24 DIP`, spring `18 rad/s`, damping ratio `1.05`, finish at `0.15 DIP` and `4 DIP/s` |

## Data and ownership flow

```text
pointer / physics raw value
  -> existing 3x unitTravelScreen mapping
  -> exponential visual rubber-band (continuous shared snapshot)
  -> round + clamp to current integer range
  -> thicknessSliderCandidateWidth
  -> drawAttributePenThickness / Popup / FineDial projection
  -> normal settle or Hold release: at most one SetPenWidth(..., true)
```

- Rendering and interaction share only ViewMode, continuous visual value, candidate-active, dragging, and physics-active snapshots in addition to the existing integer candidate/Hold state.
- `Idle / Dragging / Inertia / Settling`, anchors, range snapshot, six-sample ring, velocity, residual velocity, and clocks remain local to `Interact()`.
- When no candidate is active, rendering uses the existing `drawAttributePenThickness` animation and publishes its current visual value so a new grab can take over mid-animation without jumping to the target.
- Preset and supported pen-type changes cancel the old candidate without committing and retain FineDial; opening the pen-type menu also retains FineDial. A valid triangle exit commits the current candidate once and directly targets Preview.
- `CloseThicknessSlider`, capture loss, touch cancel, fold, availability loss, offSignal, and leaving Pen Mode clear shared gesture/physics/candidate/Hold state and target Preview.

## Static review

- Activation priority: direct FineDial drag -> Click activation validation -> Slider outward dwell -> ordinary Slider/Preview behavior. Dwell resets and suppresses Hold.
- Commit contract: no `SetPenWidth` in drag/inertia frames; only normal settle, Hold release, ordinary Slider completion, preset action, and triangle confirmation call it in their established paths.
- Programmatic-change protection: preset and supported pen switches call FineDial cancel before their existing update; physics range/mode mismatch also cancels stale motion.
- Projection: `radius=(availableHalfWidth-edgeInset)/sin(thetaLimit)` and `angularStep=unitTravelLogical/radius`, so the center derivative is exactly the existing 3x unit travel.
- Bounds: visible integer interval is computed directly, clamped to the range, and capped at 64 before iteration.
- Stable active frame: fixed arrays/stack values only; selector geometry and 64-entry label layouts are cached; no FineDial vector/string growth or COM/text-layout creation after cache warm-up.
- Inactive frame: `fineDialOpacity` guards range/projection/tick/label work; physics polling requires the shared physics flag and local Inertia/Settling phase.
- Spec sync: no `.trellis/spec/` update is needed; this is a feature-specific UI contract already captured in this task's PRD/design/research.

## Verification results

- `git diff --check`: PASS.
- Product numstat: `Bar.Main.cpp 1448/161`, `Bar.Main.cppm 18/0`, `Bar.State.cppm 26/12`; no whole-file newline churn.
- Encoding: `UTF8=True`, `BOM=False`, `BareLF=0`, `BareCR=0` for all three product files.
- Git scope: only the three product files plus this new task directory are changed/untracked.
- ARM64 host: `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe`.
- Command: `MSBuild.exe InkeysRepo.sln /t:Rebuild /m /p:Configuration=Debug /p:Platform=ARM64 /nologo`.
- Result: PASS, `0 errors`, `317 warnings`, elapsed `00:03:22.86`.
- Test discovery: no test project and no ARM64 Debug `*Test.exe` / `*Tests.exe` were found. The only produced executable is the GUI `Inkeys.exe`, which was not launched.
- Lint/type-check: no separate native linter or type-check target exists; the full C++20 module compile and link is the available static type/build gate.

`PASS` below means the contract was established by static code inspection or build evidence. Visual appearance, real pointer timing, and physical feel remain `NOT VERIFIED` because GUI execution was explicitly prohibited.

## Acceptance matrix

| # | Status | Evidence |
| ---: | --- | --- |
| 1 | PASS | Preview touch is classified only when target ViewMode is Preview and is excluded from FineDial dwell. |
| 2 | PASS | Drag Zone down calls `ActivateFineDialDrag` immediately. |
| 3 | PASS | Click Zone changes target only after captured, in-zone, <=5 DIP release. |
| 4 | PASS | Named `8 DIP` guard separates Drag and Click zones. |
| 5 | PASS | Click hit testing rejects the current Popup Surface rectangle. |
| 6 | PASS | Ordinary Slider drag tracks stable X in Click Zone for `500 ms`. |
| 7 | PASS | Dwell calls `ResetHoldLockState` and suppresses Hold updates while tracking. |
| 8 | PASS | FineDial drag reuses the existing Hold state machine after candidate movement. |
| 9 | NOT VERIFIED | Re-anchoring is static-proofed, but visible zero-jump behavior needs GUI input. |
| 10 | PASS | FineDial release retains `ThicknessViewMode::FineDial`. |
| 11 | NOT VERIFIED | Track/Thumb crossfade targets are present; visual timing was not run. |
| 12 | NOT VERIFIED | Hold opacity is independent of Slider opacity; rendered result was not run. |
| 13 | NOT VERIFIED | Overflow state is closed on activation; its visual exit was not run. |
| 14 | NOT VERIFIED | Restoration is gated on full Dial exit; its visual result was not run. |
| 15 | NOT VERIFIED | Geometry uses continuous `previewSide`; upper/lower rendering was not viewed. |
| 16 | NOT VERIFIED | Tick brush is neutral gray in code; rendered color was not viewed. |
| 17 | NOT VERIFIED | Multiples of five use longer ticks/cached labels; rendering was not viewed. |
| 18 | NOT VERIFIED | Center tick uses fixed white; rendering was not viewed. |
| 19 | NOT VERIFIED | Cached unit geometry draws two selectors; rendering was not viewed. |
| 20 | PASS | FineDial brushes use theme-neutral/fixed white colors and never read pen color. |
| 21 | PASS | `radius * angularStep == unitTravelLogical`, derived from the existing 3x constant. |
| 22 | NOT VERIFIED | `sin/cos` compression is implemented; appearance was not viewed. |
| 23 | NOT VERIFIED | Smooth edge fade starts at 68%; appearance was not viewed. |
| 24 | NOT VERIFIED | Depth lift and two envelopes are implemented; cylinder feel was not viewed. |
| 25 | PASS | Implementation uses D2D/DWrite primitives only; no D3D mesh/shader was added. |
| 26 | PASS | `firstTick/lastTick` are clamped to current min/max before bounded iteration. |
| 27 | NOT VERIFIED | Exponential 24 DIP rubber-band is implemented; resistance feel was not run. |
| 28 | PASS | Logical candidate always uses `round + clamp`. |
| 29 | NOT VERIFIED | Boundary spring is implemented; visible spring-back was not run. |
| 30 | NOT VERIFIED | Release threshold/friction are implemented; inertia feel was not run. |
| 31 | PASS | Physics integration uses `steady_clock` `dt`. |
| 32 | PASS | `dt` is capped at `0.032 s`. |
| 33 | NOT VERIFIED | Same-direction residual addition is bounded; repeated-swipe feel was not run. |
| 34 | NOT VERIFIED | Signed velocity addition supports cancellation/reversal; feel was not run. |
| 35 | PASS | Value velocity is clamped from the `900 DIP/s` screen cap. |
| 36 | NOT VERIFIED | Re-grab reads current visual snapshot; visible no-jump behavior was not run. |
| 37 | NOT VERIFIED | Projection consumes continuous visual value; rendered motion was not run. |
| 38 | PASS | Existing integer candidate and `SetPenWidth` semantics are retained. |
| 39 | PASS | Hold lock does not change FineDial ViewMode. |
| 40 | PASS | Locked state blocks `ApplyFineDialScreenX`. |
| 41 | PASS | Hold release commits directly and bypasses inertia. |
| 42 | PASS | Hold release leaves FineDial target active. |
| 43 | NOT VERIFIED | Fine endpoint X is selector center; rendered Popup position was not viewed. |
| 44 | NOT VERIFIED | Popup interpolates to the panel exterior; animation was not viewed. |
| 45 | NOT VERIFIED | Fine endpoint uses an `8 DIP` panel gap; rendered spacing was not viewed. |
| 46 | PASS | `penTypeSafeRight` is evaluated only before the fully-Fine endpoint. |
| 47 | PASS | Preset action cancels candidate but does not change FineDial ViewMode. |
| 48 | NOT VERIFIED | Existing thickness animation drives the candidate-inactive Dial; motion was not viewed. |
| 49 | PASS | Supported pen switch and pen-type menu retain FineDial. |
| 50 | PASS | Range is fetched from the current pen mode every render; stale physics range cancels. |
| 51 | PASS | Preset/pen switch cancels physics/candidate before the programmatic update. |
| 52 | PASS | Valid triangle path commits once, clears the chain, and directly targets Preview. |
| 53 | NOT VERIFIED | Geometry derives from animated panel/Preview values; animation was not viewed. |
| 54 | PASS | Hidden/unavailable lifecycle clears physics and render targets. |
| 55 | PASS | Zero FineDial opacity skips range, projection, tick, and label work. |
| 56 | PASS | Physics advances only with shared physics-active plus local Inertia/Settling. |
| 57 | PASS | Selector and label COM resources are cached; stable ticks use bounded stack work. |
| 58 | PASS | `DiscardDeviceResources` resets selector geometry and every label cache entry. |
| 59 | PASS | Capture change/touch cancel clear target, candidate, Hold, capture, and physics flags. |
| 60 | NOT VERIFIED | Existing Slider compiled, but GUI regression behavior was not run. |
| 61 | NOT VERIFIED | Existing Preview compiled, but GUI regression behavior was not run. |
| 62 | NOT VERIFIED | Existing Hold paths compiled, but animation/timing regression was not run. |
| 63 | NOT VERIFIED | Slider safe-bound branch remains, but GUI layout was not run. |
| 64 | PASS | `git diff --check` completed successfully. |
| 65 | PASS | ARM64-host full `Debug|ARM64` Solution Rebuild completed successfully. |

Summary: `36 PASS`, `0 FAIL`, `29 NOT VERIFIED`.
