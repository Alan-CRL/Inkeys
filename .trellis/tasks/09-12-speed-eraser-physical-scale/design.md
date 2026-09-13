# Current design: mouse locating, sustained erasing and visual release

## Preserved foundation
Display/Host/WindowControl, scale resolution, EDID validity and bounds are unchanged. Action and coverage units remain independent. StandardDiameterPx() explicitly returns minimumDiameterPx; no new fixed-pixel calibration or settings UI is introduced.

## Shared controller
Keep the existing80ms actual path-length window, clipping/compression, separate sample/frame states, log-speed smoothstep mapping and monotonic log-width follower. Replace acceleration boost with bounded real-motion evidence:160ms entry,380ms capacity,200ms decay. Evidence rate is derived from normalized scalar speed0.20..0.75 and real elapsed time; missing intervals above80ms provide no continuity evidence. Frame/zero motion cannot grow diameter from residual speed.
Growth permission only caps growth, not the size to hold. Actual coverage fraction controls resistance: growth280..160ms and4..6 log units/s. Enter sweeping only after real diameter reaches50% of its configured span with80% full evidence; exit below25%. Hold/confirmation interpolate100/100..650/680ms; shrink160..300ms and4..2.2 log units/s. No changes to Dmax or per-stroke adaptive speed calibration.

## MouseLifecycle production boundary
A separate pure MouseLifecycle holds locating and closing visual state, not a hidden hover velocity controller. BeginContact resets the passed Controller with the current batch config. EndContact takes the actual accepted geometric diameter by value, clears the Controller immediately, and starts a140ms smooth visual-only contraction for the final shared owner. A below-standard end diameter is not enlarged.
DrawingController calls completion at accepted Up/Cancel, records a once-only finish flag and excludes mouse speed eraser Up from reconnect deferral. Failed initialization has its current Down snapshot available for cleanup. Late bake/handback cannot reintroduce old mouse state.
Mouse release requests existing animation frames even without new input, uses actual rendering QPC, and never writes to InkPoint/history. New Down/configuration changes/cancel invalidate its visual. Pen lanes and250ms handback remain; core StartKind::Hover is not treated as a no-growth lifecycle state.
Reconnect eligibility now derives the eraser tool value from DrawingTool::Eraser instead of obsolete numeric2. Actual Touch/inverted-Pen recovery retains controller pause/copy/reanchor semantics.

## Validation and ownership
All research, edits, review, complete ARM64 solution builds and --no-window tests are performed by the main agent only. Regression was first run against c433b6f5 behavior and produced8 expected failures. See validation-mouse-interaction.md for final results and quantitative product meanings. No GUI, commit, push or task archival.
