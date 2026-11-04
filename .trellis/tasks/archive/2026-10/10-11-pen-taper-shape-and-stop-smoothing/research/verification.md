# Verification Evidence

## Build and environment

- First sandbox attempt: InkeysRepo.sln Debug|ARM64, native ARM64 MSBuild discovered through vswhere; same-invocation PATH normalization and node reuse disable. Exit 1 at 00:10:01.75; native remained at module dependency scanning, log reports Build FAILED with 0 warnings and 0 errors. Roslyn named-pipe connection timed out; PptCOM fallback compile and DLL/TLB export succeeded. No source diagnostic justified changing code or project settings.
- Accepted sandbox escalation plus command-only /m:1 /p:UseSharedCompilation=false completed the identical full solution configuration: exit 0, 00:02:37.06, 371 warnings, 0 errors. These warnings are existing third-party, Windows SDK and unrelated repository diagnostics. Log: Build/ARM64/Debug/pen-taper-msbuild-retry.log. The first attempt log also confirms all four Draw3 shader compilations succeeded.
- Necessary probe assembly fixes received incremental full-solution builds (not standalone vcxproj). Final build exit 0, 00:00:07.85, 7 warnings / 0 errors; all seven warning locations are existing ink_stroke_modeler params.h/internal/utils.h. Log: Build/ARM64/Debug/pen-taper-msbuild-verified.log.
- Final output includes the existing post-build message 'pwsh.exe is not recognized', while MSBuild still exits 0 with 0 errors. No PATH/global Git/toolchain/project edits were made to hide that environment message. Main can assess the unrelated post-build step if desired.

## Production taper probe

Command: Build/ARM64/Debug/Inkeys.exe --draw3-pen-taper-test

Exit 0; modelCases=72, resumeCases=36, maxSettleMs=166.667, maxOutputs=19, assertions=331063, failures=0.
Log: Build/ARM64/Debug/pen-taper-probe-verified.log.
Additional unexpected argument exits 2 before the normal startup/window chain.

The first probe run found 876 test-fixture failures because it incorrectly called Predict in Disabled mode. Actual product code skips those calls. The corrected probe uses the exact product guard and clears both prediction scratch and derived prediction on every frame; no product prediction policy was changed. The fixture terminal branch was also corrected to call BeginEndpointAdmission after actual kUp and pass the current lastMovementInputTime, matching product assembly after resume. Final assertions check real terminal output, pinning and completed/Stored equality.

## Existing headless suite

Command: Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window

Initial sandbox exit 1 / FAILED count=11, centred on metadata/atomic temporary-file writes. The same binary/arguments outside the sandbox exit 0, Config metadata tests: 0 failures and PASS animation correctness. Logs: Build/ARM64/Debug/pen-taper-headless.log (sandbox failure) and Build/ARM64/Debug/pen-taper-headless-verified.log (success). No code was changed to bypass those environment failures, and no GUI/window test was enabled.

## Diff / encoding / boundaries

- git diff --check --ignore-submodules passes. Unqualified git diff accesses the already inaccessible HiMsg submodule metadata; no HiMsg source or metadata was changed.
- All four edited source files retain the HEAD UTF-8 BOM and pure CRLF: IdtMain.cpp, Draw3.DrawingController.cpp, Draw3.InkPrediction.cppm, Draw3.StrokeGeometry.cpp.
- Product controller diff is limited to the actual positionMoved cap reset (5 added / 1 removed lines). Geometry changes and no-HWND probe use the existing compiled unit; no build project registration/configuration changes.
- No demo, third-party source, shader/resource, persistent format/configuration, commit or push changes. Main session owns the spec/task metadata edits.

## Limits

These tests execute production CPU geometry, shared frame/model helpers, the real modeler and completion/Stored logic. They do not create HWND/Host/RTS or run the complete DrawingController owner loop, GPU pixel rendering, real stylus input, Windows 7 runtime or visual-handwriting feel. Screenshot-specific visible behaviour remains manual device acceptance, as authorized scope excludes GUI testing.
