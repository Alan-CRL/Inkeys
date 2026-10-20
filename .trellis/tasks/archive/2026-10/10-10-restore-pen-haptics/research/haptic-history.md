# Research: Draw3 pen haptics history and compatibility

- Query: Compare the original Draw3 haptic implementation in `016ed57b`, its Win7 hardening in `dfe606c8`, and production migration `fc9a8b86`; identify the lost runtime connection, UI discrete feedback evidence, safe compatibility/lifecycle boundaries, and no-GUI validation opportunities.
- Scope: mixed — local source and read-only Git history, plus primary Microsoft API documentation. Planning only; no product changes and no task activation.
- Date: 2026-10-10

## Findings

### Precise regression evidence

1. `016ed57bfc348c6ff123b7c8b2b8362b85fa9aad` (2026-07-24, `feat(draw3): add pen haptic feedback compatibility`) introduced the haptic module, a `PenHapticFeedback*` constructor dependency, runtime Pen eligibility, pointer-binding mailboxes, and explicit continuous-feedback stops. The demo creates/initializes the object, passes its address into the controller, runs the controller, and shuts down on the same application/drawing thread. Current matching anchors: `inkStrokeModelerTest/main.cpp:107`, `:108`, `:118`, `:122`.
2. `dfe606c8b6a4425dca998695c4700dbe809270ec` (2026-08-04, `fix: harden draw3 security and Win7 compatibility`) changed `LoadLibraryW(L"combase.dll")` to the module-local `LoadSystemLibrary(L"combase.dll")`, constructing a System32 absolute path with bounds checks. This security/Win7 fix remains in production at `Inkeys/Inkeys/Drawing/Draw3/Draw3.HapticFeedback.cpp:32` and `:171`. Keep it; do not return to bare DLL loading or assume Win7 has `LOAD_LIBRARY_SEARCH_SYSTEM32`.
3. `fc9a8b865cd8233f0fca66363dc10f35823465a4` (2026-08-16, `feat(draw3): integrate Draw3 drawpad runtime`) copied both haptic module files and controller calls into production, but its new `Draw3.Host.cpp` has no haptic import/object and constructs `DrawingController(input, window, renderer, presentation, configuration, observer)` without the haptic argument. The migrated constructor retained `RuntimeMetricsSession* metrics = nullptr, PenHapticFeedback* haptics = nullptr`. Consequently every existing `if (haptics_)` branch is disabled. The main session independently confirmed the same missing connection in the present Host.
4. Current demo/production haptic files differ only in named-module and namespace declarations (`git diff --no-index` for each `.cpp`/`.cppm` pair). Both diff commands exit 1 because a difference exists; this was comparison evidence, not a validation failure. The implementation was preserved, not removed or substantially rewritten.
5. Production project already compiles the module (`Inkeys/Inkeys.vcxproj:1050` / `:1051`), the default is true (`Draw3.InkPrediction.cppm:224`), and constructor/member defaults remain null (`Draw3.DrawingController.cppm:97` / `:166`). Thus a minimum restoration must connect the existing optional dependency and its same-thread lifecycle rather than add a second drawing/input runtime.

### UI/button discrete feedback: evidence and not-found result

- `PlayDiscrete` is public at `Draw3.HapticFeedback.cppm:79`, implemented at `Draw3.HapticFeedback.cpp:413`, and wrapped at `:759`. Current source searches across demo and product found **no caller outside its own implementation**.
- Scoped read-only `git log -G 'PlayDiscrete|HapticDiscreteFeedback' -- inkStrokeModelerTest/draw3 inkStrokeModelerTest/main.cpp Inkeys/Inkeys/Drawing/Draw3` found only original introduction `016ed57b` and copy `fc9a8b86`. The original window/controller diff does not add a discrete button trigger. No historical product-owned button `PlayDiscrete` trigger was found in this requested history scope.
- User-observed historical button vibrations remain valid behavioral evidence, but these commits do not identify their exact producer; Windows/device automatic interaction feedback or a different historical UI path is not proven here. Do not claim a specific deleted button call was found.
- The repair needs an explicit discrete request for an accepted user UI action, with current Pen identity/source. Do not derive feedback from every bridge revision, every `SetActiveTool`, background state sync, startup, or replay. A mouse/touch click must not use the last retained pen controller. Pointer observation alone is not an accepted click and must not produce duplicate discrete feedback.

### Existing waveform and failure contracts

| Contract | Source evidence |
| --- | --- |
| Pen/default tools resolve to InkContinuous `0x100B` | `Draw3.HapticFeedback.cpp:255`–`:265` |
| Highlighter resolves to ChiselMarkerContinuous `0x100E` | `Draw3.HapticFeedback.cpp:259` |
| Eraser resolves to EraserContinuous `0x1010` | `Draw3.HapticFeedback.cpp:261` |
| Inverted pen tail always uses InkContinuous because some firmware lists eraser but does not drive the tail | `Draw3.DrawingController.cpp:3291`–`:3298` |
| Runtime eligibility is Pen only and excludes Laser | `Draw3.DrawingController.cpp:8765` |
| Optional continuous/discrete fallback is InkContinuous/Click | `Draw3.HapticFeedback.cpp:268` / `:274`; supported feedback lookup at `:588` |
| Negative default intensity preserves device/system settings | `Draw3.HapticFeedback.cppm:16`; `Draw3.HapticFeedback.cpp:249`, `:614`, `:630` |
| Continuous waveform starts once, unchanged waveform is not resent, changed waveform stops previous playback first | `Draw3.HapticFeedback.cpp:432`–`:454` |
| InputManager sends only if its current device type equals Pen | `Draw3.HapticFeedback.cpp:657`–`:671`; this is a type check, not identity equivalence |

The existing mapping treats HardPen and shape tools as the default Pen waveform; preserve this contract. Laser has no continuous feedback. The original source does not provide a separate Pencil waveform for HardPen.

WinRT API absence is optional capability failure. `combase.dll` and all five required WinRT entries are dynamically resolved (`Draw3.HapticFeedback.cpp:168`–`:185`), missing module/entry returns false, factory creation failures leave null optional pointers (`:310`–`:345`), and public playback methods return false without affecting drawing. PenDevice factory presence alone makes `IsAvailable()` true (`:367`), so it does **not** prove that the active pen exposes `IPenDevice2`, a controller, or any requested waveform. Attachment checks all those stages (`:392`–`:410`), and unsupported waveform lookup returns null (`:588`–`:600`). Preserve these no-op outcomes and do not gate Host startup on haptic initialization success.

### COM/thread constraints and the minimum safe production path

- `PenHapticFeedbackImpl` stores mutable controller/cache/playback fields and has no lock (`Draw3.HapticFeedback.cpp:280`). All initialize, attach, play, stop, disable and shutdown calls must be serialized on the owning thread; UI/window/RTS callbacks publish requests only.
- WinRT initialization uses MTA, accepts `RPC_E_CHANGED_MODE` without taking an uninitialization obligation, and records successful `S_OK` or `S_FALSE` initialization for paired `RoUninitialize` (`:188`–`:211`). COM initialization/uninitialization is per-thread. Construct/initialize and destroy/shutdown on the Draw3 drawing thread; do not leave a final destructor on the main/UI thread holding live COM state.
- The existing manager fallback lazily caches `InputHapticsManager.GetForCurrentThread` (`:646`–`:654`). A drawing worker does not own input focus, so simply injecting the old object into the Host cannot prove that fallback routes to the right input device.
- Minimum safe recommendation: use the **exact PenDevice controller obtained from a current Pen pointer ID** as the production path. Do not call background `GetForCurrentThread` and do not send to a generic manager solely because it currently reports Pen. This task does not need new mouse/touchpad/general haptics.
- If retaining InputManager fallback, obtain the manager from the actual input-owner/window thread and prove device identity/routing; `TryGetForThread` retrieves an existing manager and does not create one. A thread-ID lookup plus `deviceType == Pen` is insufficient evidence that it targets the same pen as the UI request. This is a distinct design/verification burden, not required for the minimum repair.

### Attach/stop/release hazards that restoration would expose

- `AttachPointerId` rejects zero, suppresses repeated failures for one ID, and stops/releases the previous controller before attempting a different ID (`Draw3.HapticFeedback.cpp:372`–`:390`). This prevents a failed new bind from sending through an old controller. `CachePointerController` also stops before changing controller (`:536`).
- `StopFeedback` clears playback state and stops both retained controllers but **does not detach** the pointer controller or clear attached/failed pointer IDs (`:457`–`:475`). There is currently no public detach method. `Shutdown` stops, releases all COM references before unloading combase, and clears IDs/capability state (`:348`–`:363`).
- A canvas PointerLeave does explicit Stop through mailbox consumption (`Draw3.WindowControl.cpp:1320`–`:1326`; `Draw3.DrawingController.cpp:12550`–`:12553`). Retained controller after canvas leave is potentially useful for a subsequent current-Pen UI request, but it must not be treated as proof of UI Pen ownership. A current Pen request should attach its own ID; unsupported/new ID failure must never fall through to old-pen playback.
- A hover/enter preset calls `TickContinuous` (`Draw3.DrawingController.cpp:12566`–`:12580`) but **does not update local `hapticContinuousActive`**. The idle stop (`:12746`) and no-active-runtime stop (`:13700`) check only that local bool. With haptics wired, a hover preset can therefore survive a tool/mode transition that never produced a stroke.
- `WindowController::SetActiveTool` (`Draw3.WindowControl.cpp:431`) and `SetSelectionMode` (`:445`) only change state/request wakes; they do not request haptic stop. The presentation admission barrier seals contacts and stops feedback (`Draw3.DrawingController.cpp:9344`–`:9375`), but it early-returns when AdmissionRevision is unchanged. A Desktop selection change need not change admission (`Draw3.Host.cpp:555`, `:1238`), so this barrier does not close the hover preset gap.
- Laser is rejected on runtime Down (`Draw3.DrawingController.cpp:8765`) and stops after a fresh pointer attach (`:12568`–`:12571`), but selecting Laser while already hovering without a new enter/down does not reach that fresh-attach branch. The same hover preset state must be cleared on entering Laser/selection/hidden or otherwise disabled drawing state.
- Touch Pan already clears pending haptic attach and requests leave (`Draw3.WindowControl.cpp:682`–`:686`, `:712`–`:715`, `:798`–`:800`). Controller refuses attachment when pan, pen suppression, quarantined contacts or admission blocking applies and stops (`Draw3.DrawingController.cpp:12559`–`:12564`). Preserve these paths and existing Pen presence/cursor ownership behavior; do not clear input mailboxes or synthesize Mouse/Pen events to route haptics.
- Runtime Up/reconnect stops are present (`Draw3.DrawingController.cpp:9318`–`:9322`, `:13682`–`:13704`) and normal loop exit stops (`:14112`). Exceptions rethrow at `:14081` / `:14107` and skip the latter normal-exit call. Owning-thread Host RAII/cleanup must shut down the haptic object on every early return, initialization failure, Run exception, normal stop, and restart.

Necessary local lifecycle repair: maintain or explicitly query preset/playback ownership consistently; stop on selection/hidden/Laser/disabled state even when no stroke was created; discard obsolete pending attaches when drawing becomes ineligible; resume only from a valid new Pen presence/request or eligible accepted stroke. Keep changes confined to haptic wiring/request/stop logic and do not alter cursor normalization, RTS authority, hover sampling, input admission or window activation/style policies.

### Meaningful no-GUI validation opportunities

- Build existing production solution `InkeysRepo.sln Debug|ARM64` with the installed ARM64 native MSBuild host, normalize `Path`/`PATH` and set `MSBUILDDISABLENODEREUSE=1` in the same PowerShell invocation, and permit at least five minutes. Do not create a new build system or alter SDK versions.
- Run `InkeysHeadlessTests.exe --no-window`; existing runner dispatch is `InkeysHeadlessTests/animation_tests.cpp:1642` / `:1692`. Add focused production-backed contract tests if a small haptic routing/backend seam is introduced; do not copy the implementation into test code.
- Test API absence independently of actual machine capabilities: missing DLL, missing each entry, unavailable class/property, null controller, unsupported waveform, failed new pointer ID. Verify false/no-op and preserved startup/input behavior. Hardware-independent backend injection can observe that no sends occur, without initializing a UI window.
- Fake backend/request tests should count real send/stop actions for: hover preset then selection/Laser/hidden/Pan; Down/Up/Cancel; interrupted reconnect; disable; exception/early-return shutdown; stale ID/new unsupported pen; mouse/touch UI event; repeated accepted Pen button click versus programmatic state revision; shutdown with pending UI request; restart clearing previous generation.
- Waveform selection tests should cover preferred supported waveform, required fallback only, neither supported, inverted tail, Laser, and default intensity. Existing **demo-only** tests at `inkStrokeModelerTestTests/contact_input_tests.cpp:2719`–`:2797` cover enum/mapping/default-disabled no-op, but cannot validate production Host wiring, UI routing, failure injection or active lifecycle transitions.
- Static review plus `dumpbin /imports` on production output must show no new static WinRT/Win8+ pointer APIs. Check source for preserved System32 absolute loading and dynamic GetProcAddress. Inspect x86/x64 import tables when those target builds are available; ARM64 cannot prove Win7 compatibility.
- Run scoped `git diff --check` and review minimal product diffs after implementation. Preserve original BOM/CRLF in existing C++/project files.
- Physical vibration quality, genuine multiple pens, Windows Settings intensity, and Win7 startup/fallback remain real-device checks. Do not launch visible GUI or Computer Use by default, and do not call headless pass proof of hardware vibration/cursor safety.

## Files Found

- `inkStrokeModelerTest/main.cpp` — original working host lifecycle and injection, lines 107–122.
- `inkStrokeModelerTest/draw3/haptic_feedback.cpp[m]` — original optional WinRT ABI/capability implementation and exported API.
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.HapticFeedback.cpp[m]` — production copy, unchanged except module/namespace naming.
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp[m]` — retained conditional haptic calls, Pen/Laser eligibility, pointer preset, stops, default-null dependency.
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp[m]` — current pointer/leave and Touch Pan haptic mailboxes mixed with existing cursor authority logic.
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp` — migration Host omitted the instance/constructor argument; current state/admission separation matters to hover preset stops.
- `Inkeys/Inkeys.vcxproj` — haptic module is already registered for production compilation.
- `inkStrokeModelerTestTests/contact_input_tests.cpp` — original demo enum/mapping/default-disabled haptic contracts.
- `InkeysHeadlessTests/animation_tests.cpp`, `InkeysHeadlessTests/InkeysHeadlessTests.vcxproj` — production no-window runner and directly compiled real-module pattern; haptic module currently absent from test project.

## External References

- [Pen interactions and haptic feedback](https://learn.microsoft.com/en-us/windows/uwp/ui-input/pen-haptics) — Windows 11 pen haptics; preset a supported continuous waveform on pointer enter, explicit stop on pointer exit, required InkContinuous/Click fallback, and system/device capability checks. It also documents temporary interruption/resumption of continuous ink by a discrete interaction. This supports preserving hover preset behavior with complete stop ownership, rather than removing preset behavior to conceal a lifecycle bug.
- [PenDevice.SimpleHapticsController](https://learn.microsoft.com/en-us/uwp/api/windows.devices.input.pendevice.simplehapticscontroller?view=winrt-26100) — Windows 11 build 22000, UniversalApiContract v14; older OS/property absence is expected.
- [Implement haptic feedback in Windows apps](https://learn.microsoft.com/en-us/windows/apps/develop/input/haptics) — current InputHapticsManager guidance lists SDK 10.0.28000.1721/contract v19 and requires UI/input-focus owning thread; a background GetForCurrentThread does not route correctly. Use type/capability checks and treat failed TrySend as non-fatal.
- [InputHapticsManager.GetForCurrentThread](https://learn.microsoft.com/en-us/uwp/api/windows.devices.haptics.inputhapticsmanager.getforcurrentthread?view=winrt-28000) — repeated same-thread calls return the same manager; reference page lists build 26100/28000 and contains prerelease labeling.
- [InputHapticsManager.TryGetForThread](https://learn.microsoft.com/en-us/uwp/api/windows.devices.haptics.inputhapticsmanager.trygetforthread?view=winrt-26100) — gets a manager if it already exists, otherwise null. It is not a creation/focus-routing substitute.
- [Microsoft Pen haptics sample](https://learn.microsoft.com/en-us/samples/microsoft/windows-universal-samples/penhaptics/) — official sample describes supported pointer ID → PenDevice → controller, continuous and interaction feedback; runtime requires Windows 11 and compatible pen/digitizer.

## Related Specs

- `.trellis/workflow.md` — research persisted within current task; planning precedes implementation.
- `.trellis/spec/native/index.md`, `modules-and-code-style.md`, `runtime-and-rendering.md`, `platform-and-resources.md`, `quality-and-validation.md` — original Draw3 modules, ownership, minimum diff, BOM/CRLF, Win7 SP1 + KB2670838 optional API/dynamic DLL contract.
- `.trellis/spec/native-desktop/index.md`, `build-and-compatibility.md`, `draw3-integration.md`, `input-and-ink.md` — production Host is unique, UI publishes immutable data/commands, drawing thread owns runtime; RTS remains input authority; Pointer and mouse normalization/cursor ownership must be preserved; no visible GUI without user authorization. User instructions override older spec build-dependency discrepancies.

## Caveats / Not Found

- No product edits, compilation, test execution or GUI/hardware tests were performed in this planning research.
- No historical explicit `PlayDiscrete` UI caller was found in the specified Draw3 paths/history; the user's observed button vibration producer cannot be uniquely attributed from these commits.
- Microsoft current InputHapticsManager reference/guidance use different SDK/build/prerelease labeling. Runtime factory probing and precise PenDevice binding are safer than an OS-version claim. The main session has confirmed local SDK 10.0.26100.0 declares the ABI, but that is compile-time evidence only.
- Existing absence of optional WinRT APIs is safe by source inspection; actual Win7 executable imports/startup and genuine Pen/controller behavior need the stated target validation.
- A failed pointer ID is cached indefinitely until ID changes/Shutdown; if the same numerical ID is reused in a new pointer lifetime, a detach/reset policy may be needed. Preserve per-lifetime failure suppression while preventing cross-generation stale identity; avoid polling/rebinding every high-frequency Move.
- `PenHapticFeedback` constructor allocates; its noexcept methods also cache a vector. This research does not broaden scope into allocation-policy refactoring. Owning Host exception cleanup remains required.
