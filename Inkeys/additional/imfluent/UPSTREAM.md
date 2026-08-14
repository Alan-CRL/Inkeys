# ImFluent source snapshot

- Upstream: https://github.com/lukaasm/ImFluent
- Commit: `fe7cf3ef784afc81aed55f24f39fcaa15cdf96cb`
- License: MIT, see `LICENSE`
- Validated Dear ImGui version: `1.92.7` (`IMGUI_VERSION_NUM == 19270`)

## Vendored files

- `imfluent.cpp`
- `imfluent.h`
- `imfluent_internal.h`
- `imfluent_icons.h`

The demo and upstream build files are intentionally not vendored. Inkeys compiles the
source directly and continues to use its own Win32/DX11 backends and embedded CSO shaders.

## Local patches

Search for `INKEYS PATCH`. Local changes are deliberately limited to:

1. Exposing `SetFluentTextStyleFont()` so Setting can bind embedded project fonts.
2. Adding `ResetContext()` to clear ImFluent's process-global state before the Dear ImGui
   context is destroyed or recreated.

The project adapter in `Inkeys/Inkeys/UI/Setting/Setting.cpp` rejects Dear ImGui versions
other than 1.92.7 at compile time. The gate deliberately lives outside the vendor snapshot,
because it is an integration constraint rather than an upstream source patch.

Do not call `LoadFluentSystemFonts()` from Inkeys. It depends on installed Segoe fonts and
would bypass the project's embedded-font compatibility path.

## Upgrade procedure

1. Replace the four source files with the desired upstream commit.
2. Reapply only the two patches listed above and update this file.
3. Compile with the pinned Dear ImGui headers for ARM64 using the project's C++20 flags.
4. Build `InkeysRepo.sln` as `Debug | ARM64`, run headless tests, then exercise every
   Setting page, theme, DPI transition, resize, Hide/Show, and device-epoch recovery.
