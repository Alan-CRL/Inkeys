# Research: desktop annotation bottom-dock background is invisible

- Query: Re-trace the desktop screen-annotation path for a completely invisible bottom-dock rounded background, including frame-state sampling, target latching, animation advancement, viewport/dirty geometry, offscreen D2D bitmap lifecycle, composition transforms, and the old/new implementation boundary.
- Scope: internal
- Date: 2026-08-21

## Findings

### Root cause with highest confidence: `Capturing` is an input-sample pulse, but visibility latches only if a render frame samples that pulse

The background target is not latched from the durable `Floating -> BottomDocked` mode transition. It is latched only when the render thread observes that transition while the current phase is still exactly `Capturing`:

- `Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h:55-62` defines entry capture as `previousMode == Floating && currentMode == BottomDocked && currentPhase == Capturing`.
- `Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h:82-90` returns true on that exact tuple; otherwise it can remain true only if the prior `captureLatched` value is already true.
- `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp:7044-7054` takes `previousFrameMode` from the last render-frame state, then advances `state.bottomDockFrameMode` to the current snapshot. The local old value is valid for the current call, so update ordering inside this block is not itself wrong.
- `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp:7124-7126` passes the render-sampled phase and the previous target latch to the helper. If the render snapshot is `{previous=Floating, current=BottomDocked, phase=Dragging, latch=false}`, the result is false and there is no later condition that can create the latch.

The input producer makes that missed tuple highly likely:

- `Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h:373-383` emits `BottomDocked + Capturing` for the first in-band input sample.
- On the very next tracker update, the already-docked branch unconditionally assigns `Dragging` unless it detaches (`Bar.BottomDock.h:397-410`). Thus `Capturing` is not a persistent state awaiting presentation; it lasts only until the next input poll.
- `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp:6009-6038` publishes every update directly into the same atomic mode/phase slots. A real mode change advances `bottomDockTransitionSerial`, but the following `Capturing -> Dragging` phase-only change does not advance the serial.
- The capture update calls `UpdateRendering(false)` because `modeChanged` makes `visualChanged` true (`Bar.Interaction.cpp:6212-6276`). This is only an asynchronous wake.
- The mouse drag loop sleeps 15 ms only when `ApplyAbsolutePointer` returns false (`Bar.Interaction.cpp:6311-6323`). A capture returns true because it is a visual/mode change, so the loop immediately polls again and can overwrite `Capturing` with `Dragging` before the woken render thread snapshots the atomics.
- The presentation barrier at `Bar.Interaction.cpp:6204-6210` defers direct HWND moves but does not block later input updates or preserve the capture phase. The render snapshot merely reads the latest stable even serial and latest phase (`Bar.RenderLoop.cpp:12106-12128`); because the phase-only overwrite leaves the serial unchanged, `{BottomDocked, Dragging}` is a valid snapshot for the capture serial.

This explains the desktop-annotation symptom without invoking the whiteboard gate. It also explains why the foreground indicator may work while the background never appears: `ResolveBarBottomDockIndicatorTarget` only requires `BottomDocked && dragActive` (`Bar.BottomDock.h:93-97`), while the background requires the missed `Capturing` pulse to seed its latch.

The failure is scheduling-dependent in the formal sense, but the polling structure makes it close to deterministic in normal mouse dragging: the producer performs the phase overwrite immediately after the capture-changing iteration, while the render consumer must wake and complete its snapshot in the small interval between those two producer iterations. Confidence: high (about 85-90%).

### The latch is not "true for only one frame" once it is successfully seeded

- If a render frame does sample `Floating -> BottomDocked + Capturing`, `ResolveBarBottomDockBackgroundTarget` returns true and `ApplyFeedbackTarget` stores `state.bottomDockBackgroundTarget = true` at `Bar.RenderLoop.cpp:7134-7157`.
- Subsequent docked frames retain it while either drag or spring feedback is active (`Bar.BottomDock.h:89-90`). Active dragging alone is sufficient, so a successfully seeded latch does not collapse on the next `Dragging` frame.
- It fades out only after drag and rebound are both inactive, or after mode becomes floating. Therefore the actual defect is "never seeded when the transient phase is missed," not "seeded for one frame and immediately cleared."

### `SetTar` and `ChangeValue` do advance the animation in the same render pass

- `Bar.RenderLoop.cpp:7134-7154` changes the target only on target-edge transitions, then `Bar.RenderLoop.cpp:7160-7165` immediately invokes `ChangeValue` when `val != tar`.
- `ChangeValue` delegates to `BarUiAdvanceAnimation` (`Bar.RenderLoop.cpp:5142-5162`). `Bar.Animation.cpp:116-175` advances by this frame's `dt`, writes `value.val`, and reports the animation active. A non-positive `dt` finishes at the target (`Bar.Animation.cpp:127-135`) rather than leaving it at zero.
- The first correctly latched frame therefore has positive background progress (or directly reaches 1 when animation is disabled), marks the feedback dirty key, and requests continuation. No call-order defect was found here.

### Geometry, capacity, viewport source, and dirty clip are consistent when progress is nonzero

- Geometry unions the main button and main bar and expands left/right/top by 10 DIP (`Bar.BottomDock.h:99-130`); the call site supplies current layout-space DIP bounds and the dock mapping base bottom (`Bar.RenderLoop.cpp:7445-7467`).
- Visibility is based on progress above `0.000001`; visible feedback bounds are converted once from DIP to layout pixels (`Bar.RenderLoop.cpp:7498-7528`). These bounds are added to cached visible content (`Bar.RenderLoop.cpp:7870-7885`) and observed under the feedback dirty key (`Bar.RenderLoop.cpp:8322-8328`).
- The capacity/viewport logic uses the same layout coordinate space. The source is `candidateViewport - capacityOrigin` (`Bar.RenderLoop.cpp:8829-8842`), tuple changes force full damage/replacement (`Bar.RenderLoop.cpp:8843-8868`), and the D2D base transform is exactly `-capacityOrigin` (`Bar.RenderLoop.cpp:8902-8905`).
- The dirty clip is pushed after setting that same base transform (`Bar.RenderLoop.cpp:9026-9028`; `Bar.Rendering.cpp:181-198`). The background destination is layout-pixel geometry multiplied by zoom exactly once, then receives only the base translation (`Bar.RenderLoop.cpp:9041-9053`). No double zoom or mismatched source/client coordinate conversion was found.
- On presentation, `pptSrc` is the candidate source, `psize` is the candidate viewport size, and tuple changes use `prcDirty = nullptr` (`Bar.RenderLoop.cpp:11757-11809`). These fields are committed only after the full D2D/GDI/ULW transaction succeeds (`Bar.RenderLoop.cpp:11818-11850`).

### Offscreen D2D bitmap creation and composition are statically valid; failures are currently under-diagnosed

- The main target is a BGRA premultiplied, `TARGET | GDI_COMPATIBLE` bitmap (`Bar.Rendering.cpp:77-111`).
- The decoration bitmap is BGRA premultiplied with `D2D1_BITMAP_OPTIONS_TARGET` and does not set `D2D1_BITMAP_OPTIONS_CANNOT_DRAW`, so it remains usable as a `DrawBitmap` source (`Bar.RenderLoop.cpp:8980-8987`).
- The code captures the main target, switches to the cache target before `BeginDraw`, clears transparent, fills blue at alpha 0.18, ends the cache draw, restores the main target, and then begins the main draw (`Bar.RenderLoop.cpp:8977-9027`). This ordering is valid; there is no overlapping draw session and no missing target restore on the shown path.
- Cache invalidation covers device generation, pixel dimensions, zoom/style, main target-size changes, and present/device-loss recovery (`Bar.RenderLoop.cpp:8963-9024`, `8401-8407`, `11954-11960`).
- `DrawBitmap` maps the whole cache to a destination with the same physical pixel extent and applies animation progress as the additional opacity (`Bar.RenderLoop.cpp:9041-9053`). This is expected premultiplied-alpha composition, not an alpha-zero path.

No bitmap/clip issue was found that deterministically makes a positive-progress background fully transparent. The remaining D2D risk is operational: `CreateBitmap`, brush creation, or cache `EndDraw` failure resets/skips the bitmap without a local log (`Bar.RenderLoop.cpp:8984-9013`). That could cause invisibility on a specific device, but current static evidence does not make it more likely than the proven lifecycle race. Estimated probability as the primary cause: below 10% unless diagnostics show positive progress with a null cache.

### Old/new implementation comparison available without repository history

- The task PRD states that the old fixed capture hint was drawn directly into the main target and could be cleared/covered by later Bar drawing (`.trellis/tasks/08-20-ui3-bar-bottom-dock-feedback/prd.md:9-12`).
- The design replaces it with a cached premultiplied bitmap composed before Bar content (`design.md:18-31`), which is what the current code implements.
- The new composition order means opaque Bar content intentionally covers the center of the background; normally only the 10 DIP outer margins and uncovered regions are visible. At 18% peak opacity this is subtle, but it cannot explain a complete absence once progress is 1.
- Exact commit-diff attribution was not inspected because the Trellis research role forbids all git operations. The main session should compare the introducing commit specifically around the input publication/lifecycle helper, not only the D2D cache block.

### Existing tests miss the producer/consumer sampling failure

- `InkeysHeadlessTests/bar_bottom_dock_tests.cpp:167-206` tests the pure helper with an explicitly supplied `Capturing` frame and then an already-true latch.
- It does not test the real sequence where the input tracker emits `Capturing`, overwrites it with `Dragging` before any render sample, and the render helper receives a false latch. The tests therefore validate helper truth-table behavior but not the cross-thread event-delivery contract on which that truth table depends.

### Minimal diagnostics if runtime confirmation is required

1. At `Bar.Interaction.cpp:6009-6038`, record a bounded trace entry for each mode/phase publication: transition serial, `captured`, `modeChanged`, mode, phase, elastic offset, and whether `UpdateRendering` will be requested. This should show `Capturing` immediately followed by `Dragging` under the same even serial.
2. At `Bar.RenderLoop.cpp:7124-7126`, record one bounded entry per target edge with `previousFrameMode`, sampled mode/phase, drag/spring flags, prior latch, resolved target, and progress `val/tar`. The predicted failing signature is `Floating, BottomDocked, Dragging, true, *, false -> false` with no earlier sampled `Capturing`.
3. Only if step 2 shows target/progress positive, add HRESULT/options diagnostics around `CreateBitmap`, cache `EndDraw`, main target restore, and main `EndDraw`, plus cache-null state. This separates resource failure from lifecycle failure without adding pixel readback or a second presentation chain.
4. Only if progress is positive and the cache is valid, log geometry, `capacityOrigin`, candidate viewport/source, `presentDirty`, current transform, and destination once. Static analysis predicts these will overlap correctly.

## Files Found

- `Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h` - dock state machine, transient phases, feedback target helper, geometry, and spring rules.
- `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp` - input polling loop, atomic mode/phase publication, transition serial, and render wake requests.
- `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp` - frame snapshot, target latch, animation call order, geometry/dirty/viewport resolution, D2D cache, and ULW transaction.
- `Inkeys/Inkeys/UI/Bar/Bar.Animation.cppm` - `BarUiValueClass::SetTar` state transition.
- `Inkeys/Inkeys/UI/Bar/Bar.Animation.cpp` - per-frame value advancement.
- `Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp` - main D2D target creation and dirty clip implementation.
- `InkeysHeadlessTests/bar_bottom_dock_tests.cpp` - pure dock/feedback tests that do not model missed phase sampling.
- `.trellis/tasks/08-20-ui3-bar-bottom-dock-feedback/{prd.md,design.md,implement.md}` - requirements, old/new rendering intent, and completed plan.
- `.trellis/spec/native-desktop/rendering-and-ui.md` - Bar premultiplied D2D, dirty/viewport, decoration target, and successful-present contracts.

## External References

- No external lookup was required. Bitmap option and alpha conclusions are limited to the option flags and composition semantics expressed by the code; runtime HRESULT logging remains the appropriate confirmation for device-specific failure.

## Related Specs

- `.trellis/spec/native-desktop/rendering-and-ui.md:39-51` - Bar owns a premultiplied D2D target and paired draw/GDI lifecycle.
- `.trellis/spec/native-desktop/rendering-and-ui.md:260-373` - dirty tracking, layout/source coordinates, decoration target restoration, single ULW chain, and successful-present tuple contracts.
- `.trellis/tasks/08-20-ui3-bar-bottom-dock-feedback/prd.md` - background must start on a real floating-to-capturing transition and persist through rebound.
- `.trellis/tasks/08-20-ui3-bar-bottom-dock-feedback/design.md` - independent visual lifecycle and cached premultiplied decoration design.

## Caveats / Not Found

- This is static analysis only; no visible GUI, D2D debug layer, bitmap pixel readback, or interactive instrumentation was run.
- The lifecycle race is the only code path found that directly explains desktop annotation plus a working dock interaction and potentially working foreground indicator. It is not a mathematical guarantee on every scheduler interleaving, hence the high-confidence rather than absolute classification.
- The role-isolation contract prohibited reading `implement.jsonl` / `check.jsonl` and prohibited git operations. `task.json`, `prd.md`, `design.md`, `implement.md`, current source, tests, workflow, and relevant specs were read. Exact introducing-commit diff details remain for the main session.
- No deterministic bitmap-options, alpha, target-lifecycle, geometry, transform, capacity, viewport-source, dirty-clip, or ULW tuple error was found after progress becomes nonzero.
