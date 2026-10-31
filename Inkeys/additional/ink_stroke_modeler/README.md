# Ink Stroke Modeler provenance

This directory contains the Google Ink Stroke Modeler headers and 13 runtime
source files used by Draw3, its standalone demo, and the module tests.

- Draw3 source snapshot: D:\Project\Inkeys\inkStrokeModelerTest\Inkeys3-Draw3,
  revision 8d045298eaaac76f752b4f8b5f3303b3520e50b7.
- Runtime source import: the user-provided `compile_library/ink-stroke-modeler`
  source tree; its upstream base is google/ink-stroke-modeler revision
  `2cd45e8683025c28fa2efcf672ad46607e8af869`.
- Upstream license: Apache License 2.0 (see ThirdpartyLicenses/Apache License 2.0).
- `runtime-source-manifest.json` records the byte-exact imported runtime files
  and the retained Modeler headers. The existing `numbers.h` compatibility
  adjustment is preserved. No upstream algorithm or public API is changed.
- `ink_stroke_modeler.sources.props` is the shared runtime source list.
  `internal/type_matchers.cc`, tests, and fuzz targets are excluded.
- `ink_stroke_modeler.props` compiles those files as ordinary C++20 translation
  units, preserving the previous MinSizeRel `/O1 /Ob1 /MT`, `NDEBUG`, iterator
  level 0, `/EHsc /GR /fp:precise`, without PCH, module scanning, RTC, or LTCG.
  Business-code optimization and debug settings are unaffected.
- Abseil is provided by the existing repository vcpkg manifest, pinned to
  `20250512.1#0`. The three consumers use vcpkg Release static libraries with
  `/MT`, `NDEBUG`, iterator level 0, C++20 and `absl::lts_20250512`.
  Abseil `.cc` files are not registered in consumer projects.
- Installed vcpkg Abseil headers precede the canonical additional directory,
  so headers and libraries come from the same package. The retained local
  `20250512.0` Abseil headers/options are unused by these consumers and remain
  unchanged; do not mix them with the active `.1` package.
- `ink_stroke_modeler.props` sets `VcpkgConfiguration=Release` before the
  vcpkg targets normalize library selection, including demo Debug builds.
  Standard vcpkg MSBuild autolinking supplies the package libraries.
- The original `ink_stroke_modeler_merge.lib` files remain unchanged as separate
  baseline and rollback inputs. The experimentally built `.0` custom Abseil
  archives are also not linked. Current consumers use vcpkg Abseil and this
  source implementation; do not link multiple Modeler/Abseil providers.

Draw3 product sources are under Inkeys/Inkeys/Drawing/Draw3. The standalone
demo entry point and performance HUD are not registered in the Inkeys product.
Behavior and performance acceptance use separate old/new no-window programs;
this source migration does not fix the curved pen-up tail.

## Local terminal-tail extension (stage 2, opt-in)

`terminal_tail_reconstructor.h/.cc` is an Inkeys extension, not an upstream
Google change. The shared build adds one ordinary TU while the original 13
runtime sources and 18 headers remain byte-exact. Its geometry API is never
called implicitly by StrokeModeler, Draw3, or any old prediction entry point.
`local-extension-manifest.json` separately records these local files; the
original runtime provenance manifest is unchanged.

The caller supplies accepted reference geometry, an ordered raw window,
actual Up, a half-open caller source range, and a baseline token. The original
first two positions define the fixed discrete entry direction; a repeated
entry is rejected. Point count and caller attribute-source interpolation are
preserved. A separate monotonic raw-segment/fraction map records geometric
correspondence. No velocities, acceleration, pressure or display-time data
are generated or fed back. Physical times stay in caller/report metadata;
matching deliberately uses geometry and actual arc length instead of time.

The main candidate q=m+d solves a tridiagonal displacement regularization
with actual nonuniform arc quadrature. Raw projection stays in a forward-only
arc band from a direction-compatible bounded entry match to Up; equal-distance
separate branches are rejected. Only displacement changes are penalized,
not q curvature. The artificial terminal-pin edge is a weak regularization
constraint and the last point remains a hard exact endpoint.

The control distributes endpoint correction using smoothstep over actual
arc length. If the reference already ends in an artificial Up pin, its driver
is the last reliable point-to-Up connector, distributed over the reliable
tail while keeping the existing final pin exact. Including the connector in
the normalization prevents forcing the penultimate point onto Up. This is a
meaningful endpoint-only control, not a claim that it solves branch matching.

Scratch is caller-owned and reusable. Returned patch spans expire at the next
call or scratch mutation. Every NoChange/Rejected has an empty patch; publishing
occurs only after finite, displacement, length, local-turn, source-order and
new-intersection checks. Full-window turn is also checked at a fixed physical
arc spacing (experimental default 0.5 input units), with every sample charged
to the validation budget. Ratios are checked before integer conversion to reject
excessive sampling counts. New adjacent reverse overlaps are rejected separately.
Real raw corners are checked after skipping repeated
positions, with raw indices preserved. Short/ambiguous/budget-limited windows
conservatively reject. Entry and Up are hard constraints, not large weights.

Defaults (distance budget 3 input units, lambda 0.03, 8 iterations and arc bands)
are experimental offline settings, not production pixel thresholds or a device
recommendation. The header exposes the relevant distance/angle/ratio/work limits;
point caps are at most 4096, iteration cap 64, and individual work caps at most
16777216. Caller units must be consistent. Actual forward-model and negative
fixtures decide whether these settings pass; compiling this extension alone
does not fix product pen-up geometry or authorize product integration.
## Local completed-tail fit (stage 3)

`ReconstructLocalTerminalTail(request, scratch, local_config)` is a separate
geometry-only API. It keeps the old stage-2 entry points unchanged. A bounded
final cubic is fitted to the accepted Modeler tail, with fixed entry position,
entry direction and exact Up. The preceding reference points, full point count
and every attribute source are preserved. The artificial pin edge is excluded
from the model shape prior and terminal-direction estimate.

Raw input is optional: zero, one or sparse points do not prevent model-assisted
smoothing. Distinct latter-half raw nodes (at most six by default) optionally
assist ordered positions and terminal direction. Physical-time trimming is the
caller's responsibility. With fewer than two raw points, `raw_match` is the
default value and carries no correspondence evidence. Genuine raw corners
are considered only where the selected evidence enters the repair segment.

The local length/probe defaults are experiment values in input distance units;
callers must scale them consistently. Validation retains finite/config/source,
work and point budgets, displacement, length, discrete and physical turn checks,
new intersections and reverse-overlap guards. Failure publishes an empty patch.
Draw3 completion is an explicit caller; live StrokeModeler state remains unchanged.
