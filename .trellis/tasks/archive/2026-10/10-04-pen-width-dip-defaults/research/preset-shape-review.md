# Independent review: preset matching and shape widths

Active task: `.trellis/tasks/10-04-pen-width-dip-defaults` (in_progress). Baseline `f8054a227174707a984291d875b4fcc6fadf605f`, branch chore/publish. Role: dispatched trellis-check; no recursive agents, full builds, GUI, commit or archive by reviewer.

## Context and scope

Read native hook saved output, full on-disk oversized Draw3 integration content and non-inlined JSONL contexts; task PRD/design/implement and latest preset-shape-scope/research are authoritative. Loaded trellis-check skill and actual package discovery; production changes affect only native-desktop Bar Layout helper and existing Interaction CPU probe. Native desktop index has no separate Quality Check section; its gates, C++ snapshot/revision, shared width policy, configuration and build contracts applied. Native/demo Quality Check cannot substitute demo builds for the actual product.

## Findings (fixed)

- File: Inkeys/Inkeys/UI/Bar/Bar.Layout.cppm
- Issue: abnormal extreme output dimensions make the shared policy fall back multiple presets to exactly the same DIP. Initial ExplicitDip matching could then highlight several buttons.
- Fix: after validating Selection and preserving Preset identity priority, ExplicitDip checks Thin to current index and returns true only for the first strict match. Main confirmed this derived UI priority; policy/state/rendering/precision remain unchanged. Added concise Chinese comment.
- File: Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp
- Issue: the production regression probe did not cover duplicate actual fallback DIP.
- Fix: use INT_MAX full output size and the real GetBarThicknessPresetMetrics to assert Brush/Highlighter duplicate fallback. ExplicitDip selects only Thin and retains its source/value; Preset Thick still selects only Thick. No copied conversion formula or production interface changes.

## Findings (not fixed)

No unresolved code findings in this increment. Real touch/visible rendering and mixed DPI hardware remain manual verification boundaries; no GUI was opened. Strict matching intentionally does not snap continuous Slider values merely because rounded labels are equal.

## Code-path verification

- Valid Preset identity wins; invalid source/value/enum/index selects none. ExplicitDip uses the active slot, family and Context in the same immutable StateMode snapshot, preserving double value. Tolerance is exactly 8*double epsilon*max(abs(value),abs(target)), with no absolute floor, integer label or quantized px comparison.
- Brush hard/soft shared width, Highlighter and Laser family presets remain unchanged. Shape StraightLine/Rectangle slots stay independent. Existing geometry initialization has empty readouts, click handlers call real SetPenWidthPresetIfRevision, number/miniature rendering use Brush GetBarThicknessPresetMetrics/GetBarThicknessPresetPx and active shape Selection; no extra value table needed.
- Actual Slider/touch release commits through SetPenWidthIfRevision, clears candidate and calls UpdateRendering() with state refresh; FineDial uses its existing successful/cancel/stale revision paths. Draw and Geometry layout and hover use the same selected helper. Selected color/opacity targets are advanced by existing animation and dirty-region tracking, so no additional wake loop or state write was added.
- New CPU regressions execute actual FineDial Begin/candidate/Commit(false) from a nonpreset width to presets, covering four tool families, ordinary/classroom contexts, fractional 1440p 275%, three shape variants and independent slots. They also check strict double/float-error boundary, equal rounded text, quantized pixel collision, sub1, malformed values, cancel, stale revision and display change without changing ExplicitDip.
- Scope/EOL: only two production source files changed. Current Layout retains UTF-8 BOM and Interaction no BOM; both CRLF with no bare LF. git diff --check on both exited 0. Reviewer independently hashed all 41 preexisting dirty paths against preset-shape-baseline.json: all unchanged before journal append.

## Verification

- Lint/whitespace: PASS (`git diff --check -- Inkeys/Inkeys/UI/Bar/Bar.Layout.cppm Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp`). No independent C++ lint/typecheck system exists; full Solution compiler is the type gate.
- Initial main-owned full InkeysRepo.sln Debug|ARM64 build: exit 0, 88.50s; initial --pen-width-production-test: exit 0, .444s. These precede the review fallback fix.
- Final TypeCheck/compiler: PASS. Main-owned final complete InkeysRepo.sln Debug|ARM64 build exit 0, 88.313s. Actual log compiles Bar.Layout.cppm and Bar.Interaction.cpp, links Inkeys.exe, and builds dependent projects/headless. Native ARM64 MSBuild was discovered by vswhere; same invocation normalizes PATH and MSBUILDDISABLENODEREUSE, uses command-only LinkIncremental=false, /m:1 /nr:false /v:minimal. Existing thirdparty/Setting warnings and normalized PATH pwsh fallback output are unrelated; no source errors or warning suppression introduced.
- Final Tests: PASS, verified final JSON exit codes and key output independently: --pen-width-production-test exit 0 (1.090s), --state-mode-production-test exit 0 (.709s), --draw3-pen-width-test exit 0 (.747s), --bar-eraser-offscreen-test exit 0 (16.358s), InkeysHeadlessTests.exe --no-window exit 0 (57.945s). Width probe executes the new same-DIP fallback fixture; state stderr reports cross-entry/revision and FineDial PASS; Draw3 stderr retains resolved 1.939393939394 DIP / ideal/applied 5.333333333333 px / integer adapter 5; headless ends with pen-width policy/codec PASS, eraser layouts failures=0 and PASS animation correctness; offscreen PageControl reports failures=0. Exact argv/cwd/seconds and streams are preset-shape-*.result.json and adjacent logs.
- Final format/scope evidence: preset-shape-format-check.json records original41 unchanged, both BOM preserved, zero bare LF, diff_check_exit0. Reviewer repeated two-source diff check after final build: exit 0. Resources, configuration, Draw3 runtime/history, engineering and other task files untouched by this increment.
- Final independent gate: PASS for the authorized code and no-window automatic scope. No unresolved code findings after the local repair. Real hardware touch/visible highlight remains manual verification; task remains in_progress, without commit/push/archive.
