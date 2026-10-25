# ImFluent source snapshot

- Upstream: https://github.com/lukaasm/ImFluent
- Commit: `d1faa09e980ef795276263267baa23c7d6aeb3d4`
- License: MIT, see `LICENSE`
- Validated Dear ImGui version: `1.92.7` (`IMGUI_VERSION_NUM == 19270`)

## Vendored files

- `imfluent.cpp`
- `imfluent.h`
- `imfluent_internal.h`
- `imfluent_icons.h`

The demo and upstream build files are intentionally not vendored. Inkeys compiles the
source directly and continues to use its own Win32/DX11 backends and embedded CSO shaders.
Upstream automation, Gallery, and platform backdrop backends are not vendored. The new
core backdrop API remains inactive (`ImFluentBackdrop_None`); settings retain their native
Mica path. Dear ImGui stays pinned at 1.92.7.

This snapshot includes upstream popup-blocking checks, keyboard actions, rounded-border
rasterization fixes, and theme changes that preserve customized metrics and text.
The separate settings ComboBox adapter mirrors the relevant interaction/drawing fixes
while retaining the project scroll, clipping, and animation behavior.

## Local patches

Search for `INKEYS PATCH`. Local changes are deliberately limited to:

1. Exposing `SetFluentTextStyleFont()` so Setting can bind embedded project fonts.
2. Adding `ResetContext()` to clear ImFluent's process-global state before the Dear ImGui
   context is destroyed or recreated.
3. Adding `SetIconFont()` / `DrawIcon()` so navigation and settings rows use the
   bundled Fluent icon face independently from text, centered by its actual glyph bounds.
4. Extracting the vertical `NavItem()` core as `NavItemEx()` for the settings shell's
   explicit pane geometry. It retains ImFluent interaction/focus behavior, stable IDs,
   a fixed icon axis, item-local bounds, and a compact tooltip. The glyph size uses
   `StandardIconSize` so desktop navigation density can be adjusted centrally. Upstream
   `NavMenuItemFlags()` / `NavMenuItemNav()` still participate when used inside the
   built-in NavigationView; settings-owned panes remain ordinary navigation stops.
5. Keeping expander header text out of the chevron slot (full text remains available
   in a tooltip), and avoiding an unmatched expander scope when the body is clipped.
6. Respecting hidden `##` / `###` suffixes in hyperlink and expander labels, so translated
   text can keep a stable interaction ID without rendering the suffix.
7. Interpolating shared animated colors in alpha-weighted RGB before returning straight
   RGBA, including transparent endpoints. This prevents transient dark hover fills when
   navigation and popup items transition from transparent black to an opaque light color.
   Durations, target colors, item IDs, and interaction behavior remain unchanged. The
   regression samples actual multi-frame NavItemEx/Button/ComboBox/Selectable DrawData.
8. Guarding `CheckedFlags()` / `OpenedFlags()` with `IMGUI_ENABLE_TEST_ENGINE`, just
   like their `ReportItemInfo()` consumer. Dear ImGui 1.92.7 only declares these semantic
   flags with its Test Engine enabled; production builds return zero without changing
   widget interaction, selection, or expansion state. No Test Engine dependency is added.
9. Keeping `ImGuiChildFlags_AlwaysUseWindowPadding` on `BeginCard()` after upstream
   replaced the native child border with manual drawing. Without it, Dear ImGui 1.92.7
   clears a borderless child's actual padding, defeating `CardPadding` and making project
   text measurement disagree with the available drawing width. The manual border,
   `NoBackground`, and flattened navigation are retained.

The new settings shell composes ImGui child regions with these ImFluent widgets.
The original navigation container remains available; no rendering backend is replaced.

The project adapter in `Inkeys/Inkeys/UI/Setting/Setting.Wrap.h` rejects Dear ImGui versions
other than 1.92.7 at compile time. The gate deliberately lives outside the vendor snapshot,
because it is an integration constraint rather than an upstream source patch.

Do not call `LoadFluentSystemFonts()` or `LoadFluentFonts()` from Inkeys. They load external
font files and would bypass the project's embedded-font compatibility path and typography.
Continue binding embedded text fonts with `SetFluentTextStyleFont()` and the icon face
with `SetIconFont()`.

## Upgrade procedure

1. Replace the four source files with the desired upstream commit.
2. Reapply the local patches listed above and update this file.
3. Compile with the pinned Dear ImGui headers for ARM64 using the project's C++20 flags.
4. Build `InkeysRepo.sln` as `Debug | ARM64`, run headless tests, then exercise every
   Setting page, theme, DPI transition, resize, Hide/Show, and device-epoch recovery when
   interactive GUI testing is explicitly authorized. Headless validation must use
   `--no-window --setting-home-only` and the existing offscreen WARP tests.
