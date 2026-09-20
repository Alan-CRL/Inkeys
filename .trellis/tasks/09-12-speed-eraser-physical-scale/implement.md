# Implementation and validation
1. Implement the approved shared API and deterministic pure tests; inject mode/display scale through Host/WindowController.
2. Adapt ink_prediction alias and width interval; batch-latch scale, preserve hover and reconnect ownership, use accepted radius for contact cursor.
3. Register shared source/test in existing projects and headless runner. Do not edit the old inkStrokeModelerTest demo.
4. Main runs the full solution once the main-session implementation is ready. Locate ARM64-native MSBuild using current VS installation; in the SAME invocation run Remove-Item Env:PATH -ErrorAction SilentlyContinue and set MSBUILDDISABLENODEREUSE=1. Use at least five minutes; no visible process windows.
5. Run Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window; review actual failures and fix causes, not assertions to hide failures. Static review includes downstream radius consumers, fake reconnect speed, time ordering, latch lifetime, DPI/revision notification, callback shutdown.
6. Update the code spec with implemented signatures, defaults and concrete tests. Keep tasks in progress; no commit or archive. Record physical device feel and Windows7 runtime compatibility as not empirically tested on this machine.

Recovery override: all steps are executed by the main session only; no subagents. See validation.md for actual results and remaining manual device acceptance.
