# Draw3 PPT page boundary implementation

## Evidence and root cause
- `DrawingController::Run` consumed canvas commands only when `active.empty()` (both the command loop and frame callers). A physically held contact, including a reconnect candidate, could indefinitely delay the trusted target. Existing successful Present -> workspaceChanged ordering was already correct and is retained.
- `Host::RuntimeSnapshot` read its runtime revision after readiness fields. A publication between the field reads and the revision read could yield old fields plus a new wait baseline. The revision is now captured before the fields; the native consumer must wait on that captured revision.
- Warm-slot activation already restores the destination's old target before `StablePresentationTopologyChanged`; this order is retained. New hidden tests exercise real park/swap and SlideID reorder, not a copied formula.
- Rejected physical Down previously used Recycle while the producer still owned the route. Recycle cannot release a Producing slot; the new discard-until-terminal handoff is also used for existing Whiteboard-selection/load-pending rejection paths.

## Changes and interfaces
- PresentationTarget and PresentationReadyIdentity include `uint64_t sessionRevision`, including idempotency and equality. StateBridge Reset retains its monotonically increasing target counter across Host generations so restarted runtime cannot reuse an old UI acknowledgment identity.
- `bool PublishProductPresentationUiReady(const Bridge::PresentationReadyIdentity&) noexcept` only accepts the bridge's latest identity that has successfully presented. StartProduct requires this acknowledgment; standalone Host opts in with `HostStartOptions::requirePresentationUiReady` (default false).
- `bool SetProductPresentationInputSuspended(const Bridge::PresentationReadyIdentity&, bool) noexcept` keeps the document alive and gates Unknown/EndScreen input. A stale identity cannot suspend or resume a different target. Suspension clears the old UI ack and rejects acknowledgments while suspended; resume requires a fresh UI commit because Unknown may have replaced the number with a placeholder. New target identity invalidates an old suspension.
- HostRuntimeSnapshot exposes `presentationInputReady` for diagnostics/tests. Publication/ready/UI acknowledgment and suspension share presentationTargetMutex. Snapshot wait baseline is captured before fields.
- ContactInputCoordinator stamps a Down admission generation. Closed-period or old-generation Down cannot become writable after reopening. `DiscardUntilTerminal` transfers a finished/rejected live route to Quarantined; Move is ignored, real Up/Cancel releases the slot exactly once and issues ControlWake. No extra thread/producer/Host or per-frame polling is added.
- DrawingController handles admission edges before incoming contacts and model updates. Accepted persistent strokes finish through existing model -> Stored/history -> L2 -> page-save paths at the last consumed position. Laser is canceled, touch pan/inertia and reconnect candidates are retired. Normal completed contacts and synthetic page-boundary completions use the same terminal ownership handoff.
- Adjacent unaccepted absolute target commands coalesce, with Clear/Undo/Redo/persistence boundaries remaining barriers. Scene-stamped commands still finish on their captured document, then Host explicitly restores latest state, including a latest Desktop state. Input remains gated during captured-scene restoration.
- Default-off `Draw3.PptTiming.h` exports `TracePptTiming(stage, session, target) noexcept`. Set INKEYS_PPT_TIMING=1 before launch. stderr and OutputDebugString include QPC and frequency. Stages: input_closed, contacts_sealed (old accepted target), document_switched, canvas_presented, page_ui_ack, input_open. QPC is a native observation, not a COM event timestamp.

## Tests added / updated
- draw3_contact_tests: production coordinator admission/reopen, queued Down rejection, ignored old Move, independent new Down, terminal slot reclamation, 128 concurrent Up-versus-quarantine handoffs.
- draw3_bridge_tests: same-target idempotency, same-document new-session identity change, captured command identity preserved across latest-page change.
- Draw3.HiddenWindowTest: all existing Presentation helper waits explicitly acknowledge UI; new real-controller still-down page boundary, canvas/UI separation, late ack, old Move/Up isolation, new input, rapid target changes, suspension/resume, A-B-A warm recovery and reordered stable SlideIDs. Uses existing hidden HWND and existing presenter modes, no visible UI.
- No new compiled translation unit; only header-only Draw3.PptTiming.h. All changed original files preserve encoding/BOM and CRLF.

## Validation status
- Scoped `git diff --check` passed before integration build.
- Compilation and headless/hidden runtime results are pending root's coordinated build; do not claim these tests ran until that report is recorded.
- Real Office/WPS, actual pen/mouse/touch drivers, native Esc and end-to-end timing: NOT VERIFIED. No baseline/post-change Office latency numbers exist yet.

## Manual / integration follow-up
Run full Debug|ARM64 solution, headless --no-window, then --draw3-hidden-test with INKEYS_PPT_TIMING=1 to distinguish synthetic stage timings from Office measurements. On actual PPT, hold each input type while directly jumping/rapidly reversing pages, then release and draw again; old ink must stay on the old SlideID and new input must target the visible page. Native must publish sessionRevision, acknowledge matching visible page commits (or no-visible-controls), and suspend without clearing the target on Unknown/EndScreen.

## Read-only native integration review
- Native expected-target callback checks local session/revision/page/count before calling Host; Host rechecks full latest identity and successful canvas readiness, so the unlocked callback gap cannot open an obsolete page.
- Native Unknown/EndScreen retains and suspends the last accepted target; valid state republishes page0->real target, satisfying the new fresh-ack resume requirement.
- Whiteboard Exiting initially returns to an isolated Presentation slot, then PptInfo republishes the latest target after phase Inactive. The isolated gap remains unwritable; Ppt facade session/layout is retained.
- True native exit publishes Desktop; the admission edge seals persistent old-page contacts before FIFO old-scene/save commands and the final workspace transition. Physical Up still retires the correct old route.
- A Host restart target-counter reuse and stale UI ack on suspension were found during integration review and fixed in owned code above.
- Timing reporting needs an explicit mapping: native observation/descriptor stage currently uses stateRevision, while Draw3 stages use targetRevision; those counters must not be mistaken for one correlation key. Root notified.

## Coordinated build correction
Full solution diagnostic build reported one C2065: sealPresentationContacts referenced hapticContinuousActive before its declaration. Moved that local declaration immediately before the lambda; other sealed-state locals and helper lambdas are already declared above the call site. TracePptTiming now accepts optional fourth `int64_t observedQpc = 0`; zero captures now, a supplied QPC preserves the original native observation timestamp while using the later accepted target revision. Existing three-argument calls are unchanged. Scoped diff check passed; root owns the rebuild.

## Final integration record

Root confirmed: full Debug|ARM64 solution, headless --no-window, managed tests, real Host hidden (including persisted cold recovery/reorder), final Bar/PageControl offscreen and diff check all exit0. Exact evidence and limitations are in verification.md. Generated .log files were preserved under D:/Project/Inkeys/Repo/Inkeys-draw/Build/Validation/ppt-ui3-scene-and-page-sync-20260925 (outside tracked task artifacts); use the same log basenames recorded above. No Office GUI/device result is implied.
