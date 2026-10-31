# Completion delivery and storage review — 2026-10-18

## Scope and verdict

Independent source review of the current PPT accepted-request terminal delivery repair, its real allocation-failure tests, the WindowController reserved terminal slot, Host delivery/restart lifecycle, CLI registration, and the Desktop index-lock timeout test. No child agent, build, executable, HWND, GUI, or commit was run by this reviewer. The root agent owns all runtime validation. Existing window owner/Z-order changes are another reviewer's responsibility and were not edited here.

**Corrected source verdict: root identified a missed harness compile guard in the initial review.** The completion fixture was initially compiled into Release. This reviewer has now guarded its Host.cpp definition, dedicated filesystem include, and Host.h declaration using the existing INKEYS_ENABLE_UI3_PRESENTATION_HARNESS boundary. Root owns strict CLI registration/off-gate handling in IdtMain.cpp. Production DrainPresentationCompletions remains unconditional. The earlier unconditional source-pass statement is withdrawn until these changes receive fresh build/runtime results.

Read parent implement/check manifests, PRD, design, C++ and resource/thread contracts, canvas-freeze executable contracts, and the admission/recovery audit. This review applies to the hashes below; subsequent changes need corresponding recheck.

## Accepted-request guarantee

- SubmitSave/SubmitLoad reserve a WorkItem list node, shared terminal payload, and the exact target/load/clear identity before returning Accepted. Allocation failure returns Invalid before the item enters the queue or accepted counters advance. ReplacedPending retains the existing intentional ordinary-tail save coalescing semantics; this is not a new promise to deliver one terminal per superseded save.
- The working list is a member constructed with the service. This matters for MSVC because even an empty std::list has an allocated sentinel. Run transfers the already accepted node by splice, avoiding a new list construction after dequeue.
- Completion identity is prepared before acceptance. Save/load processing and SnapshotTestFaults copy run inside the catch boundary. A failed load operation leaves the preprepared target, slotGeneration, loadKind, pageGuid and intervalOrdinal intact and sets IoError.
- Successful LoadPresentation returns a complete temporary before the nothrow move assignment replaces the reserved completion. An exception before return cannot partially overwrite that reserved identity. The current static assertions establish the payload/request move/default construction operations used by the publication path.
- PushCompletion releases the original work payload and splices the same node into completions. Counters change after that splice. There is no deque allocation or new shared control block at publication or shared-pointer retrieval. The value retrieval overload also uses a statically checked nothrow move.
- WAIT_ABANDONED remains successful acquisition. Timeout is IoError/failed, not NotFound or writable-empty. The 1000 ms limit concerns acquisition only, not arbitrary filesystem operations or total queue drain.

## Host, ordering, and lifecycle

- Host transfers the already reserved shared payload to WindowController. When the single reserved slot is occupied, Host retains the next terminal rather than dropping it or allocating a command queue node.
- commandsBeforePresentationCompletion counts exactly the commands preceding the terminal at successful enqueue. Only consuming one of those commands decrements the count. Commands queued afterward cannot overtake the terminal.
- A SetPresentationTarget after the reserved terminal cannot coalesce with the target before it: the new predicate permits coalescing only when at least one normal command exists after the barrier. Subsequent adjacent targets after the barrier can still coalesce normally.
- The reserved slot participates in HasPendingCanvasCommand, so the controller's existing command boundary covers the completion too. Taking the terminal publishes a wake outside the queue lock. This drives delivery of the retained successor without relying on a later stylus or persistence event.
- Attach/detach clear the slot and count together. Host cleanup resets its retained shared payload only after the drawing owner has joined (or before a drawing owner was created). Service Start calls CloseAndDrain before clearing old queues. Shared terminal ownership remains valid after the service node is removed.
- Ordinary CanvasCommand values contain scalar fields and shared pointers; their move assignment does not allocate. The new terminal path does not use the allocation-capable ordinary deque enqueue.

## Test adequacy and required root checks

- The DRAW3_TESTING allocation harness overrides actual operator new with a thread-local one-shot bad_alloc, rather than substituting a successful result. RequestCompletion, RequestIdentity, and RequestQueueNode exercise pre-accept failures for Save and Load.
- WorkerOperation failure covers Current and PreviousInterval loads and checks full identity, IoError, no loadedSnapshot, and unchanged persisted bytes. PublishBegin/PublishEnd arm/cancel the real allocation trap and prove publication did not allocate; retrieval performs the same check. Existing exception and normal persistence tests remain necessary alongside these new cases.
- The guarded --draw3-ppt-completion-delivery-test fixture must be dispatched before normal product initialization; root is correcting argument-aware registration to reject extra arguments or an off harness with exit 2. It uses real WindowController, ContactInputCoordinator, PresentationAutoSaveService, and the same Host drain function, without HWND/GPU/RTS initialization. It checks occupied-slot retention, ordinary command FIFO, target coalescing barriers, successor wake, real service terminal identities, and restart cleanup.
- Desktop TestIndexMutexTimeoutAndRetry is declared after MakeRequest, DateDirectory, WaitForIdle, ReadJson, and VerifyIndexedFiles, so the new helper calls have no declaration-order problem. Its separate service worker contends with the test thread's mutex, waits for real failed diagnostics, releases the lock before drain even under the old infinite implementation, then checks an indexed nonempty UInk commit after restart.
- Root should build the full solution Debug|ARM64, build the existing storage test target, run --presentation-autosave-only, --desktop-autosave-only, and --draw3-ppt-completion-delivery-test. The testing macro is target-wide in the storage test project; the full product build must also compile the non-DRAW3_TESTING path. A release/test configuration is useful if already part of the root verification plan; this report does not claim it ran.

## Residual boundaries

- This repair targets PPT accepted completions. PumpDesktopCompletions still takes a value, allocates make_shared, and uses the ordinary deque enqueue. A Desktop completion allocation failure can still reach the enclosing Run failure path. This is an existing separate boundary, reported to the root, and is not fixed by the Desktop mutex timeout alone.
- The mutex bound does not guarantee a fixed process-exit deadline; accepted work may have multiple acquisitions and filesystem/filter-driver calls may be slow. Persistent resource exhaustion can reject new requests before acceptance; original safety gates must remain closed where old ink has not loaded successfully.
- The production delivery CLI proves component integration and ordering, not a real Office/WPS/GPU presentation race. Real physical touch/pen and other hardware coverage remain separate from this source review.

## Source snapshot

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.PresentationAutoSave.cpp` SHA256 `2878fb73aa2bb11e174ab9b92ae2aae6ae35509a4941a033b15c673e01aae44b`; BOM=True, CRLF=2011, bare-LF=0.
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.PresentationAutoSave.cppm` SHA256 `5f286b7a8c63f754bb432db51257ce8e422d7785aad41f947dfda5b80697b7e6`; BOM=True, CRLF=222, bare-LF=0.
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp` SHA256 `0f8df3fd18e66e87e98c6432bcca99734946aa928700de8d5fafeefe0b10000b`; BOM=False, CRLF=2385, bare-LF=0.
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.h` SHA256 `af5743fe9270adaa2d486909afaf36662118290e91c8a8be090ce699e60b3232`; BOM=False, CRLF=346, bare-LF=0.
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp` SHA256 `c1c953baf28360332ec6da5ce98253ce6c863b8a7e0e646b1fc64291aaceb5f2`; BOM=True, CRLF=1865, bare-LF=0.
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cppm` SHA256 `657ff1a4e6ba0f80778d7d9ad2b797ee2c0a898d56ed8cf87a5affa7e2b9337e`; BOM=True, CRLF=352, bare-LF=0.
- `Inkeys/IdtMain.cpp` SHA256 `d2eda69a14753b43d1a2ce0bf4aafad40596d316223268385570dad577786cf9`; BOM=True, CRLF=3600, bare-LF=0.
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.AutoSave.cpp` SHA256 `645e847123063e431bc475947461d4711297a75a8b62806cd0e1aab8db314344`; BOM=True, CRLF=1392, bare-LF=0.
- `inkStrokeModelerTestTests/presentation_autosave_tests.cpp` SHA256 `37a199bee1dc4ec23fd11e9104b83e5a2d07cac3808eeb1b1e827ec0f4476ca3`; BOM=True, CRLF=3632, bare-LF=0.
- `inkStrokeModelerTestTests/desktop_autosave_tests.cpp` SHA256 `4dd34bd6fcc3bb054cd5d7f90d0a988c0761771d8e4bd465a5822a4a27fb04ac`; BOM=True, CRLF=913, bare-LF=0.
- `inkStrokeModelerTestTests/contact_input_tests.cpp` SHA256 `7fe90d5c67292cba1c1c2c7fc09931c5c73ff8073e87b14d67b88ea8de956f76`; BOM=True, CRLF=3528, bare-LF=0.

## Harness-boundary correction

The earlier review failed to catch that the newly added completion fixture had no compile guard. The guard now covers both its declaration and definition plus the new filesystem include. iostream/stdexcept were already present at HEAD; stdexcept remains required by existing ObservePresented runtime_error code, so preexisting includes stay unchanged. git diff --check for the two edited Host files passed. No compilation/executable was run by this reviewer. Root must rebuild after these edits and check harness-on success plus harness-off/extra-argument exit 2 without entering normal startup.

## Additional independent Window candidate review (2026-10-18)

**Current candidate is pending the root's final build and execution.** This is a read-only review of Window.cpp/window_tests.cpp and root-owned investigation evidence, added after the completion review and harness-boundary correction above. No product source was modified; no build, executable, HWND or desktop operation was performed by this reviewer. The source snapshot still contained temporary investigation code that root has scheduled for removal; that code is not approved as final product behavior.

### Candidate behavior and evidence

Read research/window-final-20261018.md and Build/CanvasFreezeInvestigation/final-20261018/window-owner-sequences-3.stderr.log. Earlier direct root SetWindowPos succeeded while the actual root band remained normal. Reapplying the same root operation did not reliably cure it; a root-only NOOWNERZORDER alternative also failed the complete-owned-band contract and has been withdrawn. These histories do not establish any specific IME as the cause.

The reviewed candidate first orders existing base overlay windows with HWND_TOP | SWP_NOOWNERZORDER, in owner-before-owned order (Freeze, DrawpadPresentation, Drawpad, four PPT siblings, Bar); it then performs exactly one original root TOPMOST/NOTOPMOST operation without NOOWNERZORDER. Root confirmed the separate post-root Bar HWND_TOP remains intentionally selected, supported by prior Bar restoration evidence. Setting still performs its own RaiseOwnedSetting on its HWND owner thread after root refresh.

The A native sequence starts from normal band and has all base members at TOPMOST=1 with correct owner order after A-root-after-order (log lines 887–895); B-final also shows a complete base tree (1014–1022). **The SettingCapture.10 window in these same snapshots remains unowned and normal (896/1023).** Therefore these A/B snapshots support the base-tree sequence but do not prove the owned-Setting case. Final actual Service attach plus the strict fallback-tree-and-owned-Setting assertion must supply that coverage. C was not selected and is not treated as product acceptance.

### Thread ownership, policy, and ordering review

- Base ordering runs in the existing overlay Execute path on the overlay owner thread. All reordered base roles are created in that group. There is no new worker, lock, explicit cross-thread wait or asynchronous SetWindowPos flag in this sequence.
- The existing guard still prevents the Setting owner thread from waiting for a root refresh. Root refresh completes before Submit(RaiseOwnedSetting); the Setting operation validates that its current owner is the actual Drawpad. Shutdown rejects that new raise command. Failure still propagates and the existing SetSettingOwnedByDrawpad rollback remains.
- Native SetWindowPos can send synchronous window messages to other queues. This is an existing boundary in root band propagation and owner attachment; source inspection alone does not prove all OS/message reentrancy harmless. Actual repeated HWND/GUI coverage is required and remains root-owned.
- keepTopmost remains overlayTopmost && !whiteboardWindowMode. The pre/post ordering operations use HWND_TOP and do not independently request TOPMOST. Only the root call changes the band, retaining owned-popup propagation. A partial pre-order failure returns false before the root call instead of claiming successful policy application.
- Enumerating the four existing PPT siblings top-to-bottom, reversing that subset, and raising them in the resulting order preserves their current relative order. The Bar remains last. The new assertion that a promoted PPT remains immediately below Bar after a later root refresh specifically checks that behavior.
- The production ordering array is fixed-size and does not allocate. The remaining new production behavior is scoped to root refresh and Setting owner preparation; no dependency, project, input decoder or ink persistence format changes are justified by this window repair.

### Test review and substantive remaining checks

- HasConsistentOwnerBand checks each base member's actual band and each represented child's Z position above its owner. It is stronger than checking only root return status. It is used for both requested bands and the fallback tree with an owned Setting.
- IsContinuousOwnerTree now permits only same-process windows whose owner ancestry reaches a base member. All base members must still occur. The new actual HWND positive/negative controls allow a legitimate auxiliary descendant but still reject an unrelated same-process competitor; this is a corrected premise, not general acceptance of arbitrary interleaving.
- Capture coverage now compares visible Setting hide with a same-condition native SW_HIDE control. Independently, repeated hide of an already hidden Setting must preserve another HWND's newly acquired capture. This retains a direct guard against unconditional ReleaseCapture rather than forcing the system to preserve capture through activation changes.
- **Requested final-test addition:** the existing whiteboard section primarily checks styles, visibility, and minimize/restore. Add explicit full-tree band/order checks after EnterWhiteboard plus RequestTopmostRefresh while the topmost policy remains true, and after LeaveWhiteboard plus refresh. A plain SetOverlayTopmost(false) test does not cover the whiteboard override of a true policy. Root accepted this gap and will coordinate the addition; this reviewer obeyed the source freeze and did not edit it.
- Remove all temporary RootOrder/SettingOrder/procedure and native A/B/C investigation code before the final build. In particular, synchronous stderr probes must not be left on a product window operation path. Keep necessary failure-only test snapshots and genuine contract assertions. Final product/test hashes must come from the cleaned source, not the probe binary.
- Repeat actual service attach and whole-band transitions; one green specialty run previously coexisted with later failures. No source-only claim of stable GUI acceptance is made here.

No additional blocking logic defect was found in the selected ordering/Setting implementation on source inspection. Release is conditional on cleanup, the missing whiteboard checks, actual owned-Setting coverage, final serial whole-suite/specialty results, and product GUI regression.

### Reviewed Window snapshot (before root's final cleanup)

- `Inkeys/Inkeys/Window/Window.cpp` SHA256 `40e8a47e6df582ad9e0ffa421b1ee99360803a875c4e89cd271c3e96bde4aa31`; BOM=False, CRLF=2582, bare-LF=0.
- `InkeysHeadlessTests/window_tests.cpp` SHA256 `1248462afe40adb2526e855cbc914867377d30fe5ad18c7a6b8dd920f664b25a`; BOM=False, CRLF=1255, bare-LF=0.

## Window candidate A rejected; bounded correction candidate C review

**This section supersedes the earlier Window candidate A assessment. Candidate A was rejected by actual repeated execution; its earlier static review is not a final pass. Candidate C is also not accepted yet.** Root's window-owner-before-band-3.json records exit 3 for A after two green rounds, SHA256 a2a282a17a16adad416d349d80dc77a2f9bc6d5e20c1d857adbbf8b783a17ffd. Do not quote the earlier no-additional-static-defect statement as runtime success.

This follow-up is read-only. Root owns the frozen code and serial builds/tests; this reviewer has not modified product source or run a program/build/desktop action.

### New evidence and selected finite mechanism

The newer native evidence in window-owner-before-band-3.stderr.log explicitly includes an owned Setting: control-setting-attached line 952 shows SettingCapture.10 owned by Drawpad. C-final line 1524 has that same Setting owned and TOPMOST=1 above the base windows. This is stronger than the earlier unowned-Setting A/B snapshot. Native C-final still places several IME/TSF descendants below their owners (1534 onwards), so it is not a proof that every auxiliary relationship is normalized; final base and product acceptance remain separate.

The selected production sequence now snapshots the existing PPT sibling order, performs the original root band operation, and reads the actual root style. Only an actual mismatch triggers one root-only operation with SWP_NOOWNERZORDER, followed by a checked style readback and one original-flags propagation operation. It then performs one owner-before-owned HWND_TOP | SWP_NOOWNERZORDER pass over the existing base members, Bar last. Setting still raises in its own owner thread. There is no normal reset, timer, retry loop, arbitrary sleep, or non-root TOPMOST call.

### Error/state/exit review

- Initial root call failure returns false. Conditional correction is guarded by actual style mismatch, not only the API success return. Both a failed correction and a successful call whose style still mismatches return false with a nonzero synthesized error as necessary.
- Propagation failure returns false; each base ordering failure reports the offending HWND and stops. Final root validity/style is read back. A partial native transaction can have changed some HWNDs before failure; the code does not promise an atomic OS rollback, and callers retain the existing false result/Setting owner rollback path.
- keepTopmost is the existing policy snapshot including the whiteboard override. Conditional correction uses the same desired band. HWND_TOP does not itself request a TOPMOST band for any child. PPT sibling ordering is captured before native changes and replayed in reverse visitation order to retain their previous relative order.
- All base changes remain in overlay Execute. Setting's preparation still refuses an owner-thread wait on overlay, performs root refresh before its own Raise command, and rejects Raise during shutdown. No additional explicit mutex, future wait, worker lifetime or HWND ownership model was introduced by this candidate. Existing native synchronous-message/reentrancy boundaries still require runtime verification.
- The new whiteboard tests are now present: after EnterWhiteboard, an explicit root refresh checks the complete base tree at normal band/order; after Leave, explicit refresh checks the complete restored topmost tree. This closes the previously reported coverage gap and must remain enabled.

### Current blocking runtime result

During this read-only check, the root's completed window-finite-reconcile-1.json was available: exit 1, SHA256 5c9f8421711b375e202ab3088d5c978e166cd6c54c1a8ce97b57c88f666d4f63. stderr line 1213 fails `whiteboard root refresh preserves normal owner band and order`. after-order lines 1198–1210 show root and all represented base members at TOPMOST=0, so the remaining failure is the owner ordering portion, not observed TOPMOST leakage. Root was notified immediately.

**Candidate C cannot be marked passed or ready for final delivery while this failure remains.** Production's final success predicate currently checks root validity/style, while the test checks each represented owner-child Z relation too; the former is insufficient evidence of the latter. Diagnose the specific failed pair under the actual whiteboard HWND state and repair the minimal sequence before final cleanup/build. Do not remove or weaken the whiteboard assertion merely to obtain a green run. Temporary probes remain investigational and must still be removed before final-source acceptance.

### Candidate C reviewed snapshot (probe build source)

- `Inkeys/Inkeys/Window/Window.cpp` SHA256 `6485bcba2185c5fb81fea4b0c024c30720f7fabd7af67e2173df87b7b50eed60`.
- `InkeysHeadlessTests/window_tests.cpp` SHA256 `f0598df42b023bdb7a8a30063f3c56b39b56d4ddadfa37c722d8f3edfeeaf789`.

## F root-anchor inheritance and cleaned Window source review

**This section supersedes A/C as the current candidate assessment.** The earlier A/C red results remain valid history and explain why those implementations were rejected. F addresses the independently observed remaining band propagation gap and retains the normal/whiteboard ordering correction. This reviewer independently read all twelve root-run window-root-inheritance-{1..12}.json results: each exit 0, same SHA256 807771268f6d2599b6c7ae37e999a56e7e53e8be1157aeb94b752c90b88791d7. These runs cover the actual window contract suite with original assertions, complete fallback tree plus Setting, both whiteboard directions, and promoted PPT order. They are probe-build evidence; the cleaned source has an EnumWindows change and still needs the root's final fresh build/runtime/GUI acceptance.

No product source was modified by this reviewer. No build, test executable, HWND or desktop action was run by this reviewer. Only source/evidence reads and this report append were performed.

### F mechanism, ownership, and failure behavior

- Root remains the sole node that explicitly receives HWND_TOPMOST/NOTOPMOST. The finite original-call/actual-mismatch-root-correction/original-propagation sequence remains. It is not a timer or retry-until-success loop.
- In topmost mode, each base member is inspected before ordering. Only an observed band mismatch uses the same valid root HWND as insert-after anchor, with NOACTIVATE and NOOWNERZORDER. Both API result and actual member band are checked; failure reports the member and returns false. The subsequent existing HWND_TOP step restores the owned member above its owner. There is no independent non-root TOPMOST operation or manipulation of arbitrary IME/TSF windows.
- This closes C's specific observed hole: after a successful root correction and propagation, several represented base members could still be normal; HWND_TOP alone could not move them into the root's band. F checks those actual states instead of assuming the propagation succeeded.
- In normal/whiteboard mode, F retains the native Bar HWND_TOP operation with owner linkage rather than running the full NOOWNER ordering pass that caused C's whiteboard owner-order failure. keepTopmost still includes the whiteboard override. Both Enter and Leave whiteboard full-band/order assertions remain in the cleaned tests.
- Base operations remain on the overlay HWND owner thread. Setting Raise validates current Drawpad ownership and an actual valid root matching the current policy, conditionally inherits the same root band, checks the result, and performs TOP on the Setting owner thread. PrepareSettingZOrder still rejects a Setting-thread wait on overlay, and its full postcondition plus owner rollback remain. The command is rejected during shutdown.
- No new explicit cross-thread wait, lock acquisition cycle, background thread, accepted-work queue, HWND destruction or ink state mutation was added. Existing synchronous native-message reentrancy is still a runtime boundary; twelve bounded specialty runs do not constitute a proof for every OS/hardware failure.
- Initial, correction, propagation, inheritance, and ordering API failures return false with an error code (synthesizing ERROR_GEN_FAILURE when necessary). A partially completed native Z transaction is not falsely described as an atomic rollback. Final root validity/style is checked; actual whole-tree behavior is additionally enforced by the strict runtime tests.

### Cleanup and EnumWindows review

The cleaned Window.cpp and window_tests.cpp no longer contain INKEYS_WINDOW_ORDER_PROBE, RootOrderStages, SettingOrderFailure, RootPositionProbe, native A/B/C band/owner control machinery, or the temporary synchronous product stderr samples. The retained kSettingNativeHide control is the genuine capture semantics regression, not a discarded ordering experiment.

The new PPT order capture uses EnumWindows and two fixed arrays of four HWNDs. It snapshots only the known four role handles and their current order; no callback allocation or product callback is performed. It stops when every nonnull expected handle is found. EnumWindows returning false due to that intentional early stop is accepted only because count equals expected. If a handle is missing or enumeration fails before that point, the operation reports failure before the root band transaction. Null optional roles are excluded from expected. The reversed snapshot is then copied into the fixed owner ordering array, retaining relative PPT order and placing Bar last. This avoids adding a global GetTopWindow/GetWindow chain traversal to production. The existing narrow SettingAboveDrawpads helper was left outside this change.

The final diff remains within Window Service ordering/preparation and relevant contract tests. No project, dependency, IME configuration, input decoder or persistence format was changed by this Window repair. The small equivalent attached local followed by return is a nonblocking leftover from removing a diagnostic; root was informed it may be folded for strict minimal diff.

### Current verdict and final gate

**No blocking source defect found in F or the cleaned EnumWindows implementation. A/C's runtime failures are superseded by F's twelve green probe specialty results, rather than erased or relabeled.** The earlier C-blocked statement does not describe F. The task is not marked complete by this review: root must compile the exact cleaned source, rerun the final full/default suite and relevant specialty checks, and finish product GUI/PPT regression. Hardware/Office/WPS/physical-contact limits must remain explicit in the final delivery record.

### Cleaned source snapshot reviewed

- `Inkeys/Inkeys/Window/Window.cpp` SHA256 `fcfd97da98472758ee10b12ab1eefa53a33ae1e00931ae7f667cfca4f600043f`; BOM=False, CRLF=2588, bare-LF=0.
- `InkeysHeadlessTests/window_tests.cpp` SHA256 `062cf9326ce8171ca4353df97a9c3934a8c99aa3057fd4e7ddf72320fe527179`; BOM=False, CRLF=1091, bare-LF=0.

## Control-fence idle deadline harness timing correction

Root's final clean control-fence run exited 1 with only `stationary deadline expires without fake input, frame polling or extra wake` failing. The old combined assertion did not print its elapsed time, generation, or wait count, so the historical failed conjunct cannot be established conclusively from that output alone.

Source analysis found a real measurement mismatch: WaitForEraserIdle(hasDeadline) calls production WaitForWake exactly once, and WaitForWake computes/polls a QPC deadline. The fixture instead timed a 15 ms request with GetTickCount64 and required a 10 ms elapsed reading. Microsoft's [GetTickCount64 documentation](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-gettickcount64) states typical system timer resolution is 10–16 ms. A valid interval close to 15 ms can therefore fit within one coarse tick and read as zero. Microsoft's [steady_clock documentation](https://learn.microsoft.com/en-us/cpp/standard-library/steady-clock-struct?view=msvc-170) confirms its Windows implementation wraps QPC. This makes coarse-timer quantization a supported explanation, rather than proof of a production idle failure; the old output cannot certify the exact occurrence retrospectively.

The authorized minimal repair changes only this existing harness block in DrawingController.cpp: deadline budget 64 ms spans multiple system ticks; elapsed measurement uses steady_clock; explicit 1 ms lower-bound tolerance requires elapsed >=63 ms. The original unchanged-generation and activeWaits==1 checks remain, as do all subsequent exit/exception/disarm/terminal/stale-handle assertions. A failed result now prints actual steady elapsed, coarse tick delta, requested budget/tolerance, generation/expected generation, and actual/expected active waits. GetTickCount64 is retained only as supplementary failure diagnostic here. There is no production WaitForEraserIdle/WaitForWake, wake admission, wait count, or idle protocol change.

Targeted git diff --check passed. Byte-preserving replacement retained original BOM and CRLF; no build, executable or desktop operation was run by this reviewer. Root owns incremental solution build and repeated control-fence verification. This note does not prematurely convert the new fixture into a passed runtime result.

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` SHA256 `494948f399ba33ba53460770f820281fb08842aa9716dfb3ddfcb2b339197c2f`; BOM=True, CRLF=15135, bare-LF=0.


## Visible Setting 后周期刷新补丁的独立复核

2026-10-18 completion_review_v2 只读复核最终 Window.cpp（SHA256 3ac38cf1e39160016c8f3415b5eb850b269fe04c84388e02697e4e2acd5765e2）与 window_tests.cpp（5a0f44fae9ad36cbc2d676f5c942db5aac11b96cfc9c1e7dc96122400a7ef6b5）：未发现阻断源码问题或断言弱化。Most 最后 HWND_TOP 去掉 NOOWNER 以维护 owned Setting；继承步骤仍保留 NOOWNER，根独占显式 MOST 策略、普通/白板路径、Setting owner 线程 Raise/shutdown/rollback/根等待 guard 保留。没有新增锁、future 或同步队列等待。新增 Show/Reshow 后 RootRefresh 同时核对可见、Drawpad owner、十窗完整 band/owner、两画布上方；既有断言保留。实际 GUI 点按由根会话验收，独立源码复核不代替运行结果。
