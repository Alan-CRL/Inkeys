# Research: Production width test seams and nonvisible verification

- Query: Existing headless/state/config/Bar/Draw3 test entry points, actual window behavior, meaningful extensions, and project registration.
- Scope: internal
- Date: 2026-10-04
- Baseline: PRD records local HEAD `01a953d41adb5084e25899592cdde3bcb6c4770d`; pre-existing working-tree changes are part of the actual source inspected. No Git operations, builds, or test executions performed by this researcher.

## Findings

### Files found and related specs

- `InkeysHeadlessTests/animation_tests.cpp`: console test runner; `--no-window` skips window-service/message-box HWND tests, while keeping pure algorithm, display, bridge and cursor-related coverage.
- `InkeysHeadlessTests/InkeysHeadlessTests.vcxproj`: existing console C++20 project; directly compiles production module/source subsets. Registered in `InkeysRepo.sln:15`.
- `Inkeys/IdtMain.cpp`: actual product `wWinMain` plus explicit early-exit production probes; exact argument handling matters.
- `Inkeys/IdtState.cpp:1188`: `RunStateModeProductionTest`, real state setters, revision rejection, product bridge publication and tool/PPT equivalence.
- `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp:1290`: actual private FineDial interaction state test; exported wrapper at `:5786`.
- `Inkeys/Inkeys/UI/Bar/Bar.EraserAttribute.Test.cpp:104`: actual production D2D/WARP offscreen component/render coverage; no HWND.
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.HiddenWindowTest.cpp`: mixed test collection: no-HWND GPU adapters, never-shown HWND pixel test, and Host integration tests which explicitly show HWNDs.
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp`: real private width, cursor, per-stroke model setup and CPU/persistence probes; can host narrowly scoped direct production probes.
- `InkeysHeadlessTests/draw3_bridge_tests.cpp`: value snapshots, revisions, command queue and target identity tests; `:65` already checks a continuous `4.5f` width round trip.
- `InkeysHeadlessTests/speed_eraser_tests.cpp`: example of real isolated `Config` read/write tests and owned fixture directories (around `:1482`).
- `inkStrokeModelerTestTests/contact_input_tests.cpp:3030`: separate demo/shared-engine console test dispatcher and autosave/uink/laser-only options.
- `Scripts/i18n.ps1:1`: real `sync/check` command contract; generated header should not be edited manually.
- Related specs: `.trellis/spec/native-desktop/index.md`, `build-and-compatibility.md`, `configuration-i18n-and-assets.md`, `draw3-integration.md`, `eraser-attributes.md`; `.trellis/spec/native/index.md` and `quality-and-validation.md` clarify that demo tests cannot substitute for production tests.
- `.trellis/workflow.md` and `.codex/agents/trellis-research.toml` read; research role only writes this task's research file. Native injected active-task header was complete; role-isolated JSONL manifests were not loaded.

### Invocation and authorization classification

All commands below assume repository-root working directory, a freshly built ARM64 Debug product, and shell process exit-code checking. These are researched commands, not executed PASS records.

| Entry | Invocation | Actual scope / window behavior |
| --- | --- | --- |
| Headless | `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window` | Runner `animation_tests.cpp:1611` disables HWND tests; normal algorithm/config/bridge/display collection still runs. Do not omit `--no-window`. |
| State + FineDial | `Build/ARM64/Debug/Inkeys.exe --state-mode-production-test` | `IdtMain.cpp:1056` argv recognition returns before startup/config/window creation. State setters use `save=false`. Private Bar session uses real interaction methods; Product/Window Service must remain stopped. |
| Configuration boundary | `Build/ARM64/Debug/Inkeys.exe --config-boundary-test` | `IdtMain.cpp:469` uses `TestResults/release-hardening/config-boundary-<pid>` and redirects `globalPath`; covers malformed JSON, scalar/array roots, excessive size/depth, expansion beyond write limit, first-run creation. No HWND initialization. |
| Production UI offscreen | `Build/ARM64/Debug/Inkeys.exe --bar-eraser-offscreen-test` | `Bar.EraserAttribute.Test.cpp:106` explicitly initializes shared WARP/D2D/fonts without any HWND. Creates PNGs under `Build/eraser-b/visuals`, includes PageControl `RunOffscreenTests` (`:1587`). GPU/UI component verification, not real input/window behavior. |
| Draw3 map | `Build/ARM64/Debug/Inkeys.exe --draw3-renderer-map-test` | `HiddenWindowTest.cpp:2565`: WARP FL11 device/buffers/readback; no HWND. |
| Draw3 commit failure | `Build/ARM64/Debug/Inkeys.exe --draw3-renderer-commit-failure-test` | `HiddenWindowTest.cpp:2690`: WARP failure/retry and production raster submit; no HWND. |
| Laser transaction | `Build/ARM64/Debug/Inkeys.exe --draw3-laser-raster-failure-test` | `HiddenWindowTest.cpp:2855`: WARP device plus real `RunLaserRasterFailureProductionProbe`; no HWND. |
| Production CPU/persistence | `--draw3-parked-desktop-exit-test`, `--draw3-parked-ppt-retained-test`, `--draw3-loaded-retained-install-test`, `--draw3-pending-topology-load-test` on the same Inkeys EXE | Early exits `IdtMain.cpp:1129-1139`; construct documents/history/load snapshots and isolated persistence paths, not Host windows. Useful historical geometry/save regression protection. |
| Production control/PPT CPU | `--draw3-control-fence-test`, `--draw3-fallback-stable-controller-test`, `--draw3-fallback-stable-lane-test`, `--draw3-ppt-current-load-retry-test` | `DrawingController.cpp:3789/:3856/:3888/:4149`; private production logic with no attached WindowController HWND or Host startup. |
| Never-shown HWND pixel | `--draw3-renderer-pixel-test` | `HiddenWindowTest.cpp:2171` calls `CreateWindowExW` for a layered popup at `-32000,-32000`, without WS_VISIBLE/Show. Still creates HWND; not a strict no-window test. Prefer no-HWND probes for this task; do not claim this equals CPU/no-window coverage. |
| Host hidden integration | `--draw3-hidden-test`, `--draw3-eraser-hidden-test`, `--draw3-host-metrics-smoke --output-root <path>` | Not permitted in this task's strict no-visible-GUI validation plan. See explicit evidence below. |
| PageControl hidden | `--page-control-hidden-test` | Starts Window Service at `PageControl.cpp:2609` with offscreen HWNDs, but real render commit shows clients (`:2004`) and tests inspect VisibleMask (`:2633`). Not a strict no-window test; exclude from automatic task validation. |

`--draw3-hidden-test` is materially misleading for the current authorization boundary: `HiddenWindowTest.cpp:968-972` explicitly calls `SetDrawpadSurfaceVisibility(Primary)` and asserts `IsWindowVisible(drawpad)`. At `:996-1001` it shows the Presentation surface; only at `:1003-1006` does it restore hidden offscreen windows. Initial `WindowSpec.visible=false` (`:117`) does not mean the test never shows them. The user has not authorized interactive GUI tests; do not run these entries merely because a spec historically calls them hidden. A new width production CPU probe can supply meaningful integration coverage without altering existing window tests.

Many product flags use exact `CompareStringOrdinal(lpCmdLine,...)` (`IdtMain.cpp:1100-1169`). Invalid or trailing extra arguments could fail recognition and proceed into ordinary startup. Pass exactly the single researched flag, check exit code, and avoid guessed combinations. State/pixel flags instead parse argv with exact arity (`:1050/:1056`). For a Windows-subsystem EXE when shell waiting is uncertain, use `Start-Process -FilePath <absolute exe> -ArgumentList <exact flag> -WindowStyle Hidden -Wait -PassThru -RedirectStandardError <log>` and inspect returned `ExitCode`; Hidden does not make a GUI-creating test acceptable.

### Existing real state / interaction coverage and missing cases

- State test checks bridge width equals `GetPenWidth(snapshot.state)` (`IdtState.cpp:1237` vicinity), hard/soft/highlighter/laser cross-entry equivalence, repeated intents, late callback rejection, target-slot isolation, laser memory and top-level mode restoration.
- `SetPenWidthIfRevision(..., false)` is used for accepted/rejected commit checks, preserving real state and revision semantics without saving default product configuration.
- Current FineDial test begins candidate selection, tests stale inertia/physics after tool switch, clears cancelled interaction, rejects stale commit (`Bar.Interaction.cpp:1290-1393`). It explicitly avoids successful `SetMemory` commits until configuration path is isolated. Extend this seam for opening without writes, fractional preset preservation, cancellation, valid commit to ExplicitDip, and display-version invalidation after supplying an owned config fixture.
- Current headless thickness animation tests validate pure exported morph/opacity/metrics functions, not real preset button commits or authoritative width state (`animation_tests.cpp:124` onward). Do not count them as click/preview/drawing consistency evidence.
- Headless format fallback tests use `Detail::FormatThicknessText` (`animation_tests.cpp:81`); this is an appropriate place to test honest approximate DIP labels, sub-1 widths, and formatter purity after production formatter changes.
- `draw3_bridge_tests.cpp:65` proves bridge can preserve fractional floats; it does not prove renderer interpretation is DIP. The new width contract must be tested at the real Draw3 conversion, cursor and stroke setup seams.
- Production Draw3 current `CanvasDiameterForTool` returns float (`DrawingController.cpp:181`), laser alone multiplies DPI. Down passes its continuous diameter into model parameters (`:8610`, `:8633` vicinity). Width compatibility tests must keep continuous output and separately test the explicit integer adapter; a displayed integer does not justify forcing the renderer to integer geometry.

### Minimum meaningful new coverage

1. Put pure preset/validation/conversion tests in an existing-style `InkeysHeadlessTests/pen_width_tests.cpp`, calling the actual production policy API. Use table literals for expected results; never duplicate resolver formulas in test helpers. Cover ordinary and big-screen hard/soft/highlighter matrices, same pixels across DPI, 1440p/275%, portrait/non-16:9, invalid dimensions/DPI, NaN/Infinity/zero/negative/extreme inputs, half-up integer adapter, and preset identity collision. Record resolved DIP, ideal canvas px and compatibility px in failure diagnostics.
2. Extend actual `RunStateModeProductionTest` for preset source identity vs ExplicitDip, target/paintDevice refresh, hard/soft sharing, no preference reset on tool/PPT changes, and stale revision callback rejection. Use ordinary setter APIs; avoid direct writes to private state.
3. Extend isolated production config probe (or add a dedicated early-exit width probe) with real `ReadSetting/CaptureSettingJson/WriteSetting` plus actual startup restoration API. Fresh/missing/automatic/default/explicit/remembered precedence; legacy pixel migration vs legacy laser DIP; no historical DPI fallback disclosure; invalid/unknown/partial fields; interrupted migration; write failure preserving originals; idempotent reopen; cross-device Preset vs ExplicitDip; retain unrelated keys. Failure simulation should be deterministic (owned file/directory obstruction or existing injectable write seam), not filesystem permissions guessed from the local user account.
4. Extend real FineDial session test and offscreen Bar seam for preset button submissions, slider open/cancel/commit, range inclusion of 16 DIP highlighter and sub-1 DIP target preset, oversized legal legacy value preservation, candidate-only preview and correct selection identity. Test viewport/UI zoom variation independently of the target display DPI.
5. Add a small direct production Draw3 no-HWND probe inside `DrawingController.cpp` if the private width/Down setup logic cannot be called from headless. Verify Pen/HardPen/Highlighter/shape/laser conversion and per-stroke captured style across a later target change. Existing geometry/history/persistence probes protect old document coordinate semantics. Shader raster/AA does not need redesign or per-point quantization to test nominal width.

### Project registration and build ownership

- No new solution or build system needed. Main session alone owns engineering files and full build.
- A new production `.cpp/.cppm` policy must be registered in `Inkeys/Inkeys.vcxproj` and, when used by headless, `InkeysHeadlessTests/InkeysHeadlessTests.vcxproj`. Header-only pure policy is another valid minimal option if consistent with design. Existing project filters should follow current file convention, not be reformatted.
- New headless source requires `ClCompile Include` plus declaration and invocation from `animation_tests.cpp:1625-1670`; production modules are recompiled in the headless project, not linked from the Windows executable. That project has a different Debug CRT configuration, so do not pull the full modeler runtime into it merely to access a width function.
- Extending `IdtState.cpp`, `Bar.Interaction.cpp`, or `DrawingController.cpp` internal test seams requires no new source project entry. New exported Bar wrapper must match `Bar.Main.cppm:992-993`; new Draw3 probe must match its `.cppm` and `IdtMain.cpp` early dispatcher.
- Main build: discover installed Visual Studio ARM64 host MSBuild, then in the same PowerShell invocation run `Remove-Item Env:PATH -ErrorAction SilentlyContinue`, set `$env:MSBUILDDISABLENODEREUSE='1'`, and `& $msbuild InkeysRepo.sln /m /p:Configuration=Debug /p:Platform=ARM64 /nr:false`; wait at least 5 minutes before timeout. Capture exit status and first meaningful error. Do not independently build `Inkeys.vcxproj` or let workers race a full build.
- Localization validation: `pwsh ./Scripts/i18n.ps1 sync` after source updates/translations, then `pwsh ./Scripts/i18n.ps1 check`. Source resources are zh-CN/zh-TW/en-US JSONC; generated header is not the source.
- Optional shared-engine historical regression executable comes from the separate `inkStrokeModelerTest.sln`, not main solution. Existing dispatcher offers `--uink-file-only`, `--uink-presentation-only`, `--desktop-autosave-only`, `--presentation-autosave-only`, `--laser-incremental-only` (`contact_input_tests.cpp:3030-3056`). Its project output is `$(SolutionDir)ARM64/Debug` (`inkStrokeModelerTestTests.vcxproj:57`); explicitly record this separate build/evidence, never substitute its success for Inkeys production integration.

## External references

No external test framework/documentation needed or queried for this internal inventory. Project declares Visual Studio v143 / C++20 / Windows SDK 10.0.26100.0 in the headless and product project files; availability and runtime success are not established by this read-only research.

## Caveats / Not Found

- No tests or builds were run; all findings above are source-confirmed capabilities and suggested extensions. Main session must establish baseline and report exact commands/results.
- Current safe probes cannot prove real touch, PPT/WPS interaction, target-monitor handoff, mixed-DPI hardware, Win7 compatibility or visible first stroke; keep those as manual acceptance items.
- Existing tests contain no complete pen-width migration/default/preset-identity contract. Small pure strategy tests alone do not meet production startup/UI/Draw3 acceptance.
- `tools/` and `tests/` root directories do not exist here; actual suites are `InkeysHeadlessTests`, `inkStrokeModelerTestTests`, production early probes and `PptCOM.Tests`.
- Some specs historically prescribe hidden HWND tests despite actual visibility changes. User's task restrictions take priority; skip and disclose these rather than changing the existing harness merely to pass a command list.
