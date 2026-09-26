# Native PPT / Draw3 cross-layer review

Reviewer: trellis-check / page_transaction_check, 2026-09-25. Baseline: bugfix/pptui, 94e07b2599adab9de4286aa5fb7526e2c1c681f6. Other agents' changes preserved. Active task remains in_progress. Read task manifests/PRD/design/implementation/evidence and relevant native desktop, Draw3 and COM contracts. No GUI/Office interaction, commit or push performed.

## Findings fixed

### Confirmed window loss did not end an unknown native session

- In `IdtPlug-in.cpp::PptInfo`, `EndSession` previously ran only on a reliable managed `Inactive` or a valid replacement descriptor. Office/WPS closing its last presentation, terminating or crashing before native consumed the End snapshot could let `GetPptState` reset the service. Subsequent cached state is Unknown, leaving `currentPptSession.active` and the PPT workspace indefinitely active.
- With root approval, added a narrow real-window witness: the captured current HWND no longer exists, or a successful live owner-PID query differs from the captured PID. Mere hidden/foreground changes, query failures and COM Unknown do not imply an end.
- The confirmed-window-end branch can publish Desktop without falsely changing the external lifecycle to Inactive. A valid replacement descriptor may still begin its new session in that same iteration. Live HWND owner must agree with a nonzero descriptor PID; a reliably ended extended show-session snapshot cannot reopen itself from late cached data.
- Follow-up review closed the parallel admission path: rejecting the live window or ended cached session also clears trusted-page acceptance, not only session creation. Before bridge handoff the observer rechecks service generation and cached target session/binding ownership; business requests and UI acknowledgments likewise reject stale service generations. A confirmed lost-window witness remains valid after a service reset.
- Guarded exit's subsequent selection-mode change now respects `WhiteboardTransactionActive`, so a confirmation accepted after a whiteboard transition does not change the whiteboard's mode.
- Windows/Office/WPS end/reopen behavior: NOT VERIFIED on real Office. Static ownership/call-chain verification only for this native witness.

### Hidden-window owner assertions were stale

- `Draw3.HiddenWindowTest.cpp` asserted `Drawpad -> Freeze` and `Presentation -> Freeze` before/after Host stop. Actual unchanged `Window.cpp` creates `Drawpad -> Presentation -> Freeze -> MagnifierHost` (creation at the DrawpadPresentation/Drawpad owner clauses), matching current Draw3 integration spec.
- Updated both assertions to the actual transitive owner chain without changing product window ownership, visibility or teardown.
- First integrated hidden run had exactly 12 FAIL lines: these two stale assertions in six modes. The newly added held-contact, UI-ack, warm-slot and SlideID assertions did not fail in that run. Overall first run still FAILED (exit 1); it is not treated as a pass.

### Real persistence coverage added to controller harness

- Existing `RunMode` fixtures leave `HostStartOptions.autoSaveRoot` empty. Their `save_submit/load_submit result=failed` diagnostics come from intentionally closed persistence workers and prove CPU-slot restoration only.
- Added a separate `CheckPresentationPersistence` invoked after the two modes that exercise commands. Each run uses a unique directory beside the Build executable: `Draw3HiddenPptPersistence/<PID>-<QPC>`. Artifacts remain for inspection; no recursive cleanup or external directories are involved.
- Fixtures use production `ResolvePresentationTarget` to derive document keys and source identities, not hand-written key bytes.
- Real controller accepts a still-down stroke on A/SlideID 601, seals/saves it while switching to 602, quarantines late Move/Up, parks at B, then stops and drains the actual persistence worker.
- Checks durable `presentation/index.json` and complete readable UInk files. Restarts a fresh Host/controller on the same root; cold-loads reordered IDs `[603,601,602]`, requires content only at 601, and rejects an old Host UI acknowledgment.
- This is same-process Host restart coverage, not a claim of cross-process or real Office recovery.

## Cross-layer proof checked

- Producer Down locks its admission revision. Rev changes reject queued Downs; rejected or synthetically ended physical contacts keep their own route until Up/Cancelled. Generation checks protect route reuse; terminal-first and quarantine-first races are covered in real producer/consumer headless tests.
- Controller seals already accepted last input through the existing completeModelUp -> Stored Stroke -> history/L2 path. It does not change documents while `active` still owns uncommitted ended strokes. Laser/transient state, gesture inertia and reconnect candidates are cleared at the boundary.
- Only adjacent unconsumed absolute targets coalesce in WindowControl. Scene-stamped Clear/Undo/Redo and save boundaries remain explicit queue entries; Host restores the latest scene after processing stamped commands.
- Ready is published after successful canvas Present. Host's gate compares complete target identity, checks suspension and UI-ready identity, and retains target sequence numbers across Host reset.
- Native captures the runtime revision before the readiness decision, matches session/binding/target/SlideID/page and obtains the same target identity for the UI acknowledgment.
- Unknown and EndScreen suspend an accepted presentation target. Busy/read failures do not directly request Desktop; whiteboard visibility remains separate from the native show session.
- Managed new getter returns serialized cached values; Office access remains in the owner. Old COM interface order/GUID and existing descriptor shape stay unchanged. Guarded exit captures the old View, validates session/window again after acquiring it, and cannot intentionally retarget a new session.
- Reviewed layout agent's actual-visible/in-flight PageControl acknowledgment update: frame Begin rechecks publication under the short snapshot lock; successful Window commit records actual shown/hidden state; stale frames cannot acknowledge a newer target; callbacks run after render/presentation locks are released. Failed commits retain the closed gate until retry or actual hiding.

## Verification

- `git diff --check -- Inkeys/IdtPlug-in.cpp Inkeys/Inkeys/Drawing/Draw3`: PASS before local fixes.
- Scoped `git diff --check` after native/harness fixes: PASS.
- File rewrites retain UTF-8 without BOM and CRLF; no product formatter used.
- Root coordinated all shared builds/runs and confirmed tool exit codes: final full `InkeysRepo.sln Debug|ARM64` build PASS (exit 0; `build-final.log`), `InkeysHeadlessTests.exe --no-window` PASS (exit 0; `headless-final.log`), managed descriptor/session/owner tests PASS (exit 0; `managed-integrated.log`), and `Inkeys.exe --draw3-hidden-test` PASS (exit 0; `draw3-hidden-final.*.log`). Final native build has only the three existing `hashlib++` C4267 conversion warnings, zero errors.
- Both dedicated real persistence scenarios passed: `Draw3HiddenPptPersistence/41032-1688130172949` and `Draw3HiddenPptPersistence/41032-1688211451669` under `Build/ARM64/Debug`. Logs confirm held-contact save/drain/cold reload/SlideID reorder and overall hidden integration PASS.
- Final Bar offscreen run remains root-owned; an earlier offscreen run passed exit 0. This scoped reviewer does not claim the final offscreen outcome before root collects its exit code.
- Real Office/WPS, keyboard, pen hardware, native destruction/reopen UI behavior and desktop-visible rendering remain NOT VERIFIED.

## Spec synchronization recommendations

Update the affected native/interop contracts to distinguish true show lifecycle, Unknown and EndScreen; the captured-window loss end witness; document-ready / successful Present / visible PageControl commit / input-open phases; physical contact quarantine; and revision values captured before a wait decision. Preserve the documented transitive Window Service owner chain.
## Final integration record

Root confirmed: full Debug|ARM64 solution, headless --no-window, managed tests, real Host hidden (including persisted cold recovery/reorder), final Bar/PageControl offscreen and diff check all exit0. Exact evidence and limitations are in verification.md. Generated .log files were preserved under D:/Project/Inkeys/Repo/Inkeys-draw/Build/Validation/ppt-ui3-scene-and-page-sync-20260925 (outside tracked task artifacts); use the same log basenames recorded above. No Office GUI/device result is implied.
