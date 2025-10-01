#include "IdtD2DPreparation.h"

CComPtr<ID2D1Factory1> d2dFactory1;

CComPtr<IDWriteFactory1> dWriteFactory1;
CComPtr<IDWriteFontCollection> dWriteFontCollection;

CComPtr<ID3D11Device> d3dDevice_WARP;
CComPtr<ID2D1Device> d2dDevice_WARP;

void D2DStarup()
{
	// 创建 D2D1.1 工厂
	ID2D1Factory1* tmpFactory = nullptr;
	D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory1), NULL, (IID_PPV_ARGS(&tmpFactory)));
	d2dFactory1.Attach(tmpFactory);

	IDWriteFactory1* tmpWriteFactory = nullptr;
	DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory1), reinterpret_cast<IUnknown**>(&tmpWriteFactory));
	dWriteFactory1.Attach(tmpWriteFactory);

	// 初始化 DC
	{
		// 创建 WARP 设备

		UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

		D3D_FEATURE_LEVEL featureLevels[] = {
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
		};

		D3D11CreateDevice(
			nullptr,                    // 指定 nullptr 使用默认适配器
			D3D_DRIVER_TYPE_WARP,       // **关键：使用 WARP 软件渲染器**
			nullptr,                    // 没有软件模块
			creationFlags,              // 设置支持 BGRA 格式
			featureLevels,              // 功能级别数组
			ARRAYSIZE(featureLevels),   // 数组大小
			D3D11_SDK_VERSION,          // SDK 版本
			&d3dDevice_WARP,            // 返回创建的设备
			nullptr,                    // 返回实际的功能级别
			nullptr                     // 返回设备上下文 (我们不需要)
		);

		CComPtr<IDXGIDevice> dxgiDevice;
		d3dDevice_WARP.QueryInterface(&dxgiDevice);

		d2dFactory1->CreateDevice(dxgiDevice, &d2dDevice_WARP);
	}
}
void D2DShutdown()
{
	dWriteFontCollection.Release();
	dWriteFactory1.Release();
	d2dFactory1.Release();
}