# Research: Bar (toolbar) icon inventory

- **Query**: Complete inventory of every SVG/PNG icon usage in the Inkeys Bar UI (main bar, attribute panels, page/whiteboard surface, color picker, tooltips, components), incl. placeholder/shared icons, hard-coded-color SVGs and unused resources.
- **Scope**: internal
- **Date**: 2026-11-03

Code paths are relative to `Inkeys/Inkeys/UI/Bar/` unless stated. SVG files: `Inkeys/src/UI/*.svg` (+ `color/`), registered in `Inkeys/Inkeys.rc:286-357` as type `UI`. `Inkeys/Cache/` ignored.

## 0. Color / size mechanism (recap)

- `Bar.UI.cpp:518` replaces `rgba(10,0,7,0)` with **color1**; `Bar.UI.cpp:532` replaces `rgba(9,0,2,0)` with **color2**. If color1/color2 is `nullopt` the tag is left as-is and renders fully transparent.
- `BarButtonClass` icons have color1 retargeted at runtime by `RetargetBarFixedButtonVisual` (`Bar.Button.cpp:2062-2063`): **Accent when selected, else IconPrimary** (or a content override). color2, if present, follows the same rule (`Bar.Button.cpp:2084`).
- Icon size per button layout (`Bar.Metrics.cppm:200-240`): StandardTwoTwo **28 dip** (`BarButtonTwoTwoIconSizeDip`, `Bar.Metrics.cppm:32`); StandardTwoOne **18**; StandardFourOne **18**; StandardOneOne **20**.
- Dynamic swaps go through `BarButtonClass::TransitionContent` (`Bar.Button.cpp:51-66`), which calls `icon.TransitionToResource(L"UI", name)` only for `iconKind == Svg`.

## 1. Main bar buttons (`Bar.Button.cpp` PresetInitialization 215-757; dynamic styles 1040-1260)

Init: color1 = `IconPrimary` (`defaultIconColor`, line 218), color2 = nullopt unless noted. Labels: `I18nKey.UI.Bar.MainButtons.*` (en-US values from `Inkeys/src/i18n/en-US.jsonc:65-104`).

| Button (preset / zone) | Label key -> en-US | Size / icon dip | Icon resource(s) | file:line | Notes |
|---|---|---|---|---|---|
| Divider (A1 runtime boundary) | - | oneTwo | none | 220-250 | Drawn as a Shape line; comment line 237: SVG retained but unused. `barDivider` never loaded. |
| Select (A1) | `SelectLabel` "Select"; `SelectClearLabel` "Select (Clear)" (two lines); `MoveLabel` "Move" (whiteboard) | twoTwo / 28 | `barSelect` | 278; transition 1058 | Same icon for all three label states (incl. "Move" in whiteboard). |
| Draw (A1) | `DrawLabel` "Draw"; when selected: `LaserLabel`, `HighlighterLabel`, `HardPenLabel`, `SoftPenLabel` | twoTwo / 28 | init `barBrush1`; dynamic `barLaser` / `barHighlighter1` / `barBrush1` (hard pen) / `barBrush2` (soft pen) | 312; 1195-1197 | Not selected -> icon still reflects current pen mode (soft pen default -> `barBrush2`). |
| Eraser (A1) | `EraserLabel` "Erase"; selected `AreaEraserLabel` "Area Erase" | twoTwo / 28 | `barEraser` | 379; 1233-1236 | Same icon both states. |
| Geometry (A1) | `GeometryLabel` "Shapes"; selected `StraightLineLabel` "Line" / `RectangleLabel` "Rectangle" | twoTwo / 28 | `barGeometry`; selected -> `barShapeStraightLine` / `barShapeRectangle` | 428; 1252-1254 | Only main button initialized with color2 = IconPrimary (line 427), needed by `barShapeRectangle` (fill uses color2 tag). |
| Recall / Undo (A1) | `UndoLabel` "Undo" | twoTwo / 28 | `barRecall` | 484 | |
| Redo (main, hidden by default, 2x1 above Undo) | `RedoLabel` "Redo" | twoOne / 18 | `barRedo` | 513 | |
| Clean (A1) | `ClearLabel` "Clear" | twoTwo / 28 | `barClean` | 543 | |
| Whiteboard (A2) | `WhiteboardLabel` "Board"; `ResumeWhiteboardLabel` "To board"; `CloseWhiteboardLabel` "Close"; `ReturnToShowLabel` "To show" | twoOne / 18 | `barWhiteboard`; active (not minimized) -> `barDismiss` | 579; 1215-1220 | "To board" (resume) reuses `barWhiteboard`; "To show" (PPT active) reuses `barDismiss`. |
| Pierce / click-through (runtime-only, hidden) | `ClickThroughLabel` "Click-through" | twoOne / 18 | `barPierce` | 612; 1212 | |
| Freeze (A2) | `FreezeLabel` "Freeze" | twoTwo / 28 | `barFreeze` | 639 | |
| EndShow (A2, hidden unless PPT) | `EndPresentationLabel` "End show" | twoTwo / 28 | `barEndShow` | 675 | |
| Setting (Extension zone) | `SettingsLabel` "Settings" | twoTwo / 28 | `barSetting` | 705 | |
| More (hard-coded boundary control) | `MoreLabel` "More" | twoTwo / 28 | `barMore` (chevron-up stroke) | 756 | `Bar.RenderLoop.cpp:4831-4832`: angle fixed 0; open state shown via Selected/Accent. |

Main bar logo: `Bar.Initialization.cpp:357` `logo1`, color1/color2 nullopt (full-color logo), height 80.

## 2. Component buttons (Extension zone, `RegisterBuiltInComponents`, `Bar.Button.cpp:760-927`)

All are **PNG** icons (`iconKind = Png`, `Bar.Button.cpp:792-802`) loaded via `pngIcon.InitializationFromResource(L"PNG", ...)`; not themable (no color1/color2); sized in the twoTwo icon slot (28 dip). Labels `I18nKey.UI.Bar.Components.*` (en-US `en-US.jsonc:105-117`); settings titles `SettingsUI.Component.*` (`en-US.jsonc:986-1006`).

| Component id | Bar label (en-US) | Settings title | PNG resource -> file (`Inkeys.rc` line) | file:line | Shared? |
|---|---|---|---|---|---|
| Appliance.Explorer | `ExplorerLabel` "Files" | Open File Explorer | CustomizeIco4 -> `src/quick/explorer.png` (112) | 832 | unique |
| Appliance.Taskmgr | `TaskManagerLabel` "Tasks" | Open Task Manager | CustomizeIco9 -> `src/quick/taskmgr.png` (122) | 838 | unique |
| Appliance.Control | `ControlPanelLabel` "Control" | Open Control Panel | CustomizeIco7 -> `src/quick/control.png` (118) | 844 | unique |
| System.Desktop | `ShowDesktopLabel` "Desktop" | Show desktop | CustomizeIco3 -> `src/quick/desktop.png` (110) | 851 | unique |
| System.LockWorkStation | `LockScreenLabel` "Lock" | Lock screen | CustomizeIco8 -> `src/quick/lockworkstation.png` (120) | 857 | unique |
| Keyboard.Keyboardesc | `EscapeLabel` "Esc" | Press Esc | CustomizeIco5 -> `src/quick/keyboardesc.png` (114) | 864 | unique |
| Keyboard.KeyboardAltF4 | literal `L"Alt+F4"` (not i18n) | Press Alt+F4 | CustomizeIco6 -> `src/quick/altf4.png` (116) | 870 | unique |
| RollCall.IslandCaller1 | `RollCallLabel` "Pick name" | IslandCaller 1: pick a random name | CustomizeIco2 -> `src/IslandCaller.png` (108) | 877 | **shared** with IslandCaller2 |
| RollCall.IslandCaller2 | `RollCallLabel` "Pick name" | IslandCaller 2: pick one name | CustomizeIco2 | 883 | **shared** |
| RollCall.SecRandom1 | `RollCallLabel` | SecRandom 1: quick pick | CustomizeIco10 -> `src/SecRandom.png` (131) | 889 | **shared** x3 |
| RollCall.SecRandom2 | `RollCallLabel` | SecRandom 2: quick pick | CustomizeIco10 | 895 | **shared** |
| RollCall.SecRandom2Compat | `RollCallLabel` | SecRandom 2: quick pick (compatible) | CustomizeIco10 | 901 | **shared** |
| RollCall.NamePicker | `RollCallLabel` | NamePicker: random name | CustomizeIco11 -> `src/NamePicker.png` (129) | 907 | unique |
| Linkage.ClassislandSettings | `ClassIslandSettingsLabel` "CI settings" | ClassIsland settings | CustomizeIco1 -> `src/ClassIsland.png` (106) | 914 | **shared** x3 |
| Linkage.ClassislandProfile | `ClassIslandProfileLabel` "Profiles" | ClassIsland profile editor | CustomizeIco1 | 920 | **shared** |
| Linkage.ClassislandClassswap | `ClassIslandSwapLabel` "Swap class" | ClassIsland quick class swap | CustomizeIco1 | 926 | **shared** |

Notes: component icons are app-logo bitmaps per *vendor* (ClassIsland, IslandCaller, SecRandom), not per *action*. CustomizeIco* PNGs are referenced nowhere else in code. The More panel (`Bar.RenderLoop.cpp:4819-4860`) shows the same registered `BarButtonClass` instances (Setting + components that overflow), so it has no icons of its own besides its close X.

## 3. Draw attribute panel (`Bar.Initialization.cpp`)

| Element | Label / meaning | Icon | Color1 (color2) | Size dip | file:line | Notes |
|---|---|---|---|---|---|---|
| Pen type row "Brush" | `DrawAttributes.BrushLabel` "Brush" | `barPaintBrush` | IconPrimary | 18 | 759-762 | Row is **disabled** (`enabled=false`, `Bar.RenderLoop.cpp:3513-3516`). Enum name `DrawAttributeBar_Brush2` holds barPaintBrush (naming mismatch). |
| Pen type row "Laser Pen" | `LaserLabel` | `barLaser` | IconPrimary | 18 | 763-766 | |
| Pen type row "Highlighter" | `HighlighterLabel` | `barHighlighter1` | IconPrimary | 18 | 767-770 | |
| Pen type row "Hard Pen" | `HardPenLabel` | `barBrush1` | IconPrimary | 18 | 771-774 | Enum `DrawAttributeBar_Brush1`. |
| Pen type row "Soft Pen" | `SoftPenLabel` | `barBrush2` | IconPrimary | 18 | 775-778 | Enum `DrawAttributeBar_SoftPen`. |
| Pen type extension arrow (opens Freehand / Annot. line menu) | - | `barThicknessAdjust` (chevron-up, fill) | Accent | 18 | 1033-1040 | Rotated 0/180 (`Bar.RenderLoop.cpp:3626-3635`). **Shared** glyph. |
| Pen type menu check | marks selected `FreeLineLabel` "Freehand" | `fluentCheckmark12Filled` | Accent | 12 (`Bar.Main.cpp:106`) | 1042-1052 | |
| Thickness adjust toggle | thickness view-mode toggle | `barThicknessAdjust` | IconPrimary | 18 | 900-910; render `Bar.RenderLoop.cpp:3873-3878` | **Shared** glyph. |
| Annotation tooltip info | `AnnotationUnavailableTitle` | `barQuestion` | darkStyle ? RGB(200,200,200) : IconPrimary | 14 | 1130-1133 | |
| Thickness-overflow tooltip info | `ThicknessOverflowTitle` | `barInfo` | IconPrimary | 14 | 1134-1136 | |
| Annotation popup close | - | `barCloseSmall` | TextPrimary | 14 | 1137-1140 | |
| Overflow popup close | - | `barCloseSmall` | TextPrimary | 14 | 1141-1144 | |

Color picker (same panel):

| Element | Icon | Color | Size | file:line | Notes |
|---|---|---|---|---|---|
| Preset swatches 1-11 selected check | `colorSelect` | nullopt (hard-coded colors) | 15 | 453-573 | |
| Custom color (swatch 12) wheel | PNG `colorCustom` | - | 30 | 600-606 | |
| Custom color check badge | `colorSelect` | nullopt | 15 | 609-616 | comment: reuses same green check |
| Tone toggle (light palette) | `colorSun` | IconPrimary | 20 | 619-627 | |
| Tone toggle (dark palette) | `colorMoon` | IconPrimary | 20 | 629-637 | |
| Color picker close | **no SVG**: X drawn with `DrawLine` | IconPrimary brush | 1/3 of 30-dip hit | `Bar.RenderLoop.cpp:12189-12226` | Different implementation from all other close X's. |

## 4. Geometry attribute panel (`Bar.Initialization.cpp:1148-1293`)

| Element | Label | Icon | Color1 / color2 | Size | file:line |
|---|---|---|---|---|---|
| Straight line | `GeometryAttributes.StraightLineLabel` "Line" | `barShapeStraightLine` | IconPrimary / IconPrimary | 28 | 1218-1220 |
| Rectangle | `RectangleLabel` "Rect." | `barShapeRectangle` | IconPrimary / IconPrimary | 28 | 1221-1223 |
| Thickness fine / medium / coarse | numbers only | none | - | - | 1242-1254 |
| Close | - | `barCloseSmall` | IconPrimary | 18 | 1281-1289 |

## 5. Eraser attribute panel (`Bar.EraserAttribute.cpp`)

Items handled in `Execute` (478-503); visual buttons `buttonVisuals{0,4,6,7,8,9}` (line 79). Icon color1 = `TextPrimary` (line 120), retargeted to Accent when selected.

| Item | Label (`UI.Bar.EraserAttributes.*`) | Action | Icon | Size | file:line | Shared / placeholder? |
|---|---|---|---|---|---|---|
| 0 | `ClearCanvasLabel` "Clear" | clear canvas (Draw3 Clear command) | `barClean` | 28 | 122-123, 481-496 | Shared with main Clean button (same action). |
| 1-3 | (Small / Medium / Large size) | set base eraser size | none (drawn circles) | - | 125-140 | - |
| 4 | `AutomaticLabel` "Auto size" | toggle automatic size | `barAutoEraser` | 28 | 122-123, 498 | dedicated |
| 5 | (arrow next to item 4) | open/close sensitivity menu | `barThicknessAdjust` (`automaticArrow_`), TextPrimary, Accent when auto on | 18 (`EraserAutomaticArrowSizeDip`, line 29) | 156-159, 357-364, 499 | **Shared** glyph (same as draw-panel arrows). |
| 6-8 | `LowLabel` / `MediumLabel` / `HighLabel` | set sensitivity | text only | - | 81-91, 500 | - |
| 9 | (no label) | `Inkeys::UI::Setting::ShowSpeedEraser()` (opens speed-eraser settings) | `barSetting` | 20 | 121-123, 501 | **Placeholder/shared** with main Setting button (falls into ternary default). |
| title | `SensitivityLabel` "Sensitivity" | - | - | - | 160-161, 366 | |

Line 122 ternary `i==0 ? barClean : i==4 ? barAutoEraser : barSetting` is only reached for i in {0,4,9} (`large || i==9`); items 6-8 have icon disabled (line 121).

## 6. Recall (Undo/Redo) popup (`Bar.RecallAttribute.cpp`)

| Element | Label | Icon | Color | Size | file:line |
|---|---|---|---|---|---|
| Popup command button (Redo or Undo depending on hold phase) | `RedoButtonLabel()` / `UndoButtonLabel()` | init `barRedo`; swaps `barRedo` / `barRecall` | TextPrimary | 28 | 97-100; 233-243 |

## 7. Page / whiteboard surface (`Bar.Scene.cpp`; labels set in `Inkeys/Inkeys/UI/PageControl/PageControl.cpp:1026-1033`)

Buttons init color1 = TextPrimary (`Bar.Scene.cpp:226`). `ConfigureButtonContent` (305-320) loads/transitions icons; callers `UpdateContent` (400-410) and `Configure` (487-528).

| Slot | Meaning / label key | Icon | Rotation | Size | file:line | Notes |
|---|---|---|---|---|---|---|
| page 0 | drag handle | none | - | custom | 487 | |
| page 1 Previous | `WhiteboardPreviousLabel` "Previous" (PPT and whiteboard) | `barMore` | -90 (horizontal / wb), 0 (vertical PPT) | 28 (20 in compact OneOne) | 403, 488-489 | **Shared** with main More chevron. |
| page 2 | page number | text only | - | - | 490 | |
| page 3 Next | `WhiteboardNextLabel` "Next" | `barMore` | 90 / 180 (vertical PPT) | 28 / 20 | 404-406, 509-511 | **Shared** chevron. |
| page 3 Add page (wb last page) | `WhiteboardAddPageLabel` "Add page" | `barAdd` | 0 | 28 | same | |
| page 3 End show (PPT last page) | `EndPresentationLabel` "End show" | `barEndShow` | 0 | 28 | same | Shared with main EndShow (same action). |
| page 4 whiteboard menu | `MoreLabel` "More" | `barMenu` (hamburger) | 0 | 28 | 407, 512 | Visible only in whiteboard, left surface. |
| action 0 | `CloseWhiteboardLabel` "Close" | `barCloseSmall` | 0 | 18 (StandardFourOne) | 408-409, 519-523 | Generic close X. |
| action 1 | `MinimizeWhiteboardLabel` "Minimize whiteboard" | `barMinimize` | 0 | 18 | same | **hard-coded `#fff`**, color1 has no effect. Disabled while PPT active (line 702). |
| action 2 | `WhiteboardThemeLabel` "Theme" | `barPalette` | 0 | 18 | same | **hard-coded `#fff`**. |
| previews 0-5 | background colors (`WhiteboardBackgroundColorLabel`) | `colorSelect` (selected check only) | - | 30 | 524-533, 707 | color1 reset; icon visible only when selected (id>=9, lines 704-706). |
| previews 6-8 | patterns `WhiteboardBlankLabel` "Blank" / `WhiteboardDotsLabel` "Dots" / `WhiteboardGridLabel` "Grid" | `colorSelect` (selected check only) | - | 30 | 410, 527 | No pattern-specific glyph; label + background preview + check. |

## 8. More panel

| Element | Icon | Color | Size | file:line |
|---|---|---|---|---|
| More panel close | `barCloseSmall` | IconPrimary | 18 | `Bar.Initialization.cpp:414-420`; render `Bar.RenderLoop.cpp:4822, 4960` |
| Panel content | registered Extension buttons (Setting SVG + component PNGs, sections 1-2) | - | - | `Bar.RenderLoop.cpp:4823-4860` |

## 9. Tests (secondary)

| File:line | Resource | Purpose |
|---|---|---|
| `Bar.EraserAttribute.Test.cpp:1244` | `barWhiteboard` | resume icon test |
| `Bar.EraserAttribute.Test.cpp:1245` | `barDismiss` | close icon test |
| `Bar.EraserAttribute.Test.cpp:1836` | `barAutoEraser` | eraser button test |
| `Bar.EraserAttribute.Test.cpp:2323-2324` | `logo1`, `Frame94` | logo tag binding test (B350) |
| `Bar.Presentation.Test.cpp:710-739` | all Svg-kind buttons | svg binding probe |

## A. Placeholder / shared usages that may need dedicated icons

1. **Eraser panel item 9** (open speed-eraser settings) uses main `barSetting` (`Bar.EraserAttribute.cpp:122`).
2. **Eraser panel item 0** (Clear canvas) uses main `barClean` (same semantics as main Clean).
3. **`barThicknessAdjust`** chevron reused for 3 different controls: eraser sensitivity toggle (`Bar.EraserAttribute.cpp:157`), pen-type extension arrow (`Bar.Initialization.cpp:1035`), thickness adjust toggle (`Bar.Initialization.cpp:904`).
4. **`barMore`** chevron reused rotated as Previous / Next page buttons (`Bar.Scene.cpp:403-406, 489, 510`).
5. **Whiteboard main button**: "To board" reuses `barWhiteboard`; "To show" (return to PPT) reuses `barDismiss` (`Bar.Button.cpp:1215`).
6. **Select** keeps `barSelect` for "Move" (whiteboard) and "Select (Clear)" (`Bar.Button.cpp:1058`).
7. **Eraser** keeps `barEraser` for "Area Erase" (`Bar.Button.cpp:1234`).
8. **`barCloseSmall`** used for 5 close actions (More panel, geometry panel, two tooltips, whiteboard Close); color-picker close is a line-drawn X instead.
9. **`colorSelect`** used as selection check for 11 pen swatches, custom color, 6 whiteboard color previews and 3 whiteboard pattern previews (Blank / Dots / Grid have no own glyph).
10. **Component buttons sharing a PNG**: IslandCaller1 + IslandCaller2 (CustomizeIco2); SecRandom1 + SecRandom2 + SecRandom2Compat (CustomizeIco10); ClassIsland Settings + Profile + Class swap (CustomizeIco1). All 16 components are PNG (not themable SVG). Alt+F4 bar label is a hard-coded literal.
11. **Brush** pen-type row uses `barPaintBrush`, but the row is disabled.

## B. SVGs with hard-coded colors (color1/color2 ignored)

| File | Hard-coded | Used by |
|---|---|---|
| `barMinimize.svg` | `stroke="#fff"`, viewBox 48 | whiteboard Minimize action (`Bar.Scene.cpp:408, 519`) |
| `barPalette.svg` | `stroke="#fff"`, `fill="#fff"`, viewBox 48 | whiteboard Theme action (same lines) |
| `color/colorSelect.svg` | `fill="#00c881"`, `stroke="#ffffff"` | all selection checks (A.9); always called with nullopt colors |
| `logo1.svg` | gradients, `#000000`, white | main button logo |
| `logo2.svg` | `#BAC3C8`, white, plus both tags | unused |
| `Frame 94.svg` | white + color1 tag | test only |

## C. Stroke vs fill style (relevant to a consistent filled style)

- Fill-only (`fill` = tag): barAutoEraser, barBrush1, barBrush2, barClean, barEraser, barGeometry, barHighlighter1, barPaintBrush, barPierce, barRecall, barRedo, barSelect, barSetting, barThicknessAdjust, barDivider, fluentCheckmark12Filled, colorSun, colorMoon.
- Stroke-only (`stroke` = tag): barAdd (w3), barCloseSmall (w2), barDismiss (w2), barEndShow (w1.85), barInfo (w2), barMenu (w2), barMore (w3), barQuestion (w2), barShapeStraightLine (w3.2), barWhiteboard; plus hard-coded barMinimize (w4), barPalette (w3).
- Mixed fill + stroke: barFreeze, barLaser, barShapeRectangle (stroke = color1, fill = color2).
- viewBox varies: 24x24 for most; barSelect 16x23, barRecall 19x20, barRedo 20x20, barClean / barSetting 23x24, barGeometry 26x24, barAutoEraser 28x28, colorSun / colorMoon / barDivider 20x20, barMinimize / barPalette / colorSelect 48x48, fluentCheckmark12Filled 12x12.

## D. Registered but unused resources

| Resource | Status |
|---|---|
| `BARDIVIDER` (`barDivider.svg`) | never loaded (`Bar.Button.cpp:237` comment) |
| `LOGO2` (`logo2.svg`) | never loaded anywhere |
| `FRAME94` (`Frame 94.svg`) | only in `Bar.EraserAttribute.Test.cpp:2324` |

Contrary to the initial suspicion, these ARE used: barBrush2 (soft pen), barPaintBrush (disabled Brush row), barLaser, barHighlighter1, barInfo, barQuestion (tooltips), barMenu, barMinimize, barPalette, barAdd (page / whiteboard surface), barDismiss (whiteboard active), barShapeStraightLine / barShapeRectangle (geometry).

## Caveats

- Component PNG rendered size assumed equal to the twoTwo icon slot (icon.rW/rH copied from PNG at `Bar.Button.cpp:800-801`, then sized by the shared button layout).
- Runtime color retargeting of draw-panel pen-type SVGs (selected state) lives in `Bar.RenderLoop.cpp` (e.g. 1542, 4613, 10234) and was not traced per element.
- `research/current-icons.html` (added by the coordinator) was not touched.
