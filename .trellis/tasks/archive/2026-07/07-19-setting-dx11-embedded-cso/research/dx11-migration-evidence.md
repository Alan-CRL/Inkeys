# DX11 migration evidence

## Current implementation

- `Setting.Base.cppm` currently owns `LPDIRECT3D9`, `LPDIRECT3DDEVICE9`, `D3DPRESENT_PARAMETERS`, `PDIRECT3DTEXTURE9[11]`.
- `Setting.cpp` initializes/renders/shuts down through `ImGui_ImplDX9_*`, handles D3D9 lost/reset, and has ten populated image slots.
- `Setting.Wrap.h` includes `imgui_impl_dx9.h`, `d3d9.h`, and D3DX9 texture declarations.
- `Inkeys.vcxproj` compiles DX9 backend but not DX11 backend.

## ImGui texture contract

The local backend headers/comments are authoritative for the checked-out ImGui version:

- DX9 user texture ID: `LPDIRECT3DTEXTURE9`
- DX11 user texture ID: `ID3D11ShaderResourceView*`

The rendering call may retain `(ImTextureID)(intptr_t)TextureSettingSign[i]`, but the pointed object type must become an SRV and its lifetime must outlive draw submission.

## Old archive

`Inkeys/Inkeys3Temp20260502.7z` contains an earlier incomplete DX11 migration. Useful evidence:

- old `Setting.Base.cppm` uses `ID3D11ShaderResourceView*` and creates `ID3D11Texture2D` + SRV;
- old rendering code demonstrates RTV/device ownership;
- parts of `Setting.cpp` remain DX9 and the archive is not suitable for whole-file restoration.

## Existing embedded shader path

- `.rc`: `IDR_SHADERS1` embeds `imgui_ps.cso`; `IDR_SHADERS2` embeds `imgui_vs.cso`.
- HEAD's prior `imgui_impl_dx11.cpp` custom block attempted embedded CSO loading and documented removal of `d3dcompiler_47.dll`.
- The current working backend update restored upstream runtime `D3DCompile()`.
- The old custom code loaded resource 299 as VS and 300 as PS, opposite to `.rc`.
- `Inkeys::Load::ExtractResourcePtr` takes `const std::wstring&`; passing `MAKEINTRESOURCEW(...)` is unsafe, so the backend should use direct Win32 resource APIs or a separately designed numeric-resource overload.

## CSO inspection

ARM64 Windows SDK `fxc /dumpbin` reported:

- `imgui_vs.cso`: `vs_2_0`, no D3D11-style input signature; unsuitable.
- `imgui_ps.cso`: `ps_5_0`, currently swaps R/B through `.zyxw`.

`Inkeys.vcxproj` explicitly configures pixel shader model 5.0 but leaves vertex ShaderType/ShaderModel unset, explaining the stale/default vertex profile. Both profiles and color behavior require correction and regeneration.
