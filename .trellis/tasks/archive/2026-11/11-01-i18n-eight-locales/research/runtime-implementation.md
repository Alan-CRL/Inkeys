# Runtime implementation handoff

## Scope and ownership

Implemented C++/headers, RC, project registration, Solution items and associated C++ tests only. The main session owns font binaries/licenses and final JSONC sync; translation agents own JSONC. No generated header, snapshot, translation script, binary asset, Git commit or GUI was modified/run by this worker.

## Implemented behavior

- New `Inkeys/IdtI18nLocales.h` is the supported-locale source for stable saved indices, exact LANGID, JSON resource, font resource/family, native name and Beta label. Indices remain `0=en-US,1=zh-CN,2=zh-TW`; new values `3=zh-HK,4=ja-JP,5=ko-KR,6=fr-FR,7=de-DE,8=es-ES,9=pt-BR,10=ru-RU`.
- First-install default matches exact supported regions and preserves the previous Singapore/Macau Chinese and unknown-region English fallback. `ReadSetting` still overrides the initial default, so existing saved preferences remain authoritative. Invalid persisted indices are normalized in memory as before.
- RC resources `310..317` map eight new JSONC files; `318..321` map Noto JP/KR Regular/Bold. Resource header, vcxproj/filter and Solution items are registered. Unknown embedded locale now fails instead of loading Chinese while labeling it another language.
- Setting language selector uses the metadata's native name/code/Beta labels in all current languages. CN alone omits Beta. Existing save/load/restart-confirmation flow remains.
- DWrite custom collection registers all four Noto resources; Bar typography uses a single selected locale snapshot. PageControl and Whiteboard share the Bar surface path.
- Both legacy and Design ImGui faces use the selected primary face and same-weight SC/TC/JP/KR fallback sources. Selected-language glyphs win; fallbacks only fill gaps so the mixed native-name menu and pre-restart text remain readable. Noto uses an unscaled config instead of Harmony's optical correction.
- MessageBox defaults now include Japanese, Korean, French, German, Spanish, Portuguese and Russian; HK follows traditional Chinese. Its separate GDI+ path loads Noto JP/KR into a per-dialog private collection, with system-font fallback for early failures/independent hosts. Private collection lifetime outlives FontFamily references.
- Eight active hardcoded Setting strings now reference new generated keys coordinated with the main session: Experimental.N, Sponsor.Thanks, DebugSoftware.RenderingSection/DirtyRegions.N/E/FrameRate.N/E/ConsoleSection. Internal `DRAW3_RTS_DIAGNOSTICS` development strings are unchanged.

## Verification entrypoints prepared

1. `Build/ARM64/Debug/Inkeys.exe --i18n-locales-test`
   - returns before config, mutex or product UI initialization;
   - uses real embedded JSONC and production loader for all 11 locales;
   - validates stable legacy indices, exact first-install mapping, fallback regions, LANGID, native label/Beta, unfinished markers, and actual typed `std::vformat` in narrow/wide paths;
   - also checks the legacy Recall printf-label percent/fixed-buffer boundary without invoking potentially invalid printf;
   - unknown embedded locale must fail. Exit 0 means all checks passed.
2. `Build/ARM64/Debug/Inkeys.exe --bar-eraser-offscreen-test`
   - existing no-HWND entry, now registers 8 font sources and iterates Locales;
   - checks all resource strings through real DWrite glyph runs for missing glyphs;
   - validates fixed label/short label/menu/popup bounds in existing slot contracts;
   - short Select/Clear and Click-through labels accept the existing 0/1 newline resource rule for every locale;
   - logs context in `Build/eraser-b/offscreen-results.log` (`[I18nLayout]`/`[I18nGlyph]`).
3. `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window`
   - Setting test font sources now include static Noto Regular/Bold and product-equivalent fallback merges;
   - real consumer matrix iterates 11 locales at the existing `1.0/1.5/1.875` combined scale cases, including actual Sponsor.Thanks, language-selector width reserve and regular/bold glyph availability;
   - legacy three-language approved marketing/optical snapshots stay scoped to those three languages;
   - MessageBox tests use actual embedded Noto private resources and test 11 locales at minimum width and 96/144/192 DPI.

## Checks run by this worker

- `git diff --check --ignore-submodules`: passed after runtime edits.
- Byte/EOL inspection confirmed edited existing sources are UTF-8 with their original BOM presence and CRLF; edit helper preserves those properties. New locale header is UTF-8 without BOM, CRLF.
- Non-GUI `System.Drawing.Text.PrivateFontCollection` tests loaded both local Noto families via AddFontFile and separately AddMemoryFont. Both returned exact family names `Noto Sans JP` / `Noto Sans KR`, with Regular and Bold styles available. These are asset/backend probes, not the compiled product resource test.
- No solution build or C++ executable test was run here, to avoid competing builds and incomplete RC inputs while translations are in progress. The main session will build once all 11 JSONC files and generated keys are ready.

## Follow-ups and limits

- Main session must generate the eight new keys before compilation and finish all JSONC translations; this worker did not create placeholder language files.
- Do not claim DWrite/ImGui layout or executable verification passed until the prepared compiled tests run. New long translations may require precise short labels or local layout fixes based on those results.
- Global ComboRow 320 DIP cap is unchanged. The true-font mixed selector test checks whether native name/code/Beta plus arrow reserve actually fits; adjust only the language selector if evidence requires it.
- No platform support, actual desktop GUI, touch, or legacy OS verification is implied by the asset probes or source edits.
