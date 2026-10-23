# Research: native RTS Down versus integer cursor oracle

- Query: Does PID62840's actual Exit UInk support the integer OS cursor Down oracle at .001px, or the RTS-decoded client Down plus viewport?
- Scope: internal; read-only parsing of this task's owned file/logs, no GUI, build, new binary, package installation or source/assertion changes.
- Date: 2026-10-22

## Findings

**The current .001px comparison against integer OS cursor Down would reject this actual file even though both saved first points match the observed RTS-decoded Down plus the correct viewport.** This is a concrete fixture oracle mismatch, not evidence that the saved viewport was added incorrectly.

| Stroke | Integer OS client Down | Actual RTS Down log (rounded3 decimals) | Actual UInk first point | Applicable viewport | Error from integer expected world y |
|---|---|---|---|---|---|
| A | (768,706) | raw(10160,9340), decoded(768.000,706.016) | (768,706.0157470703125) | (0,0) at A Down | +0.0157470703125px |
| B | (768,1346) | raw(10160,17806), decoded(768.000,1345.965) | (768,1361.96533203125) | (0,16) at B Down | -0.03466796875px |

B's saved first y minus saved viewport y is **1345.96533203125**, consistent with the trace's1345.965. It is not1346, and that difference already exists before world-coordinate conversion. A likewise matches its706.016 trace rather than integer706. The .001px threshold is respectively about16 and35 times smaller than these observed input quantization differences.

### Actual owned file observations

- File: `Build/ScrollFollow/product-live-62840-230285718/AutoSave/desktop/2026-10-22/232049219_6e708a52.uink`.
- Size12285bytes; SHA256 `80dde05dcc951f4f6b5d6005a66dad0a47c6af763091fc4f3fb1d961a633772a`.
- Private index has exactly one `trigger=exit` entry, dailySequence1/sequenceInSession1. Its fileGuid `b7a2fdbd-8d95-45da-94fc-06bcf96f93b7` matches the file header.
- Fully consumed the MessagePack byte stream using a small ephemeral Python reader with standard-library `struct`; no `msgpack` package was available or installed.
- Top-level records: header [0,10,fileGuid,1,1,1,timestamp] bytes0..69; workspace bytes69..188; canvas bytes188..425; two type3/inkType1 Pen records bytes425..6355 and6355..12285.
- Canvas workspaceGuid `7a33fb3b-57df-4cf0-bf16-575ae3e9a411`, pageGuid `be2bba62-2181-403c-9762-5c39b7f3d87f`, viewport **(0,16,1)**.
- Each stroke has224 stored points, first width16. A fallbackRGB16715776, B12543. These are raw file observations; this investigation does not classify B's screenshot pixels.

### Code contract and inference

1. `Draw3.RealtimeStylus.cpp:958-959` computes position as float32(raw packet) multiplied by the decoder's float32 positionScale. `:1687-1694` obtains scale from actual `GetPacketDescriptionData`; integer cursor coordinates are not the product input representation on this path.
2. `uink_codec.cpp:1337-1351` reads float32 point coordinates. `:1441-1444` uses the first stored point as an absolute anchor; subsequent points are deltas. Therefore the first-point values above require no delta accumulation to interpret. `uink_draw3_import.cpp:52-56` preserves those decoded coordinates in the snapshot.
3. Current `Draw3.ProductLiveTest.inl:513-515` instead compares firstA with the integer `aDown` and firstB with integer `bDown + viewport`, using .001px. That exact assertion has not executed in this failed-pixel run, but applying it offline to the observed file produces a deterministic mismatch.
4. As a **cross-check/inference**, using scale=float32(768/10160)=0.07559055089950562 (inferred from the logged rawX and exact savedX, not a newly observed GetPacketDescriptionData receipt), float32(9340*scale)=706.0157470703125 and float32(17806*scale)=1345.96533203125. Adding B's viewport16 reproduces its saved first y exactly. This supports the quantization explanation without claiming the inferred scale was directly logged.
5. For a strict future oracle, compare against the actual published RTS-decoded Down associated with the stroke, then independently prove that input corresponds to the intended integer cursor within a justified native quantization bound. Keep viewport/world addition strict against that decoded fact. Do not derive an expected Down from the saved first point itself, and do not silently widen a world-coordinate threshold based on this one sample. This report does not choose or change any threshold/API.

## Files Found / Related Specs

- Owned `.../index.json` and `232049219_6e708a52.uink`: actual cleanup Exit output.
- `Build/ScrollFollow/product-live-62840-230285718/result.log:20,43,52`: actual OS client Down and final Off viewport.
- `Build/ScrollFollow/live-product-ink-gt-diagnostic-230285000-test.log:128-129`: published RTS Down raw/decoded coordinates, contactSeam disabled.
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp:958`: native packet decoding.
- `inkStrokeModelerTest/draw3/uink_codec.cpp:1441`: absolute first anchor versus subsequent deltas.
- `.trellis/spec/native-desktop/scroll-follow-product.md`: B locks the current viewport exactly once; distinguish OS input evidence from contact seams.
- Task `design.md`: ordinary Pen anchors and original Exit save remain the intended contract.

## External References

The saved-file investigation used only repository code and bytes. Follow-up quantization guidance below checks official Microsoft documentation; no external desktop/input APIs were invoked.

## Caveats / Not Found

- The ephemeral MessagePack parser is an inspection tool, not execution of the C++ `ReadUInkFile`/Import validators. It consumed all bytes and inspected actual fields, but this is not a claim that the product codec's entire validation suite ran on this exact file.
- RTS diagnostic output prints three decimal places; exact matching to its displayed numbers is necessarily rounded. The raw-packet/float32 scale cross-check is explicitly inferred.
- Native run stopped at B pixel classification and cleanup saved this file. The normal `ReadInkExit` PASS and full bounded-case PASS did not occur; none is implied here.
- No pixel failure analysis, assertion edits, source changes or repeated GUI run were performed.

## Follow-up: restricted native mouse quantization bound

Root requested a minimal harness-only bound that does not require a new production observer or assume nearest rounding on every system.

**Applicable fields:** `Display::GetSnapshot()->Primary()->effectiveDpiX` and `effectiveDpiY`, declared as UINT in `Display/Display.cppm:148-149`. The fixture already gets this same primary snapshot at `Draw3.ProductLiveTest.inl:621-624` and rejects fallback/nonzero-origin/mismatched physical geometry. `Display.cpp:346-356` queries effective monitor DPI, with LOGPIXELS fallback. Capture that existing snapshot's DPI in the fixture, record it with the oracle/tolerance, and retain geometry-generation checks. Do not substitute EDID physical DPI, UI scale or divide the viewport by DPI.

**Evidence and scope:** The official [RealTimeStylus sample](https://learn.microsoft.com/en-us/windows/win32/tablet/realtimestylus-plug-in-sample) converts integer ink-space packet coordinates by graphicsDpi/2540; ink space uses .01mm units. The [GetPacketDescriptionData contract](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-irealtimestylus-getpacketdescriptiondata) supplies the actual ink-to-device factors. Production uses the supplied factors, not Display DPI. Therefore Display DPI is suitable only as the constrained dedicated OS mouse fixture's expected HIMETRIC conversion/quantization model, corroborated by the observed native mouse packet values; it is not proof that arbitrary digitizer factors equal effectiveDpi/2540. A documented universal nearest rounding rule was **not found**.

For this native mouse fixture, permit one full integer HIMETRIC quantization unit (allows nearest, floor or ceil) plus a computed float32 arithmetic allowance. Require the unchanged primary1:1 client mapping, actual mouse-only SendInput path and finite positive current effective DPI. If those preconditions or the bound fail, stop the test; do not widen it for hardware or arbitrary transformed tablet input.

For each coordinate independently, with integer actual cursor client Down `c`, actual saved viewport axis `v`, `D=effectiveDpiAxis`, `eps=std::numeric_limits<float>::epsilon()`:

```text
q = double(D)/2540.0
s = double(float(q))
R = ceil(abs(c)/q + 1)
E = R*abs(s-q) + eps*max(1, R*abs(s))
T = q + E + eps*max(1, abs(c)+abs(v)+q+E)
accept abs(savedFirstAxis - (double(c)+double(v))) <= T
```

`R<2^24` is a required guard so raw integer-to-float conversion is exact. The first term is one HIMETRIC unit in pixels; `R*abs(s-q)` covers scale representation, the next term conservatively covers multiplication rounding, and the last covers adding the latched viewport. The `eps*max(1,magnitude)` terms are conservative float32 rounding allowances, rather than an arbitrary decimal tolerance. This computation assumes the fixture's observed mouse scale model `float(D/2540)`; it must not be exported as a generic RTS validator. It also does not prove the OS always quantizes a mouse within one unit: the actual run must satisfy the bound, and contrary evidence invalidates this restricted model.

At192DPI, q=0.07559055118110236px and half-unit=0.03779527559055118px. For this run's coordinates, the full-unit bounds evaluate to:

| Axis/input | Applicable viewport | Computed T | Observed absolute error |
|---|---:|---:|---:|
| A/B x768 | 0 | 0.07577653598749448px | 0px |
| A y706 | 0 | 0.07576152500268933px | 0.0157470703125px |
| B y1346 | 16 | 0.07591840753438879px | 0.03466796875px |

Both observed rawY values also equal nearest(integerCursorY*2540/192), but the full-unit oracle deliberately does not require that narrower rounding rule. The192DPI value here is the model reconstructed exactly from actual raw/decoded/file data and the root's stated machine DPI; the old fixture did not log its Display effectiveDpi fields, so the next invocation should log those actual values rather than treating this reconstruction as a saved snapshot receipt.

Keep viewport/file identity/two-Pen checks and saved viewport comparison at the original .001px. The tolerance belongs only to **native mouse cursor→RTS input quantization** at A/B Down; it does not relax tracking displacement, ink pixel correspondence, the saved viewport or unrelated hidden deterministic coordinate tests. A/B input origins stay independently corroborated by the existing real RTS trace. The fully strict alternative remains comparison against actual published decoded Down, but that would require an existing suitable receipt or a separately reviewed observer; it is not necessary for this minimal constrained harness correction.
