> 历史记录：以下为c433b6f5基础版本验收；当前鼠标交互规则及结果见 [validation-mouse-interaction.md](validation-mouse-interaction.md)。

# Validation: Draw3 speed eraser

## Recovery and ownership
- Recovered in the same worktree on feature/eraser; HEAD c8560012b0199111307c53cc314678358885fa2a.
- Initial read-only status/diff showed completed ink_prediction/StrokeGeometry/DrawingController integration, project/test registration and the shared header. Core implementation, tests and Host publication were missing.
- After the user's recovery instruction, all inspection, implementation and validation were performed by the main session, without invoking subagents. Existing work and the pre-existing uncommitted PptCOM.dll were retained.
- Tasks remain in_progress; no commit, push or archive.

## Completed changes
- Shared hardware-independent controller and deterministic tests, independent motion/coverage scales, medium time-based dynamics, Touch displacement gate, reconnect re-anchor, accepted contact width.
- Host subscribes to Display and publishes a cached per-monitor scale; existing paintDevice changes are bridged and synchronized.
- Overlapping Down/Up batch membership uses raw QPC, including Up already consumed in the same frame.
- History compression clips expired portions and merges the shortest adjacent intervals. Repeatedly extending one old interval was caught by the rate/idle tests and fixed; acceptance thresholds were not weakened.
- Input absence exceeding one second advances idle dynamics and re-anchors the next observation; it does not guess velocity along the missing path.

## Commands and results (2026-09-12)
- Full InkeysRepo.sln Debug|ARM64, native ARM64 MSBuild located from current VS using vswhere: PASS, exit 0.
- Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window: PASS, exit 0.
- Deterministic traces: 60/125/240/1000Hz input, 30/60/144/240Hz frames, both modes, DPI96/144/192 and physical/DIP scales. Worst checkpoint diameter difference: 0.686986%, below5%.
- At1.3s after motion stops, worst diameter residual above minimum:9.52874%; complete later convergence and animation sleep also pass. Here 'near minimum' is asserted as at most13% above minimum.
- Tests also cover120ms pause/reversal retention, continuous local sweeps, sustained fine motion, sudden acceleration, jitter/tap startup, state copies, reconnect gap exclusion, stale/duplicate time, scaled width interpolation, contact radius and overlapping-QPC batch boundaries.
- Final build:3 warnings from pre-existing additional/hashlib++ conversions,0 errors.

## Environment versus code
- Sandbox build terminated after PptCOM with exit1 and no compiler error. The authorized build outside the sandbox ran successfully.
- The MSBuild invocation removes duplicate PATH and restores the captured value under the single Path spelling in the SAME PowerShell invocation, with MSBUILDDISABLENODEREUSE=1. This also keeps pwsh available for existing post-build steps. No toolchain/project workaround or global environment setting was changed.

## Manual acceptance outstanding
Real mouse/tablet/touch hardware feel, physical multi-monitor hotplug/rotation/mode changes and Win7 SP1 runtime behavior were not exercised. No interactive GUI was launched.
