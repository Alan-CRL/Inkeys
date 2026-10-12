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
  adjustment is preserved. No algorithm or public API changes are made here.
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
