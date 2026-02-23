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

module Inkeys.Rendering;

using Microsoft::WRL::ComPtr;

namespace Inkeys::UI
{
	void ResetRescources(unsigned int width, unsigned int height)
	{
		if (width == 0) width = 1;
		if (height == 0) height = 1;

		uiSet.width = width;
		uiSet.height = height;

	#pragma region 清理

		if (uiSet.d2dDeviceContext) uiSet.d2dDeviceContext->SetTarget(nullptr);

		uiSet.imGuiRTV.Reset();
		uiSet.d2DTargetBitmap.Reset();
		uiSet.gdiInterop.Reset();
		uiSet.backTexture2D.Reset();

	#pragma endregion

	#pragma region 创建纹理

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		desc.MiscFlags = D3D11_RESOURCE_MISC_GDI_COMPATIBLE; // 必须保持 GDI 兼容

		uiSet.d3dDeviceWARP->CreateTexture2D(&desc, nullptr, &uiSet.backTexture2D);

	#pragma endregion

	#pragma region 创建 Imgui RTV

		uiSet.d3dDeviceWARP->CreateRenderTargetView(uiSet.backTexture2D.Get(), nullptr, &uiSet.imGuiRTV);

	#pragma endregion

	#pragma region 创建 D2D Target Bitmap

		ComPtr<IDXGISurface> dxgiSurface;
		uiSet.backTexture2D->QueryInterface(__uuidof(IDXGISurface), &dxgiSurface);

		D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
			D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE,
			D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
		);

		uiSet.d2dDeviceContext->CreateBitmapFromDxgiSurface(dxgiSurface, &bitmapProperties, &uiSet.d2DTargetBitmap);

	#pragma endregion

	#pragma region 获取 GDI 接口并设置目标

		uiSet.d2dDeviceContext->SetTarget(uiSet.d2DTargetBitmap);
		uiSet.d2dDeviceContext->QueryInterface(__uuidof(ID2D1GdiInteropRenderTarget), &uiSet.gdiInterop);

	#pragma endregion
	}

	void StartUp(unsigned int width, unsigned int height)
	{
	#pragma region 创建 D2D1.1 工厂

		D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory1), NULL, (IID_PPV_ARGS(&uiSet.d2dFactory1)));

		// 创建 DWrite 工厂：后面渲染文字时使用
		DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory1), &uiSet.dWriteFactory1);

	#pragma endregion

	#pragma region 初始化 UI 层设备

		UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

		D3D_FEATURE_LEVEL featureLevels[] =
		{
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
		};

		D3D11CreateDevice(
			nullptr,                    // 指定 nullptr 使用默认适配器
			D3D_DRIVER_TYPE_WARP,       // 使用 WARP 软件渲染器
			nullptr,                    // 没有软件模块
			creationFlags,              // 设置支持 BGRA 格式
			featureLevels,              // 功能级别数组
			ARRAYSIZE(featureLevels),   // 数组大小
			D3D11_SDK_VERSION,          // SDK 版本
			&uiSet.d3dDeviceWARP,       // 返回创建的设备
			nullptr,                    // 返回实际的功能级别
			&uiSet.d3dDeviceContext     // 返回设备上下文
		);

		uiSet.d3dDeviceWARP->QueryInterface(__uuidof(IDXGIDevice1), &uiSet.dxgiDevice1WARP);

		uiSet.d2dFactory1->CreateDevice(dxgiDevice1, &uiSet.d2dDeviceWARP);
		uiSet.d2dDeviceWARP->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &uiSet.d2dDeviceContext);

		uiSet.d2dDeviceContext->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

	#pragma endregion

		// 设置背景资源
		ResetRescources(width, height);
	}
}