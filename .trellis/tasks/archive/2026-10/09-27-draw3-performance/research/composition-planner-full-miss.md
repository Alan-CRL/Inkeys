# Research: Composition planner full-cache miss

- Query: Find one remaining production Draw3 hotspot measurable without HWND, with a minimal safe optimization and comparable baseline.
- Scope: internal, read-only code analysis; production edits wait until UI stages finish.
- Date: 2026-10-03
- Task: Parent explicitly confirmed this Draw3 task as the research destination despite the native hook naming the UI state task.

## Findings

### Files found and production reachability

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.InkHistory.cpp:1290-1338`: production `CompositionCachePlanner::Acquire`; key hit lookup, lowest-free-slot search, then unpinned LRU eviction.
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.InkHistory.cppm:305-340`: planner owns only CPU residents/use clock; pinned slots survive budget shrink. Default capacity is 512.
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.InkHistoryGpu.cpp:920-1035`: actual `GetOrBuildNode` calls planner Acquire on every cached-node hit (`:933-934`) or composition-node miss (`:965-967`). Recursive children remain pinned until parent composition completes (`:952-960`). This is rendering-thread CPU work preceding raster/composition; not document-build time.
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.HistoryProbe.cpp:22-23,31-33,399-411`: existing production-module CLI with 3 warm-up/11 measured blocks; currently does NOT exercise the planner. Add a narrow local scenario here, retaining existing scenarios/output.
- `Inkeys/IdtMain.cpp:1123-1125`: exact `--draw3-history-benchmark` early-return entry before ordinary application startup.
- `inkStrokeModelerTestTests/ink_history_tests.cpp:472-520`: existing standalone-demo LRU/pin tests are useful scenario references, but do not establish correctness of the integrated production module.

### Best minimal candidate

At full capacity C, `Acquire` scans candidate slots 0..C-1, and each `std::any_of(entries_)` repeats a resident traversal (`Draw3.InkHistory.cpp:1307-1317`). With one resident per slot this performs C(C+1)/2 slot comparisons (131328 at C=512) even though there cannot be a hole. It subsequently scans residents again to select the LRU victim (`:1322-1333`). Capacity pressure, page replay and cache-invalidating changes can repeat this on many nodes. Actual frequency and cost have not been measured here.

Minimal production change, only inside Acquire: if `entries_.size() >= representableCapacity`, count residents whose `slot.value < representableCapacity`; only if that count equals capacity, skip the existing candidate/free-slot loop and go directly to its unchanged LRU branch. Leave partial occupancy/hole behavior intact. This removes the quadratic search from full-cache misses without a new allocation, field, index or persistent bookkeeping. Hit path returns before the new check. The full path becomes linear; partial filling remains quadratic and is explicitly outside this first optimization.

Do NOT infer fullness from `ResidentCount >= capacity` alone: after shrink, pinned residents in slots beyond capacity survive, while usable low slots can have holes (`TrimToPolicy :1256-1287`). Count only usable slots. Resident slots are unique because allocation checks occupancy and eviction removes the previous owner before reusing its slot. Preserve lowest-free-slot selection, use-clock updates, victim key/slot, pin counts and all status values. Add a short Chinese comment explaining retained high pinned slots. Do not touch the GPU allocator, raster operations, upload, shaders, input or saving.

### Narrow baseline seam and correctness cases

Inside the existing HistoryProbe anonymous namespace, add a small planner scenario block called from `RunDraw3HistoryBenchmark`. Import already exists. Keep fixture construction, expected results, correctness checks, digest and output outside timing. Use the existing 3/11 block convention and 3 serial processes; report raw block nanoseconds, operation counts, capacity/residents/pins and deterministic status/slot/evicted-key digest. A block timer around a fixed batch avoids per-call clock overhead. No HWND/D3D/worker is needed.

1. **Full miss**, capacities 128 and 512: fill keys 1..C, pin key 2, touch key 1; time 256 new keys C+1 onward. Expected eviction order starts 3..C, then 1, then previously inserted new keys, never 2. Expected reusable slot follows each victim; residents stay C, pin count 1. Precompute expected results outside timing. Add full-cache hit batch as a non-regression control (no production new work on hits).
2. **Released hole**, C=512: prepare 64 independent full planners outside timing, Release key 257; time one Acquire of key 513 for each. Every result must allocate slot 256, have no eviction and restore resident count 512. This guards preserving lowest-slot behavior and measures fallback overhead; no artificial repeated fixture construction inside timing.
3. **All pinned**, C=128: pin every resident; batch new-key misses must return `AllUsableSlotsPinned`, no evicted key, unchanged residents/pins. This is another full-cache case with the expensive old scan but no legal victim.
4. **Shrink with high pinned residents and usable holes**, tiny untimed invariant: C=4, fill keys 1..4 (slots 0..3), pin keys 3 and 4, Release key 1, shrink to C=2. Trim retains pinned high-slot keys 3/4 but evicts key 2 in its total-count loop. Residents are keys 3/4, count 2 >= capacity 2, while slots 0/1 are both free. Acquire key 5 must allocate slot 0 without eviction. Unpin key 4 must evict its high slot; unpin key 3 must evict its high slot; key 5 remains. This directly catches an incorrect size-only fullness shortcut and verifies existing over-budget pin semantics.
5. **Disabled/pinned shrink**, tiny untimed invariant: set budget zero while a key is pinned; Acquire returns Disabled without eviction; Unpin releases the out-of-budget key through existing Trim behavior.

Baseline must run AFTER the probe seam is built but BEFORE changing Acquire. Keep the exact same seam/build configuration for after. Do not label old HistoryProbe output as a planner baseline: it never calls Acquire.

Sampling command shape (parent owns execution):

```powershell
$plannerExe = (Resolve-Path 'Build/ARM64/Release/Inkeys.exe').Path
foreach ($plannerRound in 1..3) {
    $plannerProcess = Start-Process -FilePath $plannerExe -ArgumentList '--draw3-history-benchmark' -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput ".trellis/tasks/09-27-draw3-performance/research/planner-before-$plannerRound.out" -RedirectStandardError ".trellis/tasks/09-27-draw3-performance/research/planner-before-$plannerRound.err"
    if ($plannerProcess.ExitCode -ne 0) { throw "Planner baseline exit $($plannerProcess.ExitCode)" }
}
```

Use `after` filenames for optimized binary. Ensure binary rebuilt from current source, retain toolchain/config identity, check stderr/exit code and result digest equality; report median/P95 of BLOCK cost and round-to-round spread. Debug build is required correctness validation; same-configuration Release is preferable for performance. Reuse full `InkeysRepo.sln` and ARM64-native MSBuild plus same-invocation PATH normalization; parent owns any builds. No build or CLI was run by this research agent.

### Related specs and references

- `.trellis/spec/native-desktop/draw3-integration.md:64-65`: Draw3 thread ownership, separate device; CPU history and final GPU/present must remain separate evidence.
- `.trellis/spec/native-desktop/cpp-conventions.md`: minimal diff, encoding/EOL, concise Chinese comments.
- `.trellis/spec/native/quality-and-validation.md:1-22`: integrated production module versus standalone demo, scope discipline, shader contract boundary.
- Existing `history-resource-cost.md` and `history-benchmark-review.md`: reused planner observation and no-HWND CPU scope, not repeated audits.
- External references: none needed; conclusions use current local production source only.

## Caveats / Not Found

- No measured gain yet. Full-cache miss CPU improvement is a candidate, not an established visible-performance result; withdraw if same-scenario benefit is within noise or results differ.
- This CLI cannot measure real miss frequency, GPU waits/upload/composition duration, input-to-frame latency or successful Present. It reports `hwnd=0 gpu=0 present=0`; successful displayed frames are not applicable. Do not extrapolate benchmark speedup to frame rate or first-stroke latency.
- Partial-fill quadratic cost, linear FindResident, TrimToPolicy and GPU costs remain unchanged. Do not expand into a general cache rewrite merely to improve those too.
- No ULW fused-copy or Laser range-planner candidate is revisited. Win7 ULW remains deferred. No production edit, build, CLI run, GUI or git operation occurred in this analysis.
