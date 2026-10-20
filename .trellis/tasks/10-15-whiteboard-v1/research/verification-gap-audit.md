# Research: Whiteboard verification gaps and runtime diagnostics

- Query: What do the passing tests actually establish, and why can Whiteboard still stall the main Bar?
- Scope: internal; read-only code/log audit, no build or GUI, no code changes
- Date: 2026-10-15

## Findings

### Observed prior artifacts

`TestResults/whiteboard-v1-2026-10-15/ui3-unify-offscreen-checked-stderr.log` contains `[PageControlScene] failures=0`. `Build/eraser-b/offscreen-results.log` ends `[EraserVisual] failures=0`. `ui3-unify-headless-final.log` ends `PASS animation correctness`, EraserAttribute layouts=216 failures=0, RecallAttribute failures=0. These are genuine assertions/results, but none is an end-to-end Whiteboard/main-Bar frame pacing result. This investigation did not rerun tests or independently recover the prior process exit status.

### Actual code and what it exercises

- `Inkeys/Inkeys/UI/PageControl/PageControl.cpp:2613` `RunOffscreenTests`: creates one local Scene at a time (`2640`). It calls `EnsureDeviceResources` once (`2656`), manually advances 41 virtual frames with 50ms increments (`2659`), then renders the final state (`2665`). It tests real pixels, hit geometry, label visibility, green SVG sizing and texture preview symmetry. It explicitly sets background `contentEnvelope=false` (`2642`), rather than reproducing the normal runtime ConfigureSurface/RenderSurface envelope path.
- The later animation block (`2822+`) also owns one local Scene, allocates resources once (`2841`), and directly calls Advance/Render with synthetic 16ms timestamps (`2857+`). It accurately compares intermediate rectangles to `EraserSurfaceMotion` / `ResolveEraserSurfacePose` / `ComposeEraserPose` (`2852-2872`) across animation speeds and DPI. This proves equality of the sampled mathematical motion, not actual rendering throughput, true idle/resume timing or sharing the same complete main-Bar attribute implementation.
- Production path is `RenderSurface` (`1870`) -> `ConfigureSurface` (`975`) -> `PresentScene` (`1134`), with Configure on every requested frame, state revision gating, multiple locks, stable backing calculation, `EnsureDeviceResources` every visible frame (`1162`), GetDC / ULW / EndDraw (`1285-1333`), then Window Service bounds/show commands (`2143+`). The offscreen tests bypass almost all of this orchestration.
- `InkeysHeadlessTests/page_control_tests.cpp:1131` runs policy/geometry/state tests; `TestDirtyInvalidationDeduplication` (`940`) exercises pure predicates and state equivalence, not live recurring wake counts or resource allocations. It does not instantiate/measure all live Whiteboard surfaces plus main Bar.
- `InkeysHeadlessTests/animation_tests.cpp:1661+` runs the shared animation correctness/policies and `RunRenderSchedulerTests`, but does not call PageControl RunOffscreenTests. The latter is chained only from product `Bar.EraserAttribute.Test.cpp:2358`, through `IdtMain.cpp:1309` early no-window diagnostic mode.
- `InkeysHeadlessTests/render_scheduler_tests.cpp:225+` checks real Scheduler stable callback order with trivial recording callbacks; `359+` checks continued callbacks are at least 15ms apart, i.e. no more than 60fps, without an upper latency bound. These are valid scheduler contracts, but not a throughput or fairness budget for expensive actual clients.
- Some existing main-button benchmark code exists (`Bar.EraserAttribute.Test.cpp:2359+`) and emits geometry/draw means. It measures one standalone renderer/superellipse at 256x192, verifies success, and has no frame-budget assertion. It does not include Whiteboard widgets, menu traversal, ULW, multi-client alternation or current full-size Whiteboard rendering.

### Why this can affect main Bar

`RenderPipeline.cpp:806-860` dispatches actual clients serially on one rendering thread in stable order. Heavy bottom controls or Freeze callback execution therefore extends the batch and delays the next Bar callback, even though Bar is first in each batch. Correct curve math and proper 60fps maximum pacing do not provide a minimum actual frame rate.

A resource-invalidating bug dependent on alternating Bar/left/right contexts cannot be caught by the single-Scene offscreen loops. Those loops do not re-run Ensure on every sampled frame, do not alternate real clients, and do not assert stable cache creation counts over warm frames. This is a test-coverage statement; whether the running instance has this exact resource bug requires hot-path analysis/runtime measurements.

### Why text/standards regressions escaped

`whiteboard.md` explicitly records the earlier wrong secondary text slot: StandardTwoTwo secondary text has zero font size, while the primary slot is the actual label. The current real-pixel tests inspect that slot (`PageControl.cpp:2701+` and `2786+`) and would catch the final-state disappearing-label defect. Earlier policy-only or helper reuse checks were insufficient to establish that production widget specifications actually populated the shared layout's visible slot.

"Uses shared drawing helper" is not equivalent to "same complete UI component": current tests prove selected formulas/helpers and pixel outcomes. They do not compare the full production main-Bar layout/state/idle/resource behavior with PageControl integration. Repeated fixes were validated on narrower isolated behavior, leaving cross-client/resource/presentation behavior outside the gate. No evidence supports claiming actual GUI smoothness passed.

### Minimal existing diagnostics for the user's running app

- Ordinary product startup already installs `SetDiagnosticsSink` (`IdtMain.cpp:2355`) backed by async warn-level logger sharing `globalPath + log\\idt<Timestamp>.log` (`2339-2360`). Read/copy/tail that existing log; no UI control, restart, capture injection or program reconfiguration is required.
- `RenderPipeline.cpp:104-167` emits `[UI3Diag]`: batch max/total, active period max, per-client callback max/total and call counts, active and commit gap, continuation/retry, GetDC/ULW/EndDraw/lock stages, rounded/geometry mask cache hit/miss/create, failures and latest/slowest frames. Compare Bar vs PptBottomLeft/PptBottomRight/WhiteboardFreeze to locate delay attribution.
- Important limit: `RenderPipeline.Diagnostics.h:10-12` uses `LongFrameMs=50.0` and `HealthySummariesEnabled=false`. Persistent 20-40ms frame times may still feel poor relative to 60fps without producing default slow summaries. Absence of warnings is not a passing frame-rate measurement.
- Resources preparation lacks an explicit PageControl resource-stage timer around `EnsureDeviceResources` (`PageControl.cpp:1162`), so high callback time outside draw/ULW can indicate preparation but does not isolate its internal allocation count. Existing aggregate logs can narrow, not fully prove that mechanism.
- RawCapture APIs exist (`RenderPipeline.cpp:993,1079`) but the ordinary production UI has no demonstrated external enable channel. `Bar.Presentation.Test.cpp` is gated by `INKEYS_ENABLE_UI3_PRESENTATION_HARNESS`; its normal-product branch says fixture not installed (`1473+`). Compiled finite fixture scenes are only MainFold/DrawAttribute (`Bar.PresentationProbe.h:12`), not Whiteboard. Do not inject private fixture messages into the user's existing instance.

### Focused future verification (not performed)

After hot-path findings are confirmed, a meaningful no-window integration gate should alternate actual Bar/left/right Scene rendering in real dispatch order, call real Ensure each frame at stable sizes, and assert resource creation is bounded after warmup, steady-state idles, stale updates do not restart motion, label resources survive alternation, and menu content updates do not multiply full redraws. Timing measurements should use real monotonic durations and report complete batch/commit-gap percentiles on the actual machine. No synthetic timestamp test can replace those measurements.

## Related specs and files

- `.trellis/spec/native-desktop/rendering-and-ui.md`: shared serial scheduler, target generation, idle clock, per-window device resource lifecycle.
- `.trellis/spec/native-desktop/whiteboard.md`: UI3 contracts and explicit separation of headless from GUI acceptance.
- `.trellis/tasks/10-15-whiteboard-v1/validation.md`: prior results accurately note real GUI/FPS unmeasured, but user-facing claims about unified behavior require the narrower interpretation above.
- `Inkeys/Inkeys/UI/Bar/Bar.Scene.cpp:2199`: Scene delegates Ensure and resets local uploaded widget caches on real target recreation. `:799` enumerates upload/content reset. Stable cache behavior must be measured across actual alternation, not inferred from one Ensure.
- `Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp:142`: target/context reuse conditions and `:191` cache destruction.

## External references

None needed; findings are derived from repository production code and prior local test artifacts.

## Caveats / Not Found

This audit does not claim the running instance's root cause, CPU/GPU utilization or current frame rate. No GUI, build, test rerun, code/spec modification or git operation was performed. Existing runtime logs need interpretation with the live binary build identity and the exact observation time. No `currentScene` symbol exists in the audited Bar sources; if that phrase refers to a conceptual owner change, it must be traced by the separate hot-path investigation.
