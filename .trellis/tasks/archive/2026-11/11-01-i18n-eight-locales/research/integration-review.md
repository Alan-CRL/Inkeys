# Integration review

Reviewer ownership: C++/headers, RC/project registration and associated tests. JSONC,
font binaries, generated keys/snapshot and the Markdown change report remain with
their designated owners. No GUI, Computer Use, commit or push was performed.

## Findings fixed

- `Bar.EraserAttribute.Test.cpp`: the new glyph probe referenced an unimported
  `utf8ToUtf16` helper. It now reads the exact map key through `I18n::getW`.
- `IdtI18n.cpp`: the downloading-format test used an unrelated string sample.
  It now uses `IdtAtomic<int>{42}`, matching `Net.Update::downloadLine` and its
  production formatter.
- The active component registry, component settings page and DDB details page
  still contained Chinese captions. The main session confirmed these are in R4.
  The complete deduplicated inventory is 37 component keys, 38 DDB keys and one
  ordinary diagnostic-page heading, all coordinated before the next schema sync.
  Consumers now use generated keys; component IDs, callbacks and configuration
  branches remain unchanged.
- DDB row arrays retain stable key pointers, then own the translated strings for
  each render call. They do not store pointers from temporary `IA(...).c_str()`.
- Offscreen failures now log locale, stage, key, actual text, size, measured bounds
  and slot limits. The transition fixture uses the current locale's Select label.
  Existing fit limits have not been relaxed.
- Offscreen coverage additionally cold-registers all 16 real components per
  locale and checks their labels, category/settings metadata and toolbar slots.
  Headless ImGui coverage renders the actual component/DDB copy through shared
  `ToggleRow`, `Notice` and `ComboRow` consumers at narrow/wide widths.
- Real DWrite bounds identified small remaining overflows after compact copy.
  Only `DrawBarButtonVisual` primary labels enable the renderer's fit option;
  ordinary Word content keeps the prior default. The bounded 64-entry cache
  uses final geometry and font target; the same fit ratio feeds both the drawn
  size and the existing integer-pixel/animation correction target. Twelve-DIP
  single-line labels retain at least 9.5 DIP, and existing nine-DIP multiline
  labels retain at least 8.5 DIP. Slot geometry and hit areas are unchanged.
- The Pierce transition fixture previously fabricated a Select-to-Pierce text
  swap that the product never performs. It now checks actual Pierce content
  refresh idempotency; Select still verifies its real Select/Clear transition.
  Both retain the original slot bounds checks with production fit sizing.
- Sponsor thanks now centers each line's visible glyph bounds, not advance
  widths containing invisible whitespace/bearings. The sponsor function alone
  reserves the actual rounded ink top/bottom; shared homepage marketing layout
  stays unchanged. Existing centering and height assertions remain intact, with
  additional locale/scale/width/ink diagnostics on failure.
- The new locale test definition/declaration follows
  `INKEYS_ENABLE_UI3_PRESENTATION_HARNESS`; its CLI explicitly returns 2 when the
  harness is disabled, preventing a validation flag from launching product UI.

## Verified boundaries

- Existing persisted indices 0/1/2 retain their identities; new values are appended.
  Startup defaults are selected before the unchanged `ReadSetting` override.
- JSON resource IDs and names match the locale table. New values 310..321 have no
  duplicate IDs. The old `IDR_JSON1` / `IDR_ZH_TW` value-275 alias predates this task.
- Native names, locale codes and Beta status are centralized; zh-CN alone omits Beta.
- DWrite registration, selected family/locale and the ImGui fallback chain cover
  the new assets. The GDI+ dialog path independently loads private JP/KR fonts;
  each private collection outlives its FontFamily references.
- Snapshot/format generation remains owned by `Scripts/i18n.ps1`; generated header
  edits are not made by this reviewer.

## Remaining validation / scope notes

- The main session owns final Solution compilation and no-window/offscreen tests.
  The first compile failure above was a test-source error, not an SDK/environment
  failure. Final build 5 passed after the fixes (exit 0, 10.15 seconds).
- The pre-fit offscreen pass found real fixed-slot and multiline failures. Short
  resource changes were owned by the main session. The final 679-key offscreen
  pass returned exit 0 in 29.37 seconds with no `I18nBounds`/`I18nGlyph` failure;
  the overall eraser visual report records zero failures. No layout success is
  inferred from character coverage alone.
- Excluded Chinese literals are internal ImGui IDs, logs/raw copyable technical
  diagnostics, `DRAW3_RTS_DIAGNOSTICS` developer-only content, and startup failure
  fallback messages. The ordinary EDID section heading is localized separately.
  `FreezePPT`'s old loading hint is disabled by `kPptLoadingPageEnabled=false`.
  PageControl's old direction-policy text is not consumed by the product renderer.
- The configuration/i18n spec still describes a three-language target list and
  should be updated by the main session with the supported-locale registry,
  mixed-font selector and real-format verification contracts.

## Verification to date

- Diff whitespace checks: pass.
- Main vcxproj and filter XML parsing: pass.
- Original BOM and CRLF preservation for reviewer edits: pass.
- Type checking / compilation: final Debug|ARM64 Solution build 5 passed (exit 0).
- Actual embedded locale loading and typed formatting: all eleven locales passed,
  exit 0 in 0.85 seconds, `[I18n] failures=0`.
- DWrite/offscreen toolbar and component layout: pass, exit 0, zero failures.
- Full `--no-window`: still running at this update; stderr empty through the
  HomeWARP stage. Its final exit code must be confirmed by the main session.

## Final integration results (main session)

- Debug|ARM64 full Solution build 5: exit 0, 10.15 s.
- Embedded resources/typed format: exit 0, 0.85 s.
- DWrite/actual component offscreen bounds: exit 0, 29.37 s; EraserVisual failures=0.
- Complete InkeysHeadlessTests --no-window: exit 0, 338.72 s; stderr empty.
- No GUI/Computer Use, commit, push or task archive.
