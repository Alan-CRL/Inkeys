# Research: Draw3 runtime DComp failure and HWND generation

- Query: Can the product recover DComp/device failure to ULW after startup without losing the CPU document or trapping input? Check the Win7 SP1 + KB2670838, FL11/HARDWARE/WARP and FLIP constraints.
- Scope: internal
- Date: 2026-09-28

## Findings

### Core conclusion and risk status

**R-DWM-01: confirmed recovery design gap in the current code; actual OS failure is unverified.** `TransparentPresentationController::RecoverFromRuntimeFailure` releases the DComp attempt, retries DComp and then calls `TryInitialize(UlwDirtyRect)` with the **same** `primaryWindow` (`Draw3.TransparentPresentation.cpp:841-897`). `TryInitialize` calls `ConfigureWindow` (`:689-710`), which clears `WS_EX_NOREDIRECTIONBITMAP` and adds `WS_EX_LAYERED` for ULW (`:122-132`, `:551-554`). The product's DComp-capable primary Drawpad was created with `WS_EX_NOREDIRECTIONBITMAP` (`IdtMain.cpp:1833-1835`, `:1885-1891`). Window Service checks the actual post-write style and returns false on refusal (`Window.cpp:1547-1564`). The project spec records that a DComp-bound HWND may refuse clearing this creation-time bit. Therefore ULW runtime fallback is attempted but **cannot be counted as reliable recovery**; failure is conditional on Windows/driver/window history, not proven on this machine by static inspection.

If both attempts fail, `DrawingController::Run` requests its own `WindowController` exit and breaks (`Draw3.DrawingController.cpp:6346-6358`). The Host drawing thread then destroys controller/presenter/GPU resources and sets `running=false` (`Draw3.Host.cpp:1197-1226`). `WindowController::RequestExit` sets only its own `exitRequested_` and wake (`Draw3.WindowControl.cpp:559-563`); `IdtMain` waits only for the separate global `offSignal` (`IdtMain.cpp:2189-2194`). No runtime callback there observes Host termination or starts a new HWND generation. Thus the product can stay alive with Draw3 stopped; drawpad visibility and input state are not closed by this failure path. This is a **high-severity static liveness/input risk** that needs an isolated fault test to establish the visible impact. `ProductRunning()` would become false (`Draw3.Product.cpp:47-50`), but the main loop does not poll it.

`IdtMain` does have a complete **startup-only** fallback: before Setting and other UI threads start it stops Draw3, stops the entire Window Service chain, clears the primary Drawpad's `NOREDIRECTIONBITMAP` creation style, starts a new service and starts a ULW-only Host (`IdtMain.cpp:2048-2082`). There is no equivalent runtime coordinator after `:2162-2182` starts Bar/Freeze/state/PPT threads and `:2192` enters the offSignal loop. Window Service's dynamic `Destroy` is not a direct substitute: the Drawpad cannot be destroyed while Bar/PPT owned descendants exist (`Window.cpp:1809-1815`, `:1838-1862`). Its full `StopAndJoin` destroys the whole overlay/setting chain (`Window.cpp:1017-1038`), including HWNDs held by other running clients. Host Stop destroys the in-memory `DrawingController`/document (`Draw3.Host.cpp:1216-1222`, `:1303-1359`), so reusing the startup sequence after input has been accepted would lose uncommitted CPU document/contact state. A simple `StopProduct`/`StartProduct` retry is therefore not safe.

Win7 with **DComp absent at startup** is a separate case: `ShouldPreconfigureNoRedirectionBitmap()` only returns true if `dcomp.dll` exposes `DCompositionCreateDevice` (`Draw3.TransparentPresentation.cpp:114-119`, `:488-491`); `TryInitialize(DComp)` skips absent API without changing the HWND (`:689-700`). IdtMain creates the legacy-compatible primary HWND and starts ULW directly (`IdtMain.cpp:1885-1891`, `:2014-2016`). This static path does not validate Win7 actual Present.

Both DWM blur modes remain disabled by the two-element automatic array and `TryInitialize` gate (`Draw3.TransparentPresentation.cpp:94-97`, `:689-698`), plus Host forced-mode rejection (`Draw3.Host.cpp:1029-1037`). `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL` is still assigned for the swap chain (`Draw3.TransparentPresentation.cpp:635-650`) and must remain per the user's Win7 SP1 + **only KB2670838** empirical compatibility constraint, despite the cited Microsoft generic documentation conflict recorded in `draw3-integration.md:379-384`. `InitializeGraphicsDevice` tries HARDWARE, retries an 11.0-only feature-level list after `E_INVALIDARG`, then tries WARP (`Draw3.GraphicsInitialization.cpp:49-99`). These are source paths, not evidence that FL11 hardware or no-FL11/WARP presents successfully on target Win7 devices.

### Smallest safe containment versus full recovery

1. On a failed DComp recovery, do not report ULW as successful unless Window Service's style readback and a full successful ULW Present occur. If the original primary HWND cannot clear `NOREDIRECTIONBITMAP`, return an explicit `NeedsNewWindowGeneration`/fatal disposition rather than retrying indefinitely on that HWND. Keep DComp retry and the existing FLIP swap effect; do not re-enable either DWM mode.
2. Deliver the fatal disposition from the drawing thread to the product/main coordinator using a one-way callback or atomic event. The owner thread must immediately gate new Draw3 commands/contact, cancel any capture, hide **both** drawpad surfaces with Window Service's paired visibility command (`Window.cpp:1653-1733`), and enter a controlled failure/shutdown path. This contains the transparent or input-trapping stale HWND and avoids the current live-but-dead Draw3 state. Do not claim this preserves unsaved strokes.
3. Full runtime DComp→ULW recovery requires an explicit generation transaction, not a local presenter edit: stop RTS/commands at a barrier; resolve active Down/Up/Cancel and capture an authoritative CPU document/history/presentation scene snapshot while the controller still owns it; drain accepted persistence; stop Host; quiesce Bar/PPT/Setting callbacks and Window Service clients; rebuild the whole owned HWND chain with a legacy-compatible Drawpad and presentation-only ULW surface; start Host with DComp disabled; restore the exact scene/state and reattach input; prove a full successful Present before showing the correct surface. A failed restore must retain the last committed UInk and safe hidden windows. Current Host Start resets bridge and creates a fresh controller (`Draw3.Host.cpp:1038`, `:1161-1171`), so this is a substantial cross-module change requiring dedicated design and tests. A blanket ULW-at-startup workaround would alter the product's normal DComp path and needs independent performance/experience evidence; it is not a silent fix.

### Validation boundary

- No-HWND/static tests can verify mode-selection policy (DComp/ULW only), `FLIP_SEQUENTIAL` configuration, the `E_INVALIDARG`→FL11.0 and HARDWARE→WARP control paths, and that a simulated `NeedsNewWindowGeneration` event drives the coordinator's hidden/failure state. They cannot verify actual `SetWindowLongPtr`, DComp target lifetime, swap-chain Present or input handoff.
- A GUI fault-injection test on isolated user data and process is required after explicit GUI authorization: start DComp, accept ink/selection and active contact, induce DComp-only and device-lost failures, record primary/presentation HWND generation, pre/post `GWL_EXSTYLE`, selected backend, first/full successful Present, document/history identity and accepted sample count, both-window visibility, capture and input. Repeat with no content and content/PPT pages; ensure the old HWND is hidden before any failed recovery is exposed. Do not use external force-kill as a presenter fault substitute.
- On real Win7 SP1 with **only KB2670838**, record OS patch set, GPU/driver and actual feature level. Test FL11.0 HARDWARE and no-FL11.0 hardware→WARP where hardware permits, DComp-absent startup→ULW and successful `FLIP_SEQUENTIAL` Present/resize/device loss. The current Win11 ARM64 build and static import checks cannot mark those cells PASS. If hardware cannot cover a cell, retain it as manual/unverified release evidence.

## Files found

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cpp` — presenter ordering, style callback, swap chain, runtime recovery.
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` — recovery invocation, GPU cache rebuild, failure exit.
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp` and `Draw3.Product.cpp` — Host/controller/RTS lifetime and product running gate.
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.GraphicsInitialization.cpp` — FL11.1/11.0 and HARDWARE/WARP attempts.
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp` — local exit flag.
- `Inkeys/Inkeys/Window/Window.cpp` — style readback, paired surface visibility, HWND owner-chain destroy constraints.
- `Inkeys/IdtMain.cpp` — startup-only full-chain fallback and main-loop shutdown signal.

## Related specs

- `.trellis/spec/native-desktop/draw3-integration.md:58-75,379-384` — product HWND owner/renderer/presenter/FLIP contract.
- `.trellis/spec/native/platform-and-resources.md:25-75` — historic demo compatibility paths; its top note defers integrated product choices to Draw3 integration spec.
- `.trellis/spec/native-desktop/errors-logging-and-resources.md:228-233` — failure/recovery evidence boundaries.

## Caveats / Not Found

- No code, build, performance sampling, hidden-HWND or GUI test was run in this research. Conditional Win32 style refusal, physical input trapping, successful cross-generation restore and Win7 FLIP on this workspace remain unverified.
- No post-startup product path was found that atomically rebuilds the Window Service/Draw3 Host generations while transferring the live CPU document and contacts. `IdtMain.cpp:2052-2072` handles only startup before dependent UI threads.
- This is a release gate if the project requires continued usable drawing after runtime DComp/device failure. The containment action prevents silent dead input; preserving uncommitted accepted ink requires the larger generation design above.
