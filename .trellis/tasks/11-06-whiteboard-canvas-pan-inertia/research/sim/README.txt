Simulation scratch for .trellis/tasks/11-06-whiteboard-canvas-pan-inertia/research/inertia-root-cause.md
(pure python 3 stdlib, no product code touched; run with  python -I <script>  from this directory)

navport.py               exact port of Draw3.CanvasNavigation.cpp motion math (ClampVelocity, ResetVelocitySamples,
                         AppendVelocitySample, EstimateVelocity, BeginCanvasPan, ResetCanvasPanVelocitySamples,
                         UpdateCanvasPan, CanvasPanReleaseAgeSeconds, EndCanvasPan, StepCanvasPanInertia).
                         `python -I navport.py` self-checks against the C++ unit-test constants (853 DIP / 1000 DIP/s).
streams.py               finger trajectories (steady / flick_sin / flick_lin / stop_lift) + delivery model
                         (report rate 60/120/240/125/500/1000 Hz, arrival jitter, bursts, 1-3 ms quantization, delayed Up,
                         WM_MOUSEMOVE coalescing, tiny first dt) + render-loop frame clock (30/60/120/240 fps, stalls).
drivers.py               run_current(): replays Draw3.DrawingController.cpp updateCanvasNavigation (mailbox = latest
                         snapshot per contact per frame, centroid, topology reset, release age, inertia step).
proposed.py              candidate replacement: per-pointer Tracker (lsq1w50 / lsq2 / lsq2b* / avg21 / impulse),
                         soft stop gate, closed-form exp+friction glide, Chromium-style Booster, run_proposed().
run_current.py [N]       scenarios A-I against the CURRENT code            -> results_current.txt (N=500)
run_current2.py [N]      deeper tails: 2-finger stagger, mouse press-moving, drag-and-place, v^2 table, pen brake
                                                                           -> results_current2.txt (N=1500)
compare.py N models secs current vs candidate estimators on identical streams
                                                                           -> results_compare.txt (N=400,
                         models cur,lsq1w50,lsq2,impulse) and results_compare_variants.txt
                         (cur,lsq2,lsq2b6,avg21,lsq2+ring; sections 2,3,4,6; "+ring" = producer-side per-event ring)
                         section 10 (render-loop 30/60/240 fps)            -> results_compare_fps.txt (N=400, cur,lsq2,lsq2+ring)
run_stacking.py          fling-boost / stacking study                       -> results_stacking.txt
repro_topology_spike.py  deterministic (no RNG) repro of the 2->1 finger topology spike

All numbers are Monte-Carlo with fixed seeds; reproducible bit-for-bit.
Assumed (NOT measured on a device): QPC 10 MHz, arrival jitter 0.7 ms half-normal + 2 ms base latency, 0.15 DIP position noise.
