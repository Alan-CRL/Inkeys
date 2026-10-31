# Fine-band stability validation

Date: 2026-09-15
Baseline: 1236bbb4f28af2700b4229b8588ca160f13974d0
Branch: feature/eraser
Task remains in progress. Main agent only; no delegation, commit, push or archive.

## Changed files

- Draw3.SpeedEraser.h/.cpp: minimum plateau, long-window fine intent, hysteresis, confirmation, low-band following and diagnostics.
- Draw3.DrawingController.cpp: publish fine diagnostics in the existing snapshot.
- Draw3.Host.cpp: append low-frequency FineBand console output for active eraser contacts.
- InkeysHeadlessTests/speed_eraser_tests.cpp: red/green fine-band, quantization, sparse-input, timing and lifecycle coverage.
- Draw3.HiddenWindowTest.cpp: actual Host mouse ingress at sparse one-pixel steps, cursor/current-radius assertions.
- .trellis/spec/native-desktop/input-and-ink.md and task PRD/design: replace obsolete low-band rules and retain persisted area-option semantics.

Input collection, coordinates, prediction/modeling, EDID, contact-area conversion/multiplier/ceiling, DIP base sizes, settings persistence and fixed erasing were not changed.

## Parameters and units

| Response | Hold | Release | Standard transition end | Unit |
| --- | --- | --- | --- | --- |
| Indirect | 20 | 35 | 100 | DIP/s |
| Screen pen, physical | 4 | 7 | 20 | mm/s |
| Screen pen, fallback | 16 | 28 | 80 | DIP/s or its labeled heuristic action unit |
| Touch, physical | 6 | 10.5 | 30 | mm/s |
| Touch, fallback | 20 | 35 | 100 | DIP/s or reference DIP/s |

Hold and release are resolved as 0.20 and 0.35 of each model's existing fineToStandardSpeed.
Minimum/standard/maximum remain 16/32/160 DIP under the default size configuration.

- Fine observation window: 140 ms, including zero displacement and no-Move time.
- Enter/shrink confirmation: 100 ms.
- Release/growth confirmation: 160 ms.
- Shrink tau: 200 ms; growth tau: 260 ms.
- Maximum logarithmic shrink/growth: 4/3 per second.
- Stable target required before exact settling: 40 ms, using the existing settle tolerance.
- Existing short sweep windows, qualification thresholds, evidence constants, high-band formulas and resistance parameters are unchanged.

Below hold, the reference is exactly minimum. Between hold and the existing fine upper speed, offset smoothstep gives a continuous reference.
Held fine intent does not collect release evidence inside the hysteresis band. Direction confirmation is not restarted by every same-direction target change. Opposite short pulses deplete rather than immediately erase pending confirmation.
Entry/release and direction evidence run in parallel. Sweep qualification bypasses the low-band confirmation instead of waiting to grow to standard first.

## Important boundary details

- The existing effective-movement/idle clock still recognizes bounded subpixel jitter. Its history supplies fine entry evidence directly, without another serial confirmation delay.
- Minimum logarithms use double precision consistently with the idle/sleep comparison. This avoids a tiny float/double discrepancy that otherwise kept animation alive at a visibly exact 16 DIP.
- High-to-low shrinking preserves the high-band process and splits the elapsed time at standard, passing the remainder into fine following.
- Fine state belongs separately to raw sample and frame-preview state. Confirmation fields are durations, so reconnect preserves them while existing absolute timestamps/history shift. Synthetic bridge distance remains excluded.
- Fresh compatible Hover transfers a safe diameter and held fine intent, never sweep momentum.
- Area-driven sizes keep the existing area permission/response. Fine holding cannot force an accepted area floor down to minimum. New Touch, no-Move expiry, invalid-area handling and persistence are retained.

## Red-first test evidence

Tests were added before changing product behavior and run with the baseline controller.
The old implementation returned exit 1 with 1844 failed assertions in the extended suite.
The 1728-case quantized/sparse matrix measured a worst fine-band peak-to-peak value of 4.60458 DIP.
The failures exposed the missing finite plateau and weak/single-step growth; baseline code also lacked the new diagnostic interface.

Only the old 600 ms stationary convergence checkpoint was moved to 900 ms to accommodate the deliberately slower confirmed fine shrink. Its 2% size tolerance was not loosened.
Old end-to-end low/high replay output is now labeled LowBandChangedReplay, not treated as a frozen bit-pattern requirement, since the intentional low-band history changed.
The global 5% rate tolerance, 0.03-DIP bounded-noise comparison and existing area/geometry assertions were retained.

## Execution history and final results

The first full build failed on two introduced source errors: mixed float/double std::min arguments and a Windows min/max macro collision in the hidden test. Both were fixed after user approval.

The next full build succeeded. Headless then found exact-minimum sleep and bounded-jitter idle regressions. Both were fixed after user approval; neither required sweep or area parameter changes.

Final runs:

| Validation | Actual result |
| --- | --- |
| Native ARM64 full InkeysRepo.sln Debug/ARM64 | exit 0, 0 errors, 3 existing third-party warnings in final incremental build |
| InkeysHeadlessTests.exe --no-window | exit 0, all tests passed |
| Inkeys.exe --draw3-eraser-hidden-test | exit 0, DComp and ULW exercised |
| Quantized/sparse combinations | 1728 |
| Worst fine steady-state peak-to-peak | 0 DIP |
| Held exits in that matrix | 0 |
| Long idle after every matrix replay | sleeping |
| Entry to minimum + 0.5 DIP | at most 0.720 s in tested plateau traces |
| Release of held fine intent under sustained ordinary motion | 0.200 s in the representative timing traces |
| Recovery to standard - 0.5 DIP | 1.184 s in those traces |
| Existing sample/frame deviation | 1.99186%, unchanged |
| Existing area sample/frame deviation | 0.429525%, unchanged |
| Established high-zone trace comparisons | 6 rows, no bit differences |

The matrix covers 96/144/192/288 DPI, 60/125/240/1000 Hz, 30/60/144 fps, two event/grid phases, 0.5/1/2 px grids, horizontal/diagonal/reversing paths, repeated-position packets and sparse nonzero-only input.
Additional tests cover alternating 60/100 ms movement and 40/120 ms pauses, five-second weak release intent, a single pixel step, sustained ordinary motion, sweep bypass, delayed input, copying/reconnect, fresh Hover-to-Down and actual Touch startup unlocking.

The six exact high-zone comparisons begin from established large sweeps and include a short pause/reversal. These fixtures are upper-saturated traces, not a claim that all possible high-band trajectories were exhaustively compared.
Source checks additionally confirmed that physical compensation and the area converter/reference/expiry functions were unchanged. Existing high-band response and area tests all passed.

## Product ingress and geometry evidence

The hidden tests use the real Host, mailbox, drawing thread and rendering backends, not only an isolated controller.
New sparse one-pixel mouse movement tests measured:

| Backend | Peak-to-peak DIP | Maximum DIP |
| --- | --- | --- |
| DirectCompositionVisualTree | 0.0569839 | 16.0570 |
| UlwDirtyRect | 0.0549164 | 16.0549 |

The small variation includes the tail of the initial convergence before the 16.1-DIP wait threshold. Both are below the 0.5-DIP target.
Each sample also checked held fine intent and equality of cursor diameter and twice the current accepted radius.

Existing integration checks passed for pen/Touch routing, Touch point erasing, no-Move contraction and stop-waking, area-assisted 39-DIP floor, expiry back to 16 DIP, resumed 8-pixel radius with a same-position size anchor, immutable old geometry, Undo/Redo and actual UInk save/read/import.
The new sparse one-pixel ingress probe itself is Mouse; ideal/quantized Pen and unlocked Touch fine trajectories are covered headlessly. Real hardware feel remains manual acceptance.

## Build environment and preserved work

The build used the located ARM64-native MSBuild, full solution /m:1 /nr:false /p:Configuration=Debug /p:Platform=ARM64, with Path/PATH normalization in the same PowerShell invocation. No global environment or project/toolchain settings changed.
PptCOM.csproj dependency and its normal export/copy steps were retained.
PptCOM.dll SHA-256 before and after:
779908B2ACC9F37241A4C59C36822685E87A07C4BC886BDE22D9D167E68FE117

Earlier rebuild warnings include existing conversion/alignment/shadow warnings and a test-only unreachable fallback warning from supporting the old diagnostic-free red baseline. No warnings were suppressed.
No visible GUI or Computer Use was launched.

## Logs

- Build/fine-band-red-build.log
- Build/fine-band-red-tests.log
- Build/fine-band-build.log
- Build/fine-band-build-fixed.log
- Build/fine-band-tests.log
- Build/fine-band-boundary-build.log
- Build/fine-band-boundary-tests.log
- Build/fine-band-hidden-out.log
- Build/fine-band-hidden-err.log

## Remaining manual verification

Actual mouse, pen, Surface Touch and large-screen fine feel should still be compared by the user. No claim is made about newly measured physical hand motion or Windows 7 runtime.
The pre-existing touch-area setting remains persisted and the console output option is retained.

