# Research: unified UI3 input ownership and lifecycle

- Query: Unify the main Bar, PPT paging and whiteboard paging/menu/theme interaction while retaining separate HWND rendering targets.
- Scope: internal; planning research only, no product implementation or GUI execution.
- Date: 2026-10-15

## Findings

### Current thread and ownership map

- `Inkeys/Inkeys/Window/Window.cpp:235,964` creates a single overlay owner thread for Freeze, both Drawpad HWNDs, the four PPT paging HWNDs and Bar. Settings has a separate owner thread. Separate UI targets do not require separate Win32 threads today.
- `Inkeys/Inkeys/UI/Bar/Bar.Initialization.cpp:210-231` registers the Bar render callback with RenderPipeline and starts an independent `interactionThread` running `BarUISetClass::Interact()`.
- `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp:458-488,2779-2815,6012-6067` polls a per-HWND HiMsg queue and dispatches Bar interaction stages. Numerous stage handlers enter nested wait/get/sleep loops; these are not resumable event handlers. Bar dragging also runs a pointer-polling loop (`7230-7270`).
- `Inkeys/Inkeys/UI/PageControl/PageControl.cpp:2204-2609` handles PPT/WB hover, press, touch, dragging and capture directly in WndProc on the overlay owner. Its Scene state is also accessed by the RenderPipeline thread under `renderTransactionMutex`.
- PageControl long-press deadlines are advanced in `RenderSurface` (`1944-1977`), and callback emission happens after the render lock (`2096`). Thus input semantics depend on presentation cadence today.
- `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cpp:658-687,965-978` already offers `PostControl` and one serial owner, wakes it independently of frame continuation, and drains posted tasks before device recovery. This is a suitable dispatch seam, but its unbounded closure queue must not become the high-rate input store.
- Bar WndProc still modifies some interaction state directly, including capture, cancellation, raw-cursor timers and touch (`Bar.Interaction.cpp:546-1035`). Existing Bar is not itself a complete single-owner solution.

### Business callback boundaries that should survive

- PageControl callbacks are copied under `callbackMutex` and invoked after releasing it (`PageControl.cpp:551-590,636-657`). Pointer-up explicitly passes `invokeCallback=false` to Scene and invokes business after releasing the Scene lock (`2472-2521`). Keep this separation; do not invoke the same button callback a second time from a shared generic click path.
- `Inkeys/IdtPlug-in.cpp:105-137,964-969` routes PPT actions to an existing session-token-bearing FIFO and rejects another page command while one is outstanding. Settings persistence has an existing queue. Keep these owners and their backpressure.
- `Inkeys/IdtState.cpp:783-846` makes WB open/close/page actions request publications and sends theme requests with page/generation checks to Draw3. Keep these business request interfaces; UI runtime must not own document/history/storage state.
- PageControl minimize currently calls Window Service directly (`PageControl.cpp:648-653`). Make it a window effect, preserving the PPT prohibition at business/window validation boundaries.
- `PageControl.cpp:1823-1844` acknowledges a PPT page only after the required visible surfaces commit; preserve the paired page/target contract with Draw3, not a generic animation-complete acknowledgment.
- Blocking main-Bar right-click close confirmation currently runs in the interaction session (`Bar.Interaction.cpp:3907-3930`). It cannot run on the new shared UI owner; dispatch the modal business action and return a revision-tagged completion.

### Recommended exact owner and event flow

Choose the existing RenderPipeline serial thread as the owner of all mutable UI3 runtime nodes, layout targets, hover/press/gesture state and animation clocks. Keep Win32 ownership with Window Service and document/COM/storage ownership unchanged. Retire the Bar interaction worker after its blocking gestures have been made resumable. Merely polling all HWND channels from the old `Interact()` loop would retain its starvation and split-ownership faults.

Introduce a shared UI input mailbox and one scheduled-drain flag. Producer submission appends an event and posts one `PostControl` drain when no drain is pending; drain re-arms itself if producers raced with completion. No per-Move closure allocation, no polling sleeps. Enqueue must be nonblocking for low-level hooks and WndProc. Coalesce only consecutive Move records from the same surface generation/contact, never across Down/Up/Cancel/capture results/key records or a change in coordinate snapshot. If capacity cannot preserve a contact boundary, schedule an explicit Cancel and clear that contact, rather than silently dropping Up.

The normalized event carries: surface identity and HWND lifetime generation, monotonically ordered event serial, kind (pointer move/down/up/leave, wheel, key down/up, cancel, capture-result/lost, window-effect result), source (mouse/touch/pen/keyboard), contact ID, full-width screen pixels, buttons/modifiers, sample time, presented-layout revision and capture/effect sequence. Avoid ExMessage's short-coordinate truncation in the new internal API. Transform through the captured presented-layout snapshot, not the current target layout after an HWND resize.

Use a small shared runtime interface: `SubmitInput(event)`, `DispatchPendingInput(now)`, `AdvanceInteractionDeadlines(now)`, `CancelSurface(surface,generation,reason)` and `PublishPresentedInput(snapshot)`. Each surface owns its button list, panel tree, current gesture and identity-bound action bindings; dispatch and ordinary button press/release/hit rules are common. The main Bar's special picker/slider/fine dial and PPT pair-drag/repeat remain gesture policies participating in the same resumable session protocol (`OnInput`, `OnTick`, `Cancel`, next deadline). They do not own threads or nested pumps.

Generic processing returns intents (activate button, change control value, move pair, start/release capture) rather than making arbitrary business calls under runtime locks. Controller adapters validate their opaque identity token before issuing business requests. Whiteboard tokens retain generation/page and PPT tokens retain session/layout epoch/target revision. The runtime itself need not understand PPT COM identities or `.uink` pages.

### Win32 and lock details

- Publish an immutable hit snapshot only after successful visible commit of pixels and HWND bounds. It includes surface generation, actual transform/crop, interactive regions, panel/background input ownership, enabled/locked flags and keyboard claims. `WM_NCHITTEST` must answer from this snapshot without taking render/Scene locks. Transparent holes remain pass-through. Hide/destroy/workspace switching immediately invalidates the applicable input generation and cancels contacts.
- WndProc performs immediate system work only: return `MA_NOACTIVATE`/tablet flags, translate and close `WM_TOUCH` handles, filter duplicate compatibility mouse, use snapshot hit acceptance, track mouse leave, and execute capture/window effects on the proper owner. It enqueues semantic input and returns. No Scene advance, no business callback and no waiting for rendering.
- `GetCapture()` is meaningful on the calling input queue. Do not move existing PageControl capture checks onto RenderPipeline and assume they still observe owner-thread capture. On accepted Down the HWND owner may establish capture immediately from the immutable hit snapshot and include its result/sequence in the queued Down; later release commands are generation/sequence guarded. `ReleaseCapture` may synchronously reenter WndProc with `WM_CAPTURECHANGED`; invalidate the token before calling it and make duplicate cancellation idempotent.
- Window Service `Submit` is synchronous when called off owner thread (`Window.cpp:1386-1406`). The renderer must not hold a lock needed by WndProc while waiting on it. Removing WndProc access to mutable UI nodes is necessary; adding another lock around the new queue does not solve this.
- Preserve Window Service owner/activation/z-order/minimize behavior. Do not merge Draw3/RTS or Settings execution into the UI3 runtime. RTS `canvasContactDown` and MouseHook outside-down remain producers of a nonblocking dismiss intent; they must not wait on UI or window owners.

### Special lifecycle requirements

- PPT directions: preserve immediate action on Down, OS keyboard initial-delay/rate settings, cancellation after leaving the original direction or losing capture, and one EndShow at the terminal page. Move repeat deadlines out of render callbacks into the shared interaction deadline scheduler; one late deadline emits at most one request, with no catch-up burst. Keep existing page-command-outstanding backpressure.
- PPT page/drag handle: preserve click-to-preview, threshold transition from Page press to pair drag, immediate dedicated-handle drag, session/epoch rejection, collision/bounds/DPI clamping, pair-version ownership, successful presentation acknowledgment and rollback on cancel. UI owner computes candidates. Owner-thread direct pair translation becomes a generation/revision-tagged window effect with an acknowledgment; do not leave a second geometry policy in WndProc. Failed or stale effects retain the last successful presentation and retry via the common presenter, never falsely persist an uncommitted candidate.
- Keyboard: global hook reads a published keyboard-claim snapshot and queues only currently owned keys; it must not read mutable runtime structs. Preserve WB menu Escape suppression for the entire Down/repeat/Up sequence so a closed menu does not leak a late Esc to background PPT. Preserve existing eraser/recall focus-or-pointer routing and picker key policy.
- Cancel/stop: workspace switch, input lock, minimize, hide/destroy, capture loss, multi-touch primary takeover and shutdown all funnel through one idempotent cancellation contract. Clear pressed/hover/repeat/drag state, close affected popup groups, schedule owner-thread release, discard old-generation completions and prevent late clicks. Stop input producers before draining/cancelling runtime; unregister/drain render callbacks before destroying HWNDs.
- Retain fixture provenance and authorized input seams while adapting them to normalized events. Do not expose a general production input-injection API solely for testing.

### Minimum verification

Pure shared-runtime tests should cover interleaved events for five targets, unchanged Down/Up ordering under Move storms, cross-HWND capture steal, Cancel during a drag/held repeat, no late activation after scene generation change, screen coordinates across HWND resize/DPI/presentation crop, touch primary takeover and duplicate-mouse filtering, full Escape suppression, nonblocking picker/fine-dial interaction while another surface receives input, and idempotent shutdown.

Preserve the current PPT pair-drag/session and page-ready tests, and drive the new production input adapter in headless tests. Shared render/input owner alone cannot prove frame performance; measure combined main-Bar/PPT/WB scheduling and readback/present costs separately after the rendering transaction is unified.

## Files found

- `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp`: current HWND adapter, blocking interaction worker and specialized gestures.
- `Inkeys/Inkeys/UI/Bar/Bar.Initialization.cpp`: independent interaction-thread lifecycle.
- `Inkeys/Inkeys/UI/PageControl/PageControl.cpp`: shared four-HWND controller, direct WndProc interaction and presentation acknowledgments.
- `Inkeys/Inkeys/UI/PageControl/PageControl.cppm`: repeat/touch/drag policy contracts and business callback interfaces.
- `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cpp[ m ]`: serial render owner and control-task dispatch seam.
- `Inkeys/Inkeys/Window/Window.cpp`: HWND ownership, synchronous commands, capture and group visibility.
- `Inkeys/IdtDrawpad.cpp`: low-level keyboard claim/Escape routing.
- `Inkeys/IdtState.cpp`, `Inkeys/IdtPlug-in.cpp`: WB request boundary and PPT business command FIFO.

## Related specs

- `.trellis/spec/native-desktop/rendering-and-ui.md`: Window Service thread/lock/owner, touch compatibility and shutdown contracts.
- `.trellis/spec/native-desktop/whiteboard.md`: same-HWND menus, transparent input, scene transitions and identity-safe theme actions.
- `.trellis/spec/ppt-interop/native-session-ui3.md`: pair-version drag commits, session identity and visible-page acknowledgment contracts.

## External references

None used. Findings are grounded in current repository sources; no external API-version claims are needed for this planning question.

## Caveats / Not Found

- No product code changed, tests/builds or GUI executed during this research.
- The new interface names and mailbox policy above are proposed design, not currently implemented APIs.
- This scoped thread/lifecycle research does not attribute a percentage of current stutter to input, GetDC, WARP, clipping or opacity layers. A unified owner cannot hide a slow synchronous render/readback stage.
- Main dispatch requested no report writes, but this Trellis research role requires persisting every research topic under the supplied task's research directory; this file is the only write.
