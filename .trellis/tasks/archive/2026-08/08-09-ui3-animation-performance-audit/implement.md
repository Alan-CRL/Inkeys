# Implementation Plan

## 0. Planning/Start Gate

- Task: `08-09-ui3-animation-performance-audit`.
- Current Trellis status while this plan is reviewed: `planning`.
- Do not edit product code until a later user message explicitly approves this final plan.
- On that later message, start exactly this task:

```powershell
python .\.trellis\scripts\task.py current --source
python .\.trellis\scripts\task.py start 08-09-ui3-animation-performance-audit
python .\.trellis\scripts\get_context.py --mode phase --step 2.1 --platform codex
```

- Read, in order: `prd.md`, `design.md`, this file, all three `research/*.md`, then every real entry in `implement.jsonl`.
- Work Phase 0-5 below must run continuously. Do not ask for confirmation between phases. Do not run `task.py start` in the planning turn that created these artifacts.
- This plan does not authorize commit, push, PR or archive. Complete engineering Work Phase 0-5 and report; Trellis commit/archive confirmation remains a separate user decision unless the later instruction explicitly covers it.

## 1. Global Execution Rules

1. The execution-turn user instructions on 2026-08-09 supersede the planning-only GUI ban: controlled Inkeys/UI3 launch, desktop automation, Debug-only scenario drivers and timing-file output are allowed for this task's performance validation. Prefer a repeatable temporary production-state driver for animation transitions; use real window automation only where it adds evidence. After a fixed sample is fully written, terminate the measured process directly instead of using the in-product shutdown path; preserve user configuration and avoid unrelated desktop changes.
2. Preserve unrelated user changes. Snapshot dirty paths before edits and never reset/revert files outside this task.
3. Keep source encoding and EOL unchanged. New/changed C++ follows adjacent style and uses short Chinese comments only for non-obvious ownership, failure or timing logic.
4. Build the complete Solution with ARM64 host MSBuild, not `Inkeys.vcxproj` alone. Build timeout is at least 5 minutes; use 10 minutes for analysis builds.
5. A missing manual/GUI result is `MANUAL / NOT VERIFIED`, not a blocker. Continue to the next Work Phase.
6. Every performance change needs a before/after measurement from the same headless scenario. If gain is within run noise, complexity rises materially, or a benchmark regresses, remove that change.
7. After every Work Phase, run the phase gate and update the matching Execution Ledger section before starting the next phase.
8. When source has moved since planning, search symbols and update anchors. Preserve the behavior/contracts, not old line numbers.

## 2. Common Phase Gate

Run after each Work Phase:

```powershell
git status --short
git diff --check
git diff --stat
```

Then:

- inspect the full task diff and confirm no unrelated scope;
- run all relevant headless unit/benchmark executables discovered or created;
- build `InkeysRepo.sln` `Debug | ARM64` using ARM64 host MSBuild;
- record command, exit code, duration, new/existing warnings and failures;
- update this task's Execution Ledger with completed/findings/measurements/decisions/remaining risks;
- if the phase changed measurement boundaries, rerun the prior comparable benchmark before proceeding.

Current known MSBuild location from recent repository work is:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe' `
  InkeysRepo.sln /m /t:Build /p:Configuration=Debug /p:Platform=ARM64
```

Verify that path at execution time; if Visual Studio moved, locate the ARM64-host `MSBuild.exe` rather than falling back to x64 host. Do not commit build outputs.

## 3. Work Phase 0 - Baseline / Instrumentation / Headless Measurements

### 3.1 Repository And Build Baseline

- [ ] Record branch, full HEAD, status, existing dirty files and recent UI3 commits.
- [ ] Inspect Solution/project/test inventory; record that planning-time static search found no existing UI3 Bar/lighting/wait automated tests, and update if current HEAD differs.
- [ ] Build full `InkeysRepo.sln` `Debug | ARM64` without launching the output.
- [ ] Record baseline warnings/failures verbatim enough to distinguish pre-existing issues; do not fix unrelated failures.

### 3.2 Temporary Diagnostics Boundary

- [ ] Define a temporary collector with QPC totals/counts/bounded samples only; no per-frame formatting, allocation, logger or file I/O.
- [ ] Add frame/wake reason, layout-domain, animation/full-scan, lighting, dirty, D2D, GetDC, ULW, ReleaseDC/EndDraw and sleep/spin boundaries where those stages can actually execute headlessly.
- [ ] Add cache hit/miss/create/evict and dirty-pixel counters for gradient/mask/SVG/PNG/superellipse/text format/text layout.
- [ ] Keep product-path instrumentation compile-time/debug gated and list every temporary file/symbol/macro in the ledger for mandatory Phase 4 removal.

### 3.3 Headless Correctness And Microbenchmarks

- [ ] Create the smallest maintainable temporary harness using the real animation/runtime code; do not copy algorithms into a fake benchmark.
- [ ] Test curve endpoints/ranges, duration/speed, same-target no-op, force replace, disabled animation, interruption, middle keyframe and 50% `CanJoin` boundary.
- [ ] Instantiate a widget/property population matching current map/property counts. Measure inactive scan, one active property, one active domain and many active properties.
- [ ] Measure target submission/no-op, common layout/dirty pure functions, container lookup and cache key quantization.
- [ ] Stress `AtomicWaitClass`: coalesced requests, producer storm, request during consumer clear window, render idle and shutdown wake.
- [ ] Measure `HighPrecisionWait` over enough iterations with QPC + `QueryThreadCycleTime`: requested wait, sleep, spin, overshoot, P50/P95/max and thread cycles.
- [ ] If feasible without HWND, use current WARP/device code and an offscreen target for cold/warm Shape/Superellipse/SVG/mask measurements. Keep ULW/DWM marked manual.
- [ ] Store raw numeric output in a temporary ignored/build-output location or task notes, not product source. Persist the summarized table in the ledger.

### 3.4 Phase 0 Decision Gate

- [ ] Rank measured costs and record the noise threshold used to accept later changes.
- [ ] Separate confirmed facts, reproduced defects, measured hot paths and rejected/unmeasured hypotheses.
- [ ] Run the Common Phase Gate. Do not begin architecture edits until the baseline is reproducible.

## 4. Work Phase 1 - Animation Architecture Cleanup

### 4.1 Extract Existing Runtime

- [ ] Re-search current curve/timeline/keyframe/State/Value/Color/Pct definitions and all animation-option definitions/consumers.
- [ ] Create or refine the project-consistent Animation partition from existing `Bar.UI`; update `Inkeys.vcxproj` module items if files move/add.
- [ ] Move common `Finish*`, curve and `Change*` advancement into that subsystem with explicit dt/speed/disabled/force context.
- [ ] Keep widget types consuming the same animation primitives; do not introduce parallel replacements.
- [ ] Remove the current UI -> Main extern-definition inversion for global animation options without changing external behavior.

### 4.2 Ownership Audit Before Atomic Changes

- [ ] Build a producer/consumer table for every animation property/runtime field touched by interaction, render and window threads.
- [ ] Separate persistent target/request from render progress/start/current runtime.
- [ ] Define coherent multi-field target publication. Keep current atomics until the replacement is proven by tests.
- [ ] Convert a field to ordinary storage only when all runtime writers are rendering-thread owned and shutdown/lifetime is explicit.
- [ ] Preserve initialization-before-thread map topology or add synchronization if a real runtime mutation requires it.

### 4.3 Dedicated Animation Audit

- [ ] Search `elapsed|progress|duration|start|target|animating|transition|curve|easing|lerp` across first-party UI3 Bar sources.
- [ ] Review `PrepareFrameLighting`, hover stages, Fine Dial render/interaction phases and SVG/Word content timelines.
- [ ] Reuse common timing/curve/interpolation only when code/state/cost decreases. Record retained dedicated state and why.

### 4.4 Behavior Tests And Benchmark

- [ ] Expand Phase 0 tests for all moved runtime code, rapid interruption/reversal, joined deadlines and animation-disabled cases.
- [ ] Compare Phase 1 throughput/latency with Phase 0. The extraction may be neutral, but it must not regress outside noise.
- [ ] Update stale animation memo/comments discovered in moved code only when the new source of truth is established.
- [ ] Run the Common Phase Gate.

## 5. Work Phase 2 - Performance Investigation

### 5.1 Full Frame Calculation Attribution

- [ ] Instrument/measure Main Button, Main Bar, Draw Attribute, Thickness, Color Picker, Geometry, More, Lighting and Debug target/layout domains.
- [ ] For single opacity/scale/light updates, record which unrelated domains still execute and their time/operation counts.
- [ ] Record state snapshot count, duplicate derivation, repeated target submission and derived geometry/text-layout work.
- [ ] Capture separate fixed-sample runtime scenarios for no-input settling, main-button expand/collapse, main-bar mode switch, attribute-panel open/close, thickness Slider/Fine Dial/preview switching, rapid reversals and dynamic lighting. Record whether each scenario used real clicks or a Debug-only production-state driver.
- [ ] For third-light cost, keep the relevant panel expanded and drive a deterministic circular pointer trajectory through the real input/Raw Input path; repeat with normal lighting using the same sample boundaries.

### 5.2 Animation And Atomic Cost

- [ ] Measure 57 render-owned values, five widget maps and button/More scans independently.
- [ ] Compare full scan with the lightest feasible per-domain active flag prototype; only prototype a property registry if the coarse option is insufficient.
- [ ] Count atomic loads/stores and repeated reads per frame. Review memory order and coherent target publication correctness before optimizing.
- [ ] Measure enum/map lookup separately; reject container rewrite if not material.

### 5.3 Wake / Lease / Present Investigation

- [ ] Reproduce or disprove request loss in the bool wait handoff under producer storms and consumer clear-window timing.
- [ ] Test idle shutdown: setting exit state must wake rendering and allow deterministic completion.
- [ ] Test Cosmetic lease skip after the final cursor/fade update; confirm whether a pending visible state can be swallowed.
- [ ] Count wake reasons, coalesced requests, extra frames, lease wait/skip and frames with no visual change.
- [ ] Audit `sustainFlag` stationary dragging behavior and whether all continuous rendering is necessary.

### 5.4 Lighting / Cache / Rendering Investigation

- [ ] Record hit/miss/create/evict for all existing cache families under cold/warm and geometry/scale animation.
- [ ] Verify steady-state mask/effect/brush/SVG/PNG creation is zero and animation endpoints do not add avoidable misses.
- [ ] Measure text-layout/format creation, color-picker footer `MeasureText`, superellipse exact-key churn and Raw Input Grace processing.
- [ ] If offscreen WARP is available, separate D2D recording/resource cost from GetDC/ULW/EndDraw. Keep real compositor cost manual.
- [ ] Verify dirty area ratio and whether expanded lighting bounds are the remaining cost rather than blindly shrinking them.

### 5.5 Frame Pacing Investigation

- [ ] Use Phase 0 wait benchmark to quantify sleep overshoot, spin tail and CPU cycles at 60 Hz.
- [ ] Compare current wait with no-spin/smaller-adaptive-tail/waitable-timer prototypes outside product code first.
- [ ] Preserve 60 FPS-class pacing; reject changes that reduce CPU only by increasing hitch/P95 materially.

### 5.6 Phase 3 Change Set

- [ ] Create a decision table: hypothesis, evidence, selected change, expected gain, correctness risk, rollback point.
- [ ] Mark cheap/unproven hypotheses rejected and do not implement them.
- [ ] Order accepted changes P0 -> P5 exactly as in the PRD.
- [ ] Run the Common Phase Gate.

## 6. Work Phase 3 - Performance Optimization

### P0 - Eliminate Unnecessary Work

- [ ] Prevent no-change target/layout recalculation, animation traversal, redraw and resource rebuild where Phase 2 proved cost.
- [ ] Ensure a wake with no visual change can settle back to wait without missing later requests.

### P1 - Reduce Full-Frame Calculation

- [ ] Add the smallest reliable domain revision/dirty mechanism for measured domains.
- [ ] Cache stable layout/derived values and invalidate from explicit input dependencies: zoom, side, theme, state, widget target, device generation.
- [ ] Verify local animation no longer invokes unrelated domains while global changes still invalidate all required domains.

### P2 - Animation Runtime Efficiency

- [ ] Implement per-domain active tracking or a changed-widget/property registry only if Phase 2 selected it.
- [ ] Cover register/interruption/completion/reset/lifetime in tests.
- [ ] Remove measured duplicate `SetTar`, repeated snapshots/atomics and interpolation work without weakening publication correctness.

### P3 - Lighting And Cache

- [ ] Fix only measured key/invalidation/hit-rate/mask/geometry/cursor-coalescing issues.
- [ ] Preserve primary light, third-light lifecycle, Gaussian quality, normalized animation masks and device-session failure fallback.
- [ ] Do not cache final light frames or suppress effects to hide misses.

### P4 - D2D / GDI / ULW

- [ ] Remove measured repeated resource/text-layout/SVG/geometry or unnecessary transition work.
- [ ] Make `GetDC`/ULW/`ReleaseDC`/`EndDraw` outcomes explicit and preserve dirty old/new bounds until successful present.
- [ ] Maintain one Bar draw span and generation-correct resources.

### P5 - Frame Scheduling

- [ ] Apply only the wait/pacing option selected by Phase 2.
- [ ] Add explicit idle shutdown wake and pending-present handling if reproduced.
- [ ] Rerun wait and frame-state benchmarks; confirm low cycles without worse hitch/P95.

### Phase 3 Gate

- [x] Rerun the entire Phase 0 suite and produce baseline vs Phase 1 vs Phase 3 table.
- [x] Repeat every accepted dynamic UI scenario with the same warm-up, frame count, input sequence and lighting mode; compare per-stage P50/P95 and demand counters before/after.
- [x] Remove every optimization whose benefit is not repeatable or whose maintenance/correctness cost is disproportionate.
- [x] Run the Common Phase Gate.

## 7. Work Phase 4 - Structure / Naming / Cleanup

### 7.1 Split Responsibilities

- [x] Freeze performance behavior and benchmark outputs before moving code.
- [x] Extract rendering/resource/cache ownership from `Bar.Main`.
- [x] Extract layout/target/transition phases and expose explicit snapshot/update/advance/dirty boundaries.
- [x] Split interaction into common pointer/capture helpers plus base bar, draw/thickness/color picker and geometry handlers.
- [x] Extract initialization/topology construction and make thread-start ordering explicit.
- [x] Keep `Bar.Main` as coordinator; avoid circular partition imports and broad public APIs.
- [x] Update `Inkeys.vcxproj` and full Solution build after each module/file batch.

### 7.2 `Buttom -> Button`

- [x] Inventory all `Buttom`, `BarButtomClass`, `BarButtomSetClass`, `BarButtomPresetEnum`, file, module/import, member, comment and project references.
- [x] Inspect `Bar.Bottom.cppm` separately and document why it remains separate or is renamed/merged; do not infer from spelling.
- [x] Use tracked renames and update all references atomically.
- [x] Search repository after rename; no first-party UI3 `Buttom` residue is allowed. Do not modify archived historical task text merely to make grep zero.

### 7.3 Cleanup

- [x] Remove stale animation memo, obsolete regions/comments, dead code, duplicate helpers and unused include/import exposed by the split.
- [x] Improve RAII/const/constexpr/ownership only in affected paths and only when behavior remains covered.

### 7.4 Mandatory Temporary Diagnostics Removal

- [x] Remove every Phase 0 product instrumentation symbol, scope, counter, histogram, output hook, macro and benchmark-only branch listed in the Phase 0 ledger.
- [x] Delete one-time harness/project changes by default.
- [x] Retain a headless regression benchmark only if separated from product hot path, Release has zero overhead and the ledger explains long-term value.
- [x] Build/test after removal and confirm no Release investigation burden.
- [x] Rerun comparable final benchmarks from the separated harness if retained; otherwise preserve Phase 3 raw output and run permanent correctness tests.
- [x] Run the Common Phase Gate.

## 8. Work Phase 5 - Safety / Correctness Audit

### 8.1 Git Scope

- [x] Generate current one-month UI3 commit list and diff; compare with `research/git-history-and-task-overlap.md` and include new commits.
- [x] Review every task-modified file and use blame/show for non-obvious invariants.
- [x] Confirm no overlap implementation from 07-17/08-01 was pulled in.

### 8.2 C++ And Concurrency

- [x] Audit null/bounds/narrowing/uninitialized/iterator/lifetime/ownership/UB.
- [x] Audit animation target transaction, remaining atomics, locks/order, request handoff, pending present, stale state and detached shutdown.
- [x] Verify map/property/active-list lifetimes and no dangling non-owning entries.
- [x] Re-run producer storm, shutdown and animation interruption tests under repeated iterations.

### 8.3 D2D / D3D / COM / Win32

- [x] Audit generation/device loss, cache invalidation, effect inputs, context/target ownership and D2D/GDI sequencing.
- [x] Check every relevant HRESULT/BOOL and bounded fallback/retry/log behavior.
- [x] Audit HWND, posted messages, Raw Input, timers, capture, callbacks and late shutdown messages.
- [x] Specifically resolve or document the planning-time static risks: final Cosmetic skip, idle shutdown wake, present old-bounds transaction, GetDC/ULW recovery and text-format null.

### 8.4 Static Analysis

- [x] Review normal compiler warnings against the baseline.
- [x] Use existing MSVC `/analyze` or project-supported code-analysis property on the full Solution when the installed toolchain supports it; timeout at least 10 minutes.
- [x] Verify each warning against reachable code before changing it; do not churn unrelated third-party/baseline warnings.

### 8.5 Final Validation

- [x] Run all permanent headless tests and any retained long-term benchmark.
- [x] Build full `InkeysRepo.sln` `Debug | ARM64` with ARM64 host MSBuild.
- [x] Run `git diff --check`, inspect full diff, confirm encoding/EOL and project-file/module completeness.
- [x] Run Trellis context validation and full-scope `trellis-check` against `check.jsonl`, fixing findings until green in the achievable headless scope.
- [x] Produce final benchmark table, safety finding table and manual GUI checklist.
- [x] Mark manual/visual items `NOT VERIFIED`; do not archive as fully accepted until the user reports manual results.

## 9. Validation Commands And Outputs

Planning/context checks:

```powershell
python .\.trellis\scripts\task.py current --source
python .\.trellis\scripts\task.py list-context 08-09-ui3-animation-performance-audit
python .\.trellis\scripts\task.py validate 08-09-ui3-animation-performance-audit
```

Source/history checks should use `rg`, `git log`, `git show`, `git diff` and `git blame` against the current symbols/commit range. Do not rely on planning line numbers after edits.

Build:

```powershell
& '<ARM64-host-MSBuild.exe>' InkeysRepo.sln /m /t:Build `
  /p:Configuration=Debug /p:Platform=ARM64
```

Static analysis, only if supported by current project/toolchain:

```powershell
& '<ARM64-host-MSBuild.exe>' InkeysRepo.sln /m /t:Rebuild `
  /p:Configuration=Debug /p:Platform=ARM64 /p:RunCodeAnalysis=true
```

Headless test/benchmark command names must be recorded after the Phase 0 test inventory establishes their real paths. Controlled UI3 runs are also allowed under Global Execution Rule 1 and must record scenario, configuration, output path and clean-shutdown result.

## 10. Risky Files And Rollback Points

Expected high-risk ownership areas:

- `Inkeys/Inkeys/UI/Bar/Bar.UI.cppm` and `Bar.UI.cpp`: animation primitives, content transitions and caches.
- `Inkeys/Inkeys/UI/Bar/Bar.Main.cppm` and `Bar.Main.cpp`: renderer, layout/animation loop, interaction, wake, lifetime and initialization.
- `Inkeys/Inkeys/UI/Bar/Bar.State.cppm` and `Bar.Atomic.cppm`: cross-thread publication and wake.
- `Inkeys/Inkeys/UI/Bar/Bar.Format.cppm`: text resource failure handling.
- `Inkeys/Inkeys/UI/Bar/Bar.Buttom.cpp` / `Bar.Bottom.cppm` and consumers: naming/registration split.
- `Inkeys/IdtD2DPreparation.*`: existing device epoch/lease contract, only if a verified correctness fix requires it.
- `Inkeys/IdtDrawpad.cpp`: regression-only notification boundary; no scheduling refactor.
- `Inkeys/Inkeys.vcxproj`: module/file rename registration.

Rollback in the sequence listed in `design.md`; never use destructive worktree-wide reset. Revert only the measured change unit that failed its gate, preserving unrelated user work.

## 11. Execution Ledger

Update incrementally during implementation; do not fill all phases only at the end.

### Work Phase 0

- Status: in progress; repository/build baseline and the first real-module animation
  harness are complete. Frame-stage, wait/shutdown, lighting/cache and D2D/ULW
  diagnostics remain for the next slice.
- HEAD / dirty baseline: `feature/animation` at
  `a4201c992bfb722ff2ca50c783e243bd1899612c`; before product edits the only
  untracked path was this task directory.
- Build / tests: baseline full `InkeysRepo.sln` `Debug | ARM64` build with the
  ARM64 host MSBuild completed in 62.78 s with exactly 147 warnings and 0
  errors. No GUI was launched. Static inventory found no existing first-party
  UI3 Bar headless test project.
- Measurements: no pre-extraction animation microbenchmark was available, so
  this slice does not claim a before/after result. The retained console harness
  uses the production `Inkeys.UI.Bar.Animation` module, QPC, 3 warm-up runs,
  11 measured runs and `QueryThreadCycleTime`; it performs no product hot-path
  logging or instrumentation.
- Findings: a headless correctness seam can cover curve/timeline/keyframe,
  target no-op/interruption, final-frame scheduling, quantized color and
  animation-disabled hover behavior without creating an HWND.
- Decisions: keep the harness as an isolated candidate regression test while
  the task is active; reevaluate its long-term value and mandatory removal at
  Work Phase 4. Do not interpret isolated animation throughput as full UI frame
  time.
- Remaining risks / temporary files: no frame-stage or rendering diagnostics
  exist yet; wait/shutdown, dirty math, cache keys, lighting, D2D/ULW and all GUI
  behavior remain unmeasured. `InkeysHeadlessTests` is task-owned and its
  `Cache/` is ignored; `Build/` remains covered by the repository ignore rule.
- Runtime-validation override: on 2026-08-09 the user explicitly authorized
  launching windows, using other timing methods and writing then reading timing
  files. This supersedes the planning-turn GUI prohibition only for controlled
  task validation; each run must keep UI3 animation/effects enabled as required
  and avoid per-frame logging. The later user instruction requires direct process
  termination after measurement, so diagnostics must finish their fixed-sample
  one-shot report first; verify the JSON is complete, then terminate `Inkeys.exe`.
- Dynamic-runtime requirement: the user clarified that static/no-input data is
  only one part of acceptance. Runtime evidence must also exercise main-bar
  buttons and measure animation-producing state transitions. A temporary
  Debug-only driver through the production animation path is preferred for
  repeatability, especially Slider/Fine Dial/preview scenarios; use
  `computer-use` only when it materially helps. Third-light evidence must also
  cover an expanded panel while code drives a repeatable circular mouse path,
  and label every evidence source separately.

### Work Phase 1

- Status: in progress; first buildable extraction slice complete. Broader
  ownership changes, dedicated-animation audit and any atomic reduction have
  not started.
- Build / tests: after CRLF restoration and project/filter registration, full
  `InkeysRepo.sln` `Debug | ARM64` passed in 123.72 s with 297 existing-style
  compiler warnings and 0 errors; the broad rebuild was triggered by the moved
  shared header and is not directly comparable to the 15.30 s incremental
  build (46 warnings, 0 errors) recorded before final EOL normalization.
  `Build/ARM64/Debug/InkeysHeadlessTests.exe` passed both correctness and
  `--benchmark` in three post-fix runs. No GUI was launched.
- Behavior contracts: existing curve/timeline/keyframe and State/Value/Color/Pct
  primitives plus global animation options now live in
  `Inkeys.UI.Bar.Animation`; `Bar.UI` re-exports the module. Common finish,
  interpolation and advance logic is removed from the giant `Rendering()`
  lambda block. Advance reports `{changed, active}` so a visible final change
  is presented and a quantized unchanged intermediate color remains scheduled.
  Tests cover same-target no-op, interruption start capture, exact midpoint,
  final `changed=true/active=false`, disabled hover speed and opacity clamping.
- Measurements vs Phase 0: there is no valid pre-extraction microbenchmark.
  Across three Phase 1 runs, median ranges were: curve 88.20-95.13 ns
  (262-264 thread cycles), same-target no-op 162.81-166.53 ns (475 cycles),
  57 inactive values 5.017-5.071 us (13,707-14,042 cycles), and 57 values with
  one continuously active property 6.022-6.351 us (16,328-16,822 cycles).
  Within-run noise reached 101.88% for short operations and 86.07% for the
  active scan; no optimization conclusion is accepted from these data.
- Decisions / rollback: preserve all existing `IdtAtomic` fields and widget-map
  scans in this slice. The lightweight `IdtAtomic.h` is a semantic move of the
  existing template so the production animation module can compile in the
  isolated harness. The one-active benchmark now alternates target 0/1 after
  completion so measured runs cannot silently degrade into inactive scans.
- Remaining risks: real cross-thread target publication is not yet proven or
  changed; dedicated lighting/Fine Dial/SVG/Word state remains intentionally
  separate; frame calculation, active tracking, wait/shutdown and rendering
  performance remain Work Phase 2+ work. Visual semantics and GUI smoothness
  are `MANUAL / NOT VERIFIED`.

### Work Phase 2

- Status: in progress; wake/request/shutdown investigation is complete. Frame
  attribution and pacing measurements are complete. A reproduced persistent
  visual-demand loop is under attribution; lighting-only and broader cache
  conclusions remain in their own slices.
- Build / tests: two full `InkeysRepo.sln` `Debug | ARM64` builds with the
  ARM64-host MSBuild passed after the wake slice (41.74 s and 41.51 s), each
  with 135 existing-style warnings and 0 errors. The production-module
  headless correctness/stress suite passed once before the generation snapshot
  addition and six consecutive times after it; `--benchmark` also passed. No
  GUI was launched by this slice.
- Findings: the former `WaitFalse(); Store(false)` sequence has a reachable
  clear window: a producer can store `true` after the wait returns and before
  the consumer stores `false`, allowing that consumer store to erase the new
  request. Shutdown could leave the render thread blocked because observing
  `offSignal` did not notify the wait object. The two window-style retry loops
  also ignored `offSignal`.
- Measured hot paths: this slice establishes correctness evidence, not a timing
  optimization conclusion. Stress coverage uses 4 producers and 20,000 total
  notifications; the concurrent snapshot test uses 2 producers and 4,000
  notifications. Notifications may coalesce into one consumer wake while the
  final monotonic generation remains exact.
- Rejected hypotheses: retaining the bool protocol is rejected by the proven
  clear-window interleaving. No polling, extra worker, per-frame logging or
  atomic-field redesign was added.
- Selected Phase 3 changes: use the independent production module
  `Inkeys.UI.Bar.WakeSignal` with mutex/CV-protected monotonic generation,
  `Notify()`, single-consumer `WaitAndConsume()` and thread-safe
  `CurrentGeneration()`. Product and tests compile the same module.
- Remaining risks: actual HWND shutdown remains `MANUAL / NOT VERIFIED` in this
  slice. `CurrentGeneration()` is available for the broader active-render
  domain gate; that gate must compare generations while rendering rather than
  relying only on the idle consume point.
- Real UI baseline: controlled Debug ARM64 runs used QPC aggregation with UI3
  animation and full dynamic edge lighting enabled. `ui3-runtime-baseline.json`
  skipped 60 frames and captured 240; `ui3-steady-no-input-baseline.json`
  skipped 600 and captured 240. The late no-input sample still reported 240
  visual-demand frames, 0 lighting-demand frames and 0 idle waits. Median
  target/layout was 670.8 us, animation 674.0 us, animation scan 114.8 us,
  GetDC 2184.0 us and ULW 1775.3 us. This proves a persistent visual animation
  or target loop; category attribution is required before a cursor-only gate.
- Frame pacing evidence: three raw 101-sample rounds are saved as
  `Build/TestResults/frame-pacing-waitable-run1..3.txt`. Across 8/12/16 ms
  buckets, waitable-timer 0.25 ms P50 noise was 1.12%/0.49%/1.55%, and median
  thread cycles were about 29%-42% below Legacy. Every sample used the
  high-resolution timer with zero fallback/failure on this ARM64 host.
- Persistent-demand attribution: the expanded Debug counter build passed the
  full Solution gate in 46.2 s with 138 existing-style warnings and 0 errors;
  the headless correctness/stress executable reported `PASS animation
  correctness`. `ui3-active-demand-attribution-run1.json` skipped 600 frames
  and captured 120. All 120 frames had `need_rendering_demand`; only the SVG
  map and Word map animation categories were active (120 each), while content
  transition, dedicated/manual animation, sustain, debug and lighting were all
  zero. This narrows the non-settling loop to ordinary SVG/Word properties and
  requires property-level attribution before any work gate is accepted.
- Runtime process handling: the attribution report was parsed and verified at
  `frames_captured=120`, then measured `Inkeys.exe` PID 18776 was terminated
  directly; the process exited and no in-product close path was used.
- Non-settling root cause and fix: ordinary SVG/Word demand came from
  pre-scan animation targets being overwritten by post-scan derived values in
  the same frame. The obsolete PenTypeExtensionArrow geometry/opacity targets,
  ThicknessAnnotationLabel color target and ColorPickerHoldLabel membership in
  the generic theme-word target list were removed; the actual derived geometry,
  opacity and color paths remain unchanged. The intermediate after-fix JSON
  proved SVG demand dropped from 120/120 to zero and isolated the final Word
  conflict before that last one-line exclusion.
- Real idle recovery: after the final conflict fix, the identical
  `skip=600/capture=120` scenario did not produce a report within 25 seconds,
  while the measured process remained alive. During the final five seconds the
  entire process accumulated only 31.25 ms CPU time. The baseline reached all
  720 frames in about 14 seconds, so this is runtime evidence that the renderer
  now settles into its generation/CV wait rather than sustaining 60 FPS work.
  PID 41208 was then force-terminated as required. Full ARM64 Solution builds
  passed with 46 warnings and 0 errors; the headless suite again reported
  `PASS animation correctness`.

### Work Phase 3

- Status: complete; the accepted wake/shutdown, present transaction, pacing,
  dirty-area, curve/string, thickness-resource and exact A8 lighting changes
  passed the Phase 3 build, headless and controlled-runtime gates. Phase 4 and
  Phase 5 remain separate pending work.
- Build / tests: the final full-Solution incremental `Debug | ARM64` build with
  ARM64-host MSBuild passed with 47 existing-style warnings and 0 errors.
  `InkeysHeadlessTests.exe` reported `PASS animation correctness`, including
  wake/concurrency and the dense curve-equivalence coverage.
- Before/after table: before, a new bool request could be overwritten by the
  consumer clear and idle shutdown had no explicit wake; after, each producer
  increments a generation under the same mutex used by the CV predicate, the
  consumer atomically consumes the current generation, and shutdown explicitly
  notifies it. This is a correctness result; no CPU reduction is claimed.
- Retained/reverted optimizations: retained the generation protocol, explicit
  shutdown notify, prompt post-wait exit check and `offSignal`-aware style
  retries. Existing `sustainFlag`, `renderOnceFlag`, rendering cadence and all
  present/frame-pacing behavior are unchanged. No wake change was reverted.
- Remaining risks: real UI wake latency, detached-thread teardown and visual
  behavior remain manual/integration evidence. Generation overflow is limited
  to the theoretical `uint64_t` wrap case and is not reachable in product
  lifetime; the class remains intentionally single-consumer.
- Present/error handling: pending present and last bounds now commit only after
  GetDC, ULW, ReleaseDC and EndDraw all succeed; failures retain pending/full
  dirty recovery state. The headless failure matrix passes. Text-format
  factory/font/HRESULT/null paths now fail safely.
- Pacing decision: after the three-round comparison, the user approved making
  `waitable_timer_0.25ms` the default CPU scheduling strategy. High-resolution
  creation/operation failure falls back to the compatible waitable timer and
  then bounded Legacy waiting. Windows 7 compatible-timer timing and explicit
  timer fault injection remain unverified compatibility risks.
- Validation history: earlier accepted wake/present/pacing slices also passed
  their full Solution and headless gates. The final 47-warning/0-error build
  above is the source-of-truth validation for the complete Phase 3 change set.
- Backend boundary: changing the DX drawing API or rendering backend requires
  separate user confirmation. Preserve Windows 7 SP1 + KB2670838 and the
  D3D11.1/D2D1.1 ceiling; no DX backend change has been made. WARP is the
  user's current preference if a later backend change is proposed.
- Dirty-area root cause and measured result: the manual dirty scan included
  `DrawAttributeBar_ThicknessFineNumber` words that were used only as preset
  dot-opacity inputs. Because those words were not drawn as text, their
  inherited coordinates could remain at the origin and union the lower-right
  Bar with `(0, 0)`. Refreshing the inherited coordinates independently of the
  text draw reduced the paired control run from 4,942,392 to 831,616
  `dirty_pixels/present`, about 83.2%. The drawable-shape filter is also
  retained; neither change alters the rendered content.
- Stable text/layout work: fixed Color Picker footer measurement metrics are
  cached outside the frame loop. This is retained as dependency-stable derived
  data; no final rendered frame is cached and no standalone frame-time gain is
  claimed from this change.
- Animation/string result: the four integer-power curve paths
  (`EaseOutCubic`, `EaseInOutCubic`, `EaseOutBack` and `EaseInOutBack`) replace
  `std::pow` with multiplication. Frozen-formula comparison across 1,001 points
  over `[0, 1]` stayed within `1e-12`. Median thread cycles changed
  `406 -> 178`, `363 -> 186`, `450 -> 241` and `450 -> 234`, respectively;
  the cubic/back range cases changed `1652 -> 1151` and `945 -> 641`.
  Wall-clock benchmark medians remained noisy, so no precise wall-clock gain is
  claimed. Repeated same-value string submissions return inside the existing
  lock before assignment; publication, locking and public APIs are unchanged.
- Lighting/interaction result: under the same combined dynamic scenario, D2D
  P50 changed from `2.564 -> 1.659 ms` and `2.990 -> 1.735 ms` in two paired
  runs, about 35% and 42% lower. Across two rounds of the four-scenario
  thickness steady/transition x control/light matrix, D2D P50 stayed within
  `1.132-1.384 ms`; `ExactFailure=0`, and the unit thickness preview path had
  `PathCreate=0` during both sampled windows.
- Thickness resource result: the two highlighter rounds each recorded
  `GradientCreate=23`, with `GradientHit=97/95` and `GradientEvict=0`. This
  retains the bounded exact-key gradient cache while keeping creation and
  eviction visible in diagnostics.
- Final dynamic rerun: all five scenarios captured exactly 120 frames and
  reported zero present, device and exact-mask failures. The matched
  third-light control/dynamic D2D P50 values were `1.259/1.255 ms`. Every
  measured PID was force-terminated after its report and cleanup marker were
  verified; no in-product close path was used.
- Visual evidence: the DPI-aware physical full-screen capture is `2880x1920`.
  Inspection of the lower-right Bar and expanded thickness/light state found
  no dirty clipping or residual artifact.
- Measurement caution: dynamic total-frame and GetDC/ULW data were materially
  affected by external machine load and capture activity. The
  total-frame figures are therefore not used to claim an accepted improvement;
  the retained decisions use same-scenario D2D pairs, resource/failure counters
  and correctness gates. This warning remains applicable to later comparisons.

#### Phase 3 Decision Table (final checkpoint)

| Disposition | Change / evidence | Correctness gate and rollback point |
| --- | --- | --- |
| Selected / retained | Monotonic generation + CV wake protocol, explicit shutdown wake, present transaction recovery and `waitable_timer_0.25ms` pacing. | Existing correctness/stress and pacing evidence is green; keep compatibility fallbacks. |
| Selected / retained | Cache fixed Color Picker footer metrics and refresh thickness-number inherited coordinates before dirty union. The latter reduced control dirty area by about 83.2%. | Invalidate only from the existing stable dependencies; rollback the individual cache/coordinate unit if visual dirty recovery regresses. |
| Selected / retained | Replace four integer `std::pow` uses with multiplication and short-circuit same-value strings inside their current locks. Dense equivalence, full build and headless tests passed; thread-cycle medians decreased for all measured curve cases. | Preserve the frozen formulas, `1e-12` equivalence limit and existing locking/publication. Wall-clock medians remain noise-limited. |
| Selected / retained | Stable exact A8 rounded-mask cache for repeated lighting geometry. Paired combined-scenario D2D P50 fell about 35%/42%; 2x2 and final scenario runs had zero exact failures. | Keep bounded memory/eviction, device-loss invalidation and production fallback; later visual or failure regression rolls back this cache as one unit. |
| Selected / retained | Unit hard-pen preview path and bounded exact-key highlighter gradient cache. | Sampled `PathCreate=0`; gradient rounds recorded 23 creates, 97/95 hits and zero evictions. Preserve the original fallback and device-resource reset. |
| Deferred | Cursor-only target/layout gating, property registry/atomic reduction and broader ordinary-animation gating. | Revisit only after normal versus cursor wake sources and continuation/invalidation boundaries are explicit; a normal UI request must always dominate a cursor-only fast path. |
| Rejected | Lowering animation FPS, disabling custom animations/lighting, caching final light frames, or using an unconditional full-size mask to hide misses. | These change visible behavior or trade unbounded work/memory for benchmark results and violate the acceptance contract. |
| Rejected / not authorized | DX drawing API/backend switch, including an implicit WARP migration. | Requires separate user confirmation and must retain the Windows 7 SP1 + KB2670838, D3D11.1/D2D1.1 compatibility ceiling. No such switch is part of this phase. |

### Work Phase 4

- Status: complete; Phase 3 performance behavior and raw outputs remained
  frozen through the structural move. Work Phase 5 full-scope safety review is
  now in progress.
- Build / tests: after the final rendering, render-loop, interaction,
  initialization and content-timeline batches, full `InkeysRepo.sln`
  `Debug | ARM64` with ARM64-host MSBuild passed in 225.16 seconds with
  0 errors. Warning output remained in the existing numeric-narrowing and
  bundled `hashlib++` categories. The freshly built headless executable passed
  correctness once, passed 20 consecutive concurrency/animation repetitions,
  and its retained `--benchmark` run ended with `PASS animation correctness`.
- New responsibility map: `Bar.Main.cpp` is the coordinator; device resources,
  caches and D2D helpers live in `Bar.Rendering.*`; the six-stage frame flow is
  in `Bar.RenderLoop.cpp`; stable layout helpers live in `Bar.Layout.cppm`;
  pointer/capture handlers live in `Bar.Interaction.cpp`; topology and startup
  construction live in `Bar.Initialization.cpp`. The complete Solution build
  proves the final module graph and project registration are valid.
- `Buttom -> Button` search result: `Bar.Bottom.cppm` and `Bar.Buttom.cpp`
  proved to be the interface/implementation pair and are now
  `Bar.Button.cppm/.cpp`. First-party `Buttom|buttom|Bar.Bottom|:Bottom` residue
  is zero; genuine `BottomSide_*` direction names remain unchanged. The ten
  touched files preserve their original encoding/EOL, including the UTF-8 BOM
  and CRLF contract of `IdtPlug-in.cpp`.
- Temporary diagnostics removal: every product-path metrics collector,
  scenario hook and output branch was removed; a repository search found no
  product diagnostics residue. `InkeysHeadlessTests` is retained as a separate
  regression project and imports production animation/wake/pacing contracts
  without adding product hot-path or Release overhead.
- Remaining risks: the final structure review found and fixed stale content
  transition publication with a mutex-backed monotonic generation, and the
  lifecycle review fixed synchronous Setting title-bar dragging so stop and
  `offSignal` are observed with an 8 ms bounded wait. Real HWND shutdown,
  Raw Input/timer/capture behavior and visual equivalence remain Work Phase 5
  integration/manual evidence.

### Work Phase 5

- Status: accepted on 2026-08-11. Engineering checks and user-reported manual
  acceptance are complete; the task is ready for the Phase 3.4 work commit and
  Trellis archive flow.
- Final build / tests / static analysis: full `InkeysRepo.sln` `Debug | ARM64`
  with ARM64-host MSBuild passed in 40.36 seconds of MSBuild time (40.71
  seconds outer wall time), with 147 warnings and 0 errors; raw output is
  `Build/TestResults/ui3-p5-final-build.log`. Full-Solution `/analyze` passed in
  5:14.44 of MSBuild time (314.78 seconds outer wall time), with 395 warnings
  and 0 errors, three fewer than the pre-fix 398-warning run; the four affected
  files called out by the review have no analysis warning. The permanent
  headless suite passed 20/20 repeated correctness/concurrency runs, and the
  retained final benchmark exited 0 with `PASS animation correctness`.
- Final scope/structure check: `git diff --check` passed. Current product/spec/
  project/filter sources contain no first-party `Buttom` residue and no
  temporary scenario driver or metrics output hook. The 25 Bar compilation
  units match disk, `Inkeys.vcxproj` and `.filters` exactly; both new headers
  and the HeadlessTests Solution/ARM64 configuration are registered. All 52
  changed text files are valid UTF-8 with no mixed EOL, all scanned source
  files remain CRLF, and tracked-source BOM state matches HEAD. The two global
  duplicate `.filters` entries are confirmed HEAD baseline outside this task.
- Trellis validation: current task/source resolution passed, both 12-entry
  context manifests passed, and the full-scope check completed. The validator
  warns that `rendering-and-ui.md` exceeds the 32 KiB automatic-injection cap;
  this review read the complete file directly, so no check content was omitted.
- Correctness findings and fixes: rendering now starts only after a valid HWND;
  HiEasyX message select/copy/delete is one locked transaction; cursor reads
  check `GetCursorPos`; cross-thread button `state`/`emph` use `IdtAtomic`;
  idle wake rebases the animation clock before advancement; and a failed frame
  gradient creation is not retried every frame within the same device
  generation. The deterministic idle-rebase regression and the complete
  headless suite cover the new contracts.

#### Phase 5 Final Benchmark Table

| Evidence | Before / control | Final | Decision |
| --- | --- | --- | --- |
| Persistent idle demand | 240/240 late baseline frames still demanded rendering and never waited | Identical post-fix run settled before completing the fixed frame sample; final 10.042-second idle sample used 265.625 ms CPU (`2.6451%` of one core, `0.2204%` machine) | Renderer now returns to generation/CV sleep when UI3 is visually settled. |
| 16 ms pacing, same final run | Legacy 1.50 ms: P50 `16.0500 ms`, P95 `30.6311 ms`, median `214,569` thread cycles | Default waitable 0.25 ms: P50 `16.0776 ms`, P95 `16.5788 ms`, median `101,184` cycles | Retain waitable timer; 101/101 samples used the high-resolution timer with zero fallback/failure/unavailable. |
| Thickness dirty region | `4,942,392` pixels/present | `831,616` pixels/present | Retain inherited-coordinate refresh and drawable filter; about `83.2%` less dirty area. |
| Combined lighting D2D P50 | Paired controls `2.564 ms` and `2.990 ms` | Paired optimized `1.659 ms` and `1.735 ms` | Retain exact A8 mask cache; about `35%` and `42%` lower in matched runs. |
| Third-light final control/dynamic | Control `1.259 ms` D2D P50 | Dynamic `1.255 ms` D2D P50 | No measurable third-light penalty remained in the final matched sample; no visual effect was disabled. |
| Integer-power curve cycles | Ease-out/in-out cubic/back medians `406/363/450/450` | `178/186/241/234` | Retain multiplication forms; 1,001-point frozen-formula error stayed within `1e-12`. |

Final raw pacing/animation output is
`Build/TestResults/ui3-p5-final-benchmark.txt`; idle evidence is
`Build/TestResults/ui3-p5-idle-cpu.json`. The physical full-screen evidence is
`Build/TestResults/ui3-p5-final-physical-full.png` at `2880x1920`. All measured
Inkeys GUI processes were terminated directly by their exact PID after their
fixed samples were complete.

#### Phase 5 Safety Finding Table

| Finding | Disposition | Remaining boundary |
| --- | --- | --- |
| Invalid/late Bar HWND could reach startup threads | Fixed by validating `floating_window` before Hook/Rendering/Interaction start | Real multi-monitor/window teardown remains manual. |
| HiEasyX message vector escaped its lock while producer and consumer mutated it | Fixed by selecting, copying and deleting under one lock | No queue race reproduced by the final review. |
| Button `state`/`emph` and failed `GetCursorPos` handling | Fixed with `IdtAtomic<enum>` and checked, zero-initialized cursor reads | None identified in the audited path. |
| Idle sleep time could advance the first animation frame by up to 50 ms | Fixed with post-wake animation-clock rebase and deterministic regression test | Manual wake smoothness remains to be judged visually. |
| Failed point-gradient creation retried per frame | Fixed with device-generation-scoped failure suppression while preserving cached gradients and solid fallback | Device-loss/recovery visuals remain manual. |
| `stateMode` fields are ordinary storage shared by Interaction, Draw2 and RenderLoop | Deferred; a correct fix requires one coherent snapshot/ownership protocol, not a one-sided atomic patch | Track as a separate cross-thread state task. |
| Main-button drag path intentionally continues cursor polling | Deferred; no fixed sleep was added without input-latency evidence | Measure drag latency before changing sampling cadence. |
| CLR/native `PptInfoState.TotalPage/CurrentPage` synchronization | Existing baseline risk outside the UI3 Bar scope | Verify through real PowerPoint interop and address separately. |

- Backend boundary: no drawing API/backend change was made. D3D11.1/D2D1.1
  and the Windows 7 SP1 + KB2670838 ceiling remain intact; any later WARP or
  backend migration still requires explicit user confirmation.
- Manual GUI checklist (`PASS`, user-confirmed on 2026-08-11):
  - [x] On Windows 11 ARM64, compare main-button expand/collapse, main-bar mode
    switches, custom curves and rapid reversals with Inkeys2 for subjective
    smoothness and input latency.
  - [x] With the third light enabled and the thickness panel expanded, move in
    circles and repeatedly switch preview <-> Slider <-> Fine Dial; confirm the
    previously observed lighted thickness-transition hitch is gone.
  - [x] Exercise Fine Dial inertia and its threshold with mouse, pen and touch;
    confirm no missed final value or unexpected continued rendering.
  - [x] Exercise device loss/recovery, DPI/monitor changes and theme changes;
    confirm masks, gradients, old dirty bounds and text all recover without
    clipping or stale light.
  - [x] Verify Windows 7 SP1 + KB2670838 compatible-timer fallback on real
    hardware while retaining the D3D11.1/D2D1.1 compatibility boundary.
  - [x] Verify real PowerPoint page-state updates and app/window teardown, then
    report whether UI3 is at least as responsive as Inkeys2.
