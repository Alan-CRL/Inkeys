# Five eraser entries and continuous non-Touch sizing

Date: 2026-09-15
Baseline: e071ff3a00b9781c5407ee6d8e8a773c1293ad8b
Branch: feature/eraser
Main agent only. Task remains in progress. No commit, push or archive.

## Implemented boundaries

Five erasing entries now resolve independently: MouseLeft, MouseRight, Touch, PenTip and PenTail.
Entry preferences change erasing width, not whether ordinary tip/left input draws or erases.
Right/tail temporary erasing retains selectedTool separately from effectiveTool and respects selection mode and the existing tail-enable gate.
Normal eraser selection uses ConfiguredEraser. Explicit FixedEraser and SpeedEraser retain forced semantics.

Non-Touch sizing uses independent entry sessions with stable-context/effective-config checks and ownership tickets.
Up does not reset the logical diameter or play a large visual over a clipped-small logical state.
Ordinary separation advances time normally, targets min(release diameter, standard) without new Hover input, and keeps decaying sweep memory.
Fresh Hover may continue fine positioning but cannot create sweep evidence.
Down evaluates raw state at the event time, reanchors position, discards unobserved gap motion and may inherit legal size above standard.
A stale Up or later baking callback cannot overwrite a newer owner ticket.

Every independent contact still creates its own RuntimeStroke/model/geometry list.
Size sessions never generate connection geometry or change reconnect thresholds.
The old runtime may still enter the existing reconnect-candidate protocol; its accepted reconnect uses the existing pause/resume rules, independently of the ordinary detached size session.
This does not claim that size information can distinguish intentional lifts from genuine input dropout.

Touch does not use cross-contact sessions. Its small startup, independent area reference, expiry and true reconnect behavior remain separate.

## Settings, defaults and migration

Formal location: drawing settings, one eraser block containing five type rows and separate pen-tip/tail response controls.
The old global width UI and duplicate experimental area-assistance card were removed.
The independent TouchArea console option remains in Experimental Options.
The diagnostic page still writes the same area-assistance state.

Persisted keys in Inkeys/Config/main.json:
- Drawing.Eraser.MouseLeft
- Drawing.Eraser.MouseRight
- Drawing.Eraser.Touch
- Drawing.Eraser.PenTip
- Drawing.Eraser.PenTail
- Drawing.Eraser.PenTipResponse
- Drawing.Eraser.PenTailResponse

Width values: Fixed=0, Speed=1.
Pen response values: Automatic=0, ScreenPen=1, Tablet=2.
A schema value of -1 is only an absent-setting migration sentinel.

When no new width key exists, the default is Speed.
If the old disk EraserSetting.EraserMode was explicitly 2, missing left/Touch/tip keys migrate to Fixed; right/tail remain Speed because their old hardcoded Fixed behavior was not a saved user preference.
Existing new 0/1 preferences are respected. Unknown width values fall back to Speed.
The old area key Experimental.Inkeys3.Draw3.TouchContactAreaAssistance is unchanged; selecting Fixed for Touch does not clear it.

The actual local files inspected before implementation had legacy EraserMode=1, no Drawing.Eraser node and area assistance=true.
Thus this local configuration will initialize all five types to Speed and retain area assistance when the normal application next starts.
The hidden test path does not rewrite this real application configuration.

Win8+ defaults the two pen choices to Auto.
Win7 defaults missing choices to Tablet and safely resolves old Auto/unknown values to Tablet at runtime.
Win7 UI omits Auto but stores the stable values 1/2, not reduced-list indices.
A manual screen-pen choice does not fake IntegratedPen identity, mapping reliability or EDID validity. Unmapped input uses the existing DIP/heuristic fallback.
An explicit development response override wins and is identified in diagnostics.

Preference changes publish a whole table through StateBridge; each contact resolves its own entry against its latched table.
Changing a different entry or diagnostic-only metadata does not invalidate an unrelated session.
PublishProductState also now forwards the already-existing paintDevice field, previously omitted by that merge, so configured fallback selection is not lost. Model constants themselves were not changed.

## Reproduced pen-down cause and evidence

A small harness compiled the real production SpeedEraser.cpp, not a reimplementation.
Before:
- Fine pen Hover: 16 DIP; Down after only display revision/cursor metadata changed: 32 DIP.
- Mouse accepted diameter: 160 px; logical Up: 32 px; next Down: 32 px.

After:
- Fine pen Hover: 16 DIP; corresponding Down: 16 DIP.
- Mouse accepted diameter: 160 px; logical Up: 160 px; next Down after 50 ms: 160 px.
- The same harness returned exit 0.

The old full Config equality check selected Reset instead of inheritance.
DrawingController additionally invalidated the lane on whole-config changes or the 250 ms handback deadline.
The new effective-config comparison retains context, meaningful source/mapping, DPI, unit and calculation checks while ignoring harmless revisions and recognition/contact metadata.

Pending pen contact cursor resolution also uses the same entry/config/event-time path instead of a static 50-pixel fallback.
Down-only first-frame handling uses the accepted Down diameter; later frames may naturally evolve with time.
These are reproduced code-path causes, not a claim that every physical-device pen jump had one unique cause.

Current source granularity is the available tablet context plus effective mapping.
Concurrent owner tickets are isolated, but this work does not claim to identify every physical pen sharing one digitizer or separate physical mice merged by Windows into one pointer.

## Validation actually executed

| Validation | Result |
| --- | --- |
| Full native ARM64 InkeysRepo.sln Debug/ARM64 | exit 0 |
| InkeysHeadlessTests.exe --no-window | exit 0 |
| Inkeys.exe --draw3-eraser-hidden-test | exit 0; DirectCompositionVisualTree and UlwDirtyRect exercised |
| Existing fine quantization/sparse cases | 1728, 0-DIP worst peak-to-peak, zero held exits |
| Existing sample/frame deviation | 1.99186%, unchanged |
| Real Config write/read fixture | five kinds, independent tip/tail choices and retained area key passed |
| Pure non-Touch entry/gap matrix | 4 entries x 3 DPI values x 7 gaps, passed |
| Hidden non-Touch gap scenarios | 4 entries x 6 gaps x 2 backends, passed |
| Static modern Pointer imports checked with ARM64 dumpbin | none of GetPointerType/GetPointerDevices/GetPointerDeviceCursors/GetPointerDeviceRects |
| git diff --check | passed before documentation completion; final format check also recorded in the session |

The pure gap matrix includes 0/20/50/100/200/500 ms and 2 s, including a frame clock advanced beyond the subsequent Down event.
It compares event-time session diameter with Down, tests stale tickets, rejects real context/DPI/mapping changes and checks finite long-gap idle.
The hidden matrix includes five entries x Fixed/Speed, same-batch left Fixed/right Speed, ordinary pen/left drawing versus right/tail temporary erasing, harmless pen metadata revision and first-point radii.

For every logged independent gap Down, the actual new geometry list had one point.
Examples on DComp: left/right/tip/tail after 20/50 ms retained 160 px with first radius 80 px, reason=continuous.
Longer gaps naturally decayed. Down diameter matched first radius*2 within the test's 0.01-pixel tolerance.
The logged cursorPx can come from a later observation than downTime; values such as downPx=158.508 and later cursorPx=139.968 are not same-time first-frame comparisons.
The test is not claiming a pixel-readback screenshot comparison for every first-present frame or gap midpoint.

Existing Touch, area, no-Move, width-anchor, Undo/Redo and UInk save/read/import checks remained enabled and passed.
The geometry evidence uses the real Host/mailbox/drawing thread/model output, not only the standalone controller.

## Preserved calculations

Direct source comparison confirmed these production functions unchanged:
ReferenceTargetDiameterDip, CompensateTargetDiameterDip, ObserveFineIntent, FollowFineTarget, FollowTarget,
ResolveContactLengthTransform, ConvertContactArea, ObserveContactArea, AreaExpirySeconds and AreaReferenceFloor.
No eraser size defaults, fine/sweep thresholds, high-band curves or area multipliers/ceilings were retuned.

## Build and edit notes

The first full build identified two added test-source issues: Windows near macro collision and a missing Check failure-counter argument.
The latter was corrected directly.
The headless test file was memory-mapped by another process; truncating and replacing it failed. The helper was then changed from near to Near through an equal-length, non-truncating write. Test logic, encoding and newlines were unchanged. No user process was closed.
Interim builds failed only while that macro fix was still unwritten. Final full build succeeded with 17 warnings, mainly existing conversion/shadow/alignment warnings and test-only warnings. No warning was suppressed.

MSBuild was located via vswhere and ran natively on ARM64 with /m:1 /nr:false and Debug/ARM64.
Path/PATH normalization and node-reuse settings were local to that PowerShell invocation.
PptCOM dependency/export/copy steps were retained.
PptCOM.dll remained SHA-256:
779908B2ACC9F37241A4C59C36822685E87A07C4BC886BDE22D9D167E68FE117

## Changed components

- IdtConfiguration.h/.cpp: retain whether legacy disk choice was explicitly Fixed.
- IdtMain.cpp and IdtState.h/.cpp: initialize/migrate/read/write/publish entry preferences.
- Other.Config.cppm: persisted schema.
- Draw3.Bridge.h and Draw3.Product.cpp: configured eraser and full preference publication.
- Draw3.WindowControl.cppm/.cpp: thread-safe entry table, tool policy and canvas-exit revision.
- Draw3.SpeedEraser.h/.cpp: resolver, compatibility, detached state and ticketed non-Touch sessions.
- Draw3.DrawingController.cpp: per-contact entry selection, separate sessions, first-point/cursor integration.
- Draw3.Host.h/.cpp: entry diagnostics and distinct gated hidden-test identities.
- Setting.cpp: five-row formal UI and relocated area control.
- Headless/hidden tests and Trellis specs/task documentation.

## Logs and remaining manual acceptance

Logs: Build/eraser-entry-session-build-final.log, Build/eraser-entry-session-tests.log,
Build/eraser-entry-session-hidden-out.log, Build/eraser-entry-session-hidden-err.log,
Build/eraser-entry-session-imports.log. The production-controller reproduction is Build/eraser-session-baseline.cpp.

UI structure, dynamic row heights, scaling expressions, wrapped area text and Win7 enum mapping were statically reviewed.
No visible settings/application window or Computer Use was launched; visual layout and long-text appearance are not claimed as manually verified.
Actual mouse-button chords, multi-device pen behavior, Surface feel, normal GUI restart/migration and Windows 7 SP1+KB2670838 runtime remain manual acceptance.

[GetPointerType documentation](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getpointertype) specifies Windows 8 as its minimum; this implementation checks its export dynamically rather than importing it on Windows 7.

