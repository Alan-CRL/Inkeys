# Screenshot revision independent check

Reviewed current diff against the latest screenshot PRD override and native UI/window contracts. No GUI started and no build run in this agent; the main session owns ARM64 solution and offscreen/headless verification.

## Fixed findings
- Window.cpp: WM_SIZE/SIZE_MINIMIZED previously left Freeze iconic when PPT policy rejected minimization. Restore the already-minimized anchor immediately; SIZE_RESTORED observes the false group state and does not recurse into ShowWindow.
- Window.cpp: closing from the retained main bar after minimizing previously cleared the group flag while preserving the iconic anchor. Successful mode exit now normalizes Freeze without activation and keeps it hidden; it does not restore the saved group visibility. Added the close-after-minimize assertion to the existing HWND suite (compile only under the no-GUI validation constraint).
- UI implementer addressed review feedback: hidden theme starts collapsed horizontally; panel closes with a retained reveal phase; panel hit remains active during visible fade; theme panel uses shared surface tokens; custom previews no longer paint the button twice.

## Review evidence
- ResolveBarA2Projection feeds the actual whiteboard button size selection and now emits twoTwo before entry.
- StandardTwoTwo consumes primaryText; paging and More labels now populate that channel. Actual Scene offscreen tests count label pixels rather than testing only strings.
- Main center direction delegates to screen-space expansion policy; no pagination rect is supplied as an obstacle.
- Window minimization excludes Bar, explicitly re-shows the owned popup, blocks stale Show and Drawpad visibility requests, notifies pagination and Bar. PageControl consumes the shared minimized flag; main inset becomes zero while the workspace baseline remains intact.
- Minimize policy is checked at the window owner as well as menu click. Window title comes through generated i18n and Freeze receives executable icons via WM_SETICON.
- Preview selectors use 70 DIP square geometry with shared press visual and central check instead of selected outer frame.

## Pending / limits
- Main session must record actual build and test outcomes; git diff --check passed at review time.
- Fixed spring close overshoot: clamp animated w/h to zero before the shared frame uses them, skip empty custom rectangles, and render actual close/reverse frames with EndDraw assertions in the offscreen suite. These latest files require the main session incremental rebuild.
- The existing window_tests suite uses a local Service while DefaultWindowProc resolves the process GetService. Therefore its API-level assertions do not validate the process WM_SIZE veto path. Real taskbar/Win+D/restore and no-flash close behavior remain GUI acceptance items, not claimed by --no-window.
- Real rapid reversal smoothness, cross-monitor/DPI and owned-popup taskbar behavior remain manual acceptance. Offscreen pixel/hit checks can verify geometry, not compositor timing or actual Shell behavior.