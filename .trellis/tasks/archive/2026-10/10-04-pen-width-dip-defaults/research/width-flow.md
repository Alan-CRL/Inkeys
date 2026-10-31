# Research: Production pen width flow and policy seam

- Query: Trace actual width units, rounding, nominal float support, Bar interactions/preview/cursor, Draw3 bridge and target display snapshots; recommend a minimal shared contract.
- Scope: internal (current working tree, no branch changes or builds)
- Date: 2026-10-04

## Findings

### Files found and related specs

- `Inkeys/IdtState.h/.cpp`: initial width slots, shared hard/soft memory, synchronized business mutation and bridge publication.
- `Inkeys/IdtConfiguration.cpp`: legacy memory read/write (startup and migration research owned by the other researcher).
- `Inkeys/Inkeys/Business/PenToolState.hpp`: existing small header-only pure business policy, suitable pattern for a width policy header.
- `Inkeys/Inkeys/UI/Bar/Bar.Layout.cppm`: authoritative current preset arrays, slider ranges, preset selection and preview width helper.
- `Bar.Interaction.cpp`: slider continuous candidate transactions, FineDial integer step/settle, revision checks and preset submissions.
- `Bar.State.cpp`: main width text truncation.
- `Bar.RenderLoop.cpp`: animation/preview state, popup number rounding, geometry selection and preview rendering.
- `Bar.Initialization.cpp`: Bar-specific display snapshot publication and window DPI override.
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Bridge.h/.cpp`, `Draw3.Product.cpp`: ProductState snapshot bridge and merging.
- `Draw3.Host.cpp/.h`: actual Drawpad target display context, low-frequency dirty snapshot pump, style application and startup.
- `Draw3.WindowControl.cpp/.cppm`: atomics for next product visual style and mutex-protected display scale.
- `Draw3.DrawingController.cpp`: nominal canvas width, cursor, per-contact style locking and float stroke creation.
- `Draw3.StrokeGeometry.cpp`, `Draw3.InkPrediction.cppm`: float base width, float radii, model and pressure behavior.
- `Draw3/Assets/inkVertexShader.hlsl`: final physical canvas coordinates without another ordinary-ink DPI conversion.
- `Inkeys/Inkeys/Display/Display.cppm`: immutable snapshot, full monitor output pixels, effective DPI, bounds/work area distinction.
- Read specs: `.trellis/spec/native-desktop/{index,input-and-ink,draw3-integration,rendering-and-ui,cpp-conventions,configuration-i18n-and-assets}.md`, `.trellis/spec/native/runtime-and-rendering.md`, `.trellis/workflow.md`.
- Applicable contracts: Draw3 has a unique Host/input owner; bridge immutable snapshots are UI-to-render boundary; active strokes retain styles; Display subscription callbacks publish low-frequency state only; no hotpath hardware enumeration; keep encoding/EOL and minimal diff.

### Actual nominal renderer supports continuous float widths

**Do not enforce integer nominal width on production Draw3.**

- `Draw3.DrawingController.cpp:164-185`: DiameterForTool returns float product width for Pen, HardPen, Highlighter, shapes and Laser. CanvasDiameterForTool currently multiplies DPI only for Laser; ordinary state has already been treated as canvas px despite the name widthDip.
- `Draw3.DrawingController.cpp:8584`: Down copies `window_.ProductVisualStyleSnapshot()` into the runtime. `:8610-8639`: float baseDiameter is sent directly to `runtime->stroke.Reset(baseDiameter, ...)`.
- `Draw3.StrokeGeometry.cpp:237-240`: StrokeWidthEstimator accepts float base diameter and computes continuous min/max diameters. `:271`, `:296`: outputs `currentDiameter * 0.5f` float radius. `:696-714`: ActiveStroke::Reset preserves float base diameter in estimator.
- `Draw3.InkPrediction.cppm:390`: baseDiameter is float, not an integer configuration. No nominal-width integer boundary found between these entry points and float geometry upload.
- `Draw3.DrawingController.cpp:8805-8816`: terminal fallback also preserves float radius and float position.
- `Assets/inkVertexShader.hlsl:47-53`, `:66-73`: canvas world positions are normalized by physical screenWidth/screenHeight; radii remain float. These are physical canvas geometry, not DIP data.
- Ordinary nominal width can therefore remain idealCanvasWidthPx; appliedCanvasWidthPx equals ideal for continuous Draw3. A tested safe positive round adapter can exist for integer compatibility/number display without downgrading this path. No radius-first rounding is justified.
- Soft speed smoothing, pressure, predictive radius/vertices must remain continuous. Width chosen at Down is already locked by runtime state and estimator; subsequent UI/display updates should affect the next Down only.

### Business state and persistence boundary

- `IdtState.h:45-59`: initial Brush1.width/widthPreset=3, Highlighter1.width/widthPreset=35; Laser presets 3/5/7 with middle=5; line and rectangle slots independently start at 3.
- `IdtState.h:77-109`: all width slots are float. Existing Brush/Highlighter widthPreset scalar is a default placeholder, not a Thin/Medium/Thick identity.
- `IdtState.cpp:870-907`: SetPenWidthImpl validates only `targetWidth <= 0`, holds stateModeTransitionMutex, verifies optional transition revision, updates current slot, unlocks, optionally SetMemory(), then publishes Draw3. This currently accepts NaN/Infinity due to inadequate validation and does not advance transition revision on width write.
- `IdtState.cpp:883-887`: hard/soft share Brush1; Highlighter separate. `:891-897`: shapes have independent line/rectangle width slots. `:963-978`: GetPenWidth selects corresponding slot. No production assignment was found making shape width follow Brush1.
- `IdtState.cpp:212-225`: PublishDraw3State snapshots under mutex, copies `setlist.paintDevice`, clamps width to at least .1 and names it widthDip. This is presently mixed units, not a true DIP contract.
- `Draw3.Bridge.h:192-196`: ProductState.widthDip float defaults to 2. `Draw3.Product.cpp:85-99`: published state merges into existing bridge and directly copies widthDip and paintDevice. No width conversion occurs here.
- `Draw3.Host.cpp:1194-1231`: bridge revision gates style application; paintDevice==0 selects LargeScreen, otherwise Laptop; width is passed unchanged to WindowControl.
- `Draw3.WindowControl.cpp:541-557`: finite-positive style validation falls back to 2; width/color stored in atomics and next style snapshot loaded without rounding. Display style must not be derived from atomic fields spanning incompatible revisions.
- `IdtConfiguration.cpp:1106-1109`, `:1127-1130`: old memory accepts only JSON integer Width, restoring raw values directly. `:1160`, `:1168`: memory writes float Json::Value; the reader/writer currently disagree for fractional slider widths. These old ordinary fields are canvas px evidenced by UI preset write + Draw3 consumption, not names.
- `IdtMain.cpp:2291-2296`: device default initialization still assigns legacy memory/auto-default flags and 3/35 fields. Whole actual startup semantics must be handled by the configuration researcher, not inferred from these dormant-looking names.

### UI write APIs, ranges and rounding boundaries

- `Bar.Layout.cppm:23-29`: current shared brush presets 1/3/6, highlighter 35/50/70; brush min 1 px/max 30*DPI; highlighter 30*DPI..100*DPI.
- `Bar.Layout.cppm:77-102`: slider range endpoints are `int`; max/min lround logical endpoints times dpiZoom. These endpoint types cannot represent valid fractional DIP lower bounds.
- `Bar.Layout.cppm:286-297`: GetBarThicknessPresetPx performs lround(preset*dpiZoom), max 1, int narrowing with no huge-value protection. UI has already quantized pixel width before business write.
- `Bar.Interaction.cpp:5291-5297`: laser preset submits DIP, ordinary preset submits GetBarThicknessPresetPx through SetPenWidthIfRevision. `:3694-3698`: geometry preset also submits this ordinary px result.
- `Bar.Interaction.cpp:3901-3926`: slider position and direct preview touch are continuous float candidates. `:3928-3954`: candidate clamped but not rounded; `:4972`, `:5000-5003`: only true completed candidate changes commit through conditional state API. Preserve this continuous slider ability.
- `Bar.Interaction.cpp:1653-1666`: FineDial rubberband remains float visually but candidate lround -> int and marks candidate active. `:1692-1711`: commit rounds candidate again, checks revision and difference, commits integer. `:1800-1803` and `:1877`: integer settle targets. Other FineDial candidate copies round at `:4230`, `:4252`, `:4380`.
- `Bar.Interaction.cpp:1720-1749`: BeginFineDial establishes revision/range/current raw width. Merely entering FineDial can create rounded candidate, then settle/commit a noninteger preset on a tap. Need a semantic dirty flag / initial identity+value so opening or entering controls does not change width. Only genuine value-changing input may step/commit.
- `Bar.Interaction.cpp:3755-3764`: slider captures versioned state and initial width. Existing expected-revision rejection should also cover changed display/device context, not only tool/PPT transitions.
- `Bar.Layout.cppm:323-335`: ordinary preset selection compares float current width against rounded px result. Laser compares DIP. New identity selection must bypass px equality collisions; ExplicitDip should consistently show no automatic preset identity.
- `Bar.State.cpp:59-65`: static_cast<int> truncates displayed width, then clamps 0..999. This can show zero for valid <1 DIP and silently imply precision. Format logical DIP separately with approx marker and positive minimum display representation.
- `Bar.RenderLoop.cpp:6433-6441`: popup currently lrounds animated physical width 0..999 and caches integer text. This display must become logical DIP, not a rendering cache written back into state.
- `Bar.RenderLoop.cpp:4753-4754`, `:6017`: geometry button selected state reads Brush1.width, while actual shape setter/GetPenWidth use independent slots. This existing inconsistency should be corrected via active shape selection identity, not by introducing new follow behavior.
- Search found no production width setter in Business.ComponentActions or Input; the actual width write callsites are Bar and legacy configuration. Do not modify unused Draw2 shortcut handlers as current product ingress.

### Preview and cursor

- `Bar.Layout.cppm:346-355`: current preview width scales laser DIP by dpiZoom, but ordinary widths are taken raw because those contain canvas px.
- `Bar.RenderLoop.cpp:2113-2127`: render-only preview takes visual width or dragging candidate. Candidates are currently px; after migration candidate DIP must be resolved with target canvas context before generating preview geometry, while logical display must use original candidate DIP.
- `Bar.RenderLoop.cpp:10561-10577`: preview stroke/envelope uses current core width multiplied by panel animation scale, clamps against preview area's available physical height. `:10594-10602`: it morphs to a slider-track illustration. Preserve explicit visual morph/clipping; do not multiply nominal pen width by UI custom layout zoom. `uiZoom` controls area/layout, not business width.
- `Bar.Layout.cppm:351` documents preview in 96-DPI D2D space; no explicit SetDpi was found in UI module search, so the exact surface target default needs implementation review if assumptions change. Existing code manually uses physical preview widths and zoomed layout positions; ordinary width must use actual Drawpad effective DPI rather than Bar zoom.
- `Draw3.DrawingController.cpp:289-301`: pen cursor diameter=max(CanvasDiameterForTool, minimum cursor DIP*dpiScale); preserve intentional cursor visibility minimum independently of ink width.
- `:306-318`: highlighter cursor currently takes raw widthDip as px, no dpi multiplication; route through same nominal conversion as Highlighter Down. Aspect ratio and alpha preserved.
- `:321-332`: Laser cursor uses CanvasDiameterForTool once, with existing glow and particle policies elsewhere; keep Laser default and one DPI conversion.

### Actual target display source and change handling

- `Display.cppm:96-111`: MonitorInfo separates bounds, workArea, full output pixelWidth/pixelHeight, effectiveDpiX/Y, orientation and EDID; select full output pixels only. `:114-129`: immutable shared SnapshotPtr with generation, Find(HMONITOR), Primary().
- `IdtMain.cpp:2785-2788`: initial overlays use primary monitor full output pixel sizes. Initial fallback can use Display.Primary before Host exists without hardware probing or EDID.
- `Draw3.Host.cpp:1098-1105`: Display callback publishes snapshot into pendingDisplaySnapshot and sets dirty+wake. `:1108-1116`: PumpDisplayScale consumes only dirty snapshots; current callback does not enumerate per frame.
- `:1127-1138`: target is actual attached Drawpad HWND via MonitorFromWindow, snapshot Find, effective DPI reciprocal and full pixelWidth/Height. `:1186-1191`: display scale revision changes only if scale or client bounds changed.
- `Draw3.Host.h:309`: Host::EraserDisplayScaleSnapshot() is already publicly readable. `Draw3.Product.h:13`: ProductHost() exposes that Host. A pen context can reuse DPI/pixels/monitor/revision of this snapshot while excluding EDID physical fields from pen calculations.
- `Draw3.Host.cpp:38-61`: compatible DPI queries dynamically resolve GetDpiForWindow; then immutable Display snapshot; then GDI/system fallback. Preserve Win7 compatibility.
- `Draw3.Host.cpp:1538-1539`: StrokeModelConfiguration is initialized once with startup window DPI; `Draw3.InkPrediction.cpp:209-245`: dpiScale and speed scale are created together. For new ordinary widths, resolve from current context at Down; do not casually overwrite full model configuration or change pressure/speed behavior to get current width DPI. Laser behavior should retain its current single conversion.
- `Bar.Initialization.cpp:60-80`: Bar chooses PPT scene monitor or Primary and publishes its effective DPI. `:86-93`: Bar window DPI can override it independently. `Bar.RenderLoop.cpp:1294-1298`: barStyle.zoom = displayScale*customZoom and barStyle.dpiZoom = Bar displayScale. Therefore barStyle.dpiZoom cannot be the authoritative drawing monitor width context.
- Display/paintDevice change should publish one pen context revision, update cached resolved preset DIP under existing business lock, and invalidate in-progress width UI transactions. It must not reset ExplicitDip or persist on every frame. Active Draw3 stroke already keeps locked nominal pixel width; new context can safely affect next stroke.

### Recommended minimal API contract (design proposal, not current code)

Use `Inkeys/Inkeys/Business/PenWidthPolicy.hpp`, header-only like PenToolState.hpp, so traditional state, Bar module, Draw3 module and headless tests share the same production implementation without a new module dependency graph.

```cpp
enum class Family { Brush, Highlighter, Laser };
enum class Source { Preset, ExplicitDip };
enum class Preset { Thin, Medium, Thick };
struct Selection { Source source; Preset preset; double explicitDip; };
struct Context { uint64_t revision; int paintDevice; double effectiveDpi;
                 int pixelWidth; int pixelHeight; };
struct Resolved { double resolvedWidthDip; double idealCanvasWidthPx;
                  double appliedCanvasWidthPx; };
Resolved Resolve(Selection, Family, Context); // continuous applied==ideal
int RoundPositiveCanvasPx(double px); // safe checked compatibility adapter
```

- Retain float renderer slots as caches for compatibility, add Selection adjacent to each business width slot (Brush shared hard/soft, Highlighter, shape line/rectangle independent; Laser retains current 3/5/7 policy). Selection is truth; resolved DIP cache is derived.
- State public APIs: `GetPenWidthSelection(snapshot)`, `SetPenWidthPresetIfRevision(index, expectedRevision, setMemory)`, `GetPenWidthContext()`, `GetPenWidthMetrics(snapshot)`, `ApplyPenWidthContext(context)`. Existing SetPenWidth/conditional API becomes ExplicitDip commit, validated finite positive bounded, no logical >=1 clamp.
- Context must be consistent with StateModeVersionedSnapshot when captured for a gesture, or include context revision in the snapshot. A semantic changed context increments transition revision once, re-resolves Preset values, and publishes one bridge update. ExplicitDip remains same logical value but renderer next stroke converts using the new target DPI.
- Either add a Host low-frequency `penWidthContextChanged` callback after PumpDisplayScale change, or expose a product pen context cache built there; invoke State publication outside internal display locks. Do not make a render-loop Get call mutate State every frame. Startup State initialization uses the same Context resolver with Primary fallback, synchronously before producers can accept first stroke.
- Bridge may carry Selection identity+resolved DIP+context (DPI/full pixels/revision) as one immutable snapshot to avoid renderer/UI calculating from different targets. Keep history geometry physical px; changing a context must never reinterpret saved radii.
- Safe DIP -> canvas path uses double multiplication then checked float narrowing at final float renderer boundary; no early DIP rounding. Ordinary drawing/highlighter cursor/shape Down and preview call the same resolver. Laser keeps its existing DIP and one multiplier.
- Slider range should become floating DIP and include both dynamic preset extrema and current valid value. Minimum can be output-one-pixel DIP (`1/systemScale`) or lower where required; do not force 1 DIP. Preserve old approximate max usability, enlarge only if preset/current legal width demands. Final exact limits/step require design decision; FineDial integer-DIP steps may clamp to fractional endpoints only on real adjustment, not activation.
- Main/popup/preset number text use resolved logical DIP with approximation if noninteger; never show 0 DIP for valid width. Rendering cache is separated from logical display cache.

## External References

- User-supplied Windows DIP definition: https://learn.microsoft.com/en-us/windows/win32/learnwin32/dpi-and-device-independent-pixels
- User-supplied positive half-away rounding reference: https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/lround-lroundf-lroundl-llround-llroundf-llroundl
- This research is source tracing; it did not browse/independently verify external pages or current toolchain versions. Main session owns external semantic verification and builds.

## Caveats / Not Found

- Source confirmation only; no build/headless/GUI tests performed by this researcher, as dispatched.
- PRD existed in seed form at initial research read; design.md/implement.md did not yet exist. Main session is drafting them from research. Research role intentionally did not load implement/check manifests.
- Shapes have independent production width slots despite geometry UI comparing Brush1; do not claim existing shapes automatically follow Brush unless later callers prove it.
- Exact Surface D2D target creation path was not fully traced; the preview-space conclusion uses existing explicit Bar comment and physical draw computations. Review target default DPI when altering that space.
- Current startup model DPI cache is distinct from low-frequency Host display scale cache; updating ordinary next-stroke DPI must not accidentally change Laser, eraser, pressure, speed or reconnect behavior.
- Fractional memory values written by legacy float serializer currently fail isInt-only restore; migration should read valid numeric values and preserve evidence/backup even when legacy persistence failed to restore them.
- An all-atomic style snapshot contains no explicit identity/context version today. New context/identity must be grouped consistently; no duplicate preference authority in renderer cache.
