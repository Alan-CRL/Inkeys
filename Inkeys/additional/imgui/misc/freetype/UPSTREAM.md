# ImGui FreeType loader provenance

- Source: https://github.com/ocornut/imgui/tree/v1.92.7/misc/freetype
- Loader version remains pinned to Dear ImGui 1.92.7. README.md is unmodified upstream; the MIT license is retained as LICENSE.imgui.txt.
- FreeType resolves through the repository's existing vcpkg baseline/static triplets. The measured ARM64 dependency is FreeType 2.14.3 with optional compression/image features disabled. LICENSE.freetype.txt preserves the resolved package notices; NOTICE records the dependency.
- imgui_freetype.cpp and imgui_freetype.h carry the local patches described below. The resource-lifetime patches do not change glyph coverage, face selection, hinting flags, requested metrics, or pixel rendering. The optional ascent-position experiment is described separately below.

## Shared faces within one atlas

Sources with identical FontData pointer, byte length and FontNo share FT_Face through FT_Reference_Face. Each source retains its reference and its own flags. Every source is validated eagerly, including Unicode charmap selection. Sharing does not cross atlas lifetimes. Each used source/bake combination retains its own FT_Size, and every glyph request activates that size because another role may have changed the shared face's active size.

## Lazy merged sizes

FontBakedInit constructs the small per-source slot immediately. Primary source sizes initialize immediately to supply ascent/descent; merged source FT_Size allocation occurs only when an actual glyph/advance query hits that source. Missing glyphs do not allocate sizes. New_Size, Activate_Size and Request_Size failures clean up partially initialized sizes; destruction accepts unused null slots. ContainsGlyph always uses the validated face's exact FT_Get_Char_Index.

## Failure propagation

ImGuiFreeType::HasFontLoadFailure(atlas) reports a sticky failure for FreeType atlases and safely returns false for other loaders. Size allocation/activation/request, glyph load and glyph rasterization errors latch failure instead of allowing a fallback lookup to be treated as a successful frame. There is no clear-in-place API because ImGui may have cached a missing-glyph result. Setting checks candidate atlases before language commit and checks generated frames before DX11 rendering/Present. It schedules a fresh atlas at the existing safe retry boundary after a current-atlas failure.

The realloc bridge returns null while preserving the old allocation when a larger allocation fails. This avoids a null memcpy before FreeType can report the error.

The no-window production lifecycle test allocates a new primary size before arming a failing FT allocator, verifies exact Korean coverage lookup does not allocate, then injects failure at the first merged FT_Size request. The sticky error rejects that atlas; replacing it provides clean recovery. Actual compilation/runtime results belong in task validation records.

## Removed experimental work

A trial deferred fallback faces after eager validation and retained conservative Unicode coverage plus exact initialization-probe caches. Diagnostics confirmed that it eliminated unnecessary TAB/U+FFFD face opens, but legitimate used-face cost still exceeded the memory budget. Once the independently verified Alpha8 CPU/R8 GPU path supplied sufficient savings, the lazy-face/coverage machinery was removed to reduce third-party changes and cold validation cost. No full cmap copies, coverage bitmaps, probe caches, process-global font cache or diagnostic logging remain in this loader.

Oversample remains a stb-specific setting. FreeType is configured at atlas level; source-specific flags remain supported. Per-source FreeType LoaderInit/Shutdown is unsupported by this ImGui version, so the loader is not installed as a per-source override.

## Pixel-grid ascent alignment

The local loader flag `ImGuiFreeTypeLoaderFlags_SnapAscentToPixelGrid` (bit 10) rounds only the primary baked ascent using `round(ascent * baked->RasterizerDensity) / baked->RasterizerDensity`. The baked density converts ImGui coordinates to framebuffer pixels; source supersampling density and ExtraSizeScale are deliberately excluded. Glyph raster size, advances, descent, font files and semantic line boxes are unchanged. Merged sources inherit the primary ascent as before. The generic loader default remains unrounded; Inkeys Setting explicitly enables this flag in its shared typography initializer. No product preference is added.

The Setting choice follows actual CN/JP/KR 100% and187.5% A/B images plus896 paired-glyph checks: identical advances, horizontal bounds, bitmap dimensions and bitmap bytes, with baseline translation at most0.488865 framebuffer pixels. It removes fractional baseline resampling introduced by ExtraSizeScale normalization; it does not promise alignment when a caller later applies an arbitrary fractional origin/transform or scales an already baked size.