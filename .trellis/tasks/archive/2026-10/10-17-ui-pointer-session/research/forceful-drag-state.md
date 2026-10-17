# Research: forceful drag screen-edge and grab anchor

- Query: 强拖到屏幕约束边界后，指针回到可用区域仍以偏移锚点继续拖动的可能原因。
- Scope: internal, read-only Bar Seek/direct-window/presentation and existing pure tests. Parent separately owns Raw/UIPI stream research.
- Date: 2026-10-17

## Findings

### Confirmed code shape: ordinary clamp does not mutate the drag anchor

- `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp:6993` captures startPointer; :6999-7002 derives grabOffsetScreenX/Y from the successfully presented grip.
- `:7132-7137` updates previousPointer exclusively for path length and click suppression. It does not accumulate HWND deltas or rewrite the anchor.
- `:7245-7252` computes horizontal desired center from absolute pointer minus grabOffset and applies `ClampBarBottomDockMainCenterScreenX`; `:7259-7268` computes the analogous floating vertical value and clamps it.
- `Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h:389-400` clamp is a stateless std::clamp. Consequently a fixed environment plus correct absolute Sample.screen must recover the original offset when the pointer returns into range. A claim that incremental lastPointer loses excess displacement at the edge is contradicted by current Seek code.
- `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp:13533-13585` can absorb directWindowDragTranslation only after phase CAS Idle -> Absorbing; Seek owns Dragging from :6872 until :7454, so absorption cannot explain a persistent in-gesture shift under the protocol.

### Concrete conditional anchor-rebase risk

- The only later assignment to grabOffsetScreenX/Y is `Bar.Interaction.cpp:7148-7151`, inside RebasePointerForPresentedEnvironment.
- That function is called at :7193 whenever `presented.displaySerial != observedDisplaySerial || presented.zoom != interactionZoom` (:7185-7194), with no semantic comparison of monitor/work area/DPI fields.
- It recomputes the anchor as current event pointer minus the last presented grip. If the presented grip is edge-constrained, is behind current input, or includes transient rigid translation, this absorbs that discrepancy into a permanent grab offset for the remaining gesture. At zoom 1, original offset 0, edge-clamped center 40 and current pointer 0 produce offset -40; subsequent pointer 200 requests center 240. This is a deterministic code-level possibility, not measured proof of the user's incident.
- `Inkeys/Inkeys/UI/Bar/Bar.Initialization.cpp:100-107` PublishWindowDpi increments pendingDisplaySerial even if dpi is unchanged; :74-95 PublishDisplaySnapshot similarly unconditionally increments when called. Display service itself suppresses semantically identical global snapshots (`Inkeys/Inkeys/Display/Display.cpp:793`), so normal cursor motion alone is not confirmed to produce such a publication.
- `Bar.Interaction.cpp:885-892` processes WM_DPICHANGED and WM_DISPLAYCHANGE/WM_SETTINGCHANGE; there is no direct edge-clamp -> PublishWindowDpi call found. A real DPI/monitor event or redundant DPI publication would be necessary to attribute this risk to the observed case.
- Shape-only barriers intentionally avoid anchor rebasing at :7196-7207. This preserved distinction is correct; do not broaden anchor rebasing to every presented serial/shape change.

### Other failures do not match a continuing shifted drag

- Missing Bar metadata at :484-492 yields Cancel; queue/Raw failures are adjudicated at :676-690; they end or disable the optional source rather than rewriting a grab offset.
- Seek Cancel at :7376-7379 proceeds through normal release/elastic recovery, clears bottomDockDragActive/indicator at :7450-7451, finishes direct phase at :7454, and disallows click using gestureCancelled at :7457-7460. This could terminate tracking, but does not itself support a continuing drag at a new anchor.
- SetWindowPos failure rollback uses last successful presented tuple (:7388-7411), then ends the gesture. No continuing shifted state is established there.

### Existing tests and suggested smallest evidence

- `InkeysHeadlessTests/bar_bottom_dock_tests.cpp:608-617` tests standalone clamp and a few reversals; :831-841 tests only DockTracker RebaseDockGrip detach threshold. Neither runs Seek's persistent anchor-rebase path.
- `InkeysHeadlessTests/window_geometry_tests.cpp:21-100` tests direct-window presentation mutex/resize transactions; :225-244 tests translation and absorption math. Neither combines edge clamp + changing environment snapshot + original grab invariance.
- `Inkeys/Inkeys/UI/Bar/Bar.EraserAttribute.Test.cpp:169-204` tests cursor lighting rebase across layout origins, not Seek anchor.
- To decide the actual cause without guessing, capture accepted source/kind/full screen point alongside startPointer/grabOffset, desired center/clamped center, presented center, pending/presented displaySerial and zoom. If offsets never change while pointer and grip diverge, inspect upstream Sample.screen/Raw trajectory. If offsets change exactly at serial/zoom adoption, the concrete rebase path above explains it.
- A scoped regression should use a production pure anchor/clamp policy, cover all edges and large excursions followed by return, preserve original grab offset in a stable environment, and cover redundant serial versus real geometry/DPI changes. Pure helper tests alone cannot establish HWND events or actual UIPI delivery.

## Files found

- `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp`: Seek and client input normalization.
- `Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h`: clamp, tracker, screen-grip mapping pure policies.
- `Inkeys/Inkeys/UI/Bar/Bar.Main.cppm`: presented tuple and direct-window phase/translation state.
- `Inkeys/Inkeys/UI/Bar/Bar.Initialization.cpp`: display/DPI publication.
- `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp`: successful presented centers, direct translation absorption and dock spring mapping.
- `InkeysHeadlessTests/bar_bottom_dock_tests.cpp`, `window_geometry_tests.cpp`: existing non-GUI geometry coverage.

## Related specs

- `.trellis/spec/native-desktop/rendering-and-ui.md`: target/current versus presented values and HWND/presentation ownership.
- `.trellis/spec/native-desktop/ui-pointer-session.md`: native entry, optional Raw, source generations, timeout only disabling pending supplement.
- Task correction.md and timeout-correction.md remain authoritative. Existing hooks remain outside scope.

## External references

None required for these deterministic internal code paths; no OS behavior claimed.

## Caveats / Not Found

- User's clarified bug is pointer-to-grip offset after edge clamp; screenshot alone provides no exception, detached-render surface or crash evidence.
- No GUI, runtime input replay, binary build, hook change, or production code write was performed by this researcher.
- No proven production trigger for a DPI/environment publication during the user's strong drag was found. Parent/Raw researcher should correlate actual sampled screen positions before naming the rebase path as the runtime root cause.
