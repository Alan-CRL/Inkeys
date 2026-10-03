# GPU probe diagnostics review — 2026-10-03

## Findings (fixed)

- No mechanical/local source issue found in the scoped static review; no production or diagnostic source was edited by this reviewer.

## Findings (not fixed)

- Win7 SP1 + KB2670838 runtime and PowerShell 2.0 execution remain unverified on this Windows 11 ARM64 machine. No platform-support conclusion follows from local parsing, mocked collector outcomes, or Windows 11 GPU results. The next user capture is required.
- The six-case output has four required production checks (swapchain/offscreen clear and production cursor), plus two isolated blend-disabled comparisons. Exit 0 proves the four required checks and the device-removal check; comparison failures deliberately remain diagnostic observations. Pipeline statistics are optional and may fail independently of successful pixel readback. These are intended evidence boundaries, not source defects.
- The collector imposes a 30-second isolated-child timeout. Individual event/statistics waits are bounded to 3 seconds; in a pathological slow/failed-query run their cumulative waits can approach or exceed the external timeout. The collector records timeout and still collects the ordinary application. A timeout does not identify a unique GPU root cause.
- Main session added the seven-section native input-and-ink GPU diagnostic contract; reviewed signatures, four-production/two-comparison semantics, timeout behavior and evidence boundaries agree with implementation. No remaining spec-sync issue in this scope.

## Reviewed paths and behavior

- `IdtMain.cpp`: exact new diagnostic command returns before product configuration, single-instance mutex and product HWND initialization. Existing redirected-output handling remains intact.
- `Draw3.HiddenWindowTest.cpp/.h`: creates a never-shown, no-activate STATIC fixture HWND and WARP FL11_0 device, ordinary FLIP_SEQUENTIAL swapchain, and real production renderer/shaders. Both target kinds use BGRA8. Clear expected bytes are BGRA=(64,32,16,128), tolerance 1 across all pixels. Cursor checks require nonzero pixel counts, premultiplied center around alpha 128 and zero corner alpha. RTV/source matching uses canonical IUnknown identity. Readback unbinds OM, copies to CPU staging, polls an event with deadline, then maps with DO_NOT_WAIT. Comparisons replace only the isolated renderer's blend field with a write-mask-ALL disabled state; targets/blend restore after each case. Context ClearState/renderer release precede device/context/swapchain destruction and fixture HWND destruction. Diagnostic flag restoration is RAII.
- `Draw3.RendererPrimitives.cpp`: original actual render sequence and buffer upload data remain unchanged. Visible requests sample first/once per second under the existing Draw3 flag; disabled diagnostics perform no GPU state queries or formatting. Missing resources, invalid appearance and actual Map HRESULTs are distinguishable from upload completion and Draw-command submission. Before/after snapshots capture actual viewport, OM target/source identity and VS/PS/b0/VS-t0 matches. Draw-issued log is explicitly not pixel-success evidence.
- `Collect-Win7.ps1/.cmd`: isolated probe runs hidden with separate stdout/stderr before manual ordinary-app capture. Records nonzero exit, startup exception or timeout and continues. Timeout Kill targets the exact child Process object, not a process name. Existing SHA256 Clear() compatibility fix is preserved. Added Process.WaitForExit(Int32), WaitForExit(), Refresh and Kill exist in the .NET 2-era API surface; actual Win7 CLR/PS2 execution still needs the user's machine. No configuration mutation is added.

## Verification

- Lint/static: `git diff --check` passed (only existing implement.md LF-to-CRLF advisory); collector parsed without errors using local PowerShell parser. Scoped source BOMs unchanged; C++ and ASCII collector files retain working-tree CRLF, with no bare LF in those files.
- TypeCheck/build: main reports full solution Debug|ARM64 exit 0; reviewed build output contains the expected Inkeys/Headless/PptCOM artifacts. Supplementary Release|x64 build is still linking at review time, owned by main.
- Tests: main reports ARM64 isolated pixel probe exit 0; inspected log shows all four required production and both comparison cases PASS, normal device status, six VS invocations and nonzero PS invocations per draw. Cursor GPU rows capture upload/before/after and actual target/buffer/shader matches. Headless log reaches PASS animation correctness; main owns process exit-code confirmation. Implementer reports four mocked collector paths (success, nonzero exit, timeout, startup error) pass; no mocked outcome is described as Win7 execution.
- No GUI, build, commit or push was performed by this reviewer.
