# Research: Whiteboard controls architecture and UI3 reuse audit

- Query: 底部分页、更多、主题按钮实际复用了什么；为什么重复发生规范偏差、文字丢失和缓存补丁；主栏也卡顿是否有架构传播路径。
- Scope: internal, read-only code/history audit; no code changes, no builds, no GUI, no performance capture.
- Date: 2026-10-15
- Revision scope: current source after d4d42869; compare dfdc8b8e initial whiteboard source. Source line references below use repository absolute prefix `D:/Project/Inkeys/Repo/Inkeys/`.

## Findings

### 1. Actual ownership and call paths

- `Inkeys/Inkeys/UI/PageControl/PageControl.cpp:58` aliases `BarSurfaceScene`, NOT the main bar instance. Each of four PageControl surfaces owns a Scene plus independent rendering target. Whiteboard reuses PPT BottomLeft/BottomRight HWND and Scene owners; it does not turn the whiteboard controls into main-bar registered buttons.
- `Inkeys/Inkeys/UI/PageControl/PageControl.cpp:660-826` BuildWidgets manually builds 23 widget specifications for each surface (4 initial drag/prev/page/next + 19 whiteboard-specific placeholders). Only BottomLeft in whiteboard mode activates More/menu/theme/header/divider widgets. Stable IDs preserve actual widget instances, but topology/layout descriptions are recreated for content updates.
- `Inkeys/Inkeys/UI/Bar/Bar.Scene.cpp:523-552` each independent Scene owns `BarUISetClass rendererOwner`, `backgroundShape`, widgets, opacity, geometry/damage state, its own `lastFrame`, and newly added `popupClock`.
- Main bar drawing still uses `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp:11847-11853` registered `barButtonSet.buttonList` + DrawBarButtonVisual. Draw attributes are separate shape/svg maps and inheritance (`:10300-10325`). Main More uses its own packing/placement and progress state (`:5168-5285`).

Therefore “same primitives” is true, “the same complete layout/render/input runtime as main-bar attributes” is not. No inspected exported API lets a client simply instantiate an arbitrary existing Draw attribute panel with its own rows. This is an architectural limitation, not evidence that another complete canonical builder was ignored.

### 2. What really is shared today

- Button dimensions/slots: `Inkeys/Inkeys/UI/Bar/Bar.Metrics.cppm:131-204` ResolveBarButtonVisualMetrics.
- Button hover, pressed/selected alpha, press scale and theme targets: `Inkeys/Inkeys/UI/Bar/Bar.Scene.cpp:289-416`; Scene calls these at `:1079-1129`; actual main-bar code also calls RetargetBarButtonInteractionVisual at `Bar.RenderLoop.cpp:2642`.
- Actual Shape/SVG/Word rendering and press-scale transform: `Bar.Scene.cpp:454-509` DrawBarButtonVisual; both the main registered buttons (`Bar.RenderLoop.cpp:11852`) and independent Scene (`Bar.Scene.cpp:2609`) call it.
- Popup easing and pose mathematics: EraserSurfaceMotion, ResolveEraserSurfacePose and ComposeEraserPose, originally used by eraser attributes (`Bar.EraserAttribute.cpp:270-279`), now also used inside Scene (`Bar.Scene.cpp:564-577`, `:1162-1169`). They share core animation advance types/global speed and enabled configuration.
- Existing `colorSelect` SVG is used (`PageControl.cpp:785`) rather than a replacement drawn check.

### 3. What remains a separate adapter / hand-authored policy

- `PageControl.cpp:762`: menuLeft=190, menuTop=-122.5, themeLeft=345, themeTop=-301. Row placement, icon/text roles, preview grid and headers/divider are computed manually at `:766-815`. This uses shared metrics but is not main More's layout/packing routine.
- Duplicate geometry contract exists in `PageControl.cppm:677-684`: WhiteboardMenuPanel, WhiteboardMoreAnchor, WhiteboardThemePanel, WhiteboardThemeAnchor. BuildWidgets separately encodes the same positions/IDs in `.cpp`; changing one can desynchronize allocation/animation envelope from actual drawing.
- `Bar.Scene.cppm:78-94` new popupAnchor, parentPanel, panel/divider/header, selection flags and resource-release callbacks turn the former flat surface adapter into a second popup description system.
- `Bar.Scene.cpp:564-594` recursively computes per-widget parent pose, opacity and visibility. `:1162-1169` retargets and advances its popup motions. This delegates easing mathematics to existing EraserSurfaceMotion, but hierarchy traversal, clock ownership, visibility and render composition are new Scene orchestration.
- True Draw attributes remain shape-map SetTar batches at `Bar.RenderLoop.cpp:3299-3354`, then `Inherit(Center, DrawButton)` and content `Inherit(TopLeft, panel)` at `:10300-10325`. They do not run the newly added Scene popup hierarchy. “Uses the drawing attribute's actual mechanism” is therefore too broad; reuse is the common easing/inheritance idea and eraser helper, not the same full production branch.
- Divider appearance is manually recreated by Scene's ApplyPanelChromeLocked (`Bar.Scene.cpp:952-963`) plus PageControl hardcoded geometry (`PageControl.cpp:815`); main More's divider is a separately configured shape in `Bar.RenderLoop.cpp:5388-5404` rendered at `:11864-11870`. Shared Shape rendering and matching values do not mean the same complete divider component.
- FourOne visual layout was added specifically for this feature (`Bar.Metrics.cppm:152-162`); scene maps it back to existing `BarButtonSizeEnum::twoOne` (`Bar.Scene.cpp:916-920`). It is a new independent-surface metric, not an existing main-bar four-wide registered-button size.

### 4. Confirmed original missing-label cause

Initial dfdc8b8e PageControl BuildWidgets wrote previous/next direction labels to `secondaryText` while selecting StandardTwoTwo. The shared metric defines StandardTwoTwo primary font 13 and primary label slot, but leaves secondaryFontSizeDip at zero (`Bar.Metrics.cppm:105`, `:163-172`). Scene applies these metrics to actual word sizes (`Bar.Scene.cpp:898-902`).

That is a straightforward consumer contract bug: a generic primary/secondary string adapter was wired to the wrong slot, not GPU caching, missing fonts or animation speed. Current source explicitly assigns primaryText (`PageControl.cpp:683`, `:710`). Repeated reliance on visually similar adapter fields rather than a typed canonical icon+label constructor allowed this category of drift.

### 5. What the rounded-rectangle cache actually is

- Theme previews are NOT native fill content inside BarUIRendering. `PageControl.cpp:796-800` installs a business-owned DrawTheme callback per swatch/preview.
- `Inkeys/Inkeys/UI/Whiteboard/Whiteboard.Theme.h:10-15` ThemeDrawingResources contains a retained D2D device context, solid brush, rounded geometry and exact bounds. `:26` lazily creates brush; `:29-35` creates/reuses geometry when bounds change and PushLayer clips the custom fill/texture; `:58-60` pops the clip and manually draws a border.
- This cache avoids recreating a brush and clip geometry every frame while unchanged. It is device drawing resource caching, not whiteboard page persistence, and does not itself imply a leak or inherently bad design. Existing main renderer already has other resource caches, e.g. rounded diffuse-mask cache at `Bar.Rendering.cpp:1514-1691`.
- It is nevertheless extra code/lifecycle because preview fill bypasses native Shape content. There are two independent border paths: custom DrawTheme preview frame and standard button Shape/light feedback. Matching the look now depends on both paths.
- Cache lifetime is shorter than implied by “stable reusable preview resources”: BuildWidgets creates nine fresh resource holders whenever called (`PageControl.cpp:796`), and TransitionLayout unconditionally replaces each widget spec (`Bar.Scene.cpp:1617`). Open/close/menu-layout transitions replace callback-captured resources and lose their old caches. UpdateContent preserves callbacks if contentRevision is unchanged (`Bar.Scene.cpp:1547-1549`), so not every content publication discards all caches; a color change replaces affected pattern-preview callbacks. This is unnecessary churn but not profiled as the dominant stall.

### 6. Concrete rendering costs and propagation evidence

- `Bar.Scene.cpp:2577-2613` iterates each widget individually, computes inherited opacity and, while alpha<1, wraps EACH widget in a separate PushLayer/PopLayer. Parent menu/theme and children are not drawn once inside one inherited group layer. Expanded theme includes two panel layers, menu rows, nine previews, headers and divider during fade; each preview also performs its custom rounded clip layer (`Whiteboard.Theme.h:35`). Geometry caching does not remove per-frame layer clipping/compositing costs. This is confirmed extra work, not proof of the user's live stall source.
- `Bar.Scene.cpp:1162-1169` any popup advance includes FULL scene damage. Content/layout retargeting (`:1570` onward) also marks full damage. PageControl converts this to its own layered-window presentation. This is unlike “only a tiny button animates” intuitions.
- Fullscreen Whiteboard background calls DrawTheme without cached resources (`Whiteboard.cpp:119`); default cache is local (`Whiteboard.Theme.h:21`), so its brush is recreated when actually rendered. Dots/grid are drawn as individual line/ellipse calls (`:44-53`). Inspect scheduler wake cadence and actual traces before blaming this path; a static background should not render at animation cadence solely because other clients animate.
- All this runs under shared UI RenderPipeline's serial scheduling contract documented in `.trellis/spec/native-desktop/rendering-and-ui.md` (“UI3共享串行调度器合同”). Extra render work or synchronous window waits in PageControl/Freeze can consume the same serial frame budget as main Bar. Thus the user's observation that entering whiteboard slows main Bar has a credible architectural mechanism even though main Bar buttons retain their original implementation. This audit does not claim measured causal attribution.
- Scene normal button animation uses clamped lastFrame dt (`Bar.Scene.cpp:1137-1145`), popup uses separate FrameAnimationClock (`:1167`). This dual orchestration remains and is not exactly the main Bar single clock pipeline. Requires trace before blaming an observed hitch.

### 7. Why previous iterations kept drifting

1. Reuse was treated as bottom-level Shape/Button/easing reuse, while product changes repeatedly added custom slot/layout/popup/composition policy to independent Scene and PageControl.
2. Hardcoded duplicate rectangles and generic primary/secondary strings make it possible for metrics, displayed content, envelope and hit mapping to disagree.
3. An earlier patch removed a custom springLayout and adopted eraser pose math; this fixed genuine issues but did not make the whiteboard popup an instance of main Draw attributes. Earlier delivery language overstates that scope.
4. Caching addressed resource construction within the new callback path; it did not remove new hierarchy/compositing work or prove end-to-end frame budget.
5. Existing deterministic/offscreen checks establish geometry/labels/hit/semantic correctness at selected frames, not serial scheduler live rendering cost. No real UI frame-time acceptance had been run, which prior validation acknowledges. Treating these passes as smoothness confirmation would be invalid.

### 8. Smallest corrective direction (research only)

- First attribute the live cost with already available per-client/frame-stage diagnostics while user reproduces. Compare Whiteboard closed-menu idle, PageControl-only interaction, menu fade, theme fade and main-bar-only interaction. Count client requests, Continue/Retry rates, render/EndDraw/GetDC/ULW/window-owner waits and full damage; trace must precede another visual patch.
- Preserve existing ownership (same two HWND, PageControl callbacks, stable button IDs) and actual shared DrawBarButtonVisual. Do not rewrite main Bar or draw3 storage based on appearance issues.
- Canonicalize popup layout description in one shared layout resolver rather than `.cpp` plus `.cppm` duplicate coordinates; typed standard button constructors can enforce label slots and metric identity.
- If generic popup hierarchy remains, make its pose/alpha/current geometry single-source and grouped render composition part of a shared UI facility. Calling EraserSurfaceMotion alone is insufficient to claim full runtime reuse.
- Consider direct native Shape fill/texture content or grouped preview content at the shared renderer boundary to avoid business-owned duplicate border/clip layers. Optimize only after measured cost, preserving native button/light feedback.

## External References

None. This is an internal implementation and history audit; no external documentation needed.

## Related Specs

- `.trellis/spec/native-desktop/rendering-and-ui.md`: independent surface ownership, shared serial scheduler, primitive reuse and frame diagnostics.
- `.trellis/spec/native-desktop/whiteboard.md`: declared popup/button/frame-envelope contract (some descriptions overstate complete main-attribute reuse).
- `.trellis/tasks/10-15-whiteboard-v1/prd.md`, `design.md`, `implement.md`: approved product dimensions and prior validation scope.

## Caveats / Not Found

- No product code changed; no build, GUI launch, desktop control or live process capture performed by this researcher.
- User's current live slowdown is a report, not measured by this audit. Do not present extra layers/cache churn as confirmed primary root cause without frame-stage traces.
- Historical source used for the label bug is dfdc8b8e; current source already fixes the wrong slot.
- More's own anchor/separator layout constants differ from the user-approved whiteboard 5 DIP spacing; matching visual style does not require replacing explicit 5 DIP with those unrelated placement values.
