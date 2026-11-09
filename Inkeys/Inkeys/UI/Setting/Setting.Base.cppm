module;

#include "Setting.Wrap.h"
#include "Setting.Layout.h"
#include "Setting.SessionState.h"

export module Inkeys.UI.Setting:Base;

import Inkeys.UI.RenderPipeline;

ImFont* ImFontMain = nullptr;
ImFont* ImFontStrong = nullptr;
// 新页面使用独立校准字体；旧页保留原字号/行步长直至逐页迁移。
ImFont* ImFontDesignMain = nullptr;
ImFont* ImFontDesignStrong = nullptr;
ImFont* ImFontDesignIcons = nullptr;
struct SettingSignStruct
{
	int width;
	int height;
};
SettingSignStruct settingSign[14];
ID3D11ShaderResourceView* TextureSettingSign[14];
// 仅运行时窗口图标保留小幅原始像素；PNG 资源只记录可重建配方。
std::array<std::vector<unsigned char>, 14> settingTexturePixels;
enum class SettingTextureConversion { StraightBgra, WhiteAlphaMask };
struct SettingTextureRecipe
{
	LPCWSTR resourceName = nullptr;
	int requestedWidth = 0, requestedHeight = 0;
	int expectedWidth = 0, expectedHeight = 0;
	SettingTextureConversion conversion = SettingTextureConversion::StraightBgra;
};
std::array<SettingTextureRecipe, 14> settingTextureRecipes;
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
int settingTextureTestFailSlot = -1;
bool settingTextureTestFailView = false;
#endif

export float settingGlobalScale = 1.0f;

// Data
ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
Microsoft::WRL::ComPtr<ID3D11Device> g_settingDeviceLease;
Microsoft::WRL::ComPtr<ID3D11DeviceContext> g_settingContextLease;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
bool g_SwapChainOccluded = false;
UINT g_settingPresentationWidth = 0, g_settingPresentationHeight = 0;

// Forward declarations of helper functions
void CleanupDeviceD3D();

bool CreateRenderTarget()
{
	ID3D11Texture2D* backBuffer = nullptr;
	if (FAILED(g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))))
		return false;

	// RTV 仅持有交换链后缓冲，resize 前必须先释放。
	const HRESULT hr = g_pd3dDevice->CreateRenderTargetView(backBuffer, nullptr, &g_mainRenderTargetView);
	if (SUCCEEDED(hr))
	{
		D3D11_TEXTURE2D_DESC description{};
		backBuffer->GetDesc(&description);
		g_settingPresentationWidth = description.Width;
		g_settingPresentationHeight = description.Height;
	}
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

bool AcquireDeviceLease(const Inkeys::UI::RenderPipeline::DeviceEpoch& epoch)
{
	if (!epoch.d3dDevice || !epoch.immediateContext)
		return false;
	g_settingDeviceLease = epoch.d3dDevice;
	g_settingContextLease = epoch.immediateContext;
	g_pd3dDevice = g_settingDeviceLease.Get();
	g_pd3dDeviceContext = g_settingContextLease.Get();
	return true;
}

bool CreatePresentation(HWND hWnd,
	const Inkeys::UI::RenderPipeline::DeviceEpoch& epoch)
{
	if (!hWnd || !g_pd3dDevice || !epoch.dxgiFactory || g_pSwapChain)
		return false;

	DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
	swapChainDesc.BufferCount = 2;
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.OutputWindow = hWnd;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.Windowed = TRUE;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	// Setting 只创建独立呈现资源，device/context 由唯一渲染管线借用。
	const HRESULT hr = epoch.dxgiFactory->CreateSwapChain(
		g_pd3dDevice, &swapChainDesc, &g_pSwapChain);
	if (FAILED(hr))
		return false;

	if (!CreateRenderTarget())
	{
		if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
		return false;
	}
	return true;
}

void CleanupPresentation()
{
	CleanupRenderTarget();
	if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
	g_SwapChainOccluded = false;
	g_settingPresentationWidth = g_settingPresentationHeight = 0;
}

void CleanupDeviceD3D()
{
	CleanupPresentation();
	g_pd3dDeviceContext = nullptr;
	g_pd3dDevice = nullptr;
	g_settingContextLease.Reset();
	g_settingDeviceLease.Reset();
}

HRESULT ResizeSwapChain(UINT width, UINT height)
{
	// 自动匹配 HWND 的首次交换链可能已是目标尺寸；保留 RTV 可避免无意义的同步重建。
	// RTV 缺失时不能跳过，失败后的同尺寸请求仍须执行原恢复路径。
	return Inkeys::UI::Setting::ResizeSettingPresentation(width, height,
		g_settingPresentationWidth, g_settingPresentationHeight, g_mainRenderTargetView != nullptr,
		[] { CleanupRenderTarget(); },
		[=] { return g_pSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0); },
		[] { return CreateRenderTarget(); });
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

bool LoadTextureFromMemory(const unsigned char* image_data, int width, int height, ID3D11ShaderResourceView** out_texture)
{
	if (!image_data || width <= 0 || height <= 0 || !out_texture || !g_pd3dDevice)
		return false;

	std::size_t textureIndex = std::size(TextureSettingSign);
	for (std::size_t index = 0; index < std::size(TextureSettingSign); ++index)
		if (out_texture == &TextureSettingSign[index]) { textureIndex = index; break; }
	std::vector<unsigned char> iconPixels;
	if (textureIndex == 0 && settingTexturePixels[0].data() != image_data)
	{
		try { iconPixels.assign(image_data, image_data + static_cast<std::size_t>(width) * height * 4); }
		catch (...) { return false; }
	}
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
	if (settingTextureTestFailSlot == static_cast<int>(textureIndex) && !settingTextureTestFailView)
		return false;
#endif
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
	ID3D11ShaderResourceView* candidate = nullptr;
#if INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
	const HRESULT hr = settingTextureTestFailSlot == static_cast<int>(textureIndex) && settingTextureTestFailView
		? E_OUTOFMEMORY : g_pd3dDevice->CreateShaderResourceView(texture, &viewDesc, &candidate);
#else
	const HRESULT hr = g_pd3dDevice->CreateShaderResourceView(texture, &viewDesc, &candidate);
#endif
	// 两阶段成功后才发布 SRV、尺寸和图标备份，失败不留下半套恢复状态。
	texture->Release();
	if (FAILED(hr)) return false;
	if (*out_texture) (*out_texture)->Release();
	*out_texture = candidate;
	if (textureIndex < std::size(TextureSettingSign))
	{
		settingSign[textureIndex] = { width, height };
		if (textureIndex == 0 && !iconPixels.empty()) settingTexturePixels[0].swap(iconPixels);
		else if (textureIndex != 0) std::vector<unsigned char>().swap(settingTexturePixels[textureIndex]);
	}
	return true;
}

bool RecreateSettingTextures();

void CleanupSettingTextureCache()
{
	for (auto& pixels : settingTexturePixels) std::vector<unsigned char>().swap(pixels);
	settingTextureRecipes = {};
}
