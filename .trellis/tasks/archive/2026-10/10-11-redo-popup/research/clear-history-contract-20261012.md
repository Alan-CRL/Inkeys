# Research: Clear recovery, persistence and session forward history

- Query: Preserve one previous-Clear Undo interval and redo across that Clear to latest without restoring abandoned older intervals.
- Scope: internal
- Date: 2026-10-12

## Findings

### Persistence currently discards the forward path deliberately

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:11376` restores a visible canvas snapshot, sets `intervalOrdinal=0`, consumes `previousClearUndoAvailable`, and sets `clearRedoAvailable`. The PPT PreviousInterval completion repeats this at lines 11524-11559.
- Active completion immediately marks mutation and calls `capturePresentationAutoSave` (11577-11578); parked completion advances mutation and calls submitPresentationSlot (11598 onward). This makes the restored page the durable new root; keeping that operation is compatible with preventing older Undo restoration.
- `Draw3.PresentationAutoSave.cpp:1091` merges each requested canvas through `MergeDraw3UInkCanvasTail`. `inkStrokeModelerTest/draw3/uink_draw3_export.cpp:258` initializes operations empty, retains only the canonical prefix up to requested interval, and appends requested visible strokes. With ordinal zero, it stops at the first canonical stroke, so all former Clear boundaries and later intervals disappear from this page's saved log. Merely preserving fileGuid or old ordinal cannot recover the latest session redo path reliably.
- Latest native spec `runtime-and-rendering.md:1016` and native-desktop `draw3-integration.md:192` explicitly require latest Clear restore as floor zero, consumable once. Both currently state cross-Clear Redo out of scope and must be updated for the new request; older task PRD is not authoritative about this contract.

### Reusable runtime copy versus snapshot APIs

- `Draw3.InkDocument.cppm:103-169`: InkCanvas/InkPage/InkCanvasCollection contain pure value vectors and GUIDs; no deleted copy constructor or resource pointer. `InkPage` value copy preserves all stored strokes, including hidden redo strokes; `ReplacePage` at line 156 validates matching GUID and is the existing atomic page replacement boundary.
- `Draw3.InkHistory.cppm:179-213`: CanvasRuntimeHistory has value containers including items_, compositionTree_, lastVisibleIndex_, redoItems_. Default value copy preserves visible/hidden state, redo candidate order and generations. Public LastRedoItem/RedoLastUndone/RedoDepth and DiscardRedoBranch remain suitable for stepwise local redo.
- `DrawingController.cpp:403`: CanvasPageRuntimeState also contains raster state plus beforeStates/afterStates. Copying page and runtime together preserves consistency; taking only InkPage or only UInk strokes cannot reconstruct existing candidate tokens/indexes safely.
- Existing UInk `Draw3UInkCanvasSnapshot` (`uink_draw3_export.cppm:57`) contains strokes, ordered Stroke/Clear operations and ordinal, but no runtime hidden flags/redo stack. Production `BuildPresentationSaveRequest` (controller 1140; visibility filter 1199-1201) deliberately exports only visible items. `materializeCanvasPage` (10790) appends every snapshot stroke as visible and creates fresh state tokens. Neither is a drop-in forward runtime copy API.
- Minimal proposal: retain the current page/runtime value before successful previous-interval replacement, then restore it with the same GPU reset/replay transaction when stepping forward past Clear. The retained runtime must preserve its original hidden redo candidates. Snapshot only after materialization succeeds, immediately before ReplacePage; failures must leave current page and existing forward state unchanged. New stroke or explicit branch-changing Clear discards this session forward value. Keep the consumed previousClearUndoAvailable constraint independent of this redo value, so forward replay does not reactivate older Undo regions.

### Completion, scene switching and lifetime hooks

- Desktop completion (controller 11411-11447): matches Desktop recovery fileGuid before installing Loaded single canvas; failed load only clears loadPending, retaining recovery for retry. Disk-save success releases canvas fallback. Therefore forward memory is a separate session value and must not be released by ordinary durability acknowledgment.
- PPT `RoutePresentationCompletion` (642-670) checks nonzero slot generation, key, binding mode, and CanReusePresentationDocumentSlot, routing active or parked lane only. `PresentationLoadedForLane` (692) additionally validates storage track, file GUID, snapshot identity and workspace type. PreviousInterval installation (11526+) also checks target page GUID, pending flag and requested ordinal+1. Preserve all these guards; don't create/consume forward state for stale/wrong-file/failed completion.
- On valid failed PreviousInterval, existing active/parked branches clear page intervalLoadPending, allowing retry. A forward value captured for the eventual successful replacement can be installed only on success; no need to clear independent prior valid forward merely because unrelated failure arrived.
- `DrawingDocumentSlot` (429) owns per-page runtimes. Normal page switch and slot park/swap naturally retain added per-page session state. Do not introduce global forward that aliases another page/document.
- `canEvictPresentationSlot` (10526) currently permits clean durable parked slots to disappear. A memory-only forward branch requires an extra no-forward guard, else switching away and back will silently lose redo after save. Alternatively persistence must encode forward, a substantially broader change not required here.
- Topology rebind (10865-10956) rebuilds from visible-only save snapshot and only transfers selected runtime flags (10934-10941). Stable SlideID reorder therefore cannot automatically preserve hidden runtime redo/forward. Match existing page GUID and move forward state; if preserving local hidden redo as part of this change, full existing page/runtime transfer is more accurate than fresh materialization plus flags. Removed/retained slide behavior is a separate caveat: retainedSlides currently holds only visible snapshot.
- `releaseBoundaryFallback` (11470+) is tied to durable Clear ordinal. Do not use it to release forward state; fallback and forward have distinct lifetimes.

### Existing tests to reuse and extend

- `inkStrokeModelerTestTests/presentation_autosave_tests.cpp:3200`, TestClearIntervalsPersistAndLoad: saves A/Clear/B/Clear/C; Current projects C, PreviousInterval ordinal1 B, ordinal0 A. This verifies persistence projection but does not exercise controller forward redo.
- Same file 3143, TestWorkerExceptionBecomesCompletion: injected worker failures return structured Current and PreviousInterval IoError including page/ordinal/generation identity.
- Same file 2727, TestRestartDropsStaleCompletions: service restart completion lifecycle.
- `DrawingController.cpp:4187-4212`: production routing probe rejects mismatched file GUID/generation and routes late PreviousInterval to parked old lane instead of active Stable lane.
- `DrawingController.cpp:6606`, RunParkedPresentationRetainedSaveTest; 6734 RunPresentationLoadedRetainedInstallTest; 6857 RunPendingPresentationTopologyLoadTest cover save/install/topology isolation. Extend the applicable controller probe with hidden current-tail redo copied through Clear recovery, successful/failed/stale load behavior, page/slot return, no eviction while forward retained, and consumed Undo permission after forward replay.
- `inkStrokeModelerTestTests/uink_tests.cpp:3555-3567` covers MergeDraw3UInkCanvasTail multiple Clear intervals and projection. No UInk schema change is needed to retain runtime-only session forward.

## Caveats / Not Found

- No existing serialized runtime-hidden/redo-candidate snapshot API was found. UInk export intentionally captures current projection. Runtime value copy is the narrow reuse path.
- This research did not change production/spec files or run build, GUI, tests or git commands. Implementer is modifying the controller concurrently; line numbers refer to the inspected version and may shift.
- Latest runtime/native-desktop spec cross-Clear Redo restriction is superseded by this user's explicit request; preserve latest-Clear Undo floor/one-interval restriction.
- Broader retained-slide local history survival is not guaranteed by current topology pipeline; ordinary same-topology page and slot switching should be retained by per-page ownership.
- External references: none required; all findings use repository source contracts.
