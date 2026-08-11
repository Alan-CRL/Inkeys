# Technical Design

## 1. Terminology And Lifecycle

- Trellis lifecycle remains `planning` until a later user message approves this exact planning summary and `task.py start` is run.
- `Work Phase 0-5` below refers to this task's ordered engineering stages inside Trellis Execute. It does not replace Trellis Plan/Execute/Finish phases.
- The implementation run is continuous across Work Phase boundaries. Per-phase build/test/diff/task updates are gates, not user-confirmation points.
- A later user instruction authorizes controlled Debug ARM64 UI3 launches, real main-bar clicks and fixed-sample output. Each completed sample is followed by direct process termination. Subjective visual acceptance remains manual and does not block progress through Work Phase 5.

## 2. System Boundary

### Owned By This Task

The task owns the current UI3 Bar client:

```text
interaction/window state
        |
        v
render request + persistent targets
        |
        v
animation runtime -> layout/derived geometry -> lighting/dirty
        |
        v
D2D client context -> GetDC -> ULW -> EndDraw
```

It may adjust existing UI3 device-generation, frame-lease and drawing-notification consumers when needed for correctness, but it does not own a new cross-window render scheduler.

### Explicit Neighbor Boundaries

- Draw2: producer of existing drawing activity notification and shared drawing state. Regression boundary only; no scheduling redesign.
- Draw3: future replacement. Do not add UI3 APIs that require Draw2-specific objects or lifecycle.
- shared UI3 device epoch: existing process-level contract in `IdtD2DPreparation.*`; preserve Interactive/Cosmetic priority and generation invalidation.
- Setting/PPT/other HWNDs: outside implementation scope. Record cross-client findings for `08-01-render-pipeline-refactor`.

## 3. Evidence And Decision Model

Every performance proposal uses one of four statuses:

| Status | Meaning | Allowed action |
| --- | --- | --- |
| Confirmed fact | Source, test or measurement proves current behavior | May constrain design |
| Reproduced defect | Headless test/static proof plus reachable control flow demonstrates incorrect behavior | Fix in task scope |
| Measured hot path | Repeated measurements exceed run noise and materially contribute to target scenario | Optimize, then remeasure |
| Hypothesis | Plausible but unmeasured | Instrument only; no broad rewrite |

An optimization is retained only when its before/after improvement is repeatable and relevant regressions stay within the measured noise band. Complexity, memory and correctness costs are part of the decision, not afterthoughts.

## 4. Measurement Architecture

### 4.1 Temporary Aggregation

Use QPC for wall-clock boundaries and `QueryThreadCycleTime` where CPU-active time matters. Temporary scopes write only numeric aggregates:

- count, sum, min/max;
- fixed-size or bounded histogram/sample reservoir;
- median/P95 calculated after the benchmark;
- cache hit/miss/create/evict and frame/wake reason counters;
- dirty pixels/window pixels and HRESULT/BOOL outcome counters.

No scope performs formatting, allocation, file I/O or logging per frame. Any product-path collector is compile-time/debug gated and removed in Work Phase 4. Headless harnesses print one report after completion.

### 4.2 Timing Boundaries

Choose boundaries at real phase transitions, not arbitrary line ranges:

1. wake/request consumption and frame-state snapshot;
2. target/layout calculation, with per-domain child totals;
3. animation advancement and full/active scan;
4. lighting preparation and cache lookup/create;
5. dirty-region prediction;
6. D2D draw recording, split by widget/resource class when possible;
7. `GetDC`, ULW, `ReleaseDC`, `EndDraw` separately;
8. sleep and spin tail separately.

Nested totals must state whether child time is included. The final report compares the same boundaries across baseline and final code.

### 4.3 Headless Harness Tiers

Tier A, pure CPU and always preferred:

- curve/timeline/keyframe/value/color/pct advance;
- target same/no-op, interruption, force replace, disabled mode and speed changes;
- realistic widget/property population, active vs inactive scan;
- domain revision/active-list candidate overhead;
- rect/dirty union and cache-key quantization;
- `AtomicWaitClass` request storm, clear-window and shutdown;
- `HighPrecisionWait` overshoot/spin/thread cycles.

Tier B, no HWND but real WARP/D2D where feasible:

- create the existing WARP epoch and an offscreen compatible bitmap target;
- cold/warm Shape/Superellipse/SVG/PNG/mask work;
- cache miss/hit/create/evict behavior and device-generation invalidation.

Tier C, controlled runtime integration:

- real layered HWND and ULW/DWM stage timing from the production path;
- no-input settling plus main-bar expand/collapse, mode switch, attribute-panel open/close, rapid reversal and dynamic-lighting scenarios;
- a temporary Debug-only production-state driver is the default for repeatable animation transitions, especially thickness Slider/Fine Dial and preview switching; real click automation is reserved for window/coordinate confirmation when useful, with the evidence source labeled explicitly;
- expanded-panel third-light runs use a deterministic circular pointer trajectory through real input/Raw Input handling, then compare the same scenario with normal lighting;
- one-shot fixed-capacity capture written after warm-up, followed by direct process termination only after the output is complete.

Tier C may support real frame-cost conclusions but not subjective animation or lighting-quality claims. Tier A/B results alone may not be described as full UI frame time.

## 5. Animation Runtime Design

### 5.1 Extend The Existing Framework

The default architecture is an `Inkeys.UI.Bar:Animation` partition or an equivalent project-consistent partition extracted from current `Bar.UI`:

- curve application and curve spec;
- batch and keyframe timeline;
- State/Value/Color/Pct property primitives;
- common finish/advance/force-replace operations;
- global animation options currently defined downstream in Main.

`Inkeys.UI.Bar:UI` then composes these primitives into widgets. `Bar.Main` consumes them but no longer owns generic interpolation lambdas. This removes the current UI -> Main extern-definition inversion.

Do not create a parallel class family. Existing public/consumer names stay stable unless a move requires import changes.

### 5.2 Advance Contract

Common advancement receives an explicit immutable context conceptually containing:

```text
dtSeconds
speedRate
animationEnabled
forceReplace
```

It returns whether a visible/current value changed and whether the property remains active. Finish behavior is shared with disabled-animation and force-replace paths. Color/Pct interpolation retains type-specific math while sharing timing/curve/state replacement.

The runtime must preserve:

- same-target no restart;
- start value captured from current visual value on interruption;
- one middle keyframe and 50% join boundary;
- joined remaining-duration semantics;
- `animateWhenDisabled` exceptions;
- current CurveSpec continuation and overshoot behavior;
- SVG/Word content midpoint semantics.

### 5.3 Ownership Model

Classify every field before changing synchronization:

| Class | Default owner | Notes |
| --- | --- | --- |
| persistent product/interaction target | interaction/window producer, read by render | Needs coherent publication contract |
| widget topology/map membership | initialization before threads | No runtime structural mutation without new synchronization |
| render progress/start/current interpolation state | rendering thread | Candidate for non-atomic storage only after all writers are removed |
| Fine Dial gesture/physics | interaction thread | Publish minimal visual/candidate snapshot |
| D2D resources/cache | rendering thread/device generation | Never cross device/context or interaction thread |
| wake/request generation | multiple producers, render consumer | Must coalesce without losing the newest request |

Multi-field property updates need a coherent transaction. Acceptable designs include a producer request snapshot/generation consumed by render, a small lock around target publication, or a proven single-writer transfer. Retaining individual atomics without a transaction is allowed only if invariants are demonstrably order-independent.

### 5.4 Dedicated State Policy

Primary/cursor/drawing lighting, Fine Dial gesture physics, hover lifecycle and SVG/Word content transition may retain dedicated state. Reuse only common curve/timeline/interpolation pieces when that reduces duplicated code/state or measured cost. No abstraction-purity migration.

## 6. Performance Architecture

### 6.1 Separate Reasons To Wake From Work To Perform

Represent frame work by reason/domain rather than treating every wake as a full calculation. The minimal target is a frame snapshot plus domain revisions:

```text
wake reasons -> snapshot changed revisions
             -> recalc only dirty target/layout domains
             -> advance only active animation domains/properties
             -> draw only when current visual/lighting/dirty state changed
```

Candidate domains are Main Button, Main Bar, Draw Attribute, Thickness/Color Picker, Geometry, More, Lighting and Debug. A domain owns the inputs that invalidate its cached target/layout; dependency edges are explicit. Global zoom/side/theme/device changes may invalidate several or all domains.

Start with coarse domain flags/revisions. Split further only when measurements show value. Do not build a general reactive framework.

### 6.2 Active Animation Tracking Decision

Phase 1 keeps the current scan while making a single property-advance API measurable. Phase 2 compares:

- current full scan;
- per-domain active flag;
- changed-widget list;
- non-owning active property registry, if needed.

An active registry is accepted only if it wins materially and proves:

- target update always registers;
- interruption does not duplicate/stale-register;
- completion removes safely;
- widget lifetime/topology cannot dangle entries;
- reset/device/state changes clear or rebuild correctly.

Otherwise keep the simpler scan.

### 6.3 Wake And Present Correctness

Test the existing bool handoff before changing it. If producer/consumer clear-window or shutdown fails, prefer a monotonic request generation/counter or an exchange protocol where the consumer records the consumed generation after snapshot. Requirements:

- producers never block on rendering;
- multiple requests coalesce;
- a request arriving during frame calculation schedules a subsequent frame;
- shutdown explicitly wakes idle render thread;
- no steady polling.

Lighting state preparation and screen presentation must not be conflated. If Cosmetic lease skip can consume the last state change, retain a `pendingPresent`/revision until a frame successfully presents or restructure preparation so committed visible state advances only with a render lease. Preserve animation time semantics without busy retry.

### 6.4 Lighting And Cache

Keep existing 32/24/24/64 cache families and generation invalidation as baseline. Add temporary counters around key lookup, miss creation, eviction and failure. Optimize only observed causes:

- unstable float/scale/geometry keys;
- misses concentrated at animation endpoints;
- single-slot geometry churn;
- text layout/format recreation;
- Raw Input updates that cannot affect visible regions;
- dirty rect growth beyond actual light bounds.

Never cache final cursor/primary light composite frames. Never suppress third diffuse during geometry animation to avoid miss creation.

### 6.5 D2D/GDI/ULW

Maintain one Bar `BeginDraw/EndDraw` span and paired `GetDC/ReleaseDC`. Check every HRESULT and ULW BOOL at its ownership boundary. Update last-presented bounds only after successful present/commit; failures retain enough dirty state for recovery.

Device-generation and `D2DERR_RECREATE_TARGET` handling remain authoritative. Any additional recovery must not reuse old-device resources or log/retry unboundedly each frame.

### 6.6 Frame Pacing

Measure current sleep/spin first. Candidate changes, in order of complexity:

1. reduce or remove spin tail when its CPU cost exceeds pacing benefit;
2. adaptive short tail based on observed sleep overshoot;
3. waitable timer with correct cancellation/shutdown;
4. dynamic pacing only if it preserves 60 FPS-class active animation and does not add polling.

Lower animation FPS is not an option. The selected approach needs overshoot/P95 and thread-cycle evidence.

## 7. Structure Refactor

Perform after performance behavior is stable so file moves do not obscure measurement regressions. The intended responsibility graph is:

```text
Bar.Main / coordinator
  |-- Animation
  |-- LayoutTransitions
  |-- RenderingResources
  |-- Lighting
  |-- Interaction
  `-- Initialization
```

This is a responsibility target, not a mandated file count. A function may stay with its data owner when splitting would expose excessive internals or create circular imports.

### Rendering

Extract `BarUIRendering` implementation and device/cache lifetime from the coordinator. Rendering consumes an already prepared frame model/current widget values; it does not own target business decisions.

### Layout And Transitions

Own frame snapshot, domain revisions, target calculation, batch timelines, render-side runtime and derived geometry. Expose small phase functions rather than one 8,874-line body.

### Interaction

Separate common pointer/capture/hover helpers from base bar, draw/thickness/color picker and geometry handlers. Preserve nested message/capture semantics and shutdown behavior.

### Initialization

Own stable widget topology construction, registration and thread startup order. Make the “maps freeze before threads” invariant explicit or introduce synchronization if runtime mutation is truly required.

### Rename

Use `git mv` during implementation for `Bar.Buttom.*` and update project items/imports/types/enums/members/comments atomically. Search the full repository after rename. Treat `Bar.Bottom.cppm` as a separate semantic unit until its contents prove otherwise; do not merge by name alone.

## 8. Error, Lifetime And Safety Design

- COM/D2D resources remain `ComPtr` and are released in dependency order on generation change/shutdown.
- Failed factory/format/bitmap/effect creation is checked before dereference/use; fallback is bounded and does not retry/log every frame.
- last-presented dirty bounds are transactional with successful present.
- detached thread changes require explicit producer stop, idle wake, completion observation and object lifetime proof. Converting thread type is not an automatic cleanup; do it only when shutdown contract can be verified.
- Raw Input registration, timers, capture and posted messages are window-thread owned and tolerate late messages after logical shutdown.
- Static analysis findings are traced to reachable code and real trust/resource boundaries before fixing.

## 9. Compatibility And Manual Validation

Automated code must preserve current Draw2 notifications and UI3 config/API surface. Manual checklist generated at the end covers:

- Inkeys2 vs UI3 on the same device;
- normal lighting vs full dynamic lighting;
- idle, rapid hover/click/open/close/reversal and long animation;
- Draw Attribute, Geometry, More, Color Picker, Slider/Fine Dial;
- drawing start/end with cursor inside/outside UI3;
- DPI/UI zoom, main-bar side changes and device-recovery scenarios;
- Windows 7 SP1 + KB2670838 only when the user has that environment.

Runtime measurements may be marked PASS when the exact scenario and complete output are preserved. No subjective visual item is marked PASS by the agent.

## 10. Rollback Strategy

Keep independently reviewable rollback points:

1. temporary baseline instrumentation/harness;
2. animation partition/common advance extraction;
3. ownership/publication changes;
4. domain dirty/active tracking;
5. lighting/cache changes;
6. present/recovery and frame pacing;
7. file/module split;
8. `Buttom -> Button` rename;
9. safety fixes.

Each point must build and pass headless tests before proceeding. If a performance unit lacks evidence or regresses, revert that unit without undoing unrelated proven work. Temporary diagnostics are removed after Work Phase 4 regardless of optimization outcome.
