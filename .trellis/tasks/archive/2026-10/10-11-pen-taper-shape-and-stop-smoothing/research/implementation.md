# Implementation Evidence

## Production change
- The original end-time minus live-tip-duration / firstTipIndex selection is retained exactly, including the prediction reference. The entire L0 is not newly tapered.
- Shape now uses centreline arc length, the largest original radius in that candidate, and the approved SmoothStep length/diameter constraint (30% at >=2D, 65% at D, 89.0625% at 0.5D, 100% at zero).
- Dwell age uses physical display time and last real movement, with the existing future prediction reference while prediction is present. Model/terminal synthetic point timestamps cannot restart aging.
- Per-stroke transient state is only optional effective length, movement timestamp, and evaluation display timestamp. Matching older cache can cap retiring length; a newer movement or an earlier locked Up ignores incompatible cache. Actual positionMoved explicitly clears the optional cap because raw QPC assignment precedes strict model-time normalization and can repeat; successful ClearPenTerminalState also clears it. Reset clears all three.
- The compression frontier inserts a linear connector only in derived L0/completed geometry. Real points, stable commit selection, predictor configuration, and GPU/storage formats are unchanged.
- Active/completed/Stored share the production recipe and retain existing capsule tangency. Physical Up remains frozen by ResolvePenDisplayTime.

## No-HWND probe
RunPenTaperProductionProbe lives in the already compiled StrokeGeometry unit and is exported by ink_prediction. IdtMain recognizes --draw3-pen-taper-test before normal configuration, single-instance, Host/RTS or window initialization; extra arguments exit 2.

The final probe covers explicit ratio and continuity tables across 0.125/1/3/5.3333335/8/24px widths, closed-circle arc length, duplicate/equal-time/sparse points, maximum body radius, unchanged prediction cutoff, shared L1 connection/protection cursor, old/new dirty union, raw-point preservation, non-rebounding synthetic catchup, resume/reset/two-stroke isolation, earlier physical Up after a later live cache, delayed completion and Stored equality, disabled/hardware taper, and unchanged highlighter geometry.

Real-model cases use the production StrokeModeler and shared model/geometry/frame helpers at 30/60/120/240 FPS, Disabled/StrokeEnd/Kalman predictors, 25/500/1500px/s, Fixed/Simulated width. They advance actual ten-second display frame loops with bounded model scratch, verify endpoint plane/distance, retirement length/tip thickening, output budget and settled/frozen conditions, 36 cases also resume with real model input in forward/right-angle/reverse directions. All 72 send actual kUp, establish the same terminal admission boundary as the product controller, assert the exact raw endpoint is pinned, then check frozen completed/Stored widths. These are CPU model/geometry tests; they do not invoke a HWND/Host or the complete DrawingController owner loop, and are not real-device visual evidence.

## Verification state
See verification.md for final exit codes, environment recovery, logs and encoding/diff evidence.
