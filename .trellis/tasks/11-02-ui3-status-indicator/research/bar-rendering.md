# Research: UI3 Bar rendering for main-button status indicator

- **Query**: How the UI3 main button (主按钮) is built/drawn/lit, draw order, theme/opacity, animation & frame scheduling, window viewport/dirty/hit-test, button hover/press, and reusable badge/indicator concepts — to plan a status ring + capsule/circle stack on the main button.
- **Scope**: internal
- **Date**: 2026-11-02

All paths relative to `Inkeys/Inkeys/UI/Bar/` unless stated. Line numbers are from branch `feature/state` @ 6ee09764.

---

## 1. Main button geometry, border and edge lighting

### Object & geometry

| Item | Location |
|---|---|
| Single superellipse key `BarUISetSuperellipseEnum::MainButton` (only member) | `Bar.Main.cppm:412-415` |
| Class `BarUiSuperellipseClass` (x/y = **center**, w/h, `n` exponent, `ft` border width, `fill`/`frame` colors, `framePct`, `lightMaterial`, `themeSurface`, `residentSurface`, `frameRendering`, `frameLightColor`, `framePrimaryLightEnabled`, cursor-light scales) | `Bar.UI.cppm:269-301` (base `BarUiInnheritBaseClass` `:156-190`, `inhX/inhY` = drawn top-left) |
| Creation: `make_shared<BarUiSuperellipseClass>(mainX, mainY, 80, 80, n=3.0, ft=1.0, Surface, SurfaceFrame)`; `pct=BarMainButtonCollapsedFillOpacity (0.6)`, `framePct=0.18`, `frameRendering=PointLight`, `frameLightColor=PenWhenDrawing`; logo SVG `logo1` (h=80) | `Bar.Initialization.cpp:344-362` |
| Theme flags: `themeSurface=true`, `lightFillOpacityScale=LightResidentSurfaceOpacityScale(1.10)`, `residentSurface=true`, fill/frame stored as **Dark** endpoints | `Bar.Initialization.cpp:1318-1324` |
| Metrics: `BarMainButtonWidthDip/HeightDip = 80`, `BarMainButtonCollapsedFillOpacity = 0.6`, `BarMainButtonToMainBarGapDip = 10`, `BarButtonFrameThicknessDip = 1` | `Bar.Metrics.cppm:31,56-59` |
| Per-frame targets (RenderLoop `SubmitTargetsAndLayout`): w/h `SetTar(80)`; click pulse = middle keyframe `80*1.05` with `EaseOutBack/EaseInBack`; folded: `n→3.0`, `pct→0.6`; expanded: `n→10.0`, `pct→0.8`; fill/frame re-`SetDirect` to Dark endpoints | `Bar.RenderLoop.cpp:1602-1689`; constants `mainButtonScale=1.05`, `mainButtonBaseSize=80` at `:896-897` |
| Position helpers (`GetMainButton`, `SetMainButtonPosition`) | `Bar.Zoom.cppm:36-80` |
| Top-left inherit computed right before drawing: `UpInh(x.val - w.val/2, y.val - h.val/2)`; MainBar inherits `Center` from main button | `Bar.RenderLoop.cpp:9934-9941` |
| Path generation `Inkeys::UI::CreateSuperellipsePath` | `Inkeys/Inkeys/UI/Superellipse.Geometry.h:13` |
| Hit-test math `BarUiSuperellipseContainsPoint` | `Bar.UI.cpp:174`, `IsClick` `:202` |

Note: the button shape is **not static** — `n` animates 3↔10 and fill pct 0.6↔0.8 with fold, and w/h pulse to 84 on click. An overlay ring that "covers the superellipse border" must read the current `w.val/h.val/n.val/inhX/inhY` each frame.

### Draw implementation (D2D, not shader, not ImGui)

`BarUIRendering::Superellipse` — `Bar.Rendering.cpp:2962-3078`:
- Geometry from single-entry cache `GetSuperellipseGeometry(x,y,w,h,n,segs)` (`:2917-2961`; cache struct `Bar.Rendering.cppm:306-316`, member `:528`). Note it caches ONE path; drawing a second superellipse of different size/n each frame will rebuild the path each call.
- `clip=true` first fills the geometry with transparent `D2D1_PRIMITIVE_BLEND_COPY` (erases whatever was under it) (`:3001-3009`).
- Fill: `themeSurface ? material.surface : fill`, opacity `pct * ResidentFillOpacityScale(...)` (`:3016-3027`).
- Border: `themeSurface ? material.surfaceFrame : frame`, `framePct * material.frameOpacityScale`; stroke width `ft * frameZoom`; `PointLight` → `DrawPointLightFrame(..., geometry, geometryVariantQuarter=lround(n*4))`, fallback solid `DrawGeometry` (`:3029-3065`).
- Dirty rect via `BarRenderingAttribute::GetWeigetRect(superellipse, zoom)` (`Bar.RenderingAttribute.cppm:59-67`), outset = `ft + pointLightDiffuseExtraWidth(6 DIP)` (+ `SurfaceShadowOutsetDip=8` for themeSurface) + `dirtyAntialiasPadding=3` (`:14-15,34-45`).

### Edge lighting (PointLight) — `BarUIRendering::DrawPointLightFrame`, `Bar.Rendering.cpp:2502-2713`

All D2D: base flat stroke + two radial-gradient light passes (primary "第一光源", cursor "第三光源") + Gaussian "diffuse" pass using a cached pre-blurred A8 mask of the geometry (`GetGeometryDiffuseMask` `:2299-2477`, cache keyed by w/h/stroke/variant quantized to quarters, max 24 entries `:2471`).
- Light color: `material.edgeLight = <this object's drawn frame color>` then `BarThemeMaterial::ResolveLighting / ResolveCursorLighting(material, frameDrawingPenColor, penColorBlend)` (`:2525-2529`). So passing a colored frame color (e.g. status color) produces colored diffuse light automatically, but when `frameLightColor == PenWhenDrawing` it blends toward pen color during drawing.
- `penColorBlend = frameDrawingPenColorBlend` only if `frameLightColor == PenWhenDrawing`, else 0 (`:2520-2523`). Opacity multiplied by `frameDrawingLightOpacity` (mode-switch fade) in that case (`:2532-2537`).
- Gates: `BarUiEdgeLightingEnabled && frameEdgeLightingEnabled` (`:2547`); primary drawn only if `framePrimaryLightAnchorInitialized && primaryLightEnabled` and light circle intersects bounds (`:2573-2578`); cursor light only if `frameCursorLightVisible` (`:2579-2585`).
- Diffuse geometry scale for animated sizes: `SetFrameDiffuseMaskGeometryScale(1/scale)` pattern (e.g. `Bar.RenderLoop.cpp:12463-12470`).

### "First light source" (主光/第一光源) and drawing-mode detection

`BarUIRendering::PrepareFrameLighting`, `Bar.Rendering.cpp:669-1174`, called once per frame from `BarRenderLoopCoordinator::PrepareLightingAndDemand` (`Bar.RenderLoop.cpp:7673-7704`) with `stateMode`, `penMode`, brush1/laser color, highlighter color.
- Primary anchor = the button of the current tool: `IdtPen→Draw`, `IdtEraser→Eraser`, `IdtShape→Geometry`, `IdtSelection→Select`, `IdtClickThrough→Pierce`, default/unavailable → `MainButton` (bottom-center of main button) (`:779-806`; enum `Bar.Rendering.cppm:46-54`; anchors from `services_.mainLightingInput` `Bar.Rendering.cpp:181-215`).
- Pen-color lighting active ⇔ `frameDrawingUsesPenColor = (drawingMode == IdtPen || drawingMode == IdtShape)` (`:807-808`). Target `frameDrawingPenColorBlend` = 1 when drawing else 0, animated with a mid-fade (`frameDrawingLightOpacity` dips to 0 at 50%) over `BarUiDefaultOperationDur` (`:820-893`).
- Readable state: `SnapshotFrameLighting()` → `BarUiFrameLightingSnapshot{ drawingPenColor, drawingPenColorBlend, drawingLightOpacity, primaryLightVisible, cursorLightVisible, edgeLightingEnabled, ... }` (`Bar.Rendering.cpp:550-568`, struct `Bar.Rendering.cppm:57-71`). Already used in `PrepareLightingAndDemand` (`Bar.RenderLoop.cpp:7706-7725`).
- Equivalent business-state check used by logo: `BarLogoAppearance::IsDrawingMode(pen, geometry)` (`Bar.RenderLoop.cpp:1606-1608`, header `Bar.LogoAppearance.h`).
- Config gates: `SetEdgeLightingOptions(enable, dynamic)` → `BarUiEdgeLightingEnabled / BarUiDynamicEdgeLightingEnabled` (`Bar.Main.cpp:337-344`). Enable=false: no primary/cursor/Gaussian, base stroke still drawn; Dynamic=false: only cursor light off (spec `rendering-and-ui.md` "UI3 边缘光影实验开关契约").
- Light-theme lighting constants: diffuse Light 0.08/0.06 vs Dark 0.30/0.20, intensity ×1.5, radius scales 360/480 & 200/240 (`Bar.ThemeMaterial.h:135-145`, `Resolve` `:174-205`, `ResolveLighting` `:255-274`, `ResolveCursorLighting` `:276-293`).

---

## 2. Draw order / z-order

Single frame in `BarRenderLoopCoordinator::CalculateDirtyAndDrawPresent` (`Bar.RenderLoop.cpp:7799-~13150`). After `BeginFrameDraw` (`:9918`, sets dirty clip):

1. BackgroundWarning word (`:9920-9926`)
2. Main-button inherit precompute (`:9930-9941`)
3. `SetRigidTransform()` → DrawAttribute panel + all its children (`:9943-~11290`), Geometry attribute panel (`~:11290-11431`)
4. Eraser + Recall attribute panels: `owner_.eraserAttribute.Draw`, `owner_.recallAttribute.Draw` (`:11434-11437`)
5. More panel (if visible) (`:11468-11533`), then `DrawMainBar()` (lambda `:11440-11466`: `SetBodyTransform`, `DrawBarBackgroundVisual(..., clip=true)`, buttons with `SetResidentContentOpacity`)
6. **Main button**: `SetGripTransform(); state.spec.Superellipse(..., &state.current, clip=true)` then logo1 SVG with `SetResidentContentOpacity(GetThemeResidentContentOpacity())` … reset to 1 (`:11539-11554`)
7. Dirty union of More/attribute children (`:11555-11650`)
8. Thickness tooltip/annotation/overflow popups ("浮窗…徽标随后覆盖") (`:11654-12297`)
9. Thickness preview popup + slider thumb (`:12299-12419`)
10. `state.current = currentContentBounds; SetBaseTransform();` bottom-dock target indicator ("前景指示器覆盖全部 Bar/面板内容") (`:12421-12510`)
11. Debug FPS text / debug frames (`:12511-12600`)
12. `FinishFrameDrawCommands` (`:12603`) → GetDC/ULW/ReleaseDC/EndDraw.

To draw above the main button: insert right after step 6 (inside the same `SetGripTransform()` so it follows the button's rigid-grip + bottom-dock Y-jelly transform), or later (before step 8 if popups should stay on top; after step 9/10 to be above everything). Transforms: `SetBaseTransform/SetBodyTransform/SetGripTransform/SetRigidTransform` lambdas at `:9849-9888` (grip = rigid horizontal X + body scaleY/translationY; each also sets cursor-light local geometry and font target scale). Main button must use **Grip**; dirty bounds of superellipses are observed with `BottomDockBoundsTransform::Grip` (`:8843-8847`).

Note `Superellipse(..., clip=true)` erases underlying pixels inside the shape, so anything drawn before the main button in that area disappears.

---

## 3. Theme, background opacity, content opacity

| API | Location | Semantics |
|---|---|---|
| `GetThemeColor(BarThemeColorEnum)` / `(bool darkStyle, ...)` / `(BarThemeModeEnum, ...)` | `Bar.Theme.cppm:126-144` | Role colors; default overload follows current `barStyle.darkStyle` (target, not animated) |
| Role enum (Surface, SurfaceFrame, TextPrimary, Accent, PressedFill, SubtleFill, SwatchFrame, DockTarget, IconPrimary, SelectedFill, Divider, EdgeLight, ...) | `Bar.Theme.cppm:23-40`; Dark values `:91-110`; Light values `Bar.ThemeMaterial.h:41-64` |
| `BarThemeMaterial::Resolve(lightWeight, darkSurface, darkFrame)` | `Bar.ThemeMaterial.h:174-205` | continuous material; consume animated weight |
| Animated weights: `state.barLightMaterial` (bar-wide, target `requestedDarkStyle?0:1`, `BarUiDefaultOperationDur`) and `state.mainButtonLightMaterial` (main button: **0 when folded even in Light theme**; 1 only Light+expanded) | `Bar.RenderLoop.cpp:1563`, `:2623-2625`, `:698`; applied to objects `:5124-5143` |
| `GetThemeResidentBackgroundOpacityOverride()` (user %/100, 0=theme default) | `Bar.Theme.cppm:150-154` |
| `GetThemeDefaultResidentBackgroundOpacity(darkStyle)` → Dark 0.80 / Light 0.88 | `Bar.Theme.cppm:157-160`; `DefaultResidentFillOpacity` `Bar.ThemeMaterial.h:100-104` |
| `GetThemeResidentContentOpacity()` (user content %/100) | `Bar.Theme.cppm:163-167` |
| `GetThemeResidentSurfaceOpacity(darkOpacity)` (for surfaces not using material system) | `Bar.Theme.cppm:170-175` |
| `GetThemeSurfaceFrameOpacity(darkOpacity)` (frame ×1.75 in Light) | `:178-182`; `FrameOpacity` `Bar.ThemeMaterial.h:112-115` |
| `GetThemeDisabledContentOpacity()` | `:184-188` |
| `BarThemeMaterial::ResidentFillOpacityScale(lightWeight, lightScale, absoluteOverride)` | `Bar.ThemeMaterial.h:91-97` |
| `BarUIRendering::SetResidentContentOpacity(x)` — multiplies **only** `Svg()` (`Bar.Rendering.cpp:3108-3109`) and `Word()` (`:3293`) opacity; Shapes/Superellipses/manual brush draws are NOT affected; caller must reset to 1 | `Bar.Rendering.cppm:174-177` |
| Settings → bar: `SetThemeMode` / `SetResidentOpacity(bg%, content%)` publish atomics + wake | `Bar.Main.cpp:346-366` |
| Render thread applies `requestedDarkStyle` / `requestedBackgroundOpacity` / `requestedContentOpacity` at frame start; opacity change forces full damage (no animation) | `Bar.RenderLoop.cpp:1529-1563` |
| Storage `BarStyleClass{ requestedDarkStyle, darkStyle, requestedBackgroundOpacity, requestedContentOpacity, backgroundOpacity, contentOpacity }` | `Bar.State.cppm:123-139` |

Pattern for an ad-hoc themed resident surface drawn without a map entry (bottom-dock indicator): temporary `BarUiShapeClass` with Dark Surface/SurfaceFrame, `themeSurface=true`, `lightFillOpacityScale=LightResidentSurfaceOpacityScale`, `residentSurface=true`, `lightMaterial=state.barLightMaterial.val`, `frameRendering=PointLight`, `frameLightColor=PenWhenDrawing`, `SetFrameDiffuseMaskGeometryScale(1/scale)` (`Bar.RenderLoop.cpp:12440-12470`). Text there uses `GetThemeColor(TextPrimary)` + `GetFrameSolidColorBrush` (`:12497-12500`).

Spec contracts: `.trellis/spec/native-desktop/bar-theme-material.md` §3 (resident surfaces: main bar, main button, dock indicator; content opacity only for resident content and must reset to 1; edge light uses object's own frame color; dirty must use Dark-radius conservative bounds).

---

## 4. Animation infrastructure & frame scheduling

| Item | Location |
|---|---|
| Globals `BarUiDefaultOperationDur (=0.4 s)`, `BarUiAnimationEnabled`, `BarUiAnimationSpeedRate`, `BarUiDefaultDes=600` | `Bar.Animation.cppm:25-28`, defs `Bar.Animation.cpp:19-23`, `BarButtonDefaultOperationDurationSeconds` `Bar.Metrics.cppm:46` |
| `SetAnimationOptions(enable, speedRate)` — clamps 0.1..5; disabled ⇒ `SpeedRate=1e12` (instant completion), suspends cursor light, wakes Bar + PptPageMask | `Bar.Main.cpp:326-335` |
| Curves `BarUiCurveEnum` (Sine/Cubic/Back variants), `BarUiApplyCurve` (Back coefficient 1.1), extrema `BarUiGetCurveExtrema` (for envelope reservation), `BarUiApplyCurveRange` | `Bar.Animation.cppm:39-135`, `:419-433` |
| `BarUiCurveSpecClass`, `BarButtonPressCurve()` (EaseOutCubic), `BarButtonReleaseCurve()` (EaseOutBack) | `Bar.Animation.cppm:435-455` |
| `BarUiTimelineClass` (batch timeline, `CanJoin(0.5)`) and `BarUiKeyframeTimelineClass` | `Bar.Animation.cppm:457+`, `:~540-660` |
| `BarUiValueClass::SetTar(tar, dur, middleV, forceRestart, curveSpec)` — optional single middle keyframe (used for pulse/overshoot), `SetDirect`, `IsSame` | `Bar.Animation.cppm:775-860`; `BarUiColorClass` `:895-966`; `BarUiPctClass` `:968-1067` |
| `BarUiAdvanceAnimation(value, {dt, speedRate, animationEnabled, forceReplace})` → `{changed, active}` | `Bar.Animation.cppm:1069-1090` |
| Reusable open/close motion with Back geometry + Sine opacity: `BarAttributePanelMotion` (`Retarget/Advance/Geometry/Opacity/Visible`), pose helpers `ResolveEraserSurfacePose`, `FitEraserSurfacePose` (clamp into work area), `ResolveEraserPopupEnvelope` (Back-extrema envelope) | `Bar.EraserAttributeMotion.cppm:10-140` |
| True damped spring (ω=18, ζ=0.72, 120 Hz substeps) `AdvanceBarBottomDockSpring`, `BarBottomDockSpringState{positionDip, velocity}` | `Bar.BottomDock.h:21-24`, `:1658-1720` |
| Indicator-style state machine with absolute deadline + independent visual demand (good template for delayed show/hide): `PageLoadingIndicator::Advance/NeedsFrames/WakeDeadline`, `PageLoadingDeadline` | `Bar.PageLoadingIndicator.h:1-74`; spec `rendering-and-ui.md` "Scenario: Page 右上角延迟环形指示器" (L2727+) |

Per-frame pipeline `BarRenderLoopCoordinator::RenderFrame` (`Bar.RenderLoop.cpp:13205-13675`): `WakeAndSnapshot` (`:1163`) → `ApplyDisplayTransition` (`:1228`) → `SubmitTargetsAndLayout` (`:1521`, sets targets) → `AdvanceAnimationsAndDeriveLayout` (`:5046`, advances; lambdas `ChangeValue/ChangePct/ChangeColor` mark `needRendering` and `dirtyRegionTracker.MarkChanged(key)` `:5079-5118`) → `PrepareLightingAndDemand` (`:7673`, builds demand; `hasActiveRendering` `:7748-7755`; unclassified damage fallback `:7786-7791`) → `CalculateDirtyAndDrawPresent` (`:7799`) → `PaceFrame` (`:13152`, ≤60 FPS).
- Idle: when no demand, returns Idle and calls `animationClock.SuspendForIdle()`; first wake frame has dt=0 (spec "UI3 idle 唤醒动画时钟合同").
- Wake from other threads: `barUISet.UpdateRendering(false)` → `BarAtomic::wait.Notify()` + `RenderPipeline::Request(Client::Bar)` (`Bar.Main.cpp:287-314`).
- Any new animated value must be advanced in `AdvanceAnimationsAndDeriveLayout` and must `MarkChanged` a dirty key, otherwise the frame is treated as unclassified (full damage) or the loop idles.

### Dirty region / layered window update

- Tracker `BarDirtyRegionTracker` (`Bar.DirtyRegion.h`), fixed group keys `BarDirtyFixedVisual { MainGroup, DrawAttributeGroup, GeometryAttributeGroup, MoreGroup, PrimaryLight, CursorLight, DockTargetIndicator, EraserAttributeGroup, RecallAttributeGroup }` (`Bar.RenderLoop.cpp:407-418`), per-object key = pointer (`:420-430`).
- Observe pattern for a self-drawn group (eraser): `Observe(key, bounds)` + intersect bounds with primary/cursor light influence (`Bar.RenderLoop.cpp:8981-8993`). Main group superellipses observed at `:8843-8847`.
- Light damage: `ResolveBarLightBorderDamage(outer, content, lightInfluence)`; dock indicator example `:8963-8979`.
- Present: `GetDC(COPY) → UpdateLayeredWindowIndirect(pptDst/psize/pptSrc/prcDirty) → ReleaseDC → EndDraw` (ULW blend `AC_SRC_ALPHA`, `ULW_ALPHA` `:1153-1160`); only full success commits damage, hits and viewport (spec "UI3 基于变化的脏区事务合同", `rendering-and-ui.md` L607-858).

---

## 5. Window bounds / viewport / hit-test

- **Layout space = monitor**: `barWindow.w/h = monitor bounds` (`Bar.Initialization.cpp:290-291`, updated `Bar.RenderLoop.cpp:1337-1338, 1385-1387`). All `inhX/inhY` are monitor-local DIP; `frameZoom` converts to px.
- **Real HWND = dynamic viewport** sub-rect: `visibleContentBounds` is the union of visible shapes/panels (`IncludeShapeBounds/IncludeVisibleBounds`, incl. `eraserAttribute.Bounds()` `:8346-8394`), only recomputed when `collectVisibleContentBoundsForWindowSizing` (else cached `:8545-8549`). `currentContentBounds` (`:9057`), `layoutBounds = {0,0,barWindow.w,barWindow.h}` (`:9060-9062`).
- `reserveAnimationEnvelope` list of active animations (`:9101-9132`) → `predictedEnvelope` (Back-extrema swept envelope) (`:9134-9527`), intersected with `layoutBounds` (monitor) unless dock-drag (`:9526-9527`).
- `candidateViewport = viewportController.Resolve(currentContentBounds, predictedEnvelope, layoutBounds, padding=2, settle, translation)` (`:9559-9561`); `ClipBarViewportForTaskbarEdge` (`:9562-9566`); capacity `ResolveBarWindowCapacity` (`:9584-9612`; impl `Bar.WindowGeometry.h:440`, controller `:489-567`).
- Consequence: content can extend anywhere inside the monitor; the HWND grows to include it **only if** its bounds are added to `visibleContentBounds` (and its animation listed in `reserveAnimationEnvelope` so the first frame reserves the Back peak). Spec rule: only animations that change the top-level outer frame expand viewport; plain hover/press/light must not (`rendering-and-ui.md` L725+).
- Precedent of "popup exceeds main bar area": eraser attribute panel/menu computed with `FitEraserSurfacePose(pose, rect, input.work)` where `work` = monitor work area in layout DIP (`Bar.EraserAttribute.cpp:196, 269-272`), bounds via `BarEraserAttributePanel::Bounds()` (`:449-460`, padding = frame + `pointLightDiffuseExtraWidth` + AA). Draw uses `PushPanelOpacity` + `PushAxisAlignedClip` (`:404-447`).
- **Click-through**: Bar has no `WM_NCHITTEST` handler (only PageControl/Setting/MessageBox do — grep). The layered ULW window relies on per-pixel alpha: alpha=0 pixels pass input through (standard Win32 layered-window behavior; inference, not stated in spec).
- **Logic hit-test** uses the last *presented* snapshot: `PreparePresentedButtonHits()` collects every `shapeMap` and `superellipseMap` entry (superellipse uses bounds + exponent) (`Bar.Button.cpp:2505-2538`), `PublishPresentedButtonHits()` after successful present (`Bar.Button.cpp:2540-2544`, called `Bar.RenderLoop.cpp:13032`). Queries: `HitTestPresentedShape/Button` (`Bar.Button.cpp:2485-2503`), `IsPresentedBarVisualHit(owner, shape, x, y)` seqlock wrapper (`Bar.Interaction.cpp:196-207`). An element not in `shapeMap`/`superellipseMap` needs its own presented-hit snapshot (eraser keeps `PresentedRegions()` `Bar.EraserAttribute.cpp:469-473`).
- Main button input: `IsPresentedBarVisualHit(barUISet, MainButton, gripMessage.x, gripMessage.y)` with grip inverse mapping; Down → `Seek` (drag) → if `allowClick`, `TryBeginToggle(Main)` then toggle `fold`, `mainButtonClickPulseSerial++`, `SuppressHoverUntilPointerMove()` (`Bar.Interaction.cpp:6099-6142`). A new hit target over the main button must be tested before this block or it will be swallowed (`continueFlag=false`).

---

## 6. Button hover / press / click

- Shared helpers (`Bar.Main.cppm:198-232`, impl `Bar.Button.cpp:1781-1960`):
  - `StartBarButtonHoverVisual(pct, fill, stage)`: fill→`PressedFill`, pct→`BarButtonHoverOpacity 0.18` over `BarButtonHoverTransitionDuration 0.24 s`, EaseOutSine, stage `Showing` (`:1781-1796`).
  - `StopBarButtonHoverVisual(..., immediate, preserveVisual)` (`:1798-1824`); `UpdateBarButtonHoverVisual` Showing→Fading over `BarButtonHoverFadeDurationSeconds 5 s` (`:1852-1895`).
  - `SetBarButtonPressedVisual`, `RetargetBarFixedButtonVisual(parts, visible, enabled, selected, pressed, duration, ...)`: pressed pct `0.10` (Light selected ×0.6), `pressScale→0.95` with `BarButtonPressCurve/ReleaseCurve`, PointLight frame with `framePrimaryLightEnabled=false`, cursor-light scales 0.30/0.40 (`:1904-1960`).
  - Metrics `Bar.Metrics.cppm:37-46`.
- Independent (non-list) hover targets: `IndependentHoverTargetEnum` + `HoverVisualRef{pct, fill, stage}` + `GetIndependentHoverVisual` + `StartIndependentHover/StopIndependentHover` (`Bar.Interaction.cpp:3444-3475`, `:4002`, `:4190-4206`).
- **No hover after click until pointer moves**: `SuppressHoverUntilPointerMove()` stores screen point (`Bar.Interaction.cpp:4207-4215`); `WM_MOUSEMOVE` with identical screen point is consumed (`:5219-5232`). Worker-side equivalent `ButtonPointerState::Update` (Down clears suppression; Up sets it with screen point; Move with same point keeps hover 0) (`Bar.InteractionWorker.h:132-181`).
- Spec: "UI3 按钮悬停与按压衔接约定" (`rendering-and-ui.md` ~L1432-1445): press first then `StopHover(preserveVisual=true)`; leave uses non-immediate stop; validate "抬起后未移动指针时不重新触发悬停". Also "UI3 按钮边框光影约定" (frameLightPct separate from pct). New buttons must also honour pen haptics (`pen-haptics.md`, spec L2475).
- **Font**: `BarUIRendering::Word(..., fontWeight = DWRITE_FONT_WEIGHT_BOLD default, ...)` (`Bar.Rendering.cppm:104`); family `fontFamily` default `L"HarmonyOS Sans SC"` (`Bar.Rendering.cppm:524`), replaced per locale by `ConfigureLocalizedTypography()` (`Bar.Rendering.cpp:291-301`); `BarButtonDrawOptions::primaryWeight = BOLD` (`Bar.Main.cppm:243-253`). Manual text: `state.barMedia.formatCache->GetFormat(state.spec.GetFontFamily(), size, context.assets.fontCollection.Get(), DWRITE_FONT_WEIGHT_BOLD, ...)` with `ResolveWordFontSize` + `FontTargetScope` (example `Bar.RenderLoop.cpp:12485-12493`); static-size floor rules in spec "主栏与控件静止字号及连续动画目标" (L2564+). Button label size `BarButtonTwoTwoLabelFontSizeDip = 13` (`Bar.Metrics.cppm:36`).

---

## 7. Existing badge / indicator / overlay concepts

| Concept | Where | Reuse value |
|---|---|---|
| Page corner loading ring (delayed 500 ms, 180/140 ms fade, rotating 250° arc, private cached arc/brush) | `Bar.PageLoadingIndicator.h`; drawn in PageControl Scene (`Bar.Scene.cpp`); spec L2727-2761 | State machine + independent visual demand + deadline wake pattern |
| `BarUIRendering::DrawProgressRing(center, radius, stroke, progress, trackColor, progressColor, ...)` | `Bar.Rendering.cpp:1439-1484` | Simple ring draw (creates a path geometry per call when partial) |
| Bottom-dock target indicator (foreground over all bar content, themed resident surface with PointLight, Back-peak capacity reservation, own dirty key) | Draw `Bar.RenderLoop.cpp:12424-12510`; dirty/light `:8963-8979`; capacity `:9075-9077` | Closest template for an overlay drawn above everything with theme + lighting |
| Thickness annotation/overflow info badges + popups (`DrawAttributeBar_ThicknessOverflowBadge`, `drawAttributeOverflowBadgeProgress`) | `Bar.Main.cppm:386`, `Bar.RenderLoop.cpp:805, 3961, 5241, 6419`, draw `:11654+` | Badge anchored to a host with progress-driven appear/disappear |
| Main-button click pulse via middle keyframe | `Bar.RenderLoop.cpp:1638-1658` | Overshoot without spring |
| No existing notification/status badge on the main button was found (grep for badge/notification/徽标 in `UI/Bar`, `UI/PageControl`). | — | — |

## Related Specs

- `.trellis/spec/native-desktop/bar-theme-material.md` — material weights, resident surface/content opacity, edge-light color rules, dirty upper bounds.
- `.trellis/spec/native-desktop/rendering-and-ui.md` — `Inkeys.UI.Bar` (L39-81), idle clock (L139), scheduler (L197), dirty-region transaction (L607-858), lighting notify (L860), edge-lighting switch (L1354-1417), button frame light (L1418), hover/press (L1432), CanJoin/keyframes (L1549-1577), font sizing (L2564), page loading ring (L2727).
- `.trellis/spec/native-desktop/ui-pointer-session.md`, `mouse-hook-and-autocollapse.md`, `pen-haptics.md` — input/capture/haptics for new hit targets.
- `.trellis/spec/shaders/index.md` — covers only Draw3 HLSL; Bar edge lighting is pure D2D (no shader involvement).

## Caveats / Not Found

- Click-through via alpha=0 pixels is inferred from ULW_ALPHA usage and absence of `WM_NCHITTEST` in Bar; not explicitly documented in specs.
- `SetResidentContentOpacity` does not affect `Shape`/`Superellipse`/manual brush draws; an indicator drawn with shapes must apply content/background opacity explicitly.
- `superellipseGeometryCache` is single-entry; a second superellipse (e.g. ring overlay) with different size/n per frame will thrash it unless separately cached.
- PPT/SuperTop/new-version status sources were not researched here.
