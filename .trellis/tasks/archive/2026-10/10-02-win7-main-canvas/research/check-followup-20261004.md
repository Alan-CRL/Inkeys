# Win7 follow-up quality review — 2026-10-04

Active task: `.trellis/tasks/10-02-win7-main-canvas`. This reviewer owns only this report. Source and collector fixes are coordinated with their implementers; the main session owns builds, specification updates and delivery.

## Findings (fixed)

- File: `Inkeys/IdtMain.cpp`
  - Issue: Raw `lpCmdLine` comparison rejected the trailing U+0020 produced by installed Windows PowerShell, so the intended diagnostic could fall through into ordinary application startup.
  - Fix: GPU diagnostic alone now uses the existing `CommandLineToArgvW` block. Exact token matching remains case insensitive; exactly two tokens invoke the self-test, and additional arguments return 2 before product configuration, single-instance setup or HWND initialization. `LocalFree` is executed on both branches. The state-mode and host-metrics branches and other diagnostic modes are unchanged.
  - Review: Actual Win32 tokenization and comparison tested independently through ctypes in seven cases: exact / trailing space / quoted / uppercase / trailing tab take the two-token path; extra token / empty extra token take return 2. This test launches no product and creates no HWND. The implementer's separate twelve-case API matrix is also retained. The main session also verified the built x64 executable returns 2 for an extra argument without ordinary startup.

- File: `.trellis/tasks/10-02-win7-main-canvas/research/Collect-Win7.ps1`
  - Issue: The previous probe combined `WindowStyle Hidden` with redirection, which failed in the user's PowerShell 2.0 parameter binding before any probe process launched.
  - Fix: Probe uses `NoNewWindow` with stdout/stderr redirection and passes the explicit early diagnostic flag. It retains its owned process handle, does not invalidate it with `Refresh`, and records an unreadable exit code as an error. `ProbeOnly` records termination metadata, returns nonzero on probe failure, and never enters the normal application branch.
  - Review: Added APIs are available in .NET Framework 2.0; public `HashAlgorithm.Clear()` is retained. Timeout termination remains exact owned-child only. New ASCII/CRLF `Run-Win7-PixelTest.cmd` invokes the ProbeOnly branch without requiring another manual drawing reproduction.

- File: `.trellis/tasks/10-02-win7-main-canvas/research/Collect-Win7.ps1`
  - Issue: An exception after a successful start could leave the owned probe running because the catch only recorded the error.
  - Fix: At reviewer request, the collector owner added best-effort Kill/Wait on that exact child in catch and retained the original error text before nested cleanup. No name-based termination was added.
  - Review: Final diff reviewed, syntax and diff checks rerun. Injected waiting exceptions in both ProbeOnly and full-collection mock paths retain failure metadata and terminate only their owned child. The full path still reaches the mocked normal collection branch. No remaining local code findings.

## Findings (not fixed)

- Actual Windows 7 / PowerShell 2.0 execution of the corrected collector and the six GPU cases remains unverified. This machine provides Windows PowerShell 5.1 on Windows 11 ARM64. A constrained parameter-set model plus the captured PS2 error supports the parameter correction, but is not a real PS2 runtime test.
- Unique GPU root cause remains unproved. The captured 25 RTS Down/Up and controller observations establish mouse input; 39 raw ULW staging samples are zero and five cursor GPU samples show successful uploads and matched bindings. `Draw` issued, object identity and successful API calls do not establish shader execution, nonzero GPU output or desktop visibility. The independent Win7 Clear / production Draw / blend comparison did not run in this capture and must still be collected.
- Two disabled-blend cases are diagnostic comparisons. Four production cases determine self-test exit 0; readers must inspect all six cases individually. No source evidence warrants speculative production changes to input, shaders, blend, swapchain or renderer backend.

## Verification

- Lint: PASS — `git diff --check` exit 0; existing implement.md LF-to-CRLF Git warning does not indicate a source formatting rewrite.
- PowerShell syntax: PASS — parser reports no errors. Collector and both command launchers use ASCII CRLF; `IdtMain.cpp` retains UTF-8 BOM and CRLF. Tracked Git blobs have normalized LF, so comparison to blob line endings is not a worktree regression.
- TypeCheck / Build: PASS — main session reports full `InkeysRepo.sln Debug|ARM64` and `Release|x64` builds exit 0 using native ARM64 MSBuild. Reviewer inspected final target outputs; build ownership remains with the main session.
- Tests: PASS — main session reports `InkeysHeadlessTests.exe --no-window` exit 0; reviewer inspected its final PASS output. Seven independent Win32 argv cases pass. Collector success, nonzero, startup-error, unknown-exit, large-stream and timeout cases are documented in `win7-224836-followup/collector-validation/validation-summary.json`; their normal-application branch is mocked, not a product GUI test.
- Tests: PASS — final collector exception cleanup has both only/full waiting-error injections, exact owned-child cleanup and no false success. Corrected x64 collection through actual installed Windows PowerShell 5.1 records `pixel-test-exit=0`, EXE SHA `2D468B91156333C5CBD9E1E3C82D5713961DD94647ECCF4B44285FFC8EDDD72C`; reviewer inspected identity and all six individual PASS rows in `actual x64 corrected/Win7-Diagnostics/20261004-074303-285/pixel-test.stderr.txt`. Four production cases report failures=0; both blend comparisons also pass. Hidden fixture records visible=0. ARM64 actual collection likewise reports all six PASS and no product configuration side effects. These are local evidence, not Win7 acceptance.
- Final script SHA-256: `6E56DC40B66592487FC69EFEDD06B58706D3962257F9F08FBE0D1DE8B06F4CAA`; `Run-Win7-PixelTest.cmd`: `C784735BC7E9FCFD911F9FB494AA19341E396CEAFF53E2033FCB2747E1054799`.
- No GUI/product execution, build, commit, push or task archival was performed by this reviewer. Original G: capture and existing shader binaries were not changed.

## Documentation consistency

The main session has synchronized the existing seven-section GPU diagnostic contract in `native-desktop/input-and-ink.md`. Reviewer inspected the diff: argv matching/extra-argument rejection, PS2 default parameter-set redirection, ProbeOnly, unreadable exit errors, exact owned-child cleanup and local-vs-Win7 test boundaries match the implementation. No project registration/template changes are needed because this reuses the existing CLI and only adds a stand-alone collector launcher. The unresolved Win7 task must remain in progress.

## References

[Microsoft Process.WaitForExit](https://learn.microsoft.com/en-us/dotnet/api/system.diagnostics.process.waitforexit?view=netframework-2.0) supports draining asynchronous output after the timed overload returns true and retaining exited-process information through its handle. [Microsoft Start-Process](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.management/start-process) describes NoNewWindow/redirection; its current parameter table alone is not evidence of the historical PS2 table. Historical failure here is directly supported by the supplied Win7 identity and the constrained compatibility test.
