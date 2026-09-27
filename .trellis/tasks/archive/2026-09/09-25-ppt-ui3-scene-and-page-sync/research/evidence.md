# Baseline evidence
2026-09-25 initial HEAD/remote dev94e07b2599adab9de4286aa5fb7526e2c1c681f6, branch bugfix/pptui, clean, no current task. Ten other tasks untouched.

## Source facts
- PageControl.cpp release invokes PersistDragPosition only when tracker.persist (rememberPosition). FinishDragPersistence follows dispatch, not disk success. Facade therefore remains old when remember off.
- Ppt.cpp PublishSnapshot unlocks before PublishPptState. NotifyConfigurationChanged reloads entire legacy config. PageControl only preserves positions while ownsLayout. remember-on late snapshot race requires deterministic reproduction (not claimed Office-reproduced).
- Plugin persistence writes both groups and ignores write result. Setting.cpp:389 captures full JSON before independent queue worker. Writer truncates destination before writing.
- PageControl layout multiplies DPI and user scale; ApplySceneBounds reclamps to4, Bar.Scene.cpp:97/1780 reclamps [.5,4]. FrameZoom itself accepts positive scale.
- PptInfo's wait rereads runtime revision after decision; native visibility500ms is separate from50ms page sampling.
- PptCOM SlideShowChange writes page quickly, requests descriptor refresh. Owner refresh at1482 then Sleep500 at1491: source of possible descriptor lag, not a measured fixed latency. Binding revision increments on FullCleanup, not each show.
- DrawingController.cpp:5310/6497 processes commands only with active.empty; held contact delays pages. stageWorkspaceReady already runs after successful Present (7564).
- Controller:5634-5652 parked/warm old target restored before comparing topology and assigning new target: already correct, preserve + test.
- Ppt facade passes presentationVisible to Bar; PptInfo sets false under whiteboard, so not lifecycle.
- Bar SetPptPresentationActive only toggles bool. Dock helper already supports5DIP screen inset; do not reuse whiteboard lock. Bar PPT references found only cursor-light region registration, not obstacles.
- Plugin EndShow currently no modal and changes selection first. A2 dispatcher has bool outstanding, no session/id. MessageBox always system-fallback on failure; new confirmation needs opt-out.
- vcxproj891 IdtDrawpad.cpp=None;901 IdtDrawpadFacade=compiled, hook install empty. WindowControl WM_MOUSEACTIVATE respects activationAllowed.

## Documentation drift
ppt-interop/index and portions of com-contract describe active Draw2/PptImg flow/page-only readiness/confirmation absent in current code. Update touched contracts and position triggers; don't revive Draw2.

## Baseline verification
git status/branch/rev-parse/task current/list, gh api repos/Alan-CRL/Inkeys/commits/dev --jq .sha succeeded read-only, git diff --check exit0. vswhere located ARM64 native MSBuild. No builds/Office timings yet; no fabricated figures.
