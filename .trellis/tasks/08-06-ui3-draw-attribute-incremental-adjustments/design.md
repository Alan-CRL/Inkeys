# 技术设计

## Boundaries

实现限制在 UI3 Bar。预计直接修改 `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp`，如需声明或状态字段才修改 `Bar.Main.cppm`、`Bar.State.cppm`。`Bar.Theme.cppm`、`Bar.UI.cppm` 是复用主题和动画基础的阅读依据，默认不改。`Inkeys/IdtFloating.cpp` 和 `origin/inkeys2-final:智绘教/IdtFloating.cpp` 仅用于历史比较。

## Relevant Code

| Area | Actual code and role |
| --- | --- |
| Draw attribute constants | `Bar.Main.cpp:62-177`: highlighter DIP presets, pen-type extension dimensions, Slider sizes, Color Picker `300 x 209 DIP`、`0.24 s` 动画常量。 |
| DIP conversion | `GetBarThicknessSliderRange()` and `GetBarThicknessPresetPx()` near `Bar.Main.cpp:214-318`; both multiply DIP constants by `barStyle.dpiZoom`. |
| Slider geometry | `CalculateBarThicknessPreviewGeometry()` near `Bar.Main.cpp:252-304`; `sliderCenterY` is the preview center. |
| Slider position/session | local `drawAttributeThicknessSliderPositionLocked`, `drawAttributeThicknessSliderUsePositionB`, `drawAttributeOverflowSliderSessionAllowsHint`, `ResolveThicknessSliderCenterY()` near `Bar.Main.cpp:3201-3227`; lifecycle updates near `3581-3712` and `5470-5495`. |
| Slider input | `Bar.Main.cpp:12280-12840`: Slider Hit/Thumb pointer loop, `pressOnThumb`, capture, candidate update and release-time `thicknessSliderPinned` write. |
| Pen-type extension | extension layout around `5191-5254`, absolute geometry around `7520-7569`, rendering around `8788-8822`, initialization around `14217-14240`. |
| Pen-type menu state | `Bar.State.cppm:21-26`; `ClosePenTypeMenu()` at `Bar.Main.cpp:3002`; direction lock/open at `3615-3642` and pointer action at `12999-13031`. |
| Color Picker state/layout | `Bar.State.cppm:53-66`; progress curve at `Bar.Main.cpp:3485-3512`; panel layout/fields at `8056-8270`; top-layer rendering at `10225-10425`; initialization at `14080-14135`. |
| Theme and lights | `Bar.Theme.cppm:59-79` defines `Accent`; `Bar.UI.cppm:730-765` defines PointLight's `framePrimaryLightEnabled` and `frameCursorLightIntensityScale`; `Bar.Main.cpp:14091-14094` initializes the Color Picker panel. |
| SVG angle animation | `Bar.UI.cppm:768-794` exposes `BarUiSVGClass::angle` as `BarUiValueClass`; generic SVG advancement calls `ChangeValue(val->angle)` at `Bar.Main.cpp:7080-7094`. |
| Historical highlighter | `origin/inkeys2-final:智绘教/IdtFloating.cpp:6003,6031` use 35/50; `IdtDraw.cpp:357-360` defines screen scaling; `IdtDrawpad.cpp:2126-2139` sets default 35; `IdtState.cpp:13-31` owns the stored width. |

## Existing Behavior And Root Causes

1. **Pen-type divider press coupling.** `extensionDivider` is laid out as a separate shape but rendered inside the D2D transform established for `extensionHit`'s press scale (`Bar.Main.cpp:8799-8821`). Therefore the parent press transform scales the divider together with the background and SVG.
2. **Divider color.** Its initialization uses `SurfaceFrame` for fill/frame (`14225-14235`). No selected-state color target subsequently replaces it, so the dark-theme divider is white.
3. **Thumb click and triangle state.** The Slider pointer loop records `pressOnThumb` but waits until mouse-up to set `thicknessSliderPinned = true` on every completed non-drag gesture (`12763-12772`). `thicknessSliderPinned` drives the adjust button's selected/Accent presentation (`5433-5455`), so an unchanged Thumb click can retain the expanded state.
4. **Two Slider Ys.** `ResolveThicknessSliderCenterY()` returns Position A unless a session-locked snapshot says the Overflow Hint was visible; otherwise it offsets/clamps to Position B. `drawAttributeThicknessSliderUsePositionB` is assigned from Hint visibility at the Preview -> Slider edge.
5. **Current presets.** `BarHighlighterThicknessPresetDip[] = {30, 50, 80}` is the only UI3 quick-preset source; the same conversion helper serves rendering and click handling.
6. **Inkeys2 facts and units.** Inkeys2's direct highlighter choices are only 35 and 50, not three. The historical `drawingScale` normalizes to a 1920x1080 screen and its width is passed directly to GDI+ `Pen::SetWidth` / ellipse width, so it represents stroke diameter in scaled drawing pixels. UI3's DIP-to-`dpiZoom` path is different; at 100% DPI, 35 DIP and 50 DIP are the comparable baseline values.
7. **Color Picker Popup.** It is an absolute surface in the existing `floating_window`, anchored below/above the custom swatch. `drawAttributeColorPickerProgress` uses `EaseOutBack` on open and `EaseInCubic` on close; layout currently scales `0.85 -> 1.00`, fades opacity and moves 20 DIP along the outward direction.
8. **RGB/Alpha layout.** The current code already allocates independent column rectangles and pads numeric strings to three characters, so adjacent column starts are mostly stable. However it derives the outer X edges from `paletteLeft/right` (5 DIP) while the footer's actual vertical whitespace is materially larger; its leading-space formatting also couples numeric alignment to font whitespace and hardcoded Chinese alpha text.
9. **Color Picker lights.** The panel is `PointLight`, has `framePrimaryLightEnabled = false`, and retains `frameCursorLightIntensityScale = BarButtonCursorLightIntensity`. In this UI3 naming, `frameCursor` is the third mouse light. The requested Light 1 exclusion is already present and must remain local to this panel.
10. **Triangles.** The thickness `barThicknessAdjust` SVG has no angle target. The pen-type SVG has an angle target based only on open-below direction, then the absolute layout overwrites it every frame with `SetDirect` (`7564-7568`), preventing a state-driven rotation and interruptible reverse animation.

## Design

### 1. Independent divider visual

Keep hit testing and press feedback on `DrawAttributeBar_PenTypeExtensionHit`. Compute the divider from the same selected extension geometry, but render it after restoring the D2D transform used for `extensionHit` and arrow. This makes it geometrically independent instead of compensating for scale. Drive both surface/frame color through `GetThemeColor(BarThemeColorEnum::Accent)` whenever the selected extension is visible, retaining the current PointLight and third-light settings.

### 2. Slider interaction and unified Y

Use the existing `pressOnThumb` calculated at pointer-down as the state-transition point. A Thumb press clears the fixed expand state immediately and triggers render; its mouse-up path must not unconditionally restore the pin when no value or drag occurred. Track click behavior and capture lifecycle remain on their existing path. Preserve `thicknessSliderPressed`/`Dragging` so the Slider stays interactive during a press.

Remove `drawAttributeThicknessSliderUsePositionB` and its snapshot assignment/reset. `ResolveThicknessSliderCenterY()` always applies the current Position B offset and clamp. Keep `drawAttributeThicknessSliderPositionLocked` and `drawAttributeOverflowSliderSessionAllowsHint`: they continue to encode the Overflow Hint lifecycle, not the Y selection.

### 3. Highlighter values

Change only `BarHighlighterThicknessPresetDip` to the planned `35.0, 50.0, 70.0`; do not alter Slider min/max. At 100% this retains the historical default/medium values. `GetBarThicknessPresetPx()` continues to be the single conversion point, so high-DPI values remain `round(DIP * dpiZoom)` without `drawingScale` or UI zoom double application.

### 4. Color Picker

Retain the existing Popup progress and its `EaseOutBack` primitive. Tune only if run-time observation shows the present 15% scale/20 DIP translation needs to be reduced; no second Popup animation system or new window is permitted. Rework the footer around explicit outer/column/value anchors: calculate a common `footerOuterPadding` from its vertical space, reserve maximum glyph widths for `255`/`100%`, fix R/G/B label starts and alpha's right edge, and right-align values within their reserved slots. Obtain alpha text from the existing localization path if one is already used at the call site; do not change fonts.

No Color Picker Light 1 code is needed unless inspection during implementation finds an intervening write: preserve its per-shape primary-light false flag and third-light scale. This is a regression check rather than a global light change.

### 5. Triangle targets

Use the existing `BarUiSVGClass::angle` and normal `SetTar`/generic `ChangeValue` advancement, with no per-frame `SetDirect` angle write. Choose a collapsed base angle from the actual outward direction: down uses the SVG's down-facing angle and up its opposite. Expanded target is the base plus/minus 180 represented as the opposite stable `0` or `180` value. Use `thicknessSliderPinned` for the thickness stable state and `penTypeMenuOpen` plus locked `penTypeMenuOpenBelow` for the pen-type stable state. Retarget from the current value with the nearby default operation duration/curve; a new click or forced close therefore reverses smoothly rather than completing stale work.

## Compatibility And Risks

- The input loop is capture-sensitive. Do not clear `thicknessSliderPressed`, `Dragging`, candidate width or capture while merely changing pinned visual state.
- Keep `CloseThicknessOverflowTooltip()` and session gating paths intact; Position B unification must not reintroduce Hint creation in Slider.
- The `frameCursorLightIntensityScale` name is easy to misread. It is third-light control, so it must not be set to zero while satisfying the Light 1 request.
- Inkeys2 supplies two, not three, direct highlighter shortcuts. The third UI3 value is a documented design addition (`70 DIP`), not a falsely claimed historical value.
- UI3 has no automated animation target. Build plus scripted/static checks cannot replace manual visual and rapid-interaction verification.
