# Research: UI3 Bar pen-only discrete haptic path

- Query: Find the smallest safe way for accepted UI3 tool/type clicks to request pen haptics, while preserving WM_TOUCH normalization, compatibility mouse suppression, capture, cursor, and unsupported-system fallback.
- Scope: mixed; current Bar/Product/Host code and Microsoft primary Win32 documentation. Historical Draw3 backend and git history are owned by another researcher and were not inspected here.
- Date: 2026-10-10

## Findings

### Files found

| File | Relevant responsibility |
| --- | --- |
| `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp` | Bar WndProc, sole WM_TOUCH-to-ExMessage translator, private wheel markers, dequeue/clear wrappers, actual button commit branches. |
| `Inkeys/Inkeys/UI/Bar/Bar.Initialization.cpp` | Starts and joins the separate Bar interaction thread. |
| `Inkeys/Inkeys/UI/Bar/Bar.Button.cpp` | Main Draw/Eraser/Geometry callback business decisions; callback signature itself does not report acceptance. |
| `Inkeys/Inkeys/Message/Message.cpp` / `.cppm` | Existing promoted Pen/Touch mouse signature filtering. |
| `Inkeys/additional/HiMsg/HiMsg/HiMsg.hpp` | Shared ExMessage layout; no timestamp, pointer ID, or extra metadata member. Read-only reference; do not modify this third-party header for this task. |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Product.cpp` / `.h` | Unique product Host and stopping/call gate around thread-safe publication. |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp` / `.h` | Drawing owner, input control wakes, state/command consumption, and stop/join boundaries. |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp` / `.cppm` | Current dynamically resolved Pointer information helpers and canvas-specific pointer/cursor handling. |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.HapticFeedback.cppm` | Public haptic API and discrete Click enum; implementation/history deliberately outside this research topic. |
| `Inkeys/IdtState.cpp` | Tool business transition, including accepted repeated selection that returns false for no visual change. |
| `InkeysHeadlessTests/message_tests.cpp` | Existing source marker roundtrip and promoted-mouse regression coverage. |
| `InkeysHeadlessTests/animation_tests.cpp` | Headless runner, --no-window guard, message/bridge/contact tests. |

### Current contracts and code anchors

1. **Keep the current input route.** `Bar.Interaction.cpp:860-996` chooses one primary/fallback WM_TOUCH contact, publishes marked Down/Move/Up to `Window::Enqueue`, and closes the touch handle. The translator currently does not distinguish `TOUCHEVENTF_PEN` from genuine Touch in ExMessage metadata. Active contact replacement emits a marked canceled Up at `:901-916`.
2. **Keep both compatibility-mouse filters.** WndProc `Bar.Interaction.cpp:1015-1018` and the HiMsg enqueue callback `:7342-7345` reject Pointer-generated mouse copies. Real mouse messages continue through the existing route. Do not resurrect a Pen-promoted mouse branch or use the signature low bit to assign Pen identity.
3. **Preserve four private wheel semantics.** Constants at `Bar.Interaction.cpp:54-57`, `MarkBarTouchPointerMessage(:245)`, `IsBarTouchPointerMessage(:258)`, `IsBarTouchCancelMessage(:267)`, and `IsBarTouchScreenMessage(:274)` encode ordinary contact / canceled contact / absolute screen sample / canceled absolute screen sample. `PrepareBarInteractionMessage(:373-397)` rewrites screen samples to layout samples and re-marks the message; `Seek(:6044-6057)` can restore screen semantics. Any added token must survive both transformations and must still satisfy these existing contact/cancel/screen queries. Actual wheel events must never decode as a token.
4. **The consumer is a different thread.** `Bar.Initialization.cpp:184` creates `interactionThread`, and `:196` joins it during shutdown. Bar queue access is concentrated in `WaitForBarInteractionMessage(:457-475)`, `TryGetBarInteractionMessage(:478-488)`, and `ClearBarInteractionMessages(:491-495)`. Query Pointer APIs in the Bar window-owner WndProc, not in these consumer functions or on the Draw3 thread.
5. **No existing ExMessage identity field.** `HiMsg.hpp:46-86` contains the mouse union fields, HWND, and category. Its window wParam/lParam overlap the mouse union, so writing them is not a legitimate way to add identity. The existing private wheel metadata is the least intrusive transport available inside Bar.
6. **Current Pointer helpers are private and canvas-specific.** `Draw3.WindowControl.cpp:107-174` dynamically resolves GetPointerType/GetPointerInfo/GetPointerPenInfo and returns type/position/primary/contact/eraser data, but does not expose sourceDevice/cursorId. These functions are in the implementation's private namespace; there is no reusable exported identity helper. `WindowControl.cppm:224-225` exposes ConsumeHapticPointerId for the canvas. Do not run Bar Pointer messages through this canvas controller just to reuse it.
7. **A dedicated product publication gate is available.** `Draw3.Product.cpp:13` has `productCallMutex`; `:32-44` stops new calls, drains entrants, then stops Host without holding that mutex across join. State publication `:85-100` and command publication `:181-189` demonstrate the stopping check / call lock / recheck pattern. A Bar haptic API should follow this same pattern.
8. **Host already wakes its sole consumer.** `Draw3.Host.cpp:201-206` owns bridge/input/window and command publication synchronization. `:1397-1405` consumes control wakes on the drawing owner, and `:2146-2147` wakes state changes. A small dedicated haptic mailbox can use General wakes without adding drawing contacts or routing Bar HWND messages into `Host::ForwardMessage(:2121-2132)`.

### Preferred minimal integration

Use **a Bar-only bounded contact-token tracker transported through the existing private wheel marker**, then a **dedicated bounded Product/Host haptic request mailbox**. This adds no input message and leaves all input effects in the existing translator.

#### 1. Passive owner-thread observation

- Observe client `WM_POINTERENTER/DOWN/UPDATE/UP/LEAVE` and `WM_POINTERCAPTURECHANGED` only as supplemental haptic evidence. Resolve User32 Pointer entry points dynamically. Preserve caller LastError, as the current source query does at `WindowControl.cpp:63-89`.
- On a successful PT_PEN query, record immutable `{Bar HWND generation, pointer lifetime generation, pointerId, sourceDevice, cursorId if available, raw screen point, system event time, phase, canceled/in-range flags, monotonic observed-at time}`. GetPointerCursorId is a supplemental discriminator; absent/failed identity information makes haptics ineligible rather than changing normal input.
- Only Pointer DOWN creates an eligible click lifecycle; hover merely provides fresh identity and never authorizes a Click. Pointer UP seals it for the bounded business-consumption window. New lifetime, pointer/source/cursor replacement, cancellation, capture loss, out-of-range/leave, Bar destruction, or product stopping invalidates outstanding eligibility. These invalidations may also retire queued haptic work, but must not change normal capture or cursor state.
- Every observed Pointer message still reaches the same DefWindowProcW path exactly once. Do not return 0 selectively, call SetCapture/ReleaseCapture/SetCursor, mutate Inputs key state, call RegisterTouchWindow/UnregisterTouchWindow, EnableMouseInPointer, SkipPointerFrameMessages, SendInput, or enqueue synthetic Down/Move/Up from the observer.

#### 2. Pen origin and token binding at existing WM_TOUCH publication

- Only the WM_TOUCH contact already selected by the current translator can receive a token. Its Down must explicitly have `TOUCHEVENTF_PEN`, and Up must remain the same `(hSource, dwID)` contact generation. Genuine Touch and real mouse receive no token, even if a pen was recently nearby.
- Select exactly one fresh observed PT_PEN Down candidate for that Bar generation and phase. Use strict evidence: equal raw source handle where the driver exposes a shared handle, the same trusted system timestamp, and exact screen point under the translator's existing conversion. Require usable system time (TOUCHINPUTMASKF_TIMEFROMSYSTEM), and reject ambiguity; do not add a nearest-pen search, a wide time/position tolerance, or a last-known-pen fallback.
- **This correlation is a conservative compatibility policy, not a Microsoft-guaranteed physical-pen identity contract.** See the critical caveat below. If these strict checks cannot associate the two APIs on a device, leave its Bar click haptic unbound and preserve the normal business message. Source handle equality is a rejection gate, not a claim that every driver uses the same handles.
- A fixed small table (for example 8 recent lifecycles) owns full generations. The wheel code carries only a Bar-private token plus the original cancel/screen bits. Keep existing untagged marker values valid, and do not alter HiMsg's category/union or third-party ABI. Concrete numeric encoding can be chosen during implementation.
- Move messages can retain the token for lifecycle transport, but never request a pulse. A canceled Up retains cancel semantics and is never eligible. On slot exhaustion, missing Pointer messages, source query failure, failed association, or token expiration, enqueue the original untagged message normally. Losing haptic metadata must never lose the button contact.
- Short wire-token reuse needs an explicit guard: do not recycle a token while any tagged message from its previous lifecycle is still queued or any consuming press still holds it. Track queued references through the existing enqueue/dequeue/clear wrappers under the tracker mutex; copies to the interaction session retain the full generation. Clear(EM_MOUSE) retires corresponding metadata with the actual queue clear; haptic cleanup itself must never call Clear. A canceled/expired tombstone remains bounded and noneligible until its queued references drain. Full slots simply suppress new haptics.
- Keep the tracker lock around small memory/queue bookkeeping only. Do not hold it across DefWindowProc, callbacks, Product calls, WinRT calls, or joins. If queue bookkeeping needs serialization, all Bar publishers/readers of tagged messages must use the same narrow wrapper; current product Bar Enqueue/TryGet/Clear call sites are all in this implementation unit.

#### 3. Prove an actual accepted tool/type click

- Latch the full token generation on the button's consumed Down. Request one discrete Click only after the same contact's explicit `WM_LBUTTONUP`, uncanceled, inside the relevant button, and after that operation's business branch accepts it. The token must match at Down and Up and be unconsumed; it must not be looked up from whichever pen is latest when the callback eventually runs.
- Scope is exactly the existing Draw/Eraser tool buttons and HardPen / SoftPen / Highlighter / Laser subtype buttons at `Bar.Interaction.cpp:5810-5941`. Do not add pulses for More, settings, color swatches, thickness presets/sliders/fine dial, dragging, hover, geometry edits, global state revision, PPT takeover, or programmatic configuration changes.
- `SelectPenTool` at `:5492-5515` is the narrow shared commit point for HardPen/SoftPen/Highlighter. Laser commits at `:5825-5832`. Their current `!msg.lbutton` terminal conditions are broader than an explicit Up, so the haptic gate must separately require `WM_LBUTTONUP` and a noncancel token. Do not widen this task into a rewrite of every existing button loop.
- `ChangeStateModeToPenTool` at `IdtState.cpp:1175-1193` returns false for a valid repeated selection because it produces no visual change, but still records newer user intent. For the four known valid enum choices, a completed accepted click can still pulse once; do not treat false as universally meaning rejected or use revision changes as a click detector.
- Main callbacks are `void` and may reject toggles (`Bar.Button.cpp:300-330`, `:364-379`). The generic main loop's `clickCompleted=true` at `Bar.Interaction.cpp:3985` proves only that the Up reached the callback path. For Draw/Eraser feedback, arm a narrowly scoped current-click context around the callback and request inside its actual accepted change/toggle branch. Do not pulse every time `clickFunc()` returns.

#### 4. Dedicated thread-safe request and bounded expiry

- Publish immutable `{Host run generation, Bar generation, full contact generation, pointerId, sourceDevice/cursorId evidence, sealed-Up monotonic time, feedback=Click}` through the normal Product stopping/call gate. Return false if Host is stopping, absent, queue full, or metadata is invalid; do not block or perform WinRT on the Bar owner/interaction thread.
- Use a fixed small Host mailbox and a General control wake. The drawing owner is the sole haptic backend caller, keeping all Attach/Play/Stop/Shutdown operations on its existing haptic owner. Consume at most one request per accepted contact generation, outside mailbox locks.
- Suggested explicit limits: identity DOWN association freshness at most 100 ms; sealed-Up click request expiry at most 250 ms from observed Up; incomplete press absolute lifetime at most 5 s. These limits disable only stale haptics, never throttle or cancel the UI operation. Timer values are application policy, not Windows guarantees. A consumer stall beyond the expiry intentionally produces no pulse.
- Invalidation must be visible to the Host consumer, not only to the Bar tracker: keep a small synchronized source-validity generation alongside the mailbox, or publish a cancellation record that cannot be lost behind ordinary queue saturation. Check its current generation immediately before backend use. Merely putting the old pointerId in a FIFO does not prevent leave/capture-loss/stop races.
- Never play with the backend's previously attached pen when this request's AttachPointerId or identity validation fails. Clearing/disabling a source explicitly stops its associated feedback through the backend owner. A late Bar request must not reattach an old pen or overwrite a newer canvas pen binding; require source/run generation consistency, and drop conflicting requests.
- On Stop/reset, first reject production requests through the existing gate, invalidate all source generations and pending records, then drain/join and let the backend owner stop/shutdown. No new worker thread or independent Host is needed.

### External references and versions

All references below are Microsoft primary docs, checked 2026-10-10.

- [WM_POINTERDOWN](https://learn.microsoft.com/en-us/windows/win32/inputmsg/wm-pointerdown) and [WM_POINTERUP](https://learn.microsoft.com/en-us/windows/win32/inputmsg/wm-pointerup): Windows 8+; unhandled messages go to DefWindowProc. Selectively consuming a sequence is undefined, and capture loss means paired Down/Up cannot be assumed. This supports passive observation and explicit cancellation.
- [GetPointerInfo](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getpointerinfo): Windows 8+; information belongs to the calling thread's most recently retrieved Pointer message and can disappear at the next retrieval. A thread not owning the receiving window can fail with ERROR_ACCESS_DENIED. Capture immutable evidence in WndProc.
- [POINTER_INFO](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-pointer_info): pointerId lasts only for the detected lifetime; sourceDevice is usable with raw-input/digitizer APIs; raw/predicted screen positions and event timestamps are available. Pointer ID alone is not a permanent pen identity.
- [GetPointerCursorId](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getpointercursorid): Windows 8+; provides a tablet cursor identifier, and opposite tips of one physical pen can be separate cursor objects. Treat cursor changes as a fresh binding.
- [TOUCHINPUT](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-touchinput): Windows 7+; TOUCHEVENTF_PEN identifies stylus-origin contact. dwID lasts only for that touch contact and can be reused. hSource's example uses GetRawInputDeviceInfo; provider time is not trustworthy unless the system-time mask is set. This doc does not equate dwID with Pointer pointerId.
- [WM_POINTERCAPTURECHANGED](https://learn.microsoft.com/en-us/windows/win32/inputmsg/wm-pointercapturechanged): no new Pointer data accompanies this notification; later contact notifications may stop. Invalidate using the message's existing ID/lifecycle rather than inventing an Up.
- [RegisterTouchWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registertouchwindow): Windows 7+; registration belongs to the window owner. Keep the existing registration and flags; haptic observation requires no new registration.
- [Pen haptics](https://learn.microsoft.com/en-us/windows/uwp/ui-input/pen-haptics): parent research verified this separately; the UI Click must use the existing discrete capability/fallback layer, with no promise that a successful no-op means physical vibration. Backend feature/version probing is outside this topic.

### Related specs

- `.trellis/workflow.md`: planning persists in task artifacts; no product changes during this research dispatch.
- `.trellis/spec/native-desktop/input-and-ink.md:102-108`: UI3 Bar contact normalization, both Pointer compatibility filters, low signature bits not an ownership contract, explicit Up click completion.
- `.trellis/spec/native-desktop/input-and-ink.md:30-61`: mouse/touch/pen cursor authority and compatibility filters must remain isolated from haptics.
- `.trellis/spec/native-desktop/draw3-integration.md:54-66`: Window Service owns HWNDs; unique Host/RTS/drawing thread; UI publishes immutable state/commands; core does not depend on Bar.
- `.trellis/spec/native-desktop/cpp-conventions.md`: minimal diff, preserved encoding/EOL, short Chinese comments, state snapshots and no HWND/GPU/COM side effects under state locks.
- `.trellis/spec/native-desktop/build-and-compatibility.md`: existing Solution/native-host verification, no toolchain change. Root user AGENTS instructions are authoritative where current spec describes different PptCOM dependency facts.

### Meaningful no-GUI regression validation for implementation

No tests or builds were run in planning. Proposed checks exercise production marker/tracker/mailbox helpers rather than a test-only reimplementation:

1. Private marker roundtrip preserves contact/cancel/screen classifications, full token recovery, screen-to-layout-to-screen transformations, and existing untagged fixtures; wheel input stays wheel input. Existing anchors: `message_tests.cpp:79-107`.
2. Valid Pen Down/Up -> accepted subtype Click -> exactly one request. Same button repeated selection -> one request for each completed fresh contact, not one per state revision. Invalid Up, Move without left-button, drag out, canceled contact, and business rejection -> zero requests.
3. Fresh pen followed by real mouse or Touch button click -> zero Pen request, even at identical coordinates; promoted Pen Down with one low signature value and Up with another remains discarded as before. Existing anchors: `message_tests.cpp:109-156`.
4. Two matching candidates, missing trusted time/device identity, source/cursor replacement, reused pointerId/touchId, capture loss, leave/out-of-range, late Up, stale consumer, Bar/Host generation replacement, and reset -> zero request for the old lifecycle. Old queued token cannot alias a recycled slot.
5. Full Bar token table and full Host mailbox still permit all business messages and tool mutations; only haptic publication fails. Enqueue failure/refcount/clear races cannot leave a reusable token referring to an old queued contact.
6. Missing User32 Pointer exports, query failure, backend unavailable, failed attach, unsupported waveform, and shutdown interleavings -> no exception, no stale attached-pen pulse, no wait cycle. Spy backend checks every call occurs on the intended sole haptic owner.
7. Keep existing mouse/touch cursor and bridge/contact tests passing. Build `InkeysRepo.sln`, Debug|ARM64, with discovered ARM64-native MSBuild, PATH normalization in the same PowerShell invocation, node reuse disabled, and at least five minutes allowed. Run `InkeysHeadlessTests/Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window`. Review minimal diff and static scan that Bar observation does not call the canvas forwarding path, capture/cursor APIs, or alter touch registration.

## Caveats / Not Found

- **Critical: cross-API delivery and physical-pen association are not guaranteed by the inspected source or docs.** Neither the existing code nor Microsoft references provide a supported WM_TOUCH contact -> WM_POINTER pointerId identity conversion. `(dwID == pointerId)`, nearest position/time, and the most recently observed pen are insufficient. Even strict source/time/point correlation is an application inference; the Windows/driver layer can omit one route, use different source handles, or provide ambiguous timing. Do not label such correlation precise physical-pen identity or claim universal UI haptics restoration from headless tests. The safe first implementation drops UI haptics whenever strict evidence is unavailable; real supported hardware must confirm that the association actually succeeds. Continuous canvas haptics can be restored independently.
- A Bar wheel token solves transport and stale-context isolation; it does not manufacture missing source identity. If exact same-pen identity is a mandatory universal requirement, the planning gate must explicitly resolve this limitation before implementation. Changing Bar's entire input route to native Pointer handling would be a larger separately reviewed change, with cursor/long-press/Win7 regression risks; it is not the preferred minimal fix here.
- No public identity helper suitable for Bar was found. The RTS private classifier uses dynamically resolved GetPointerDevices/GetPointerDeviceCursors at `RealtimeStylus.cpp:426-428`, but it is input-source classification, not Bar click provenance. Do not infer a device HANDLE by casting RTS TABLET_CONTEXT_ID.
- The current product does not encode the physical button contact in main callback return values. Adding main-tool Click feedback requires a small accepted-business handshake as described above; blindly pulsing after every void callback or state sync is not valid.
- No hardware, GUI, hidden-window, build, git-history, or backend implementation checks were performed by this researcher. Native Win7, nonhaptic pens, genuine haptic pens, and mixed Pen/Touch/Mouse behavior remain separate runtime evidence requirements.
