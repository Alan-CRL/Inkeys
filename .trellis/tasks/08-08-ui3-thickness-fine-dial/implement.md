# Implementation Record

## Task state

- Task: `08-08-ui3-thickness-fine-dial`
- Final status for this session: `in_progress`
- Base feature checkpoint: `44e4eaba2ca8e463343b44bc90069bfe011e230e` (`feat(ui3): add thickness fine dial`).
- First GUI-correction checkpoint: `b272d4c922cd792c0d68e9fc52d43f35120cc9d3` (`fix(ui3): refine thickness fine dial activation`).
- Second GUI-correction checkpoint: `21dbd22607627d603018d1401be494160e75b0b9` (`fix(ui3): refine fine dial recognition and popup exit`). The worktree was clean before the third correction, so no empty checkpoint was created.
- The user authorized one separate final commit for the third correction after verification; do not amend `21dbd226`, push, archive, or launch/manipulate the GUI.
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
- Task remained `in_progress`; the correction was subsequently committed as checkpoint `21dbd22607627d603018d1401be494160e75b0b9` before the next round.

## Third GUI correction after `21dbd226`

### Pre-implementation audit

- Baseline: clean `feature/animation` worktree at `21dbd22607627d603018d1401be494160e75b0b9`.
- Hit ownership: the old Drag gap is `3 DIP`, Click depth stops after `18 DIP`, `activationCorridor` begins after the gap, and `ThicknessSliderHit` covers the full Preview. Therefore the gap and space beyond `clickFar` can return `None` and reach ordinary Slider projection.
- Slider preview: `thicknessSliderDragging` may be set on track press, while recognition visibility is only published inside `UpdateFineActivationDwell`; there is no distinct real-drag preview lifecycle or stable preview-value anchor.
- Visual layering: labels/selectors are formal-only, but major tick length is currently applied before activation and the tick lattice always reads the live candidate/display value.
- Endpoints/collision: major is only `tick % 5 == 0`; cached layouts expose `layoutWidth`, so fixed-array projected bounds can resolve endpoint priority without new allocations.
- Slider exit: expanded morph targets zero in the same frame as Thumb opacity. It must remain at one until Thumb opacity is about `<= 0.04`, then reverse through `SetTar`.
- Popup fit: measured-fit logic uses `BarThicknessPreviewNumberInset = 4.0`; a small inset/bias adjustment is the scoped mechanism for the `49/50` target.
- Frame zoom: the rendering loop and `BarUIRendering` helpers reread live `barStyle.zoom` across layout, lighting, masks, controls, Popup, FineDial and dirty bounds. A validated per-iteration snapshot is required through the final present/dirty calculation.
- Geometry color: `logoInk` is Pen-only; lighting updates color only in Pen and uses a Pen-only/non-penetrate condition. Geometry must read persistent `stateMode.Pen.Brush1.color`, and actual mode identity must trigger the existing fade transition even when Pen and Shape both use pen-colored lighting.

### Ordered implementation plan

- [x] Reshape FineDial ownership geometry to `Slider edge -> 5 DIP consumed blank -> 12 DIP Drag -> Click through panel edge`, preserving Popup/transient consume and mirrored directions.
- [x] Separate real Slider-drag preview visibility from dwell, latch a stable preview visual anchor on first horizontal movement, and use an independent about `0.30 s` fade-out.
- [x] Gate preactivation to short ticks/envelope only; interpolate anchor to live visual value on formal activation and add formal long-tick/label/selector entrances.
- [x] Promote both range endpoints to major ticks and suppress colliding ordinary labels with a fixed `<=64` projected-bounds array, endpoint-first.
- [x] Gate Slider morph exit on Thumb opacity `<=0.04` while preserving FineDial direct exit and reversible `SetTar` behavior.
- [x] Adjust the measured Popup fit inset/bias to target the `1.0x` `49 outside / 50 inside` boundary without a thickness literal branch.
- [x] Add a validated `frameZoom` snapshot at each rendering-loop iteration, route it through all rendering helpers and `RefreshBorderCursorVisibleRegions`, and leave input/live `dpiZoom` semantics unchanged.
- [x] Show persistent Brush1 color in Geometry `logoInk` and Primary light, independent of penetrate; track actual mode transitions for fade-out/recolor/fade-in.

### Verification plan

- [x] Static trace of press-only, first movement, dwell enter/leave, release-before-activation, formal handoff, rapid reversal, min/max, lifecycle cancellation, and Slider/FineDial exit paths.
- [x] Search the rendering-loop body and `BarUIRendering` helpers to prove there is exactly one live `barStyle.zoom` read per loop iteration and no helper rereads it.
- [x] Confirm no per-frame vector/string/COM/TextLayout creation was introduced; label collision storage and visible tick count stay fixed at `64`.
- [x] Run `git diff --check`, `git diff --numstat`, scope review, UTF-8/no-BOM/CRLF checks, and verify task `08-07` remains unchanged.
- [x] Run all discoverable non-GUI ARM64 Debug tests, without launching `Inkeys.exe`.
- [x] Rebuild `InkeysRepo.sln` with ARM64-host `MSBuild.exe`, `Debug | ARM64`, timeout at least five minutes.
- [x] Record requirement 1-8 evidence and mark visual/feel items `NOT VERIFIED`; keep task `in_progress`, then create one separate final commit.

### Third-correction implementation record

1. Ownership geometry: `CalculateBarThicknessFineDialGeometry(...)` now derives the ownership near edge from the Slider Thumb outward edge, applies `BarThicknessFineDialBlankDepthDip = 5.0`, then a `12.0 DIP` Drag Zone, with the Click Zone and `ownershipCorridor` continuing to the animated panel edge. `HitTestBarThicknessFineDialFreshActivation(...)` returns `Consumed` for the blank band, Popup occlusion, incomplete Thumb animation, and every unclassified corridor point; the outer interaction dispatch checks this classification before ordinary Slider projection.
2. Slider-drag preview: shared state now has a separate `thicknessFineDialActivationPreviewVisualWidth` anchor. A press publishes no preview; first real horizontal Slider motion begins recognition from zero, latches the current candidate, and targets base opacity with `0.18 s` entry. `IsBarThicknessFineDialRelatedZone(...)` and `SyncFineActivationRecognitionRegion(...)` distinguish leaving the whole Slider/FineDial area from merely leaving the Click dwell zone: whole-area exit/release/cancel targets zero over `0.30 s`, while dwell-only exit retains the base preview; re-entry relatches the current candidate.
3. Visual layering: preactivation uses the fixed anchor and only short ticks plus envelope. Major length/weight, endpoint treatment, labels, center line, and selector geometry are multiplied by formal selection progress. Activation interpolates the fixed anchor to the live continuous FineDial value with `drawAttributeThicknessFineDialProgress`, preserving the existing min/max-clamped visible interval and `64`-tick cap.
4. Endpoint majors and labels: `major = tick % 5 == 0 || tick == range.min || tick == range.max`. Label candidates use cached DWrite layout widths and projected pixel bounds in fixed `array<..., 64>` storage. Endpoint bounds are accepted first; colliding ordinary multiple-of-five labels are skipped without moving ticks.
5. Slider exit: `BarThicknessSliderThumbMorphExitOpacity = 0.04`. While the Thumb is above that opacity, Slider morph and track targets remain at one; only after crossing the threshold do they retarget to Preview through reversible `SetTar`. FineDial -> Preview remains the direct path because its Thumb is already hidden.
6. Popup threshold: `BarThicknessPreviewNumberInset` changed from `4.0` to `5.0`; the decision remains `circleDiameter >= max(measured text width, measured text height) + inset * 2`, with no thickness literal branch. The requested `49 outside / 50 inside` appearance remains a GUI observation item.
7. Frame zoom: each `Rendering()` iteration reads and validates `barStyle.zoom` once into `frameZoom`, publishes it to `BarUIRendering::SetFrameZoom(...)`, and uses that snapshot for layout pixel conversion, predicted/current/final dirty bounds, Popup, FineDial, Slider, panels, debug drawing, lighting/masks, and `RefreshBorderCursorVisibleRegions(frameZoom)`. Rendering helpers no longer reread live zoom; input hit testing continues to read current input zoom and `dpiZoom` is unchanged.
8. Geometry color: Geometry `logoInk` and Primary light use persistent `stateMode.Pen.Brush1.color`; Geometry bypasses `penetrate.select` suppression and keeps the Geometry anchor. A private integer mode identity snapshot makes Pen/Shape/other mode changes restart the existing light fade-out -> recolor/anchor transition -> fade-in even when the two drawing modes use the same color.

### Third-correction review findings

- Fixed during `trellis-check`: the base recognition preview previously remained active after capture moved outside the entire Slider + FineDial related region. The new related-zone synchronization covers both mouse-move messages and the 8 ms no-message polling path, while preserving base preview for dwell-zone-only exits.
- Fixed during build: `StateModeSelectEnum` was not visible in the module interface where the private lighting identity field was first declared. The field now stores an internal integer identity and implementation assignments use explicit casts; no public API changed.
- No other static findings remain. GUI-only continuity, feel, dynamic-zoom appearance, and the exact Popup fit boundary remain `NOT VERIFIED`.

### Third-correction verification

- Modified product files: `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp`, `Bar.Main.cppm`, and `Bar.State.cppm` only.
- Modified task files: `prd.md`, `design.md`, and this `implement.md`; task `08-07` and unrelated source files remain unchanged.
- Static zoom search: within the `Rendering()` loop there is one live `barStyle.zoom` read at frame start; all later rendering coordinates and `BarUIRendering` helpers use `frameZoom`. Remaining live reads are outside the rendering loop in input/window-position paths.
- Stable-frame allocation review: tick and collision storage are fixed arrays capped at `64`; the preactivation path does not request label layouts; formal labels reuse the bounded cache; no new per-frame vector/string/COM/effect creation was introduced.
- Non-GUI tests: no test project or headless executable is present in `InkeysRepo.sln`; `Inkeys.exe` was not launched.
- First full Rebuild exposed the private module-interface type visibility issue (`4 errors`, `185 warnings`) and was used only as a diagnostic run.
- Final full Rebuild: PASS with ARM64-host MSBuild, `Debug | ARM64`, `0 errors`, `317 warnings`, elapsed `00:02:37.57`.
- Git/encoding: final `git diff --check` and scope checks pass; all six modified files are UTF-8 without BOM and CRLF-only after final normalization.
- GUI: not run by explicit instruction. Visual continuity, physical feel, upper/lower mirrored appearance, live zoom changes, and exact 1.0x `49 -> 50` threshold require manual observation.

### Third-correction acceptance matrix (66-74)

| # | Status | Evidence |
| ---: | --- | --- |
| 66 | NOT VERIFIED | Static state flow proves press-only publishes no preview and first real horizontal drag targets entry from zero; visible smoothness requires GUI. |
| 67 | PASS | Preactivation uses the latched anchor, short uniform ticks and envelope only; major length, endpoints, labels, center line and selectors are formal-progress gated. |
| 68 | NOT VERIFIED | Dwell/formal handoff preserves current local opacity/anchor, and release/whole-area exit targets zero over `0.30 s`; visible continuity and absence of residue require GUI. |
| 69 | PASS | Geometry and dispatch statically enforce `Slider edge -> 5 DIP consumed blank -> 12 DIP Drag -> Click/owned through panel edge`, including Popup/transient consume. |
| 70 | PASS | Endpoints are unique majors; fixed cached-layout bounds accept endpoints before ordinary labels and suppress collisions without moving ticks. |
| 71 | NOT VERIFIED | Morph remains targeted at one until Thumb opacity is `<= 0.04` and all targets are reversible `SetTar`; visible two-phase timing and rapid reversal require GUI. |
| 72 | NOT VERIFIED | Measured-fit inset changed from `4.0` to `5.0` without a literal thickness branch; the exact 1.0x `49 outside / 50 inside` boundary requires GUI measurement. |
| 73 | PASS | Static search proves one validated live zoom read per rendering iteration and snapshot use through helpers and final dirty calculation; full ARM64 build passes. |
| 74 | NOT VERIFIED | Static flow proves persistent Brush1 color, Geometry anchor, penetrate independence and mode-identity light fade; visible no-black/no-flicker behavior requires GUI. |

Third-correction supplement: `4 PASS`, `0 FAIL`, `5 NOT VERIFIED`. The task remains `in_progress` and must not be archived.

## Fourth GUI correction after `2bb90cd`

- Checkpoint: `2bb90cd060a135a05a6ba5e22e32c8743ee046f6` (`fix(ui3): refine fine dial preview and rendering`).
- GUI finding: the endpoint-first label collision filter alternated the surviving ordinary major labels as the projection moved, producing sequences such as `1/10/20/30` and `5/15/25` instead of showing every visible 5-multiple.
- Fix: retain the fixed `64`-entry candidate array and cached DWrite layouts, but remove bounds collision acceptance. The formal layer now draws every visible `tick % 5 == 0 || endpoint` label once; preactivation still creates no labels and edge fading/range clipping remain unchanged.
- Static verification confirms no endpoint/selected-value/collision branch filters label candidates. ARM64-host full `Debug | ARM64` Solution Rebuild: PASS, `0 errors`, `317 warnings`, elapsed `00:02:52.14`; Git/encoding checks pass. GUI was not launched, so continuous visibility of every 5-multiple and endpoint while the Dial moves remains `NOT VERIFIED` pending manual observation.

## Fifth GUI correction after `c290a385`

- GUI finding: the recognition preview started only after real horizontal Slider motion and was terminated when the captured pointer left the Slider/FineDial related geometry. Dwell also accumulated solely from zone residency, so movement inside the zone did not invalidate the timer.
- Required correction: an ordinary captured Slider press immediately latches the current visual value and animates the base recognition layer from `0` to `0.5`. That base layer remains active for the complete captured press regardless of pointer region, and ends only on release/cancel/capture loss/lifecycle cleanup or formal FineDial handoff.
- Dwell correction: entering the Click/dwell zone latches client X/Y. The existing `5 DIP` input slop, converted through the gesture DPI snapshot, is applied independently to both axes; exceeding it on either axis relatches the current point and restarts the `1000 ms` timer. Leaving the zone resets only dwell and retains the press-owned base preview. Hold remains reset and suppressed while dwell is valid.
- Scope: only `Bar.Main.cpp` plus this task's `prd.md`, `design.md`, and `implement.md`; direct-touch Preview, Drag/Click activation, candidate mapping, FineDial physics, and rendering resources are unchanged.
- Static verification: PASS. Ordinary Slider press publishes the base preview before entering the capture loop; pointer-region changes only reset dwell; release/cancel/capture loss/lifecycle cleanup end recognition; formal activation preserves the one-shot handoff. Direct-touch Preview and Drag/Click activation remain excluded.
- Stillness verification: PASS by control flow. Zone entry latches both client axes, either-axis displacement `> 5 DIP` relatches and resets the timer, displacement `<= 5 DIP` retains it, leaving the zone clears the anchor/timer, and Hold is suppressed during valid dwell.
- Git/encoding: PASS. `git diff --check` succeeds; scope is `Bar.Main.cpp` plus the three current task documents; task `08-07` is unchanged; all four modified files are UTF-8 without BOM and CRLF-only.
- Build: PASS. ARM64-host full `InkeysRepo.sln` Rebuild, `Debug | ARM64`, completed with `0 errors`, `317 warnings`, elapsed `00:01:49.61`.
- Tests/GUI: no headless test target covers this UI3 interaction and `Inkeys.exe` was not launched. Base-opacity continuity, real pointer stillness, and activation feel remain `NOT VERIFIED`.
