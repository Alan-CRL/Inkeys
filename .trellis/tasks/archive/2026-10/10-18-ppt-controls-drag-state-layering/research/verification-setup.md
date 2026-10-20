# Implementation verification setup

- Date: 2026-10-18
- User approval: items 1-3, task in_progress; no GUI/Office or commit/push authorization.
- Build system: existing InkeysRepo.sln, Debug|ARM64, native ARM64 MSBuild discovered with vswhere (current Visual Studio installation). No project/SDK/dependency changes.
- Supervisor: 900 seconds; same PowerShell invocation removes Env:PATH and sets MSBUILDDISABLENODEREUSE=1, MSBuild /nr:false /p:PreferredToolArchitecture=arm64. Runner/logs are ignored under Build/Validation/ppt-controls-20261018.
- Previous task proved Codex sandbox PDB IPC C1902 and headless temporary-file failures using same-command outside-sandbox controls; use the approved unrestricted execution path for this build/test, no code/toolchain workarounds.

## Test wiring evidence

- InkeysHeadlessTests.vcxproj compiles PageControl.cppm, not PageControl.cpp. --no-window RunPageControlTests exercises pure exported production strategies, not PageControl::RunOffscreenTests.
- IdtMain.cpp:1307-1309 recognizes exact --bar-eraser-offscreen-test before normal configuration/single-instance/HWND startup and returns RunEraserAttributeOffscreenTest.
- Bar.EraserAttribute.Test.cpp:152-165 initializes only COM/WARP/D2D/font resources; source explicitly forbids creating/displaying HWND and no window-creation call was found in the test. Its line2341 calls PageControl::RunOffscreenTests; resource/report output stays in Build/eraser-b.
- This existing harness allows actual private bounds/configure/pair/capture-injection regression tests to run without adding an entire UI implementation to the headless project. It does not execute actual USER32 capture, hidden HWND or Office tests.
- Final test results must distinguish --no-window pure strategies from the product offscreen harness, and mark real pointer/capture/Office/GUI acceptance as NOT VERIFIED.

## Checks before execution

- Source and tests are owned by separate implementers; main session does not build while they are mutating.
- Independent trellis-check follows implementation, then final source freeze/build/test and format/scope checks.
- Run commands with explicit arguments only; the main product without its test flag is never launched.
