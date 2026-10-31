# Independent implementation review

- Active task: `.trellis/tasks/10-04-pen-width-dip-defaults`
- Role: existing dispatched `trellis-check`; no recursive delegation, complete builds, GUI tests, commits or task archival.
- Baseline: `01a953d41adb5084e25899592cdde3bcb6c4770d`, `chore/publish`. The 41 pre-existing paths in `research/baseline.json`, including Win7 presenter/pixel diagnostics, were excluded from this task's findings.
- Source review: **PASS after the local fixes below**. Final available automatic quality gate: **PASS**, based on the post-fix records inspected below. Real-device/manual acceptance remains pending; this document does not substitute source reading for runtime results.

## Findings fixed

### F1 — width text/interaction preparation narrowed DIP before presentation

- Files: `Bar.State.cpp/.cppm`, `Bar.Main.cpp`, `Bar.RenderLoop.cpp`.
- The state display overload still accepted float and the no-argument overload explicitly narrowed resolved double. The regular Bar update called the float legacy getter. RenderLoop initialization and FineDial fallback/visual publication also retained unnecessary float conversions.
- For valid `65535.999 DIP`, narrowing to float can produce exactly `65536`, incorrectly removing the approximation marker. A presentation refresh must not discard the logical precision used to decide whether a label is approximate.
- The display overload now accepts double, all relevant presentation/preparation sites use `GetPenWidthMetrics(snapshot).resolvedWidthDip`, and the existing double FineDial visual mailbox retains double. Legacy float width caches and diagnostic `penWidthBits` remain compatible caches rather than renderer/config authority.
- Added a headless production formatter assertion for the large fractional value. Existing production Bar/State tests exercise the shared metric path and compile the changed module interface.

### F2 — preview overflow compared logical DIP with physical pixel capacity

- File: `Bar.RenderLoop.cpp`.
- `previewOverflow` compared `GetPenWidth(stateMode)` against `expandedPreviewCapacity` already multiplied by layout `frameZoom`. After the unit conversion, these are DIP and pixels respectively.
- It now compares the existing authoritative `GetBarCurrentPenThicknessVisualWidth(..., snapshot)` canvas-pixel result against the pixel capacity. Thus `30 DIP` at 200% correctly yields `60 px` and exceeds a `50 px` preview capacity.
- Added a real Bar production-helper regression asserting `60 px`, overflow against `50 px`, and independence from custom Bar layout zoom.

### F3 — envelope text measurement reused an obsolete clamped integer cache key

- File: `Bar.RenderLoop.cpp`.
- The interaction envelope measured the new formatted DIP text but cached it by `round(clamp(width,0,999))`. Different legal large widths and exact/approximate labels could share the same key while requiring different text bounds.
- The measurement cache now keys on its actual formatted text, matching the existing popup measurement strategy. A short Chinese comment records why the integer key is unsuitable. No animation or interaction model changed.

### F4 — new FineDial production assertion expected a value outside its real range

- File: `Bar.Interaction.cpp`, newly added test only.
- The final width probe correctly passed preference and state tests but failed `fine-dial-negative-half-step`: its 96-DPI range minimum is 1 DIP, while the test expected `1.93939 - 1 = 0.93939`. The production endpoint clamp was correct.
- The assertion now expects `max(range.min, origin - 1)`, with a Chinese boundary comment. Additional real-session positive and negative half-step assertions use `origin + 2` inside the range to verify symmetric steps retaining the origin fraction. Each session is cancelled before attempting an empty commit; production algorithms are unchanged.

All local edits preserved existing UTF-8/BOM and CRLF bytes. `git diff --check` exited 0 after the fixes. Source modifications were paused during the first complete build and again immediately on notification of the second build; main was told that the last envelope edit must be included in a later build if its source scan had already occurred.

## Findings not fixed

No outstanding source correctness blocker found within the authorized scope. The following are evidence limits and remaining acceptance activities, not claims of failures:

- Actual touch/hold/FineDial feel, live PPT/WPS, mixed-DPI monitor handoff and Win7 compatibility require the main session's manual checklist. Visible Host tests were not run under this task's GUI restrictions.
- The finite UI presentation diagnostic signature retains its pre-existing 32-bit width cache plus tool revision. That diagnostic cache is not used to resolve, save or render width; changing its public proof schema would be outside this unit contract and was not done.
- Historical geometry is verified by source tracing and existing/new no-window history/persistence tests where executed. The new width probe's small CPU Undo/Redo assertion alone cannot establish all UInk recovery behavior; existing production persistence probes remain necessary regression evidence.

## Cross-layer review confirmation

- `PenWidthPolicy.hpp` is the sole source for ordinary 1/3/6 DIP, highlighter 16/24/36 DIP, large-screen rotated full-output 1080p policy, and Laser 3/5/7 DIP. It validates finite/positive DIP, float-underflow feasibility and documented caps before conversion. Its integer adapter is separate from continuous Draw3 rendering.
- Selection is authoritative Preset identity or ExplicitDip. The State snapshot contains Context and all selections under the existing state lock. Setters re-resolve caches, increment revision, unlock, then perform memory/bridge effects; stale gesture setters reject old revisions. Device updates change only the device field under the same lock. Equivalent display contexts are inert.
- Bridge/Product merging and WindowController style snapshots carry double DIP together with Context. `CanvasDiameterForTool` performs the single policy conversion and only then narrows physical canvas pixels to float. Down captures style/context before resetting nominal width. Per-point model/pressure/soft smoothing, existing history/modeler/UInk geometry and eraser configuration remain continuous physical geometry.
- Host consumes dirty display state using one snapshot and the attached Drawpad monitor's complete output dimensions. Synchronous callback and bridge pump occur before graphics-ready notification enables RTS. Product startup restores/migrates preferences before configuration cleanup, using the ready Primary snapshot, and the initial Host callback republishes even when context is equal after Host reset. Ordinary preview and drawing consume the same state target, not Bar DPI.
- Laser nominal width uses one DPI conversion; renderer glow/bounds use each captured visual style's target scale. Global particle simulation remains the existing separately configured transient visual cache. No blanket replacement of model/startup DPI changes eraser, pressure or historical geometry.
- Hard/soft share Brush; shape slots remain independently selected. Shape buttons now check the active shape selection identity rather than a rounded Brush cache. No new follow relationship is introduced.
- The preference codec honors enabled valid memory, then explicit startup selection when automatic defaults are disabled, then shared Medium. It preserves Preset identity across devices and does not overwrite startup preferences with temporary session values.
- Legacy ordinary/highlighter pixels are divided once using migration target scale; legacy Laser stays DIP. Migration retains Legacy raw fields, estimated source DPI and provenance. New version/unit blocks any second divide. Partial fields decode independently; unknown/future schema objects survive both AutoClean and memory save without downgrade.
- Memory root reading is bounded and rejects malformed/scalar/directory/reparse sources. In-process memory I/O is serialized; state setters release the state lock before acquiring that persistence mutex, so the new preference-lock-to-state reads do not introduce a matching state-lock-to-preference inversion. Same-directory existing atomic replacement preserves the old source on write failure, and unknown root members/legacy values remain retained.
- Slider ranges dynamically include presets and current valid values without open/cancel clamping. FineDial origin is exact; signed 1 DIP step counts preserve its fraction, no dirty open commits, cancellation preserves Selection, and the continuation adopts only its own successfully committed revision.
- Three language source templates use DIP and source generation/check was run by the main session. Formatters mark noninteger values with `≈`, sub-1 widths never display zero, and labels never feed back into state/config.
- New `pen-width.md` records the executable unit boundary, future schema safety and one-time retired Preset compatibility exception; the configuration spec now explicitly points to that exception.

## Verification evidence at initial report

Main session records inspected directly:

- `integrated-headless.result.json`: `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window`, exit 0, 94.38 seconds. This first run preceded this reviewer's local UI assertions.
- `integrated-policy.result.json`: `Build/ARM64/Debug/InkeysHeadlessTests.exe --pen-width-only --no-window`, exit 0.
- `integrated-i18n.result.json`: `pwsh -NoProfile -File Scripts/i18n.ps1 check`, exit 0.
- Complete build 1 was reported failed by main on a local `near` macro name in the newly added Draw3 probe; the owning Draw3 worker is correcting it. This is a source test-name error, not an SDK/environment problem.
- Complete build 2 and production probes are main-owned and still pending when this report was written. Compilation/type verification for the final source and post-fix tests must be recorded before claiming the final quality gate passed.
- Reviewer-run `git diff --check`: exit 0. No standalone C++ linter/type checker exists for this module setup; complete MSBuild provides type/compile checking, whose final result remains pending here.

## Subsequent evidence

- `integrated-build-2.result.json` was inspected: source compilation included this reviewer's full UI fixes, but linking failed inside ARM64 incremental linker `IncrBuildImage` with `LNK1000/C0000005`. This is an internal tool failure; it was not worked around by changing source/project dependencies.
- `integrated-build-3.result.json` was inspected: complete `InkeysRepo.sln Debug|ARM64` using native ARM64 MSBuild and invocation-only `/p:LinkIncremental=false` exited 0 in 21.47 seconds. The setting applies only to the verification invocation, with no persistent toolchain change.
- `final-draw3-width.result.json` and its stderr were inspected: `Inkeys.exe --draw3-pen-width-test` exited 0, recording resolved `1.939393939394 DIP`, ideal/continuous applied `5.333333333333 px`, integer adapter `5 px` for 1440p/275%.
- `final-width-production.result.json` and both streams were inspected: preference and state production tests passed; Bar assertion F4 failed. F4 has since been corrected as described above, and requires main's rebuild/rerun before a final runtime gate PASS.
- `format-check.json` was inspected: pre-existing files unchanged; original BOM/EOL preserved across recorded touched source files. The added F4 edit likewise used exact UTF-8 byte replacement retaining CRLF.

## Final available automatic quality gate — PASS

The main session rebuilt after F4; the reviewer inspected the final result JSON records and the production width/state output. All listed final records exited 0. Earlier failed records are retained as failure history and are not used as final acceptance evidence.

| Record / exact entry | Result |
| --- | --- |
| `final-build.result.json`: native ARM64 MSBuild `InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /p:LinkIncremental=false /m:1 /nr:false /v:minimal`, same-invocation PATH normalization and node reuse disabled | PASS, 17.26 s; includes all reviewer fixes |
| `final-headless.result.json`: `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window` | PASS, 97.26 s; includes new formatter/strategy/codec checks |
| `final-width-production-rerun.result.json`: `Build/ARM64/Debug/Inkeys.exe --pen-width-production-test` | PASS; real preferences, state and Bar/FineDial seams including corrected boundary/interior assertions |
| `final-state.result.json`: `Build/ARM64/Debug/Inkeys.exe --state-mode-production-test` | PASS; cross-entry/revision/slot/laser and FineDial regressions |
| `final-draw3-width-rerun.result.json`: `Build/ARM64/Debug/Inkeys.exe --draw3-pen-width-test` | PASS; continuous pixel boundary, real Down capture/reset, cursor and CPU history assertions |
| `final-config-boundary.result.json`: `Build/ARM64/Debug/Inkeys.exe --config-boundary-test` | PASS |
| `final-bar-offscreen.result.json`: `Build/ARM64/Debug/Inkeys.exe --bar-eraser-offscreen-test` | PASS, 25.15 s; offscreen rendering, not live input/window acceptance |
| `final-draw3-map.result.json`: `--draw3-renderer-map-test` on the same Inkeys executable | PASS |
| `final-draw3-commit-failure.result.json`: `--draw3-renderer-commit-failure-test` | PASS |
| `final-laser-raster-failure.result.json`: `--draw3-laser-raster-failure-test` | PASS |
| `final-parked-desktop.result.json`, `final-parked-ppt.result.json`, `final-loaded-retained.result.json`, `final-pending-topology.result.json` | PASS for `--draw3-parked-desktop-exit-test`, `--draw3-parked-ppt-retained-test`, `--draw3-loaded-retained-install-test`, `--draw3-pending-topology-load-test` respectively |
| `final-control-fence.result.json`, `final-fallback-controller.result.json`, `final-fallback-lane.result.json`, `final-ppt-load-retry.result.json` | PASS for `--draw3-control-fence-test`, `--draw3-fallback-stable-controller-test`, `--draw3-fallback-stable-lane-test`, `--draw3-ppt-current-load-retry-test` respectively |
| `integrated-i18n.result.json`: `pwsh -NoProfile -File Scripts/i18n.ps1 check` | PASS; three language source templates validated |
| Reviewer `git diff --check` after F4 and final report evidence reads | PASS |

No independent C++ linter/type checker is configured for these module units. Diff/i18n checks passed; type/compile checking passed through the complete final Solution build. The build's existing environment/PATH post-build `pwsh` warning does not replace the separately successful explicit i18n check. Unrelated pre-existing compiler warnings were not changed to force zero warnings.

Remaining evidence boundary: first user-visible startup stroke, touch/hold interaction feel, mixed-DPI hardware handoff, live PPT/WPS and Windows/architecture matrix remain **manual, not executed** as documented in `manual-acceptance.md`. Source confirms synchronous first-input preparation and unchanged UInk physical point/radius transfer (`uink_draw3_export.cpp` and `uink_draw3_import.cpp`); automatic software tests verify the available strategy/state/adapter/history seams. There is no outstanding handled-source or automatic-test blocker for the task's available quality gate.
