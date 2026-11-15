## Facts

References below use `B = Inkeys/Inkeys/UI/Bar/`, `D = Inkeys/Inkeys/Drawing/Draw3/`, and `O = Inkeys/Inkeys/UI/SelectionOverlay/`.

**A concrete input-coverage defect exists: construction rejects body hits logically, but still makes the entire construction bounding box receive native input.** This is the strongest explanation for the reported central-screen failures. (`B/Bar.ShapeEditor.cpp:360–374,864–868`)

1. **Canvas → construction → vertex append.** Canvas touch uses RealTimeStylus bound to its HWND; `StylusDown` decodes the contact and calls `ContactInputCoordinator::PublishDown`. The drawing consumer checks admission/loading/selection, then calls `initializeStroke` → `beginShapeContact`. (`D/Draw3.RealtimeStylus.cpp:2796,1234–1351`; `D/Draw3.DrawingController.cpp:18240–18268,17541–17554`)

   `beginShapeContact` creates or resumes the construction session, marks `contactActive`, and calls `PrepareRuntimeConstructionContact`. Raw Up supplies the final endpoint; `ConfirmRuntimeConstructionContact` calls `ConstructionSession::ConfirmDrag`. Unless point-editing, that function appends a vertex and leaves point construction awaiting explicit completion. (`D/Draw3.ShapeRuntimeActions.inl:515–542`; `D/Draw3.DrawingController.cpp:4000–4013,17811–17825,4054–4076`; `inkStrokeModelerTest/draw3/shape_interaction.inl:806–825`)

2. **SelectionOverlay has conflicting native and logical footprints.** It is a layered popup registered for touch. Its size includes construction bounds; active point constructions publish a preview object, bounds, and vertex handles, so the overlay remains visible and input-enabled. (`O/SelectionOverlay.cpp:283–287`; `B/Bar.ShapeEditor.cpp:1392–1397,1012–1019,1434`; `D/Draw3.ShapeRuntimeActions.inl:387–398`)

   During construction, `Impl::Hit` accepts **only vertex disks of radius `22 × displayScale` pixels**. However, `DrawEditorInputCoverage` unconditionally fills `snapshot.bounds`, expanded by three scaled pixels, with nonzero alpha. Only its separate `frameRect` fill excludes construction. `Impl::Draw` still invokes this coverage during construction. (`B/Bar.ShapeEditor.cpp:864–868,349–374,1854`)

   If such a blank-body touch reaches SelectionOverlay, `ContactNative` rejects it because `Hit().occupied` is false, but `WindowProc` nevertheless closes the `WM_TOUCH` handle and returns zero. There is no canvas forwarding in that branch. Pointer handling explicitly handles PT_PEN; touch follows the `WM_TOUCH` branch. (`O/SelectionOverlay.cpp:164–176,208–229`)

3. **The construction prompt belongs to the main Bar HWND.** It is anchored to the visible bar, and its pointer handler consumes its presented panel rectangle, including temporarily disabled animation states. Interaction processes it before ordinary bar handling. This supports localized obstruction near the bar, rather than an independent full-screen prompt window. (`B/Bar.ConstructionPrompt.cpp:168–190,280–296`; `B/Bar.RenderLoop.cpp:11916,12536`; `B/Bar.Interaction.cpp:8502`; `.trellis/spec/native-desktop/rendering-and-ui.md:2997`)

4. **Existing vertices intentionally capture nearby taps.** Both overlay hit-testing and canvas fallback use a 22-scaled-pixel radius. A captured stationary tap enters point-editing and completes without appending a vertex. This threshold is shared with mouse input; it is not touch-only. (`B/Bar.ShapeEditor.cpp:866–867,2354–2378`; `D/Draw3.DrawingController.cpp:4003`; `inkStrokeModelerTest/draw3/shape_interaction.inl:783–799,811–815`)

5. **Round-nine exit logic is guarded by completed-editor state.** `beginShapeContact` invokes editing completion only when `editor.object` or focus exists; `finishShapeEditing` requires an editor object. `NewContact` explicitly preserves ongoing construction. Tool restoration also checks workspace/entry identity and pin state. (`D/Draw3.ShapeRuntimeActions.inl:327–346,499–505`; `Inkeys/IdtState.cpp:2476–2485`)

## Hypotheses

1. **Highest confidence — invisible construction-body input coverage swallows touch.** The mismatch is certain in source; whether it caused this device’s particular failures needs native routing evidence. It predicts failures **inside the current shape’s bounding rectangle**, away from all vertices, while outside taps succeed. “Near screen center” follows naturally when the construction spans the center; there is no center-specific coordinate test here. Mouse success does not disprove it: `WM_NCHITTEST` returns `HTTRANSPARENT` for blank body, whereas an already-delivered `WM_TOUCH` is consumed. The project explicitly warns against relying on cross-thread `HTTRANSPARENT` touch forwarding. (`B/Bar.ShapeEditor.cpp:360–374,864–868`; `O/SelectionOverlay.cpp:207–218`; `.trellis/spec/native-desktop/shapes.md:271`)

2. **Medium confidence — a nearby intended vertex becomes an existing-vertex edit.** The hit disk has a 44-DIP diameter, versus the ordinary handle’s 20-DIP visual diameter. At 150% scaling, the radius is 33 physical pixels. This explains failures close to previous vertices, but not a large empty central area. It should also reproduce with mouse at equivalent coordinates. (`B/Bar.ShapeEditor.cpp:866–867,1989`; `D/Draw3.DrawingController.cpp:4003`; `inkStrokeModelerTest/draw3/shape_interaction.inl:811–815`)

3. **Medium/low confidence — rapid taps encounter a stale construction-active flag.** Ingress can drain multiple Downs before construction terminal processing. The next Down rejects `state.contactActive`; that flag clears in `ConfirmRuntimeConstructionContact`, called later while processing ended strokes. Thus queued, physically sequential contacts merit a separate regression. This predicts timing/load dependence rather than spatial dependence. (`D/Draw3.DrawingController.cpp:175–190,22083–22118,22763–22771,4057`; `D/Draw3.ShapeRuntimeActions.inl:533`)

4. **Lower confidence — palm, overlapping contacts, or missing terminal.** Touch-specific rejection exists for additional contacts and palm blocking. Quarantine itself has generation checks and terminal recycling; no permanent-ID leak is demonstrated. Pan requires selection mode, and editor multifinger transforms explicitly exclude construction gestures. These fit the center-specific report less well than hypothesis 1. (`D/Draw3.DrawingController.cpp:5337–5361,17353–17382,3104–3113`; `D/Draw3.ContactInput.cpp:969–991,665–670`; `B/Bar.ShapeEditor.cpp:2453–2459`)

## Needs runtime test

- **Device confirmation:** compare taps inside the construction AABB but outside every vertex disk against taps outside the AABB. Record receiving HWND, overlay `Hit`, RTS Down publication, and vertex count. A failed tap reaching overlay `WM_TOUCH`, producing no occupied hit and no canvas contact, confirms hypothesis 1’s delivery mechanism. (`O/SelectionOverlay.cpp:172–176,208–218`; `D/Draw3.RealtimeStylus.cpp:1350`; `B/Bar.ShapeEditor.cpp:864–868`)

- **Offscreen regression:** extend `RunShapeEditorOffscreenTest` with real active Polyline and Polygon snapshots, render through `Present`, commit, and inspect bitmap alpha. For confirmed points `(600,300)` and `(1000,700)`, sample `(800,450)`: logical hit must be empty **and overlay alpha must be zero**. Current coverage should fail the alpha assertion. Check vertex disks still accept editing, then feed the blank tap through the construction coordinator and assert one added vertex. Repeat with scaled coordinates and nonzero canvas origin. The harness already supports bitmap readback; the existing construction test instead substitutes a presented fixture and checks logical hits only. (`B/Bar.ShapeEditor.cpp:2178–2223,2987,3530–3537`; `D/Draw3.ShapeRuntimeTests.inl:19–67,107–124`)

- Add a queued `Down₁/Up₁/Down₂/Up₂` test before consumer advancement for hypothesis 3. Existing construction tests finish each contact synchronously, and native routing tests exercise ownership predicates rather than layered-window pixel admission. (`D/Draw3.ShapeRuntimeTests.inl:117–120`; `O/SelectionOverlay.cpp:254–280`)

## Recommendation

**First apply a one-function production fix:** in `DrawEditorInputCoverage`, gate the bounding-box `FillRectangle` with `!snapshot.construction.active`. Preserve the vertex hit disks and completed-object coverage. This aligns native construction coverage with `Impl::Hit`, while retaining point dragging. (`B/Bar.ShapeEditor.cpp:349–374,864–868`)

Validate with the extended `--ui3-shape-editor-test` and construction regressions under `--draw3-shape-core-test`; use device routing evidence to establish the touch delivery result. Investigate the separate queued-contact issue if failures remain. These flags dispatch to the existing offscreen and production tests. (`Inkeys/IdtMain.cpp:1937–1941,1964–1968`; `D/Draw3.DrawingController.cpp:11740`)

No files were modified; no builds, executables, or runtime tests were run.