# Independent planning review

- Active task: `.trellis/tasks/10-04-pen-width-dip-defaults`
- Role: dispatched `trellis-check`, planning review only; no production edits, builds, task activation or archive.
- Baseline: `01a953d41adb5084e25899592cdde3bcb6c4770d`, `chore/publish`; existing working-tree changes remain part of inspected source.
- Result: **PASS for implementation start** after the main session incorporated the corrections below into `design.md`. This is design approval, not a code/test acceptance claim.

## Context and evidence

Loaded the native hook saved output and task PRD/design/implement/check manifests, relevant native desktop specs, cross-layer guide, and finalized `config-startup.md`, `width-flow.md`, `tests.md`. Inspected actual Host display/bridge/startup ordering and DrawingController Down/diameter paths. No recursive subagent dispatch.

- `Draw3.DrawingController.cpp:181-185` currently multiplies DPI only for Laser; ordinary widths arrive as canvas pixels despite the `widthDip` name. At `:8584`, Down captures the visual style; `:8610-8633` passes a float base diameter directly into stroke reset. Keeping ordinary/shape/highlighter nominal geometry continuous is required; the integer compatibility adapter must not replace this production path.
- `Draw3.Host.cpp:1108-1191` consumes the display dirty flag using the actual attached Drawpad monitor and a single Display snapshot. `:1194-1243` calls display pumping before grabbing the bridge snapshot. This supplies a narrow existing integration point for the shared pen context.
- The current startup run thread creates DrawingController then calls `PumpBridgeState` before its first frame, while RTS approval already occurs earlier. The new first-input guarantee therefore needs the explicitly planned initial synchronous width-context/bridge preparation before user admission, rather than assuming existing startup order already proves it.
- `Bar.Rendering.cpp:1835-1839` checks its rendering device context is exactly 96 DPI. Preview metrics can stay physical canvas pixels without another implicit target DPI multiplier; layout zoom must remain independent.
- Config research confirms retired `Preset` keys and raw ordinary/highlighter pixel memory without associated source DPI. Laser DIP must be treated separately. Existing memory writes truncate the source, unlike the deploy atomic writer. AutoClean also means keeping backup information only under an unknown legacy key is insufficient.
- `tests.md` distinguishes no-HWND probes from misleadingly named hidden tests that actually show windows. The planned offscreen Bar probe is permitted; visible Host tests remain manual/unexecuted under current authorization.

## Corrections incorporated by main before start

1. **Memory migration save safety:** replace the truncating memory write narrowly with existing atomic-write and bounded parse/valid-or-absent mechanisms. Preserve unrelated root members, legacy values/provenance, and original disk bytes on parse/write failure. Deploy retains its established safe writer.
2. **Future schema preservation:** unknown future width schema is never decoded as legacy px or overwritten with current defaults during ordinary save, including AutoClean. The original object survives without downgrade; runtime fallback does not claim successful migration.
3. **One-generation state:** Context is part of the same locked StateMode/VersionedSnapshot as Selection and width. `GetPenWidthMetrics(snapshot)` consumes that snapshot only. Independent getter calls must not assemble mixed generations.
4. **First input and bridge ordering:** initial target resolution/preferences complete before RTS user admission. Host display callback synchronously applies state and publishes the matching bridge; bridge pumping then consumes the same DIP/context generation. No asynchronous first-stroke correction.
5. **FineDial step boundary:** use exact entering origin plus signed rounded step count times 1 DIP, preserving the origin fraction. No dirty interaction means no commit; cancellation preserves Selection identity. This avoids opening-time rounding and asymmetric fractional-origin jumps.
6. **Defined numeric safety:** `(0,65536]` DIP and maximum `1048576` canvas px; setter/codec reject invalid inputs, resolver uses safe corresponding middle fallback, final canvas adaptation saturates before narrowing/rounding. The integer adapter is separately tested.

## Implementation and independent code-review checkpoints

- Publish exact core interfaces and config schema to UI/Draw3 workers before dependent edits. Bridge must carry enough immutable context to avoid multiplying a DIP width using a stale startup-only `configuration_.dpiScale`. Context ownership belongs to the existing state/display owners, not preview or renderer caches.
- Unknown enum/version, partial per-slot fields and malformed roots need production codec/read/write coverage; failed migration save must preserve originals and same-process runtime must not divide migrated DIP twice. Version, identity and migration provenance must be committed together. Future-schema tests must also exercise AutoClean.
- Config precedence remains enabled valid memory, then explicit startup choice when automatic defaults are disabled, then middle Preset. Restore width once after configuration and target are ready; tool/PPT transitions must not restore factory values. Do not reintroduce a retired full Preset settings UI.
- Shape slots are genuinely independent in current setters/getters. Fix their UI selected identity to reflect the active shape slot without inventing a brush-following relationship. Ordinary hard/soft remain one shared slot.
- Check all production conversion/cursor/preview sites for legacy `>=1 DIP`/`0.1 DIP` clamps and early rounding. Safety must be applied at its documented boundary; valid sub-1 DIP target presets must survive. Soft pressure/speed/prediction and historical geometry remain float physical geometry.
- Display semantic changes invalidate old gesture revisions; equivalent refreshes do not reset preference, save each frame, or create a publication loop. An active stroke retains its Down style while the next stroke uses updated target metrics.
- Real tests must call policy/codec/state/Bar/Draw3 seams, not reproduce formulas. Include table expectations, fractional 1440p at 275%, preset identity collisions, UI zoom separation, no-op opens/cancels and active-stroke style capture. Matrix integer values remain adapter expectations while real Draw3 keeps ideal float widths.
- Update configuration/i18n and Draw3 specs after implementation: the existing spec says retired Preset reads are ignored and ordinary width is already pixels. The authorized task adds a one-time compatibility decoder and genuinely DIP bridge, so those contracts need explicit replacement without reviving obsolete UI or Draw2.
- Final gates remain main-owned complete Solution build, no-window production/headless/UI/Draw3 probes, i18n generation/check, scope/EOL review, and independent implementation review. Touch/PPT/mixed-DPI/Win7 experience remains separately documented manual acceptance.

## Verification status

- Design/evidence consistency: PASS following corrections above.
- Production edits by this reviewer: none.
- Lint/type-check/tests: not executed by this planning reviewer; main session owns baseline and later integration verification. No runtime PASS inferred from source reading.
- Remaining blocking product decisions: none within the already authorized scope.
