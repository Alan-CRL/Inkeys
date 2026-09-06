module;

#include "Setting.Wrap.h"
#include "Setting.Layout.h"

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
SettingSignStruct settingSign[11];
ID3D11ShaderResourceView* TextureSettingSign[11];
std::array<std::vector<unsigned char>, 11> settingTexturePixels;

export float settingGlobalScale = 1.0f;
export float settingUserScale = 1.0f;
export float settingSystemDpiScale = 1.0f;

// Data
ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
Microsoft::WRL::ComPtr<ID3D11Device> g_settingDeviceLease;
Microsoft::WRL::ComPtr<ID3D11DeviceContext> g_settingContextLease;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
bool g_SwapChainOccluded = false;

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
	CleanupRenderTarget();
	const HRESULT resizeResult = g_pSwapChain->ResizeBuffers(
		0, width, height, DXGI_FORMAT_UNKNOWN, 0);
	if (FAILED(resizeResult)) return resizeResult;
	return CreateRenderTarget() ? S_OK : E_FAIL;
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

	*out_texture = nullptr;
	for (std::size_t textureIndex = 0; textureIndex < std::size(TextureSettingSign);
		++textureIndex)
	{
		if (out_texture != &TextureSettingSign[textureIndex]) continue;
		settingSign[textureIndex] = { width, height };
		if (settingTexturePixels[textureIndex].data() != image_data)
		{
			settingTexturePixels[textureIndex].assign(image_data,
				image_data + static_cast<std::size_t>(width) * height * 4);
		}
		break;
	}
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

bool RecreateSettingTextures()
{
	CleanupSettingTextures();
	for (std::size_t index = 0; index < settingTexturePixels.size(); ++index)
	{
		const auto& pixels = settingTexturePixels[index];
		if (pixels.empty()) continue;
		if (!LoadTextureFromMemory(pixels.data(), settingSign[index].width,
			settingSign[index].height, &TextureSettingSign[index]))
			return false;
	}
	return true;
}

void CleanupSettingTextureCache()
{
	for (auto& pixels : settingTexturePixels) pixels.clear();
}
