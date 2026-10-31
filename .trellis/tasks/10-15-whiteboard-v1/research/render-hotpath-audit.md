# Research: Whiteboard rendering hot path audit

- Query: Why can Whiteboard bottom controls and theme panels stall the main Bar despite using UI3 animation helpers? Audit the actual Scene/present route, activity lifecycle, resources, cache, and serial scheduler. No fixes.
- Scope: internal; current working-tree implementation at the reported d4d42869 implementation, plus existing runtime log.
- Date: 2026-10-15

## Findings

### 1. Confirmed cross-client stall mechanism

`Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cpp:53-62` defines dispatch order Bar, StartupPreview, four PageControl clients, Settings, WhiteboardFreeze. The scheduler invokes callbacks synchronously in one loop at `:809-859`; the next batch cannot start until all work in this batch completes. Its 16.67ms deadline is pacing, not a budget that preempts slow callbacks (`:738-740`). Slow PageControl rendering therefore delays the following Bar animation/pointer feedback frame. Sharing animation primitives does not isolate CPU/WARP rendering cost.

Main-session runtime inspection of `Build/ARM64/Debug/log/idt1792070038504.log:34-36` reports:

- PptBottomLeft: 26 calls, callback total916.541ms/max104.363ms, GetDC total794.385ms/max95.722ms; Draw command-recording total30.308ms/max5.220ms, target1244x856.
- Following samples: 43 calls with callback541.86ms/GetDC435.01ms, then 48 calls.
- Rounded-cache hit/miss/create in those intervals: 26/0/0, 43/0/0, 48/0/0, creation0ms. First interval maskDraws234, fallback_transform26: nine mask slices per call, not per-frame mask creation.

This proves actual slow UI rendering intervals and the mechanism that can affect Bar. It does not prove which individual D2D command is responsible: GetDC(COPY) flushes queued rendering and exposes synchronization/readback as well as rendering completion cost. Do not interpret the Draw timer as total rasterization time. A separate Settings callback590.499ms and following Bar gap599.795ms at21:14:59 must not be attributed to Whiteboard.

### 2. Confirmed divergence from canonical Bar: damage is only used at presentation

`PageControl.cpp:1197-1204` begins D2D drawing, clears the entire target, and calls Scene.Render before even fetching PendingDamage. At `:1220-1227`, an empty damage set falls back to the full presentation rectangle. `:1279-1306` calls GDI GetDC(COPY) and submits ULW using the calculated damage. Scene.Render (`Bar.Scene.cpp:2510-2623`) walks all visible panels/widgets and draws them, with no frame dirty clip.

In contrast, canonical `Bar.RenderLoop.cpp:10266-10276` calls spec.PushFrameDirtyClip(presentDirtyRect) immediately after BeginDraw; subsequent Clear and draw commands are clipped. This is a concrete rendering-pipeline omission, although individual buttons reuse DrawBarButtonVisual.

`ResolveStableBackingSize` in `PageControl.cppm:986-1010` deliberately retains high-water capacity; RenderSurface `PageControl.cpp:1980-2024` allocates from the conservative popup envelope. Keeping capacity is reasonable, but unconditional full-target Clear means a tiny hover/light update after popup expansion still touches the retained large buffer. Runtime target1244x856 corresponds to over1million pixels, while the ordinary left bottom body at2x is approximately610x160 before outsets. Presentation crop does not clip D2D work to that crop.

### 3. Confirmed extra opacity-layer structure; cost magnitude unmeasured

`Bar.Scene.cpp:2547-2555` uses one surface-opacity D2D layer. `:2575-2613` separately uses PushLayer(InfiniteRect) for each widget whose inherited popup opacity is below1. Children inherit parent opacity recursively (`:576-583`), but the panel is drawn in an earlier pass and its layer is popped before children are drawn. During theme-popup animation this can mean layers for panel, three menu rows, second panel, nine preview buttons and headers/divider, rather than one compositing layer per whole popup subtree.

Canonical EraserAttribute Draw (`Bar.EraserAttribute.cpp:413-436`) composites its whole panel in one opacity layer, draws all its children, then pops it; its submenu has its own group. The difference is structural. D2D can optimize allocations, so the source alone does not establish full-target allocation for every layer or exact milliseconds. It is a prioritized performance hypothesis given WARP and observed GetDC flush cost.

### 4. Resource ownership does not support the alternating-context-recreation hypothesis

`Bar.Scene.cpp:534-539` owns a separate BarUISetClass rendererOwner for each Scene. `Bar.Rendering.cpp:142-153` compares device generation and its own context, target and width/height. RecreateDeviceResources `:162-186` calls epoch.d2dDevice.CreateDeviceContext and creates a private target bitmap; it sets that new context's target and moves it into that renderer instance. `Bar.Scene.cpp:2199-2214` uses exactly this owner for resource checks and resets scene caches only after actual successful recreation.

Bar, four PageControl scenes and Freeze share the device/epoch and WARP execution resources, not one D2D device context/target. No evidence that merely switching callbacks invalidates each other's target or creates a new target each frame. Epoch/size changes and actual failures do recreate resources.

### 5. Two different kinds of rounded-rectangle cache must be distinguished

**Theme preview clip**: `Whiteboard.Theme.h:10-15` holds a context reference, solid brush, rounded clip geometry and bounds. `:24-37` resets on context change and creates geometry when bounds change; drawing uses that clip and a solid rectangle/texture. `PageControl.cpp:795-800` supplies one shared resource record per preview callback; `Bar.Scene.cpp:798-805` releases such resources on device-cache reset. This is normal device-resource reuse, not another animation engine. Press scaling uses matrix transforms, so it need not change logical clip geometry every press frame. BuildWidgets does allocate fresh callback/resource wrappers on state publication, but UpdateContent only replaces the stored callback on contentRevision change (`Bar.Scene.cpp:1535-1555`). Stable content does not discard the active preview clip every frame.

**Renderer lighting masks**: `Bar.Rendering.cpp:1514-1547` caches point-light rounded-rectangle Gaussian masks by quarter-pixel radius/stroke/blur parameters. Main Bar and Eraser draw supply inverse animation geometry scale before drawing (`Bar.EraserAttribute.cpp:413,429,432,436`; Bar.RenderLoop many property routes). Scene.Render instead changes renderer frame zoom to dpi*popupPose.scale (`Bar.Scene.cpp:2581`) without setting inverse FrameDiffuseMaskGeometryScale. This can generate more mask shapes during animation. It is a real integration difference, not the demonstrated cause of the slow intervals: runtime slow intervals above had zero mask misses/creation.

Scene also never calls PushFrameDirtyClip. That API is the only frameDiffuseMaskFrameSerial increment (`Bar.Rendering.cpp:258-276`), while the optional exact-mask optimization cannot promote with serial0 (`:1902`). Scene therefore lacks this optimization even where the other eligibility checks could pass. In this runtime, however, fallback_transform triggers first (`:1847-1859`) because Scene applies presentation translation; no claim that adding frame serial alone would fix these observed calls.

### 6. Activity/wake lifecycle: duplicated wake, but no confirmed endless oscillator

Scene.Advance (`Bar.Scene.cpp:2479-2494`) calls hooks.wake while active. Render in the same frame calls Advance again but is idempotent by frameTime (`:1135`); Render also calls wake if active (`:2621`). The caller returns Continue independently (`PageControl.cpp:2030-2033,2202`). Requests are bitmasks, so these extra requests generally coalesce, though they can leave an extra callback at the end. This is not proof of an endless loop.

Main Bar publishes shared lighting (`Bar.RenderLoop.cpp:8158-8185`); Scene publication checks equivalent lighting and then synchronously updates subscribed scenes (`Bar.Scene.cpp:2074-2099`). Primary-light changes mark full damage (`:826-836`); cursor changes compute intersecting border damage (`:724-754`). Thus Bar lighting can require PageControl frames, which then execute the unbounded redraw route above. Hidden PageControl clients unsubscribe when not transitioning (`PageControl.cpp:1935-1944`).

The historical icon opacity oscillation fix is present: both ApplyButtonInteractionTargets and ApplyButtonTargets now agree on no-icon/unselected-indicator opacity0 (`Bar.Scene.cpp:1103-1124`, Retarget helper `:405-410`). Pure Whiteboard content changes use UpdateContent, not TransitionLayout (`PageControl.cpp:1034-1040`). PublishWhiteboardState compares full state before revising/waking (`:3337-3352`). No concrete continuing target1→0 oscillation was found. The shared popup clock suspends when inactive (`Bar.Scene.cpp:1241-1243`), while non-popup dt uses a separate clamped lastFrame delta (`:1135-1143`); there is still an integration difference from canonical full FrameAnimationClock handling, but not a demonstrated stutter cause.

### 7. Other blocking work and background redraw

Each visible PageControl callback calls Window Service SetBounds then Show after ULW (`PageControl.cpp:2132-2141`); even hidden surfaces call Hide. Service methods always submit owner-thread commands (`Window.cpp:362-412`). Submit queues a promise and waits for owner completion (`:1386-1408`), so static/unchanged HWND commits add synchronous crossings. They should be measured separately before claiming primary root cause. Existing PresentLockWait timers measure lock acquisition, not the entire owner-command wait.

Whiteboard Freeze rendering (`Whiteboard.cpp:140-239`) is another serial client, full-screen Clear/Draw/GetDC/ULW and synchronous SetBounds/Show. Static background is not subscribed to shared lighting and does not request per-stroke continuous refresh: requests originate from entry/active change, display changes, theme change (`:272-276,313-322,331-350`). It is a potential single theme-change stall, not a proven permanent fullscreen60FPS loop. DrawTheme uses no persistent resource record for fullscreen background (`:119`), so its brush is recreated on each actual background callback; the number of such callbacks needs runtime evidence.

## Files found

- Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cpp — serial scheduler, dispatch order and Continue/Retry logic.
- Inkeys/Inkeys/UI/PageControl/PageControl.cpp / .cppm — surface configuration, snapshots, conservative backing, D2D and ULW presentation.
- Inkeys/Inkeys/UI/Bar/Bar.Scene.cpp — new standalone shared surface adapter, animation traversal, child pose inheritance and rendering.
- Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp — private device resources, lighting mask caches, dirty clip and exact-mask optimization.
- Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp — canonical Bar frame dirty clip and property rendering integration.
- Inkeys/Inkeys/UI/Bar/Bar.EraserAttribute.cpp — canonical grouped popup opacity and mask scale usage.
- Inkeys/Inkeys/UI/Whiteboard/Whiteboard.cpp / Whiteboard.Theme.h — static backdrop renderer and custom theme preview painter/cache.
- Inkeys/Inkeys/Window/Window.cpp — synchronous owner-thread window commands.
- Build/ARM64/Debug/log/idt1792070038504.log — existing runtime diagnostic evidence supplied/inspected by main session; not newly generated by this audit.

## Related specs

- .trellis/spec/native-desktop/rendering-and-ui.md — existing dirty-region/idle/device ownership contracts and UI3 render route.
- .trellis/spec/native-desktop/whiteboard.md — unified menu animation, independent cache/resource ownership and single-frame geometry claims.

## External references

None. All findings are based on current production source and existing logs. No web lookup, GUI action, build, test execution, code edit, or Git operation was performed.

## Caveats / Not Found

- This audit records mechanisms and observed timing, not a measured causal A/B profile. Exact contribution from Clear, opacity-layer compositing, point-light nine-slice masks, GetDC copy, and scheduling contention remains to be separated.
- No D2D resource identity/counter dump, ETW trace, per-preview draw timer or API readback-size measurement exists in the examined log.
- No evidence of shared D2D context target thrashing, fullscreen background continuous redraw, permanent icon-target oscillation, or mask-creation-driven slow samples was found. Do not present these as established root causes.
- Passing isolated offscreen Scene tests can validate geometry/pixels yet misses serial multi-client WARP latency and real GetDC/ULW/owner-thread stalls.
