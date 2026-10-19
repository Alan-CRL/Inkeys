# Research: Elevated foreground and mandatory Raw Mouse completion

- Query: Why do Bar buttons animate on Down but perform no click when an elevated window owns the foreground after the UI pointer migration?
- Scope: mixed; read-only production/tests/spec review plus Microsoft API/security documentation.
- Date: 2026-10-17

## Findings

### Evidence boundary

- User observation: before the migration, clicks worked with an elevated foreground but dragging failed. After migration, press animation occurs but action does not.
- Proven from code: the migration accepts native mouse Down before binding a Raw Down, suppresses native Up unconditionally, and commits only on an accepted private Raw Up. A missing or unmatched Raw sequence therefore turns a previously usable native click into animation followed by Cancel/no action.
- Not observed in this investigation: which packets actually arrive on the affected machine. Missing WM_INPUT, Raw Down filtering, timestamp/point mismatch, missing Raw Up, desktop check failure, and unrelated cancellation must not be presented as measured facts.

### Exact regression path

1. `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp:7433-7493`, `QueueWindowMessageInLayoutSpace`: native Down receives preserved screen/time metadata and is enqueued. At **7442-7445 every WM_LBUTTONUP is discarded**, regardless of Raw capability/session binding; active mouse Move is discarded as well.
2. Same file **746-758**, `TryReadBarPointerMessage`: `BeginMouse` may only arm a pending match yet returns true. Bar starts a Mouse Session with generation 0, records state, then returns the original native Down to the business handler. Actual Raw confirmation is not required for initial visual feedback.
3. Same file **4151-4154** sets the pressed visual and updates rendering. At **4162** Cancel breaks the loop; **4167** executes click only for explicit `WM_LBUTTONUP && !lbutton`.
4. Same file **648-683** private Raw Down binds the real generation and Raw Up produces the locally adapted Up. At **762**, the reader also rejects native mouse Up if one reaches it. There is no native completion fallback.
5. `Inkeys/Inkeys/Input/UiPointerInput.cpp:298-318`: historical Raw Down must match native Down **exact `messageTime`, screen.x, screen.y**; otherwise `Begin` leaves the channel armed and returns true. `Append` **212-242** requires the same exact match for future packets; a later-time Down or Up cancels pending binding. **171-178** cancels unbound starts after 1000 ms. Thus missing and merely unmatched Raw Down can produce indistinguishable user symptoms.

### Registration, parser, and cancellation gates

- `UiPointerInput.cpp:441-461`: sole provider registers mouse usage 1/2 with `RIDEV_INPUTSINK`, valid owner HWND/thread, and no `RIDEV_NOLEGACY`. `available=true` means `RegisterRawInputDevices` succeeded; **487-491 `RawMouseAvailable()` returns registration state**, not recent WM_INPUT reception or ability to read across integrity levels. It must not be used as a real capability probe.
- `Bar.Interaction.cpp:863-871`: WM_INPUT is passed to `HandleRawMouse`, then existing activity handling and DefWindowProc. No production reception/cancellation/match diagnostic is emitted here or in the provider.
- `UiPointerInput.cpp:529-585`: GetRawInputData errors/short header cancel every active/pending channel (**536-539**); wrong type/short mouse/promoted extra-info signature return false (**542-544**). A valid packet uses dispatch MSG.pt/time (**550-557**), otherwise current GetCursorPos/GetMessageTime (**558-565**); cursor failure cancels. Physical left Down/Up flags drive the provider (**569-584**). The real parser is not exercised by the current headless tests.
- `UiPointerInput.cpp:494-521`: active checks compare input and calling-thread desktop names; OpenInputDesktop/read/name failure invokes global InputUnavailable Cancel. An ordinary elevated foreground window is not itself a desktop switch. This path is a lower-priority candidate until its return/error is recorded.
- `Bar.Interaction.cpp:627-645`: desktop failure, mode revision/fold change, and producer cancellation serial also cancel before draining the private channel. Animation alone cannot distinguish these paths.
- `Inkeys/Inkeys/Window/Window.MessageDispatch.h:33-40` correctly preserves the current dispatch snapshot; `Window.cpp:930-937` scopes the existing queue dispatch. This proves sampling provenance, **not an equality contract between independently produced WM_INPUT and WM_LBUTTONDOWN messages**.

### Candidate priority

1. **High priority, structural regression proven:** mandatory Raw-only completion removed the valid native Up path. Elevated foreground is precisely a condition in which background-registration success cannot establish cross-integrity input access. Actual WM_INPUT absence must still be measured.
2. **High priority independent robustness issue:** exact time+point binding is an application assumption, not a public Win32 guarantee found in MSG/Raw API contracts. A 1 ms or 1 pixel discrepancy can leave a legitimate click pending/canceled while still drawing its press animation. Do not arbitrarily widen matching and risk attaching another press; revise acquisition/ownership deliberately.
3. **Lower priority pending evidence:** parser filter/read failure, input-desktop probe failure, explicit mode/capture/cancel transitions, stolen registration. These have identifiable code gates but no runtime evidence in the user's report.

### Tests and why they did not establish real-event behavior

- `InkeysHeadlessTests/ui_pointer_tests.cpp:16-24` creates Raw samples by assigning coordinates/time directly. **151-154**, **169-175**, and same-time tests deliberately give BeginMouse identical values. They verify the implementation's exact-match policy, not that Windows supplies equal metadata.
- **238-258** creates artificial MSG structures and publishes copied samples with matching BeginMouse values. It checks preservation of the synthetic history but does not call GetRawInputData or verify actual WM_INPUT/legacy correspondence.
- **291-310** expects pending timeout to Cancel and bound stationary holds to persist; **316-325** expects mismatching-time Down to Cancel. Those assertions can all pass while the user cannot click under elevated foreground.
- There is no no-HWND test of Bar accepting native Down and valid native Up with no corresponding Raw Down. There is no runtime elevated-foreground event trace. Existing automated PASS must not be presented as UIPI/real-device validation.

### Minimal observability recommendation

Record one bounded interaction trace, rather than change hooks or run a GUI without authorization:

- Native Down/Up receipt **before discard**, logical button, HWND/client identity, screen/time, source signature, ordinal.
- Raw packet arrival and GetRawInputData result/last-error, type, left flags, extra-info filter result, MSG snapshot/fallback provenance, screen/time, generation; counts for ignored packets.
- Begin result split into bound/pending/rejected; candidate Raw vs native time and point deltas; timeout, exact cancellation reason and its site.
- Desktop probe stage/result/error plus names; registration success/error and actual registered sink; token integrity/UIAccess and foreground process integrity for the reproducer.
- Final session outcome and click commit/cancel. Rate-limit movement, preserve all edges. Current `CancelReason::InputUnavailable` conflates too many branches to diagnose by reason alone.

### Minimal fix direction, not implemented

- Preserve native mouse Down/explicit Up as authoritative for a matched in-window click even when Raw is unavailable or never binds. Raw can extend the same owned gesture outside the HWND, not become an unconditional prerequisite for every click. Keep Raw outside HiMsg and avoid inferring release from missing Move/MK_LBUTTON/global key state.
- Native fallback requires explicit session/source/ordinal ordering and duplicate retirement so delayed Raw/legacy edges cannot complete another press. Simply letting every Up through after the current discard is unsafe.
- Remove the unsupported exact equality precondition as the only route to acquire a session; use an explicit broker acquisition model and tests for independent timing/positions. This is a design correction, not a recommendation to borrow arbitrary nearby Raw Down packets.
- Native fallback restores clicks while it cannot by itself promise full out-of-window dragging under a genuinely restricted Raw stream. If full cross-integrity coverage is required, discuss foreground/activation or deployment UIAccess/elevation alternatives explicitly; do not silently change privilege policy or the retained hooks.

## Files found

- `Inkeys/Inkeys/Input/UiPointerInput.h/.cpp`: provider, mailbox, registration, exact-match binding, cancellation and testing seams.
- `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp`: native queue filtering, generation-0 Down, private stream fusion and click commit.
- `Inkeys/Inkeys/Window/Window.MessageDispatch.h`, `Window.cpp`: original MSG snapshot around actual dispatch.
- `InkeysHeadlessTests/ui_pointer_tests.cpp`: deterministic samples; no real parser, HWND or integrity-boundary proof.
- `.trellis/tasks/10-17-ui-pointer-session/{prd,design,implement,validation}.md`: intended contracts and explicit real-device validation limits. PRD retains an initial duplicate TBD scaffold before its populated section; no edits made.

## External references

- [RAWINPUTDEVICE](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawinputdevice): RIDEV_INPUTSINK permits background reception; it is not a documented UIPI exemption. Minimum client XP.
- [UIAccess policy reference](https://learn.microsoft.com/en-us/previous-versions/windows/it-pro/windows-server-2012-r2-and-2012/jj852244(v=ws.11)): applies to Win7 and lists cross-integrity reading through raw input among UIAccess capabilities. This directly contradicts treating background registration as unrestricted cross-integrity access; it does not independently prove this machine's missing packets.
- [MSG](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-msg): time and pt refer to that message's posting; no equality promise between legacy and Raw messages.
- [RAWINPUTHEADER](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawinputheader): header.wParam is WM_INPUT wParam; zero hDevice can be precision touchpad. The current exact TryGet wParam comparison is supported, and null-device filtering is not this parser's problem.
- [WM_INPUT](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-input): foreground/background code and HRAWINPUT reading contract; no cross-integrity guarantee.

## Related specs

- `.trellis/spec/native-desktop/ui-pointer-session.md`: currently enshrines exact matching and Raw-only completion; must be revised if the fallback/acquisition design is corrected. It already says no UIPI immunity claim.
- `.trellis/spec/native-desktop/index.md`: evidence levels, ownership and platform-validation limits.
- `.trellis/spec/native-desktop/input-and-ink.md`: distinct hardware/producer/consumer evidence and bounded diagnostics.

## Caveats / Not Found

- No production/test/spec changes, hook changes, GUI, HWND creation, compile, Git operation or runtime input capture performed.
- Only this research file was written. Parent should compare findings against its independent Bar/PageControl tracing before selecting a fix.
- The symptom is consistent with absent/unmatched Raw edges; diagnosing all of it as "UIPI definitely blocks WM_INPUT here" without a trace would overstate the evidence.
