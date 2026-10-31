// dear imgui: Renderer Backend for DirectX11
// This needs to be used along with a Platform Backend (e.g. Win32)

// Implemented features:
//  [X] Renderer: User texture binding. Use 'ID3D11ShaderResourceView*' as texture identifier. Read the FAQ about ImTextureID/ImTextureRef!
//  [X] Renderer: Large meshes support (64k+ vertices) even with 16-bit indices (ImGuiBackendFlags_RendererHasVtxOffset).
//  [X] Renderer: Texture updates support for dynamic font atlas (ImGuiBackendFlags_RendererHasTextures).
//  [X] Renderer: Expose selected render state for draw callbacks to use. Access in '(ImGui_ImplXXXX_RenderState*)GetPlatformIO().Renderer_RenderState'.

// You can use unmodified imgui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire imgui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#pragma once
#include "imgui.h"      // IMGUI_IMPL_API
#ifndef IMGUI_DISABLE

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11SamplerState;
struct ID3D11Buffer;

// Follow "Getting Started" link and check examples/ folder to learn about using backends!
IMGUI_IMPL_API bool     ImGui_ImplDX11_Init(ID3D11Device* device, ID3D11DeviceContext* device_context);
// [Inkeys] 初始化尚无 backend owner 时，也向调用方保留实际 HRESULT；原两参数入口不变。
IMGUI_IMPL_API bool     ImGui_ImplDX11_Init(ID3D11Device* device, ID3D11DeviceContext* device_context, long* result);
IMGUI_IMPL_API void     ImGui_ImplDX11_Shutdown();
IMGUI_IMPL_API void     ImGui_ImplDX11_NewFrame();
IMGUI_IMPL_API void     ImGui_ImplDX11_RenderDrawData(ImDrawData* draw_data);
// [Inkeys] 保留原 void API；调用方在 Present 前读取本 context 最近一次提交/创建的 HRESULT。
IMGUI_IMPL_API long     ImGui_ImplDX11_GetLastResult();

// Use if you want to reset your rendering device without losing Dear ImGui state.
IMGUI_IMPL_API bool     ImGui_ImplDX11_CreateDeviceObjects();
IMGUI_IMPL_API void     ImGui_ImplDX11_InvalidateDeviceObjects();

// (Advanced) Use e.g. if you need to precisely control the timing of texture updates (e.g. for staged rendering), by setting ImDrawData::Textures = nullptr to handle this manually.
IMGUI_IMPL_API void     ImGui_ImplDX11_UpdateTexture(ImTextureData* tex);

#ifdef INKEYS_IMGUI_DX11_TESTING
// 仅无窗口测试可注入一次故障；产品编译不含这些入口或状态。
enum ImGui_ImplDX11_TestFailure { ImGui_ImplDX11_TestFailure_None, ImGui_ImplDX11_TestFailure_Texture,
    ImGui_ImplDX11_TestFailure_TextureView, ImGui_ImplDX11_TestFailure_IndexMap };
struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;
IMGUI_IMPL_API void ImGui_ImplDX11_TestFailNext(ImGui_ImplDX11_TestFailure stage, long result);
IMGUI_IMPL_API void ImGui_ImplDX11_TestPublishPartialTexture(ImTextureData* tex, ID3D11Texture2D* texture);
#endif

// [BETA] Selected render state data shared with callbacks.
// This is temporarily stored in GetPlatformIO().Renderer_RenderState during the ImGui_ImplDX11_RenderDrawData() call.
// (Please open an issue if you feel you need access to more data)
struct ImGui_ImplDX11_RenderState
{
    ID3D11Device*           Device;
    ID3D11DeviceContext*    DeviceContext;
    ID3D11SamplerState*     SamplerLinear;
    ID3D11SamplerState*     SamplerNearest;
    ID3D11Buffer*           VertexConstantBuffer;
};

#endif // #ifndef IMGUI_DISABLE
