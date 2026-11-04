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

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2026-01-19: DirectX11: Added 'SamplerNearest' in ImGui_ImplDX11_RenderState. Renamed 'SamplerDefault' to 'SamplerLinear'.
//  2025-09-18: Call platform_io.ClearRendererHandlers() on shutdown.
//  2025-06-11: DirectX11: Added support for ImGuiBackendFlags_RendererHasTextures, for dynamic font atlas.
//  2025-05-07: DirectX11: Honor draw_data->FramebufferScale to allow for custom backends and experiment using it (consistently with other renderer backends, even though in normal condition it is not set under Windows).
//  2025-01-06: DirectX11: Expose VertexConstantBuffer in ImGui_ImplDX11_RenderState. Reset projection matrix in ImDrawCallback_ResetRenderState handler.
//  2024-10-07: DirectX11: Changed default texture sampler to Clamp instead of Repeat/Wrap.
//  2024-10-07: DirectX11: Expose selected render state in ImGui_ImplDX11_RenderState, which you can access in 'void* platform_io.Renderer_RenderState' during draw callbacks.
//  2022-10-11: Using 'nullptr' instead of 'NULL' as per our switch to C++11.
//  2021-06-29: Reorganized backend to pull data from a single structure to facilitate usage with multiple-contexts (all g_XXXX access changed to bd->XXXX).
//  2021-05-19: DirectX11: Replaced direct access to ImDrawCmd::TextureId with a call to ImDrawCmd::GetTexID(). (will become a requirement)
//  2021-02-18: DirectX11: Change blending equation to preserve alpha in output buffer.
//  2019-08-01: DirectX11: Fixed code querying the Geometry Shader state (would generally error with Debug layer enabled).
//  2019-07-21: DirectX11: Backup, clear and restore Geometry Shader is any is bound when calling ImGui_ImplDX11_RenderDrawData. Clearing Hull/Domain/Compute shaders without backup/restore.
//  2019-05-29: DirectX11: Added support for large mesh (64K+ vertices), enable ImGuiBackendFlags_RendererHasVtxOffset flag.
//  2019-04-30: DirectX11: Added support for special ImDrawCallback_ResetRenderState callback to reset render state.
//  2018-12-03: Misc: Added #pragma comment statement to automatically link with d3dcompiler.lib when using D3DCompile().
//  2018-11-30: Misc: Setting up io.BackendRendererName so it can be displayed in the About Window.
//  2018-08-01: DirectX11: Querying for IDXGIFactory instead of IDXGIFactory1 to increase compatibility.
//  2018-07-13: DirectX11: Fixed unreleased resources in Init and Shutdown functions.
//  2018-06-08: Misc: Extracted imgui_impl_dx11.cpp/.h away from the old combined DX11+Win32 example.
//  2018-06-08: DirectX11: Use draw_data->DisplayPos and draw_data->DisplaySize to setup projection matrix and clipping rectangle.
//  2018-02-16: Misc: Obsoleted the io.RenderDrawListsFn callback and exposed ImGui_ImplDX11_RenderDrawData() in the .h file so you can call it yourself.
//  2018-02-06: Misc: Removed call to ImGui::Shutdown() which is not available from 1.60 WIP, user needs to call CreateContext/DestroyContext themselves.
//  2016-05-07: DirectX11: Disabling depth-write.

#include "imgui.h"
#ifndef IMGUI_DISABLE
#include "imgui_impl_dx11.h"

// DirectX
#include <stdio.h>
#include <math.h>
#include <limits.h>
#include <d3d11.h>
// [Inkeys] 自定义开始：声明 EXE 内嵌 CSO 的 Win32 资源 ID。
#include <windows.h>
#include "../../resource.h"
// [Inkeys] 自定义结束

// Clang/GCC warnings with -Weverything
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wold-style-cast"         // warning: use of old-style cast                            // yes, they are more terse.
#pragma clang diagnostic ignored "-Wsign-conversion"        // warning: implicit conversion changes signedness
#endif

// DirectX11 data
struct ImGui_ImplDX11_Texture
{
    ID3D11Texture2D*            pTexture;
    ID3D11ShaderResourceView*   pTextureView;
    ImGui_ImplDX11_Texture*     Next;
    bool                       AlphaR8;
};

// [Inkeys] 每帧由 DrawData 解析的组：帧缓冲像素范围、嵌套层级及透明度；不跨帧保存命令指针。
static const int ImGui_ImplDX11_OpacityGroupLevels = 4;
struct ImGui_ImplDX11_OpacityGroup
{
    D3D11_RECT                  Rect;
    float                       Opacity;
    int                         Depth;
    bool                        Closed;
    bool                        Valid;
};

struct ImGui_ImplDX11_Data
{
    ID3D11Device*               pd3dDevice;
    ID3D11DeviceContext*        pd3dDeviceContext;
    IDXGIFactory*               pFactory;
    ID3D11Buffer*               pVB;
    ID3D11Buffer*               pIB;
    ID3D11VertexShader*         pVertexShader;
    ID3D11InputLayout*          pInputLayout;
    ID3D11Buffer*               pVertexConstantBuffer;
    ID3D11PixelShader*          pPixelShader;
    ID3D11PixelShader*          pAlphaPixelShader;
    ImGui_ImplDX11_Texture*     Textures;
    bool                       SupportsR8;
    bool                       ForceRgbaAlpha;
    ID3D11SamplerState*         pTexSamplerLinear;
    ID3D11SamplerState*         pTexSamplerNearest;
    ID3D11RasterizerState*      pRasterizerState;
    ID3D11BlendState*           pBlendState;
    ID3D11DepthStencilState*    pDepthStencilState;
    int                         VertexBufferSize;
    int                         IndexBufferSize;
    HRESULT                     LastResult;
    // [Inkeys] 整块透明度：每层嵌套一张与目标同尺寸的离屏纹理，空闲后释放；随设备对象一起失效。
    ID3D11Texture2D*            pGroupTexture[ImGui_ImplDX11_OpacityGroupLevels];
    ID3D11RenderTargetView*     pGroupTarget[ImGui_ImplDX11_OpacityGroupLevels];
    ID3D11ShaderResourceView*   pGroupView[ImGui_ImplDX11_OpacityGroupLevels];
    UINT                        GroupWidth, GroupHeight;
    ID3D11Buffer*               pGroupVB;
    int                         GroupVertexCapacity;
    ID3D11BlendState*           pGroupClearBlend;
    ID3D11BlendState*           pGroupCompositeBlend;
    ULONGLONG                   GroupLastUse;
    ImVector<ImGui_ImplDX11_OpacityGroup> Groups;
#if defined(INKEYS_IMGUI_DX11_TESTING) || (defined(INKEYS_ENABLE_UI3_PRESENTATION_HARNESS) && INKEYS_ENABLE_UI3_PRESENTATION_HARNESS)
    ImGui_ImplDX11_TestFailure   TestFailure;
    HRESULT                     TestResult;
#endif

    ImGui_ImplDX11_Data()       { memset((void*)this, 0, sizeof(*this)); VertexBufferSize = 5000; IndexBufferSize = 10000; }
};

struct VERTEX_CONSTANT_BUFFER_DX11
{
    float   mvp[4][4];
};

// Backend data stored in io.BackendRendererUserData to allow support for multiple Dear ImGui contexts
// It is STRONGLY preferred that you use docking branch with multi-viewports (== single Dear ImGui context + multiple windows) instead of multiple Dear ImGui contexts.
static ImGui_ImplDX11_Data* ImGui_ImplDX11_GetBackendData()
{
    return ImGui::GetCurrentContext() ? (ImGui_ImplDX11_Data*)ImGui::GetIO().BackendRendererUserData : nullptr;
}

// [Inkeys] 失败留在各自 context，清理不得覆盖原 HRESULT；由共享管线调用方判定恢复路径。
static bool ImGui_ImplDX11_CheckResult(HRESULT result)
{
    if (SUCCEEDED(result)) return true;
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    if (SUCCEEDED(bd->LastResult)) bd->LastResult = result;
    return false;
}

long ImGui_ImplDX11_GetLastResult()
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    return bd ? bd->LastResult : E_FAIL;
}

// [Inkeys] 自定义开始：整块透明度离屏合成。
template<typename T> static inline T GroupMin(T a, T b) { return a < b ? a : b; }
template<typename T> static inline T GroupMax(T a, T b) { return a < b ? b : a; }
bool ImGui_ImplDX11_SupportsOpacityGroups()
{
    return ImGui_ImplDX11_GetBackendData() != nullptr;
}

static void ImGui_ImplDX11_ReleaseOpacityGroupTextures(ImGui_ImplDX11_Data* bd)
{
    for (int level = 0; level < ImGui_ImplDX11_OpacityGroupLevels; level++)
    {
        if (bd->pGroupView[level])    { bd->pGroupView[level]->Release(); bd->pGroupView[level] = nullptr; }
        if (bd->pGroupTarget[level])  { bd->pGroupTarget[level]->Release(); bd->pGroupTarget[level] = nullptr; }
        if (bd->pGroupTexture[level]) { bd->pGroupTexture[level]->Release(); bd->pGroupTexture[level] = nullptr; }
    }
    bd->GroupWidth = bd->GroupHeight = 0;
}

// 按渲染时相同的列表/命令顺序配对哨兵，求各组实际像素范围；返回需要的离屏层数。
static int ImGui_ImplDX11_ResolveOpacityGroups(ImGui_ImplDX11_Data* bd, const ImDrawData* draw_data)
{
    bd->Groups.resize(0);
    const ImVec2 off = draw_data->DisplayPos, scale = draw_data->FramebufferScale;
    const LONG width = (LONG)(draw_data->DisplaySize.x * scale.x), height = (LONG)(draw_data->DisplaySize.y * scale.y);
    int stack[16];
    for (const ImDrawList* draw_list : draw_data->CmdLists)
    {
        int depth = 0;
        for (const ImDrawCmd& cmd : draw_list->CmdBuffer)
        {
            if (cmd.UserCallback == ImGui_ImplDX11_OpacityGroupBegin)
            {
                ImGui_ImplDX11_OpacityGroup group = {};
                group.Rect = { LONG_MAX, LONG_MAX, LONG_MIN, LONG_MIN };
                const unsigned int bits = (unsigned int)(uintptr_t)cmd.UserCallbackData;
                memcpy(&group.Opacity, &bits, sizeof(group.Opacity));
                group.Depth = depth;
                if (depth < IM_ARRAYSIZE(stack)) stack[depth] = bd->Groups.Size;
                depth++;
                bd->Groups.push_back(group);
            }
            else if (cmd.UserCallback == ImGui_ImplDX11_OpacityGroupEnd)
            {
                if (depth == 0) continue;
                if (--depth >= IM_ARRAYSIZE(stack)) continue;
                ImGui_ImplDX11_OpacityGroup& group = bd->Groups[stack[depth]];
                group.Closed = true;
                if (depth > 0 && depth - 1 < IM_ARRAYSIZE(stack) && group.Rect.left < group.Rect.right)
                {
                    // 子组像素也属于父组；父组只合成一次它自己的离屏结果。
                    D3D11_RECT& parent = bd->Groups[stack[depth - 1]].Rect;
                    parent.left = GroupMin(parent.left, group.Rect.left); parent.top = GroupMin(parent.top, group.Rect.top);
                    parent.right = GroupMax(parent.right, group.Rect.right); parent.bottom = GroupMax(parent.bottom, group.Rect.bottom);
                }
            }
            else if (cmd.UserCallback == nullptr && cmd.ElemCount > 0 && depth > 0 && depth <= IM_ARRAYSIZE(stack))
            {
                float min_x = FLT_MAX, min_y = FLT_MAX, max_x = -FLT_MAX, max_y = -FLT_MAX;
                for (unsigned int element = 0; element < cmd.ElemCount; element++)
                {
                    const ImVec2 pos = draw_list->VtxBuffer.Data[cmd.VtxOffset + draw_list->IdxBuffer.Data[cmd.IdxOffset + element]].pos;
                    min_x = GroupMin(min_x, pos.x); min_y = GroupMin(min_y, pos.y); max_x = GroupMax(max_x, pos.x); max_y = GroupMax(max_y, pos.y);
                }
                // 与逐命令 scissor 相同的截断规则；顶点外接框覆盖抗锯齿边缘。
                const LONG left = GroupMax(GroupMax((LONG)floorf((min_x - off.x) * scale.x), (LONG)((cmd.ClipRect.x - off.x) * scale.x)), 0L);
                const LONG top = GroupMax(GroupMax((LONG)floorf((min_y - off.y) * scale.y), (LONG)((cmd.ClipRect.y - off.y) * scale.y)), 0L);
                const LONG right = GroupMin(GroupMin((LONG)ceilf((max_x - off.x) * scale.x), (LONG)((cmd.ClipRect.z - off.x) * scale.x)), width);
                const LONG bottom = GroupMin(GroupMin((LONG)ceilf((max_y - off.y) * scale.y), (LONG)((cmd.ClipRect.w - off.y) * scale.y)), height);
                if (left >= right || top >= bottom) continue;
                D3D11_RECT& rect = bd->Groups[stack[depth - 1]].Rect;
                rect.left = GroupMin(rect.left, left); rect.top = GroupMin(rect.top, top);
                rect.right = GroupMax(rect.right, right); rect.bottom = GroupMax(rect.bottom, bottom);
            }
        }
    }
    int levels = 0;
    for (ImGui_ImplDX11_OpacityGroup& group : bd->Groups)
    {
        // 未配对或无可见像素的组按普通命令直接绘制；超出层级只会出现在异常嵌套中。
        group.Valid = group.Closed && group.Depth < ImGui_ImplDX11_OpacityGroupLevels
            && group.Rect.left < group.Rect.right && group.Rect.top < group.Rect.bottom;
        if (group.Valid) levels = GroupMax(levels, group.Depth + 1);
    }
    return levels;
}

// 创建本帧所需离屏层并一次上传全部清除/合成四边形；失败经 LastResult 交给调用方仲裁，不提交半成品。
static bool ImGui_ImplDX11_PrepareOpacityGroups(ImGui_ImplDX11_Data* bd, const ImDrawData* draw_data, ID3D11RenderTargetView* base, int levels)
{
    if (!base) { for (ImGui_ImplDX11_OpacityGroup& group : bd->Groups) group.Valid = false; return true; }
    ID3D11Resource* resource = nullptr;
    base->GetResource(&resource);
    ID3D11Texture2D* texture = nullptr;
    const HRESULT query = resource ? resource->QueryInterface(IID_PPV_ARGS(&texture)) : E_NOINTERFACE;
    if (resource) resource->Release();
    if (!ImGui_ImplDX11_CheckResult(query)) return false;
    D3D11_TEXTURE2D_DESC target_desc = {};
    texture->GetDesc(&target_desc);
    texture->Release();
    if (bd->GroupWidth != target_desc.Width || bd->GroupHeight != target_desc.Height)
        ImGui_ImplDX11_ReleaseOpacityGroupTextures(bd);
    bd->GroupWidth = target_desc.Width; bd->GroupHeight = target_desc.Height;
    for (int level = 0; level < levels; level++)
    {
        if (bd->pGroupView[level]) continue;
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = target_desc.Width; desc.Height = target_desc.Height;
        desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (!ImGui_ImplDX11_CheckResult(bd->pd3dDevice->CreateTexture2D(&desc, nullptr, &bd->pGroupTexture[level]))
            || !ImGui_ImplDX11_CheckResult(bd->pd3dDevice->CreateRenderTargetView(bd->pGroupTexture[level], nullptr, &bd->pGroupTarget[level]))
            || !ImGui_ImplDX11_CheckResult(bd->pd3dDevice->CreateShaderResourceView(bd->pGroupTexture[level], nullptr, &bd->pGroupView[level])))
        {
            ImGui_ImplDX11_ReleaseOpacityGroupTextures(bd);
            return false;
        }
    }
    if (!bd->pGroupClearBlend || !bd->pGroupCompositeBlend)
    {
        // 清除直接写零；合成输入为预乘 RGBA，RGB/A 同乘组透明度后按 ONE/INV_SRC_ALPHA 叠回。
        D3D11_BLEND_DESC desc = {};
        desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (!bd->pGroupClearBlend && !ImGui_ImplDX11_CheckResult(bd->pd3dDevice->CreateBlendState(&desc, &bd->pGroupClearBlend)))
            return false;
        desc.RenderTarget[0].BlendEnable = true;
        desc.RenderTarget[0].SrcBlend = desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        desc.RenderTarget[0].DestBlend = desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[0].BlendOp = desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        if (!bd->pGroupCompositeBlend && !ImGui_ImplDX11_CheckResult(bd->pd3dDevice->CreateBlendState(&desc, &bd->pGroupCompositeBlend)))
            return false;
    }
    const int vertices = bd->Groups.Size * 12;
    if (!bd->pGroupVB || bd->GroupVertexCapacity < vertices)
    {
        if (bd->pGroupVB) { bd->pGroupVB->Release(); bd->pGroupVB = nullptr; }
        bd->GroupVertexCapacity = vertices + 48;
        D3D11_BUFFER_DESC desc = {};
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.ByteWidth = bd->GroupVertexCapacity * sizeof(ImDrawVert);
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (!ImGui_ImplDX11_CheckResult(bd->pd3dDevice->CreateBuffer(&desc, nullptr, &bd->pGroupVB)))
            return false;
    }
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (!ImGui_ImplDX11_CheckResult(bd->pd3dDeviceContext->Map(bd->pGroupVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return false;
    ImDrawVert* vertex = (ImDrawVert*)mapped.pData;
    const ImVec2 off = draw_data->DisplayPos, scale = draw_data->FramebufferScale;
    for (const ImGui_ImplDX11_OpacityGroup& group : bd->Groups)
    {
        // 四边形顶点落在整像素边界，采样点即离屏纹素中心；颜色 (o,o,o,o) 让默认 PS 输出预乘结果。
        const float x0 = group.Rect.left / scale.x + off.x, y0 = group.Rect.top / scale.y + off.y;
        const float x1 = group.Rect.right / scale.x + off.x, y1 = group.Rect.bottom / scale.y + off.y;
        const float u0 = (float)group.Rect.left / bd->GroupWidth, v0 = (float)group.Rect.top / bd->GroupHeight;
        const float u1 = (float)group.Rect.right / bd->GroupWidth, v1 = (float)group.Rect.bottom / bd->GroupHeight;
        const ImU32 value = (ImU32)(GroupMin(GroupMax(group.Opacity, 0.0f), 1.0f) * 255.0f + 0.5f);
        const ImU32 color = IM_COL32(value, value, value, value);
        const ImDrawVert quad[12] =
        {
            { { x0, y0 }, { 0, 0 }, 0 }, { { x1, y0 }, { 0, 0 }, 0 }, { { x1, y1 }, { 0, 0 }, 0 },
            { { x0, y0 }, { 0, 0 }, 0 }, { { x1, y1 }, { 0, 0 }, 0 }, { { x0, y1 }, { 0, 0 }, 0 },
            { { x0, y0 }, { u0, v0 }, color }, { { x1, y0 }, { u1, v0 }, color }, { { x1, y1 }, { u1, v1 }, color },
            { { x0, y0 }, { u0, v0 }, color }, { { x1, y1 }, { u1, v1 }, color }, { { x0, y1 }, { u0, v1 }, color },
        };
        memcpy(vertex, quad, sizeof(quad));
        vertex += 12;
    }
    bd->pd3dDeviceContext->Unmap(bd->pGroupVB, 0);
    bd->GroupLastUse = GetTickCount64();
    return true;
}

static void ImGui_ImplDX11_DrawOpacityQuad(ImGui_ImplDX11_Data* bd, const D3D11_RECT& rect, UINT first,
    ID3D11BlendState* blend, ID3D11ShaderResourceView* view, ID3D11PixelShader* restore_shader)
{
    ID3D11DeviceContext* device = bd->pd3dDeviceContext;
    const float factor[4] = { 0.f, 0.f, 0.f, 0.f };
    const UINT stride = sizeof(ImDrawVert), offset = 0;
    device->IASetVertexBuffers(0, 1, &bd->pGroupVB, &stride, &offset);
    device->OMSetBlendState(blend, factor, 0xffffffff);
    device->RSSetScissorRects(1, &rect);
    device->PSSetShader(bd->pPixelShader, nullptr, 0);
    device->PSSetShaderResources(0, 1, &view);
    device->PSSetSamplers(0, 1, &bd->pTexSamplerNearest);
    device->Draw(6, first);
    // 还原逐命令依赖的固定状态；纹理和 scissor 由下一条普通命令重新设置。
    ID3D11ShaderResourceView* none = nullptr;
    device->PSSetShaderResources(0, 1, &none);
    device->PSSetSamplers(0, 1, &bd->pTexSamplerLinear);
    device->PSSetShader(restore_shader, nullptr, 0);
    device->OMSetBlendState(bd->pBlendState, factor, 0xffffffff);
    device->IASetVertexBuffers(0, 1, &bd->pVB, &stride, &offset);
}
// [Inkeys] 自定义结束

#if defined(INKEYS_IMGUI_DX11_TESTING) || (defined(INKEYS_ENABLE_UI3_PRESENTATION_HARNESS) && INKEYS_ENABLE_UI3_PRESENTATION_HARNESS)
void ImGui_ImplDX11_TestFailNext(ImGui_ImplDX11_TestFailure stage, long result)
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    bd->TestFailure = stage; bd->TestResult = result;
}
static HRESULT ImGui_ImplDX11_TestResult(ImGui_ImplDX11_TestFailure stage)
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    if (bd->TestFailure != stage) return S_OK;
    bd->TestFailure = ImGui_ImplDX11_TestFailure_None;
    return bd->TestResult;
}
void ImGui_ImplDX11_TestPublishPartialTexture(ImTextureData* tex, ID3D11Texture2D* texture)
{
    IM_ASSERT(tex->BackendUserData == nullptr);
    ImGui_ImplDX11_Texture* partial = IM_NEW(ImGui_ImplDX11_Texture)();
    partial->pTexture = texture;
    if (texture) texture->AddRef();
    tex->BackendUserData = partial;
    tex->SetTexID(ImTextureID_Invalid);
    tex->SetStatus(ImTextureStatus_OK);
}
#endif

// Functions
#if defined(INKEYS_IMGUI_DX11_TESTING) || (defined(INKEYS_ENABLE_UI3_PRESENTATION_HARNESS) && INKEYS_ENABLE_UI3_PRESENTATION_HARNESS)
void ImGui_ImplDX11_TestForceRgbaAlpha(bool force)
{
    ImGui_ImplDX11_GetBackendData()->ForceRgbaAlpha = force;
}
#endif

static void ImGui_ImplDX11_SetupRenderState(const ImDrawData* draw_data, ID3D11DeviceContext* device_ctx)
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();

    // Setup viewport
    D3D11_VIEWPORT vp = {};
    vp.Width = draw_data->DisplaySize.x * draw_data->FramebufferScale.x;
    vp.Height = draw_data->DisplaySize.y * draw_data->FramebufferScale.y;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = vp.TopLeftY = 0;
    device_ctx->RSSetViewports(1, &vp);

    // Setup orthographic projection matrix into our constant buffer
    // Our visible imgui space lies from draw_data->DisplayPos (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayPos is (0,0) for single viewport apps.
    D3D11_MAPPED_SUBRESOURCE mapped_resource;
    if (!ImGui_ImplDX11_CheckResult(device_ctx->Map(bd->pVertexConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource)))
        return;
    {
        VERTEX_CONSTANT_BUFFER_DX11* constant_buffer = (VERTEX_CONSTANT_BUFFER_DX11*)mapped_resource.pData;
        float L = draw_data->DisplayPos.x;
        float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
        float T = draw_data->DisplayPos.y;
        float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
        float mvp[4][4] =
        {
            { 2.0f/(R-L),   0.0f,           0.0f,       0.0f },
            { 0.0f,         2.0f/(T-B),     0.0f,       0.0f },
            { 0.0f,         0.0f,           0.5f,       0.0f },
            { (R+L)/(L-R),  (T+B)/(B-T),    0.5f,       1.0f },
        };
        memcpy(&constant_buffer->mvp, mvp, sizeof(mvp));
        device_ctx->Unmap(bd->pVertexConstantBuffer, 0);
    }

    // Setup shader and vertex buffers
    unsigned int stride = sizeof(ImDrawVert);
    unsigned int offset = 0;
    device_ctx->IASetInputLayout(bd->pInputLayout);
    device_ctx->IASetVertexBuffers(0, 1, &bd->pVB, &stride, &offset);
    device_ctx->IASetIndexBuffer(bd->pIB, sizeof(ImDrawIdx) == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, 0);
    device_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    device_ctx->VSSetShader(bd->pVertexShader, nullptr, 0);
    device_ctx->VSSetConstantBuffers(0, 1, &bd->pVertexConstantBuffer);
    device_ctx->PSSetShader(bd->pPixelShader, nullptr, 0);
    device_ctx->PSSetSamplers(0, 1, &bd->pTexSamplerLinear);
    device_ctx->GSSetShader(nullptr, nullptr, 0);
    device_ctx->HSSetShader(nullptr, nullptr, 0); // In theory we should backup and restore this as well.. very infrequently used..
    device_ctx->DSSetShader(nullptr, nullptr, 0); // In theory we should backup and restore this as well.. very infrequently used..
    device_ctx->CSSetShader(nullptr, nullptr, 0); // In theory we should backup and restore this as well.. very infrequently used..

    // Setup render state
    const float blend_factor[4] = { 0.f, 0.f, 0.f, 0.f };
    device_ctx->OMSetBlendState(bd->pBlendState, blend_factor, 0xffffffff);
    device_ctx->OMSetDepthStencilState(bd->pDepthStencilState, 0);
    device_ctx->RSSetState(bd->pRasterizerState);
}

// Render function
void ImGui_ImplDX11_RenderDrawData(ImDrawData* draw_data)
{
    // Avoid rendering when minimized
    if (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f)
        return;

    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    ID3D11DeviceContext* device = bd->pd3dDeviceContext;
    // NewFrame 的资源创建失败时不提交残缺状态，下次 NewFrame 再按原调度重试。
    if (!bd->pVertexShader || !bd->pInputLayout || !bd->pVertexConstantBuffer || !bd->pPixelShader
        || !bd->pAlphaPixelShader || !bd->pBlendState || !bd->pRasterizerState || !bd->pDepthStencilState
        || !bd->pTexSamplerLinear || !bd->pTexSamplerNearest)
    {
        if (FAILED(bd->LastResult) || !ImGui_ImplDX11_CreateDeviceObjects()) return;
    }
    bd->LastResult = S_OK;

    // Catch up with texture updates. Most of the times, the list will have 1 element with an OK status, aka nothing to do.
    // (This almost always points to ImGui::GetPlatformIO().Textures[] but is part of ImDrawData to allow overriding or disabling texture updates).
    if (draw_data->Textures != nullptr)
        for (ImTextureData* tex : *draw_data->Textures)
        {
            if (tex->Status != ImTextureStatus_OK)
                ImGui_ImplDX11_UpdateTexture(tex);
            // 未上传成功时不可调用 draw command 的 GetTexID 或继续绘制。
            if (FAILED(bd->LastResult)) return;
            if (tex->Status == ImTextureStatus_OK && tex->TexID == ImTextureID_Invalid)
            { ImGui_ImplDX11_CheckResult(E_FAIL); return; }
        }

    // Create and grow vertex/index buffers if needed
    if (!bd->pVB || bd->VertexBufferSize < draw_data->TotalVtxCount)
    {
        if (bd->pVB) { bd->pVB->Release(); bd->pVB = nullptr; }
        bd->VertexBufferSize = draw_data->TotalVtxCount + 5000;
        D3D11_BUFFER_DESC desc = {};
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.ByteWidth = bd->VertexBufferSize * sizeof(ImDrawVert);
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags = 0;
        if (!ImGui_ImplDX11_CheckResult(bd->pd3dDevice->CreateBuffer(&desc, nullptr, &bd->pVB)))
            return;
    }
    if (!bd->pIB || bd->IndexBufferSize < draw_data->TotalIdxCount)
    {
        if (bd->pIB) { bd->pIB->Release(); bd->pIB = nullptr; }
        bd->IndexBufferSize = draw_data->TotalIdxCount + 10000;
        D3D11_BUFFER_DESC desc = {};
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.ByteWidth = bd->IndexBufferSize * sizeof(ImDrawIdx);
        desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (!ImGui_ImplDX11_CheckResult(bd->pd3dDevice->CreateBuffer(&desc, nullptr, &bd->pIB)))
            return;
    }

    // Upload vertex/index data into a single contiguous GPU buffer
    D3D11_MAPPED_SUBRESOURCE vtx_resource = {}, idx_resource = {};
    if (!ImGui_ImplDX11_CheckResult(device->Map(bd->pVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &vtx_resource)))
        return;
    HRESULT index_map_result = S_OK;
#if defined(INKEYS_IMGUI_DX11_TESTING) || (defined(INKEYS_ENABLE_UI3_PRESENTATION_HARNESS) && INKEYS_ENABLE_UI3_PRESENTATION_HARNESS)
    index_map_result = ImGui_ImplDX11_TestResult(ImGui_ImplDX11_TestFailure_IndexMap);
#endif
    if (SUCCEEDED(index_map_result)) index_map_result = device->Map(bd->pIB, 0, D3D11_MAP_WRITE_DISCARD, 0, &idx_resource);
    if (!ImGui_ImplDX11_CheckResult(index_map_result))
    {
        // 第二次 Map 失败也必须归还已经映射的顶点缓冲，避免下一次重试污染状态。
        device->Unmap(bd->pVB, 0);
        return;
    }
    ImDrawVert* vtx_dst = (ImDrawVert*)vtx_resource.pData;
    ImDrawIdx* idx_dst = (ImDrawIdx*)idx_resource.pData;
    for (const ImDrawList* draw_list : draw_data->CmdLists)
    {
        memcpy(vtx_dst, draw_list->VtxBuffer.Data, draw_list->VtxBuffer.Size * sizeof(ImDrawVert));
        memcpy(idx_dst, draw_list->IdxBuffer.Data, draw_list->IdxBuffer.Size * sizeof(ImDrawIdx));
        vtx_dst += draw_list->VtxBuffer.Size;
        idx_dst += draw_list->IdxBuffer.Size;
    }
    device->Unmap(bd->pVB, 0);
    device->Unmap(bd->pIB, 0);

    // [Inkeys] 整块透明度：只有 DrawData 含哨兵时才解析和准备离屏层；空闲两秒后归还纹理显存。
    const int group_levels = ImGui_ImplDX11_ResolveOpacityGroups(bd, draw_data);
    ID3D11RenderTargetView* group_base_target = nullptr;
    ID3D11DepthStencilView* group_base_depth = nullptr;
    if (group_levels > 0)
    {
        device->OMGetRenderTargets(1, &group_base_target, &group_base_depth);
        if (!ImGui_ImplDX11_PrepareOpacityGroups(bd, draw_data, group_base_target, group_levels))
        {
            if (group_base_target) group_base_target->Release();
            if (group_base_depth) group_base_depth->Release();
            return;
        }
    }
    else if (bd->pGroupTexture[0] && GetTickCount64() - bd->GroupLastUse > 2000)
        ImGui_ImplDX11_ReleaseOpacityGroupTextures(bd);
    int group_cursor = 0, group_depth = 0;
    int group_stack[16];
    // Backup DX state that will be modified to restore it afterwards (unfortunately this is very ugly looking and verbose. Close your eyes!)
    struct BACKUP_DX11_STATE
    {
        UINT                        ScissorRectsCount, ViewportsCount;
        D3D11_RECT                  ScissorRects[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        D3D11_VIEWPORT              Viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        ID3D11RasterizerState*      RS;
        ID3D11BlendState*           BlendState;
        FLOAT                       BlendFactor[4];
        UINT                        SampleMask;
        UINT                        StencilRef;
        ID3D11DepthStencilState*    DepthStencilState;
        ID3D11ShaderResourceView*   PSShaderResource;
        ID3D11SamplerState*         PSSampler;
        ID3D11PixelShader*          PS;
        ID3D11VertexShader*         VS;
        ID3D11GeometryShader*       GS;
        UINT                        PSInstancesCount, VSInstancesCount, GSInstancesCount;
        ID3D11ClassInstance         *PSInstances[256], *VSInstances[256], *GSInstances[256];   // 256 is max according to PSSetShader documentation
        D3D11_PRIMITIVE_TOPOLOGY    PrimitiveTopology;
        ID3D11Buffer*               IndexBuffer, *VertexBuffer, *VSConstantBuffer;
        UINT                        IndexBufferOffset, VertexBufferStride, VertexBufferOffset;
        DXGI_FORMAT                 IndexBufferFormat;
        ID3D11InputLayout*          InputLayout;
    };
    BACKUP_DX11_STATE old = {};
    old.ScissorRectsCount = old.ViewportsCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    device->RSGetScissorRects(&old.ScissorRectsCount, old.ScissorRects);
    device->RSGetViewports(&old.ViewportsCount, old.Viewports);
    device->RSGetState(&old.RS);
    device->OMGetBlendState(&old.BlendState, old.BlendFactor, &old.SampleMask);
    device->OMGetDepthStencilState(&old.DepthStencilState, &old.StencilRef);
    device->PSGetShaderResources(0, 1, &old.PSShaderResource);
    device->PSGetSamplers(0, 1, &old.PSSampler);
    old.PSInstancesCount = old.VSInstancesCount = old.GSInstancesCount = 256;
    device->PSGetShader(&old.PS, old.PSInstances, &old.PSInstancesCount);
    device->VSGetShader(&old.VS, old.VSInstances, &old.VSInstancesCount);
    device->VSGetConstantBuffers(0, 1, &old.VSConstantBuffer);
    device->GSGetShader(&old.GS, old.GSInstances, &old.GSInstancesCount);

    device->IAGetPrimitiveTopology(&old.PrimitiveTopology);
    device->IAGetIndexBuffer(&old.IndexBuffer, &old.IndexBufferFormat, &old.IndexBufferOffset);
    device->IAGetVertexBuffers(0, 1, &old.VertexBuffer, &old.VertexBufferStride, &old.VertexBufferOffset);
    device->IAGetInputLayout(&old.InputLayout);

    // Setup desired DX state
    ImGui_ImplDX11_SetupRenderState(draw_data, device);

    // Setup render state structure (for callbacks and custom texture bindings)
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    ImGui_ImplDX11_RenderState render_state;
    render_state.Device = bd->pd3dDevice;
    render_state.DeviceContext = bd->pd3dDeviceContext;
    render_state.SamplerLinear = bd->pTexSamplerLinear;
    render_state.SamplerNearest = bd->pTexSamplerNearest;
    render_state.VertexConstantBuffer = bd->pVertexConstantBuffer;
    render_state.PixelShader = bd->pPixelShader;
    render_state.AlphaPixelShader = bd->pAlphaPixelShader;
    platform_io.Renderer_RenderState = &render_state;

    bool automaticPixelShader = true;
    ID3D11PixelShader* selectedPixelShader = bd->pPixelShader;
    // Render command lists
    // (Because we merged all buffers into a single one, we maintain our own offset into them)
    int global_idx_offset = 0;
    int global_vtx_offset = 0;
    ImVec2 clip_off = draw_data->DisplayPos;
    ImVec2 clip_scale = draw_data->FramebufferScale;
    for (const ImDrawList* draw_list : draw_data->CmdLists)
    {
        if (FAILED(bd->LastResult)) break;
        for (int cmd_i = 0; cmd_i < draw_list->CmdBuffer.Size && SUCCEEDED(bd->LastResult); cmd_i++)
        {
            const ImDrawCmd* pcmd = &draw_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != nullptr)
            {
                // User callback, registered via ImDrawList::AddCallback()
                // (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
                if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
                {
                    ImGui_ImplDX11_SetupRenderState(draw_data, device);
                    automaticPixelShader = true;
                    selectedPixelShader = bd->pPixelShader;
                }
                else if (pcmd->UserCallback == ImGui_ImplDX11_OpacityGroupBegin)
                {
                    // 组内命令按原混合画入清零的透明层，得到整块的预乘 RGBA。
                    const int index = group_cursor++;
                    // 只有全部外层也已离屏时才进入下一层，异常哨兵流退回普通绘制而不丢内容。
                    const bool parent = group_depth == 0 || (group_depth <= IM_ARRAYSIZE(group_stack) && group_stack[group_depth - 1] >= 0);
                    const bool active = group_levels > 0 && parent && index < bd->Groups.Size && bd->Groups[index].Valid;
                    if (group_depth < IM_ARRAYSIZE(group_stack)) group_stack[group_depth] = active ? index : -1;
                    group_depth++;
                    if (active)
                    {
                        const ImGui_ImplDX11_OpacityGroup& group = bd->Groups[index];
                        ID3D11ShaderResourceView* none = nullptr;
                        device->PSSetShaderResources(0, 1, &none);
                        device->OMSetRenderTargets(1, &bd->pGroupTarget[group.Depth], nullptr);
                        ImGui_ImplDX11_DrawOpacityQuad(bd, group.Rect, (UINT)index * 12, bd->pGroupClearBlend, nullptr, selectedPixelShader);
                    }
                }
                else if (pcmd->UserCallback == ImGui_ImplDX11_OpacityGroupEnd)
                {
                    if (group_depth == 0) continue;
                    const int index = --group_depth < IM_ARRAYSIZE(group_stack) ? group_stack[group_depth] : -1;
                    if (index >= 0)
                    {
                        // 回到父层后整体乘透明度合成一次，重叠图元不再分别透出下层。
                        const ImGui_ImplDX11_OpacityGroup& group = bd->Groups[index];
                        if (group.Depth == 0) device->OMSetRenderTargets(1, &group_base_target, group_base_depth);
                        else device->OMSetRenderTargets(1, &bd->pGroupTarget[group.Depth - 1], nullptr);
                        ImGui_ImplDX11_DrawOpacityQuad(bd, group.Rect, (UINT)index * 12 + 6, bd->pGroupCompositeBlend,
                            bd->pGroupView[group.Depth], selectedPixelShader);
                    }
                }
                else
                {
                    pcmd->UserCallback(draw_list, pcmd);
                    // 无操作/采样器回调不改变着色策略；只有外部PS接管后才停止自动选择。
                    ID3D11PixelShader* active = nullptr;
                    device->PSGetShader(&active, nullptr, nullptr);
                    automaticPixelShader = active == bd->pPixelShader || active == bd->pAlphaPixelShader;
                    selectedPixelShader = active;
                    if (active) active->Release();
                }
            }
            else
            {
                // Project scissor/clipping rectangles into framebuffer space
                ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x, (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
                ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x, (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);
                if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
                    continue;

                // Apply scissor/clipping rectangle
                const D3D11_RECT r = { (LONG)clip_min.x, (LONG)clip_min.y, (LONG)clip_max.x, (LONG)clip_max.y };
                device->RSSetScissorRects(1, &r);

                // Bind texture, Draw
                ID3D11ShaderResourceView* texture_srv = (ID3D11ShaderResourceView*)pcmd->GetTexID();
                if (automaticPixelShader)
                {
                    // 按SRV身份识别，兼容缓存DrawData只保存裸TexID的路径。
                    bool alpha = false;
                    for (auto* owned = bd->Textures; owned; owned = owned->Next)
                        if (owned->pTextureView == texture_srv) { alpha = owned->AlphaR8; break; }
                    auto* shader = alpha ? bd->pAlphaPixelShader : bd->pPixelShader;
                    if (shader != selectedPixelShader) { device->PSSetShader(shader, nullptr, 0); selectedPixelShader = shader; }
                }
                device->PSSetShaderResources(0, 1, &texture_srv);
                device->DrawIndexed(pcmd->ElemCount, pcmd->IdxOffset + global_idx_offset, pcmd->VtxOffset + global_vtx_offset);
            }
        }
        global_idx_offset += draw_list->IdxBuffer.Size;
        global_vtx_offset += draw_list->VtxBuffer.Size;
        group_depth = 0; // 与解析相同，哨兵只在单个列表内配对。
    }
    platform_io.Renderer_RenderState = nullptr;
    if (group_levels > 0)
    {
        // 中途失败也必须回到调用方目标，离屏层不能残留为后续 Present/Copy 的输出。
        device->OMSetRenderTargets(1, &group_base_target, group_base_depth);
        if (group_base_target) group_base_target->Release();
        if (group_base_depth) group_base_depth->Release();
    }

    // Restore modified DX state
    device->RSSetScissorRects(old.ScissorRectsCount, old.ScissorRects);
    device->RSSetViewports(old.ViewportsCount, old.Viewports);
    device->RSSetState(old.RS); if (old.RS) old.RS->Release();
    device->OMSetBlendState(old.BlendState, old.BlendFactor, old.SampleMask); if (old.BlendState) old.BlendState->Release();
    device->OMSetDepthStencilState(old.DepthStencilState, old.StencilRef); if (old.DepthStencilState) old.DepthStencilState->Release();
    device->PSSetShaderResources(0, 1, &old.PSShaderResource); if (old.PSShaderResource) old.PSShaderResource->Release();
    device->PSSetSamplers(0, 1, &old.PSSampler); if (old.PSSampler) old.PSSampler->Release();
    device->PSSetShader(old.PS, old.PSInstances, old.PSInstancesCount); if (old.PS) old.PS->Release();
    for (UINT i = 0; i < old.PSInstancesCount; i++) if (old.PSInstances[i]) old.PSInstances[i]->Release();
    device->VSSetShader(old.VS, old.VSInstances, old.VSInstancesCount); if (old.VS) old.VS->Release();
    device->VSSetConstantBuffers(0, 1, &old.VSConstantBuffer); if (old.VSConstantBuffer) old.VSConstantBuffer->Release();
    device->GSSetShader(old.GS, old.GSInstances, old.GSInstancesCount); if (old.GS) old.GS->Release();
    for (UINT i = 0; i < old.VSInstancesCount; i++) if (old.VSInstances[i]) old.VSInstances[i]->Release();
    device->IASetPrimitiveTopology(old.PrimitiveTopology);
    device->IASetIndexBuffer(old.IndexBuffer, old.IndexBufferFormat, old.IndexBufferOffset); if (old.IndexBuffer) old.IndexBuffer->Release();
    device->IASetVertexBuffers(0, 1, &old.VertexBuffer, &old.VertexBufferStride, &old.VertexBufferOffset); if (old.VertexBuffer) old.VertexBuffer->Release();
    device->IASetInputLayout(old.InputLayout); if (old.InputLayout) old.InputLayout->Release();
}

static void ImGui_ImplDX11_DestroyTexture(ImTextureData* tex)
{
    if (ImGui_ImplDX11_Texture* backend_tex = (ImGui_ImplDX11_Texture*)tex->BackendUserData)
    {
        IM_ASSERT(backend_tex->pTextureView == (ID3D11ShaderResourceView*)(intptr_t)tex->TexID);
        // 可撤销旧版本遗留的空/半成品资源，重复清理不会重复 Release。
        auto** link = &ImGui_ImplDX11_GetBackendData()->Textures;
        while (*link && *link != backend_tex) link = &(*link)->Next;
        if (*link) *link = backend_tex->Next;
        if (backend_tex->pTextureView) backend_tex->pTextureView->Release();
        if (backend_tex->pTexture) backend_tex->pTexture->Release();
        IM_DELETE(backend_tex);

        // Clear identifiers and mark as destroyed (in order to allow e.g. calling InvalidateDeviceObjects while running)
        tex->BackendUserData = nullptr;
    }
    tex->SetTexID(ImTextureID_Invalid);
    tex->SetStatus(ImTextureStatus_Destroyed);
}

// 临时展开缓冲允许失败；失败不消费纹理状态，下一帧可重试。
struct ImGui_ImplDX11_AlphaExpansion
{
    ImU32* Data = nullptr;
    int Size = 0;
    ~ImGui_ImplDX11_AlphaExpansion() { IM_FREE(Data); }
    bool resize(int count)
    {
#if defined(INKEYS_IMGUI_DX11_TESTING) || (defined(INKEYS_ENABLE_UI3_PRESENTATION_HARNESS) && INKEYS_ENABLE_UI3_PRESENTATION_HARNESS)
        if (!ImGui_ImplDX11_CheckResult(ImGui_ImplDX11_TestResult(ImGui_ImplDX11_TestFailure_TextureConversion))) return false;
#endif
        Data = static_cast<ImU32*>(IM_ALLOC(size_t(count) * sizeof(ImU32)));
        if (!Data) return ImGui_ImplDX11_CheckResult(E_OUTOFMEMORY);
        Size = count;
        return true;
    }
    bool empty() const { return Data == nullptr; }
    ImU32& operator[](int index) { return Data[index]; }
};

void ImGui_ImplDX11_UpdateTexture(ImTextureData* tex)
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    bd->LastResult = S_OK;
    if (tex->Status == ImTextureStatus_WantCreate)
    {
        // Create and upload new texture to graphics system
        //IMGUI_DEBUG_LOG("UpdateTexture #%03d: WantCreate %dx%d\n", tex->UniqueID, tex->Width, tex->Height);
        IM_ASSERT(tex->TexID == ImTextureID_Invalid && tex->BackendUserData == nullptr);
        IM_ASSERT(tex->Format == ImTextureFormat_RGBA32 || tex->Format == ImTextureFormat_Alpha8);
        // 单通道覆盖率直接上传R8；不支持该格式的设备保留RGBA展开回退。
        const bool alphaR8 = tex->Format == ImTextureFormat_Alpha8 && bd->SupportsR8 && !bd->ForceRgbaAlpha;
        ImGui_ImplDX11_AlphaExpansion converted;
        if (tex->Format == ImTextureFormat_Alpha8 && !alphaR8)
        {
            if (!converted.resize(tex->Width * tex->Height)) return;
            const auto* alpha = static_cast<const unsigned char*>(tex->GetPixels());
            for (int i = 0; i < converted.Size; ++i) converted[i] = 0x00ffffffU | (ImU32(alpha[i]) << 24);
        }
        const void* pixels = converted.empty() ? tex->GetPixels() : converted.Data;
        ImGui_ImplDX11_Texture* backend_tex = IM_NEW(ImGui_ImplDX11_Texture)();

        // Create texture
        D3D11_TEXTURE2D_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.Width = (UINT)tex->Width;
        desc.Height = (UINT)tex->Height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = alphaR8 ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;
        D3D11_SUBRESOURCE_DATA subResource;
        subResource.pSysMem = pixels;
        subResource.SysMemPitch = desc.Width * (alphaR8 ? 1 : 4);
        subResource.SysMemSlicePitch = 0;
        HRESULT result = S_OK;
#if defined(INKEYS_IMGUI_DX11_TESTING) || (defined(INKEYS_ENABLE_UI3_PRESENTATION_HARNESS) && INKEYS_ENABLE_UI3_PRESENTATION_HARNESS)
        result = ImGui_ImplDX11_TestResult(ImGui_ImplDX11_TestFailure_Texture);
#endif
        if (SUCCEEDED(result)) result = bd->pd3dDevice->CreateTexture2D(&desc, &subResource, &backend_tex->pTexture);
        if (SUCCEEDED(result) && !backend_tex->pTexture) result = E_FAIL;
        if (!ImGui_ImplDX11_CheckResult(result))
        {
            if (backend_tex->pTexture) backend_tex->pTexture->Release();
            IM_DELETE(backend_tex);
            return; // 保留 WantCreate/无 ID，允许完整重试。
        }

        // Create texture view
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
        ZeroMemory(&srvDesc, sizeof(srvDesc));
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = desc.MipLevels;
        srvDesc.Texture2D.MostDetailedMip = 0;
#if defined(INKEYS_IMGUI_DX11_TESTING) || (defined(INKEYS_ENABLE_UI3_PRESENTATION_HARNESS) && INKEYS_ENABLE_UI3_PRESENTATION_HARNESS)
        result = ImGui_ImplDX11_TestResult(ImGui_ImplDX11_TestFailure_TextureView);
#endif
        if (SUCCEEDED(result)) result = bd->pd3dDevice->CreateShaderResourceView(backend_tex->pTexture, &srvDesc, &backend_tex->pTextureView);
        if (SUCCEEDED(result) && !backend_tex->pTextureView) result = E_FAIL;
        if (!ImGui_ImplDX11_CheckResult(result))
        {
            if (backend_tex->pTextureView) backend_tex->pTextureView->Release();
            backend_tex->pTexture->Release();
            IM_DELETE(backend_tex);
            return; // SRV 失败也撤销本次 texture，禁止发布半成品。
        }

        backend_tex->AlphaR8 = alphaR8;
        backend_tex->Next = bd->Textures;
        bd->Textures = backend_tex;
        // Store identifiers
        tex->SetTexID((ImTextureID)(intptr_t)backend_tex->pTextureView);
        tex->SetStatus(ImTextureStatus_OK);
        tex->BackendUserData = backend_tex;
    }
    else if (tex->Status == ImTextureStatus_WantUpdates)
    {
        // Update selected blocks. We only ever write to textures regions which have never been used before!
        // This backend choose to use tex->Updates[] but you can use tex->UpdateRect to upload a single region.
        ImGui_ImplDX11_Texture* backend_tex = (ImGui_ImplDX11_Texture*)tex->BackendUserData;
        if (!backend_tex || !backend_tex->pTexture || !backend_tex->pTextureView)
        {
            ImGui_ImplDX11_DestroyTexture(tex);
            ImGui_ImplDX11_CheckResult(E_FAIL);
            return;
        }
        IM_ASSERT(backend_tex->pTextureView == (ID3D11ShaderResourceView*)(intptr_t)tex->TexID);
#if defined(INKEYS_IMGUI_DX11_TESTING) || (defined(INKEYS_ENABLE_UI3_PRESENTATION_HARNESS) && INKEYS_ENABLE_UI3_PRESENTATION_HARNESS)
        if (!ImGui_ImplDX11_CheckResult(ImGui_ImplDX11_TestResult(ImGui_ImplDX11_TestFailure_TextureUpdate))) return;
#endif
        for (ImTextureRect& r : tex->Updates)
        {
            D3D11_BOX box = { (UINT)r.x, (UINT)r.y, (UINT)0, (UINT)(r.x + r.w), (UINT)(r.y + r .h), (UINT)1 };
            ImGui_ImplDX11_AlphaExpansion converted;
            const void* pixels = tex->GetPixelsAt(r.x, r.y);
            UINT pitch = (UINT)tex->GetPitch();
            if (tex->Format == ImTextureFormat_Alpha8 && !backend_tex->AlphaR8)
            {
                // 只展开本次脏矩形，转换缓冲在提交后释放，不扩大字体稳态内存。
                if (!converted.resize(r.w * r.h)) return;
                for (int y = 0; y < r.h; ++y)
                {
                    const auto* alpha = static_cast<const unsigned char*>(tex->GetPixelsAt(r.x, r.y + y));
                    for (int x = 0; x < r.w; ++x) converted[y * r.w + x] = 0x00ffffffU | (ImU32(alpha[x]) << 24);
                }
                pixels = converted.Data;
                pitch = r.w * sizeof(ImU32);
            }
            bd->pd3dDeviceContext->UpdateSubresource(backend_tex->pTexture, 0, &box, pixels, pitch, 0);
        }
        // UpdateSubresource 无 HRESULT；确认设备仍有效后才能消费更新请求。
        if (!ImGui_ImplDX11_CheckResult(bd->pd3dDevice->GetDeviceRemovedReason())) return;
        tex->SetStatus(ImTextureStatus_OK);
    }
    if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0)
        ImGui_ImplDX11_DestroyTexture(tex);
}

bool    ImGui_ImplDX11_CreateDeviceObjects()
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    if (!bd->pd3dDevice)
        return false;
    bd->LastResult = S_OK;
    ImGui_ImplDX11_InvalidateDeviceObjects();

    // [Inkeys] 自定义开始：仅将 shader 创建从运行时编译改为 EXE 内嵌 CSO。
    // 资源映射必须与 Inkeys.rc 一致：IDR_SHADERS2=VS，IDR_SHADERS1=PS。
    auto load_embedded_shader = [](UINT resource_id, const void** shader_data, SIZE_T* shader_size) -> bool
    {
        HMODULE module = GetModuleHandleW(nullptr);
        HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(resource_id), L"SHADERS");
        if (resource == nullptr)
            return false;

        HGLOBAL loaded_resource = LoadResource(module, resource);
        const DWORD resource_size = SizeofResource(module, resource);
        const void* resource_data = loaded_resource ? LockResource(loaded_resource) : nullptr;
        if (resource_data == nullptr || resource_size == 0)
            return false;

        *shader_data = resource_data;
        *shader_size = static_cast<SIZE_T>(resource_size);
        return true;
    };

    const void* vertex_shader_data = nullptr;
    SIZE_T vertex_shader_size = 0;
    const void* pixel_shader_data = nullptr;
    SIZE_T pixel_shader_size = 0;
    if (!load_embedded_shader(IDR_SHADERS2, &vertex_shader_data, &vertex_shader_size) ||
        !load_embedded_shader(IDR_SHADERS1, &pixel_shader_data, &pixel_shader_size))
    { ImGui_ImplDX11_CheckResult(E_FAIL); return false; }

    if (!ImGui_ImplDX11_CheckResult(bd->pd3dDevice->CreateVertexShader(vertex_shader_data, vertex_shader_size, nullptr, &bd->pVertexShader)))
    { ImGui_ImplDX11_InvalidateDeviceObjects(); return false; }

    D3D11_INPUT_ELEMENT_DESC local_layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0, (UINT)offsetof(ImDrawVert, pos), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,   0, (UINT)offsetof(ImDrawVert, uv),  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, (UINT)offsetof(ImDrawVert, col), D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    if (!ImGui_ImplDX11_CheckResult(bd->pd3dDevice->CreateInputLayout(local_layout, 3, vertex_shader_data, vertex_shader_size, &bd->pInputLayout)))
    {
        ImGui_ImplDX11_InvalidateDeviceObjects();
        return false;
    }

    D3D11_BUFFER_DESC constant_buffer_desc = {};
    constant_buffer_desc.ByteWidth = sizeof(VERTEX_CONSTANT_BUFFER_DX11);
    constant_buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
    constant_buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    constant_buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (!ImGui_ImplDX11_CheckResult(bd->pd3dDevice->CreateBuffer(&constant_buffer_desc, nullptr, &bd->pVertexConstantBuffer)))
    {
        ImGui_ImplDX11_InvalidateDeviceObjects();
        return false;
    }

    if (!ImGui_ImplDX11_CheckResult(bd->pd3dDevice->CreatePixelShader(pixel_shader_data, pixel_shader_size, nullptr, &bd->pPixelShader)))
    {
        ImGui_ImplDX11_InvalidateDeviceObjects();
        return false;
    }
    const void* alpha_shader_data = nullptr;
    SIZE_T alpha_shader_size = 0;
    if (!load_embedded_shader(IDR_SETTING_IMGUI_ALPHA_PS, &alpha_shader_data, &alpha_shader_size))
    { ImGui_ImplDX11_CheckResult(E_FAIL); ImGui_ImplDX11_InvalidateDeviceObjects(); return false; }
    if (!ImGui_ImplDX11_CheckResult(bd->pd3dDevice->CreatePixelShader(alpha_shader_data, alpha_shader_size, nullptr, &bd->pAlphaPixelShader)))
    { ImGui_ImplDX11_InvalidateDeviceObjects(); return false; }
    UINT support = 0;
    bd->SupportsR8 = SUCCEEDED(bd->pd3dDevice->CheckFormatSupport(DXGI_FORMAT_R8_UNORM, &support))
        && (support & (D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE))
        == (D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE);
    // [Inkeys] 自定义结束

    // Create the blending setup
    {
        D3D11_BLEND_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.AlphaToCoverageEnable = false;
        desc.RenderTarget[0].BlendEnable = true;
        desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (!ImGui_ImplDX11_CheckResult(bd->pd3dDevice->CreateBlendState(&desc, &bd->pBlendState)))
        { ImGui_ImplDX11_InvalidateDeviceObjects(); return false; }
    }

    // Create the rasterizer state
    {
        D3D11_RASTERIZER_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.FillMode = D3D11_FILL_SOLID;
        desc.CullMode = D3D11_CULL_NONE;
        desc.ScissorEnable = true;
        desc.DepthClipEnable = true;
        if (!ImGui_ImplDX11_CheckResult(bd->pd3dDevice->CreateRasterizerState(&desc, &bd->pRasterizerState)))
        { ImGui_ImplDX11_InvalidateDeviceObjects(); return false; }
    }

    // Create depth-stencil State
    {
        D3D11_DEPTH_STENCIL_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.DepthEnable = false;
        desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
        desc.StencilEnable = false;
        desc.FrontFace.StencilFailOp = desc.FrontFace.StencilDepthFailOp = desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
        desc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
        desc.BackFace = desc.FrontFace;
        if (!ImGui_ImplDX11_CheckResult(bd->pd3dDevice->CreateDepthStencilState(&desc, &bd->pDepthStencilState)))
        { ImGui_ImplDX11_InvalidateDeviceObjects(); return false; }
    }

    // Create texture sampler
    // (Bilinear sampling is required by default. Set 'io.Fonts->Flags |= ImFontAtlasFlags_NoBakedLines' or 'style.AntiAliasedLinesUseTex = false' to allow point/nearest sampling)
    {
        D3D11_SAMPLER_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.MipLODBias = 0.f;
        desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        desc.MinLOD = 0.f;
        desc.MaxLOD = 0.f;
        if (!ImGui_ImplDX11_CheckResult(bd->pd3dDevice->CreateSamplerState(&desc, &bd->pTexSamplerLinear)))
        { ImGui_ImplDX11_InvalidateDeviceObjects(); return false; }
        desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        if (!ImGui_ImplDX11_CheckResult(bd->pd3dDevice->CreateSamplerState(&desc, &bd->pTexSamplerNearest)))
        { ImGui_ImplDX11_InvalidateDeviceObjects(); return false; }
    }

    return true;
}

void    ImGui_ImplDX11_InvalidateDeviceObjects()
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    if (!bd->pd3dDevice)
        return;

    // Destroy all textures
    for (ImTextureData* tex : ImGui::GetPlatformIO().Textures)
        if (tex->RefCount == 1)
            ImGui_ImplDX11_DestroyTexture(tex);

    // [Inkeys] 整块透明度资源与设备对象同生命周期。
    ImGui_ImplDX11_ReleaseOpacityGroupTextures(bd);
    if (bd->pGroupVB)               { bd->pGroupVB->Release(); bd->pGroupVB = nullptr; }
    bd->GroupVertexCapacity = 0;
    if (bd->pGroupClearBlend)       { bd->pGroupClearBlend->Release(); bd->pGroupClearBlend = nullptr; }
    if (bd->pGroupCompositeBlend)   { bd->pGroupCompositeBlend->Release(); bd->pGroupCompositeBlend = nullptr; }
    if (bd->pTexSamplerLinear)      { bd->pTexSamplerLinear->Release(); bd->pTexSamplerLinear = nullptr; }
    if (bd->pTexSamplerNearest)     { bd->pTexSamplerNearest->Release(); bd->pTexSamplerNearest = nullptr; }
    if (bd->pIB)                    { bd->pIB->Release(); bd->pIB = nullptr; }
    if (bd->pVB)                    { bd->pVB->Release(); bd->pVB = nullptr; }
    if (bd->pBlendState)            { bd->pBlendState->Release(); bd->pBlendState = nullptr; }
    if (bd->pDepthStencilState)     { bd->pDepthStencilState->Release(); bd->pDepthStencilState = nullptr; }
    if (bd->pRasterizerState)       { bd->pRasterizerState->Release(); bd->pRasterizerState = nullptr; }
    if (bd->pAlphaPixelShader)      { bd->pAlphaPixelShader->Release(); bd->pAlphaPixelShader = nullptr; }
    if (bd->pPixelShader)           { bd->pPixelShader->Release(); bd->pPixelShader = nullptr; }
    if (bd->pVertexConstantBuffer)  { bd->pVertexConstantBuffer->Release(); bd->pVertexConstantBuffer = nullptr; }
    if (bd->pInputLayout)           { bd->pInputLayout->Release(); bd->pInputLayout = nullptr; }
    if (bd->pVertexShader)          { bd->pVertexShader->Release(); bd->pVertexShader = nullptr; }
}

bool    ImGui_ImplDX11_Init(ID3D11Device* device, ID3D11DeviceContext* device_context)
{
    return ImGui_ImplDX11_Init(device, device_context, nullptr);
}

bool    ImGui_ImplDX11_Init(ID3D11Device* device, ID3D11DeviceContext* device_context, long* result_out)
{
    if (result_out) *result_out = S_OK;
    ImGuiIO& io = ImGui::GetIO();
    IMGUI_CHECKVERSION();
    IM_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized a renderer backend!");

    if (!device || !device_context)
    {
        if (result_out) *result_out = E_INVALIDARG;
        return false;
    }
    // factory 和 lease 引用完整后才发布 backend；恢复期 OOM 不得空指针 AddRef。
    ImGui_ImplDX11_Data* bd = IM_NEW(ImGui_ImplDX11_Data)();
    // Get factory from device
    IDXGIDevice* pDXGIDevice = nullptr;
    IDXGIAdapter* pDXGIAdapter = nullptr;
    IDXGIFactory* pFactory = nullptr;

    HRESULT result = device->QueryInterface(IID_PPV_ARGS(&pDXGIDevice));
    if (SUCCEEDED(result)) result = pDXGIDevice->GetParent(IID_PPV_ARGS(&pDXGIAdapter));
    if (SUCCEEDED(result)) result = pDXGIAdapter->GetParent(IID_PPV_ARGS(&pFactory));
    if (pDXGIDevice) pDXGIDevice->Release();
    if (pDXGIAdapter) pDXGIAdapter->Release();
    if (FAILED(result) || !pFactory)
    {
        if (result_out) *result_out = FAILED(result) ? result : E_FAIL;
        if (pFactory) pFactory->Release();
        IM_DELETE(bd);
        return false;
    }
    bd->pd3dDevice = device;
    bd->pd3dDeviceContext = device_context;
    bd->pFactory = pFactory;
    bd->pd3dDevice->AddRef();
    bd->pd3dDeviceContext->AddRef();

    io.BackendRendererUserData = (void*)bd;
    io.BackendRendererName = "imgui_impl_dx11";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;   // We can honor ImGuiPlatformIO::Textures[] requests during render.

    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    platform_io.Renderer_TextureMaxWidth = platform_io.Renderer_TextureMaxHeight = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;

    return true;
}

void ImGui_ImplDX11_Shutdown()
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    IM_ASSERT(bd != nullptr && "No renderer backend to shutdown, or already shutdown?");
    ImGuiIO& io = ImGui::GetIO();
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();

    ImGui_ImplDX11_InvalidateDeviceObjects();
    if (bd->pFactory)             { bd->pFactory->Release(); }
    if (bd->pd3dDevice)           { bd->pd3dDevice->Release(); }
    if (bd->pd3dDeviceContext)    { bd->pd3dDeviceContext->Release(); }

    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
    platform_io.ClearRendererHandlers();
    IM_DELETE(bd);
}

void ImGui_ImplDX11_NewFrame()
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    IM_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call ImGui_ImplDX11_Init()?");

    if (!bd->pVertexShader)
        ImGui_ImplDX11_CreateDeviceObjects(); // 运行时资源失败交给调用方恢复，不能靠断言继续提交。
}

//-----------------------------------------------------------------------------

#endif // #ifndef IMGUI_DISABLE
