module;

#include "Setting.Wrap.h"

export module Inkeys.UI.Setting:Base;

ImFont* ImFontMain = nullptr;
struct SettingSignStruct
{
	int width;
	int height;
};
SettingSignStruct settingSign[11];
ID3D11ShaderResourceView* TextureSettingSign[11];

export float settingGlobalScale = 1.0f;

// Data
ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
bool g_SwapChainOccluded = false;
UINT g_ResizeWidth = 0, g_ResizeHeight = 0;

// Forward declarations of helper functions
void CleanupDeviceD3D();

bool CreateRenderTarget()
{
	ID3D11Texture2D* backBuffer = nullptr;
	if (FAILED(g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))))
		return false;

	// RTV 仅持有交换链后缓冲，resize 前必须先释放。
	const HRESULT hr = g_pd3dDevice->CreateRenderTargetView(backBuffer, nullptr, &g_mainRenderTargetView);
	backBuffer->Release();
	return SUCCEEDED(hr);
}

void CleanupRenderTarget()
{
	if (g_pd3dDeviceContext)
		g_pd3dDeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
	if (g_mainRenderTargetView)
	{
		g_mainRenderTargetView->Release();
		g_mainRenderTargetView = nullptr;
	}
}

bool CreateDeviceD3D(HWND hWnd)
{
	DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
	swapChainDesc.BufferCount = 2;
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.OutputWindow = hWnd;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.Windowed = TRUE;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	const D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
	D3D_FEATURE_LEVEL createdFeatureLevel = D3D_FEATURE_LEVEL_11_0;
	const HRESULT hr = D3D11CreateDeviceAndSwapChain(
		nullptr,
		D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
		D3D11_CREATE_DEVICE_BGRA_SUPPORT,
		featureLevels,
		static_cast<UINT>(std::size(featureLevels)),
		D3D11_SDK_VERSION,
		&swapChainDesc,
		&g_pSwapChain,
		&g_pd3dDevice,
		&createdFeatureLevel,
		&g_pd3dDeviceContext);
	if (FAILED(hr))
		return false;

	// 设置窗口独占其 D3D11 设备，不复用进程级 D2D/WARP 设备。
	if (!CreateRenderTarget())
	{
		CleanupDeviceD3D();
		return false;
	}
	return true;
}

void CleanupDeviceD3D()
{
	CleanupRenderTarget();
	if (g_pd3dDeviceContext)
		g_pd3dDeviceContext->ClearState();
	if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
	if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
	if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
	g_SwapChainOccluded = false;
	g_ResizeWidth = g_ResizeHeight = 0;
}

bool ResizeSwapChain(UINT width, UINT height)
{
	CleanupRenderTarget();
	if (FAILED(g_pSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0)))
		return false;
	return CreateRenderTarget();
}

void CleanupSettingTextures()
{
	// 设置图片 SRV 必须先于所属 device 释放。
	for (ID3D11ShaderResourceView*& textureView : TextureSettingSign)
	{
		if (textureView)
		{
			textureView->Release();
			textureView = nullptr;
		}
	}
}

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
bool LoadTextureFromMemory(const unsigned char* image_data, int width, int height, ID3D11ShaderResourceView** out_texture)
{
	if (!image_data || width <= 0 || height <= 0 || !out_texture || !g_pd3dDevice)
		return false;

	*out_texture = nullptr;
	D3D11_TEXTURE2D_DESC textureDesc = {};
	textureDesc.Width = static_cast<UINT>(width);
	textureDesc.Height = static_cast<UINT>(height);
	textureDesc.MipLevels = 1;
	textureDesc.ArraySize = 1;
	// EasyX 图像缓冲为 BGRA，格式必须与原始字节顺序一致。
	textureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.Usage = D3D11_USAGE_IMMUTABLE;
	textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA initialData = {};
	initialData.pSysMem = image_data;
	initialData.SysMemPitch = static_cast<UINT>(width * 4);

	ID3D11Texture2D* texture = nullptr;
	if (FAILED(g_pd3dDevice->CreateTexture2D(&textureDesc, &initialData, &texture)))
		return false;

	D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
	viewDesc.Format = textureDesc.Format;
	viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	viewDesc.Texture2D.MipLevels = 1;
	const HRESULT hr = g_pd3dDevice->CreateShaderResourceView(texture, &viewDesc, out_texture);
	// SRV 成功后会持有 texture 引用，临时 texture 可立即释放。
	texture->Release();
	if (FAILED(hr))
	{
		*out_texture = nullptr;
		return false;
	}
	return true;
}
