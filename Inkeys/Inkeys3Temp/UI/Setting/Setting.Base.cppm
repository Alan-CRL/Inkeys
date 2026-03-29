module;

#include "Setting.Wrap.h"

#include <wrl/client.h>

#define _CRT_SECURE_NO_WARNINGS
#define STB_IMAGE_IMPLEMENTATION
#include "stbimage/stb_image.h"
#include "stbimage/stb_image_resize.h"

export module Inkeys.UI.Setting:Base;

import Inkeys.Helper.Enum;

using Microsoft::WRL::ComPtr;

WNDCLASSEXW ImGuiWc;
ImFont* ImFontMain = nullptr;

struct SettingImageStruct
{
	int width, height;
	ComPtr<ID3D11ShaderResourceView> img;
};
enum class ImgEnum
{
	Home1,
	Home2,
	PluginFlag1,
	PluginFlag2,
	PluginFlag3,
	PluginFlag4,
};
InkeysEnumArray <ImgEnum, SettingImageStruct> Img;

export float settingGlobalScale = 1.0f;

// Data
ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
bool g_SwapChainOccluded = false;
UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// 基础封装（历史遗留）
void HelpMarker(const char* desc, ImVec4 tmp)
{
	if (!ImFontMain) return;
	ImFontMain->Scale = 0.45f;
	ImGui::PushFont(ImFontMain);
	ImGui::TextColored(ImVec4(13 / 255.0f, 83 / 255.0f, 255 / 255.0f, 1.0f), "\ue90a");
	ImGui::PopFont();

	ImFontMain->Scale = 0.7f;
	ImGui::PushFont(ImFontMain);
	if (ImGui::IsItemHovered())
	{
		ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(236, 241, 255, 200));
		ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(175, 197, 255, 255));
		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(13, 83, 255, 255));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);

		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
		ImGui::TextUnformatted(desc);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();

		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar(1);
	}
	ImGui::PopFont();
}
void CenteredText(const char* desc, float displacement)
{
	float temp = ImGui::GetCursorPosY();
	ImGui::SetCursorPosY(temp + displacement);
	ImGui::TextUnformatted(desc);
	ImGui::SetCursorPosY(temp);
}
void ScrollWhenDraggingOnVoid(const ImVec2& delta, ImGuiMouseButton mouse_button)
{
	ImGuiContext& g = *ImGui::GetCurrentContext();
	ImGuiWindow* window = g.CurrentWindow;
	bool hovered = false;
	bool held = false;
	ImGuiID id = window->GetID("##scrolldraggingoverlay");
	ImGui::KeepAliveID(id);

	ImGui::ButtonBehavior(window->Rect(), id, &hovered, &held, mouse_button);
	if (!held && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseDown(mouse_button))
		held = true;

	if (held && delta.x != 0.0f) ImGui::SetScrollX(window, window->Scroll.x + delta.x);
	if (held && delta.y != 0.0f) ImGui::SetScrollY(window, window->Scroll.y + delta.y);
}
bool LoadTextureFromMemory(const void* data, size_t data_size, ID3D11ShaderResourceView** out_srv, int* out_width, int* out_height)
{
	// Load from disk into a raw RGBA buffer
	int image_width = 0;
	int image_height = 0;
	unsigned char* image_data = stbi_load_from_memory((const unsigned char*)data, (int)data_size, &image_width, &image_height, NULL, 4);
	if (image_data == NULL)
		return false;

	// Create texture
	D3D11_TEXTURE2D_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
	desc.Width = image_width;
	desc.Height = image_height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = 0;

	ID3D11Texture2D* pTexture = NULL;
	D3D11_SUBRESOURCE_DATA subResource;
	subResource.pSysMem = image_data;
	subResource.SysMemPitch = desc.Width * 4;
	subResource.SysMemSlicePitch = 0;
	g_pd3dDevice->CreateTexture2D(&desc, &subResource, &pTexture);

	// Create texture view
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	ZeroMemory(&srvDesc, sizeof(srvDesc));
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = desc.MipLevels;
	srvDesc.Texture2D.MostDetailedMip = 0;
	g_pd3dDevice->CreateShaderResourceView(pTexture, &srvDesc, out_srv);
	pTexture->Release();

	*out_width = image_width;
	*out_height = image_height;
	stbi_image_free(image_data);

	return true;
}
bool LoadTextureFromMemory(const void* data, size_t data_size, ID3D11ShaderResourceView** out_srv, int target_width, int target_height)
{
	// 1. 加载原始图像
	int original_width = 0;
	int original_height = 0;
	unsigned char* original_data = stbi_load_from_memory((const unsigned char*)data, (int)data_size, &original_width, &original_height, NULL, 4);
	if (original_data == NULL)
		return false;

	// 2. 准备拉伸后的缓冲区
	// 我们直接根据传入的 target_width/height 分配内存
	unsigned char* resized_data = (unsigned char*)malloc(target_width * target_height * 4);
	if (resized_data == NULL) {
		stbi_image_free(original_data);
		return false;
	}

	// 3. 执行拉伸 (非等比例拉伸，强制填充目标宽高)
	stbir_resize_uint8(original_data, original_width, original_height, 0,
		resized_data, target_width, target_height, 0, 4);

	// 4. 创建 D3D11 纹理，使用拉伸后的数据和尺寸
	D3D11_TEXTURE2D_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
	desc.Width = target_width;      // 使用目标宽度
	desc.Height = target_height;    // 使用目标高度
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = 0;

	ID3D11Texture2D* pTexture = NULL;
	D3D11_SUBRESOURCE_DATA subResource;
	subResource.pSysMem = resized_data; // 使用拉伸后的内存
	subResource.SysMemPitch = desc.Width * 4;
	subResource.SysMemSlicePitch = 0;

	HRESULT hr = g_pd3dDevice->CreateTexture2D(&desc, &subResource, &pTexture);
	if (FAILED(hr)) {
		free(resized_data);
		stbi_image_free(original_data);
		return false;
	}

	// 5. 创建视图
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	ZeroMemory(&srvDesc, sizeof(srvDesc));
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = desc.MipLevels;
	srvDesc.Texture2D.MostDetailedMip = 0;

	g_pd3dDevice->CreateShaderResourceView(pTexture, &srvDesc, out_srv);

	// 6. 清理临时资源
	pTexture->Release();
	free(resized_data);
	stbi_image_free(original_data);

	return true;
}