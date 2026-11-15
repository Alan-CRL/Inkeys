## Facts

**The minimal implementation is a new property action that composes a rotation into `ShapeObject::transform`. Planar presets remain editable without conversion; current 3D contracts reject screen-plane rotation.** (`inkStrokeModelerTest/draw3/shape_interaction.inl:261–269`; `inkStrokeModelerTest/draw3/shape_core.cpp:460`)

Paths below use these aliases:

- `Bar` = `Inkeys/Inkeys/UI/Bar/`
- `Draw` = `Inkeys/Inkeys/Drawing/Draw3/`
- `Core` = `inkStrokeModelerTest/draw3/`
- `Tests` = `inkStrokeModelerTestTests/`

### Flip, end to end

1. `Bar/Bar.ShapeEditor.cpp:52` allocates `FlipHorizontal`, `FlipVertical`, then `Count`. `ToolbarItems` explicitly orders both flips before Finish, independently of numeric IDs (`:55–57`).
2. `Resource()` selects the dedicated SVG names (`:388–389`). Generic initialization loads resource type `UI` before configuring `StandardOneOne` (`:727–746`).
3. Labels are localized but visually disabled, leaving icon-only buttons (`:1127–1129`). `wanted[]` exposes both flips for every preset except Circle (`:1133–1139`).
4. Layout always uses two rows. Color/style/pattern/thickness comprise row one; remaining catalog members occupy row two. Finish receives additional right alignment (`:1166–1191`).
5. Activation copies the presented snapshot identity and publishes distinct flip actions through `Emit()` (`:2079–2088`, `:2121`). The production sender calls `PublishShapeUiAction` (`:491–493`).
6. `Draw/Draw3.ShapeInteraction.h:42` defines the actions; `BuildShapePropertyCandidate()` dispatches to `FlipShape()` (`:78–79`). Runtime explicitly admits flips only outside an active editing gesture (`Draw/Draw3.ShapeRuntimeActions.inl:637–640`).
7. `FlipShape()` validates identity/revision, builds transformed geometry, computes its **path-point screen AABB**, reflects around its center, promotes legacy representation, composes the reflection outside the existing transform, then rebuilds (`Core/shape_interaction.inl:484–499`).

### Representation and rotation support

There is no general scalar rotation field: every object has a six-component affine transform plus local geometry and optional typed construction. `SolidConstruction::baseRotation` is a separate **model-space Z rotation**, not screen rotation (`Core/shape_core.h:18–30`, `:59–75`, `:98–109`).

| Presets | Authoritative/local representation | Discrete screen rotation |
|---|---|---|
| Rectangle, rounded rectangle | Local axis-aligned corners and radius; construction retained | Supported through outer transform; retain rectangle identity/radius (`Core/shape_core.cpp:108–113`). |
| Ellipse, circle | Center/half-axes or center/radius | Transform supported; Circle can be hidden by UI policy (`Core/shape_core.cpp:115–127`; `Core/shape_interaction.inl:30–31`). |
| Triangle variants, parallelogram, rhombus, trapezoid | Derived polygon vertices; edited variants can store `VertexConstruction`; triangle constraints retain vertex identity | Supported without rewriting vertices or constraints (`Core/shape_core.cpp:128–152`; `Core/shape_presets.inl:8–49`). |
| Regular polygon | Center, first vertex, side count; derived vertices | Supported by orthogonal rotation; uniform-transform constraint remains satisfied (`Core/shape_core.cpp:142–143`, `:457–459`). |
| Polyline, free polygon | Typed vertex list | Supported through outer transform (`Core/shape_presets.inl:198–214`). |
| Line, arrow | Endpoints; arrow derives a Compound shaft and head | Supported by transform despite lacking a dedicated rotate handle (`Core/shape_core.cpp:90–106`; `Core/shape_interaction.inl:28–29`, `:501–518`). |
| Arc, sector | Center/radius point plus sweep; derived sampled vertices | Supported without losing sweep semantics (`Core/shape_presets.inl:216–231`). |
| Cuboid, cube, cylinder, cone, prism, pyramid | Typed height/base size/base rotation/mirror/offset, deriving projected parts | **Screen rotation unsupported:** validation requires zero `m12/m21` and equal absolute axis scales (`Core/shape_core.h:59–65`; `Core/shape_presets.inl:234–236`; `Core/shape_core.cpp:460`). |

Arbitrary planar rotation already exists: rotate handle ID 1 uses the transformed **local path-bounds center**, applies an `atan2` delta, and composes an outer rotation. The preset table enables planar rotation; projected solids get Move instead (`Core/shape_interaction.inl:25–49`, `:228–233`, `:261–269`).

Positive angles are clockwise on a y-down screen: `x'=cosθ·x−sinθ·y`, `y'=sinθ·x+cosθ·y`. Thus use **+π/4 and +π/2**, including after reflection; compose outside the existing transform (`Core/shape_core.cpp:403–409`; `Core/shape_interaction.inl:267–269`).

### History, revision, derived state

Flips receive revisions from the page allocator and strict current-snapshot identity validation (`Draw/Draw3.ShapeRuntimeActions.inl:31–34`, `:613`, `:635`; `Draw/Draw3.ShapeRuntime.inl:150–162`).

Each click follows the existing Replace transaction: stable object/item/index, semantic before/after journal entry, redo-branch truncation, updated tile footprint and raster token. Undo/redo rematerializes with fresh revisions (`Draw/Draw3.DrawingController.cpp:634–665`, `:610–622`).

Focus-layer commits update CPU/journal and redraw L1; fallback replay restores candidate composition before committing CPU state and rolls back on failure. Both notify content changes (`Draw/Draw3.ShapeRuntimeActions.inl:109–139`, `:352–369`, `:687`).

Flip performs no snapping or external relation-solving step. Construction is rebuilt unchanged; bounds/frame/handles are regenerated for the published object. Intrinsic triangle/trapezoid constraints are validated during construction (`Core/shape_interaction.inl:92–105`, `:484–499`; `Core/shape_presets.inl:8–49`; `Draw/Draw3.ShapeRuntimeActions.inl:411–421`).

### Assets and localization

Flip assets are named `UI` resources in `Inkeys/Inkeys.rc:421–422`, project `None` items in `Inkeys/Inkeys.vcxproj:1267–1268`, and `资源文件` filter items in `Inkeys/Inkeys.vcxproj.filters:2767–2771`. Loading is name-based through `ExtractResourceString`/`FindResourceW`; no new numeric resource-ID entry or SVG enum table is needed for this path (`Bar/Bar.UI.cpp:229–233`; `Inkeys/Inkeys/Load/Load.cppm:38–54`).

Both SVGs use `rgba(10,0,7,0)` for primary strokes/fill and `rgba(9,0,2,0)` with `fill-opacity="0.24"` for secondary fill (`Inkeys/src/UI/barShapeFlipHorizontal.svg:2–4`; `Inkeys/src/UI/barShapeFlipVertical.svg:2–4`). These tokens map to `color1`/`color2` (`Bar/Bar.UI.cpp:512–533`).

Every locale stores `FlipHorizontalLabel` at line **177**, `FlipVerticalLabel` at **178**, under `Inkeys/src/i18n/`:

`de-DE.jsonc:177`, `en-GB.jsonc:177`, `en-US.jsonc:177`, `es-ES.jsonc:177`, `es-MX.jsonc:177`, `fr-FR.jsonc:177`, `id-ID.jsonc:177`, `it-IT.jsonc:177`, `ja-JP.jsonc:177`, `ko-KR.jsonc:177`, `pl-PL.jsonc:177`, `pt-BR.jsonc:177`, `ro-RO.jsonc:177`, `ru-RU.jsonc:177`, `tr-TR.jsonc:177`, `uk-UA.jsonc:177`, `vi-VN.jsonc:177`, `zh-CN.jsonc:177`, `zh-HK.jsonc:177`, `zh-TW.jsonc:177`.

`Scripts/i18n.ps1` uses zh-CN as source, generates `IdtI18nKeys.g.h`, synchronizes locales and source snapshot, then checks parity (`:12–16`, `:479–487`, `:708–736`). The existing generated keys are `Inkeys/IdtI18nKeys.g.h:180–181`.

## Hypotheses

- Matching the existing rotate handle’s center should give consistent repeated rotations. Copying flip’s screen-AABB center instead can choose a different pivot for asymmetric rotated geometry; those centers are calculated differently (`Core/shape_interaction.inl:85–90`, `:263–264`, `:488–496`).
- Excluding Circle follows current flip policy. Excluding all six solids is technically required; changing `baseRotation` would implement a different operation (`Bar/Bar.ShapeEditor.cpp:1136–1137`; `.trellis/spec/native-desktop/shapes.md:336–337`; `Core/shape_core.h:63`).

## Needs runtime test

Extend these existing fixtures; no builds or tests were executed during this read-only investigation:

- UI action/icon/visibility/order checks: `Bar/Bar.ShapeEditor.cpp:3694–3700`; transformed docking/reopen/undo fixture: `:3165–3183`.
- All-preset geometry, reversible transforms, post-transform handle editing and GPU width checks: `Tests/shape_core_tests.cpp:1150–1198`.
- Identity rejection and Replace/undo/redo/cold-history fixtures: `Draw/Draw3.ShapeRuntimeTests.inl:203–227`, `:256–280`, `:293–326`.
- Transform metadata and full UInk roundtrip: `Tests/shape_uink_tests.inl:509–536`.
- Verify actual icon pixels, hit areas, narrow-workspace fitting, themes/DPI, clockwise direction, reflected objects, eight 45°/four 90° rotations, unchanged constraints/width, and rejection of solids. Existing contracts require real content-pixel checks and distinguish offscreen from device testing (`.trellis/spec/native-desktop/shapes.md:73`, `:349`).

## Recommendation

1. Add `RotateShape` beside `FlipShape`, using the rotate handle’s pivot calculation and outer rotation; preserve construction and rebuild. Reject solids explicitly rather than calling a helper that silently discards their rotation (`Core/shape_interaction.h:21`; `Core/shape_interaction.inl:261–269`, `:507`).
2. Append `Rotate45Clockwise`/`Rotate90Clockwise` actions; dispatch in `BuildShapePropertyCandidate` and add both to runtime’s property-action whitelist (`Draw/Draw3.ShapeInteraction.h:42`, `:78`; `Draw/Draw3.ShapeRuntimeActions.inl:637–640`).
3. Append button IDs before Count; order **Flip H, Flip V, Rotate 45°, Rotate 90°, Finish** in `ToolbarItems`. Add resources, semantic labels, activation mappings and planar/non-Circle visibility; retain existing two-row/group layout (`Bar/Bar.ShapeEditor.cpp:52–57`, `:388`, `:1127–1139`, `:1166–1191`, `:2088`).
4. Add `barShapeRotate45.svg`/`barShapeRotate90.svg` through the same RC/project/filter registrations and color tokens (`Inkeys/Inkeys.rc:421–422`; `Inkeys/Inkeys.vcxproj:1267–1268`; `Inkeys/Inkeys.vcxproj.filters:2767–2771`).
5. Add both labels across the 20 locale sources, run the existing generator/check workflow, then extend the fixtures above (`Scripts/i18n.ps1:708–741`).