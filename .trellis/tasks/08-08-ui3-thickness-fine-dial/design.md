# Technical Design

## Boundaries and ownership

- Product code remains in `Bar.Main.cpp`, `Bar.Main.cppm`, and `Bar.State.cppm`; no project-file, resource, public API, or shared animation-framework changes.
- `ThicknessViewMode` is the only cross-thread view target. `thicknessSliderPinned` remains only as Slider persistence/leave policy. Rendering owns visual transition progress; interaction owns gesture/physics phases.
- Shared atomics are limited to ViewMode, FineDial continuous visual value, candidate-active/dragging/physics flags, Slider-drag preview visibility/anchor, dwell completion snapshots, and one normal-Preview Popup-exit latch request. The interaction thread owns gesture qualification, anchors, range snapshot, raw position, velocity, fixed sample ring, residual velocity, phase, and timestamps; the renderer owns the actual Popup center latch and all visual transition values.

## State machine

```text
Preview --hover/click--> Slider
Slider --drag-zone slop / click-zone down / region-only 1000ms dwell--> FineDial
FineDial --pointer up / preset / supported pen switch--> FineDial
FineDial --valid expanded-triangle click--> Preview
Any --close/fold/unavailable/cancel/offSignal--> Preview + cleared interaction
```

- Hover may target Slider only while not pinned; leave returns to Preview. Pinned decides that policy but never replaces ViewMode as the visual source.
- Renderer keeps an expanded/morph progress at 1 for Slider or FineDial. Slider track/Thumb opacity targets 1 only for Slider; FineDial progress targets 1 only for FineDial. FineDial→Preview drives morph and dial exit together, so no Slider frame appears.
- Slider-press preview does not change ViewMode and is separate from dwell recognition. Establishing an ordinary captured Slider press latches the current visual value and targets base Dial/tick opacity from `0` to about `0.5`; pointer movement and current-region ownership do not end it while capture remains held. Entering Click/dwell region starts the remaining `0.5` progress from a latched X/Y stillness anchor. Leaving Click/dwell region cancels only dwell and retains the base preview. Release/cancel/capture loss/lifecycle cleanup before activation targets both layers to `0` with an independent about `0.30 s` fade; formal activation performs the one-shot handoff. Labels, long ticks, center line, and selectors use a separate formal-selection animation.
- Hit-testing checks target ViewMode, not animated opacity. FineDial remains drawable during exit but is immediately non-interactive.

## Geometry and activation

- `CalculateBarThicknessPreviewGeometry` remains the common source of `panelScale`, preview bounds, track bounds, and continuous `previewSide`. `outwardDirection = sign(previewSide)`; near-zero side disables activation.
- `CalculateBarThicknessFineDialGeometry` derives center axis, dial bounds, Slider-side ownership corridor, `5 DIP` blank band, Drag Zone, and Click Zone. The ownership corridor begins at the Slider Thumb outward edge and extends to the panel edge; Drag starts after the scaled blank band, keeps its `12 DIP` depth, and Click owns all remaining outward space. `clickNear == dragFar`; a collapsed sub-zone remains owned and consumed.
- Hit rectangles are logical UI coordinates scaled by `panelScale`; client-message conversion uses current input zoom. Ownership is geometric rather than an ordering accident: points in the blank band, Popup occlusion, a not-yet-complete Thumb animation, or any unclassified point in the ownership corridor return `Consumed`, never ordinary Slider projection. Existing full Preview SliderHit may continue to support hover, but it cannot claim an owned Pointer Down.
- Dwell is eligible for an ordinary captured Slider press inside Click Zone. Entry latches client X/Y; both axes must remain within the existing `5 DIP` slop for `1000 ms`. If either axis exceeds that slop, the current point becomes the new anchor and the timer restarts. Leaving Click Zone resets only dwell completion and returns to the latched recognition dark state; it does not end the press-owned base preview. Release/cancel/capture loss or lifecycle end terminates the recognition session. While in-region, Hold state is reset and suppressed.
- Dwell tracking suppresses only Hold. Ordinary Slider projection, integer candidate publication, Thumb position, and Popup continue following pointer X throughout the recognition interval. On the completion frame, the current pointer X is projected before `ActivateFineDialDrag(...)`, then FineDial re-anchors from that exact candidate/X pair.
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

- `ResolveThicknessFineDialUnitTravel(trackTravel, dpiZoom)` obtains the current Brush range via `GetBarThicknessSliderRange(IdtPenBrush1, dpiZoom)` and returns `trackTravel * BarThicknessPreviewTouchDragTravelScale / brushRangeSpan`. Interaction passes screen track travel; rendering passes logical track travel. Drag, velocity, inertia thresholds, rubber-band/spring DIP conversion, and projection therefore share one Brush-canonical unit while current pen min/max remain independent. Ordinary Slider and direct-touch Preview keep their current-range mapping.
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

- Logical center spacing is `ResolveThicknessFineDialUnitTravel(trackWidth - thumbDiameter * panelScale, dpiZoom)`, whose denominator is always the current Brush range span rather than the current pen range.
- With `thetaLimit=1.20`, `radius=(availableHalfWidth-edgeInset)/sin(thetaLimit)` and `angularStep=unitTravelLogical/radius`. Thus `dx/dValue` at `theta=0` equals the 3× unit travel exactly.
- For each visible integer: `theta=(tick-visual)*angularStep`, `x=centerX+radius*sin(theta)`, `depth=max(0,cos(theta))`, and `y=centerY-outwardDirection*(1-depth)*4 DIP*panelScale`. Depth controls length/scale/alpha; a smoothstep fade starts at 68% of the horizontal half-width.
- Compute `firstTick/lastTick` directly from `visual ± thetaLimit/angularStep`, clamp to range, and cap at 64. A fixed white center line/selector represents the continuous axis; integer ticks stay gray so rounding does not teleport the dial.
- Recognition preview computes only envelope and uniform short tick lines at the activation-region center using the latched Slider-drag preview value. It never follows the live Slider candidate. Major status is not applied until formal-selection progress begins; formal visual value interpolates from the latched preview anchor to the live FineDial visual value, so the tick lattice does not jump at handoff.
- Formal major status is `multiple-of-five || range.min || range.max`. Projected label candidates are recorded in a fixed stack array and all visible major candidates are drawn from their cached layouts; no collision or selected-value filter may alternate which 5-multiple labels remain visible. Ticks stay at their projected positions and the loop stays capped at `64`.
- Slider track/Thumb and Dial crossfade only after formal activation. Popup anchor interpolates from the existing safe-bound Thumb result to the selector X and an external Y computed from the animated panel edge; only the Slider endpoint applies `penTypeSafeRight`.
- Slider -> Preview is explicitly staged: while Thumb opacity is above approximately `0.04`, expanded morph remains targeted at `1`; after the Thumb crosses the threshold, morph retargets to `0`. FineDial -> Preview is unaffected because its Thumb is already hidden. Every transition uses `SetTar`, so a re-entry reverses from the current value.
- Popup number fit keeps measured DWrite width/height and circle diameter as the source of truth; only the inset/bias is adjusted so the `1.0x` observed boundary moves from about `47/48` to about `49/50`.
- A normal triangle-confirmed FineDial→Preview transition publishes a one-shot latch request. While the panel remains expanded and the main bar is not folded, rendering captures the actual rendered Popup center and scales surface/circle/text around that center until hidden. A rapid show reverses through a local retarget progress from the latched geometry. Panel collapse, fold, availability loss, capture loss, and other lifecycle cleanup clear or bypass the request; if collapse starts during a latch, rendering releases it and restores the original chase geometry.
- Overflow target/hit state closes immediately on FineDial entry. Business overflow remains untouched and can recreate its UI only after FineDial exit progress reaches zero.

## Resource and performance contract

- Reuse the existing solid-brush and format caches. Lazily create one unit selector PathGeometry and cached DWrite label layouts; reset them from `DiscardDeviceResources`.
- Inactive fast path is a single composite opacity/physics check before any tick, projection, text, or FineDial geometry work. Recognition/dwell/formal-selection animations request frames only while their local `BarUiValueClass` is moving; completed cancellation returns to zero and stops rendering. Physics settle disables polling and stops render requests.
- Active hot path uses fixed arrays/stack values only. No per-frame vector growth, `to_wstring`, text measurement/layout creation, brush/effect/geometry creation, or full-range iteration.

## Frame zoom snapshot

- At the top of every rendering-loop iteration, read `barStyle.zoom` once, validate it to a positive finite fallback, store it in local `frameZoom`, and publish the same value to `BarUIRendering`.
- Every operation in that iteration, including UI layout-derived pixel coordinates, Popup transforms, predicted bounds, `BeginDraw` content, debug bounds, final dirty rect, and `RefreshBorderCursorVisibleRegions`, uses local `frameZoom`.
- `PrepareFrameLighting`, diffuse-mask lookup, `DrawPointLightFrame`, `Shape`, `Superellipse`, `Svg`, `Png`, and `Word` read only the renderer snapshot. Interaction hit-testing remains outside this contract and may read the latest live zoom. `dpiZoom` is unchanged.

## Geometry pen-color ownership

- `stateMode.Pen.Brush1.color` is the persistent Geometry color source. `logoInk` uses current `GetPenColor()` in Pen mode and Brush1 color in Shape mode; it is hidden in other modes.
- Lighting uses current Pen color for Pen and Brush1 color for Shape. `frameDrawingUsesPenColor` is true for Shape regardless of `penetrate.select`, while Pen keeps the existing non-penetrate condition.
- Track mode identity for current state, but start the opacity/color-role transition only when `desiredPenColorBlend` changes. Pen <-> Shape with the same target color keeps full light opacity and lets the independent primary-anchor interpolation move Draw <-> Geometry. Pen/Shape <-> non-pen-colored modes still use the established fade-out, recolor, fade-in transition; direct pen-color changes keep their existing smooth color interpolation.
- At the start of each rendering iteration, snapshot StateMode, PenMode, Brush1 color, Highlighter color, and penetrate state once for the drawing-logo/light consumers. Both `logoInk` and `PrepareFrameLighting` consume that same snapshot, so one frame cannot mix a Shape identity with a Highlighter lookup.
- Treat Brush1 and Highlighter as separate visual color sources. A source handoff into Shape synchronizes to the Brush1 snapshot instead of continuing the Highlighter color interpolator; a Brush1 Pen <-> Shape handoff keeps the same source and therefore preserves the no-fade anchor-only transition.

## FineDial pen-range transition

- Rendering owns a local `Idle / RevealNewRange / MoveValue / RetireOldRange` phase and two reversible range-membership opacity values. It starts only while FineDial is targeted/visible, no candidate is active, the supported pen range changes, and the current continuous display value lies outside the new range.
- On start, stop the current programmatic thickness animation at its visual value, snapshot the old and new ranges, and render their union. Tick opacity is `max(oldMembership * oldOpacity, newMembership * newOpacity)`: overlapping ticks never blink, new-only ticks fade in first, and old-only ticks remain available for the subsequent travel.
- After new membership reaches full opacity, retarget the existing `drawAttributePenThickness` value to the new pen's real thickness. Popup, selector projection, and the published takeover visual value continue to read this same animation, so they move together. Once it arrives, old-only membership fades to zero and rendering returns to the current range.
- The projected visible interval is clamped to the union only for rendering and remains capped at `64`; current-range endpoints and still-visible old endpoints are majors. Candidate clamp, physics, Slider normalization, and `SetPenWidth` continue using the new current range.
- A shared render-owned transition-active flag only prevents a FineDial Pointer Down from starting on visual-only old ticks. FineDial exit, fold/close/availability loss, or unsupported mode clears it. A later supported pen switch restarts from the current visual value without restoring stale candidate/physics state.

## Compatibility and rollback

- Unsupported pen modes continue to use `ThicknessSliderAvailable()` and close the complete thickness interaction. Brush↔Highlighter keeps FineDial and changes its own min/max before consuming the existing animated thickness value; canonical unit spacing continues to resolve from Brush for both modes.
- Existing direct-touch Preview, Slider absolute projection, Hold timing, Popup normal safe bound, and SetPenWidth memory semantics remain unchanged outside FineDial branches.
- Rollback is confined to the three product files plus this task directory; no schema, migration, resource, or project-file rollback is needed.
