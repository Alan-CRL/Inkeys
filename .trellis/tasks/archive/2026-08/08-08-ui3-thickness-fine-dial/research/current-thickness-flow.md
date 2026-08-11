# Current UI3 Thickness Flow

## Files and state

- `Inkeys/Inkeys/UI/Bar/Bar.State.cppm` owns cross-thread Slider state: hover/pinned/dragging/Preview-dragging/pressed/capture, integer candidate, and Hold hint/lock/progress.
- `Inkeys/Inkeys/UI/Bar/Bar.Main.cppm` declares `BarUIRendering`, its device caches, `BarUISetClass::Interact`, and `CloseThicknessSlider`.
- `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp` contains UI constants, geometry, state animation, rendering, input loops, capture callback, presets, pen switches, and lifecycle cleanup.

## Existing contracts

- `BarThicknessPreviewTouchDragTravelScale = 3.0` is the only 3× precision constant. `ProjectRelativePreviewWidth` maps `(screenX-pressScreenX)/(trackTravelScreenX*scale)*rangeSpan`, then rounds/clamps.
- `CalculateBarThicknessPreviewGeometry` continuously derives `panelScale`, `previewSide`, preview bounds and track bounds during expand/collapse and side flips.
- `GetBarThicknessSliderRange` yields integer ranges per supported pen mode and DPI.
- `ApplyCandidateWidth` blocks while Hold-locked, writes only integer `thicknessSliderCandidateWidth`, and tracks whether the final candidate differs from the gesture start.
- Rendering reads candidate only during Slider/Preview dragging, otherwise animates from `GetPenWidth()` through `drawAttributePenThickness`; `drawAttributeThicknessSliderNormalized` supplies the programmatic Thumb transition.
- A normal captured gesture calls `SetPenWidth(finalWidth, true)` once after Pointer Up, then clears candidate/drag/Hold state. `SetPenWidth(..., true)` also persists memory, so inertia cannot call it per frame.

## Input and timing

- Mouse, pen compatibility messages, and synthesized primary touch messages converge on `ExMessage`. `directTouchPreviewGesture` is separately marked and must remain Preview-only.
- The current gesture loop already uses `peekmessage_win32` and an 8 ms no-message sleep so Hold can advance while stationary. The outer loop otherwise blocks in `getmessage_win32`.
- `WM_CAPTURECHANGED` clears the Slider state and queues a synthetic Pointer Up to wake the nested gesture loop. `CloseThicknessSlider` is the common availability/lifecycle cleanup path.
- Hold uses 5 px stillness, 500 ms before hint, and a further 1500 ms before lock. Lock freezes candidate until real Pointer Up.

## Rendering and Popup

- Slider active currently derives from hover/pinned/pressed/dragging. `drawAttributeThicknessSliderProgress` morphs Preview to the flat track; Thumb visibility waits for that progress to complete.
- Popup X originates at the animated Thumb center, then clamps against `penTypeSafeRight`; Popup Y follows `previewSide`. Popup and Hold already use reversible `BarUiValueClass` curves.
- Overflow business state (`thicknessPreviewOverflow`) is separate from badge/popup interaction state. Existing rendering decides when a Slider session may carry or recreate the hint.
- `BarUIRendering::GetFrameSolidColorBrush` reuses one mutable solid brush. `DiscardDeviceResources` resets device-bound brushes, effects, paths and caches.

## Integration consequences

- ViewMode must replace the visual meaning of pinned/hover while retaining pinned only for Slider leave policy.
- Candidate-active must include FineDial drag/inertia/settle so Popup and number remain live after Pointer Up without committing.
- Physics should be advanced from the interaction thread's conditional outer-loop polling; rendering remains a pure atomic consumer.
- FineDial programmatic motion should read `drawAttributePenThickness.val`, avoiding a duplicate preset/pen-type animator.
- Popup needs a second geometry endpoint but can reuse its existing visibility/scale animation and interpolate with FineDial progress.
