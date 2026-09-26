# Bar / confirmation / native-session review

Scope: root-owned Bar lifecycle/dock/focus changes, Bar.A2 dispatcher, MessageBox fail-closed fallback/IsShowing, native Business/PptSession parser and ppt_session_tests.cpp. Review performed against task PRD/design/check contexts and actual production paths. No GUI started. Full solution and runtime test execution are owned by root to avoid concurrent compiles.

## Findings fixed in this review

1. `Bar.Main.cpp` used `BarPptSceneState` without including `Bar.BottomDock.h` in its own global module fragment. Main.cppm's non-exported include did not make the header declaration visible to this implementation unit. Root's second full build reported C2039/C2065. Added the direct include.
2. `PublishPptSession` exposed pending scene edge under `pptSceneMutex`, unlocked, and only then wrote `pptPresentationWindow` and `pptPresentationActive`. An already-running render callback could consume an enter while observing the previous HWND/dock condition. Publish window/active and pending edge under the same mutex; preserve content-ready A2 StateUpdate and wake outside it. The applied-scene change below additionally prevents an in-flight frame from reading an unconsumed dock condition.
3. After a secondary-monitor PPT scene, `PublishDisplaySnapshot` always selected retained `pptSceneMonitor`, including while Whiteboard (whose production `PrimaryBounds` is Primary) owned the workspace. On Whiteboard entry, clear the PPT monitor and republish the primary display; display callbacks explicitly prefer Primary while Whiteboard is active. `UpdatePptSceneMonitor` serializes monitor writes with the entry clear and does not override Whiteboard. Root explicitly approved keeping the existing folded/primary position on return rather than replaying PPT entry. Changes are in `Bar.Main.cpp` and `Bar.Initialization.cpp`.

4. Render/input handoff race: the new RenderFrame block originally checked dragActive=false/phase=Idle, consumed the scene event, and unconditionally wrote a dock tuple. Seek could CAS Idle -> Dragging between the check and write. Fixed by claiming the existing Idle -> Absorbing phase and directWindowDragMutex before consuming; a competing input owner leaves the pending scene untouched. This uses the protocol Seek already waits for.
5. Lost true-exit edge: native PptInfo can call EndSession followed by PublishSession(new,true) within one iteration. Single pending optional<bool> overwrote false with true, leaving an expanded ordinary-floating bar unchanged (enter Keep) although exit requires center. Root approved retaining a pending exit obligation until consumed. BarPptSceneState now preserves false; final CenterDock chooses the latest active PPT window, or current Bar monitor when inactive. No new public type or duplicate entry animation. Added production-state-machine regression for A active -> inactive -> B active before Take.
6. In-flight render/drag-deferred frames read a live semantic PptPresentationActive for dock inset, so native publication could change the line before render accepted the scene. Added internal applied-PPT-dock activity, updated only by render ConsumePptSceneTransition; input/render inset now reads applied state. A2 and command semantics still use the current semantic lifecycle.
7. Shared initial/whiteboard/PPT placement unconditionally set fold=false after resolving the scene. A new user collapse between scene consumption and placement could be overwritten. PPT placement no longer forces expansion; initial/whiteboard behavior remains unchanged, including simultaneous Whiteboard and PPT placement requests (Whiteboard still wins).

## Findings reported outside ownership

- `message_box_tests.cpp` modified invalidForFallback.fallback.enabled=false for the new assertion then reused it in the existing fallback/reentry assertion. Root was notified and owns the request-reuse fix in its test file.
- No unresolved parser/dispatcher/MessageBox implementation defect found in the scoped review. Production Bar lifecycle concurrency was reviewed statically; full native scene animation under actual Office/multiple monitors is still NOT VERIFIED.

## Evidence and unaffected behavior

- Folded detachment rebase correctly reads successful presented mainCenterScreenX, mainCenterScreenY + rigidTranslationDip * zoom. X already includes rigid-grip and direct translation; Y adds the stored vertical mapping once. It does not derive the anchor from shadow HWND bounds.
- CenterDock placement uses existing main-button/main-bar visible bounds and ResolveBarBottomDockCenterScreenY, not capacity bounds. Animation derives from displayCenter and the standard centered-root gate. Production integration still needs lifecycle/drag/failure/whiteboard coverage; pure action-table tests alone are insufficient.
- Main Bar `Bar.Button.cpp` is the only production RequestEndShow click source. PageControl callback goes directly to PptUiBusinessCommand::EndShow; it does not enter the main confirmation dispatcher.
- BarA2CallbackDispatcher copies callback under lock, invokes outside lock, assigns monotonically stamped request ids, rejects concurrent requests, and only completes a matching id. Exception dispatch cleanup is tested. Native business completion RAII covers cancellation, stale session, unsupported capability and ordinary completion.
- Native confirmation is on the PPT business consumer outside its queue lock; MessageBox is default-Cancel/dismiss-Cancel and fallback.enabled=false. The low-level EndPptShow wrapper has no new modal. Managed expected-session exit is revalidated by execution owner; native mode change happens only after reported successful exit.
- Focus callback excludes color picker and eraser editor; native execution also rejects whiteboard, Settings foreground, any active/queued MessageBox, unrelated foreground process, stale session and invalid HWND/PID. These are static call-path findings, not a claim of real Office keyboard validation.
- PptSession parser bounds the envelope, rejects duplicate keys/extra data/bad schemas/statuses, validates signed-range revisions and nested binding agreement, and reuses the production Draw3 descriptor parser. Unknown remains distinct from Inactive. Tests use the real parser and dispatcher, not cloned formulas.

## Verification

- Scoped `git diff --check -- Inkeys/Inkeys/UI/Bar Inkeys/Inkeys/UI/MessageBox`: exit 0 after fixes.
- Changed existing Bar.Main.cpp and Bar.Initialization.cpp retained UTF-8/CRLF; byte-preserving edits did not rewrite unrelated lines.
- Full solution compile: root's third attempt PASSED (0 errors, 12 unrelated hashlib++ warnings), confirming the include/publication/whiteboard fixes. Later scene handoff/reentry/applied-inset edits require root's next incremental full-solution pass before final TypeCheck status.
- Runtime/headless tests: root owns execution; this reviewer did not start a duplicate build/test process. New rapid-exit/reentry state-handoff regression is queued for the next headless pass.
- Real multi-monitor PPT/Office/WPS, focus/keyboard, modal lifecycle and visible animation are NOT VERIFIED.

## Documentation synchronization

Root confirmed central touched-spec updates are being made, including `.trellis/spec/ppt-interop/native-session-ui3.md`. Record whiteboard Primary priority and folded return semantics there. Older rendering-and-ui / com-contract sections still state PageControl uses Bar RequestEndShow plus confirmation; latest approved main-only contract must be linked as the authoritative replacement or the stale lines corrected. Final verification should not claim all scene/stale-frame cases solely from the helper state table.

## Final integration record

Root confirmed: full Debug|ARM64 solution, headless --no-window, managed tests, real Host hidden (including persisted cold recovery/reorder), final Bar/PageControl offscreen and diff check all exit0. Exact evidence and limitations are in verification.md. Generated .log files were preserved under D:/Project/Inkeys/Repo/Inkeys-draw/Build/Validation/ppt-ui3-scene-and-page-sync-20260925 (outside tracked task artifacts); use the same log basenames recorded above. No Office GUI/device result is implied.
