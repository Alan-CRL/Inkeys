# Font baseline and FreeType integration progress

- Frozen product baseline: cf4d9120; original Typography bytes preserved in stb-baseline-typography.h.txt.
- Before any loader/source-density product change, existing --no-window --setting-general-only passed (exit0,16.86s); actual WARP font comparison100/150/200% files copied to Build/setting-fonts/stb-baseline/.
- Official ImGui v1.92.7 FreeType loader sources introduced unchanged, with pinned provenance and MIT license. FreeType resolved at existing vcpkg baseline to2.14.3 with default optional features disabled, using existing static-v143 triplets.
- Sandbox full solution attempt exited1 after301s without meaningful compiler diagnostic. Repeating same solution elevated enabled the existing dependency install; this was not a source workaround.
- First elevated solution built/linked Inkeys.exe successfully, but HeadlessTests started before shared manifest installation and reported ft2build.h missing. Tests had VcpkgEnabled=false and no solution build dependency. A minimal HeadlessTests->Inkeys solution dependency now serializes that prerequisite on a clean checkout; no SDK/toolchain/other version changes.
- Font candidate remains opt-in helper for A/B until measurements: atlas-level FreeType loader, LightHinting, source density1, existing role normalization. Per-source FT cannot be used because1.92.7 rejects source loaders with LoaderInit/Shutdown; icons can retain per-source stb as needed.
- Runtime memory is NOT claimed from atlas byte counts alone. Dedicated benchmark measures ImGui allocation totals, FreeType allocations, all atlas CPU buffers as a subset, and actual live D3D font texture bytes, without duplicate accounting. Existing resource font bytes count once per source.

## Measured iterations (not final acceptance)

- Initial FT RGBA full text fixture:415.02s, exit22. All22 budget failures at100% (11locales, two themes); no allocator leak reports. Archived under Build/setting-fonts/benchmark-ft-rgba/.
- CPUAlpha8, unchangedRGBA GPU: focuseden-US6scales×2themes23.03s exit2. English100 old1,724,566B ->1,839,741B, still115,175B over. Snapshot benchmark-alpha8-only/.
- Same-atlas FT_Face sharing: focuseden-US24.30s exit0; English1001,641,009B vs1,724,566B baseline. All12 candidatePNG hashes identical to pre-sharing Alpha8 images. This is textfixture evidence only.
- Realpage fixture initially omitted Legacy roles; corrected to exact4textroles+legacyiconmerges+actualtitle/version drawing. ExactEnglish1003pages×2themes43.12s exit1: language3,193,855B ->3,292,807B (+98,952), general+99,018,home+99,702. Therefore product memory acceptance not achieved despite textfixture pass.
- Lazy merged FT_Size reduced exactpage overhead only~7.7KB:40.59s exit1, language+91,208B, general+91,274B, home+90,902B. No budget relaxation. Lazyface extension under investigation by font_lifecycle agent.
- Real production font lifecycle33switches/stale preparation/uploadretry test passed before and after lazy-size (latest1.11s). RegularFT general controls suite passed15.70s and16.63s.
- New isolatedWarp probe crashed C0000005 in10.15s, while samebinary ordinaryFT/general and production lifecycle passed. Reviewer replacing liveformat-rebuild pixel comparison with independentRGBA context; this remains unverified until next build/run.
- i18n resource/transaction tests passed0.65s. Bar test failed10 immediate separate-mode label checks; runtimeowner fixed pending content fade semantics, requires rerun. Multitarget longrun stopped after known culling/timing failures; incomplete, no baseline claim yet. Bounded samebinary probe-on/off A/B pending.

## Format and resource details

CPUAlpha8 upload expands to existingRGBA32 textures, including dirtyrects; expansion allocations use fallible ImGui allocator and release immediately, tracked by allocatorpeak. Temporary allocation failure leaves texture status pending. New Debug-only TextureConversion injection exercises retry. Shader/sampler unchanged. Pixel-equivalence tests pending.

Harmony SC/TC files contain COLR/CPAL/SVG tables (see font-color-table-audit.json); current loaderflags intentionally do not enable LoadColor and retain existing grayscale text behavior. ConfigureFreeTypeAtlas selectsRGBA when atlas LoadColor is explicitly enabled. Do not describe these fontfiles as inherently monochrome.

New dependency notices are copied verbatim to additional/imgui/misc/freetype/LICENSE.freetype.txt and rootNOTICE; no unverified distribution-pipeline assertion remains in UPSTREAM.md.

## R8 candidate and independent gates

- R8 full solution build passed80.67s; callback/no-op refinement build passed12.09s. Product font lifecycle passed1.15s.
- Exact English100% real4roles/titlebar, languagepopup open/scrolltop/bottom/close, basic/home/returnlanguage passed62.20s with currentlazy metadata+R8. Language old3,222,230B ->3,107,701B; general3,540,110B ->3,425,647B; home/return4,532,653B ->4,417,818B. CPU/GPU font atlas each65,536B vs262,144B each inbaseline. This is focused acceptance only, not all-language matrix.
- Bounded mainbar samebinary probeOFF/ON600frames both exit1 exclusivelyculling gate (0culled), timingspassed:13.429ms/12.803ms p95. This excludes addedlocale probe as cause, not proof originalcommitbaseline sharesfailure. Full failingmatrixterminated intentionally, notpassed.
- Alpha pixel fixture remains underdiagnosis; rawGPUcreate/dirtyupload/OOMretry reached. Temporary process-only exceptionhandler writes Build/setting-fonts/warp-crash.dmp. OriginalfaultatD3D11 PSSetShaderResources: minidump appbase0x7FF634670000, ARM64PACreturn0x5703fff63527e39c normalizedlow47bits→RVA0xC0E39C. OfflineDbgHelp(PDBmatchedcurrentbinary) resolved ImGui_ImplDX11_RenderDrawData+4172, imgui_impl_dx11.cpp:424. Investigatinginvalid/stale/crosscontextSRV; no claim ofdriverbug orproductfailure. Temporarydiagnosticswillberemovedafterfix.
