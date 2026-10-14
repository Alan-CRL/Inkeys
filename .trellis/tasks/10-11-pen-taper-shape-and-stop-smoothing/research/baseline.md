# Baseline Evidence / Decisions

Read-only planning baseline: branch bugfix/penstroke, HEAD a50ad143, parent working tree clean via git status --short --ignore-submodules.

- ApplyLiveTipTaper currently endTime-T firstTipIndex, temporal actualSpan/newestScale=lerp(1,.28,S(span/T)); per-point age multiplier. L0 copies realPoints[committedIndex:] then predictions; completed copies real only.
- CommitStablePrefixToL1 protects realEnd-(T+predictionDuration); controller suspends stable commit while endpointAdmission.active. User explicitly confirms preserve original window and 100% onset; do NOT taper entire L0.
- realPoints keep untapered widths. stationary/model points can have repeated/clamped display timestamps. Any new frontier based only on their timestamp crossing may abruptly erase a tail; use arc length for width distribution and real-motion clock for aging.
- Display time ResolvePenDisplayTime = terminalDisplayTime.value_or(logicalInputTime). PhysicalUp lock, independent model/display clocks and fixed stop admission must remain.
- Independent demo has mirrored stroke_geometry but is outside this task. Existing demo test success cannot substitute for production probe.
- Main no-HWND production probe example RunPenWidthProductionProbe in DrawingController.cpp and IdtMain argv early exit. Existing --draw3-hidden-test creates visible HWNDs; do NOT run it.
- vswhere exists and found ARM64 native MSBuild at installed VS/Community/MSBuild/Current/Bin/arm64/MSBuild.exe; discover per command, not hardcode version.
- Mathematical evaluation: L/D 0,.25,.5,1,1.5,2 -> tip widths 100,96.9921875,89.0625,65,40.9375,30 percent; uniform-base maximum radius slope .2953125.

Reference: .trellis/tasks/09-19-draw3-low-speed-stop-convergence/research/dwell-appearance-and-resume.md and current native/runtime-and-rendering.md. User approved the final plan in the subsequent implementation message. No extra creation/start approval needed.
