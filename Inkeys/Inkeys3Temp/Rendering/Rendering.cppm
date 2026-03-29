module;

#include "../../IdtMain.h"

#include <wrl/client.h>

#include <d3d11_1.h>
#include <d2d1_1.h>
#include <dwrite_1.h>
#include <dxgi1_2.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dxgi.lib")

export module Inkeys.Rendering;

using Microsoft::WRL::ComPtr;

// 老旧遗留
template <class T>
void DxObjectSafeRelease(T** ppT)
{
	if (*ppT)
	{
		(*ppT)->Release();
		*ppT = NULL;
	}
}

// UI 层
namespace Inkeys::UI
{
	class UISetClass
	{
	public:
		// UI 层设备
		ComPtr<ID3D11Device> d3dDeviceWARP;
		ComPtr<ID2D1Device> d2dDeviceWARP;
		ComPtr<IDXGIDevice1> dxgiDevice1WARP;

		// UI 层设备上下文
		ComPtr<ID2D1DeviceContext> d2dDeviceContext;
		ComPtr<ID3D11DeviceContext> d3dDeviceContext;

		// UI 层工厂
		ComPtr<ID2D1Factory1> d2dFactory1;
		ComPtr<IDWriteFactory1> dWriteFactory1;
		ComPtr<IDWriteFontCollection> dWriteFontCollection;

		// UI 层资源
		ComPtr<ID3D11Texture2D> backTexture2D;
		ComPtr<ID3D11RenderTargetView> imGuiRTV;
		ComPtr<ID2D1GdiInteropRenderTarget> gdiInterop;
		ComPtr<ID2D1Bitmap1> d2DTargetBitmap;

		// 相关尺寸
		unsigned int width = 0;
		unsigned int height = 0;
	} uiSet;

	export void ResetRescources(unsigned int width = 0, unsigned int height = 0);
	export void StartUp(unsigned int width, unsigned int height);
}