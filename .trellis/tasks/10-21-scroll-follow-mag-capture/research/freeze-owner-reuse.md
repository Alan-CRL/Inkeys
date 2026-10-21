# Research: reuse existing Freeze Mag owner and controls

> 历史提案，非当前实施范围：后续用户要求保持现有定格线程，只做原 Mag 的软件内 PrintWindow(2) 验证。此文中的 backend/reservation/通用销毁接线及回调尚不可用的初步判断均未作为当前实现合同；最新事实见 design.md 与 research/freeze-printwindow.md。

- Query: Can capture reuse actual Freeze Host/Child, update/exclude first, allow settle latency and read with PrintWindow(2), without second Mag owner?
- Scope: internal; external references recorded for completion-signal boundaries
- Date: 2026-10-21

## Findings

### Actual thread model (important correction)

- `Inkeys/IdtMain.cpp:3327` builds MagnifierHost WindowSpec: initial hidden, primary screen size minus 1px in height, popup/clipchildren, overlay exStyle. beforeCreate=PrepareMagnifierWindow, created=MagnifierHostCreated, destroyed=ShutdownMagnifierWindow. Child is WC_MAGNIFIER/WS_CHILD|WS_VISIBLE|MS_CLIPAROUNDCURSOR with noactivation, created=MagnifierChildCreated.
- `Inkeys/Inkeys/Window/Window.cpp:918` RunGroup(false) owns Overlay group, initializes STA, creates both HWNDs and pumps their messages. This same group also owns other interactive overlay windows: blocking it affects product UI.
- `Inkeys/IdtMain.cpp:3714` starts separate `jthread(MagnifierThread)`.
- `Inkeys/IdtMagnification.cpp:340` MagnifierThread waits Coordinator requests, invokes Win32MagnifierOperations. Thus existing Freeze initialization/HWND creation thread and actual Mag Filter/Source/Redraw calling thread ARE ALREADY DISTINCT. Prior blanket claims that Freeze updates run on HWND owner are false. Show/Hide are marshaled by Window Service, direct Mag calls run coordinator worker.
- This does not prove a new independent Mag HWND owner can work: user now asks reuse actual pre-existing controls, avoiding that difference.

### Existing reusable transaction

- `IdtMagnification.cpp:72` CurrentTargets reads role handles; `:81` AreCurrentTargets checks ready, PID, current handles and parent relation. These checks are validation, NOT lifetime pinning: destruction can race after return.
- `:96` BuildCurrentFilterList enumerates ten known roles: MagnifierHost, Freeze, DrawpadPresentation, Drawpad, four PPT controls, Bar, Setting; shared BuildFilterList validates PID/top-level/deduplicates (`Freeze.MagnifierCoordinator.h:14`). Reuse this exact exclusion path. It currently omits arbitrary same-process popup windows, so optional complete EnumWindows augmentation requires scope review rather than claiming current list covers everything.
- Win32MagnifierOperations `:134` PrepareHidden sets alpha=0 then calls Service.Show for host/child. It preserves visible drawable lifecycle while user sees no content.
- `:149` SubmitFilter; `:157` SubmitSource currently hardcodes (0,0,SM_CXSCREEN-1,SM_CYSCREEN-1), NOT Display primary bounds. New adapter needs explicit source argument and geometry validation (actual host height one less). Do not silently publish full 2880x1920 frame metadata from existing shorter child.
- `:167` Invalidate and `:172` Redraw use RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN; `:178` Reveal raises alpha=255; capture must never call Reveal.
- `:184` Conceal sets alpha=0 then hides both via Service. Avoid hiding between capture update/read, because hidden lifecycle gave invalid contents in prior probes.
- `Freeze.MagnifierCoordinator.h:84` Coordinator serializes requests with mutex/condition_variable, revision and visible state. It has no capture reservation token today. Snapshot test alone cannot grant exclusive controls.

### Suggested narrow adapter contract (proposal, not implemented)

Keep independent MagCaptureService sampling/monitor/quality logic, replace its own HWND creation branch with an injected backend/adapter backed by existing Freeze controls. IdtMagnification owns adapter state and serialization; no arbitrary WindowGroup refactor.

Suggested operations (names illustrative):

- TryBeginCapture(session, explicitSource, callback/requestState) -> asynchronous accepted/rejected. Atomically reserve against Freeze activation/pending requests, pin HWND generation and runtime lifetime. Reject when Freeze active.
- PrepareCaptureFrame(session, sequence) -> verify reservation/generation, PrepareHidden if needed, SubmitFilter, explicitSource, Invalidate, Redraw. Return stage/errors and completion timestamp, NOT a claim of fresh frame.
- ReadCaptureFrame: after longer first settle and shorter steady settle on worker, PrintWindow(child, DIB DC,2); worker owns DIB. Maintain in-flight read reference until synchronous call ACTUALLY returns. Do not let callback own raw adapter stack lifetime.
- EndCapture: immediately close publication and set cancellation; no owner-thread join. After outstanding update/read exits, owner may Conceal/restore and release reservation. Freeze activation must enqueue behind this barrier, then perform its unchanged PrepareHidden/Filter/Source/Invalidate/Redraw/Reveal.

The lowest-risk serialization is extending existing MagnifierThread Coordinator to service capture requests (and retain first/steady due timestamps using timed waits), rather than simultaneous Freeze and capture worker mutation. UI Stop only marks cancellation. If another capture worker reads concurrently, mutation/disposal still require read-in-flight handshake; no lock held while calling Service.Show/Hide or PrintWindow that can enter the owner thread.

Owner commands are possible via Window Service but existing API does not provide arbitrary callback dispatch. `Window.cpp:681` CommandType and `:706` Command would need a narrow new kind, handled by Execute at :1954. Submit at :1459 queues/promise-waits cross-thread and directly executes when already on owner; SubmitNoWait :1426 queues without promise. This is useful for lifecycle callbacks, not evidence that it is safe to place PrintWindow on UI owner.

### Destruction barrier is mandatory, currently missing

`Window.cpp:1315` DestroyGroup destroys MagnifierChild before MagnifierHost. DestroyWindowFor marks not-ready then directly DestroyWindow around :1350. Only after destruction do spec.destroyed callbacks execute at :1414. Therefore ShutdownMagnifierWindow (:324) is too late to prevent HWND teardown under in-flight capture. Runtime lease alone prevents DLL uninitialization but DOES NOT pin HWND.

Need pre-destroy lifecycle hook/reservation veto/async drain before both Mag child and host destruction. Reject new reads, keep owner message pump alive until in-flight calls return; only then destroy. RunGroup currently leaves main loop and immediately calls DestroyGroup (:1010), so merely delaying a callback or skipping DestroyWindow is insufficient: owner thread exit destroys owned HWNDs regardless. If PrintWindow hangs permanently, do not force owner/DIB/runtime destruction; use existing failed-cleanup deadline/report policy, and do not pretend arbitrary hang is solved by lease. Owner teardown must not wait on worker while worker awaits owner message handling.

### Test feasibility

Headless project already compiles `Window.cpp/.cppm`, Freeze.cppm, MagnificationRuntime.cpp, DibSurface/capture code and mag_capture_tests.cpp. It DOES NOT compile IdtMagnification.cpp, which depends on legacy Idt globals/config/draw and main app logger. A new native integration test can create Service with narrow test WindowSpecs as existing Draw3.HiddenWindowTest does, exercising actual Window Service STA owner; actual adapter/backend should be extracted into a small normal header/CPP with explicit dependencies to reuse production logic, avoiding adding whole legacy Idt graph to Headless.

`Draw3.HiddenWindowTest.cpp:3764`/`:3946` already create hidden test MagnifierHost-role ordinary windows; these tests are patterns for Window Service lifecycle, not actual WC_MAGNIFIER pixel evidence.

Real test must create actual WC_MAGNIFIER and runtime initialization through same owner lifecycle, external known pattern + same-process blue exclusion overlay, source coordinate/dim checks, first wait and changing/static content, then Stop -> Freeze reveal/normal hide -> recapture, resources. Previous-frame permitted; recurring stale frame/non-updating content and all-grey/all-black initial F0 remain failures. Capture mutex does not replace this test.

### Refresh completion and references

No public completion token is established by current source path. MagSetWindowSource BOOL only accepts source, RedrawWindow UPDATENOW processes required window painting; neither verifies latest compositor pixels. Parent independently checked DwmFlush and deprecated MagImageScalingCallback and reported neither is usable completion contract here. Waiting must be cancellable worker wait/timed coordinator wait while owner pumps messages; never Sleep on Overlay owner. Fixed wait is latency policy, not freshness proof.

- https://learn.microsoft.com/en-us/windows/win32/api/magnification/nf-magnification-magsetwindowsource
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-redrawwindow
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-printwindow
- https://learn.microsoft.com/en-us/windows/win32/api/magnification/nf-magnification-magsetwindowfilterlist

PrintWindow is synchronous, and caller location on worker does not guarantee HWND owner never participates or blocks. Worker call across this actual Mag control must be tested; current code inspection cannot prove that safety property.

## Related specs

`.trellis/spec/native-desktop/scroll-follow-capture.md` records current old independent-owner contract and hard gate; new adapter is a user-authorized direction change, not an already implemented contract. Child design.md already records user acceptance of previous-frame latency. Spec update belongs parent, not this research role.

## Caveats / Not Found

No production edits, tests, GUI, or git operation performed by researcher. No trustworthy freshness event found in repository. No existing lifetime pin / capture reservation for actual Freeze HWND exists. A phase-one adapter without lifecycle guard cannot safely be claimed production-ready. Reusing these controls also inherits 1px-height and origin assumptions which must be made explicit in metadata and product geometry handling.

## Follow-up: normal shutdown ordering / ordinary geometry updates

Normal product shutdown already provides a useful producer-before-HWND destruction barrier:

- `IdtMain.cpp:367` SetOffSignal marks shutdown, `:369` WindowService.BeginShutdown rejects future display operations; this does not stop owner or destroy HWNDs.
- `IdtMain.cpp:391` calls StopMagnifierCoordinator to wake its wait.
- `IdtMain.cpp:3831` joins magnifierThread before `:3838` WindowService.StopAndJoin, while owner message loop remains alive to answer painting/show/hide traffic. Other business producer threads are also joined in this section.
- Therefore if capture update/read work is owned by this SAME MagnifierThread and its outstanding read must finish before that thread exits, normal shutdown already drains capture before destroying controls. Alternatively add explicit capture stop+drain beside existing magnifier join before WindowService.StopAndJoin. Do not block the HWND owner waiting on a worker which may synchronously need it. A permanently stuck read uses existing supervised process exit, not artificial completion.
- Startup rollback/optional target failure StopAndJoin sites in main (`:3512`,`:3604`,`:3615`,`:3634`,`:3652`,`:3678`) precede magnifier worker creation at `:3714`, so a capture service unavailable until successful startup cannot overlap those particular cleanup sites.
- No production Service.Destroy call site was found in `Inkeys/**/*.cpp`/`*.cppm` (search returned wrapper only). Window Service API nevertheless exposes Destroy(role), and `DestroyDynamic` at `Window.cpp:2674` can destroy MagnifierChild (not overlay chain root). Parent Host is guarded by dependent chain/control checks at `:2704`/`:2726`.
- Ordinary SetBounds/show/hide/minimize/whiteboard transitions adjust windows rather than recreate Mag pair; Resize did not reveal a destruction/recreate path. Geometry changes should cancel capture to protect fixed source/dimensions, but do not themselves imply destroyed HWND.
- Owner RunGroup still exits its loop on WAIT_FAILED (`Window.cpp:980` region) or stop token and calls DestroyGroup at `:1008`; startup group failure uses :945. Unexpected owner exit is outside main producer-drain sequence. Captured HWND generation must become invalid and publication be revoked. This exception means existing shutdown sequencing alone is not an unrestricted HWND pin guarantee for all Service users/failure modes.

Conclusion refinement: a generic preDestroy hook is NOT strictly necessary just for ordinary main shutdown if capture is integrated into the existing producer lifetime. Prefer that smaller integration and explicitly cover/restrict dynamic destroy and abnormal owner termination instead of automatically adding broad Window lifecycle machinery. Earlier generic-hook recommendation describes the unrestricted independently managed capture-worker case.
