# Follow-up UI implementation

Active task: `.trellis/tasks/10-04-pen-width-dip-defaults`; dispatched trellis-implement, no recursive delegation. Loaded saved native hook output plus current PRD/design/implement and followup-scope. Latest scope overrides prior approximate DIP display. No builds, GUI, commits or task-state changes by this worker.

## Production cause and change

- Direct Touch Preview route in Bar.Interaction sets thicknessPreviewDragging after 5 DIP slop, computes ProjectRelativePreviewWidth and invokes ApplyCandidateWidth -> immediate double candidate publication + UpdateRendering(false). Business width commits only on successful revision-protected release.
- Bar.RenderLoop already directly set numeric width and slider normalized position during active candidates. Its independent core/outer preview width animations still used SetTar(operationDur); rendered stroke geometry reads their val. That animation caused the preview to trail the finger although the candidate had arrived.
- Extracted the existing narrow candidate publication into BarInteractionSession::PublishThicknessSliderCandidate; same real mouse/touch route calls it. No per-frame StateMode/config/bridge writes added. Existing hold lock and allowed gate remain.
- GetBarThicknessPreviewMetrics chooses valid current candidate vs committed snapshot and calls shared policy. UpdateBarThicknessPreviewWidth sets the animation val directly during candidate interaction; inactive/programmatic changes retain SetTar. Existing Laser Hold/core/morph/color/shell policies and soft preview geometry are untouched.
- Bar.I18nFormat displays safe half-up positive integer only; sub-1 remains minimum display 1. No DIP suffix or approximation glyph; formatting never writes back. Three language ThicknessFormat source strings and malformed-format fallback match.

## Changed ownership files

Bar.I18nFormat.h, Bar.Layout.cppm, Bar.Interaction.cpp, Bar.RenderLoop.cpp; InkeysHeadlessTests/animation_tests.cpp; Inkeys/src/i18n/{zh-CN,zh-TW,en-US}.jsonc and script-generated Scripts/i18n.zh-CN.snapshot.jsonc. Generated key header content unchanged.

## Verification executed by worker

- `pwsh -NoProfile -File Scripts/i18n.ps1 sync`: regenerated resources; expected exit 1 while two updated strings held Need translation markers. Filled en-US/zh-TW source translations, preserved original Traditional Chinese opening comment overwritten by the generator.
- `pwsh -NoProfile -File Scripts/i18n.ps1 check`: exit 0, both supported alternate languages 332/332 PASS.
- `git diff --check`: exit 0; only main-owned task/spec LF normalization warnings, no whitespace errors.
- Per-file byte check against HEAD: BOM preserved and all nine ownership sources/snapshot are CRLF only; inspected focused git diff, no whole-file formatting changes.

## Tests added, pending main build/runtime

- Headless: integer, fractional classroom, 4.5 -> 5, 4.49 -> 4, sub-1 minimum, max legal fractional, NaN/Infinity/negative fallback, malformed localized format yields integer alone.
- Real early production Bar session probe: soft/hard/highlighter plus existing laser preview path; publish two consecutive fractional candidates through actual session method, select production metrics at 150%, update the exact width animation seam used by RenderLoop and read val immediately without animation tick. Checks expected 18.375/17.625 px, laser core /3, unchanged business revision/bridge, hold lock, real public CollapseAuxiliaryPanels cancellation (which calls CloseThicknessSlider), restored authoritative value, retained non-interaction SetTar, and existing revision-protected fractional commit/stale rejection. Laser test supplies a bounded synthetic range because Laser exposes no real slider.
- Existing FineDial cancel/activation/physics/revision and unit matrix coverage retained. Main owns full Solution build and execution.

Real touchscreen follow behavior remains manual acceptance for this incremental change; source and CPU seam checks do not claim hardware experience verification.
