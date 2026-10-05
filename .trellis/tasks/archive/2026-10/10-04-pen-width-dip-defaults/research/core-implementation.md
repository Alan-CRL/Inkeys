# Core implementation handoff

Active task: `.trellis/tasks/10-04-pen-width-dip-defaults`.
Baseline from main: `01a953d41adb5084e25899592cdde3bcb6c4770d`, branch `chore/publish`. Existing workspace changes preserved.

## Ownership and scope

- `Inkeys/Inkeys/Business/PenWidthPolicy.hpp`: single preset/continuous DIP-to-canvas policy, safe integer compatibility adapter.
- `Inkeys/Inkeys/Business/PenWidthPreferences.hpp`: JSON schema/selection decoder and encoder, legacy ordinary pixel migration and priority.
- `Inkeys/IdtState.h/.cpp`: per-slot identity, same-generation target Context, validated double explicit setters, preset/revision commits, context-only re-resolution, locked startup restoration, no-window state probe.
- `Inkeys/IdtConfiguration.h/.cpp`: real startup restoration, migration/version/provenance, atomic memory writes retaining unrelated data, future schema protection under AutoClean, no-window production preferences probe.
- `InkeysHeadlessTests/pen_width_tests.cpp`: production policy/codec tests and metrics CSV (no reimplemented conversion formula).

No main/Bar/Draw3/Setting/project/spec changes by this worker. No build, GUI, commit, push or archive initiated. Main owns integration and complete Solution build.

## Public contract

Namespace `Inkeys::Business::PenWidth` contains `Family{Brush,Highlighter,Laser}`, `Source{Preset,ExplicitDip}`, `Preset{Thin=0,Medium=1,Thick=2}`, `Selection{source,preset,double explicitDip}`, `Context{revision,paintDevice,effectiveDpi,pixelWidth,pixelHeight}`, and `Metrics{resolvedWidthDip,idealCanvasWidthPx,appliedCanvasWidthPx}`.

`Resolve(selection,family,context)` owns automatic device presets. `CanvasWidthPx(double,context)` owns continuous canvas conversion with final safe positive float range; `RoundPositiveCanvasPx(double)` is an integer compatibility adapter only. Current Draw3 uses continuous applied=ideal, retaining soft stroke float radii/pressure/prediction. UI integer representation must never become authority. Invalid DPI (nonfinite or outside [1,1536]) falls back to96. Setter/codec reject nonpositive/nonfinite/over65536 DIP and values that underflow the retained float runtime cache. Canvas output is bounded1048576 and positive float representability; integer half-up adapter saturates before narrowing. Extremely small unrepresentable values are invalid, distinct from valid classroom sub1 DIP.

State `snapshot.penWidthContext` and every width `slot.widthSelection` reside in the existing mode lock. `GetPenWidthMetrics(snapshot)` resolves from that value only. Existing float `slot.width` is a compatibility cache. Bridge publication takes authoritative double metrics and Context in the same lock. `GetPenWidthSelection/GetPenWidthFamily/GetPenWidthMetrics(snapshot)`, `SetPenWidthPresetIfRevision`, `ApplyPenWidthContext`, `RestorePenWidthSelections` are declared in `IdtState.h`. Ordinary hard/soft use Brush1; shapes retain independent line/rectangle slots. Laser constructor cache presets derive from the same policy.

Width commits advance state revision and reject stale gestures. Semantic equivalent display refresh is a no-op; changed Context only re-resolves Preset and preserves ExplicitDip. Device settings through SyncDraw3State change only device under the same lock, avoiding overwriting new target DPI with an earlier independent getter snapshot. No display update saves preferences. Active stroke capture is Draw3-worker-owned.

## Startup/configuration/migration

Main calls `InitializePenWidthPreferences(Context)` after ReadSetting and target discovery, before WriteSetting and first input admission. Priority is valid enabled memory, then explicit startup with automatic defaults disabled, then Medium identity. Reentering Initialize does not restore defaults; normal tool/PPT switches do not call restoration. MemoryColor default remains existing false.

`deploy.json` known root `PenWidth` has Version1/UnitDIP, MemoryWidth/MemoryColor/AutoDefaultWidth, and Startup per Brush1/Highlighter1/Laser. `memory.json` corresponding root uses Selections. Selection objects encode SourcePreset+Preset integer or SourceExplicitDip+Dip double; no resolved pixels persist as truth.

Legacy startup keys are only historically confirmed `Preset.MemoryWidth/MemoryColor/AutoDefaultWidth/DefaultBrush1Width/DefaultHighlighter1Width`. Explicit3 or35 is not guessed as factory. Legacy ordinary/highlighter raw numeric widths are pixels; missing source DPI uses ready target scale exactly once. Migration retains the complete legacy object, Estimated=true, EstimatedSourceDpi and explanatory note. Legacy memory Draw.Width raw fields remain alongside known new schema; optional Laser old memory values remain DIP. No invented legacy startup Laser field is read. New version never divides again. Invalid/missing slots independently fall back; unknown/malformed marked schemas are opaque and survive ordinary save and AutoClean.

Memory reader uses existing bounded parser, refuses malformed/reparse/directory source; memory save reuses existing WriteAtomically and valid-or-absent safety rather than truncate. Save failure leaves disk original and runtime already-resolved values intact. Root fields and raw legacy values survive atomic migration. Startup/default authority is separate from temporary current-session memory.

## Test coverage and verification status

- Headless entry `RunPenWidthTests()`: Brush ordinary96/120/144/192/264 DPI integer matrix; ordinary Highlighter16/24/36 DIP; classroom1080/1440/2160 both families across every scale; portrait/non16:9/invalid output;1440p275% precision; half-up/odd widths; identity collisions; numeric underflow/nonfinite/overflow; legacy3 preservation, fractional pixel and Laser migration; priority; roundtrip/idempotence; future/malformed/partial schema. Emits production-resolved CSV for audit.
- `RunPenWidthPreferencesProductionTest()` creates an owned independent temporary directory and exercises actual ReadSetting/Initialize/GetMemory/WriteSetting/SetMemory: fresh/first Medium, partial legacymemory, one-time and portable reparse, memoryoff explicitstartup versus temporary edits, preset memory on another target, deterministic blocked replacement preserving original, future schema with AutoClean, malformed file refusal. Does not create HWND. Cleans only a directory it successfully created.
- `RunPenWidthStateProductionTest()` exercises actual state APIs: classroom fractional Medium, selection identity, stale revisions, frozen Context snapshots, explicitDIP across DPIchange, equivalent refresh revision stability, bad setter rejection, sharedhard/soft and Laser5.
- Actual commands already run by worker: `git diff --check -- Inkeys/IdtState.cpp Inkeys/IdtState.h Inkeys/IdtConfiguration.cpp Inkeys/IdtConfiguration.h` (exit0); Python byte audit confirms original BOM and CRLF, no loneLF in touched files. These are static checks, not compiled runtime success.
- Main must run complete Solution then headless `--no-window` (or focused `--pen-width-only --no-window`) and `Inkeys.exe --pen-width-production-test` plus existing state/UI/Draw3 probes. Worker has not run or claimed those tests.

## Remaining acceptance and rollback

Real touch/PPT/mixedDPI/Win7 remains manual. Classroom uses full target output, cannot recognize PPT black bars. Missing historical source DPI is a documented upgrade-device appearance estimate, not recovered portable-original DIP. Revert only this task delta and use retained raw/provenance for manual old-config recovery; never reset unrelated changes. Independent final review and main build remain pending at handoff.
