# Round 7 audit and completion of requirement gaps

Date: 2026-09-14. User requested a requirement/constraint audit and minimal corrections.
Task remains in progress. No commit, push or archive was performed for this audit.

## Findings corrected

### Long-idle Touch speed erasing could still lose the resumed segment

The prior follow-up corrected a test timeout but left a real input limitation: a sufficiently long held contact could exceed the modeler's existing 2000-output limit on its next Move or Up.

The Touch speed-erasing path now checks the gap since the last successfully appended modeled input against the configured output rate and per-call bound. If necessary, it reuses Reset/Update with unchanged model parameters and a short-time seed at the last accepted position.

It does not clear the existing modeled-result vector, converted-result cursor, real geometry, document item, contact identity, speed controller or area reference. The synthetic seed never enters movement or area statistics. Mouse, Pen and other eraser modes do not use this recovery. The modeler output limit itself is unchanged.

### Area diagnostics confused measurement validity with feature enablement

The disabled branch previously returned before validating or converting otherwise usable contact dimensions. Unverified per-context values were also labelled as pixels.

Raw area validation now runs independently of the option gate. Disabling assistance still prevents reference creation and all area-driven growth, but valid DIP measurements remain observable. Unit status is shown explicitly; unverified measurements are not displayed as verified DIP. Reference freshness is separately exposed.

## Additional coverage

- Physical, manual, resolution/DPI heuristic and DIP-only Touch routes now execute local-sweep replay tests at 60/125/240/1000 Hz and 30/60/120 Hz.
- Area-off valid measurement diagnostics and unverified-unit rejection.
- Explicit seven-second extra no-input hold after area expiry, before resuming real motion.
- Explicit seven-second held Touch followed directly by Up.
- Recovery-path counter must increment, new geometry radius must remain small, and sweep evidence must remain absent after the synthetic model seed.
- Existing real UInk boundary roundtrip and Undo/Redo checks remain in the hidden test.

## Results actually obtained

- ARM64-native full InkeysRepo.sln Debug|ARM64 build: exit 0.
- InkeysHeadlessTests.exe --no-window: exit 0.
- Area sampling/frame-rate deviation: 0.429525%.
- Existing speed sampling/frame-rate deviation: 1.99186%.
- Twelve frozen Mouse/ScreenPenHybrid float-bit trajectories: no differences.
- Inkeys.exe --draw3-eraser-hidden-test: exit 0 in the combined DComp/ULW run.
- Both long-idle recovery cases produced a new 8 px maximum radius while preserving the old 19.5 px history radius.
- The long held Up cases completed normally.
- No model gap/output-limit error appeared in the audit hidden stdout.
- git diff --check: exit 0.
- Modified source text remains valid UTF-8 with existing BOM and LF/CRLF conventions preserved.

Full build command uses the located ARM64 MSBuild with:
InkeysRepo.sln /m:4 /nr:false /p:Configuration=Debug /p:Platform=ARM64 /v:minimal.
The Path/PATH normalization was local to that invocation and preserved Path contents.

Logs:
- Build/touch-area-audit-build.log
- Build/touch-area-audit-tests.log
- Build/touch-area-audit-hidden-out.log
- Build/touch-area-audit-hidden-err.log

## Changed components

- Draw3.DrawingController.cpp: bounded, Touch-only long-idle model recovery.
- Draw3.SpeedEraser.cpp/.h: measurement validity independent of option state, explicit unit names and reference freshness.
- Setting.cpp: clearer raw/per-context/unit/DIP diagnostics.
- Draw3.HiddenWindowTest.cpp: intentional long-hold recovery and Up regression.
- speed_eraser_tests.cpp: area diagnostics and all-scale-route replay coverage.
- input-and-ink.md and task design: corresponding executable contracts.

The Experimental Options switch remains default-off, in-memory, shared with the diagnostic-page control and latched per contact batch. No pressure-based erasing, OOBE, new hardware query path or renderer replacement was introduced.

## Remaining manual acceptance

No visible GUI or Computer Use was launched. Actual Surface finger feel, driver WIDTH/HEIGHT metadata, UI layout, real teaching displays, external tablets and Windows 7 runtime still require manual/device acceptance. Synthetic hidden surfaces are not claimed as real display measurements. The older full hidden suite was not rerun or declared passed.

