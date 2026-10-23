# Implementation: native ROI LK maxLevel calibration

- Date: 2026-10-23; current Trellis persistence-regression task remains in progress.
- Scope: authorized minimal production parameter calibration and native-size tests; no GUI, commit, push or task completion in this implementation step.

## Changes and unchanged contracts

`Inkeys/Inkeys/CV/VisualMotionTracker.cpp` changes only the forward/backward LK `maxLevel` literals from3 to5 and adds one Chinese comment. **maxLevel is a zero-based highest level**:5 requests levels0..5; OpenCV may build fewer levels for small images. The21×21 window, termination criteria, FB/consistency/coverage/quality gates, birth anchors, accepted-reference transaction, cumulative output and three-reject sticky pause remain byte-for-byte unchanged outside those two literals/comment. No Config, public API, phase correlation, dependency or capture/input/GPU/save changes were added.

`InkeysHeadlessTests/visual_motion_tracker_tests.cpp` appends only new helpers/cases and two calls. Removing those new blocks/calls reconstructs the original UTF8-BOM/CRLF file exactly; the original14 legal groups/6918 frames, tolerances and all old negative assertions remain intact. Both changed files preserve UTF8 BOM, pure CRLF and original EOF.

## Regression evidence

- Original14 groups:6918/6918 legal frames accepted, zero rejected/paused. Original invalid layout, black/pure-color, rejection-gap transaction, shape changes, no-overlap and majority-fixed4px ambiguity cases still pass.
- New native ROI is immutable controlled BGRA world texture,2880×1808, crop-origin equivalent to physical(0,48). It is generated from the same world formula used by the dedicated native GUI fixture; it is not a saved historical Mag frame.
-22 new legal groups/704 Process frames: native stationary300,20 four-direction amplitude16/32/64/128/192 cases with stops/reversals (120 frames), and a128px-step roundtrip (284 frames). Every legal frame must be accepted and satisfy the unchanged `max(3px,path*0.2%)` prefix bound; native stationary300 uses the original stricter0.5px cumulative limit. Stops also retain a0.5px per-stop drift assertion. All passed.
- Native stationary300: accepted300, max/final/stop drift0.00000px. Process median33.29500ms/P9543.82760ms in this run.
- Native128 roundtrip:142 steps each way,18176px per leg (at least10×1808), path36352px, accepted284/rejected0, max/final cumulative error0.00118px. The last initialization feature retires on accepted frame14;270 further accepted frames and13853 added features directly prove continued relay. The code does not infer retirement from relayGeneration or rejected-result defaults. Process median47.79480ms/P9570.57380ms.
- Four new native-size negative groups reuse the original middle-patch/affine construction: majority-fixed plus middle4px returns AmbiguousMotion; disjoint new world page returns ForwardBackward; scale1.02 and rotation1degree return NonTranslation. Each rejects three changed frames, preserves total/delta/last trusted reference, pauses on the third and stays paused when the original image returns. These12 changed-frame checks and four post-pause probes passed.
- The same new positive128 case was compiled against an exact frozen originalL3 source copy in an ignored executable. It fails both acceptance and prefix-error checks (intentional exit2/failure count2), with43 FB-valid points/two inliers/InconsistentTranslation. This establishes sensitivity to the former defect; it is not a failed final production regression.

## Builds, commands and receipts

- `Build/ScrollFollow/build-motion-tracker.ps1 -Round native-lk5-calibration-1023`: full `InkeysRepo.sln Debug|ARM64`, ARM64-native MSBuild via vswhere, original same-invocation PATH normalization/node-reuse disable,600s script budget. Exit0/18.935s, no warning in this build.
- `InkeysHeadlessTests.exe --visual-motion-tracker-only`: PID4204, exit0/334.743s,600s budget; stdout ends `PASS visual motion tracker failures=0`, stderr0. Private writable TEMP retained. The full UI headless suite was not repeated because the change is restricted to the independent tracker and its tests.
- Final product protocol/state: exit0/276ms and0/118ms;25 malformed internal CLI combinations each exit2/stdout0/stderr0. Receipt `Build/ScrollFollow/product-live-stage/offline-7819578/result.json`.
- Recompiled ignored diagnostic directly from final production source and existing ARM64/OpenCV/zs libraries. Native sweep exit0/28.918s;960×540 spot exit0/1.114s;1920×1080 spot exit0/1.547s. Raw `Build/ScrollFollow/texture-motion-diagnostic/final-source/*`, including source/executable hashes and metadata.

The native diagnostic admits128/192px in all four directions for both controlled texture and original test Canvas. This is **not universal128px support**:960×540 half-size480×270 clamps requested maxLevel5 to actual3 and rejects128px;1920×1080 half-size960×540 clamps to actual4, where the controlled texture accepts horizontal128 but rejects vertical128, and the original Canvas rejects all four. These remain observed capability boundaries, not permanent normative rejection tests. All refusals retain the trusted position.

## Artifacts and ownership handoff

- `Build/ScrollFollow/texture-motion-diagnostic/native-calibration/` contains exact before files, `production-calibration.patch`, `manifest.json`, `matrix-summary.json`, complete tracker logs/result, and old-L3 single-case compile/run receipts.
- `Build/ScrollFollow/texture-motion-diagnostic/final-source/summary.json` lists the final three-size diagnostic metadata and first-step results.
- Final production Tracker SHA256: `BF3409F88F606C82B31AE4BEB30B592C105E945028C115FF06109EDB843B1978`.
- Final tests SHA256: `EF45B732AE3D2D9432F9FFA76E6C06C1697BE17E711F01B152F6B2BEA92FE667`.
- Final product EXE SHA256: `8DAF6D3B9DB1368C487CEB76CEA9E8EE733F8444CCE68FD421207FD133FDED21`.
- Final headless EXE SHA256: `CC3F315472FA01EAAF1DD9E00557C9099868CF1C6B77F36963CE1D4138FFF0A5`.

Source/binaries are frozen and build/test/GUI execution ownership has been returned to the root session. Independent review and actual short/unchanged128px-step19200px-long GUI gates remain required. No GUI was run after the calibration. No claim is made that all historical Mag failure conditions are reproduced by synthetic images. Native per-frame feature-cap assertions and maximum (rather than median/P95) Process latency were not separately added; they are nonblocking observation gaps. The existing original feature-budget tests still pass. Process timings exclude test image generation; neither they nor whole diagnostic duration establish capture/product FPS.
