# Research: continued release Draw3 real product paths

- Query: Independently establish Draw3 backend selection, available real-product metrics, and a finite evidence-driven hotspot shortlist after UI3.
- Scope: internal; no production edits, build, GUI or git operations.
- Date: 2026-10-03

## Findings

### Device and presenter facts

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.GraphicsInitialization.cpp:61-113`: independent D3D11 device with BGRA, hardware-first; E_INVALIDARG retries FL11_0 on the same driver; only final hardware failure selects WARP. No product configuration forces WARP. Successful hardware logs are limited but failure HRESULT and WARP attempts remain emitted.
- `Draw3.Host.cpp:1457-1503`: startup stdout always records graphics driver/featureLevel, main/aux HWND, presenter mode and size. `CaptureRuntimeMetricsMetadata` (`:416-434`) additionally copies adapter description and output target/revision without exposing COM ownership.
- `Inkeys/IdtMain.cpp:2760`: normal DComp preference = `Experimental.Inkeys3.Draw3.EnableDirectComposition` (default true in Other.Config.cppm:291) AND API capability. `Draw3.TransparentPresentation.cpp:613` probes preferred mode/API. A present-selection log alone does not distinguish user configuration from API absence.
- `IdtMain.cpp:723-784`: failed DComp Host startup causes a sequential hidden HWND-chain rebuild and `allowDirectComposition=false` legacy retry. Read failure and retry evidence before calling ULW a normal or forced choice.
- `Draw3.TransparentPresentation.cpp:822-842`: both DWM paths disabled. `:750-779` retains FLIP_SEQUENTIAL, optional waitable-chain then ordinary fallback. Selection state always presents via separate selection ULW surface (`:1130-1139`), even if main Drawpad is DComp. Record workspace/tool/selection and output target alongside active main mode.
- Existing UI3 prior log files under `Build/automation-perf/ui3-product-20261003` contain UI3 logger summaries but no Draw3 stdout startup lines found. They cannot establish Draw3 adapter/presenter. Native-desktop rendering spec separately documents UI3 shared WARP resource owner.

### Existing metrics and smallest product hookup

- `Draw3.Host.h:170-193`: HostStartOptions has `enableRuntimeMetrics=false`, max samples default32768. Product Main options at `IdtMain.cpp:2956` do not opt in. Runtime session exists but currently only explicit hidden fixtures opt in; no ordinary product CLI/log control found for it.
- `Draw3.Product.cpp:19`: `ProductHost()` exposes existing Host. Main normally calls StopProduct at `IdtMain.cpp:3229`. A temporary diagnostic harness can opt in existing options and call `ProductHost().WriteRuntimeMetrics(absolutePath)` after real Stop/join, then restore diagnostic enable before final build. No new benchmark framework or rendering backend choice needed.
- `Draw3.Host.cpp:470-486`: export rejects failed/unavailable/unjoined/unsealed session and relative paths; write is create-new. Caller must ensure isolated output root, then capture metadata/runtime snapshot separately since session JSON does not include Host adapter metadata. Restart loses prior run session; use unique file names per process/run.
- `Draw3.RuntimeMetrics.cppm:55-103`: actual-owner Ingress, ModelPrediction, GeometryRasterSubmit, Composite spans include wall + thread CPU. Parent and child spans overlap: do not add them. Each render attempt includes reason bits, physical contacts, terminals, actual Present success/return QPC. Laser spans distinguish incremental/bake/particle submit CPU work; actual GPU work is unavailable.
- `Draw3.DrawingController.cpp:7045-7089`: PresentFrame measures presenter call and records true return QPC before observation callbacks. `:13806-13830` records frame CPU/work spans before frame pacing; `:13843-13873` records active frame interval separately. Loop interval is not optical FPS.
- `Draw3.RuntimeMetrics.cpp:1084-1173`: raw `landings`, `frames`, `presents`, active intervals and costs are available without old phase216 acceptance. Host exporter uses the non-phase WriteJson overload. Group raw samples by run/operation and reason flags; do not block optimization on legacy phase goals.
- Down landing proof is matched to successfully submitted content and output generation (`RuntimeMetrics.cpp:597-609`), a software input-to-successful-Present proxy only. Export check contact/frame/present drop and invalid counts. A short run exceeding capacity should be shortened, not counted as complete evidence.
- `IdtMain.cpp:2387-2406`: ConsoleOutput.Draw3 enables startup adapter diagnostics only in !IDT_RELEASE. `Diagnostics.cpp:655-680` frame console logging requires _DEBUG, but vcxproj Debug undefines _DEBUG for fixed Modeler ABI. Do not expect Debug console to provide frame timing. Existing RuntimeMetrics is available in Release.

### Finite measurements and candidates (source leads, not demonstrated bottlenecks)

1. First stroke after startup/idle, continuous strokes, then same gestures over representative existing content. Inspect raw ingress/model/raster/composite/present distributions and down landing proxy. UI interactions must be outside drawing frame population. Keep drawing backend, viewport, tool settings and display unchanged.
2. Normal fixed/speed eraser, Laser complete active/hold/fade lifecycle, Undo/Redo or existing-content switch/restore. Use tool-specific populations and terminal frames; Laser display interval does not count as ongoing physical writing rate. Do not clear user data to create the scenario.
3. If Present dominates: ULW source `TransparentPresentation.cpp:405-463` = dirty GPU copy -> blocking Map -> CPU CopyAndInspectUlwDirtyRows -> USER32 update. Map may contain earlier raster execution waiting. DComp instead uses Present1 (`:1144`). Main DComp and selection ULW are different populations. At most a few temporary subspan timers to distinguish synchronous wait/copy/update; no new renderer or sync removal without proof. Earlier fused ULW copy regressed and must not be retried unchanged.
4. If history/raster CPU dominates: `InkHistoryGpu.cpp:920-1035` recursive cached nodes pin children then Acquire; full512 allocator improvement applies only node miss when cache has no usable empty slot. Hit and partial-fill paths do not receive that gain. `:828-845` resident lookup/removal still linear, but no optimization justified before real replay/cold-undo cost is measured. Do not claim microbenchmark speedup as normal firstDown gain.
5. If pacing/ingress dominates: `ContactInput.cpp:1130-1195` waits distinguish generation-sensitive idle wake from fixed active frame deadline with coarse wait + last1.25ms spin. Controller physical drawing waits to existing target_fps; do not remove pacing or drop samples merely to increase loop counts. Need actual blocked span/queue evidence before changes.

### Related specs and files

- `.trellis/spec/native-desktop/draw3-integration.md`: sole HWND/Host/RTS, independent devices, owned draw thread, DComp/ULW and persistence constraints.
- `.trellis/spec/native/runtime-and-rendering.md`: stable/live layers, dirty unions, model/input order; legacy presenter recommendations yield to current integration contract.
- `.trellis/spec/native-desktop/input-and-ink.md`: real input normalization and cursor logging; do not infer production behavior from Draw2 history.
- `.trellis/tasks/09-27-draw3-performance/research/composition-planner-full-miss.md`: previous allocator baseline/proof, no real miss-frequency evidence.
- External references: none needed for these repository-local facts; no external API performance assumption used.

## Caveats / Not Found

- Real current Draw3 adapter, presenter, fallback HRESULT and active target still need capture from this run; no inference from UI3 WARP/ULW.
- No liveproduct metrics control presently wired. Minimal existing-session opt-in/export is necessary if real per-frame stage evidence is required; this analysis added none.
- Current metrics contain CPU/wall and software successful Present, no GPU timestamp query or optical latency. Wall minus CPU alone does not prove GPU cost.
- No hotspot claimed as established, no visible-frame benefit measured by researcher. UI3 must finish or record specific blocker before Draw3 implementation.
- Do not retry UI3 sin/cos, failed target-capacity, pixel-unequal A8 or prior fused ULW-copy candidates. Win7 SP1/KB2670838 special ULW problem remains explicitly deferred; PPT/storage/default update/HTTP policies untouched.
## 后续补证
实际采样已确认UI3 WARP/ULW与独立Draw3 Adreno Hardware/DComp；真实输入与私有可见nonRTS三轮对照已保存，首visible候选无收益撤回，Compose无重复clear，临时源码全部恢复。当前Host JSON不导出精确terminal phase proof。最终生产仅UI3；以父任务automation-20261003.md最终段为准，前文待采集为研究时点。
