# Early contract review (2026-11-02)

Read-only preflight against baseline/current implementation; no product edits or build/test runs. This is not final diff approval.

## Verified findings requiring implementation attention

1. **Queued dialog language can split across versions.** `Setting.cpp:1055-1084` reads title/buttons and `I18n::languageId()` when the business worker dequeues Information/ConfirmRestart, but `command.text` was already translated before `QueueInformation`/`QueueConfirmRestart` (`:1313-1321`). A locale switch while queued can yield an old-language body with new-language buttons/font. MessageBox's existing immutable request does not prevent this upstream race. Snapshot coherently at request creation or queue translation keys/arguments and translate coherently at execution. Whether already-queued messages count as existing requests should preserve coherent text, never mixed generations. Sent to live_locale; coordinate Setting edits with font owner.

2. **A staged atlas is not installed by assigning io.Fonts alone.** `imgui.cpp:8991-9011` maintains g.FontAtlases, RefCount, DrawListSharedData; `:8894-8930` drives NewFrame ownership/GC and PlatformIO texture queue from registered atlases. A self-managed atlas must receive `ImFontAtlasUpdateNewFrame`, or be assigned correct OwnerContext consistently with ownership/deletion. Test failed staging and swap using real context/backend, not only Build return.

3. **Retired atlas GPU cleanup and prior texture queue need explicit ordering.** `imgui_draw.cpp:2658-2664` destructor deletes texture metadata; `imgui_impl_dx11.cpp:665-669` shutdown/invalidation traverses existing PlatformIO.Textures and only destroys RefCount==1 textures. Unregistering/deleting first may leave stale queue entries, while detached candidate textures may never enter this queue. Destroy backend resources while metadata remains valid, then remove queue references and atlas ownership. Prior frame DrawData must be retired. Sent to dynamic_fonts.

4. **CompactCache is not safe on an uninitialized candidate.** `imgui_draw.cpp:4243-4257` dereferences Builder and TexData and discards unused bakes before repacking. Call only after initialization and at safe frame boundaries; do not rely on it as a cheap per-frame GC. Memory snapshots should include all TexList entries (old+new) rather than only TexData because the former persists until backend destruction completes (`:2798-2847`).

## Useful existing contracts (avoid unnecessary rewrites)

- `Bar.Format.cppm:56` keys DWrite format cache by font family + locale, and `:96+` Clean removes unused entries. Correct renderer locale refresh is needed, but existing format-key isolation should be retained.
- MessageBox request.language already controls font rebuild, so fixing queued request construction is more relevant than rebuilding its lower-level localization mechanism.
- Source RasterizerDensity participates in multiplication even with RendererHasTextures (`imgui_draw.cpp:4725-4727`); it must be measured rather than dismissed due to header LEGACY comment.

## Verification

- Read source and complete hook-file context (tool output remained display-truncated).
- No lint/typecheck/build/tests run: parent explicitly assigned early read-only review and font owner serializes builds.
- No product files changed. Final review pending completed implementation and evidence.

## Runtime diff review and test additions

Reviewed IdtI18n prepare/commit/capture, renderer revision consumption, Bar in-place labels, PageControl configuration gate, MessageBox callsites and current Setting staged-atlas implementation.

### Changes made by reviewer
- IdtMain.cpp: registered `--setting-font-lifecycle-test` before product bootstrap, Debug harness gated, calls `Setting::RunFontLifecycleProductionTest()`; Release returns2. No GUI entry is taken.
- PageControl.cpp RunOffscreenTests: added 11-language real ConfigureSurface regression for all four production SurfaceState objects at an unchanged business revision. Checks consumed locale revision, renderer font family, visual invalidation, and that input presentation generation does not advance before presentation. Runs under existing `--bar-eraser-offscreen-test` through its PageControl test dispatch; no separate PageControl CLI exists.
- Preserved original UTF8 BOM presence and CRLF in both edited files. `git diff --check --ignore-submodules` passed.

### Verified implementation properties
- I18n prepares outside publication, checks base revision while exclusively locked, then swaps language/map before release-publishing revision. capture copies text and LANGID under one read lock; failure retains prior output.
- Bar language refresh runs on render owner and updates persistent labels in place. Its normal main projection path (`Bar.RenderLoop.cpp:1677`) still updates dynamic Draw/Eraser/Geometry/Selection/Whiteboard labels after styleKey invalidation; no second projection added.
- PageControl configuration now includes localeRevision, and scene ConfigurePagination refreshes that scene's own renderer. Correctly avoids relying only on changed business revision.
- MessageBox upstream nonSetting consumers capture consistent text/language; existing MessageBox request snapshot remains intact.
- Setting candidate atlas now has owner+registration; failure restores prior roles/default and current font before disposing candidate. Disposal explicitly destroys backend textures, removes PlatformIO references, unregisters, then deletes metadata. Previous early findings2/3 are addressed in inspected code.

### Still pending / not claimed
- Staged upload failure and postcommit lazy first-glyph upload failure/retry are not covered by the initial new main harness (only 11×3 success cycles and stale commit rollback). Sent to font_lifecycle; existing headless backend failure seam may be reused but production assertions must be meaningful. Existing production path declines Present after backend failure; this source fact alone is not runtime evidence.
- All runtime compilation and tests are serialized by dynamic_fonts. Reviewer did not run them. Await actual exit codes before final approval.
- Entire task memory/visual/latency budgets are owned by font benchmark and remain outside this source-review pass; no total-memory success claim.

## Final lifecycle/runtime source pass (before final benchmark acceptance)

Two concrete integration defects fixed in Setting.cpp:
1. Successful language publication had no Bar/PageControl wake request. Since revision publication alone does not schedule idle render clients, already-visible surfaces could retain old language until unrelated activity. Added existing RenderPipeline::Request(Bar) and PageControl::NotifyLayoutChanged after successful commit, before persistence; no new scheduler or thread.
2. QueueWorkspaceTransitionFailureNotice is called from IdtState's business failure path, not exclusively the render thread. Its body was fetched separately from QueueBusiness's title/button/font snapshot. Added one capture for body+title+buttons+LANGID and taught QueueBusiness to preserve already-complete snapshots. English fallback is internally consistent. Ordinary render-frame requests continue freezing their common labels at enqueue.

Other checked lifecycle details:
- ReplaceSettingFonts restores role pointers, FontDefault, ImFluent role binding, DPI scale and current ImGui font on staged failure; uses registered/owned atlas and explicit texture cleanup.
- Font serial increment/reset of page+submission cache covers language changes even when reusing the same font family.
- Main render loop still checks actual backend HRESULT before Present; a failed texture upload cannot submit the partially rendered discard buffer. This is source-path evidence, not real HWND/Present runtime testing.
- New harness now injects actual candidate Texture E_OUTOFMEMORY and checks old atlas/revision retained; then forces current backend texture rebuild failure and retry without republishing locale. It does not call Present or prove actual screen presentation. It tests CreateTexture recreation; it is not coverage of every possible lazy WantUpdates failure.
- ImFluent's font binding has no extra hidden Design pointers: Setting.Shell.cpp BindTextFonts delegates to the same ImFluent role table; explicit saved role rebinding covers it.

Reviewer edits remain no-BOM UTF8/CRLF for Setting.cpp (same original format). Diff whitespace check passes. Source-ready notification sent to build owner; compilation/tests and font appearance/resource/performance evidence still awaited. No whole-task acceptance asserted.

## Alpha8 upload review, first source pass

Current implementation keeps GPU RGBA32 and expands Alpha8 on create and per dirty rectangle. Create assumes tightly packed ImTextureData Alpha8 (valid); update uses GetPixelsAt per source row and contiguous r.w*4 destination pitch (correct). Original RGBA32 path unchanged. Texture/SRV failure keeps WantCreate and cleans partial native resources; dirty upload retains WantUpdates when device-removed check fails.

Two concrete caveats sent to owner:
- Temporary ImVector<ImU32>::resize does not give backend HRESULT failure handling for allocation failure. New potentially fullRGBA scratch allocation should use fallible allocation with E_OUTOFMEMORY and unchanged texture request, then release scratch after upload. Scratch peak must be reported.
- ImTextureData::Create zeroes all RGBA bytes, i.e. unused padding starts transparent black. Expanding Alpha8 to white RGB makes these transparent white. Bilinear samples crossing padding at fractional positions/scales can differ; actual oldRGBA/newAlpha pixel equivalence must include font edges, AA lines/white UV and nontext. It cannot be proven merely by identical alpha values.

No backend edits by reviewer (owner active). FT_Face-sharing patch not yet present at this review point; follow-up required. No new builds/tests run by reviewer.

## FT face-sharing + revised Alpha8 source pass

Within the current production default-FreeType atlas, the sharing patch is source-safe pending runtime evidence:
- Matches same atlas source by FontData pointer, byte size and FontNo; non-default loaders (stb icons) excluded.
- FT_Reference_Face is balanced by existing CloseFont/FT_Done_Face, including InitFont failure cleanup.
- Each source/bake retains separate FT_Size and flags. Unconditional FT_Activate_Size precedes LoadGlyph, including metrics-only calls, preventing another source from leaving the shared face at the wrong size.
- ContainsGlyph only queries the fixed Unicode charmap; no size activation required. Source GlyphOffset/ExtraSizeScale stay separate.
- FreeType library/face allocations continue through the same library allocator hooks, so shared faces are counted once and distinct source/bake allocations remain counted.

Revised AlphaExpansion now allocates fallibly via IM_ALLOC, returns E_OUTOFMEMORY without consuming the texture request, and frees conversion scratch after the operation. The new TextureConversion failure seam is present. No corresponding Alpha8/TextureConversion tests were found in HeadlessTests at this inspection; runtime evidence still must cover create + nonzero-offset/disjoint dirty rectangles + failure/retry + RGBA comparison. Transparent-padding RGB caveat remains a validation requirement, not a presumed visible regression.

Read-only pass; no loader/backend edits or builds. Reported source-safe does not imply the full memory/visual/performance acceptance gates have passed.

## Alpha8 tests implemented

Added CheckAlpha8Uploads to existing setting_home_warp_tests.cpp and invoked it from RunSettingHomeWarpProbe; no new project registration.

- Actual GPU texture readback checks nonuniform11×9 Alpha8 create expansion, then two disjoint dirty rectangles at nonzero offsets; compares all pixels including unchanged area.
- TextureConversion E_OUTOFMEMORY injected before create and dirty-update conversions; verifies request status/resources/oldGPU pixels remain valid and same request succeeds on retry.
- Real DX11 rendered fixture uses original RGBA font atlas (asserted) and Alpha8 clone, identical actual DrawData with fractional font vertices. AAline, solid whiteUV primitive and colouredRGBA image must remain pixel-identical; text pixel difference count is reported, not assumed zero. Emits [Alpha8Pixels] with sourceformat and text/nontext counts.
- Production backend+embeddedCSO/WARP, noHWND; original BOM/CRLF preserved, diff-check passed. Build/test owner notified; execution results pending.

## Lazy merged FT_Size review + Alpha fixture isolation

Lazy-size patch keeps eager FT face/charmap validation and primary metrics initialization. Merged sources allocate only after a real cmap match; each existing/new FT_Size is activated before advance or bitmap loading. FT_New_Size / Activate / Request errors return failure, partial sizes are released and reset, and destructor tolerates an unused merged source. No new source-level blocker confirmed; actual fallback/role alternation and budget tests remain necessary. Returning false to ImGui on allocation failure is not evidence that every subsequent glyph request retries (ImGui may cache missing glyph results); do not overclaim general OOM recovery from the helper alone.

Build owner observed isolatedWarp C0000005 (not a normal assertion failure) while basic lifecycle and ordinary setting tests passed. Before rerun, Alpha pixel test now uses a separate ImGui context, independent RGBA atlas rebuilt from real source configs and a separate Offscreen backend, restoring caller context afterwards; never changes the caller's production Alpha8 atlas format. Added unbuffered phase markers to identify whether crash occurs in cold-entry, backend-failure, rawAlpha or independentRGBA stages. New source ready; rerun pending owner build.

## Lazy-face and failure-latch review

No confirmed source blocker in the latest lazy-face patch. 136 ImU32 words cover Unicode0..10FFFF in conservative256-codepoint blocks. Empty blocks are a true-negative optimization; occupied blocks still call exact FT cmap lookup. Initial face/charmap validation remains eager. Duplicate coverage/face sharing is confined to identical buffer/size/face index. FT face reference ownership, source flags and per-role FT_Size activation remain separate where required.

Late face/charmap/size/glyph-load/render failures latch LoadFailed on the atlas; candidate publication checks this before commit, and production Setting checks after ImGui::Render but before backend/Present, queues font rebuild and retries with a new atlas. This addresses the prior poisoned-missing-glyph-cache concern. The allocatorfault test verifies absent-block lookup does not consume allocation failure, actual Koreanfallback lookup does latchfailure, then a fresh atlas clears the failure. Execution results remain owned by build agent.

Alpha fixture crash/assert root cause confirmed by build owner: ImGui::CreateContext() restores the previous current context when one already exists. The test had cloned sources into that same old atlas, invalidating the source loop. Fixed explicit SetCurrentContext(isolatedContext) immediately after CreateContext and explicit DestroyContext(isolatedContext) before restoring the caller. No assertions were relaxed. Rebuild/rerun pending.

## R8 GPU path source review

Confirmed one blocking callback semantics bug in first R8 draft: every non-reset ImDrawCallback disabled automatic pixel-shader selection, even a no-op or sampler-only callback. R8→no-op→RGBA then incorrectly keeps R8 shader. Sent owner fix direction: inspect PS after callback; retain auto format selection for backend-owned RGBA/R8 shaders and honor genuinely foreign PS until reset. Existing new test used the owned RGBA shader as a purported custom shader, so requested a genuinely distinct shader (existing EraserDemo PS) plus actual mixed-format noop/sampler callback pixel regression. Owner agreed; correction pending serialized build.

Other reviewed R8 paths: texture/SRV formats and full/rect row pitch match R8 or RGBA storage; failed create/SRV never enters intrusive registry; successful registry entry retains native resources and unlink happens before release; raw SRV lookup supports cached DrawData; alpha shader resource322 creation/cleanup is paired and existing previousPS restoration remains intact. Unsupported R8 format path uses fallible RGBA conversion. No changes to main Bar/PPT DirectWrite renderer.

Exact rendered R8-vs-expandedAlphaRGBA comparison is distinct from originalRGBA-vsAlpha padding comparison; both tests are required. Build/tests are owner-run; source review alone does not prove pixels or performance.

## Final frozen-profile source review

The earlier experimental lazy-face/coverage/probe-cache implementation is removed. Current supported profile is eager validated/shared FT_Face, lazy merged FT_Size, sticky size/glyph/rasterization failure, LightHinting, and explicit Setting-only SnapAscentToPixelGrid. Generic loader default remains unrounded; paired-glyph validation is separate from resource measurement. Production source/atlas flags and provenance documentation match this profile.

Confirmed callback fix now classifies the actual postcallback pixel shader: backend-owned RGBA/R8 retain automatic selection, truly foreign shaders retain ownership until reset. PSGetShader references are released. Earlier callback finding is resolved in source; independent owner reports actual pixel tests passed. R8/Alpha resource identity uses GetTexRef, not the CPU object's address as a raw SRV. Shader state restoration and partial-resource failure cleanup remain paired.

Hot-switch flow remains Setting-request → prepare/fontcandidate → commit → persist plus explicit Bar/PageControl wake; same-family reuse still increments font/cache version, cross-family swap retires old atlas safely. Bar and pagination preserve DirectWrite, MessageBox preserves GDI+ and request-language snapshots. No additional confirmed product blocker found in this final source pass.

Measurement scope: requested live ImGui allocations (including CPU atlas), separately metered FT allocations, and actual D3D texture descriptor bytes; shared immutable font source is separately recorded and not duplicated. These figures are not exact total-process/driver memory, and WARP GPU resources are system memory, not dedicated VRAM. Actual-page measurement reads already generated glyph/bake state rather than triggering new glyphs. Final text/page/performance matrices and complete no-window run remain pending owner logs at review time; no blanket validation pass asserted.

One minor spec correction sent root: real PageControl ConfigureSurface regression is dispatched through --bar-eraser-offscreen-test, while --ui3-multitarget-benchmark covers real Bar RenderFrame. Keep the extra Bar culled=0 gate distinct from required hot-language assertions; probe ON/OFF timing does not establish historical baseline behavior and does not authorize unrelated DirectWrite culling changes.

No product changes in this pass. git diff --check --ignore-submodules passed. Final acceptance still requires user visual review and accurate reporting of remaining runtime/platform evidence.

## Final targeted delta review and handoff

Reviewed the final sponsor layout correction, FT advance assertion and explicit page-matrix routing. No additional functional blocker found.

- RenderSponsorThanks reserves measured glyph overhang plus one physical pixel before wrapping, then retains existing ink-centering/line-height/vertical registration. This is a narrowly scoped response to FT ink extending past advance bounds, not font-size or style redesign.
- FT full-width advance tolerance is limited to half a physical raster pixel plus26.6 request precision; the independent stb criterion and frozen negative controls remain unchanged.
- Heavy A/B page validation is explicitly routed through --setting-font-pages-only; ordinary --no-window keeps functional FT coverage. Matrix acceptance therefore relies on its separately executed evidence, not on the ordinary suite alone.
- Adaptive page stabilization requires minimum24 warm frames and8 identical post-submit resource/layout samples, completed real popup visitation and ready textures; critical English100% andJapanese200% retain120-frame comparisons.
- Notified root of currently visible added BOMs in Setting.Pages.cpp and setting_design_tests.cpp after late edits; root owns final byte-format restoration. No further code writes by reviewer.

Reported final runtime evidence from root/build owners (reviewer did not rerun concurrently):720 page rows /360 candidate comparisons all under same-scene memory budget, zero missing metric glyphs; ordinary full --no-window exit0 in342.39s after sponsor fix; R8 sameAlpha/RGBA whole-pixel comparison and complete WARP probe exit0 (~107s); lifecycle pass. Warm ABBA English100%/Chinese200% regression did not reproduce, detailed final owner report pending. These results supersede earlier failed experimental candidates, but do not establish native GUI appearance or untested platform support.

Extra Bar benchmark culled=0 remains a separately disclosed non-passing gate, with no historical-baseline proof; no unrelated culling algorithm change was made. Final user visual acceptance remains pending. Root will consolidate authoritative logs, restore generated unrelated Draw3 CSOs and final format drift, and decide task handoff. Source review is ready; no remaining confirmed product blocker identified in the frozen implementation.
