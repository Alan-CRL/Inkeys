# Research: elevated foreground click release regression

- Query: Why do Bar buttons show their pressed animation but fail to execute when an elevated window is foreground after the pointer-session change?
- Scope: internal; read-only product/test/spec investigation. No GUI, hidden HWND, hook edits, or git operations performed by this researcher.
- Date: 2026-10-17

## Findings

### Confirmed regression mechanism

The current Bar adapter allows native mouse Down to enter control business before a matching Raw Down has been confirmed, but requires Raw Input exclusively for the completion edge. This makes an ordinary click depend on Raw availability and exact Raw/native pairing even while its native window messages are deliverable.

1. `Bar.Interaction.cpp:744-755`: native Down invokes `barPointerMouse.BeginMouse(screen, messageTime)`, sets `barMouseArmed`, claims the mouse, and begins a provisional `Session` with generation 0. The original Down is then returned to existing control handlers at `:771-774`.
2. `UiPointerInput.cpp:311-318`: `Provider::Begin` returns true when no matching Raw Down is in history. It merely sets `state.armed = true`; true does not mean Raw Down was received or bound. This is why visible Down feedback can occur with unavailable/missing/mismatched Raw packets. If the provider were already unavailable and Begin returned false, Bar would cancel before the ordinary control's pressed animation, so that specific failure is less consistent with the reported animation.
3. `Bar.Interaction.cpp:4151-4154`: the ordinary main-bar button immediately sets pressed visual and updates rendering. `:4157-4167` then waits, breaks on Cancel, and executes click only on explicit `WM_LBUTTONUP && !lbutton`. Actual `clickFunc` execution is at `:4196`; pressed visual is cleared at `:4220`.
4. `Bar.Interaction.cpp:7442-7445`: `QueueWindowMessageInLayoutSpace` unconditionally discards real mouse `WM_LBUTTONUP`, including when a Raw session has not bound yet. It also suppresses Move once the mouse is claimed. `:762-765` independently rejects legacy mouse Up/Move at the consumer. Native Up is therefore unavailable as completion evidence even if Windows delivered it to our window.
5. `Bar.Interaction.cpp:648-683`: only the private Raw channel generates the mouse `WM_LBUTTONUP` seen by the handlers; `Session::Accept` requires matching source/generation/device/contact (`UiPointerInput.cpp:60-70`).
6. With missing matching Raw Down, `UiPointerInput.cpp:171-179` expires the armed startup after 1000 ms and emits `InputUnavailable` Cancel. `CheckInputDesktop` calls this expiry at `:499`; the Bar wait loop calls the desktop check whenever active or armed (`Bar.Interaction.cpp:627-635`). `:658-661` converts any channel Cancel to a Bar cancel marker. This removes pressed feedback without executing the click, exactly matching the reported symptom shape.

The root session independently read the pre-change HEAD and supplied the baseline: native Up was not discarded there; WM_INPUT was only used by Draw3/light. This researcher did not run git, in accordance with the research role's prohibition.

### Scope of the dependency

This affects more than dragging. All Bar handlers using `WaitForBarInteractionMessage` / `TryGetBarInteractionMessage` now consume this adapter, including ordinary buttons, preset selections, color controls, thickness controls, and the main-button click/drag path.

`Seek` now waits through the same adapter (`Bar.Interaction.cpp:7303-7317`), marks any cancel as `gestureCancelled`, and passes that to `ShouldAllowBarBottomDockClick` (`:7389-7392`). A missing Raw completion therefore suppresses both toolbar dragging and main-button fold click. There is no ordinary native-message completion path left in the current adapter.

### PageControl contrast

PageControl is also Raw-dependent for physical mouse gestures, but defers its business Down:

- `PageControl.cpp:2324-2342`: native Down sets `pendingMouseDown`, calls `BeginMouse`, installs a timer, and drains; it does not call `scene.PointerDown` immediately.
- `:2201-2213`: only receipt of the matched Raw Down removes pending and dispatches the Down into PageControl business.
- `:2368-2370`: native Up is suppressed there too.
- `:2588-2600`: scene press/session begins only with a dispatched input sample.

Thus Bar's pressed-animation/no-action shape is explained by its earlier native Down feedback; PageControl may show no press at all if Raw never binds. The PageControl difference does not solve missing Raw input.

### What remains unproven about the elevated case

Static tracing proves that the change discarded a previously usable ordinary click path. It does not establish which of the following environmental/input conditions actually triggers the failure:

- No `WM_INPUT` reaches the Bar sink while the elevated window remains foreground.
- Raw arrives but matching fails. Current binding requires exact native/Raw message timestamp AND exact screen point (`UiPointerInput.cpp:302-307`, armed binding `:214-224`). A different later Down or Up cancels the pending bind (`:216-219`, `:240-242`); same-time different-point Down is skipped.
- Raw binds but Up never reaches the provider, or a Raw read fails (`:533-540`). A bound stationary session has no startup timeout, intentionally.
- `OpenInputDesktop` / `GetUserObjectInformationW` fails or desktop-name comparison fails (`:503-520`), causing immediate cancellation. The code does not cancel merely because foreground process integrity is elevated; elevation itself is not checked.
- Mode revision/fold changes cancel in `Bar.Interaction.cpp:627-635`, or window cancellation serial changes at `:638-646`.

Do not describe these alternatives as a proven UIPI/Raw Input platform limitation without runtime input evidence. The code also discards `cancelReason` when `MakeBarPointerCancel` creates its generic local cancel marker (`:504-513`), which makes visual symptom attribution impossible today.

### Existing capture and hook paths

- `WM_CAPTURECHANGED` only clears capture flags (`Bar.Interaction.cpp:1067-1072`); it does not itself terminate a Raw mouse session. Therefore this is not an immediate false Up caused by losing capture.
- `ShutdownWindowInput` publishes cancellation (`:1370-1391`), so explicit `WM_CANCELMODE` can cause the same outward result.
- The existing hook's `clickCollapsePending` delayed callback remains separate from Bar's old `ConfirmaNoMouMsgSignal` clearing (`MouseHook.cpp:45-74`, `:132-137`; `Bar.Interaction.cpp:851-854`). This known old issue may change mode/fold during long operations, but no evidence ties it specifically to elevated foreground or makes it necessary to explain this regression. Do not change hook code in this task.

### Diagnostic seams and coverage limits

- `UiPointerInput.cpp` has no runtime trace for registration success/failure, Raw edge arrival, native/Raw pairing, desktop check error, or cancellation reason. `RegisterRawInputDevices` and desktop Win32 failures are not logged with `GetLastError`.
- Bar adapter does not trace native Down, discarded native Up, provisional/bound state, or cancel cause.
- `PageControl.cpp:123-135` provides existing `[PageControlDrag]` stdout + OutputDebugString traces, but its `down` trace at `:2618` occurs only after Raw dispatch reaches business. Absence of this trace cannot distinguish missing Raw from binding failure.
- `InkeysHeadlessTests/ui_pointer_tests.cpp:291-321` explicitly tests pending Raw Down expiry/cancellation and mismatched future Down rejection. These are deterministic provider tests. They do not cover Bar receiving a valid native Down/Up while Raw is absent, nor OS integrity/foreground delivery. Passing them therefore does not validate preservation of ordinary click behavior in the reported environment.
- Useful additional diagnostic evidence, without changing hooks or launching GUI from the agent, would correlate native Down/Up screen/time, Raw Down/Up screen/time and arrival, bind state/generation, cancellation reason, desktop check Win32 error/name, mode revision/fold snapshot. This is a research recommendation only; no instrumentation was added.

## Files found

- `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp`: private pointer adapter, window producer, existing control handlers and main-button Seek.
- `Inkeys/Inkeys/Input/UiPointerInput.h/.cpp`: Raw provider, strict Raw/native pairing, channel/session ownership, startup expiry, desktop checks.
- `Inkeys/Inkeys/UI/PageControl/PageControl.cpp`: deferred native Down and separate PageControl state machine, existing drag diagnostics.
- `Inkeys/Inkeys/Window/Window.MessageDispatch.h`: full MSG.pt/time sampling; used by Raw and native Down.
- `Inkeys/Inkeys/Window/Window.cpp:936-937`: dispatch scope around DispatchMessageW.
- `InkeysHeadlessTests/ui_pointer_tests.cpp`: deterministic provider/session tests, no elevated OS delivery test.
- `Inkeys/Inkeys/Input/MouseHook.cpp`: read-only context for existing delayed collapse path; excluded from changes.

## Related specs

- `.trellis/spec/native-desktop/ui-pointer-session.md`: current contract says native Down is only hit seed and Raw is authoritative for mouse release. This contract currently makes ordinary clicks depend on unverified Raw delivery; it should not be treated as evidence that platform delivery is reliable.
- `.trellis/spec/native-desktop/input-and-ink.md`: UI session remains distinct from Draw3 contact pipeline.
- `.trellis/tasks/10-17-ui-pointer-session/design.md`: explicitly records administrator/Win7 device tests as unverified; implementation's automated success cannot prove those outcomes.

## External references

None used for this source-level topic. Windows/UIPI delivery guarantees require a separate official-document/runtime investigation.

## Caveats / Not Found

No production, test, spec, or hook file changed. No build/test or GUI executed. Only this research file was written. No git operation performed. The exact missing/mismatched Raw condition on the user's machine remains unobserved; the deterministic click regression mechanism is confirmed by current code and the root-provided baseline.
