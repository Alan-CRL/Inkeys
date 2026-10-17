# Edge-anchor / silent-stream fresh check

- Role: fresh `trellis-check`; no child agents, GUI, hidden HWND, hooks, activation/capture/privilege changes, git commit or push.
- Scope: latest six-file follow-up in UiPointerInput.h/.cpp, Bar.Interaction.cpp, Bar.BottomDock.h, ui_pointer_tests.cpp and bar_bottom_dock_tests.cpp. Earlier uncommitted work was preserved.
- Authority: edge-and-stream-correction.md first, then correction.md / timeout-correction.md, registered check context and current PRD/design/implement. Newest contract overrides historical Raw-exclusive wording.

## Findings (fixed)

- File: `InkeysHeadlessTests/bar_bottom_dock_tests.cpp:620`.
- Issue: the newly added inner-loop `monitor` hides the test function's existing local (`C4456` in the parent full build).
- Fix: renamed only that declaration and its two references to `edgeMonitor`. No production behavior changed; original UTF-8/no-BOM and CRLF retained.

## Findings (not fixed)

No further confirmed implementation defect found in the reviewed follow-up. The following are explicit evidence limits, not silently fixed design gaps:

1. If neither native Move nor Raw Move reaches the consumer outside the toolbar, the 50ms arbitration has no position to apply. The change cannot guarantee global cross-integrity dragging. Activation, capture strategy and UIAccess/deployment remain outside this change.
2. The old environment regrab can deterministically latch a clamped/lagging presented location; removing it preserves the logical grab point. No trace yet proves that a display serial/zoom adoption occurred during the user's same-monitor ordinary-foreground incident. Helper tests and compilation must not be described as proof that the whole observed geometry incident is fixed.
3. UiPointerDrag arrival counters are consumer-dequeue counts, distinct from accepted business movement and from global WM_INPUT provider reception. They diagnose a stalled active consumer together with cursor/presentation snapshots; they do not alone identify which OS filter or registration/matching stage lost data.
4. The active trace is bounded to begin/end and one tracking summary per 500ms. It uses existing logger facilities and is intentionally present for the unresolved hardware reproduction. Real Win7 touch, administrator foreground and cursor crossing were not GUI-tested.

## Verified code paths

- `MouseGesture::AcceptNative` (`UiPointerInput.cpp:150`): only a valid current-generation Move at least 50ms after the last valid Raw activity can take over. Timing uses signed DWORD event-time differences, including wrap; no wall-clock held deadline or inferred Up/Cancel. The native floor checks prevent an older stationary native sample from lowering a previously advanced floor.
- `AcceptRaw` (`:185`): valid repeated-coordinate Raw packets update freshness, while stale/old-floor packets do not. Normal Raw same-ms different-point excursions are retained. A native takeover advances a strict Raw replay floor even when no business movement is emitted; only newer Raw can resume, preserving one movement authority.
- Moving authority does not disable Raw Up. Current Raw identity and actual-drag admission still apply, older terminal positions cannot rewind Latest, and duplicate native/Raw terminal events finish once. Existing source takeover/generation checks and generation=0 pending-expiry behavior are unchanged.
- `ScaleBarDragGrabOffsetForZoom` (`Bar.BottomDock.h:258`) is called by Seek for both axes only on actual presented zoom change (`Bar.Interaction.cpp:7263`). Serial/monitor adoption still rebases layout/tracker state, without deriving a new grab offset from the clamped presentation.
- Seek remains absolute pointer-minus-offset plus clamp. It continues to use the same TryReadBarPointerMessage path as the old wait wrapper, so requested/source/desktop/mode/fold cancellation and orderly cleanup remain. Its outer loop can now produce a rate-limited summary even with no input.
- Diagnostics read only interaction-thread state and existing snapshots; GetCursorPos is comparison-only, not an input producer. Capture is queried via GetGUIThreadInfo for the HWND owner thread with success recorded. No button polling, synthetic input or new producer race was introduced.
- New tests directly exercise production MouseGesture and production geometry helpers, including 49/50ms, multi-second native continuation, stationary floors/freshness, delayed and resumed Raw, DWORD wrap, Raw/native terminal ordering and logical grab coordinates across zoom.

## Verification

- Static/lint: PASS. `git diff --check --ignore-submodules=all` exit 0. All six files decode as UTF-8/no-BOM, contain only CRLF, and retain tracked-file BOM identity. No new GetAsyncKeyState/IsKeyBoardDown usage, activation or capture policy was added.
- Compile/type check: parent full `InkeysRepo.sln Debug|ARM64` build PASS, actual exit 0, 0 errors (`Build/ARM64/Debug/ui-pointer-session-edge-build.log`, 64.32s). After the C4456 rename, parent final solution build also PASS, actual exit 0, 0 warnings/errors (`Build/ARM64/Debug/ui-pointer-session-edge-final-build.log`, 6.74s). Reviewer did not start a competing MSBuild.
- Tests: parent normal-host `InkeysHeadlessTests.exe --no-window`, `Inkeys.exe --state-mode-production-test` and `Inkeys.exe --pen-width-production-test` all PASS, actual exit 0. Logs: `Build/ARM64/Debug/ui-pointer-session-edge-tests.log`, `ui-pointer-session-edge-state-probe.log`, `ui-pointer-session-edge-width-probe.log`. These ran against the main implementation build before the sole local test-identifier rename; final rebuild includes that behavior-neutral rename, with no product changes after tests. No repeated test run was necessary for an identifier rename.
- Source handoff stable: only the C4456 local test rename was changed by this reviewer. Parent notified before final rebuild. No further source edits will follow this report.
