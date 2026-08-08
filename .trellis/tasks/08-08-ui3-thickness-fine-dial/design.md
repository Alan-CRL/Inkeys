# Technical Design

## Boundaries and ownership

- Product code remains in `Bar.Main.cpp`, `Bar.Main.cppm`, and `Bar.State.cppm`; no project-file, resource, public API, or shared animation-framework changes.
- `ThicknessViewMode` is the only cross-thread view target. `thicknessSliderPinned` remains only as Slider persistence/leave policy. Rendering owns visual transition progress; interaction owns gesture/physics phases.
- Shared atomics are limited to ViewMode, FineDial continuous visual value, candidate-active, dragging, and physics-active flags. The interaction thread owns anchors, range snapshot, raw position, velocity, fixed sample ring, residual velocity, phase, and timestamps.

## State machine

```text
Preview --hover/click--> Slider
Slider --drag-zone down / click-zone click / outward dwell--> FineDial
FineDial --pointer up / preset / supported pen switch--> FineDial
FineDial --valid expanded-triangle click--> Preview
Any --close/fold/unavailable/cancel/offSignal--> Preview + cleared interaction
```

- Hover may target Slider only while not pinned; leave returns to Preview. Pinned decides that policy but never replaces ViewMode as the visual source.
- Renderer keeps an expanded/morph progress at 1 for Slider or FineDial. Slider track/Thumb opacity targets 1 only for Slider; FineDial progress targets 1 only for FineDial. FineDial→Preview drives morph and dial exit together, so no Slider frame appears.
- Hit-testing checks target ViewMode, not animated opacity. FineDial remains drawable during exit but is immediately non-interactive.

## Geometry and activation

- `CalculateBarThicknessPreviewGeometry` remains the common source of `panelScale`, preview bounds, track bounds, and continuous `previewSide`. `outwardDirection = sign(previewSide)`; near-zero side disables activation.
- `CalculateBarThicknessFineDialGeometry` derives center axis, dial bounds, Drag Zone, Click Zone, and Popup target. Named values: drag gap 3 DIP, drag depth 12 DIP, click guard 8 DIP, click depth 18 DIP, Popup panel gap 8 DIP.
- Hit rectangles are logical UI coordinates scaled by `panelScale`; the existing `barStyle.zoom` conversion is used for client-message coordinates. Click classification reuses the 5 DIP two-dimensional slop and excludes the current Popup Surface hit rectangle while it is visually present.
- Outward dwell is eligible only for an ordinary Slider drag inside Click Zone. X movement beyond 5 DIP resets the 500 ms timer; leaving, release, cancel, or availability changes cancel it. While eligible, Hold state is reset and suppressed.
- On every activation, `anchorPointerX` is the current screen X and `anchorContinuousValue` is the current candidate/visual value. Hold becomes eligible only after a subsequent FineDial candidate change.

## Value and commit flow

```text
pointer/physics raw position
  -> exponential boundary projection
  -> continuous visual value (may overscroll)
  -> round(clamp(value, range))
  -> existing thicknessSliderCandidateWidth
  -> Popup / drawAttributePenThickness direct candidate display
  -> settle: one SetPenWidth(final, true)
```

- `unitTravelScreen = trackTravelScreenX * BarThicknessPreviewTouchDragTravelScale / rangeSpan`. FineDial drag and velocity conversion use this exact quantity; the value `3.0` is never duplicated.
- Renderer uses the continuous atomic only while candidate-active. Otherwise it uses `drawAttributePenThickness.val`, so preset and pen-type transitions rotate the Dial through the existing programmatic animation.
- Normal settle commits only when selection changed. Hold release commits the frozen candidate and never starts inertia. Abnormal/lifecycle cancellation clears without commit.
- A valid triangle exit pauses/stops physics, commits the current rounded/clamped candidate once, clears the chain, and targets Preview. Preset or supported pen-type actions clear without commit immediately before their existing programmatic update. Invalid press resumes from the same raw/visual state with a reset clock.

## Physics

- Local phase enum: `Idle`, `Dragging`, `Inertia`, `Settling`. The outer interaction loop uses blocking `getmessage_win32` unless physics is active; active physics uses `peekmessage_win32`, advances once per no-message iteration, sleeps 8 ms, and clamps `dt` to 32 ms.
- A six-entry fixed ring retains pointer samples from the latest 96 ms. Segment velocities use recency weights; screen velocity converts to value/s through `unitTravelScreen`.
- Below 80 DIP/s release starts Settling; otherwise Inertia. Speed is clamped to 900 DIP/s and decays by `exp(-10 * dt)`.
- Re-grab stores `residual = currentVelocity * exp(-6 * heldSeconds)`. Release combines sampled velocity plus `0.35 * residual`, naturally cancelling opposite directions, then clamps to the maximum.
- For raw overshoot `d`, visible overshoot is `L * (1 - exp(-d/L))`, where `L = 24 DIP / unitTravelScreen`; logical candidate always clamps to the valid range.
- Outside bounds, and later while snapping to the nearest integer, acceleration is `-omega² * error - 2 * dampingRatio * omega * velocity`, with `omega=18 rad/s` and ratio `1.05`. Settle completes below 0.15 DIP position error and 4 DIP/s velocity.

## Projection and rendering

- Logical center spacing is `(trackWidth - thumbDiameter * panelScale) * BarThicknessPreviewTouchDragTravelScale / rangeSpan`.
- With `thetaLimit=1.20`, `radius=(availableHalfWidth-edgeInset)/sin(thetaLimit)` and `angularStep=unitTravelLogical/radius`. Thus `dx/dValue` at `theta=0` equals the 3× unit travel exactly.
- For each visible integer: `theta=(tick-visual)*angularStep`, `x=centerX+radius*sin(theta)`, `depth=max(0,cos(theta))`, and `y=centerY-outwardDirection*(1-depth)*4 DIP*panelScale`. Depth controls length/scale/alpha; a smoothstep fade starts at 68% of the horizontal half-width.
- Compute `firstTick/lastTick` directly from `visual ± thetaLimit/angularStep`, clamp to range, and cap at 64. A fixed white center line/selector represents the continuous axis; integer ticks stay gray so rounding does not teleport the dial.
- Major labels are multiples of 5 and sit toward the control row (`-outwardDirection`) to avoid the external Popup. Label text/layout is cached by value and zoom. Two fixed-segment low-alpha envelope edges add cylinder depth without effects or heap allocation.
- Slider track/Thumb and Dial crossfade. Popup anchor interpolates from the existing safe-bound Thumb result to the selector X and an external Y computed from the animated panel edge; only the Slider endpoint applies `penTypeSafeRight`.
- Overflow target/hit state closes immediately on FineDial entry. Business overflow remains untouched and can recreate its UI only after FineDial exit progress reaches zero.

## Resource and performance contract

- Reuse the existing solid-brush and format caches. Lazily create one unit selector PathGeometry and cached DWrite label layouts; reset them from `DiscardDeviceResources`.
- Inactive fast path is a single visibility/physics check before any tick, projection, text, or FineDial geometry work. Physics settle disables polling and stops render requests.
- Active hot path uses fixed arrays/stack values only. No per-frame vector growth, `to_wstring`, text measurement/layout creation, brush/effect/geometry creation, or full-range iteration.

## Compatibility and rollback

- Unsupported pen modes continue to use `ThicknessSliderAvailable()` and close the complete thickness interaction. Brush↔Highlighter keeps FineDial and changes range before consuming the existing animated thickness value.
- Existing direct-touch Preview, Slider absolute projection, Hold timing, Popup normal safe bound, and SetPenWidth memory semantics remain unchanged outside FineDial branches.
- Rollback is confined to the three product files plus this task directory; no schema, migration, resource, or project-file rollback is needed.
