# Touch contact-area metadata conversion

Date: 2026-09-14
Baseline: cb38173d0a6b4990370323ee3712dc0762ac86f6
Branch: feature/eraser
Task remains in progress. Main agent only. No commit, push or archive.

## Scope

Only contact-area metadata interpretation, relative-length conversion, cached console diagnostics and related tests changed.
Mouse/pen/Touch speed thresholds, resistance, size curves, DIP base diameters, area multiplier/ceiling, startup/idle/reconnect policies and EDID semantics are unchanged.
The existing dirty PptCOM.dll was not reverted or staged. The normal solution build retained its PptCOM dependency and export/copy steps.

## Findings and implementation

1. The old equality-only gate rejected valid length properties with different resolutions.
2. The old area code multiplied WIDTH/HEIGHT by the context ink-to-digitizer factor, independently of the existing position-to-canvas mapping.
3. The old TouchAreaDevice dump was compiled out unless DRAW3_RTS_DIAGNOSTICS was defined, and could not replay an already-created context.

The product now caches each axis transform when publishing the RTS context:
spanToAxis = axisResolution / spanResolution * spanUnitCm / axisUnitCm.
spanToCanvas = spanToAxis * abs(existingPositionScale).

WIDTH/HEIGHT are interpreted by their own returned PROPERTY_METRICS. There is no assumption that every property has been normalized like X/Y, no fixed divisor, no position translation, no subtraction of a logical origin and no second context scaling.
The existing position expression is untouched. Canvas-to-DIP conversion remains in the existing controller.

Inches and centimeters are explicitly supported. Missing units/properties, unsupported unit semantics, invalid resolution, invalid declared range, bad transforms, nonfinite values and out-of-range packets remain unavailable with distinct reasons.
A valid conversion does not bypass the existing area plausibility, outlier, startup or movement checks.

TraceTouchAreaDiagnostics copies the existing bounded RTS cache, then formats outside cache/lifetime locks. Enabling diagnostics after initialization replays cached Touch metadata. Actual source requests match context/generation and resolve the same cursor mapping; no Mouse/Pen substitution is used.
The existing runtime switch and console are reused. Metadata changes are limited to at most one dump attempt per second, apart from explicit enablement; normal TouchArea frame samples retain the existing 250 ms limit.

## Actual device metadata obtained

The hidden integration run successfully queried the existing RTS cache on this machine after contexts were created.
One Touch context had no WIDTH/HEIGHT. Another integrated Touch context returned:

| Property | Present | Index | Units | Resolution | Logical range |
| --- | --- | --- | --- | --- | --- |
| X | yes | 0 | 2, centimeters | 1000.29218 | 0..27388 |
| Y | yes | 1 | 2, centimeters | 1000.43835 | 0..18258 |
| WIDTH | yes | 3 | 2, centimeters | 1000 | 0..27388 |
| HEIGHT | yes | 4 | 2, centimeters | 999.999939 | 0..18258 |

The old tolerance was relative 0.00001. These resolution differences exceed that tolerance, although all four properties are lengths in centimeters.

The corresponding context factors were 0.718845129 and 0.718818903.
Multiplying the user's supplied raw 1374 x 1578 by those factors reproduces the old 987.693 x 1134.296 values. This is strong evidence for the old conversion defect, not a live packet capture.

In this hidden, early-startup context the existing shared position factors were 0.0377952754 x 0.0377952754, not the normal visible application's 192-DPI assumption.
The cached length factors became 0.0378063182954 x 0.0378118454864.
Applying the supplied raw values would give approximately 51.9459 x 59.6671 pixels in THIS hidden coordinate mapping. This is arithmetic replay only, not measured Surface output, and must not be copied into the product as a calibration.

No real finger packet or cursor-to-monitor association was captured in this run. The dump explicitly says mapping=awaiting-touch-cursor. The context IDs are ephemeral, not hardcoded or assumed equal across runs.

## Validation actually executed

| Validation | Result |
| --- | --- |
| Pre-change InkeysHeadlessTests.exe --no-window | exit 0 |
| ARM64-native full InkeysRepo.sln Debug/ARM64, /m:1 | exit 0, 0 errors, 129 warnings |
| Updated InkeysHeadlessTests.exe --no-window | exit 0 |
| Inkeys.exe --draw3-eraser-hidden-test, invisible DComp/ULW HWNDs | exit 0 |
| 12 frozen Mouse/ScreenPen float-bit traces | unchanged |
| 27 baseline/current Frozen/R7/SpeedEraser diagnostic lines | no differences |
| Area sampling/frame deviation | unchanged, 0.429525% |
| Speed sampling/frame deviation | unchanged, 1.99186% |
| git diff --check | exit 0 |
| UTF-8/BOM/source EOL checks | passed; existing formats retained |

New headless cases execute the production converter for equal/different resolutions, inch/cm conversion both ways, missing/unsupported units, invalid resolutions/ranges/scales, nonfinite/out-of-range packets, nonzero logical origins, reflected axes, anisotropic/rotated axes and 96/144/192 DPI.
Synthetic converted areas are also fed through the actual controller to confirm small Touch Down, reference creation, unchanged 39-DIP example floor and real no-Move expiry.

The hidden ingress test now obtains its synthetic area through the production converter before injecting it. Existing checks still cover point-erasing startup, same-position pressure changes, no-Move shrink/expiry, long-idle recovery, unchanged old geometry, new small-radius segments, Undo/Redo and actual UInk save/read/import.
The observed hidden example settled at 39 DIP, returned to 16 DIP after expiry and resumed with an 8-pixel radius while keeping historical geometry.

The first /m:4 solution invocation stopped before C++ compilation, returning 1 with 0 logged errors. A sequential /m:1 invocation, with diagnostic logging, completed in 1:33.56 without changing source or project/toolchain settings between attempts. The precise multi-node runner failure remains undiagnosed.
Both invocations used the located native ARM64 MSBuild, normalized Path/PATH in the same PowerShell invocation and disabled node reuse.
Warnings include existing third-party/conversion warnings and three new C4456 local-name shadow warnings in conversion tests; no warnings were suppressed.

Logs:
- Build/touch-area-metadata-baseline-tests.log
- Build/touch-area-metadata-build.log
- Build/touch-area-metadata-build-diagnostic.log
- Build/touch-area-metadata-tests.log
- Build/touch-area-metadata-hidden-out.log
- Build/touch-area-metadata-hidden-err.log

## Remaining device acceptance

Synthetic conversion and eraser integration pass. Surface real-packet acceptance is NOT yet verified.
The remaining evidence must come from a normal drawing session using the same source:
- TouchAreaDevice context/generation/cursor and actual mapped monitor.
- Actual raw WIDTH/HEIGHT and runtime position factors.
- convertedPx, width/height DIP, sampleValid and reason.
- referenceReady (ready), activeFloor (acceptedFloor), areaActive.
- Actual newly generated eraser geometry, cursor size and preserved historical radii.

Enable the existing experimental TouchArea console option, restart normally, then compare area assistance off/on during a real Touch drag. Supply matching TouchAreaDevice and TouchArea lines.
Do not interpret this hidden metadata dump as proof of real Surface feel or coverage. No visible UI or Computer Use was launched; external hardware and Windows 7 runtime were not tested.

## References

- [PROPERTY_METRICS](https://learn.microsoft.com/en-us/windows/win32/api/tpcshrd/ns-tpcshrd-property_metrics): logical range, physical units, resolution.
- [PROPERTY_UNITS](https://learn.microsoft.com/en-us/windows/win32/api/tpcshrd/ne-tpcshrd-property_units): DEFAULT is unknown; inches and centimeters are explicit lengths.
- [GetPacketDescriptionData](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-irealtimestylus-getpacketdescriptiondata): actual returned property order/metrics and ink-to-digitizer scaling factors.
- [PacketPropertyGuids](https://learn.microsoft.com/en-us/windows/win32/tablet/packetpropertyguids-constants): WIDTH/HEIGHT are touch contact extents.

