# Research: Configuration and startup width provenance

- Query: Which production startup/config/settings paths own width preferences, what units did old values contain, and how can preset identity migrate safely?
- Scope: mixed (current repository source; official Windows DPI semantics)
- Date: 2026-10-04

## Findings

### Files found and related specs

- `Inkeys/IdtState.h` / `.cpp`: float business widths, shared soft/hard pen state, guarded setters, bridge publication.
- `Inkeys/IdtConfiguration.h` / `.cpp`: legacy deploy JSON, old preset placeholders, memory JSON reader/writer.
- `Inkeys/IdtMain.cpp`: actual production startup, display initialization, default configuration, first legacy write, Draw3 startup.
- `Inkeys/IdtDrawpadFacade.cpp`: compiled Draw3 facade; old drawpad startup is a no-op.
- `Inkeys/IdtDrawpad.cpp`: uncompiled historical Draw2 default/memory priority evidence.
- `Inkeys/Inkeys/UI/Setting/Setting.cpp`: active device mode selector; no active startup width controls remain.
- `Inkeys/Inkeys/Display/Display.cppm` / `.cpp`: versioned immutable display snapshots, full output pixel bounds and effective DPI fallback.
- `Inkeys/PptSettingsPersistence.h`: shared atomic JSON replacement implementation.
- `Inkeys/Inkeys/Other/Other.Config.cppm`: second config schema; no pen width preference fields found.
- `Inkeys/src/i18n/{zh-CN,zh-TW,en-US}.jsonc`, `Scripts/i18n.ps1`: source languages and generated-key workflow.
- Related specs read: `.trellis/spec/native-desktop/index.md`, `configuration-i18n-and-assets.md`, `startup-preview.md`, `draw3-integration.md`, `build-and-compatibility.md`, `cpp-conventions.md`, `.trellis/spec/native/index.md`; workflow and actual `.codex/agents/trellis-research.toml` read.

### Actual startup is different from the reference assumptions

1. Display service is synchronously initialized at `IdtMain.cpp:2218`; snapshot is available before config setup (`2220`). This already obtains the facts required for width policy without any new EDID query.
2. Runtime placeholder defaults: `stateMode` constructor assigns brush 3, highlighter 35, laser preset 3/5/7 and laser width 5 (`IdtState.h:45-52`). Business fields are `float` (`77-95`), so the datatype itself can preserve resolved fractional DIP.
3. `setlist.presetSetting` initializes memoryWidth=true, memoryColor=false, autoDefaultWidth=true, defaults 3/35 (`IdtMain.cpp:2291-2296`). However these legacy preset fields are no longer read from current deploy configuration and `CaptureSettingJson` explicitly removes the entire `Preset` object (`IdtConfiguration.cpp:509`). This is an intentional prior retirement contract in the config spec, not evidence that old user preferences never existed.
4. New module config `ReadAll()` happens at `IdtMain.cpp:2409`; old deploy `ReadSetting()` at `2477`; unconditional startup `WriteSetting()` at `2478`. **Migration of retired old `Preset` data must happen before that write erases it.** New policy belongs with the legacy pen/device preferences and must coexist with both config files, rather than moving unrelated configuration.
5. `GetMemory()` only actual drawing startup call is in `IdtDrawpad.cpp:2174`. That file is a `None` project item (`Inkeys.vcxproj:904`). Compiled `IdtDrawpadFacade.cpp:57-60` has a no-op `drawpad_main()`. Thus production currently does not restore width memory or execute autoDefaultWidth/defaultWidth priority at all. Adding a real production startup initialization is necessary; updating dormant Draw2 code cannot meet first-stroke acceptance.
6. Current canvas startup uses primary monitor full pixels (`IdtMain.cpp:2785-2787`), starts Draw3 (`3022`) and syncs business state (`3068`) before checking first frame (`3070`). Recommended insertion: read/migrate preferences and initialize identities after display/deploy are ready, before Draw3 starts; publish initialized state using current owner/lock entry points. Do not rely on a later async Bar refresh.

### Historical priority and old-unit evidence

- Historical Draw2 startup computes preset cache as `round(3*drawingScale)` / `round(35*drawingScale)` (`IdtDrawpad.cpp:2161-2162`). autoDefaultWidth=true uses those; otherwise `round(defaultBrush1Width/defaultHighlighter1Width)` (`2164-2172`). It then calls GetMemory (`2174`), so valid remembered widths override startup defaults only when memoryWidth is enabled.
- `GetMemory` reads `Inkeys/Memory/memory.json` (`IdtConfiguration.cpp:1063`). Old paths: `Draw.Brush1.Width`, `Draw.Highlighter1.Width`; restoration requires `isInt()` and memoryWidth (`1106-1109`, `1127-1130`). Invalid/partial fields leave current state, color restored independently only under memoryColor (`1119`, `1140`). No laser field exists in this memory reader/writer.
- `SetMemory` snapshots state (`1153`) and writes raw `float` business widths (`1160`, `1168`) unconditionally, even when memoryWidth is disabled; restore flag, not save flag, owns priority. The write may serialize nonintegral JSON numbers although the old reader accepts only integral JSON. Migration should recognize valid numeric legacy widths instead of perpetuating that mismatch.
- Current Bar preset function explicitly returns integer pixels `lround(base*dpiZoom)` (`Bar.Layout.cppm:286-297`), while current ordinary visual width returns stored width unchanged (`346-355`) and laser visual multiplies DPI. This confirms current ordinary/highlighter stored values are intended canvas pixels, laser stored values DIP. Current bridge misleadingly copies GetPenWidth into `ProductState.widthDip` (`IdtState.cpp:220`). Source DPI is not stored in memory JSON and is not available from any width schema field found. Never infer source DPI from UI zoom, current EDID, or an unrelated cache.
- Old default custom fields are raw values then rounded directly with no DPI conversion in historical startup; the old preset cache is resolution-scaled pixels (`GetDrawingScale` reads Primary pixel sizes, `IdtDraw.cpp:390-395`). Therefore legacy custom default and memory ordinary/highlighter values need px-to-DIP migration. Preserve old laser DIP if a new/other persistence path supplies it; do not apply old ordinary migration blindly by field name.
- A width of 3 or 35 is not proof of a factory choice; explicit legacy field presence and autoDefaultWidth/memory enable govern provenance.
- Main-supplied read-only historical evidence: `git log -S DefaultBrush1Width` located `e6c729d2` (`refactor(settings): clean up obsolete options`); `git show e6c729d2^:Inkeys/IdtConfiguration.cpp` confirms exact keys `Preset.MemoryWidth`, `Preset.MemoryColor`, `Preset.AutoDefaultWidth` (bool), `Preset.DefaultBrush1Width`, `Preset.DefaultHighlighter1Width` (`isDouble/asDouble`, float assignment), and matching output keys. Main confirms reference `529722e6877a35d2aa5e804657e55543436610ef` already removed them. Use these historical spellings in migration, preserve explicit custom defaults when AutoDefaultWidth=false, and never infer factory status from equality to 3/35.

### Device choice and output context

- `paintDevice=0` is touch/classroom mode; `1` is mouse/pen/tablet or ordinary mode. Direct confirmation: bridge comment (`Draw3.Bridge.h:197`), settings vector index 0 Touch, index 1 MousePen (`Setting.cpp:3787-3789`), active speed eraser receives classroom for state.paintDevice==0 (`Draw3.Host.cpp:1224`).
- Existing *factory device selection* uses digitizer and physical-size evidence (`IdtMain.cpp:2375-2385`); then explicit persisted `PaintDevice` overrides it (`IdtConfiguration.cpp:233-234`). Reuse this result; do not add another width-specific autodetection algorithm.
- Device settings updates setlist -> `SyncDraw3State` -> `WriteSetting` (`Setting.cpp:3803-3805`). Integrate width-context refresh with this existing path and do not silently overwrite explicit selection.
- `MonitorInfo` has separate bounds/workArea and pixelWidth/pixelHeight/effectiveDpiX/Y (`Display.cppm:100-105`). Pixel sizes derive from `rcMonitor`, not work area or EDID timing (`Display.cpp:380-387`). Snapshot has generation and Find(HMONITOR) (`Display.cppm:116,125`). Use target HWND monitor with snapshot.Find, matching Draw3 Host `PumpDisplayScale` (`Draw3.Host.cpp:1127-1138`), rather than Bar HWND.
- DPI API dynamically loads GetDpiForMonitor and falls back to GDI display DC, then 96 (`Display.cpp:330-348`), preserving Win7 compatibility. Snapshot refresh retains previous logical DPI/output bounds on enumeration failure and invalidates physical evidence (`779-787`). Subscriber callbacks occur outside internal locks (`794-795`).
- Existing startup display subscriber currently updates drawingScale/stopTimingError only (`IdtMain.cpp:2519-2537`); it does not restore business widths. A revision-aware width refresh here can resolve Preset without new polling/EDID work.

### Save safety and migration extension

- deploy read only installs successfully parsed root (`IdtConfiguration.cpp:194`). Malformed/oversize/partial I/O safeguards are already tested by main probe functions (`IdtMain.cpp:501-573`). Preserve those boundaries.
- deploy writer serializes with BOM (`IdtConfiguration.cpp:608`), serializes writes under mutex (`613`), bounds size (`614-616`), refuses overwrite of existing unreadable/malformed source (`618-619`), then calls atomic writer (`620`). That writer creates same-directory exclusive temp, writes/flushes/closes, then `MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH)` (`PptSettingsPersistence.h:149-176`); failed replacement preserves old file.
- memory writer currently truncates original before serialization/write (`IdtConfiguration.cpp:1183-1198`), builds fresh JSON rather than preserving unknown members, has no migration journal/backup/source DPI/version. **Do not mark migration safe while retaining this destructive write for migrated preferences.** Reuse the existing bounded-parser + atomic writer pattern narrowly for memory; retain old fields/value metadata or backup before replacing, preserve original on parse/save failure.
- `CaptureSettingJson` optionally clears all unknown keys under Config.AutoClean (`IdtConfiguration.cpp:461`); retaining migration metadata only in an old unknown object is insufficient. Explicitly emit version/unit and needed raw legacy/migration metadata in new known pen preference output.
- Minimal identity extension: use the existing state/config system with one per-shared-brush and per-highlighter width selection `(Preset,index)` or `(ExplicitDip,value)`; laser own selection remains current 3/5/7 DIP. Business float width stays resolved runtime cache, identity/value is authority. Keep shape inheritance as actual current state transition implements; do not add a parallel global preference engine.
- Separate startup defaults from session memory. Precedence: valid enabled memory selection -> explicit startup selection -> auto/default middle preset. Memory disabled must not restore last session; session setter must not rewrite explicit startup default. Missing keys/unknown enums must use validated fallback independently per tool. Newly versioned DIP must never divide DPI again.
- Where no value-associated source DPI exists (currently none found), convert old px with ready target effective DPI and record migration estimate + raw px + used DPI, not a claim to recover original portable device DIP. A successful atomic save must persist version and provenance together. On failed save keep disk legacy data intact and never convert already migrated in-memory selections twice.
- Test pure shared decoder/encoder and real GetMemory/ReadSetting/startup paths for missing fields, bad versions/enums, integral/fractional numeric input, memory priority, idempotence/roundtrip, disabled restore, original retention on failed save.

### Settings and localization

- Current Setting page has device selector but no active memoryWidth/autoDefaultWidth/default pen width controls (all uses searched). Restoring a retired entire Preset UI is unnecessary; new preference codec may read old flags and preserve startup choices while using current Bar entries.
- Bar source localized thickness template is `BarUI.DrawAttributes.ThicknessFormat` (`zh-CN.jsonc:98`) and overflow title/body (`102-103`). Device labels use `SettingsUI.Draw.Effect.Device` (`389-394`). All supported languages are zh-CN, zh-TW, en-US.
- Generated `IdtI18nKeys.g.h:3` explicitly forbids hand editing. `Scripts/i18n.ps1 sync` regenerates other language structure, key header and base snapshot; then fill translations and run `check` (spec `configuration-i18n-and-assets.md:272-281`, script `709-723`). Use only source JSONC for unit/approximation wording and generation pipeline for header.
- Current `Bar.State.cpp:61` truncates display via `static_cast<int>`; its presentation must change without rounding state/config. New noninteger DIP needs approximate text, especially sub-1 values; presentational rounding must not create 0 DIP.

### External references

- Official Microsoft Windows DPI guidance (page updated 2023-05-24; opened 2026-10-04): https://learn.microsoft.com/en-us/windows/win32/learnwin32/dpi-and-device-independent-pixels . DIP is 1/96 logical inch; pixel conversion uses effective DPI/96; Direct2D may perform its own target DPI scaling, so canvas coordinate contract must be inspected separately.
- User-provided lround reference: https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/lround-lroundf-lroundl-llround-llroundf-llroundl . Opening via web tool failed; no external behavior claim inferred from that failed lookup.

## Caveats / Not Found

- No builds, GUI tests or code edits performed by this research role. All evidence above is source inspection, not runtime success.
- Exact retired deploy `Preset` JSON leaf spellings are absent from current source, but were confirmed by main's read-only historical inspection above. Research role did not perform any git operation.
- Production currently does not execute historical memory restore. The implementation must restore the intended product priority consciously, not claim it already works.
- No historical width-associated source DPI found. Portable-config migration necessarily estimates using target effective DPI.
- The proposed known pen config path is a design recommendation, not an established existing schema; parent coordinates final interface and independent review.
