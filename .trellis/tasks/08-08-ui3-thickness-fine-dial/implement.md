# Implementation Record

## Task state

- Task: `08-08-ui3-thickness-fine-dial`
- Final status for this session: `in_progress`
- Base feature checkpoint: `44e4eaba2ca8e463343b44bc90069bfe011e230e` (`feat(ui3): add thickness fine dial`).
- First GUI-correction checkpoint: `b272d4c922cd792c0d68e9fc52d43f35120cc9d3` (`fix(ui3): refine thickness fine dial activation`).
- The second GUI correction remains uncommitted and unstaged in the working tree; there is no push, archive, or GUI launch.
- No public API, project-file, resource, configuration, or i18n change.
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
| Activation | Drag gap `3 DIP`, Drag depth `12 DIP`, Drag/Click shared boundary, Click depth `18 DIP`, dwell `1000 ms`, armed-Drag horizontal slop `5 DIP`, recognition base opacity `0.5` |
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
  -> Brush-canonical 3x unitTravelScreen mapping
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

## Checkpoint verification results

The build and 65-item matrix below belong to checkpoint `44e4eaba`; the correction matrix later in this file supersedes conflicting activation behavior.

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

## Checkpoint acceptance matrix

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

## GUI correction after checkpoint

### Implemented correction

- Major labels retain the `DWRITE_TEXT_ALIGNMENT_CENTER` cache and now center the complete `metrics.layoutWidth` box on the exact projected `tickX` used by `DrawLine`.
- `TryGetBarThicknessFineDialActivationGeometry(...)` is the single activation-geometry source. `HitTestBarThicknessFineDialFreshActivation(...)` keeps Popup exclusion, while `IsBarThicknessFineDialDwellZone(...)` tests ongoing Slider dwell geometry without Popup occlusion.
- Slider dwell requires a latched outward approach, keeps a fixed 5 DIP X anchor, lasts 1000 ms, suppresses Hold, and publishes an independent `0 -> 0.5` activation-preview progress while `ThicknessViewMode` remains `Slider`.
- Renderer-local activation-preview progress only contributes to final Dial opacity. Slider Track/Thumb, Popup endpoint and Overflow suppression continue to use the real ViewMode/full FineDial progress. Cancellation fades the preview out; formal activation hands the shared `0.5` preview to the renderer once and holds it until full progress catches up.
- Pointer-to-value mapping and sampled screen-velocity conversion both use the reversed sign. Residual velocity, inertia, boundary and spring code continue to consume the same value-velocity convention.
- Drag Zone Down arms `fineDragActivationArmed`; only horizontal movement beyond 5 DIP activates from the current value/current X. Down+Up restores the prior ViewMode/hover/pinned/value state.
- Click Zone Down immediately calls the normal FineDial drag activation path using current thickness/current X. The old release-validation state was removed, and this path never calls `ProjectWidthFromScreenX(...)`.
- Ordinary dwell cancellation, touch cancel and abnormal capture-loop exits clear activation-preview shared state. A successful dwell activation preserves only the one-shot handoff until the renderer consumes it; `CloseThicknessSlider` and `WM_CAPTURECHANGED` still force-clear all preview state.

### Review finding (fixed)

- File: `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp`.
- Issue: `ActivateFineDialDrag(...)` previously called `ResetFineActivationDwell()` before changing `ThicknessViewMode`. That cleared shared preview progress to zero without any renderer acknowledgement, so a render thread that had not consumed the last `0.5` dwell frame could restart the formal Dial opacity from zero.
- Fix: ongoing dwell now passes `preserveActivationPreview=true`. Formal activation marks the shared preview inactive but preserves its progress as a one-shot handoff. Slider/FineDial transition frames seed the renderer-local preview from that value; the first FineDial frame consumes it, and full FineDial progress then catches up without an opacity reset. Fresh Click/FineDial re-grab and Drag-armed activation pass `false`; ordinary cancellation still clears the shared preview immediately.
- Findings not fixed: none from static review. GUI-only visual continuity and input feel remain `NOT VERIFIED`.

### Correction static evidence

- Checkpoint hash: `44e4eaba2ca8e463343b44bc90069bfe011e230e`.
- GUI: `NOT RUN` by explicit instruction.
- ARM64 host: `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe`.
- Command: `MSBuild.exe InkeysRepo.sln /t:Rebuild /m /p:Configuration=Debug /p:Platform=ARM64 /nologo`.
- Debug | ARM64 correction build: PASS, `0 errors`, `317 warnings`, elapsed `00:01:42.14`.
- Lint/type-check: no separate native linter or type-check target exists; the full C++20 module compile and link passed.
- Tests: no automated test project or non-GUI test executable is available for this task; `Inkeys.exe` was not launched.
- No new heap container, COM resource, D2D resource or TextLayout creation was added to a stable frame.
- Inactive stable state keeps shared/local activation-preview progress at zero, so the existing `fineDialOpacity` guard skips geometry, ticks and labels and the animation clock stops requesting frames.

### Correction acceptance matrix

`PASS` means the contract is directly established by static code or the completed build. Visual appearance and physical timing/feel remain `NOT VERIFIED` because the GUI was not run.

| # | Status | Static evidence |
| ---: | --- | --- |
| 1 | PASS | Label layout-box center and major `DrawLine` both use the same projected `tickX`. |
| 2 | PASS | Label origin is calculated inside the same projected-tick iteration for every Dial angle. |
| 3 | PASS | Existing 64-entry TextLayout cache remains; only cached layout width metadata was added. |
| 4 | NOT VERIFIED | Dwell state path is present; real pointer reliability needs GUI input. |
| 5 | PASS | Ongoing dwell calls `IsBarThicknessFineDialDwellZone`, which does not inspect Popup Surface. |
| 6 | PASS | Fresh hit-test still rejects the actual Popup Surface rectangle. |
| 7 | PASS | Dwell latches only after signed outward displacement reaches the 5 DIP threshold. |
| 8 | PASS | After latching, Y monotonicity is not rechecked; only Click Zone containment remains. |
| 9 | PASS | X is compared to one fixed dwell anchor and restarts when displacement exceeds 5 DIP. |
| 10 | PASS | `BarThicknessFineDialActivationDwellMs == 1000`. |
| 11 | NOT VERIFIED | Independent progress publishes `0 -> 0.5`; rendered smoothness needs GUI. |
| 12 | PASS | Dwell never changes `ThicknessViewMode::Slider`; only formal activation does. |
| 13 | PASS | Slider Track target still depends only on Slider ViewMode. |
| 14 | PASS | Thumb visibility still depends only on Slider ViewMode/progress. |
| 15 | PASS | Popup visibility remains driven by Slider Thumb or formal FineDial mode. |
| 16 | PASS | Popup external interpolation still reads full FineDial progress only. |
| 17 | PASS | Tracking dwell resets Hold and short-circuits ordinary Hold updates. |
| 18 | NOT VERIFIED | Renderer-local 0.18 s fade-out is present; appearance needs GUI. |
| 19 | NOT VERIFIED | A one-shot shared handoff seeds renderer-local opacity at `0.5` even if the prior preview frame was not consumed; visible continuity still needs GUI. |
| 20 | NOT VERIFIED | Activation re-anchors current candidate/current X; visible no-jump needs GUI. |
| 21 | PASS | Direct pointer mapping and release velocity conversion both reverse the sign. |
| 22 | NOT VERIFIED | Math moves ticks right for rightward pointer drag; visual result needs GUI. |
| 23 | NOT VERIFIED | Math moves ticks left for leftward pointer drag; visual result needs GUI. |
| 24 | PASS | Sampled screen velocity uses the same negative screen-to-value sign as direct drag. |
| 25 | PASS | Residual multi-swipe combination remains in value-velocity space. |
| 26 | PASS | Boundary and spring equations are unchanged and consume corrected value velocity. |
| 27 | PASS | Drag Zone Down only sets `fineDragActivationArmed`. |
| 28 | PASS | Armed Down+Up restores prior ViewMode/hover/pinned and clears candidate state. |
| 29 | PASS | Armed Down bypasses `ProjectWidthFromScreenX` and candidate activation. |
| 30 | PASS | Activation requires horizontal displacement greater than the existing 5 DIP slop. |
| 31 | PASS | Slop-crossing frame activates with current value and current pointer X as anchor. |
| 32 | PASS | Click Zone is classified into `fineDialGesture` before initial rendering update. |
| 33 | PASS | Old Click release-validation branch and state were removed. |
| 34 | PASS | Subsequent held moves immediately use `ApplyFineDialScreenX`. |
| 35 | PASS | Normal release keeps FineDial ViewMode and ends the FineDial drag chain. |
| 36 | PASS | Click Zone path is excluded from the ordinary Slider projection block. |
| 37 | PASS | Click activation starts from `GetPenWidth`/current FineDial snapshot, not pointer projection. |
| 38 | PASS | `directTouchPreviewGesture` remains separately classified and excluded from activation dwell. |
| 39 | PASS | FineDial drag still uses the existing Hold state machine after candidate movement. |
| 40 | PASS | Preset/pen-type programmatic animation paths and render value source are unchanged. |
| 41 | NOT VERIFIED | Ordinary Slider/Preview branches remain, but GUI regression testing was not run. |
| 42 | NOT VERIFIED | Expand/collapse geometry path is unchanged, but GUI regression testing was not run. |
| 43 | PASS | Zero full/preview opacity skips Dial projection/ticks; completed fade no longer wakes rendering. |
| 44 | PASS | `git diff --check` is required and recorded after final EOL restoration. |
| 45 | PASS | ARM64-host full `Debug|ARM64` Solution Rebuild completed with `0 errors` and `317 warnings`. |

Correction summary: `36 PASS`, `0 FAIL`, `9 NOT VERIFIED`.

## Second GUI correction after `b272d4c`

### Implemented correction

- Added `ResolveThicknessFineDialUnitTravel(trackTravel, dpiZoom)`. It resolves the current Brush range and returns `trackTravel * BarThicknessPreviewTouchDragTravelScale / brushRangeSpan`; interaction passes screen track travel and rendering passes logical track travel, while each active pen mode keeps its own min/max.
- Removed the Click guard gap. `clickNear == dragFar`, and the Drag/Click union is an explicit activation corridor. Corridor points that cannot activate, including Popup exclusion and transient Thumb animation, return `Consumed` and never reach `ProjectWidthFromScreenX(...)`.
- Removed the Slider-dwell X-stability anchor and explicit outward-displacement latch. The timer now depends only on an ordinary Slider drag continuously remaining in the Click/dwell zone for `1000 ms`; leaving the zone resets dwell and restores the dark recognition preview.
- Split recognition visibility, dwell completion, formal-selection UI, and geometry transition. Recognition contributes the base `0.5`; dwell contributes the remaining `0.5`; cancellation retargets through existing `BarUiValueClass` animation. Major labels, center line, and selector triangles are gated by formal-selection progress.
- Added a one-shot normal-Preview Popup latch request. Rendering captures the actual rendered Popup center only for a triangle-confirmed FineDial -> Preview transition while the drawing panel remains open and the main bar is not folded. During that exit, surface, circle, and text scale/fade around the captured center; rapid show retargets from the latched geometry.
- Drawing-attribute collapse, main-bar fold, availability/capture loss, and other lifecycle cleanup never request the latch. If collapse/fold starts after a normal exit already latched, rendering releases the latch and restores the original Slider-target chase geometry.

### Second-correction verification

- Checkpoint before this correction: `b272d4c922cd792c0d68e9fc52d43f35120cc9d3` (`fix(ui3): refine thickness fine dial activation`).
- GUI: `NOT RUN` by explicit instruction; all visual appearance, physical direction/feel, and real pointer-timing criteria remain `NOT VERIFIED`.
- Product scope: `Bar.Main.cpp` and `Bar.State.cppm`; `Bar.Main.cppm` and unrelated product files are unchanged in this correction.
- Task-document scope: `prd.md`, `design.md`, and this `implement.md`; old task `08-07` is unchanged.
- Lint/type-check: there is no separate native lint/type-check target; the full C++20 module Solution rebuild is the applicable gate.
- Automated tests: no non-GUI test executable covers UI3 FineDial; `Inkeys.exe` was not launched.
- Final encoding, Git, and ARM64 Rebuild evidence is recorded after the matrix below.

### Second-correction review finding (fixed)

- File: `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp`.
- Issue: a triangle press could outlive drawing-panel collapse, main-bar fold, or thickness availability loss. The synthetic/up event then still committed the FineDial candidate and published the Popup exit-latch request after hiding had already started, leaving a stale request for a later session.
- Fix: triangle release now reconfirms Pen Mode, current range support, expanded/unfolded state, and current FineDial ViewMode before committing or requesting the latch. An invalid lifecycle release clears the request and does not commit.
- Findings not fixed: none from static review. GUI-only visual continuity and input feel remain `NOT VERIFIED`.

### Second-correction acceptance matrix

`PASS` means static control/data-flow evidence or an executed non-GUI gate proves the criterion. GUI-only appearance, feel, and real-input timing are deliberately `NOT VERIFIED`.

| # | Status | Evidence |
| ---: | --- | --- |
| 1 | PASS | Checkpoint `b272d4c922cd792c0d68e9fc52d43f35120cc9d3` exists before the current working-tree correction. |
| 2 | NOT VERIFIED | Normal FineDial Popup exit latches the rendered center; the visible in-place result requires GUI observation. |
| 3 | PASS | During hide, the latched branch uses the captured rendered center with zero retarget progress instead of the moving Slider endpoint. |
| 4 | PASS | For Brush, the canonical helper reduces to the prior current-range 3x formula. |
| 5 | PASS | Renderer tick spacing always divides by the Brush range span, including Highlighter mode. |
| 6 | PASS | Interaction pointer delta always divides by the same Brush-canonical unit travel. |
| 7 | PASS | Candidate clamp and visible tick interval still read the current pen mode range. |
| 8 | PASS | Rendering and interaction call the same `ResolveThicknessFineDialUnitTravel(...)` helper. |
| 9 | PASS | No dwell X anchor/tolerance comparison remains. |
| 10 | PASS | No explicit outward-displacement gate remains in dwell activation. |
| 11 | PASS | While the pointer stays in the dwell zone, elapsed time accumulates regardless of X/Y motion. |
| 12 | PASS | Leaving the dwell zone calls `ResetFineActivationDwell`; release/cancel/lifecycle ends recognition. |
| 13 | NOT VERIFIED | Recognition targets a `0.5` base Dial/tick preview; its rendered appearance requires GUI observation. |
| 14 | NOT VERIFIED | Recognition geometry uses the activation Click Zone vertical center and mirrored side; placement requires GUI observation. |
| 15 | NOT VERIFIED | Dwell contributes `0.5 -> 1.0` over `1000 ms`; animation appearance requires GUI observation. |
| 16 | NOT VERIFIED | Dwell cancellation retargets to dark recognition through `SetTar`; visible continuity requires GUI observation. |
| 17 | NOT VERIFIED | Ordinary cancellation avoids `SetDirect(0)` in renderer-local values; absence of a visible opacity jump requires GUI observation. |
| 18 | PASS | Major-label drawing and cache lookup are gated by formal-selection progress. |
| 19 | PASS | Center line and both selector triangles are gated by formal-selection progress. |
| 20 | NOT VERIFIED | Selector/label short entrance animation is present; natural timing requires GUI observation. |
| 21 | NOT VERIFIED | Recognition center interpolates to final FineDial geometry; visible non-teleport behavior requires GUI observation. |
| 22 | PASS | `clickNear == dragFar`; the 8 DIP Click guard constant and gap are removed. |
| 23 | PASS | The former gap belongs to the activation corridor and cannot enter ordinary Slider adjustment. |
| 24 | PASS | Any corridor hit is Drag, Click, or `Consumed`; none reaches `ProjectWidthFromScreenX(...)`. |
| 25 | PASS | Popup exclusion returns `Consumed`, not `None`. |
| 26 | PASS | Pointer Down on the real Slider track outside the corridor retains existing click-to-jump projection. |
| 27 | PASS | Drag Zone Down still arms and waits for the existing horizontal slop before zero-jump activation. |
| 28 | PASS | Click Zone Down still enters FineDial immediately and can continue dragging. |
| 29 | NOT VERIFIED | Direct drag sign code is unchanged from the accepted checkpoint; real GUI direction remains unrun. |
| 30 | NOT VERIFIED | Inertia consumes the same value-velocity sign; visible direction remains unrun. |
| 31 | NOT VERIFIED | Hold state machine remains wired and suppressed only during activation dwell; real timing/locking remains unrun. |
| 32 | PASS | Preset/pen-type cancellation and programmatic animation paths are unchanged by this correction. |
| 33 | NOT VERIFIED | Geometry continues to use `previewSide` and animated panel values; upper/lower GUI layouts remain unrun. |
| 34 | PASS | Zero formal/recognition/dwell opacity skips projection, ticks, labels, and related render work. |
| 35 | PASS | Stable active frames reuse cached label layouts/selector geometry and add no per-frame DWrite/D2D creation. |
| 36 | PASS | Final `git diff --check` passes after CRLF restoration. |
| 37 | PASS | ARM64-host full `Debug|ARM64` Solution Rebuild passes. |

Second-correction summary: `24 PASS`, `0 FAIL`, `13 NOT VERIFIED`.

### Final second-correction gate evidence

- `git diff --check`: PASS.
- Encoding: product files are UTF-8 without BOM and contain CRLF only.
- ARM64 host: `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe`.
- Command: `MSBuild.exe InkeysRepo.sln /t:Rebuild /m /p:Configuration=Debug /p:Platform=ARM64 /nologo`.
- Rebuild result: PASS, `0 errors`, `317 warnings`, elapsed `00:01:46.74`.
- Task remains `in_progress`; current correction is not staged or committed.
